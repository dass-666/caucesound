// 0.9.2: estudio de VocalShift como inserción en vivo (prueba de concepto OCULTA, no está en el catálogo).
// Lo que demuestran estas pruebas, en x86 y con señal sintética (no en un teléfono ni con voz real):
//  - el efecto declara su latencia (~150 ms) y el PDC por pista del motor la compensa durante la reproducción;
//  - process() no reserva memoria, ni siquiera al mover parámetros en marcha;
//  - el coste de CPU (factor de tiempo real) medido en esta máquina;
//  - el catálogo y addInsert NO lo ofrecen todavía (no se anuncia como disponible).
#include "Test.h"

#include "cs/Engine.h"
#include "cs/Processor.h"
#include "cs/Session.h"
#include "cs/Api.h"
#include "cs/VocalBridge.h"
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace cs;
using nlohmann::json;

namespace
{
constexpr double kRate = 48000.0;

std::shared_ptr<AudioData> tone (double seconds)
{
    // Tono armónico de 196 Hz con vibrato y envolvente por sílabas: una «voz» sintética reconocible por correlación.
    auto a = std::make_shared<AudioData>();
    const Frame n = (Frame) (seconds * kRate);
    a->allocate (1, n);
    double ph = 0;
    for (Frame i = 0; i < n; ++i)
    {
        const double t = (double) i / kRate;
        ph += 2.0 * M_PI * 196.0 * (1.0 + 0.01 * std::sin (2 * M_PI * 5.0 * t)) / kRate;
        double s = 0;
        for (int h = 1; h <= 8; ++h) s += std::sin (ph * h) / h;
        const double env = 0.5 - 0.5 * std::cos (2 * M_PI * std::fmod (t, 0.5) / 0.5);
        a->channels[0][(size_t) i] = (float) (0.25 * s * env);
    }
    return a;
}

std::vector<float> render (const std::shared_ptr<AudioData>& a, const ProcessorPtr& fx, Frame at, Frame frames, int block)
{
    auto g = std::make_unique<RenderGraph>();
    g->keepAlive.push_back (a);
    TrackRT tr;
    tr.slot = 0;
    ClipRT c;
    c.data = a.get(); c.start = at; c.length = a->numFrames(); c.gain = 1.0f;
    tr.clips.push_back (c);
    if (fx) tr.inserts.push_back (fx);
    g->tracks.push_back (tr);
    Engine e;
    e.prepare (kRate, 1024);
    e.publish (std::move (g));
    e.seek (0);
    e.play();
    std::vector<float> out ((size_t) frames * 2), buf ((size_t) block * 2);
    for (Frame done = 0; done < frames; done += block)
    {
        const int n = (int) std::min<Frame> (block, frames - done);
        e.process (nullptr, 0, buf.data(), 2, n);
        std::copy (buf.begin(), buf.begin() + n * 2, out.begin() + (long) done * 2);
    }
    return out;
}

ProcessorPtr neutralPoC()
{
    auto fx = createProcessor ("cs.vocalshift.live");
    if (! fx) return nullptr;
    // Sin cambio de tono, sin segunda voz ni «magia»: la salida debería ser casi la entrada (para medir alineación).
    fx->setParameter (0, 0); fx->setParameter (2, 0); fx->setParameter (3, 0); fx->setParameter (4, -60);
    fx->prepare (kRate, 1024);
    return fx;
}
} // namespace

TEST (vocalshift_live_poc_is_hidden_from_catalog_and_addInsert)
{
    const auto types = effectTypes();
    CHECK (std::find (types.begin(), types.end(), "cs.vocalshift.live") == types.end());
    Engine e;
    Session s (e);
    const std::string dir = "/tmp/cs_test_vs_live_hidden";
    std::filesystem::remove_all (dir); // la carpeta de una ejecución anterior haría fallar createProject
    CHECK (s.createProject (dir + "/p", "vs_live_hidden").ok);
    const auto tr = s.addTrack ("Voz", "audio");
    CHECK (tr.ok);
    const auto r = s.addInsert (tr.id, "cs.vocalshift.live");
    CHECK (! r.ok);
}

TEST (vocalshift_live_poc_declares_latency_and_pdc_keeps_it_aligned)
{
    auto fx = neutralPoC();
    if (! fx) { MEASURE ("VocalShift no está compilado en este binario: prueba omitida"); return; }
    const int L = fx->latencySamples();
    MEASURE ("latencia declarada de la inserción en vivo: %d muestras = %.1f ms", L, L * 1000.0 / kRate);
    CHECK (L > 0 && L <= (int) (0.25 * kRate));
    const auto a = tone (3.0);
    const Frame at = 48000, frames = (Frame) (5 * kRate);
    const auto dry = render (a, nullptr, at, frames, 256);
    for (int block : { 192, 256 })
    {
        const auto wet = render (a, neutralPoC(), at, frames, block);
        // Desfase por correlación en la parte estable del clip (de 1,5 s a 3,5 s del proyecto).
        int bestLag = 9999;
        double best = -1e30;
        for (int lag = -600; lag <= 600; lag += 1)
        {
            double acc = 0;
            for (Frame i = 72000; i < 168000; i += 2) acc += (double) dry[(size_t) i * 2] * wet[(size_t) (i + lag) * 2];
            if (acc > best) { best = acc; bestLag = lag; }
        }
        MEASURE ("bloque %d: desfase seco/procesado durante la reproducción = %d muestras", block, bestLag);
        CHECK (std::abs (bestLag) <= 2);
        // Antes del clip no aparece audio adelantado (el PDC lee el contenido L muestras por delante, sin pre-eco).
        float pre = 0;
        for (Frame i = 0; i < at - 2400; ++i) pre = std::max (pre, std::fabs (wet[(size_t) i * 2]));
        CHECK (pre < 1e-3f);
    }
}

TEST (vocalshift_live_poc_process_does_not_allocate_and_measures_cpu)
{
    auto fx = createProcessor ("cs.vocalshift.live");
    if (! fx) { MEASURE ("VocalShift no está compilado en este binario: prueba omitida"); return; }
    fx->prepare (kRate, 256);
    const auto a = tone (10.0);
    std::vector<float> l (256), r (256);
    float* io[2] = { l.data(), r.data() };
    const Frame n = a->numFrames();
    cstest::allocationCount.store (0);
    cstest::countAllocations.store (true);
    const auto t0 = std::chrono::steady_clock::now();
    int k = 0;
    for (Frame done = 0; done + 256 <= n; done += 256, ++k)
    {
        std::copy_n (a->channels[0].data() + done, 256, l.data());
        std::copy_n (a->channels[0].data() + done, 256, r.data());
        if (k % 200 == 0)
        {
            // setParameter llega desde el hilo de control: no se cuenta. Lo que se mide es process() aplicando el cambio.
            cstest::countAllocations.store (false);
            fx->setParameter (0, (k / 200) % 2 ? 5.0f : 7.0f);
            cstest::countAllocations.store (true);
        }
        fx->process (io, 2, 256);
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    cstest::countAllocations.store (false);
    MEASURE ("reservas de memoria en process(): %ld", cstest::allocationCount.load());
    CHECK (cstest::allocationCount.load() == 0);
    const double rtf = secs / ((double) n / kRate);
    MEASURE ("CPU x86 de esta máquina: %.3f s para 10 s de audio estéreo (factor de tiempo real %.3f = %.1f %% de un núcleo)", secs, rtf, rtf * 100);
    bool finite = true;
    for (float v : l) finite = finite && std::isfinite (v);
    CHECK (finite);
}

// --- Misión 0.10: comportamiento durante la reproducción (sigue oculto y experimental) --------------------------------
namespace
{
// Reproduce un clip con la inserción en vivo: arranca en 'playFrom', y si seekTo >= 0 salta ahí al llegar a 'seekAt'
// frames de salida. Devuelve la salida estéreo intercalada y, en firstCallMs, lo que tardó el primer bloque.
std::vector<float> playLive (const std::shared_ptr<AudioData>& a, const ProcessorPtr& fx, Frame clipAt, Frame playFrom, Frame frames,
                             Frame seekAt = -1, Frame seekTo = -1, double* firstCallMs = nullptr, long* primes = nullptr)
{
    auto g = std::make_unique<RenderGraph>();
    g->keepAlive.push_back (a);
    TrackRT tr;
    tr.slot = 0;
    ClipRT c;
    c.data = a.get(); c.start = clipAt; c.length = a->numFrames(); c.gain = 1.0f;
    tr.clips.push_back (c);
    if (fx) tr.inserts.push_back (fx);
    g->tracks.push_back (tr);
    Engine e;
    e.prepare (kRate, 1024);
    e.publish (std::move (g));
    e.seek (playFrom);
    e.play();
    const int block = 192;
    std::vector<float> out ((size_t) frames * 2), buf ((size_t) block * 2);
    for (Frame done = 0; done < frames; done += block)
    {
        if (seekAt >= 0 && done >= seekAt) { e.seek (seekTo); seekAt = -1; }
        const int n = (int) std::min<Frame> (block, frames - done);
        const auto t0 = std::chrono::steady_clock::now();
        e.process (nullptr, 0, buf.data(), 2, n);
        if (done == 0 && firstCallMs) *firstCallMs = 1000.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::copy (buf.begin(), buf.begin() + n * 2, out.begin() + (long) done * 2);
    }
    if (primes) *primes = e.primeCount();
    return out;
}
double rmsOf (const std::vector<float>& v, Frame from, Frame to)
{
    double s = 0;
    for (Frame i = from; i < to; ++i) s += (double) v[(size_t) i * 2] * v[(size_t) i * 2];
    return std::sqrt (s / (double) std::max<Frame> (1, to - from));
}
} // namespace

// Antes de la 0.10, al pulsar Play en mitad de una frase los primeros 150 ms de la pista procesada salían en silencio
// (la inserción estaba vacía y el PDC ya leía por delante). Ahora el motor ceba la inserción al arrancar.
TEST (vocalshift_live_play_in_the_middle_of_a_phrase_starts_with_sound)
{
    auto fx = neutralPoC();
    if (! fx) { MEASURE ("VocalShift no está compilado en este binario: prueba omitida"); return; }
    const int L = fx->latencySamples();
    const auto a = tone (4.0);
    // Referencia: reproducción continua desde el principio. Prueba: Play directamente en el segundo 1,1 (en plena sílaba).
    const Frame from = (Frame) (1.1 * kRate), span = (Frame) (1.0 * kRate);
    const auto ref = playLive (a, neutralPoC(), 0, 0, from + span);
    double firstMs = 0;
    long primes = 0;
    const auto got = playLive (a, fx, 0, from, span, -1, -1, &firstMs, &primes);
    CHECK (primes == 1);
    const double refStart = rmsOf (ref, from, from + L), gotStart = rmsOf (got, 0, L);
    MEASURE ("primeros %d ms tras Play: nivel %.1f dB frente a %.1f dB de la reproducción continua", (int) (L * 1000 / kRate), 20 * std::log10 (gotStart + 1e-9), 20 * std::log10 (refStart + 1e-9));
    CHECK (gotStart > 0.5 * refStart); // antes: silencio
    // Tras el arranque, la salida coincide con la reproducción continua (misma alineación).
    double err = 0, sig = 0;
    for (Frame i = (Frame) (0.3 * kRate); i < span; ++i) { const double d = got[(size_t) i * 2] - ref[(size_t) (from + i) * 2]; err += d * d; sig += (double) ref[(size_t) (from + i) * 2] * ref[(size_t) (from + i) * 2]; }
    MEASURE ("diferencia con la reproducción continua a partir de 300 ms: %.1f dB", 10 * std::log10 (err / sig + 1e-12));
    CHECK (err < 0.05 * sig);
    MEASURE ("coste del primer bloque (ceba %d ms de audio): %.2f ms en x86-64; un bloque de 192 frames dura 4,0 ms. SIN MEDIR EN ARM", (int) ((L + std::min (L, 12000)) * 1000 / kRate), firstMs);
}

// Saltar durante la reproducción a una zona sin audio: no debe sonar una cola de 150 ms del sitio anterior.
TEST (vocalshift_live_seek_does_not_replay_audio_from_the_previous_position)
{
    auto fx = neutralPoC();
    if (! fx) { MEASURE ("VocalShift no está compilado en este binario: prueba omitida"); return; }
    const auto a = tone (2.0);
    const Frame seekAt = (Frame) (1.0 * kRate), total = (Frame) (2.0 * kRate);
    long primes = 0;
    const auto out = playLive (a, fx, 0, 0, total, seekAt, (Frame) (10.0 * kRate), nullptr, &primes); // el segundo 10 está vacío
    CHECK (primes == 2); // arranque y salto
    const Frame s0 = ((seekAt + 191) / 192) * 192;
    const double before = rmsOf (out, s0 - 9600, s0), after = rmsOf (out, s0, s0 + 9600);
    MEASURE ("nivel antes del salto %.1f dB; en los 200 ms posteriores %.1f dB", 20 * std::log10 (before + 1e-9), 20 * std::log10 (after + 1e-9));
    CHECK (before > 0.01);
    CHECK (after < 1e-4);
    // La exportación offline tampoco pierde el principio de la pista.
    Engine e;
    e.prepare (kRate, 1024);
    auto g = std::make_unique<RenderGraph>();
    g->keepAlive.push_back (a);
    TrackRT tr; tr.slot = 0;
    ClipRT c; c.data = a.get(); c.start = 0; c.length = a->numFrames(); c.gain = 1.0f;
    tr.clips.push_back (c);
    tr.inserts.push_back (neutralPoC());
    g->tracks.push_back (tr);
    e.publish (std::move (g));
    const Frame from = 12000, n = 24000; // exportar desde 0,25 s (en mitad de una sílaba)
    std::vector<float> l ((size_t) n), r ((size_t) n);
    CHECK (e.renderOffline (from, l.data(), r.data(), n));
    double head = 0, dryHead = 0;
    for (Frame i = 0; i < 4800; ++i) { head += (double) l[(size_t) i] * l[(size_t) i]; const float d = a->channels[0][(size_t) (from + i)]; dryHead += (double) d * d; }
    MEASURE ("exportación desde mitad de frase: primeros 100 ms a %.1f dB del original", 10 * std::log10 (head / dryHead + 1e-12));
    CHECK (head > 0.25 * dryHead);
}

// Cambiar parámetros mientras suena: se mide el mayor salto entre muestras consecutivas alrededor del cambio frente al
// mayor salto de la señal estable. Un clic es un salto varias veces mayor. Esto detecta clics, no dice si el cambio
// «suena natural»: eso hay que escucharlo.
TEST (vocalshift_live_parameter_changes_during_playback_do_not_click)
{
    auto probe = createProcessor ("cs.vocalshift.live");
    if (! probe) { MEASURE ("VocalShift no está compilado en este binario: prueba omitida"); return; }
    struct Change { const char* name; int index; float from, to; };
    const Change changes[] = { { "tono 0 -> +7", 0, 0, 7 }, { "tono +7 -> -5", 0, 7, -5 }, { "segunda voz apagada -> encendida", 3, 0, 1 },
                               { "nivel 2.ª voz -12 -> 0 dB", 4, -12, 0 }, { "magia 0 -> 100", 2, 0, 100 }, { "carácter 20 -> 90", 1, 20, 90 } };
    const auto a = tone (4.0);
    for (const auto& ch : changes)
    {
        auto fx = createProcessor ("cs.vocalshift.live");
        fx->setParameter (ch.index, ch.from);
        fx->prepare (kRate, 256);
        std::vector<float> l (256), r (256), out;
        float* io[2] = { l.data(), r.data() };
        const Frame n = a->numFrames(), at = (Frame) (2.0 * kRate);
        for (Frame done = 0; done + 256 <= n; done += 256)
        {
            std::copy_n (a->channels[0].data() + done, 256, l.data());
            std::copy_n (a->channels[0].data() + done, 256, r.data());
            if (done <= at && at < done + 256) fx->setParameter (ch.index, ch.to);
            fx->process (io, 2, 256);
            out.insert (out.end(), l.begin(), l.end());
        }
        auto maxStep = [&] (Frame from, Frame to) { float m = 0; for (Frame i = from + 1; i < to; ++i) m = std::max (m, std::fabs (out[(size_t) i] - out[(size_t) i - 1])); return m; };
        const float steady = std::max (maxStep ((Frame) (1.0 * kRate), (Frame) (1.9 * kRate)), maxStep ((Frame) (3.0 * kRate), (Frame) (3.9 * kRate)));
        const float around = maxStep (at - 256, at + (Frame) (0.4 * kRate));
        bool finite = true;
        for (float v : out) finite = finite && std::isfinite (v);
        MEASURE ("%s: salto máximo alrededor del cambio %.4f frente a %.4f en régimen estable (x%.1f)", ch.name, around, steady, around / std::max (steady, 1e-6f));
        CHECK (finite);
        CHECK_MSG (around < 3.0f * steady + 0.02f, std::string ("clic al cambiar: ") + ch.name);
    }
}

// Protección contra sobrecarga: con el umbral a cero (simula un teléfono que no puede con el efecto) la inserción pasa
// a la señal seca, alineada y sin clic; al volver a pulsar Play (reset) se rearma.
TEST (vocalshift_live_overload_protection_falls_back_to_aligned_dry_signal)
{
    auto fx = createProcessor ("cs.vocalshift.live");
    if (! fx) { MEASURE ("VocalShift no está compilado en este binario: prueba omitida"); return; }
    fx->setParameter (0, 7);
    fx->prepare (kRate, 256);
    const int L = fx->latencySamples();
    const auto a = tone (3.0);
    std::vector<float> l (256), r (256), out;
    float* io[2] = { l.data(), r.data() };
    const Frame n = a->numFrames(), tripAt = (Frame) (1.0 * kRate);
    CHECK (! vocal::liveInsertOverloaded (fx.get()));
    for (Frame done = 0; done + 256 <= n; done += 256)
    {
        if (done >= tripAt) vocal::setLiveOverloadThreshold (0.0);
        std::copy_n (a->channels[0].data() + done, 256, l.data());
        std::copy_n (a->channels[0].data() + done, 256, r.data());
        fx->process (io, 2, 256);
        out.insert (out.end(), l.begin(), l.end());
    }
    vocal::setLiveOverloadThreshold (0.5);
    CHECK (vocal::liveInsertOverloaded (fx.get()));
    // Un segundo después del disparo la salida es exactamente la entrada retrasada L muestras.
    double err = 0, sig = 0;
    for (Frame i = (Frame) (2.2 * kRate); i < (Frame) (2.9 * kRate); ++i) { const double d = out[(size_t) i] - a->channels[0][(size_t) (i - L)]; err += d * d; sig += (double) out[(size_t) i] * out[(size_t) i]; }
    MEASURE ("tras la protección: diferencia con la señal seca retrasada %d muestras = %.1f dB", L, 10 * std::log10 (err / (sig + 1e-12) + 1e-12));
    CHECK (err < 1e-6 * sig);
    // El paso a seco no hace clic.
    float steady = 0, around = 0;
    for (Frame i = (Frame) (0.5 * kRate) + 1; i < (Frame) (0.9 * kRate); ++i) steady = std::max (steady, std::fabs (out[(size_t) i] - out[(size_t) i - 1]));
    for (Frame i = tripAt + 1; i < tripAt + (Frame) (0.3 * kRate); ++i) around = std::max (around, std::fabs (out[(size_t) i] - out[(size_t) i - 1]));
    MEASURE ("salto máximo al pasar a seco %.4f frente a %.4f estable", around, steady);
    CHECK (around < 3.0f * steady + 0.02f);
    fx->reset();
    CHECK (! vocal::liveInsertOverloaded (fx.get()));
}

// El interruptor experimental: apagado, nada cambia (el efecto sigue siendo «desconocido»); encendido, solo pistas de
// audio y un máximo de instancias. La medición para el teléfono devuelve cifras con sentido.
TEST (vocalshift_live_experimental_switch_limits_tracks_and_instances_and_bench_reports)
{
    if (! createProcessor ("cs.vocalshift.live")) { MEASURE ("VocalShift no está compilado en este binario: prueba omitida"); return; }
    Engine e; e.prepare (48000, 1024);
    Session s (e);
    const std::string dir = cstest::tempDir ("vlive_exp");
    std::filesystem::remove_all (dir);
    CHECK (s.createProject (dir + "/p", "x", 48000, "trap").ok);
    auto cmd = [&] (const json& q) { return json::parse (handleCommand (s, e, q.dump())); };
    std::string audio, synth;
    for (const auto& t : s.model().tracks) { if (t.kind == "audio") audio = t.id; if (t.kind == "synth") synth = t.id; }
    CHECK (! cmd ({ { "cmd", "addInsert" }, { "trackId", audio }, { "type", "cs.vocalshift.live" } }).value ("ok", true)); // apagado
    CHECK (cmd ({ { "cmd", "setExperimental" }, { "liveVocal", true } }).value ("liveVocal", false));
    CHECK (! cmd ({ { "cmd", "addInsert" }, { "trackId", synth }, { "type", "cs.vocalshift.live" } }).value ("ok", true)); // no es de audio
    CHECK (cmd ({ { "cmd", "addInsert" }, { "trackId", audio }, { "type", "cs.vocalshift.live" } }).value ("ok", false));
    CHECK (cmd ({ { "cmd", "addInsert" }, { "trackId", audio }, { "type", "cs.vocalshift.live" } }).value ("ok", false));
    const json third = cmd ({ { "cmd", "addInsert" }, { "trackId", audio }, { "type", "cs.vocalshift.live" } });
    CHECK (! third.value ("ok", true));
    CHECK (third.value ("error", std::string()).find ("máximo") != std::string::npos);
    // El catálogo que lee la app sigue sin ofrecerlo.
    const std::string cat = handleCommand (s, e, R"({"cmd":"catalog"})");
    CHECK (cat.find ("cs.vocalshift.live") == std::string::npos);
    const json b = cmd ({ { "cmd", "benchLiveVocal" }, { "seconds", 2 } });
    CHECK (b.value ("ok", false));
    MEASURE ("benchLiveVocal en esta máquina: %s", b.dump().c_str());
    CHECK (b.value ("cpuPercentOfOneCore", 0.0) > 0.0 && b.value ("startBurstMs", 0.0) > 0.0);
    CHECK (std::fabs (b.value ("latencyMs", 0.0) - 150.0) < 1.0);
    s.close();
}
