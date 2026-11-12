// Cauce Drums: caja de ritmos de 8 carriles con sonidos 100 % sintetizados aquí (sin muestras de terceros, ver
// docs/LICENSES.md). Los sonidos de cada kit se generan una vez en prepare() (hilo de control); el hilo de audio
// solo reproduce búferes ya calculados. Nota MIDI 36 + carril (canal indiferente).
#include "cs/Instruments.h"
#include "cs/Midi.h"

#include "Dsp.h"

#include <array>
#include <atomic>
#include <cmath>
#include <memory>

namespace cs
{
namespace
{
using dsp::Biquad;
using dsp::kPi;
using dsp::Rng;
using Buf = std::vector<float>;

Buf envelopeLen (double sr, double seconds) { return Buf ((size_t) std::max (1.0, seconds * sr), 0.0f); }

void normalize (Buf& b, float peak)
{
    float m = 0;
    for (float v : b) m = std::max (m, std::fabs (v));
    if (m > 1e-9f) for (float& v : b) v *= peak / m;
    // fundido final de 2 ms para no terminar con un escalón
    const size_t f = std::min (b.size(), (size_t) 96);
    for (size_t i = 0; i < f; ++i) b[b.size() - 1 - i] *= (float) i / (float) f;
}

Buf kick (double sr, double f0, double f1, double pitchTau, double ampTau, double len, double click, double drive)
{
    Buf b = envelopeLen (sr, len);
    Rng rng (7);
    Biquad hp; hp.set (Biquad::HighPass, sr, 1500, 0.7);
    double ph = 0;
    const double dn = std::tanh (drive);
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double f = f1 + (f0 - f1) * std::exp (-t / pitchTau);
        ph += f / sr;
        const double amp = std::exp (-t / ampTau) * std::min (1.0, t / 0.0008);
        double x = std::sin (2 * kPi * ph) * amp;
        x = std::tanh (x * drive) / dn;
        const double c = t < 0.006 ? hp.process (rng.next()) * click * (1.0 - t / 0.006) : 0.0;
        b[i] = (float) (x + c);
    }
    return b;
}

Buf snare (double sr, double tone, double toneTau, double noiseTau, double hpHz, double lpHz, double toneMix, double len)
{
    Buf b = envelopeLen (sr, len);
    Rng rng (11);
    Biquad hp, lp; hp.set (Biquad::HighPass, sr, hpHz, 0.7); lp.set (Biquad::LowPass, sr, lpHz, 0.7);
    double ph1 = 0, ph2 = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double bend = 1.0 + 0.5 * std::exp (-t / 0.01);
        ph1 += tone * bend / sr; ph2 += tone * 1.6 * bend / sr;
        const double body = (std::sin (2 * kPi * ph1) + 0.5 * std::sin (2 * kPi * ph2)) * std::exp (-t / toneTau);
        const double n = lp.process (hp.process (rng.next())) * std::exp (-t / noiseTau);
        b[i] = (float) ((body * toneMix + n * (1.0 - toneMix) * 2.2) * std::min (1.0, t / 0.0005));
    }
    return b;
}

Buf clap (double sr, double center, double tail, double len)
{
    Buf b = envelopeLen (sr, len);
    Rng rng (23);
    Biquad bp; bp.set (Biquad::BandPass, sr, center, 1.4);
    Biquad hp; hp.set (Biquad::HighPass, sr, 600, 0.7);
    const double spacing = 0.0095;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        double env = 0;
        for (int k = 0; k < 3; ++k)
        {
            const double tk = t - k * spacing;
            if (tk >= 0 && tk < spacing) env = std::max (env, std::exp (-tk / 0.0025));
        }
        const double tt = t - 3 * spacing;
        if (tt >= 0) env = std::max (env, std::exp (-tt / tail));
        b[i] = (float) (hp.process (bp.process (rng.next())) * env);
    }
    return b;
}

Buf hat (double sr, double tau, double hpHz, double len, uint32_t seed)
{
    // Seis osciladores cuadrados a frecuencias inarmónicas (técnica clásica de cajas analógicas) + ruido.
    static const double fr[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    Buf b = envelopeLen (sr, len);
    Rng rng (seed);
    Biquad bp; bp.set (Biquad::BandPass, sr, 10000, 0.9);
    Biquad hp1, hp2; hp1.set (Biquad::HighPass, sr, hpHz, 0.7); hp2.set (Biquad::HighPass, sr, hpHz, 0.7);
    double ph[6] = { 0, 0.1, 0.2, 0.3, 0.4, 0.5 };
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        double m = 0;
        for (int k = 0; k < 6; ++k) { ph[k] += fr[k] * 1.7 / sr; ph[k] -= std::floor (ph[k]); m += ph[k] < 0.5 ? 1.0 : -1.0; }
        const double x = m / 6.0 * 0.7 + rng.next() * 0.3;
        const double env = std::exp (-t / tau) * std::min (1.0, t / 0.0004);
        b[i] = (float) (hp2.process (hp1.process (bp.process ((float) x))) * env);
    }
    return b;
}

Buf tom (double sr, double f0, double f1, double tau, double len, double noiseAmt)
{
    Buf b = envelopeLen (sr, len);
    Rng rng (31);
    Biquad bp; bp.set (Biquad::BandPass, sr, f0 * 3, 1.0);
    double ph = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double f = f1 + (f0 - f1) * std::exp (-t / 0.03);
        ph += f / sr;
        const double env = std::exp (-t / tau) * std::min (1.0, t / 0.0006);
        const double n = t < 0.03 ? bp.process (rng.next()) * noiseAmt * std::exp (-t / 0.008) : 0.0;
        b[i] = (float) (std::sin (2 * kPi * ph) * env + n);
    }
    return b;
}

Buf rim (double sr, double hz, double tau)
{
    Buf b = envelopeLen (sr, 0.12);
    Biquad bp; bp.set (Biquad::BandPass, sr, hz, 3.0);
    Rng rng (41);
    double ph = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        ph += hz / sr;
        const double x = (ph - std::floor (ph) < 0.5 ? 1.0 : -1.0) * 0.6 + rng.next() * 0.4;
        b[i] = (float) (bp.process ((float) x) * std::exp (-t / tau));
    }
    return b;
}

Buf cowbell (double sr, double f1, double f2, double tau)
{
    Buf b = envelopeLen (sr, 0.5);
    Biquad bp; bp.set (Biquad::BandPass, sr, (f1 + f2) * 0.5 * 2.2, 1.2);
    double p1 = 0, p2 = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        p1 += f1 / sr; p2 += f2 / sr;
        const double x = ((p1 - std::floor (p1) < 0.5) ? 1.0 : -1.0) + ((p2 - std::floor (p2) < 0.5) ? 1.0 : -1.0);
        const double env = (0.6 * std::exp (-t / 0.015) + 0.4 * std::exp (-t / tau)) * std::min (1.0, t / 0.0005);
        b[i] = (float) (bp.process ((float) (x * 0.5)) * env);
    }
    return b;
}

Buf shaker (double sr, double hz, double attack, double tau)
{
    Buf b = envelopeLen (sr, 0.25);
    Rng rng (53);
    Biquad bp; bp.set (Biquad::BandPass, sr, hz, 1.5);
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double env = std::min (1.0, t / attack) * std::exp (-std::max (0.0, t - attack) / tau);
        b[i] = (float) (bp.process (rng.next()) * env);
    }
    return b;
}

struct KitSounds { std::array<Buf, kDrumLanes> lanes; };

std::shared_ptr<KitSounds> buildKit (int kit, double sr)
{
    auto k = std::make_shared<KitSounds>();
    auto& L = k->lanes;
    switch (kit)
    {
        default:
        case 0: // Trap
            L[0] = kick (sr, 150, 47, 0.045, 0.5, 1.5, 0.25, 1.6);
            L[1] = snare (sr, 190, 0.06, 0.13, 1500, 9000, 0.35, 0.45);
            L[2] = clap (sr, 1400, 0.17, 0.5);
            L[3] = hat (sr, 0.032, 7500, 0.18, 61);
            L[4] = hat (sr, 0.33, 7000, 1.2, 67);
            L[5] = rim (sr, 1700, 0.022);
            L[6] = cowbell (sr, 540, 800, 0.12);
            L[7] = tom (sr, 190, 120, 0.25, 0.9, 0.3);
            break;
        case 1: // Reggaetón
            L[0] = kick (sr, 200, 58, 0.025, 0.22, 0.65, 0.4, 2.0);
            L[1] = snare (sr, 240, 0.05, 0.09, 2000, 10000, 0.5, 0.35);
            L[2] = clap (sr, 1600, 0.12, 0.4);
            L[3] = hat (sr, 0.028, 8000, 0.16, 71);
            L[4] = hat (sr, 0.25, 7500, 0.9, 73);
            L[5] = tom (sr, 520, 420, 0.12, 0.45, 0.25);  // timbal agudo
            L[6] = tom (sr, 330, 270, 0.16, 0.55, 0.25);  // timbal grave
            L[7] = shaker (sr, 7000, 0.008, 0.07);
            break;
        case 2: // Hip-hop
            L[0] = kick (sr, 120, 52, 0.03, 0.3, 0.85, 0.5, 2.5);
            L[1] = snare (sr, 180, 0.08, 0.18, 900, 7000, 0.45, 0.55);
            L[2] = clap (sr, 1100, 0.2, 0.55);
            L[3] = hat (sr, 0.045, 6500, 0.22, 79);
            L[4] = hat (sr, 0.3, 6000, 1.0, 83);
            L[5] = rim (sr, 1500, 0.025);
            L[6] = shaker (sr, 6000, 0.012, 0.09);
            L[7] = tom (sr, 140, 100, 0.3, 1.0, 0.25);
            break;
        case 3: // Electrónica
            L[0] = kick (sr, 260, 55, 0.02, 0.28, 0.75, 0.6, 1.2);
            L[1] = snare (sr, 210, 0.05, 0.2, 1200, 12000, 0.3, 0.5);
            L[2] = clap (sr, 1250, 0.25, 0.6);
            L[3] = hat (sr, 0.025, 9000, 0.15, 89);
            L[4] = hat (sr, 0.45, 8500, 1.4, 97);
            L[5] = rim (sr, 2000, 0.018);
            L[6] = cowbell (sr, 587, 845, 0.1);
            L[7] = tom (sr, 220, 160, 0.2, 0.8, 0.35);
            break;
    }
    static const float peaks[kDrumLanes] = { 0.95f, 0.75f, 0.7f, 0.42f, 0.4f, 0.55f, 0.5f, 0.65f };
    for (int i = 0; i < kDrumLanes; ++i) normalize (L[(size_t) i], peaks[i]);
    return k;
}

constexpr int kLaneParams = 6; // vol, pan, pitch, decay, mute, solo
enum LaneParam { lVol, lPan, lPitch, lDecay, lMute, lSolo };

const std::vector<ParameterInfo>& drumParams()
{
    static const std::vector<ParameterInfo> ps = []
    {
        std::vector<ParameterInfo> v;
        v.push_back ({ "kit", "Kit", 0, 3, 0 });
        v.push_back ({ "volumeDb", "Volume (dB)", -40, 6, -3 });
        for (int l = 0; l < kDrumLanes; ++l)
        {
            const std::string n = std::to_string (l);
            v.push_back ({ "vol" + n, "Volume " + n, -60, 6, 0 });
            v.push_back ({ "pan" + n, "Pan " + n, -1, 1, 0 });
            v.push_back ({ "pitch" + n, "Pitch " + n, -12, 12, 0 });
            v.push_back ({ "decay" + n, "Decay " + n, 0.1f, 1, 1 });
            v.push_back ({ "mute" + n, "Mute " + n, 0, 1, 0 });
            v.push_back ({ "solo" + n, "Solo " + n, 0, 1, 0 });
        }
        return v;
    }();
    return ps;
}
constexpr int kFirstLaneParam = 2;
inline int laneParam (int lane, LaneParam p) { return kFirstLaneParam + lane * kLaneParams + (int) p; }

class Drums final : public Instrument
{
public:
    Drums()
    {
        const auto& ps = drumParams();
        values = std::make_unique<std::atomic<float>[]> (ps.size());
        for (size_t i = 0; i < ps.size(); ++i) values[i].store (ps[i].defaultValue);
    }
    std::string typeId() const override { return "cs.drums"; }
    // El kit es estructural: se elige con loadState() antes de prepare(); prepare() genera los sonidos.
    void prepare (double rate, int) override
    {
        sr = rate;
        builtKit = (int) std::lround (values[0].load());
        sounds = buildKit (builtKit, sr);
        reset();
    }
    void reset() override { for (auto& lv : laneVoices) for (auto& v : lv) v = Voice(); }
    std::vector<ParameterInfo> parameters() const override { return drumParams(); }
    void setParameter (int i, float v) override
    {
        const auto& ps = drumParams();
        if (i < 0 || i >= (int) ps.size() || ! std::isfinite (v)) return;
        values[(size_t) i].store (std::clamp (v, ps[(size_t) i].minValue, ps[(size_t) i].maxValue));
    }
    float getParameter (int i) const override { return i >= 0 && i < (int) drumParams().size() ? values[(size_t) i].load() : 0.0f; }
    int builtKitIndex() const { return builtKit; }
    int activeVoices() const override
    {
        int n = 0;
        for (const auto& lv : laneVoices) for (const auto& v : lv) n += v.active;
        return n;
    }
    int tailSamples() const override
    {
        size_t m = 0;
        if (sounds) for (const auto& b : sounds->lanes) m = std::max (m, b.size());
        return (int) (m * 2); // con el tono bajado 12 semitonos el sonido dura el doble
    }

    void handleMidi (const MidiEvent* ev, int count) override
    {
        if (! sounds) return;
        for (int i = 0; i < count; ++i)
        {
            if ((ev[i].status & 0xF0) != 0x90 || ev[i].data2 == 0) continue; // one-shot: Note Off no corta
            const int lane = (int) ev[i].data1 - kDrumBaseNote;
            if (lane < 0 || lane >= kDrumLanes) continue;
            trigger (lane, ev[i].data2);
            if (lane == 3) chokeLane (4); // el charles cerrado corta al abierto
        }
    }
    void allNotesOff (bool hard) override { if (hard) reset(); } // los golpes terminan solos

    void process (float* const* io, int numChannels, int n) override
    {
        if (! sounds) return;
        bool anySolo = false;
        for (int l = 0; l < kDrumLanes; ++l) anySolo |= values[(size_t) laneParam (l, lSolo)].load (std::memory_order_relaxed) >= 0.5f;
        const float master = dsp::dbToLin (values[1].load (std::memory_order_relaxed));
        float* L = io[0];
        float* R = numChannels > 1 ? io[1] : io[0];
        for (int l = 0; l < kDrumLanes; ++l)
        {
            const bool mute = values[(size_t) laneParam (l, lMute)].load (std::memory_order_relaxed) >= 0.5f;
            const bool solo = values[(size_t) laneParam (l, lSolo)].load (std::memory_order_relaxed) >= 0.5f;
            const bool audible = ! mute && (! anySolo || solo);
            const float g = audible ? dsp::dbToLin (values[(size_t) laneParam (l, lVol)].load (std::memory_order_relaxed)) * master : 0.0f;
            const float pan = values[(size_t) laneParam (l, lPan)].load (std::memory_order_relaxed);
            const float gl = g * (pan > 0 ? 1.0f - pan : 1.0f), gr = g * (pan < 0 ? 1.0f + pan : 1.0f);
            const Buf& b = sounds->lanes[(size_t) l];
            const double size = (double) b.size();
            for (auto& v : laneVoices[(size_t) l])
            {
                if (! v.active) continue;
                for (int i = 0; i < n; ++i)
                {
                    const size_t i0 = (size_t) v.pos;
                    if (v.pos >= size - 1 || v.pos >= v.end) { v.active = false; break; }
                    const float frac = (float) (v.pos - (double) i0);
                    float x = b[i0] + (b[i0 + 1] - b[i0]) * frac;
                    // decay < 1: fundido exponencial desde 'fadeFrom'; robo/choke: rampa de 3 ms
                    if (v.pos > v.fadeFrom) x *= (float) std::exp (-(v.pos - v.fadeFrom) / (v.fadeLen + 1.0));
                    if (v.release >= 0) { x *= v.release; v.release -= v.releaseStep; if (v.release <= 0) { v.active = false; break; } }
                    x *= v.gain;
                    L[i] += x * gl;
                    if (R != L) R[i] += x * gr;
                    v.pos += v.rate;
                }
            }
        }
        // Protección de salida (transparente por debajo de -2 dBFS).
        for (int i = 0; i < n; ++i) { L[i] = dsp::softClip (L[i]); if (R != L) R[i] = dsp::softClip (R[i]); }
    }

private:
    struct Voice
    {
        bool active = false;
        double pos = 0, rate = 1, end = 0, fadeFrom = 0, fadeLen = 1;
        float gain = 1, release = -1, releaseStep = 0;
    };

    void startRelease (Voice& v)
    {
        if (! v.active || v.release >= 0) return;
        v.release = 1.0f;
        v.releaseStep = 1.0f / (float) (0.003 * sr);
    }
    void chokeLane (int lane) { for (auto& v : laneVoices[(size_t) lane]) startRelease (v); }

    void trigger (int lane, int vel)
    {
        auto& lv = laneVoices[(size_t) lane];
        // Cada carril es monofónico: el golpe anterior se desvanece en 3 ms (sin clic) mientras empieza el nuevo.
        Voice* slot = nullptr;
        for (auto& v : lv) { if (v.active && v.release < 0) startRelease (v); }
        for (auto& v : lv) if (! v.active) { slot = &v; break; }
        if (! slot)
        {
            slot = &lv[0];
            for (auto& v : lv) if (v.release >= 0 && v.release < slot->release) slot = &v;
        }
        const Buf& b = sounds->lanes[(size_t) lane];
        Voice v;
        v.active = true;
        v.rate = std::pow (2.0, values[(size_t) laneParam (lane, lPitch)].load (std::memory_order_relaxed) / 12.0);
        const double decay = values[(size_t) laneParam (lane, lDecay)].load (std::memory_order_relaxed);
        v.end = (double) b.size();
        v.fadeFrom = decay >= 0.999 ? 1e18 : (double) b.size() * decay * 0.35;
        v.fadeLen = (double) b.size() * decay * 0.12;
        const float vn = (float) std::clamp (vel, 1, 127) / 127.0f;
        v.gain = std::pow (vn, 1.3f);
        *slot = v;
    }

    double sr = 48000;
    int builtKit = -1;
    std::unique_ptr<std::atomic<float>[]> values;
    std::shared_ptr<KitSounds> sounds;
    std::array<std::array<Voice, 3>, kDrumLanes> laneVoices {};
};
} // namespace

ProcessorPtr createDrums() { return std::make_shared<Drums>(); }

const std::vector<DrumKitInfo>& drumKits()
{
    static const std::vector<DrumKitInfo> k = {
        { "trap", "Trap", { "Kick 808", "Snare", "Clap", "Closed Hat", "Open Hat", "Rim", "Cowbell", "Tom" } },
        { "reggaeton", "Reggaetón", { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Timbal Hi", "Timbal Lo", "Shaker" } },
        { "hiphop", "Hip-hop", { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Rim", "Shaker", "Tom" } },
        { "electronic", "Electrónica", { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Rim", "Cowbell", "Tom" } },
    };
    return k;
}

int drumKitIndex (const std::string& id)
{
    const auto& k = drumKits();
    for (int i = 0; i < (int) k.size(); ++i) if (k[(size_t) i].id == id) return i;
    return -1;
}
} // namespace cs
