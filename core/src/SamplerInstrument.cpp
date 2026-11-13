// Instrumento "cs.sampler" (misión 0.9): un instrumento melódico de un paquete Cauce (Cauce Keys, Bass Lab...) en una
// pista de instrumento. El estado guarda el identificador estable del instrumento y la versión del paquete, nunca
// rutas. La carga de muestras ocurre en prepare() (hilo de control); el hilo de audio solo reproduce.
#include "cs/Instruments.h"
#include "cs/Packs.h"
#include "cs/Sampler.h"

#include "Dsp.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cmath>

namespace cs
{
namespace
{
enum P { pVolume, pTranspose, pRelease, pVelSens, pDrive, pTone, pCount };

const std::vector<ParameterInfo>& samplerParams()
{
    static const std::vector<ParameterInfo> ps = {
        { "volumeDb", "Volume (dB)", -40, 6, 0 },
        { "transpose", "Transpose (st)", -24, 24, 0 },
        { "release", "Release", 0.25f, 4, 1 },       // multiplica el release del mapa
        { "velSens", "Velocity", 0, 1, 1 },          // sensibilidad a la fuerza
        { "drive", "Drive", 0, 1, 0 },               // saturación suave (armónicos audibles en altavoces pequeños)
        { "tone", "Tone", -1, 1, 0 },                // inclinación: -1 oscuro .. +1 brillante (shelving a 1,5 kHz)
    };
    return ps;
}

class SamplerInstrument final : public Instrument
{
public:
    SamplerInstrument()
    {
        for (int i = 0; i < pCount; ++i) values[(size_t) i].store (samplerParams()[(size_t) i].defaultValue);
    }
    std::string typeId() const override { return "cs.sampler"; }

    void prepare (double rate, int) override
    {
        sr = rate;
        loadError.clear();
        std::shared_ptr<const SamplerProgram> prog;
        if (! key.empty()) prog = packLibrary().loadInstrument (key, (int) rate, loadError);
        program = prog;
        engine.prepare (prog, rate);
        reset();
    }
    void reset() override
    {
        engine.reset();
        toneL = dsp::Biquad(); toneR = dsp::Biquad();
        lastTone = 1e9f;
        volSmooth = -1.0f;
    }
    std::vector<ParameterInfo> parameters() const override { return samplerParams(); }
    void setParameter (int i, float v) override
    {
        if (i < 0 || i >= pCount || ! std::isfinite (v)) return;
        const auto& p = samplerParams()[(size_t) i];
        values[(size_t) i].store (std::clamp (v, p.minValue, p.maxValue));
    }
    float getParameter (int i) const override { return i >= 0 && i < pCount ? values[(size_t) i].load() : 0.0f; }

    std::string saveState() const override
    {
        auto j = nlohmann::json::parse (AudioProcessor::saveState());
        j["instrument"] = key;
        if (! packVersion.empty()) j["packVersion"] = packVersion;
        return j.dump();
    }
    bool loadState (const std::string& s) override
    {
        const auto j = nlohmann::json::parse (s, nullptr, false);
        if (j.is_discarded() || ! j.is_object() || ! j.contains ("instrument") || ! j["instrument"].is_string()) return false;
        const std::string k = j["instrument"].get<std::string>();
        if (k.find (':') == std::string::npos || k.size() > 140) return false;
        if (! AudioProcessor::loadState (s)) return false;
        key = k;
        packVersion = j.value ("packVersion", std::string());
        return true;
    }

    int activeVoices() const override { return engine.activeVoices(); }
    int tailSamples() const override { return engine.tailSamples(); }

    void handleMidi (const MidiEvent* ev, int count) override
    {
        const float rel = values[pRelease].load (std::memory_order_relaxed);
        const float vs = values[pVelSens].load (std::memory_order_relaxed);
        engine.setModulation (rel, vs);
        const double rateMul = std::pow (2.0, std::round (values[pTranspose].load (std::memory_order_relaxed)) / 12.0);
        for (int i = 0; i < count; ++i)
        {
            const int type = ev[i].status & 0xF0;
            if (type == 0x90 && ev[i].data2 > 0) engine.noteOn (ev[i].data1, ev[i].data2, 0, rateMul);
            else if (type == 0x80 || (type == 0x90 && ev[i].data2 == 0)) engine.noteOff (ev[i].data1);
            else if (type == 0xB0) engine.controlChange (ev[i].data1, ev[i].data2);
        }
    }
    void allNotesOff (bool hard) override { engine.allNotesOff (hard); }

    void process (float* const* io, int numChannels, int n) override
    {
        if (! program) return;
        float* L = io[0];
        float* R = numChannels > 1 ? io[1] : io[0];
        // Las voces se suman a un búfer propio para aplicar volumen, tono y saturación solo a este instrumento.
        for (int off = 0; off < n; off += kChunk)
        {
            const int m = std::min (n - off, kChunk);
            std::fill (bufL.begin(), bufL.begin() + m, 0.0f);
            std::fill (bufR.begin(), bufR.begin() + m, 0.0f);
            engine.process (bufL.data(), bufR.data(), m);
            const float target = dsp::dbToLin (values[pVolume].load (std::memory_order_relaxed));
            if (volSmooth < 0) volSmooth = target;
            const float step = 1.0f / (float) (0.01 * sr);
            const float drive = values[pDrive].load (std::memory_order_relaxed);
            const float tone = values[pTone].load (std::memory_order_relaxed);
            if (tone != lastTone)
            {
                toneL.set (dsp::Biquad::HighShelf, sr, 1500, 0.7, tone * 9.0);
                toneR.set (dsp::Biquad::HighShelf, sr, 1500, 0.7, tone * 9.0);
                lastTone = tone;
            }
            const bool useTone = std::fabs (tone) > 1e-3f;
            const float dg = 1.0f + drive * 6.0f, dn = 1.0f / std::tanh (dg);
            for (int i = 0; i < m; ++i)
            {
                volSmooth += std::clamp (target - volSmooth, -step, step);
                float l = bufL[(size_t) i], r = bufR[(size_t) i];
                if (useTone) { l = toneL.process (l); r = toneR.process (r); }
                if (drive > 0.001f) { l = std::tanh (l * dg) * dn; r = std::tanh (r * dg) * dn; }
                // Protección de salida: transparente por debajo de -2 dBFS, nunca recorta en seco.
                l = dsp::softClip (l * volSmooth);
                r = dsp::softClip (r * volSmooth);
                if (R != L) { L[off + i] += l; R[off + i] += r; }
                else L[off + i] += 0.5f * (l + r);
            }
        }
    }

    const std::string& instrumentKey() const { return key; }
    const std::string& error() const { return loadError; }
    std::shared_ptr<const SamplerProgram> currentProgram() const { return program; }

private:
    static constexpr int kChunk = 256;
    double sr = 48000;
    std::array<std::atomic<float>, pCount> values;
    std::string key, packVersion, loadError;
    std::shared_ptr<const SamplerProgram> program;
    SamplerEngine engine;
    std::array<float, kChunk> bufL {}, bufR {};
    dsp::Biquad toneL, toneR;
    float lastTone = 1e9f, volSmooth = -1.0f;
};
} // namespace

SamplerStatus drumSampleStatus (const AudioProcessor* p); // Drums.cpp

ProcessorPtr createSampler() { return std::make_shared<SamplerInstrument>(); }

SamplerStatus samplerStatus (const AudioProcessor* p)
{
    SamplerStatus st;
    auto* s = dynamic_cast<const SamplerInstrument*> (p);
    if (! s) return drumSampleStatus (p);
    st.key = s->instrumentKey();
    st.error = s->error();
    if (auto prog = s->currentProgram())
    {
        st.loaded = true;
        st.name = prog->name;
        st.bytes = prog->bytes();
        st.regions = (int) prog->regions.size();
        st.missingSamples = (int) prog->missingSamples.size();
    }
    return st;
}
} // namespace cs
