// Cauce FX: efectos de inserción propios (sin código ni muestras de terceros).
//
// Contrato de tiempo real (ver Processor.h): prepare() reserva todos los búferes; process() no reserva, no bloquea
// y no toca disco. Los parámetros son atómicos; los coeficientes se recalculan al inicio del bloque cuando cambian.
// Todos los efectos tienen latencia 0 (no necesitan compensación de retardo).
#include "Dsp.h"
#include "cs/Processor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>

namespace cs
{
namespace
{
using namespace dsp;

template <size_t N>
class FxBase : public AudioProcessor
{
public:
    explicit FxBase (std::array<ParameterInfo, N> info) : infos (std::move (info))
    {
        for (size_t i = 0; i < N; ++i) values[i].store (infos[i].defaultValue);
    }
    std::vector<ParameterInfo> parameters() const override { return { infos.begin(), infos.end() }; }
    void setParameter (int index, float v) override
    {
        if (index < 0 || index >= (int) N || ! std::isfinite (v)) return;
        values[(size_t) index].store (std::clamp (v, infos[(size_t) index].minValue, infos[(size_t) index].maxValue));
        version.fetch_add (1, std::memory_order_release);
    }
    float getParameter (int index) const override { return index >= 0 && index < (int) N ? values[(size_t) index].load() : 0.0f; }

protected:
    float p (size_t i) const { return values[i].load (std::memory_order_relaxed); }
    // Devuelve true si algún parámetro cambió desde la última llamada (hilo de audio).
    bool changed()
    {
        const unsigned v = version.load (std::memory_order_acquire);
        if (v == seen) return false;
        seen = v;
        return true;
    }
    void forceUpdate() { seen = version.load() - 1; }

    std::array<ParameterInfo, N> infos;
    std::array<std::atomic<float>, N> values;
    std::atomic<unsigned> version { 1 };
    unsigned seen = 0;
    double sr = 48000;
};

// Suavizado lineal de una ganancia para evitar clics al mover controles.
struct Smooth
{
    float cur = 0, step = 0.001f;
    void prepare (double sr, float v) { step = (float) (1.0 / (0.02 * sr)); cur = v; }
    float next (float target)
    {
        if (cur < target) cur = std::min (cur + step, target);
        else if (cur > target) cur = std::max (cur - step, target);
        return cur;
    }
};

inline float sane (float x) { return std::isfinite (x) ? x : 0.0f; }

// --- Filtro (paso alto / paso bajo resonante, SVF TPT) ---------------------------------------------------------
class FilterFx final : public FxBase<3>
{
public:
    FilterFx() : FxBase ({ { { "mode", "Mode (0 LP, 1 HP)", 0, 1, 0 }, { "cutoffHz", "Cutoff", 20, 20000, 20000 }, { "resonance", "Resonance", 0, 1, 0.1f } } }) {}
    std::string typeId() const override { return "cs.filter"; }
    void prepare (double rate, int) override { sr = rate; forceUpdate(); reset(); }
    void reset() override { for (auto& s : st) s = { 0, 0 }; forceUpdate(); }
    void process (float* const* io, int nch, int n) override
    {
        if (changed())
        {
            g = std::tan (kPi * std::min<double> (p (1), sr * 0.45) / sr);
            k = 2.0 - 1.9 * p (2);
            a1 = 1.0 / (1.0 + g * (g + k));
            hp = p (0) >= 0.5f;
        }
        for (int c = 0; c < std::min (nch, 2); ++c)
        {
            auto& [ic1, ic2] = st[(size_t) c];
            float* x = io[c];
            for (int i = 0; i < n; ++i)
            {
                const double v0 = x[i];
                const double v1 = a1 * (ic1 + g * (v0 - ic2));
                const double v2 = ic2 + g * v1;
                ic1 = 2 * v1 - ic1;
                ic2 = 2 * v2 - ic2;
                const double y = hp ? v0 - k * v1 - v2 : v2;
                if (! std::isfinite (ic1) || ! std::isfinite (ic2)) { ic1 = ic2 = 0; x[i] = 0; continue; }
                x[i] = (float) y;
            }
        }
    }

private:
    std::array<std::pair<double, double>, 2> st {};
    double g = 1, k = 2, a1 = 1;
    bool hp = false;
};

// --- EQ de 3 bandas: graves (shelf 120 Hz), medios (campana ajustable), agudos (shelf 8 kHz) ---------------------
class EqFx final : public FxBase<4>
{
public:
    EqFx() : FxBase ({ { { "lowDb", "Low", -15, 15, 0 }, { "midDb", "Mid", -15, 15, 0 }, { "midHz", "Mid freq", 200, 5000, 1000 }, { "highDb", "High", -15, 15, 0 } } }) {}
    std::string typeId() const override { return "cs.eq"; }
    void prepare (double rate, int) override { sr = rate; reset(); }
    void reset() override { for (auto& ch : bq) for (auto& b : ch) b.reset(); forceUpdate(); }
    void process (float* const* io, int nch, int n) override
    {
        if (changed())
            for (auto& ch : bq)
            {
                ch[0].set (Biquad::LowShelf, sr, 120, 0.707, p (0));
                ch[1].set (Biquad::Peak, sr, p (2), 0.9, p (1));
                ch[2].set (Biquad::HighShelf, sr, 8000, 0.707, p (3));
            }
        for (int c = 0; c < std::min (nch, 2); ++c)
            for (int i = 0; i < n; ++i)
            {
                float v = io[c][i];
                for (auto& b : bq[(size_t) c]) v = b.process (v);
                io[c][i] = v;
            }
    }

private:
    std::array<std::array<Biquad, 3>, 2> bq;
};

// --- Saturación suave con mezcla y compensación de nivel -------------------------------------------------------
class SaturationFx final : public FxBase<3>
{
public:
    SaturationFx() : FxBase ({ { { "drive", "Drive", 0, 1, 0.3f }, { "mix", "Mix", 0, 1, 1 }, { "outputDb", "Output", -24, 6, 0 } } }) {}
    std::string typeId() const override { return "cs.saturation"; }
    void prepare (double rate, int) override { sr = rate; reset(); }
    void reset() override { out.prepare (sr, dbToLin (p (2))); }
    void process (float* const* io, int nch, int n) override
    {
        const float pre = 1.0f + 15.0f * p (0) * p (0);
        const float comp = 1.0f / std::tanh (pre * 0.5f) * 0.5f; // nivel aproximado constante a -6 dBFS
        const float mix = p (1), target = dbToLin (p (2));
        for (int i = 0; i < n; ++i)
        {
            const float g = out.next (target);
            for (int c = 0; c < std::min (nch, 2); ++c)
            {
                const float x = io[c][i];
                const float wet = std::tanh (x * pre) * comp;
                io[c][i] = sane ((x + (wet - x) * mix) * g);
            }
        }
    }

private:
    Smooth out;
};

// --- Compresor (detector de pico estéreo enlazado, rodilla suave) ----------------------------------------------
class CompressorFx final : public FxBase<5>
{
public:
    CompressorFx() : FxBase ({ { { "thresholdDb", "Threshold", -48, 0, -18 }, { "ratio", "Ratio", 1, 20, 4 }, { "attackMs", "Attack", 0.1f, 100, 10 },
                                 { "releaseMs", "Release", 10, 1000, 120 }, { "makeupDb", "Makeup", 0, 24, 0 } } }) {}
    std::string typeId() const override { return "cs.compressor"; }
    void prepare (double rate, int) override { sr = rate; reset(); }
    void reset() override { env = 0; grDb.store (0); forceUpdate(); }
    // getParameter(-1): reducción de ganancia actual en dB (medidor, solo lectura).
    float getParameter (int index) const override { return index == -1 ? grDb.load() : FxBase::getParameter (index); }
    void process (float* const* io, int nch, int n) override
    {
        if (changed())
        {
            att = (float) std::exp (-1.0 / (p (2) * 0.001 * sr));
            rel = (float) std::exp (-1.0 / (p (3) * 0.001 * sr));
        }
        const float thr = p (0), ratio = p (1), makeup = dbToLin (p (4)), knee = 6.0f;
        float maxGr = 0;
        for (int i = 0; i < n; ++i)
        {
            float peak = 0;
            for (int c = 0; c < std::min (nch, 2); ++c) peak = std::max (peak, std::fabs (io[c][i]));
            env = peak > env ? att * env + (1 - att) * peak : rel * env + (1 - rel) * peak;
            if (! std::isfinite (env)) env = 0;
            const float lev = env > 1e-6f ? 20.0f * std::log10 (env) : -120.0f;
            const float over = lev - thr;
            float gr = 0;
            if (over > knee / 2) gr = over * (1 - 1 / ratio);
            else if (over > -knee / 2) gr = (1 - 1 / ratio) * (over + knee / 2) * (over + knee / 2) / (2 * knee);
            maxGr = std::max (maxGr, gr);
            const float g = dbToLin (-gr) * makeup;
            for (int c = 0; c < std::min (nch, 2); ++c) io[c][i] = sane (io[c][i] * g);
        }
        grDb.store (maxGr);
    }

private:
    float env = 0, att = 0.99f, rel = 0.999f;
    std::atomic<float> grDb { 0 };
};

// --- Delay estéreo (tiempo en ms, realimentación filtrada) -----------------------------------------------------
class DelayFx final : public FxBase<5>
{
public:
    static constexpr double kMaxSeconds = 2.0;
    DelayFx() : FxBase ({ { { "timeMs", "Time", 10, 2000, 375 }, { "feedback", "Feedback", 0, 0.9f, 0.35f }, { "mix", "Mix", 0, 1, 0.25f },
                            { "toneHz", "Tone", 500, 16000, 6000 }, { "pingPong", "Ping-pong", 0, 1, 0 } } }) {}
    std::string typeId() const override { return "cs.delay"; }
    void prepare (double rate, int) override
    {
        sr = rate;
        for (auto& b : buf) b.assign ((size_t) (kMaxSeconds * sr) + 4, 0.0f);
        reset();
    }
    void reset() override
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& t : tone) t.reset();
        pos = 0;
        time.prepare (sr, (float) (p (0) * 0.001 * sr));
        time.step = 2.0f; // cambio de tiempo audible pero sin clics: ~2 muestras por muestra
        forceUpdate();
    }
    int tailSamples() const override
    {
        // Tiempo hasta caer 60 dB con la realimentación actual.
        const double fb = std::max (0.01f, p (1));
        const double repeats = std::ceil (-3.0 / std::log10 (fb));
        return (int) std::min (sr * 20.0, (repeats + 1) * p (0) * 0.001 * sr);
    }
    void process (float* const* io, int nch, int n) override
    {
        if (buf[0].empty()) return;
        if (changed()) for (auto& t : tone) t.set (Biquad::LowPass, sr, p (3), 0.707);
        const size_t len = buf[0].size();
        const float fb = p (1), mix = p (2);
        const bool pp = p (4) >= 0.5f;
        const float target = (float) std::clamp (p (0) * 0.001 * sr, 1.0, (double) len - 2);
        for (int i = 0; i < n; ++i)
        {
            const float d = time.next (target);
            const double rp = (double) pos - d + (double) len;
            const size_t i0 = (size_t) rp % len, i1 = (i0 + 1) % len;
            const float fr = (float) (rp - std::floor (rp));
            float in[2] = { io[0][i], nch > 1 ? io[1][i] : io[0][i] };
            float wet[2];
            for (int c = 0; c < 2; ++c) wet[c] = buf[(size_t) c][i0] + (buf[(size_t) c][i1] - buf[(size_t) c][i0]) * fr;
            for (int c = 0; c < 2; ++c)
            {
                const float back = tone[(size_t) c].process (wet[pp ? 1 - c : c]) * fb;
                const float feed = pp ? (c == 0 ? 0.5f * (in[0] + in[1]) : 0.0f) : in[c];
                buf[(size_t) c][pos] = sane (feed + back);
            }
            for (int c = 0; c < std::min (nch, 2); ++c) io[c][i] = sane (in[c] * (1 - mix) + wet[c] * mix);
            pos = (pos + 1) % len;
        }
    }

private:
    std::array<std::vector<float>, 2> buf;
    std::array<Biquad, 2> tone;
    size_t pos = 0;
    Smooth time;
};

// --- Reverb algorítmica (red de combs filtrados + all-pass, estilo Schroeder/Moorer; implementación propia) ------
class ReverbFx final : public FxBase<4>
{
public:
    ReverbFx() : FxBase ({ { { "size", "Size", 0, 1, 0.6f }, { "damping", "Damping", 0, 1, 0.4f }, { "mix", "Mix", 0, 1, 0.25f }, { "width", "Width", 0, 1, 1 } } }) {}
    std::string typeId() const override { return "cs.reverb"; }
    void prepare (double rate, int) override
    {
        sr = rate;
        const double scale = sr / 44100.0;
        static const int combLen[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int apLen[4] = { 556, 441, 341, 225 };
        for (int c = 0; c < 2; ++c)
        {
            const int spread = c == 0 ? 0 : 23;
            for (int k = 0; k < 8; ++k) combs[(size_t) c][(size_t) k].assign ((size_t) ((combLen[k] + spread) * scale), 0.0f);
            for (int k = 0; k < 4; ++k) aps[(size_t) c][(size_t) k].assign ((size_t) ((apLen[k] + spread) * scale), 0.0f);
        }
        reset();
    }
    void reset() override
    {
        for (auto& ch : combs) for (auto& b : ch) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& ch : aps) for (auto& b : ch) std::fill (b.begin(), b.end(), 0.0f);
        cpos = {}; apos = {}; lp = {};
    }
    int tailSamples() const override { return (int) (sr * (1.0 + 5.0 * p (0))); }
    void process (float* const* io, int nch, int n) override
    {
        if (combs[0][0].empty()) return;
        const float fb = 0.70f + 0.28f * p (0), damp = 0.05f + 0.6f * p (1), mix = p (2), width = p (3);
        for (int i = 0; i < n; ++i)
        {
            const float l = io[0][i], r = nch > 1 ? io[1][i] : l;
            const float input = (l + r) * 0.015f;
            float out[2];
            for (int c = 0; c < 2; ++c)
            {
                float acc = 0;
                for (size_t k = 0; k < 8; ++k)
                {
                    auto& b = combs[(size_t) c][k];
                    size_t& p0 = cpos[(size_t) c][k];
                    const float y = b[p0];
                    float& z = lp[(size_t) c][k];
                    z = y * (1 - damp) + z * damp;
                    b[p0] = sane (input + z * fb);
                    if (++p0 == b.size()) p0 = 0; // sin división por muestra
                    acc += y;
                }
                for (size_t k = 0; k < 4; ++k)
                {
                    auto& b = aps[(size_t) c][k];
                    size_t& p0 = apos[(size_t) c][k];
                    const float bo = b[p0];
                    b[p0] = sane (acc + bo * 0.5f);
                    acc = bo - acc;
                    if (++p0 == b.size()) p0 = 0; // sin división por muestra
                }
                out[c] = acc;
            }
            const float wl = out[0] * (0.5f + 0.5f * width) + out[1] * (0.5f - 0.5f * width);
            const float wr = out[1] * (0.5f + 0.5f * width) + out[0] * (0.5f - 0.5f * width);
            io[0][i] = sane (l * (1 - mix) + wl * 3.0f * mix);
            if (nch > 1) io[1][i] = sane (r * (1 - mix) + wr * 3.0f * mix);
        }
    }

private:
    std::array<std::array<std::vector<float>, 8>, 2> combs;
    std::array<std::array<std::vector<float>, 4>, 2> aps;
    std::array<std::array<size_t, 8>, 2> cpos {};
    std::array<std::array<size_t, 4>, 2> apos {};
    std::array<std::array<float, 8>, 2> lp {};
};
} // namespace

ProcessorPtr createEffect (const std::string& typeId)
{
    if (typeId == "cs.filter") return std::make_shared<FilterFx>();
    if (typeId == "cs.eq") return std::make_shared<EqFx>();
    if (typeId == "cs.saturation") return std::make_shared<SaturationFx>();
    if (typeId == "cs.compressor") return std::make_shared<CompressorFx>();
    if (typeId == "cs.delay") return std::make_shared<DelayFx>();
    if (typeId == "cs.reverb") return std::make_shared<ReverbFx>();
    return nullptr;
}

std::vector<std::string> effectTypes() { return { "cs.gain", "cs.filter", "cs.eq", "cs.saturation", "cs.compressor", "cs.delay", "cs.reverb" }; }
} // namespace cs
