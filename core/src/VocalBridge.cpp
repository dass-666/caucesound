#include "cs/VocalBridge.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

#ifdef CS_WITH_VOCALSHIFT
#include "core/VocalChain.h"
#include "core/state/FactoryPresets.h"
#endif

namespace cs::vocal
{
using nlohmann::json;

const char* pinnedVersion() { return "vocalshift-ai-studio@10e679c"; }

std::string Params::toJson() const
{
    return json { { "preset", preset }, { "semitones", semitones }, { "character", character }, { "magic", magic },
                  { "secondVoice", secondVoice }, { "secondGainDb", secondGainDb }, { "radical", radical }, { "spatial", spatial },
                  { "formantOffset", formantOffset }, { "brightnessOffset", brightnessOffset }, { "mixPercent", mixPercent },
                  { "pan", pan }, { "offsetMs", offsetMs } }.dump();
}

bool Params::fromJson (const std::string& text, Params& out)
{
    const json j = json::parse (text, nullptr, false);
    if (j.is_discarded() || ! j.is_object()) return false;
    Params p;
    bool ok = true;
    auto num = [&] (const char* k, float& v, float lo, float hi)
    {
        if (! j.contains (k)) return;
        if (! j[k].is_number()) { ok = false; return; }
        const double d = j[k].get<double>();
        if (! std::isfinite (d)) { ok = false; return; }
        v = (float) std::clamp (d, (double) lo, (double) hi);
    };
    auto flag = [&] (const char* k, bool& v) { if (j.contains (k)) { if (j[k].is_boolean()) v = j[k].get<bool>(); else ok = false; } };
    if (j.contains ("preset") && j["preset"].is_string()) p.preset = j["preset"].get<std::string>();
    num ("semitones", p.semitones, -24, 24);
    num ("character", p.character, 0, 100);
    num ("magic", p.magic, 0, 100);
    num ("secondGainDb", p.secondGainDb, -60, 6);
    num ("formantOffset", p.formantOffset, -12, 12);
    num ("brightnessOffset", p.brightnessOffset, -12, 12);
    num ("mixPercent", p.mixPercent, 0, 100);
    num ("pan", p.pan, -100, 100);
    num ("offsetMs", p.offsetMs, 0, 100);
    flag ("secondVoice", p.secondVoice);
    flag ("radical", p.radical);
    flag ("spatial", p.spatial);
    if (ok) out = p;
    return ok;
}

#ifndef CS_WITH_VOCALSHIFT
std::shared_ptr<AudioProcessor> createLiveInsertPoC() { return nullptr; }
bool liveInsertOverloaded (const AudioProcessor*) { return false; }
void setLiveOverloadThreshold (double) {}
std::string benchLiveInsert (double) { return "{\"ok\":false,\"error\":\"VocalShift no está en esta compilación\"}"; }
bool available() { return false; }
std::vector<PresetInfo> presets() { return {}; }
std::string process (const AudioData&, Frame, Frame, const Params&, const Sink&, std::atomic<float>*, const std::atomic<bool>*, Stats*, double)
{
    return "VocalShift no está incluido en esta compilación";
}
#else
bool available() { return true; }

namespace
{
::vs::VoiceParams toVs (const Params& p)
{
    ::vs::VoiceParams v;
    v.mode = p.radical ? ::vs::VoiceMode::radical : ::vs::VoiceMode::keepIdentity;
    v.route = p.secondVoice ? ::vs::VoiceRoute::secondVoice : ::vs::VoiceRoute::transform;
    v.semitones = p.semitones;
    v.character = p.character;
    v.magic = p.magic;
    v.formantOffset = p.formantOffset;
    v.brightnessOffset = p.brightnessOffset;
    v.mixPercent = p.mixPercent;
    v.secondGainDb = p.secondGainDb;
    v.pan = p.pan;
    v.offsetMs = p.offsetMs;
    v.spatial = p.spatial;
    v.improved = true;
    v.autoLevel = true;   // iguala la sonoridad de la voz procesada: la comparación A/B no premia el volumen
    v.safetyClip = true;
    return v;
}

Params fromVs (const ::vs::VoiceParams& v, const std::string& id)
{
    Params p;
    p.preset = id;
    p.semitones = v.semitones + v.fineCents / 100.0f;
    p.character = v.character;
    p.magic = v.magic;
    p.secondVoice = v.route == ::vs::VoiceRoute::secondVoice;
    p.secondGainDb = v.secondGainDb;
    p.radical = v.mode == ::vs::VoiceMode::radical;
    p.spatial = v.spatial;
    p.formantOffset = v.formantOffset;
    p.brightnessOffset = v.brightnessOffset;
    p.mixPercent = v.mixPercent;
    p.pan = v.pan;
    p.offsetMs = v.offsetMs;
    return p;
}
} // namespace

std::vector<PresetInfo> presets()
{
    std::vector<PresetInfo> out;
    for (const auto& f : ::vs::factoryPresets()) out.push_back ({ f.id, f.name, f.description, fromVs (f.params, f.id) });
    return out;
}

std::string process (const AudioData& in, Frame from, Frame length, const Params& p, const Sink& sink,
                     std::atomic<float>* progress, const std::atomic<bool>* cancel, Stats* stats, double tempoBpm)
{
    const int inCh = in.numChannels();
    if (inCh < 1 || in.sampleRate < 8000) return "audio no válido";
    if (from < 0 || length <= 0 || from + length > in.numFrames()) return "rango fuera del audio";
    if ((double) length / in.sampleRate > kMaxClipSeconds) return "la toma es demasiado larga para procesarla (máximo 10 minutos)";
    const auto t0 = std::chrono::steady_clock::now();

    constexpr int block = 512;
    ::vs::VocalChain chain;
    chain.prepare ((double) in.sampleRate, 2, block);
    if (tempoBpm > 0) chain.setTempoBpm (tempoBpm);
    chain.setParams (toVs (p), false);
    // VocalShift suaviza sus parámetros (pensado para mover controles en vivo) y solo los «clava» en prepare().
    // Sin esta segunda llamada, el desfase de la segunda voz arrancaba en 0 y tardaba hasta 3 s en llegar al valor
    // pedido (con una leve desafinación mientras tanto). Offline no hay nada que suavizar: se parte del valor final.
    chain.prepare ((double) in.sampleRate, 2, block);
    if (tempoBpm > 0) chain.setTempoBpm (tempoBpm);
    chain.setParams (toVs (p), false);
    chain.reset();
    chain.setParams (toVs (p), false);
    const int latency = chain.latencySamples();
    const bool hasTail = p.secondVoice && p.spatial && p.magic > 0.0f;
    const Frame maxTail = hasTail ? (Frame) (kMaxTailSeconds * in.sampleRate) : 0;
    const Frame minTail = hasTail ? (Frame) (0.25 * in.sampleRate) : 0;

    std::vector<float> l ((size_t) block), r ((size_t) block), inter ((size_t) block * 2);
    float* io[2] = { l.data(), r.data() };
    const float* srcL = in.channels[0].data() + from;
    const float* srcR = (inCh > 1 ? in.channels[1].data() : in.channels[0].data()) + from;

    Stats st;
    st.latencySamples = latency;
    st.inputFrames = length;
    Frame fed = 0, skipped = 0, written = 0;
    const Frame total = length + latency + maxTail; // máximo de frames que se alimentan
    int quietBlocks = 0;
    double inSq = 0, outSq = 0;
    while (fed < total)
    {
        if (cancel && cancel->load()) return "cancelado";
        const int n = (int) std::min<Frame> (block, total - fed);
        for (int i = 0; i < n; ++i)
        {
            const Frame f = fed + i;
            const bool inside = f < length;
            l[(size_t) i] = inside ? srcL[f] : 0.0f;
            r[(size_t) i] = inside ? srcR[f] : 0.0f;
            if (inside)
            {
                st.inputPeak = std::max ({ st.inputPeak, std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i]) });
                inSq += (double) l[(size_t) i] * l[(size_t) i] + (double) r[(size_t) i] * r[(size_t) i];
            }
        }
        chain.process (io, 2, n);
        fed += n;
        // Compensación de latencia: se descartan los primeros 'latency' frames de salida.
        int start = 0;
        if (skipped < latency) { start = (int) std::min<Frame> (n, latency - skipped); skipped += start; }
        const int out = n - start;
        if (out > 0)
        {
            float peak = 0;
            for (int i = 0; i < out; ++i)
            {
                float a = l[(size_t) (start + i)], b = r[(size_t) (start + i)];
                if (! std::isfinite (a)) a = 0;
                if (! std::isfinite (b)) b = 0;
                inter[(size_t) i * 2] = a;
                inter[(size_t) i * 2 + 1] = b;
                peak = std::max ({ peak, std::fabs (a), std::fabs (b) });
                if (written + i < length) outSq += (double) a * a + (double) b * b;
            }
            st.outputPeak = std::max (st.outputPeak, peak);
            if (! sink (inter.data(), out)) return "no se pudo escribir el resultado (¿almacenamiento lleno?)";
            written += out;
            // Cola: tras el final de la entrada se sigue hasta que la reverb decae por debajo de -72 dBFS.
            if (written > length)
            {
                quietBlocks = peak < 2.5e-4f ? quietBlocks + 1 : 0;
                if (written - length >= minTail && quietBlocks >= 8) break;
            }
            if (! hasTail && written >= length) break;
        }
        if (progress) progress->store ((float) std::min (1.0, (double) fed / (double) (length + latency)) * 0.98f);
    }
    st.outputFrames = written;
    if (inSq > 1e-9 && outSq > 1e-9) st.levelDb = (float) std::clamp (10.0 * std::log10 (outSq / inSq), -24.0, 24.0);
    st.tailFrames = std::max<Frame> (0, written - length);
    st.nonFiniteEvents = chain.nonFiniteEvents();
    st.clipAltered = chain.clipAlteredSamples();
    st.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    if (written < length) return "el motor devolvió menos audio del esperado";
    if (stats) *stats = st;
    return {};
}

// --- 0.9.2: prueba de concepto de inserción en vivo ----------------------------------------------------------------
namespace
{
std::atomic<double> gOverloadThreshold { 0.5 };

class VocalShiftLivePoC final : public AudioProcessor
{
public:
    std::string typeId() const override { return "cs.vocalshift.live"; }
    void prepare (double sr, int maxBlock) override
    {
        // Puede reservar memoria: el motor lo llama en el hilo de control al construir el grafo, nunca en el de audio.
        rate = sr;
        block = std::max (64, maxBlock);
        chain.prepare (sr, 2, block);
        applyParams();
        chain.prepare (sr, 2, block); // «clava» los suavizados en el valor inicial (como offline)
        applyParams();
        chain.reset();
        latency = chain.latencySamples();
        for (auto& d : dry) d.assign ((size_t) std::max (1, latency), 0.0f);
        for (auto& d : dryOut) d.assign ((size_t) block, 0.0f);
        dryPos = 0;
        fadeStep = (float) (1.0 / (0.010 * sr)); // 10 ms
    }
    void reset() override
    {
        chain.reset();
        for (auto& d : dry) std::fill (d.begin(), d.end(), 0.0f);
        dryPos = 0;
        // Volver a pulsar Play (o saltar) rearma la protección: el motor reinicia las inserciones en ese momento.
        ema = 0; hot = 0; wet = 1.0f;
        tripped.store (false, std::memory_order_relaxed);
    }
    int latencySamples() const override { return latency; }
    int tailSamples() const override { return (int) (1.5 * rate); }
    void process (float* const* io, int numChannels, int numFrames) override
    {
        for (int done = 0; done < numFrames; done += block)
        {
            const int n = std::min (block, numFrames - done);
            float* part[2] = { io[0] + done, (numChannels >= 2 ? io[1] : io[0]) + done };
            processBlock (part, numChannels >= 2 ? 2 : 1, n);
        }
    }
    std::vector<ParameterInfo> parameters() const override
    {
        return { { "semitones", "Tono (semitonos)", -12, 12, 7 }, { "character", "Carácter (%)", 0, 100, 65 }, { "magic", "Magia (%)", 0, 100, 70 },
                 { "secondVoice", "Segunda voz", 0, 1, 1 }, { "secondGainDb", "Nivel 2.ª voz (dB)", -60, 6, -12 } };
    }
    void setParameter (int index, float v) override
    {
        if (index < 0 || index >= 5 || ! std::isfinite (v)) return;
        const auto info = parameters()[(size_t) index];
        values[index].store (std::clamp (v, info.minValue, info.maxValue));
        dirty.store (true, std::memory_order_release);
    }
    float getParameter (int index) const override { return index >= 0 && index < 5 ? values[index].load() : 0.0f; }
    bool overloaded() const { return tripped.load (std::memory_order_relaxed); }
    long overloads() const { return trips.load (std::memory_order_relaxed); }

private:
    // Protección contra sobrecarga (0.10): si el efecto, él solo, gasta de forma sostenida más de la mitad del tiempo
    // real, deja de procesar y pasa la señal SECA con el mismo retardo (la pista sigue alineada por el PDC), con un
    // fundido de 10 ms para que el cambio no haga clic. Se rearma al volver a pulsar Play. Es preferible perder el
    // efecto a que toda la canción se entrecorte.
    void processBlock (float* const* io, int ch, int n)
    {
        if (dirty.exchange (false, std::memory_order_acq_rel)) applyParams(); // tiempo real: solo copia ajustes
        // Línea de retardo de la señal seca (siempre al día, cuesta una copia).
        const int len = (int) dry[0].size();
        for (int c = 0; c < 2; ++c)
        {
            const float* in = io[c < ch ? c : 0];
            int p = dryPos;
            for (int i = 0; i < n; ++i) { dryOut[(size_t) c][(size_t) i] = dry[(size_t) c][(size_t) p]; dry[(size_t) c][(size_t) p] = in[i]; if (++p >= len) p = 0; }
        }
        dryPos = (dryPos + n) % len;
        const bool off = tripped.load (std::memory_order_relaxed);
        if (! off || wet > 0.0f)
        {
            const auto t0 = std::chrono::steady_clock::now();
            if (ch >= 2) chain.process (io, 2, n);
            else { float* two[2] = { io[0], io[0] }; chain.process (two, 1, n); }
            const double load = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / ((double) n / rate);
            ema = 0.8 * ema + 0.2 * load;
            hot = ema > gOverloadThreshold.load (std::memory_order_relaxed) ? hot + 1 : 0;
            if (! off && hot >= 8) { tripped.store (true, std::memory_order_relaxed); trips.fetch_add (1, std::memory_order_relaxed); }
        }
        if (! tripped.load (std::memory_order_relaxed) && wet >= 1.0f) return;
        const float target = tripped.load (std::memory_order_relaxed) ? 0.0f : 1.0f;
        for (int i = 0; i < n; ++i)
        {
            wet = target > wet ? std::min (target, wet + fadeStep) : std::max (target, wet - fadeStep);
            for (int c = 0; c < ch; ++c) io[c][i] = wet * io[c][i] + (1.0f - wet) * dryOut[(size_t) c][(size_t) i];
        }
    }
    void applyParams()
    {
        Params p;
        p.semitones = values[0].load(); p.character = values[1].load(); p.magic = values[2].load();
        p.secondVoice = values[3].load() >= 0.5f; p.secondGainDb = values[4].load();
        chain.setParams (toVs (p), false);
    }
    ::vs::VocalChain chain;
    double rate = 48000;
    int latency = 0, block = 1024, dryPos = 0, hot = 0;
    double ema = 0;
    float wet = 1.0f, fadeStep = 0.002f;
    std::vector<float> dry[2], dryOut[2];
    std::atomic<float> values[5] { { 7.0f }, { 65.0f }, { 70.0f }, { 1.0f }, { -12.0f } };
    std::atomic<bool> dirty { false }, tripped { false };
    std::atomic<long> trips { 0 };
};
} // namespace

std::shared_ptr<AudioProcessor> createLiveInsertPoC() { return std::make_shared<VocalShiftLivePoC>(); }
bool liveInsertOverloaded (const AudioProcessor* p) { const auto* v = dynamic_cast<const VocalShiftLivePoC*> (p); return v && v->overloaded(); }
void setLiveOverloadThreshold (double fractionOfRealTime) { gOverloadThreshold.store (fractionOfRealTime); }

// Medición para decidir si el efecto en vivo cabe en un teléfono (se ejecuta en el hilo de control, sin audio): procesa
// 'seconds' de una voz sintética en bloques de 192 frames, como el callback del teléfono, y mide el coste medio, el
// peor bloque y la ráfaga del arranque (lo que cuesta cebar el efecto al pulsar Play).
std::string benchLiveInsert (double seconds)
{
    nlohmann::json out = { { "ok", false } };
    auto fx = createLiveInsertPoC();
    if (! fx) { out["error"] = "VocalShift no está en esta compilación"; return out.dump(); }
    const double sr = 48000.0;
    const int block = 192;
    fx->prepare (sr, 1024);
    seconds = std::clamp (seconds, 1.0, 30.0);
    const int blocks = (int) (seconds * sr / block);
    std::vector<float> l ((size_t) block), r ((size_t) block);
    float* io[2] = { l.data(), r.data() };
    double ph = 0, total = 0, worst = 0;
    long n = 0;
    auto fill = [&] (int count)
    {
        for (int i = 0; i < count; ++i, ++n)
        {
            const double t = (double) n / sr;
            ph += 2.0 * 3.14159265358979 * 196.0 * (1.0 + 0.01 * std::sin (2 * 3.14159265358979 * 5.0 * t)) / sr;
            double v = 0;
            for (int h = 1; h <= 8; ++h) v += std::sin (ph * h) / h;
            l[(size_t) i] = r[(size_t) i] = (float) (0.25 * v * (0.5 - 0.5 * std::cos (2 * 3.14159265358979 * std::fmod (t, 0.5) / 0.5)));
        }
    };
    for (int b = 0; b < blocks; ++b)
    {
        fill (block);
        const auto t0 = std::chrono::steady_clock::now();
        fx->process (io, 2, block);
        const double dt = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        total += dt;
        if (b > 8) worst = std::max (worst, dt);
    }
    // Ráfaga de arranque: reinicio + (calentamiento + latencia) frames seguidos, como Engine::primeLatentTracks.
    const int prime = fx->latencySamples() + std::min (fx->latencySamples(), (int) sr / 4);
    const auto t0 = std::chrono::steady_clock::now();
    fx->reset();
    for (int done = 0; done < prime; done += block) { fill (block); fx->process (io, 2, block); }
    const double burst = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const double blockSec = block / sr;
    out = { { "ok", true }, { "seconds", seconds }, { "latencyMs", 1000.0 * fx->latencySamples() / sr },
            { "cpuPercentOfOneCore", std::round (1000.0 * total / (blocks * blockSec)) / 10.0 },
            { "worstBlockPercent", std::round (1000.0 * worst / blockSec) / 10.0 },
            { "startBurstMs", std::round (10000.0 * burst) / 10.0 }, { "blockMs", std::round (10000.0 * blockSec) / 10.0 },
            { "protectionTripped", liveInsertOverloaded (fx.get()) },
#if defined(__aarch64__)
            { "arch", "arm64" },
#elif defined(__x86_64__)
            { "arch", "x86-64" },
#else
            { "arch", "otra" },
#endif
    };
    return out.dump();
}
#endif
} // namespace cs::vocal
