#include "cs/VocalBridge.h"
#include "cs/Processor.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>

namespace cs
{
namespace { std::atomic<int> gLiveInstruments { 0 }; }
Instrument::Instrument() { gLiveInstruments.fetch_add (1, std::memory_order_relaxed); }
Instrument::~Instrument() { gLiveInstruments.fetch_sub (1, std::memory_order_relaxed); }
int Instrument::liveInstances() { return gLiveInstruments.load (std::memory_order_relaxed); }

std::string AudioProcessor::saveState() const
{
    nlohmann::json j = nlohmann::json::object();
    const auto ps = parameters();
    for (int i = 0; i < (int) ps.size(); ++i) j[ps[(size_t) i].id] = getParameter (i);
    return j.dump();
}

bool AudioProcessor::loadState (const std::string& s)
{
    const auto j = nlohmann::json::parse (s, nullptr, false);
    if (j.is_discarded() || ! j.is_object()) return false;
    const auto ps = parameters();
    std::vector<float> values;
    for (const auto& p : ps)
    {
        float v = p.defaultValue;
        if (j.contains (p.id))
        {
            if (! j[p.id].is_number()) return false;
            v = j[p.id].get<float>();
            if (! std::isfinite (v) || v < p.minValue || v > p.maxValue) return false;
        }
        values.push_back (v);
    }
    for (int i = 0; i < (int) values.size(); ++i) setParameter (i, values[(size_t) i]);
    return true;
}

namespace
{
// Procesador integrado mínimo: ganancia en dB con suavizado. Sirve para validar la cadena de inserciones
// (tiempo real, estado, persistencia) antes de integrar los motores de los plugins.
class GainProcessor final : public AudioProcessor
{
public:
    std::string typeId() const override { return "cs.gain"; }
    void prepare (double sr, int) override { step = (float) (1.0 / (0.010 * sr)); reset(); }
    void reset() override { current = target(); }
    void process (float* const* io, int numChannels, int numFrames) override
    {
        const float t = target();
        for (int i = 0; i < numFrames; ++i)
        {
            if (current < t) current = std::min (current + step, t);
            else if (current > t) current = std::max (current - step, t);
            for (int c = 0; c < numChannels; ++c) io[c][i] *= current;
        }
    }
    std::vector<ParameterInfo> parameters() const override { return { { "gainDb", "Gain", -60.0f, 12.0f, 0.0f } }; }
    // Valores no finitos se ignoran (antes se guardaban como null en el proyecto: detectado por fx_all_effects...).
    void setParameter (int index, float v) override { if (index == 0 && std::isfinite (v)) gainDb.store (std::clamp (v, -60.0f, 12.0f)); }
    float getParameter (int index) const override { return index == 0 ? gainDb.load() : 0.0f; }

private:
    float target() const { return std::pow (10.0f, gainDb.load() / 20.0f); }
    std::atomic<float> gainDb { 0.0f };
    float current = 1.0f, step = 0.001f;
};
} // namespace

ProcessorPtr createSynth();
ProcessorPtr createDrums();
ProcessorPtr createSampler(); // SamplerInstrument.cpp (Cauce Sampler, 0.9)
ProcessorPtr createEffect (const std::string& typeId); // Effects.cpp (Cauce FX)

ProcessorPtr createProcessor (const std::string& typeId)
{
    if (typeId == "cs.gain") return std::make_shared<GainProcessor>();
    if (typeId == "cs.synth") return createSynth();
    if (typeId == "cs.drums") return createDrums();
    if (typeId == "cs.sampler") return createSampler();
    // 0.9.2: prueba de concepto oculta (no está en effectTypes(), así que ni el catálogo ni la interfaz la ofrecen).
    if (typeId == "cs.vocalshift.live") return vocal::createLiveInsertPoC();
    return createEffect (typeId);
}

std::vector<std::string> availableProcessorTypes()
{
    auto v = effectTypes();
    v.push_back ("cs.synth");
    v.push_back ("cs.drums");
    v.push_back ("cs.sampler");
    return v;
}
} // namespace cs
