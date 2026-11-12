// Pruebas de Cauce FX: estabilidad, comportamiento medible, independencia del tamaño de bloque, tiempo real
// (sin reservas) e integración con la sesión (persistencia, deshacer, bypass, exportación con colas).
#include "Test.h"

#include "cs/Api.h"
#include "cs/Engine.h"
#include "cs/Instruments.h"
#include "cs/Processor.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <random>

using namespace cs;
using nlohmann::json;

namespace
{
constexpr double kSr = 48000;
const char* kTypes[] = { "cs.gain", "cs.filter", "cs.eq", "cs.saturation", "cs.compressor", "cs.delay", "cs.reverb" };

ProcessorPtr make (const std::string& type, std::initializer_list<std::pair<const char*, float>> params = {})
{
    auto p = createProcessor (type);
    if (! p) return nullptr;
    p->prepare (kSr, 1024);
    for (const auto& [id, v] : params) p->setParameter (parameterIndex (*p, id), v);
    return p;
}

// Procesa una señal estéreo (L = R = x) en bloques de tamaño fijo (> 0) o aleatorio (<= 0).
std::vector<float> run (AudioProcessor& p, const std::vector<float>& x, int block, uint32_t seed = 5)
{
    std::vector<float> l (x), r (x);
    std::mt19937 rng (seed);
    size_t done = 0;
    while (done < x.size())
    {
        const int n = (int) std::min<size_t> (block > 0 ? (size_t) block : (size_t) (1 + rng() % 700), x.size() - done);
        float* io[2] = { l.data() + done, r.data() + done };
        p.process (io, 2, n);
        done += (size_t) n;
    }
    return l;
}

std::vector<float> sine (double hz, double seconds, float amp)
{
    std::vector<float> v ((size_t) (seconds * kSr));
    for (size_t i = 0; i < v.size(); ++i) v[i] = amp * (float) std::sin (2 * M_PI * hz * (double) i / kSr);
    return v;
}

float rms (const std::vector<float>& v, size_t from = 0, size_t to = SIZE_MAX)
{
    to = std::min (to, v.size());
    double s = 0;
    for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
    return to > from ? (float) std::sqrt (s / (double) (to - from)) : 0.0f;
}

float db (float ratio) { return 20.0f * std::log10 (std::max (ratio, 1e-9f)); }
} // namespace

TEST (fx_all_effects_stay_finite_and_bounded_with_extreme_params)
{
    std::mt19937 rng (11);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    std::vector<float> noise ((size_t) kSr);
    for (auto& s : noise) s = 0.25f * u (rng); // -12 dBFS de pico: margen para EQ +15 dB y makeup +24 dB
    for (const char* type : kTypes)
    {
        auto p = make (type);
        CHECK_MSG (p != nullptr, type);
        if (! p) continue;
        CHECK (p->latencySamples() == 0);
        const auto ps = p->parameters();
        for (int corner = 0; corner < 3; ++corner) // mínimos, máximos, por defecto
        {
            for (int i = 0; i < (int) ps.size(); ++i)
                p->setParameter (i, corner == 0 ? ps[(size_t) i].minValue : corner == 1 ? ps[(size_t) i].maxValue : ps[(size_t) i].defaultValue);
            p->setParameter (0, std::nanf ("")); // ignorado
            auto out = run (*p, noise, 0);
            float peak = 0;
            bool finite = true;
            for (float s : out) { finite = finite && std::isfinite (s); peak = std::max (peak, std::fabs (s)); }
            CHECK_MSG (finite && peak < 8.0f, std::string (type) + " esquina " + std::to_string (corner) + " pico " + std::to_string (peak));
        }
        // El estado se guarda y se restaura, y un estado inválido no cambia nada.
        auto q = make (type);
        CHECK_MSG (q->loadState (p->saveState()) && q->saveState() == p->saveState(), std::string (type) + " " + p->saveState() + " / " + q->saveState());
        CHECK_MSG (! q->loadState ("{\"no\":") && q->saveState() == p->saveState(), type);
    }
}

TEST (fx_filter_and_eq_shape_the_spectrum)
{
    auto lp = make ("cs.filter", { { "mode", 0 }, { "cutoffHz", 300 }, { "resonance", 0 } });
    auto hi = sine (6000, 0.5, 0.5f);
    const float lpAtt = db (rms (run (*lp, hi, 256), 4800) / rms (hi, 4800));
    auto hp = make ("cs.filter", { { "mode", 1 }, { "cutoffHz", 3000 }, { "resonance", 0 } });
    auto lo = sine (100, 0.5, 0.5f);
    const float hpAtt = db (rms (run (*hp, lo, 256), 4800) / rms (lo, 4800));
    auto eq = make ("cs.eq", { { "lowDb", 12 } });
    auto bass = sine (40, 0.5, 0.1f);
    const float eqGain = db (rms (run (*eq, bass, 256), 9600) / rms (bass, 9600));
    auto flat = make ("cs.eq");
    auto mid = sine (1000, 0.25, 0.3f);
    const float flatGain = db (rms (run (*flat, mid, 256), 2400) / rms (mid, 2400));
    MEASURE ("filtro LP 300 Hz a 6 kHz: %.1f dB; HP 3 kHz a 100 Hz: %.1f dB; EQ graves +12 a 40 Hz: %+.1f dB; EQ plano: %+.2f dB", lpAtt, hpAtt, eqGain, flatGain);
    CHECK (lpAtt < -24 && hpAtt < -24);
    CHECK (eqGain > 10.5f && eqGain < 13.0f);
    CHECK (std::fabs (flatGain) < 0.05f);
}

TEST (fx_compressor_reduces_loud_signals_and_reports_it)
{
    auto c = make ("cs.compressor", { { "thresholdDb", -24 }, { "ratio", 4 }, { "attackMs", 5 }, { "releaseMs", 100 } });
    auto loud = sine (200, 1.0, 0.5f); // -6 dBFS de pico
    auto out = run (*c, loud, 128);
    const float red = db (rms (out, 24000) / rms (loud, 24000));
    const float gr = c->getParameter (-1);
    auto quiet = sine (200, 1.0, 0.01f); // -40 dBFS: por debajo del umbral
    auto c2 = make ("cs.compressor", { { "thresholdDb", -24 }, { "ratio", 4 } });
    const float untouched = db (rms (run (*c2, quiet, 128), 24000) / rms (quiet, 24000));
    MEASURE ("compresor -24 dB 4:1 con seno a -6 dBFS: %.1f dB (medidor %.1f dB); señal a -40 dBFS: %+.2f dB", red, gr, untouched);
    CHECK (red < -8 && red > -16 && gr > 8);
    CHECK (std::fabs (untouched) < 0.1f);
}

TEST (fx_delay_echo_lands_on_the_exact_sample)
{
    auto d = make ("cs.delay", { { "timeMs", 250 }, { "feedback", 0.5f }, { "mix", 0.5f }, { "toneHz", 16000 } });
    std::vector<float> imp ((size_t) kSr, 0.0f);
    imp[100] = 1.0f;
    auto out = run (*d, imp, 0);
    const size_t delay = (size_t) (0.25 * kSr);
    size_t firstEcho = 0;
    for (size_t i = 200; i < out.size(); ++i) if (std::fabs (out[i]) > 0.1f) { firstEcho = i; break; }
    MEASURE ("delay 250 ms: eco en la muestra %zu (esperada %zu), cola declarada %d muestras", firstEcho, 100 + delay, d->tailSamples());
    CHECK (firstEcho == 100 + delay);
    CHECK (std::fabs (out[100 + 2 * delay]) > 0.05f); // segunda repetición por la realimentación
    CHECK (d->tailSamples() > (int) (delay * 5));
}

TEST (fx_reverb_tail_decays_within_declared_length)
{
    auto r = make ("cs.reverb", { { "size", 0.7f }, { "mix", 0.5f } });
    const int tail = r->tailSamples();
    std::vector<float> imp ((size_t) tail + 4800, 0.0f);
    imp[0] = 1.0f;
    auto out = run (*r, imp, 512);
    const float early = rms (out, 2400, 24000), late = rms (out, (size_t) tail - 4800, (size_t) tail);
    MEASURE ("reverb: energía 50-500 ms %.4f, al final de la cola (%d muestras) %.6f (%.0f dB)", early, tail, late, db (late / early));
    CHECK (early > 1e-3f);
    CHECK (db (late / early) < -55);
}

TEST (fx_output_is_identical_for_any_block_size)
{
    std::mt19937 rng (3);
    std::uniform_real_distribution<float> u (-0.5f, 0.5f);
    std::vector<float> sig ((size_t) (kSr / 2));
    for (auto& s : sig) s = u (rng);
    for (const char* type : kTypes)
    {
        auto a = make (type), b = make (type), c = make (type);
        const auto ref = run (*a, sig, 1024);
        const auto r1 = run (*b, sig, 1);
        const auto rr = run (*c, sig, 0, 77);
        CHECK_MSG (rms (ref) > 0.01f, std::string (type) + " salida en silencio: la comparación no probaría nada");
        float diff = 0;
        for (size_t i = 0; i < ref.size(); ++i) diff = std::max ({ diff, std::fabs (ref[i] - r1[i]), std::fabs (ref[i] - rr[i]) });
        CHECK_MSG (diff == 0.0f, std::string (type) + " diferencia " + std::to_string (diff));
    }
}

TEST (fx_process_does_not_allocate)
{
    std::vector<ProcessorPtr> fx;
    for (const char* type : kTypes) fx.push_back (make (type));
    std::vector<float> l (256, 0.25f), r (256, -0.25f);
    std::vector<float> defaults;
    for (auto& p : fx) defaults.push_back (p->parameters()[0].defaultValue);
    float* io[2] = { l.data(), r.data() };
    cstest::allocationCount.store (0);
    cstest::countAllocations.store (true);
    for (int k = 0; k < 2000; ++k)
        for (size_t j = 0; j < fx.size(); ++j)
        {
            if (k % 100 == 0) fx[j]->setParameter (0, defaults[j] * (k % 200 == 0 ? 1.0f : 0.9f)); // cambios en marcha
            fx[j]->process (io, 2, 256);
        }
    cstest::countAllocations.store (false);
    MEASURE ("reservas de memoria en 2000 bloques x 7 efectos: %ld", cstest::allocationCount.load());
    CHECK (cstest::allocationCount.load() == 0);
}

TEST (fx_inserts_in_session_persist_undo_bypass_and_export_with_tail)
{
    const std::string dir = cstest::tempDir ("fx_session");
    Engine engine;
    engine.prepare (kSr, 1024);
    Session s (engine);
    CHECK (s.createProject (dir + "/p", "fx", 48000, "trap").ok);
    std::string synth;
    for (const auto& t : s.model().tracks) if (t.kind == "synth") { synth = t.id; break; }
    CHECK (! s.addInsert (synth, "cs.nope").ok);
    auto add = s.addInsert (synth, "cs.reverb");
    CHECK (add.ok);
    const std::string rev = add.id;
    CHECK (s.setInsertParam (rev, "mix", 0.6f).ok);
    CHECK (! s.setInsertParam (rev, "nope", 1).ok);
    auto eq = s.addInsert (synth, "cs.eq");
    CHECK (eq.ok && s.moveInsert (eq.id, 0).ok);
    CHECK (s.model().findTrack (synth)->inserts[0].id == eq.id);
    // Deshacer el movimiento y rehacerlo.
    CHECK (s.undo().ok && s.model().findTrack (synth)->inserts[0].id == rev);
    CHECK (s.redo().ok && s.model().findTrack (synth)->inserts[0].id == eq.id);
    // Exportación: incluye la cola de la reverb y coincide con la reproducción en vivo.
    const std::string wav = dir + "/fx.wav";
    long clipped = -1;
    CHECK (s.exportMix (wav, 32, 0, &clipped).ok);
    AudioData a;
    std::string err;
    CHECK (readWav (wav, a, err));
    const Frame end = s.model().contentEnd();
    CHECK (a.numFrames() > end + (Frame) kSr); // cola de reverb > 1 s
    CHECK (clipped == 0);
    // Persistencia.
    CHECK (s.save().ok);
    s.close();
    Session s2 (engine);
    CHECK (s2.openProject (dir + "/p").ok);
    auto m2 = s2.model();
    const auto* t = m2.findTrack (synth);
    CHECK (t && t->inserts.size() == 2 && t->inserts[1].type == "cs.reverb");
    if (t && t->inserts.size() == 2)
    {
        const auto st = json::parse (t->inserts[1].state);
        CHECK (std::fabs (st["mix"].get<float>() - 0.6f) < 1e-6f);
    }
    CHECK (s2.setInsertBypass (rev, true).ok && s2.model().findTrack (synth)->inserts[1].bypass);
    CHECK (s2.removeInsert (eq.id).ok && s2.model().findTrack (synth)->inserts.size() == 1);
    // API JSON.
    const auto r = json::parse (handleCommand (s2, engine, json { { "cmd", "addInsert" }, { "trackId", synth }, { "type", "cs.delay" } }.dump()));
    CHECK (r["ok"] == true);
    const auto cat = json::parse (handleCommand (s2, engine, R"({"cmd":"catalog"})"));
    CHECK (cat["effects"].size() == 7);
}

namespace
{
// Efecto de prueba con latencia conocida: retardo puro de L muestras (como un procesador con look-ahead).
class LatencyFx final : public AudioProcessor
{
public:
    explicit LatencyFx (int l) : lat (l) {}
    std::string typeId() const override { return "test.latency"; }
    void prepare (double, int) override { buf.assign ((size_t) lat * 2, 0.0f); reset(); }
    void reset() override { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    int latencySamples() const override { return lat; }
    void process (float* const* io, int nch, int n) override
    {
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                float& slot = buf[(size_t) (w * 2 + c)];
                const float x = c < nch ? io[c][i] : 0.0f;
                const float y = slot;
                slot = x;
                if (c < nch) io[c][i] = y;
            }
            w = (w + 1) % lat;
        }
    }
    int lat;
    std::vector<float> buf;
    int w = 0;
};

std::unique_ptr<RenderGraph> impulseGraph (const std::shared_ptr<AudioData>& a, int latencyB, Frame at)
{
    auto g = std::make_unique<RenderGraph>();
    g->keepAlive.push_back (a);
    for (int t = 0; t < 2; ++t)
    {
        TrackRT tr;
        tr.slot = t;
        ClipRT c;
        c.data = a.get(); c.start = at; c.length = a->numFrames(); c.gain = 1.0f;
        tr.clips.push_back (c);
        if (t == 1 && latencyB > 0) { auto fx = std::make_shared<LatencyFx> (latencyB); fx->prepare (kSr, 1024); tr.inserts.push_back (fx); }
        g->tracks.push_back (tr);
    }
    return g;
}

std::vector<size_t> peaks (const std::vector<float>& lr, float thr)
{
    std::vector<size_t> p;
    for (size_t i = 0; i < lr.size() / 2; ++i) if (std::fabs (lr[i * 2]) > thr) p.push_back (i);
    return p;
}
} // namespace

TEST (pdc_latent_track_stays_aligned_live_with_any_block_and_in_loop)
{
    auto a = std::make_shared<AudioData>();
    a->allocate (1, 4800);
    a->channels[0][0] = 0.5f; // impulso al inicio del clip
    const int L = 7200;       // 150 ms, como VocalShift
    for (int block : { 64, 192, 1000 })
    {
        Engine e;
        e.prepare (kSr, 1024);
        e.publish (impulseGraph (a, L, 48000));
        e.seek (0);
        e.play();
        std::vector<float> out ((size_t) kSr * 2 * 2), buf ((size_t) block * 2);
        for (size_t done = 0; done < out.size() / 2; done += (size_t) block)
        {
            const int n = (int) std::min<size_t> ((size_t) block, out.size() / 2 - done);
            e.process (nullptr, 0, buf.data(), 2, n);
            std::copy (buf.begin(), buf.begin() + n * 2, out.begin() + (long) done * 2);
        }
        const auto p = peaks (out, 0.1f);
        // Ambas pistas suman su impulso en el mismo frame (0,5 + 0,5): una sola muestra no nula.
        CHECK_MSG (p.size() == 1 && p[0] == 48000 && std::fabs (out[48000 * 2] - 1.0f) < 1e-6f,
                   "bloque " + std::to_string (block) + ": " + std::to_string (p.size()) + " picos, primero en " + std::to_string (p.empty() ? 0 : p[0]));
    }
    // Bucle [0, 1 s) con el impulso en 0,5 s: tras varias vueltas, las dos pistas siguen alineadas.
    Engine e;
    e.prepare (kSr, 1024);
    e.publish (impulseGraph (a, L, 24000));
    e.setLoop (true, 0, 48000);
    e.seek (0);
    e.play();
    std::vector<float> out ((size_t) kSr * 4 * 2), buf (512 * 2);
    for (size_t done = 0; done < out.size() / 2; done += 512)
    {
        e.process (nullptr, 0, buf.data(), 2, 512);
        std::copy (buf.begin(), buf.end(), out.begin() + (long) done * 2);
    }
    const auto p = peaks (out, 0.1f);
    bool aligned = p.size() >= 3;
    for (size_t k = 0; k < p.size(); ++k) aligned = aligned && p[k] == 24000 + k * 48000 && std::fabs (out[p[k] * 2] - 1.0f) < 1e-6f;
    MEASURE ("PDC: pista con %d muestras de latencia alineada a la muestra (bloques 64/192/1000 y %zu vueltas de bucle)", L, p.size());
    CHECK (aligned);
}

TEST (pdc_export_matches_live_with_latent_insert)
{
    auto a = std::make_shared<AudioData>();
    a->allocate (1, 4800);
    a->channels[0][0] = 0.5f;
    Engine e;
    e.prepare (kSr, 1024);
    e.publish (impulseGraph (a, 7200, 10000));
    std::vector<float> l ((size_t) kSr), r ((size_t) kSr);
    e.renderOffline (0, l.data(), r.data(), (Frame) kSr);
    size_t at = 0;
    int count = 0;
    for (size_t i = 0; i < l.size(); ++i) if (std::fabs (l[i]) > 0.1f) { at = i; ++count; }
    CHECK (count == 1 && at == 10000 && std::fabs (l[10000] - 1.0f) < 1e-6f);

    // La misma sesión reproducida en vivo (otro motor, bloques de 256) da exactamente la misma señal.
    Engine live;
    live.prepare (kSr, 1024);
    live.publish (impulseGraph (a, 7200, 10000));
    live.seek (0);
    live.play();
    std::vector<float> buf (256 * 2);
    float diff = 0;
    for (size_t done = 0; done < l.size(); done += 256)
    {
        const int n = (int) std::min<size_t> (256, l.size() - done);
        live.process (nullptr, 0, buf.data(), 2, n);
        for (size_t i = 0; i < (size_t) n; ++i)
            diff = std::max ({ diff, std::fabs (buf[i * 2] - l[done + i]), std::fabs (buf[i * 2 + 1] - r[done + i]) });
    }
    MEASURE ("PDC: exportación vs reproducción en vivo, diferencia máxima %g", diff);
    CHECK (diff == 0.0f);
}

// Seek con PDC: lejos del contenido, la alineación es exacta; justo antes (menos de L frames), la pista latente
// pierde ese tramo (limitación documentada: no hay precarga). La prueba fija ambos comportamientos.
TEST (pdc_after_seek_is_aligned_and_documents_the_preroll_gap)
{
    auto a = std::make_shared<AudioData>();
    a->allocate (1, 4800);
    a->channels[0][0] = 0.5f;
    const int L = 7200;
    auto playFrom = [&] (Frame from)
    {
        Engine e;
        e.prepare (kSr, 1024);
        e.publish (impulseGraph (a, L, 48000));
        e.seek (from);
        e.play();
        std::vector<float> out ((size_t) kSr * 2), buf (256 * 2);
        for (size_t done = 0; done < out.size() / 2; done += 256)
        {
            const int n = (int) std::min<size_t> (256, out.size() / 2 - done);
            e.process (nullptr, 0, buf.data(), 2, n);
            std::copy (buf.begin(), buf.begin() + n * 2, out.begin() + (long) done * 2);
        }
        return out;
    };
    const auto far = playFrom (30000); // 18000 frames antes del impulso (> L)
    const auto pf = peaks (far, 0.1f);
    CHECK (pf.size() == 1 && pf[0] == 18000 && std::fabs (far[18000 * 2] - 1.0f) < 1e-6f);
    const auto near = playFrom (46000); // 2000 frames antes (< L): solo suena la pista sin latencia
    const auto pn = peaks (near, 0.1f);
    CHECK (pn.size() == 1 && pn[0] == 2000); // un solo golpe a 0,5: falta el de la pista latente
    float m = 0;
    for (size_t i = 0; i < near.size() / 2; ++i) m = std::max (m, std::fabs (near[i * 2]));
    MEASURE ("PDC tras seek: alineada si el contenido está a más de %d frames; si no, solo suena la pista sin latencia (pico %.2f)", L, m);
    CHECK (std::fabs (m - 0.5f) < 1e-6f);
}
