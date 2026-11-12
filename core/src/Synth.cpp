// Cauce Synth: sintetizador sustractivo polifónico (2 osciladores, filtro paso bajo resonante, ADSR, glide).
// Todo el estado es por muestra o por voz con contadores propios: la salida no depende del tamaño de bloque.
#include "cs/Instruments.h"

#include "Dsp.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cmath>

namespace cs
{
namespace
{
enum P
{
    pOsc1Wave, pOsc2Wave, pOsc2Mix, pOsc2Detune, pOsc2Octave, pOctave,
    pAttack, pDecay, pSustain, pRelease,
    pCutoff, pResonance, pFilterEnv, pFilterDecay,
    pDrive, pVolume, pMono, pGlide, pPolyphony,
    pCount
};

const std::vector<ParameterInfo>& synthParams()
{
    static const std::vector<ParameterInfo> ps = {
        { "osc1Wave", "Osc 1", 0, 3, 2 },          // 0 seno, 1 triángulo, 2 sierra, 3 cuadrada
        { "osc2Wave", "Osc 2", 0, 3, 2 },
        { "osc2Mix", "Osc 2 mix", 0, 1, 0 },
        { "osc2Detune", "Detune (cents)", -100, 100, 0 },
        { "osc2Octave", "Osc 2 octave", -2, 2, 0 },
        { "octave", "Octave", -3, 3, 0 },
        { "attackMs", "Attack (ms)", 0.5f, 5000, 5 },
        { "decayMs", "Decay (ms)", 1, 5000, 300 },
        { "sustain", "Sustain", 0, 1, 0.7f },
        { "releaseMs", "Release (ms)", 1, 8000, 250 },
        { "cutoffHz", "Cutoff (Hz)", 40, 18000, 8000 },
        { "resonance", "Resonance", 0, 0.95f, 0.1f },
        { "filterEnv", "Filter env", 0, 1, 0 },
        { "filterDecayMs", "Filter decay (ms)", 1, 5000, 300 },
        { "drive", "Drive", 0, 1, 0 },
        { "volumeDb", "Volume (dB)", -40, 6, -6 },
        { "mono", "Mono", 0, 1, 0 },
        { "glideMs", "Glide (ms)", 0, 1000, 0 },
        { "polyphony", "Voices", 1, (float) kSynthMaxVoices, 8 },
    };
    return ps;
}

struct Snapshot
{
    int w1, w2, oct, oct2, poly;
    float mix, detune, att, dec, sus, rel, cutoff, res, fenv, fdec, drive, vol, glide;
    bool mono;
};

class Synth final : public Instrument
{
public:
    Synth()
    {
        const auto& ps = synthParams();
        for (int i = 0; i < pCount; ++i) values[(size_t) i].store (ps[(size_t) i].defaultValue);
    }
    std::string typeId() const override { return "cs.synth"; }
    void prepare (double rate, int) override { sr = rate; reset(); }
    void reset() override
    {
        for (auto& v : voices) v = Voice();
        heldCount = 0;
        volSmooth = -1.0f;
        counter = 0;
    }
    std::vector<ParameterInfo> parameters() const override { return synthParams(); }
    void setParameter (int i, float v) override
    {
        if (i < 0 || i >= pCount || ! std::isfinite (v)) return;
        const auto& p = synthParams()[(size_t) i];
        values[(size_t) i].store (std::clamp (v, p.minValue, p.maxValue));
    }
    float getParameter (int i) const override { return i >= 0 && i < pCount ? values[(size_t) i].load() : 0.0f; }
    int activeVoices() const override
    {
        int n = 0;
        for (const auto& v : voices) n += v.stage != Off;
        return n;
    }
    int tailSamples() const override
    {
        const float r = values[pRelease].load();
        return (int) (sr * (r * 0.001 * 7.0 + 0.05)); // ~ -60 dB
    }

    void handleMidi (const MidiEvent* ev, int count) override
    {
        const Snapshot s = snap();
        for (int i = 0; i < count; ++i)
        {
            const int type = ev[i].status & 0xF0;
            if (type == 0x90 && ev[i].data2 > 0) noteOn (ev[i].data1, ev[i].data2, s);
            else if (type == 0x80 || (type == 0x90 && ev[i].data2 == 0)) noteOff (ev[i].data1, s);
            else if (type == 0xB0 && (ev[i].data1 == 123 || ev[i].data1 == 120)) allNotesOff (ev[i].data1 == 120);
        }
    }

    void allNotesOff (bool hard) override
    {
        heldCount = 0;
        for (auto& v : voices)
        {
            if (hard) v = Voice();
            else if (v.stage != Off) v.stage = Release;
        }
    }

    void process (float* const* io, int numChannels, int n) override
    {
        const Snapshot s = snap();
        const float volTarget = dsp::dbToLin (s.vol) * 0.35f;
        if (volSmooth < 0) volSmooth = volTarget;
        const double attK = rateFor (s.att), decK = 1.0 - std::exp (-1.0 / (std::max (1.0f, s.dec) * 0.001 * sr / 4.0));
        const double relK = 1.0 - std::exp (-1.0 / (std::max (1.0f, s.rel) * 0.001 * sr / 4.0));
        const double fdecK = std::exp (-1.0 / (std::max (1.0f, s.fdec) * 0.001 * sr / 4.0));
        const double glideK = s.glide > 0 ? 1.0 - std::exp (-1.0 / (s.glide * 0.001 * sr / 4.0)) : 1.0;
        const double driveGain = 1.0 + s.drive * 8.0, driveNorm = 1.0 / std::tanh (driveGain);
        const double k = 2.0 - 2.0 * std::clamp ((double) s.res, 0.0, 0.95);
        // Detune centrado: el osc 1 baja y el osc 2 sube en proporción a la mezcla, de modo que la afinación media
        // percibida queda en la nota (antes solo subía el osc 2: el pad sonaba ~9 cents alto).
        const double ratio1 = std::pow (2.0, -s.mix * s.detune / 1200.0);
        const double ratio2 = std::pow (2.0, s.oct2 + (1.0 - s.mix) * s.detune / 1200.0) / ratio1;
        const float volStep = 1.0f / (float) (0.010 * sr);
        float* L = io[0];
        float* R = numChannels > 1 ? io[1] : io[0];

        for (int i = 0; i < n; ++i)
        {
            float mix = 0.0f;
            for (auto& v : voices)
            {
                if (v.stage == Off) continue;
                // Envolvente de amplitud
                switch (v.stage)
                {
                    case Attack: v.env += attK; if (v.env >= 1.0) { v.env = 1.0; v.stage = Decay; } break;
                    case Decay: v.env += (s.sus - v.env) * decK; if (std::fabs (v.env - s.sus) < 1e-5) { v.env = s.sus; v.stage = Sustain; } break;
                    case Sustain: v.env = s.sus; if (s.sus <= 0.0f) v.stage = Release; break;
                    case Release: v.env -= v.env * relK; break;
                    default: break;
                }
                if (v.stage == Release && v.env < 1e-4) { v = Voice(); continue; }
                v.fenv *= fdecK;
                v.freq += (v.target - v.freq) * glideK;

                // Coeficientes del filtro cada 16 muestras de la propia voz (determinista).
                if ((v.age & 15) == 0)
                {
                    double fc = s.cutoff * std::pow (2.0, (double) s.fenv * 6.0 * v.fenv);
                    fc = std::clamp (fc, 20.0, sr * 0.45);
                    v.g = std::tan (dsp::kPi * fc / sr);
                    v.a1 = 1.0 / (1.0 + v.g * (v.g + k));
                }
                ++v.age;

                const double dt1 = v.freq * ratio1 / sr;
                const double dt2 = dt1 * ratio2;
                double o = osc (s.w1, v.ph1, dt1);
                if (s.mix > 0.0f) o = o * (1.0 - s.mix) + osc (s.w2, v.ph2, dt2) * s.mix;
                v.ph1 += dt1; if (v.ph1 >= 1.0) v.ph1 -= 1.0;
                v.ph2 += dt2; if (v.ph2 >= 1.0) v.ph2 -= 1.0;

                // SVF TPT paso bajo (Zavalishin): estable con cualquier corte y resonancia acotada.
                const double v3 = o - v.ic2;
                const double v1 = v.a1 * v.ic1 + v.a1 * v.g * v3;
                const double v2 = v.ic2 + v.g * v1;
                v.ic1 = 2 * v1 - v.ic1;
                v.ic2 = 2 * v2 - v.ic2;
                double y = v2;
                if (s.drive > 0.0f) y = std::tanh (y * driveGain) * driveNorm;
                y *= v.env * v.velGain;
                if (! std::isfinite (y) || ! std::isfinite (v.ic1) || ! std::isfinite (v.ic2)) { v = Voice(); continue; }
                mix += (float) y;
            }
            volSmooth += std::clamp (volTarget - volSmooth, -volStep, volStep);
            const float out = dsp::softClip (mix * volSmooth);
            L[i] += out;
            if (R != L) R[i] += out;
        }
    }

private:
    enum Stage { Off, Attack, Decay, Sustain, Release };
    struct Voice
    {
        Stage stage = Off;
        int key = -1;
        uint64_t order = 0, age = 0;
        double env = 0, fenv = 0, freq = 440, target = 440, ph1 = 0, ph2 = 0;
        double ic1 = 0, ic2 = 0, g = 0, a1 = 1;
        float velGain = 1;
    };

    double rateFor (float ms) const { return 1.0 / std::max (1.0, ms * 0.001 * sr); }

    static double osc (int w, double ph, double dt)
    {
        switch (w)
        {
            case 0: return dsp::sin2pi (ph);
            case 1: return 4.0 * std::fabs (ph - 0.5) - 1.0;
            case 2: return 2.0 * ph - 1.0 - dsp::polyBlep (ph, dt);
            default:
            {
                double v = ph < 0.5 ? 1.0 : -1.0;
                v += dsp::polyBlep (ph, dt);
                double t2 = ph + 0.5; if (t2 >= 1.0) t2 -= 1.0;
                v -= dsp::polyBlep (t2, dt);
                return v * 0.7;
            }
        }
    }

    Snapshot snap() const
    {
        auto g = [&] (int i) { return values[(size_t) i].load (std::memory_order_relaxed); };
        Snapshot s;
        s.w1 = (int) std::lround (g (pOsc1Wave)); s.w2 = (int) std::lround (g (pOsc2Wave));
        s.mix = g (pOsc2Mix); s.detune = g (pOsc2Detune); s.oct2 = (int) std::lround (g (pOsc2Octave)); s.oct = (int) std::lround (g (pOctave));
        s.att = g (pAttack); s.dec = g (pDecay); s.sus = g (pSustain); s.rel = g (pRelease);
        s.cutoff = g (pCutoff); s.res = g (pResonance); s.fenv = g (pFilterEnv); s.fdec = g (pFilterDecay);
        s.drive = g (pDrive); s.vol = g (pVolume); s.mono = g (pMono) >= 0.5f; s.glide = g (pGlide);
        s.poly = std::clamp ((int) std::lround (g (pPolyphony)), 1, kSynthMaxVoices);
        return s;
    }

    double keyFreq (int key, const Snapshot& s) const { return 440.0 * std::pow (2.0, (key - 69 + 12 * s.oct) / 12.0); }

    void start (Voice& v, int key, int vel, const Snapshot& s, bool legato)
    {
        v.key = key;
        v.target = keyFreq (key, s);
        if (! legato || v.stage == Off || s.glide <= 0) v.freq = v.target;
        if (! legato || v.stage == Off || v.stage == Release)
        {
            v.stage = Attack; // desde el nivel actual: sin clic al robar una voz
            v.fenv = 1.0;
            if (v.age == 0) { v.ph1 = 0; v.ph2 = 0.25; }
        }
        v.velGain = 0.25f + 0.75f * (float) vel / 127.0f;
        v.order = ++counter;
    }

    void noteOn (int key, int vel, const Snapshot& s)
    {
        if (s.mono)
        {
            pushHeld (key, vel);
            Voice& v = voices[0];
            for (size_t i = 1; i < voices.size(); ++i) if (voices[i].stage != Off) voices[i].stage = Release;
            start (v, key, vel, s, v.stage != Off && v.stage != Release);
            return;
        }
        // Reutiliza la voz si la misma nota sigue sonando; si no, una libre; si no, roba.
        Voice* target = nullptr;
        for (int i = 0; i < s.poly; ++i) if (voices[(size_t) i].stage != Off && voices[(size_t) i].key == key) { target = &voices[(size_t) i]; break; }
        if (! target) for (int i = 0; i < s.poly; ++i) if (voices[(size_t) i].stage == Off) { target = &voices[(size_t) i]; break; }
        if (! target)
        {
            // Prioridad de robo: la voz en release más silenciosa; si no hay, la más antigua.
            for (int i = 0; i < s.poly; ++i)
            {
                Voice& v = voices[(size_t) i];
                if (v.stage == Release && (! target || v.env < target->env)) target = &v;
            }
            if (! target)
                for (int i = 0; i < s.poly; ++i)
                    if (! target || voices[(size_t) i].order < target->order) target = &voices[(size_t) i];
        }
        start (*target, key, vel, s, false);
        // Si se redujo la polifonía, las voces sobrantes se liberan.
        for (size_t i = (size_t) s.poly; i < voices.size(); ++i) if (voices[i].stage != Off && voices[i].stage != Release) voices[i].stage = Release;
    }

    void noteOff (int key, const Snapshot& s)
    {
        if (s.mono)
        {
            removeHeld (key);
            Voice& v = voices[0];
            if (v.key != key || v.stage == Off) return;
            if (heldCount > 0) start (v, held[(size_t) heldCount - 1].first, held[(size_t) heldCount - 1].second, s, true);
            else v.stage = Release;
            return;
        }
        for (auto& v : voices) if (v.stage != Off && v.stage != Release && v.key == key) v.stage = Release;
    }

    void pushHeld (int key, int vel)
    {
        removeHeld (key);
        if (heldCount == (int) held.size()) { for (size_t i = 1; i < held.size(); ++i) held[i - 1] = held[i]; --heldCount; }
        held[(size_t) heldCount++] = { key, vel };
    }
    void removeHeld (int key)
    {
        int w = 0;
        for (int r = 0; r < heldCount; ++r) if (held[(size_t) r].first != key) held[(size_t) w++] = held[(size_t) r];
        heldCount = w;
    }

    double sr = 48000;
    std::array<std::atomic<float>, pCount> values;
    std::array<Voice, kSynthMaxVoices> voices;
    std::array<std::pair<int, int>, 16> held {};
    int heldCount = 0;
    uint64_t counter = 0;
    float volSmooth = -1.0f;
};
} // namespace

ProcessorPtr createSynth() { return std::make_shared<Synth>(); }

const std::vector<SynthPreset>& synthPresets()
{
    // Nombres descriptivos: son sonidos de síntesis, no imitaciones fieles de instrumentos acústicos.
    static const std::vector<SynthPreset> p = {
        { "sub_bass", "Sub Bass", { { "osc1Wave", 0 }, { "osc2Wave", 1 }, { "osc2Mix", 0.15f }, { "octave", -1 }, { "attackMs", 4 }, { "decayMs", 400 },
                                    { "sustain", 0.9f }, { "releaseMs", 140 }, { "cutoffHz", 600 }, { "resonance", 0 }, { "volumeDb", -3 }, { "mono", 1 }, { "polyphony", 1 } } },
        { "bass_808", "808 Bass", { { "osc1Wave", 0 }, { "octave", -1 }, { "attackMs", 2 }, { "decayMs", 1800 }, { "sustain", 0.0f }, { "releaseMs", 350 },
                                    { "cutoffHz", 2500 }, { "resonance", 0 }, { "drive", 0.45f }, { "volumeDb", -5 }, { "mono", 1 }, { "glideMs", 70 }, { "polyphony", 1 } } },
        { "lead", "Lead", { { "osc1Wave", 2 }, { "osc2Wave", 2 }, { "osc2Mix", 0.5f }, { "osc2Detune", 12 }, { "attackMs", 6 }, { "decayMs", 400 }, { "sustain", 0.75f },
                            { "releaseMs", 220 }, { "cutoffHz", 2800 }, { "resonance", 0.3f }, { "filterEnv", 0.35f }, { "filterDecayMs", 350 }, { "volumeDb", -9 }, { "polyphony", 6 } } },
        { "soft_keys", "Soft Keys", { { "osc1Wave", 1 }, { "osc2Wave", 0 }, { "osc2Mix", 0.35f }, { "osc2Octave", 1 }, { "attackMs", 3 }, { "decayMs", 900 },
                                      { "sustain", 0.25f }, { "releaseMs", 450 }, { "cutoffHz", 3000 }, { "resonance", 0.05f }, { "filterEnv", 0.2f }, { "filterDecayMs", 500 },
                                      { "volumeDb", -6 }, { "polyphony", 10 } } },
        { "pad", "Pad", { { "osc1Wave", 2 }, { "osc2Wave", 2 }, { "osc2Mix", 0.5f }, { "osc2Detune", 18 }, { "attackMs", 600 }, { "decayMs", 1200 }, { "sustain", 0.8f },
                          { "releaseMs", 1600 }, { "cutoffHz", 1600 }, { "resonance", 0.15f }, { "volumeDb", -12 }, { "polyphony", 10 } } },
        { "pluck", "Pluck", { { "osc1Wave", 2 }, { "osc2Wave", 3 }, { "osc2Mix", 0.3f }, { "attackMs", 1 }, { "decayMs", 260 }, { "sustain", 0.0f }, { "releaseMs", 220 },
                              { "cutoffHz", 500 }, { "resonance", 0.25f }, { "filterEnv", 0.6f }, { "filterDecayMs", 180 }, { "volumeDb", -6 }, { "polyphony", 8 } } },
        { "synth_bass", "Synth Bass", { { "osc1Wave", 3 }, { "osc2Wave", 2 }, { "osc2Mix", 0.5f }, { "osc2Octave", -1 }, { "octave", -1 }, { "attackMs", 2 }, { "decayMs", 300 },
                                        { "sustain", 0.6f }, { "releaseMs", 120 }, { "cutoffHz", 700 }, { "resonance", 0.4f }, { "filterEnv", 0.45f },
                                        { "filterDecayMs", 220 }, { "volumeDb", -3 }, { "mono", 1 }, { "glideMs", 25 }, { "polyphony", 1 } } },
    };
    return p;
}

std::string synthPresetState (const std::string& presetId)
{
    for (const auto& p : synthPresets())
        if (p.id == presetId)
        {
            nlohmann::json j = nlohmann::json::object();
            for (const auto& info : synthParams()) j[info.id] = info.defaultValue;
            for (const auto& [k, v] : p.values) j[k] = v;
            j["preset"] = p.id;
            return j.dump();
        }
    return {};
}

int parameterIndex (const AudioProcessor& p, const std::string& id)
{
    const auto ps = p.parameters();
    for (int i = 0; i < (int) ps.size(); ++i) if (ps[(size_t) i].id == id) return i;
    return -1;
}
} // namespace cs
