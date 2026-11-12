#include "cs/Processor.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cmath>

namespace cs
{
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
    void setParameter (int index, float v) override { if (index == 0) gainDb.store (v); }
    float getParameter (int index) const override { return index == 0 ? gainDb.load() : 0.0f; }

private:
    float target() const { return std::pow (10.0f, gainDb.load() / 20.0f); }
    std::atomic<float> gainDb { 0.0f };
    float current = 1.0f, step = 0.001f;
};
} // namespace

ProcessorPtr createProcessor (const std::string& typeId)
{
    if (typeId == "cs.gain") return std::make_shared<GainProcessor>();
    return nullptr;
}

std::vector<std::string> availableProcessorTypes() { return { "cs.gain" }; }
} // namespace cs
