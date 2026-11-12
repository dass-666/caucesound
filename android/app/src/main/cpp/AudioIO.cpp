#include "AudioIO.h"

#include <android/log.h>

#include <cstring>
#include <sstream>

#define LOGI(...) __android_log_print (ANDROID_LOG_INFO, "CauceSound", __VA_ARGS__)
#define LOGW(...) __android_log_print (ANDROID_LOG_WARN, "CauceSound", __VA_ARGS__)

namespace csandroid
{
constexpr int kProjectRate = 48000;

bool AudioIO::openOutputLocked()
{
    oboe::AudioStreamBuilder b;
    b.setDirection (oboe::Direction::Output)
        ->setPerformanceMode (oboe::PerformanceMode::LowLatency)
        ->setSharingMode (oboe::SharingMode::Exclusive)
        ->setFormat (oboe::AudioFormat::Float)
        ->setChannelCount (oboe::ChannelCount::Stereo)
        ->setSampleRate (kProjectRate)
        // Si el hardware no trabaja a 48 kHz, Oboe convierte (añade algo de latencia; se informa en statusJson)
        ->setSampleRateConversionQuality (oboe::SampleRateConversionQuality::Medium)
        ->setUsage (oboe::Usage::Media)
        ->setContentType (oboe::ContentType::Music)
        ->setDataCallback (this)
        ->setErrorCallback (this);
    const oboe::Result r = b.openStream (out);
    if (r != oboe::Result::OK) { LOGW ("salida: %s", oboe::convertToText (r)); out.reset(); return false; }
    // Doble burst: el mínimo estable recomendado para baja latencia.
    out->setBufferSizeInFrames (out->getFramesPerBurst() * 2);
    return true;
}

bool AudioIO::openInputLocked()
{
    oboe::AudioStreamBuilder b;
    b.setDirection (oboe::Direction::Input)
        ->setPerformanceMode (oboe::PerformanceMode::LowLatency)
        ->setSharingMode (oboe::SharingMode::Exclusive)
        ->setFormat (oboe::AudioFormat::Float)
        ->setChannelCount (oboe::ChannelCount::Mono)
        ->setSampleRate (kProjectRate)
        ->setSampleRateConversionQuality (oboe::SampleRateConversionQuality::Medium)
        // VoicePerformance: preset de baja latencia para música/voz en directo (sin cancelación de eco)
        ->setInputPreset (oboe::InputPreset::VoicePerformance);
    oboe::Result r = b.openStream (in);
    if (r != oboe::Result::OK)
    {
        LOGW ("entrada VoicePerformance: %s, reintento con Generic", oboe::convertToText (r));
        b.setInputPreset (oboe::InputPreset::Generic);
        r = b.openStream (in);
    }
    if (r != oboe::Result::OK) { LOGW ("entrada: %s", oboe::convertToText (r)); in.reset(); return false; }
    inBuf.assign ((size_t) std::max (8192, in->getBufferCapacityInFrames() * 2), 0.0f);
    return true;
}

void AudioIO::closeLocked()
{
    inRaw.store (nullptr);
    if (out) { out->stop(); out->close(); out.reset(); }
    if (in) { in->stop(); in->close(); in.reset(); }
}

bool AudioIO::start()
{
    std::lock_guard<std::mutex> l (mtx);
    if (running.load()) return true;
    if (! openOutputLocked()) return false;
    if (wantInput.load() && openInputLocked())
    {
        in->requestStart();
        inRaw.store (in.get());
        primingInput = true;
    }
    if (out->requestStart() != oboe::Result::OK) { closeLocked(); return false; }
    running.store (true);
    LOGI ("audio en marcha: %d Hz, burst %d, API %s", out->getSampleRate(), out->getFramesPerBurst(),
          out->getAudioApi() == oboe::AudioApi::AAudio ? "AAudio" : "OpenSLES");
    return true;
}

void AudioIO::stop()
{
    std::lock_guard<std::mutex> l (mtx);
    running.store (false);
    closeLocked();
}

bool AudioIO::setInputEnabled (bool on)
{
    wantInput.store (on);
    std::lock_guard<std::mutex> l (mtx);
    if (! running.load()) return true; // se aplicará en start()
    if (on && ! in)
    {
        if (! openInputLocked()) return false;
        // Para que entrada y salida arranquen alineadas, reiniciamos la salida junto con la entrada.
        out->stop(); // bloquea hasta que el callback termina
        in->requestStart();
        primingInput = true;
        inRaw.store (in.get());
        out->requestStart();
    }
    else if (! on && in)
    {
        inRaw.store (nullptr);
        // Esperamos a que el callback deje de usar la entrada antes de cerrarla.
        out->stop(); // bloquea hasta que el callback termina
        in->stop(); in->close(); in.reset();
        out->requestStart();
    }
    return true;
}

oboe::DataCallbackResult AudioIO::onAudioReady (oboe::AudioStream* stream, void* audioData, int32_t numFrames)
{
    callbacks.fetch_add (1, std::memory_order_relaxed);
    float* output = static_cast<float*> (audioData);
    const float* input = nullptr;
    oboe::AudioStream* inStream = inRaw.load (std::memory_order_acquire);
    if (inStream && (size_t) numFrames <= inBuf.size())
    {
        if (primingInput)
        {
            // Vaciamos lo acumulado antes del primer callback para no empezar con latencia extra.
            int32_t drained = 0;
            while (true)
            {
                auto r = inStream->read (inBuf.data(), numFrames, 0);
                if (! r || r.value() <= 0 || ++drained > 64) break;
            }
            primingInput = false;
        }
        auto r = inStream->read (inBuf.data(), numFrames, 0); // timeout 0: nunca bloquea
        const int32_t got = r ? r.value() : 0;
        if (got < numFrames)
        {
            std::memset (inBuf.data() + std::max (0, got), 0, sizeof (float) * (size_t) (numFrames - std::max (0, got)));
            inputShortReads.fetch_add (1, std::memory_order_relaxed);
        }
        input = inBuf.data();
    }
    engine.process (input, input ? 1 : 0, output, stream->getChannelCount(), numFrames);
    return oboe::DataCallbackResult::Continue;
}

void AudioIO::onErrorAfterClose (oboe::AudioStream* stream, oboe::Result error)
{
    LOGW ("stream cerrado: %s", oboe::convertToText (error));
    if (error != oboe::Result::ErrorDisconnected) return;
    // Cambio de dispositivo (auriculares desconectados, USB, Bluetooth): el transporte se detiene por seguridad
    // (evita que el audio salte de golpe al altavoz) y se reabre con el nuevo dispositivo por defecto.
    engine.stop();
    std::lock_guard<std::mutex> l (mtx);
    if (! running.load()) return;
    closeLocked();
    reopenCount.fetch_add (1);
    if (openOutputLocked())
    {
        if (wantInput.load() && openInputLocked()) { in->requestStart(); inRaw.store (in.get()); primingInput = true; }
        out->requestStart();
    }
    else
        running.store (false);
}

std::string AudioIO::statusJson()
{
    std::lock_guard<std::mutex> l (mtx);
    std::ostringstream s;
    s << "{\"running\":" << (running.load() ? "true" : "false");
    s << ",\"reopenCount\":" << reopenCount.load() << ",\"inputShortReads\":" << inputShortReads.load();
    s << ",\"callbacks\":" << callbacks.load();
    s << ",\"dspLoadAvg\":" << engine.dspLoadAverage() << ",\"dspLoadPeak\":" << engine.takeDspLoadPeak()
      << ",\"dspOverBudget\":" << engine.blocksOverBudget();
    if (out)
    {
        auto lat = out->calculateLatencyMillis();
        s << ",\"outRate\":" << out->getSampleRate() << ",\"outBurst\":" << out->getFramesPerBurst()
          << ",\"outBuffer\":" << out->getBufferSizeInFrames()
          << ",\"outApi\":\"" << (out->getAudioApi() == oboe::AudioApi::AAudio ? "AAudio" : "OpenSLES") << "\""
          << ",\"outExclusive\":" << (out->getSharingMode() == oboe::SharingMode::Exclusive ? "true" : "false")
          << ",\"outXruns\":" << (out->getXRunCount() ? out->getXRunCount().value() : -1)
          << ",\"outLatencyMs\":" << (lat ? lat.value() : -1.0);
        engine.reportXruns (out->getXRunCount() ? out->getXRunCount().value() : 0);
    }
    if (in)
    {
        auto lat = in->calculateLatencyMillis();
        s << ",\"inRate\":" << in->getSampleRate() << ",\"inBurst\":" << in->getFramesPerBurst()
          << ",\"inLatencyMs\":" << (lat ? lat.value() : -1.0)
          << ",\"inExclusive\":" << (in->getSharingMode() == oboe::SharingMode::Exclusive ? "true" : "false");
    }
    s << "}";
    return s.str();
}
} // namespace csandroid
