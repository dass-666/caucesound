// Misión 0.5: trabajos offline y VocalShift como procesado no destructivo.
// Lo que estas pruebas demuestran (con audio sintético, no con una voz real: la naturalidad se juzga escuchando):
//  - el original nunca se modifica; el resultado es otra fuente y deshacer lo quita;
//  - la latencia del motor queda compensada a la muestra;
//  - cancelar, cerrar el proyecto o cambiar de proyecto no deja archivos temporales ni toca otro proyecto;
//  - guardar, reabrir, comparar A/B, reprocesar, descartar y exportar funcionan.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Jobs.h"
#include "cs/Processor.h"
#include "cs/Session.h"
#include "cs/VocalBridge.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <random>

#include <chrono>
#include <thread>

using namespace cs;
using nlohmann::json;

namespace
{
struct Fx
{
    Engine engine;
    Session session { engine };
    std::string dir;
    explicit Fx (const std::string& name) { dir = cstest::tempDir (name); engine.prepare (48000, 512); }
    json cmd (const json& q) { return json::parse (handleCommand (session, engine, q.dump())); }
};

// «Voz» sintética: fuente glotal (armónicos de 150-220 Hz con vibrato) con sílabas separadas por silencios y un
// soplo de ruido al empezar cada una (consonante). No es una voz, pero tiene tono, ataques y pausas.
AudioData voice (double seconds, int channels = 1, float level = 0.5f, int rate = 48000)
{
    AudioData a;
    a.sampleRate = rate;
    a.allocate (channels, (Frame) (seconds * rate));
    double phase = 0;
    uint32_t rng = 12345;
    for (Frame i = 0; i < a.numFrames(); ++i)
    {
        const double t = (double) i / rate;
        const double syl = std::fmod (t, 0.5);          // una sílaba cada 0,5 s: 0,38 s de voz y 0,12 s de pausa
        const double f0 = 165.0 + 30.0 * std::sin (t * 1.3) + 3.0 * std::sin (t * 2 * 3.14159265 * 5.5);
        phase += f0 / rate;
        double v = 0;
        for (int h = 1; h <= 14; ++h) v += std::sin (2 * 3.14159265358979 * phase * h) / h * (h == 4 || h == 9 ? 2.2 : 1.0);
        double env = syl < 0.38 ? std::min (1.0, syl / 0.02) * std::min (1.0, (0.38 - syl) / 0.05) : 0.0;
        rng = rng * 1664525u + 1013904223u;
        const double noise = ((double) (rng >> 8) / 8388608.0 - 1.0) * (syl < 0.03 ? 0.6 : 0.0);
        const float s = (float) ((v * 0.35 * env + noise * 0.3) * level);
        for (int c = 0; c < channels; ++c) a.channels[(size_t) c][(size_t) i] = c == 0 ? s : s * 0.8f;
    }
    return a;
}

std::string writeVoice (const std::string& path, double seconds, int channels = 1, float level = 0.5f)
{
    std::string err;
    const AudioData a = voice (seconds, channels, level);
    if (! writeWav (path, a, WavFormat::Float32, err)) cstest::fail (__FILE__, __LINE__, err);
    return path;
}

std::string fileBytes (const std::string& path) { std::string s; readFile (path, s); return s; }

int tempFiles (const std::string& projectDir)
{
    int n = 0;
    for (const auto& name : listDir (projectDir + "/audio")) if (name.size() > 7 && name.compare (name.size() - 7, 7, ".vs.tmp") == 0) ++n;
    return n;
}

json waitJob (Fx& f, const std::string& jobId)
{
    for (int i = 0; i < 60000; ++i)
    {
        const json j = f.cmd ({ { "cmd", "jobStatus" }, { "jobId", jobId } });
        const std::string st = j.value ("state", std::string());
        if (st == "done" || st == "failed" || st == "cancelled") return j;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    return {};
}

const json kUrban = { { "preset", "urban_spatial" }, { "semitones", 7 }, { "character", 65 }, { "magic", 70 }, { "secondVoice", true }, { "secondGainDb", -12 } };

bool allFinite (const AudioData& a)
{
    for (const auto& ch : a.channels) for (float v : ch) if (! std::isfinite (v)) return false;
    return true;
}
float peakOf (const AudioData& a) { float p = 0; for (const auto& ch : a.channels) for (float v : ch) p = std::max (p, std::fabs (v)); return p; }

// Procesa en memoria con el puente y devuelve la salida estéreo.
AudioData run (const AudioData& in, const vocal::Params& p, vocal::Stats* st = nullptr, std::string* err = nullptr)
{
    AudioData out;
    out.sampleRate = in.sampleRate;
    out.channels.assign (2, {});
    const std::string e = vocal::process (in, 0, in.numFrames(), p, [&] (const float* x, int n)
    {
        for (int i = 0; i < n; ++i) { out.channels[0].push_back (x[i * 2]); out.channels[1].push_back (x[i * 2 + 1]); }
        return true;
    }, nullptr, nullptr, st);
    if (err) *err = e; else CHECK_MSG (e.empty(), e);
    return out;
}
} // namespace

// --- Cola de trabajos ---------------------------------------------------------------------------------------------
TEST (jobs_run_in_order_report_progress_and_final_states)
{
    JobRunner r;
    std::vector<int> order;
    std::mutex om;
    const auto a = r.submit ("t", "p1", "x", [&] (JobContext& c, std::string& res, std::string&) { c.progress = 0.5f; { std::lock_guard<std::mutex> l (om); order.push_back (1); } res = "{\"v\":1}"; return true; });
    const auto b = r.submit ("t", "p1", "y", [&] (JobContext&, std::string&, std::string& e) { { std::lock_guard<std::mutex> l (om); order.push_back (2); } e = "sin espacio"; return false; });
    const auto c = r.submit ("t", "p1", "z", [&] (JobContext&, std::string&, std::string&) -> bool { throw std::runtime_error ("boom"); });
    r.waitIdle();
    CHECK (order.size() == 2 && order[0] == 1 && order[1] == 2);
    JobInfo i;
    CHECK (r.info (a, i) && i.state == "done" && i.result == "{\"v\":1}" && i.progress == 1.0f);
    CHECK (r.info (b, i) && i.state == "failed" && i.error == "sin espacio");
    CHECK (r.info (c, i) && i.state == "failed" && i.error.find ("boom") != std::string::npos); // una excepción no tumba el hilo
    CHECK (r.pending() == 0);
    CHECK (! r.info ("job_999", i));
}

TEST (jobs_cancel_running_queued_by_project_and_reject_when_full)
{
    JobRunner r;
    std::atomic<bool> started { false };
    std::atomic<int> ran { 0 };
    auto slow = [&] (JobContext& c, std::string&, std::string& e) { started = true; ++ran; while (! c.cancel.load()) std::this_thread::sleep_for (std::chrono::milliseconds (1)); e = "cancelado"; return false; };
    auto quick = [&] (JobContext&, std::string&, std::string&) { ++ran; return true; };
    const auto a = r.submit ("t", "pA", "", slow);
    while (! started) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    std::vector<std::string> q;
    for (int i = 0; i < JobRunner::kMaxQueued; ++i) q.push_back (r.submit ("t", i < 2 ? "pA" : "pB", "", quick));
    for (const auto& id : q) CHECK (! id.empty());
    CHECK (r.submit ("t", "pB", "", quick).empty()); // cola llena: se rechaza en vez de crecer sin límite
    r.cancelProject ("pA");                           // el que corre y los dos en cola de pA
    r.waitIdle();
    JobInfo i;
    CHECK (r.info (a, i) && i.state == "cancelled");
    CHECK (r.info (q[0], i) && i.state == "cancelled");
    CHECK (r.info (q[1], i) && i.state == "cancelled");
    CHECK (r.info (q[2], i) && i.state == "done");
    CHECK (r.info (q[3], i) && i.state == "done");
    CHECK (ran == 3); // los cancelados en cola nunca llegaron a ejecutarse
}

TEST (jobs_destructor_cancels_running_work)
{
    std::atomic<bool> started { false }, sawCancel { false };
    {
        JobRunner r;
        r.submit ("t", "p", "", [&] (JobContext& c, std::string&, std::string&) { started = true; while (! c.cancel.load()) std::this_thread::sleep_for (std::chrono::milliseconds (1)); sawCancel = true; return false; });
        while (! started) std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    CHECK (sawCancel);
}

// --- Puente con VocalShift ------------------------------------------------------------------------------------------
TEST (vocalshift_presets_include_urban_spatial_reference)
{
    CHECK (vocal::available());
    const auto ps = vocal::presets();
    CHECK (ps.size() >= 10);
    bool found = false;
    for (const auto& p : ps)
    {
        CHECK (! p.id.empty() && ! p.name.empty());
        vocal::Params back;
        CHECK (vocal::Params::fromJson (p.params.toJson(), back)); // todos los presets pasan la validación de rangos
        if (p.id != "urban_spatial") continue;
        found = true;
        CHECK_NEAR (p.params.semitones, 7, 1e-6);
        CHECK_NEAR (p.params.character, 65, 1e-6);
        CHECK_NEAR (p.params.magic, 70, 1e-6);
        CHECK_NEAR (p.params.secondGainDb, -12, 1e-6);
        CHECK (p.params.secondVoice && p.params.spatial);
        MEASURE ("preset de referencia: %s", p.name.c_str());
    }
    CHECK (found);
    vocal::Params bad;
    CHECK (! vocal::Params::fromJson ("{\"semitones\":\"x\"}", bad));
    CHECK (! vocal::Params::fromJson ("[1]", bad));
    CHECK (vocal::Params::fromJson ("{\"semitones\":999,\"magic\":-5}", bad) && bad.semitones == 24 && bad.magic == 0); // se acota
}

TEST (vocalshift_latency_is_compensated_to_the_sample)
{
    // Segunda voz a -60 dB: la salida es prácticamente la voz original retrasada por la latencia del motor. Si la
    // compensación es correcta, la salida coincide con la entrada sin desplazamiento.
    const AudioData in = voice (2.0);
    vocal::Params p;
    p.secondGainDb = -60;
    p.magic = 0;
    p.spatial = false;
    vocal::Stats st;
    const AudioData out = run (in, p, &st);
    CHECK (st.latencySamples > 0);
    MEASURE ("latencia real del motor: %d muestras = %.1f ms a 48 kHz", st.latencySamples, st.latencySamples * 1000.0 / 48000.0);
    CHECK (out.numFrames() >= in.numFrames());
    int bestLag = 999;
    double best = -1;
    for (int lag = -400; lag <= 400; ++lag)
    {
        double acc = 0;
        for (Frame i = 4800; i < 48000; ++i) acc += (double) in.channels[0][(size_t) i] * out.channels[0][(size_t) (i + lag)];
        if (acc > best) { best = acc; bestLag = lag; }
    }
    MEASURE ("desfase original/procesado tras compensar: %d muestras", bestLag);
    CHECK (bestLag == 0);
    // El ataque de la primera sílaba se conserva: no se pierde el principio de la toma.
    float early = 0;
    for (Frame i = 0; i < 960; ++i) early = std::max (early, std::fabs (out.channels[0][(size_t) i]));
    CHECK (early > 0.02f);
}

TEST (vocalshift_second_voice_level_and_tail)
{
    const AudioData in = voice (2.0);
    auto rmsDiff = [&] (float gainDb)
    {
        vocal::Params p; // por defecto: Urbano Espacial
        p.secondGainDb = gainDb;
        const AudioData out = run (in, p);
        double acc = 0;
        for (Frame i = 0; i < in.numFrames(); ++i) { const double d = out.channels[0][(size_t) i] - in.channels[0][(size_t) i]; acc += d * d; }
        return std::sqrt (acc / (double) in.numFrames());
    };
    const double quiet = rmsDiff (-30), ref = rmsDiff (-12), loud = rmsDiff (0);
    MEASURE ("segunda voz (RMS de lo añadido): -30 dB %.4f, -12 dB %.4f, 0 dB %.4f", quiet, ref, loud);
    CHECK (quiet < ref && ref < loud); // el fader de la segunda voz actúa de verdad
    vocal::Params p;
    vocal::Stats st;
    const AudioData out = run (in, p, &st);
    MEASURE ("cola del motor espacial: %.2f s; pico entrada %.3f, salida %.3f; proceso %.2f s para %.1f s de audio",
             (double) st.tailFrames / 48000.0, st.inputPeak, st.outputPeak, st.seconds, 2.0);
    CHECK (st.tailFrames > 0 && st.tailFrames <= (Frame) (vocal::kMaxTailSeconds * 48000) + 512);
    CHECK (st.nonFiniteEvents == 0);
    CHECK (allFinite (out) && peakOf (out) <= 1.0f);
    // Sin segunda voz no hay cola y la duración es exactamente la de la entrada.
    vocal::Params t;
    t.secondVoice = false;
    const AudioData o2 = run (in, t, &st);
    CHECK (o2.numFrames() == in.numFrames() && st.tailFrames == 0);
}

// Misión 0.6 (A4): evaluación técnica con el preset de referencia sobre la voz de prueba (sílabas con ataque de ruido
// tipo consonante y pausas). Son medidas objetivas: dicen si se conservan los ataques, las consonantes, los silencios
// y los extremos del clip; NO dicen si la voz suena natural, que se juzga escuchando una voz real.
TEST (vocalshift_keeps_attacks_consonants_silences_and_clip_ends_and_is_deterministic)
{
    const AudioData in = voice (3.0);
    vocal::Params p; // Urbano Espacial — Segunda voz
    vocal::Stats st;
    const AudioData out = run (in, p, &st);
    const int sr = 48000;
    auto rmsOf = [&] (const AudioData& a, Frame from, Frame to)
    {
        double acc = 0;
        for (Frame i = from; i < to; ++i) acc += (double) a.channels[0][(size_t) i] * a.channels[0][(size_t) i];
        return std::sqrt (acc / std::max<Frame> (1, to - from));
    };
    // 1) Ataques: cada sílaba empieza cada 0,5 s. El primer instante en que la envolvente pasa de un umbral debe
    //    coincidir con el del original (la voz original va delante; la segunda voz no la adelanta ni la emborrona).
    int worstMs = 0, syll = 0;
    for (int k = 1; k < 6; ++k)
    {
        const Frame t0 = (Frame) (k * 0.5 * sr) - sr / 100;
        auto onset = [&] (const AudioData& a)
        {
            const double ref = rmsOf (a, t0 + sr / 50, t0 + sr / 10) * 0.35;
            for (Frame i = t0; i < t0 + sr / 5; ++i) if (std::fabs (a.channels[0][(size_t) i]) > ref) return i;
            return t0 + sr / 5;
        };
        const int d = (int) std::lround ((double) (onset (out) - onset (in)) * 1000.0 / sr);
        worstMs = std::max (worstMs, std::abs (d));
        ++syll;
    }
    MEASURE ("ataques: %d sílabas, desviación máxima del inicio %d ms respecto al original", syll, worstMs);
    CHECK (worstMs <= 5);
    // 2) Consonantes: el soplo de ruido de los primeros 30 ms de cada sílaba (por encima de 4 kHz) conserva su nivel.
    auto highRms = [&] (const AudioData& a, Frame from, Frame to)
    {
        double acc = 0, prev = a.channels[0][(size_t) from], prev2 = prev; // diferencia segunda: realza agudos
        for (Frame i = from + 2; i < to; ++i)
        {
            const double x = a.channels[0][(size_t) i], d = x - 2 * prev + prev2;
            acc += d * d; prev2 = prev; prev = x;
        }
        return std::sqrt (acc / std::max<Frame> (1, to - from));
    };
    double worstCons = 0;
    for (int k = 1; k < 6; ++k)
    {
        const Frame t0 = (Frame) (k * 0.5 * sr);
        const double dbd = 20.0 * std::log10 (highRms (out, t0, t0 + sr * 3 / 100) / highRms (in, t0, t0 + sr * 3 / 100));
        if (std::fabs (dbd) > std::fabs (worstCons)) worstCons = dbd;
    }
    MEASURE ("consonantes (agudos de los primeros 30 ms de cada sílaba): cambio de nivel máximo %+.1f dB", worstCons);
    CHECK (std::fabs (worstCons) < 3.0);
    // 3) Silencios: en las pausas (últimos 60 ms antes de cada sílaba) solo queda la cola de la segunda voz, bien
    //    por debajo de la voz. Se mide, y se exige que no «rellene» la pausa.
    double pause = 0, body = 0;
    for (int k = 1; k < 6; ++k)
    {
        const Frame t0 = (Frame) (k * 0.5 * sr);
        pause += rmsOf (out, t0 - sr * 6 / 100, t0 - sr / 100) / 5.0;
        body += rmsOf (out, t0 + sr / 20, t0 + sr * 3 / 10) / 5.0;
    }
    MEASURE ("silencios: nivel en las pausas %.1f dB por debajo de la voz (es la cola de la segunda voz y del espacio)", 20.0 * std::log10 (body / pause));
    CHECK (20.0 * std::log10 (body / pause) > 12.0);
    // 4) Extremos del clip: sin chasquido al empezar ni al terminar (la salida arranca y acaba cerca de cero y sin saltos).
    float jump = 0, typical = 0;
    for (Frame i = 1; i < out.numFrames(); ++i) typical = std::max (typical, std::fabs (out.channels[0][(size_t) i] - out.channels[0][(size_t) (i - 1)]));
    const Frame n = out.numFrames();
    const float first = std::fabs (out.channels[0][0]), last = std::fabs (out.channels[0][(size_t) (n - 1)]);
    for (Frame i = n - 480; i < n; ++i) jump = std::max (jump, std::fabs (out.channels[0][(size_t) i] - out.channels[0][(size_t) (i - 1)]));
    const float firstIn = std::fabs (in.channels[0][0]);
    MEASURE ("extremos: primera muestra %.4f (la del original es %.4f), última %.5f, mayor salto en los últimos 10 ms %.5f (mayor salto de toda la toma %.3f)", first, firstIn, last, jump, typical);
    CHECK (std::fabs (first - firstIn) < 0.02f && last < 0.005f && jump < 0.01f);
    // 5) Desfase de la segunda voz: lo añadido (salida − original) con 60 ms es lo mismo que con 0 ms, 60 ms más tarde.
    auto added = [&] (float offsetMs)
    {
        vocal::Params q = p;
        q.offsetMs = offsetMs; q.spatial = false; q.magic = 0;
        const AudioData o = run (in, q);
        std::vector<double> d ((size_t) in.numFrames());
        for (Frame i = 0; i < in.numFrames(); ++i) d[(size_t) i] = o.channels[0][(size_t) i] - in.channels[0][(size_t) i];
        return d;
    };
    const auto d0 = added (0), d60 = added (60);
    int bestLag = -1;
    double best = -1e30;
    for (int lag = 0; lag <= sr / 10; lag += 4)
    {
        double acc = 0;
        for (size_t i = (size_t) sr / 4; i + (size_t) sr / 4 < d0.size(); i += 2) acc += d0[i] * d60[i + (size_t) lag];
        if (acc > best) { best = acc; bestLag = lag; }
    }
    MEASURE ("desfase de la segunda voz: pedir 60 ms la retrasa %.1f ms (medido por correlación)", bestLag * 1000.0 / sr);
    CHECK (std::abs (bestLag * 1000.0 / sr - 60.0) < 3.0);
    // 6) Reprocesar con los mismos ajustes da exactamente el mismo audio (por eso la prueba rápida puede reutilizarse).
    const AudioData again = run (in, p);
    CHECK (again.numFrames() == out.numFrames());
    float diff = 0;
    for (Frame i = 0; i < std::min (again.numFrames(), out.numFrames()); ++i) diff = std::max (diff, std::fabs (again.channels[0][(size_t) i] - out.channels[0][(size_t) i]));
    MEASURE ("reprocesado con los mismos ajustes: diferencia máxima %.1e", diff);
    CHECK (diff == 0.0f);
}

TEST (vocalshift_edge_inputs_silence_low_level_extremes_short_stereo)
{
    vocal::Params p;
    vocal::Stats st;
    // Silencio: sale silencio, sin NaN.
    AudioData sil;
    sil.allocate (1, 48000);
    AudioData o = run (sil, p, &st);
    CHECK (allFinite (o) && peakOf (o) < 1e-4f);
    // Señal muy baja (-60 dBFS): no explota por la nivelación automática.
    o = run (voice (1.5, 1, 0.001f), p, &st);
    CHECK (allFinite (o) && peakOf (o) < 0.05f);
    MEASURE ("entrada a -60 dBFS: pico de salida %.5f", peakOf (o));
    // Extremos de todos los controles.
    for (float semi : { -24.0f, 24.0f })
    {
        vocal::Params e;
        e.semitones = semi; e.character = 100; e.magic = 100; e.secondGainDb = 6; e.radical = true;
        e.formantOffset = semi > 0 ? 12.0f : -12.0f; e.brightnessOffset = 12; e.pan = 100; e.offsetMs = 100;
        o = run (voice (1.0, 2, 0.9f), e, &st);
        CHECK (allFinite (o) && peakOf (o) <= 1.0f && st.nonFiniteEvents == 0);
    }
    // Clip muy corto (menos que la latencia) y estéreo.
    AudioData tiny = voice (0.01, 2);
    o = run (tiny, p, &st);
    CHECK (o.numFrames() >= tiny.numFrames() && allFinite (o));
    // Rangos no válidos y tomas demasiado largas se rechazan sin procesar.
    std::string err;
    AudioData none;
    vocal::process (voice (0.1), 0, 999999, p, [] (const float*, int) { return true; }, nullptr, nullptr, nullptr);
    CHECK (! vocal::process (voice (0.1), 100, 999999, p, [] (const float*, int) { return true; }, nullptr, nullptr, nullptr).empty());
    // Disco lleno simulado: el sumidero falla y el proceso termina con error.
    CHECK (! vocal::process (voice (0.5), 0, 24000, p, [] (const float*, int) { return false; }, nullptr, nullptr, nullptr).empty());
}

// --- Flujo completo en la sesión -----------------------------------------------------------------------------------
TEST (vocalshift_process_is_nondestructive_undoable_and_ab_comparable)
{
    Fx f ("vocal_flow");
    CHECK (f.session.createProject (f.dir + "/p", "x").ok);
    CHECK (f.session.addTrack ("Voz").ok);
    const std::string trk = f.session.model().tracks.back().id;
    const auto imp = f.session.importWav (writeVoice (f.dir + "/voz.wav", 3.0), trk, 48000);
    CHECK (imp.ok);
    const std::string clip = imp.id;
    ProjectModel before = f.session.model();
    const ClipModel c0 = *before.findClip (clip);
    const std::string origFile = f.dir + "/p/" + before.findSource (c0.sourceId)->file;
    const std::string origBytes = fileBytes (origFile);
    CHECK (origBytes.size() > 1000);

    const json started = f.cmd ({ { "cmd", "vocalProcess" }, { "clipId", clip }, { "params", kUrban } });
    CHECK_MSG (started.value ("ok", false), started.dump());
    // El mismo clip no se procesa dos veces a la vez.
    const json again = f.cmd ({ { "cmd", "vocalProcess" }, { "clipId", clip }, { "params", kUrban } });
    const json done = waitJob (f, started.value ("jobId", std::string()));
    CHECK_MSG (done.value ("state", std::string()) == "done", done.dump());
    CHECK (! again.value ("ok", true) || waitJob (f, again.value ("jobId", std::string())).value ("state", std::string()) == "done");
    f.session.jobRunner().waitIdle();
    MEASURE ("3 s de voz procesados en %.2f s (x86, Release o la configuración de esta prueba)", done["result"].value ("seconds", 0.0));

    ProjectModel m = f.session.model();
    const ClipModel c1 = *m.findClip (clip);
    CHECK (c1.processed() && c1.procPlugin == "vocalshift" && ! c1.playingOriginal());
    CHECK (c1.origSourceId == c0.sourceId && c1.origOffset == c0.sourceOffset && c1.origLength == c0.length);
    CHECK (c1.sourceId == c1.procSourceId && c1.sourceId != c0.sourceId);
    CHECK (c1.start == c0.start);           // misma posición en el tiempo
    CHECK (c1.length >= c0.length);         // la cola se añade, nada se recorta
    CHECK (m.findSource (c1.procSourceId)->channels == 2);
    CHECK (fileBytes (origFile) == origBytes); // el WAV original no cambió ni un byte
    CHECK (fileExists (f.dir + "/p/" + m.findSource (c1.procSourceId)->file));
    CHECK (tempFiles (f.dir + "/p") == 0);
    vocal::Params stored;
    CHECK (vocal::Params::fromJson (c1.procParams, stored) && stored.semitones == 7 && stored.secondGainDb == -12);

    // A/B: cambia la versión que suena sin mover el clip ni crear pasos de deshacer.
    CHECK (f.cmd ({ { "cmd", "clipVersion" }, { "clipId", clip }, { "original", true } }).value ("ok", false));
    ClipModel ca = *f.session.model().findClip (clip);
    CHECK (ca.playingOriginal() && ca.sourceId == c0.sourceId && ca.length == c0.length && ca.start == c0.start && ca.processed());
    CHECK (f.cmd ({ { "cmd", "clipVersion" }, { "clipId", clip }, { "original", false } }).value ("ok", false));
    CHECK (! f.session.model().findClip (clip)->playingOriginal());

    // Deshacer quita el procesado entero; rehacer lo devuelve.
    CHECK (f.session.undo().ok);
    CHECK (! f.session.model().findClip (clip)->processed() && f.session.model().findClip (clip)->sourceId == c0.sourceId);
    CHECK (f.session.redo().ok);
    CHECK (f.session.model().findClip (clip)->processed());

    // Reprocesar parte del original, no del procesado.
    json p2 = kUrban;
    p2["semitones"] = -5;
    const json s2 = f.cmd ({ { "cmd", "vocalProcess" }, { "clipId", clip }, { "params", p2 } });
    CHECK (waitJob (f, s2.value ("jobId", std::string())).value ("state", std::string()) == "done");
    const ClipModel c2 = *f.session.model().findClip (clip);
    CHECK (c2.origSourceId == c0.sourceId && c2.origLength == c0.length && c2.procSourceId != c1.procSourceId);
    CHECK (fileBytes (origFile) == origBytes);

    // Guardar y reabrir: el clip sigue procesado, suena la versión procesada y el original sigue disponible para A/B.
    CHECK (f.session.save().ok);
    f.session.close();
    LoadReport rep;
    CHECK (f.session.openProject (f.dir + "/p", &rep).ok);
    CHECK (rep.missingSources.empty());
    const ClipModel c3 = *f.session.model().findClip (clip);
    CHECK (c3.processed() && c3.procParams == c2.procParams && c3.sourceId == c2.procSourceId);
    CHECK (json::parse (f.session.stateJson()).at ("missingPlugins").empty());
    CHECK (f.session.setClipVersion (clip, true).ok);  // carga el original bajo demanda
    CHECK (f.session.setClipVersion (clip, false).ok);

    // Exportar con la versión procesada: audio válido y no silencioso.
    long clipped = -1;
    CHECK (f.session.exportMix (f.dir + "/mix.wav", 24, 0, &clipped).ok);
    AudioData mix;
    std::string err;
    CHECK (readWav (f.dir + "/mix.wav", mix, err));
    CHECK (allFinite (mix) && peakOf (mix) > 0.02f);

    // Descartar: vuelve el original (con deshacer). El archivo original sigue intacto.
    CHECK (f.cmd ({ { "cmd", "discardProcess" }, { "clipId", clip } }).value ("ok", false));
    const ClipModel c4 = *f.session.model().findClip (clip);
    CHECK (! c4.processed() && c4.sourceId == c0.sourceId && c4.length == c0.length && c4.sourceOffset == c0.sourceOffset);
    CHECK (f.session.undo().ok);
    CHECK (f.session.model().findClip (clip)->processed());
    CHECK (fileBytes (origFile) == origBytes);
}

// RMS (dB) de los primeros 'frames' de una mezcla exportada.
static double mixRmsDb (Fx& f, const std::string& path, Frame frames)
{
    CHECK (f.session.exportMix (path, 24).ok);
    AudioData mix;
    std::string err;
    CHECK (readWav (path, mix, err));
    double sq = 0;
    long n = 0;
    for (const auto& ch : mix.channels)
        for (Frame i = 0; i < std::min<Frame> (frames, (Frame) ch.size()); ++i) { sq += (double) ch[(size_t) i] * ch[(size_t) i]; ++n; }
    return 10.0 * std::log10 (std::max (1e-12, sq / (double) std::max (1L, n)));
}

// Regresión del defecto de la 0.5: «Igualar volumen» se guardaba en el clip y, si se salía de VocalShift sin pulsar
// «Aceptar», la exportación y el proyecto guardado salían con la versión procesada atenuada. El igualado es solo
// de escucha: nunca cambia lo que se exporta ni lo que se guarda.
TEST (vocalshift_level_match_is_listen_only_and_never_reaches_export_or_the_saved_project)
{
    Fx f ("vocal_match");
    CHECK (f.session.createProject (f.dir + "/p", "x").ok);
    CHECK (f.session.addTrack ("Voz").ok);
    const auto imp = f.session.importWav (writeVoice (f.dir + "/voz.wav", 3.0), f.session.model().tracks.back().id, 48000);
    CHECK (imp.ok);
    const std::string clip = imp.id;
    const Frame len = f.session.model().findClip (clip)->length;
    json p = kUrban;
    p["secondGainDb"] = 0; // un ajuste que cambia el nivel de forma clara
    const json done = waitJob (f, f.cmd ({ { "cmd", "vocalProcess" }, { "clipId", clip }, { "params", p } }).value ("jobId", std::string()));
    CHECK_MSG (done.value ("state", std::string()) == "done", done.dump());
    f.session.jobRunner().waitIdle();
    const double level = done["result"].value ("levelDb", 0.0);
    MEASURE ("la versión procesada mide %+.2f dB respecto a la original (RMS)", level);
    CHECK (std::fabs (level) >= 1.0);
    CHECK (std::fabs (f.session.model().findClip (clip)->procLevelDb - (float) level) < 1e-3f);

    // Lo que se oye en vivo (el mismo camino que el callback de audio): RMS de la región del clip.
    auto liveRmsDb = [&]
    {
        f.engine.stop();
        f.engine.seek (48000);
        f.engine.play();
        std::vector<float> out (512 * 2);
        double sq = 0;
        long n = 0;
        for (Frame done = 0; done < len; done += 512)
        {
            f.engine.process (nullptr, 0, out.data(), 2, 512);
            for (float v : out) { sq += (double) v * v; ++n; }
        }
        f.engine.stop();
        return 10.0 * std::log10 (std::max (1e-12, sq / (double) n));
    };
    const std::string savedBefore = toJson (f.session.model(), false);
    const bool undoBefore = json::parse (f.session.stateJson()).value ("canUndo", false);
    const double liveReal = liveRmsDb();

    // El usuario compara con «Igualar volumen» activo y se va sin pulsar «Aceptar».
    CHECK (f.cmd ({ { "cmd", "clipVersion" }, { "clipId", clip }, { "original", false }, { "match", true } }).value ("ok", false));
    CHECK (json::parse (f.session.stateJson()).value ("listenMatchClip", std::string()) == clip);
    const double liveMatched = liveRmsDb();
    MEASURE ("escucha en vivo: procesada %.2f dB, con el igualado %.2f dB (diferencia %.2f, medida %.2f)", liveReal, liveMatched, liveReal - liveMatched, level);
    CHECK (std::fabs ((liveReal - liveMatched) - level) < 0.3);                 // al escuchar sí se iguala
    CHECK (toJson (f.session.model(), false) == savedBefore);                   // sin cambiar el proyecto
    CHECK (json::parse (f.session.stateJson()).value ("canUndo", false) == undoBefore); // ni crear pasos de deshacer
    const double exportedWhileMatching = mixRmsDb (f, f.dir + "/b1.wav", len);
    CHECK (f.cmd ({ { "cmd", "clipVersion" }, { "clipId", clip }, { "original", false }, { "match", false } }).value ("ok", false));
    const double exportedReal = mixRmsDb (f, f.dir + "/b2.wav", len);
    CHECK (f.cmd ({ { "cmd", "clipVersion" }, { "clipId", clip }, { "original", true } }).value ("ok", false));
    const double exportedOriginal = mixRmsDb (f, f.dir + "/a.wav", len);
    MEASURE ("exportación: original %.2f dB, procesada %.2f dB, procesada con el igualado activo %.2f dB", exportedOriginal, exportedReal, exportedWhileMatching);
    CHECK (std::fabs (exportedWhileMatching - exportedReal) < 0.05);            // el igualado no llega a la exportación
    CHECK (std::fabs ((exportedReal - exportedOriginal) - level) < 0.5);        // y la procesada sale a su nivel real

    // Tampoco se guarda: tras reabrir, nada queda igualado y la exportación sigue siendo la real.
    CHECK (f.cmd ({ { "cmd", "clipVersion" }, { "clipId", clip }, { "original", false }, { "match", true } }).value ("ok", false));
    CHECK (f.session.save().ok);
    CHECK (toJson (f.session.model(), false).find ("\"match\"") == std::string::npos);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (json::parse (f.session.stateJson()).value ("listenMatchClip", std::string ("x")).empty()); // cerrar lo apaga
    CHECK (std::fabs (liveRmsDb() - liveReal) < 0.05);
    CHECK (std::fabs (mixRmsDb (f, f.dir + "/b3.wav", len) - exportedReal) < 0.05);
    CHECK (std::fabs (f.session.model().findClip (clip)->procLevelDb - (float) level) < 1e-3f); // la medida sí se conserva
    CHECK (f.session.model().findClip (clip)->gainDb == 0.0f);                                  // la ganancia del usuario no se toca

    // Un proyecto guardado por la 0.5.0 con "match": true se abre sin igualado (y sin perder nada más).
    f.session.close();
    std::string raw;
    CHECK (readFile (f.dir + "/p/project.json", raw));
    const size_t at = raw.find ("\"levelDb\"");
    CHECK (at != std::string::npos);
    raw.insert (at, "\"match\": true, ");
    std::string err;
    CHECK (atomicWriteFile (f.dir + "/p/project.json", raw, err, true));
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (f.session.model().findClip (clip)->processed());
    CHECK (std::fabs (mixRmsDb (f, f.dir + "/b4.wav", len) - exportedReal) < 0.05);
    CHECK (std::fabs (liveRmsDb() - liveReal) < 0.05);
}

// Reproducción en vivo (camino del callback) de 'frames' desde 'from': canal izquierdo.
static std::vector<float> liveLeft (Fx& f, Frame from, Frame frames)
{
    f.engine.stop();
    f.engine.seek (from);
    f.engine.play();
    std::vector<float> out (512 * 2), left;
    left.reserve ((size_t) frames + 512);
    while ((Frame) left.size() < frames)
    {
        f.engine.process (nullptr, 0, out.data(), 2, 512);
        for (int i = 0; i < 512; ++i) left.push_back (out[(size_t) i * 2]);
    }
    f.engine.stop();
    left.resize ((size_t) frames);
    return left;
}

static double maxDiff (const std::vector<float>& a, const std::vector<float>& b, Frame from, Frame to)
{
    double d = 0;
    for (Frame i = from; i < to; ++i) d = std::max (d, (double) std::fabs (a[(size_t) i] - b[(size_t) i]));
    return d;
}

static int audioFiles (const std::string& projectDir) { return (int) listDir (projectDir + "/audio").size(); }

// Escucha previa (0.6): una región corta se procesa en segundo plano y se oye en su sitio dentro del clip, sin
// tocar el proyecto, sin archivos nuevos y sin repetir el cálculo si nada cambió.
TEST (vocalshift_preview_renders_a_short_region_without_touching_the_project)
{
    Fx f ("vocal_preview");
    CHECK (f.session.createProject (f.dir + "/p", "x").ok);
    CHECK (f.session.addTrack ("Voz").ok);
    const auto imp = f.session.importWav (writeVoice (f.dir + "/voz.wav", 3.0), f.session.model().tracks.back().id, 48000);
    CHECK (imp.ok);
    const std::string clip = imp.id;
    const Frame start = 48000, len = f.session.model().findClip (clip)->length;
    CHECK (f.session.save().ok);
    const std::string saved = toJson (f.session.model(), false);
    const int files = audioFiles (f.dir + "/p");
    const bool canUndo = json::parse (f.session.stateJson()).value ("canUndo", false);
    const auto original = liveLeft (f, start, len);
    long clipped = 0;
    CHECK (f.session.exportMix (f.dir + "/antes.wav", 24, 0, &clipped).ok);

    // Sin prueba no hay nada que escuchar.
    CHECK (! f.cmd ({ { "cmd", "previewListen" }, { "clipId", clip }, { "on", true } }).value ("ok", true));

    const Frame from = 48000, plen = 72000; // 1,5 s a partir del segundo 1 del clip
    const json req = { { "cmd", "vocalPreview" }, { "clipId", clip }, { "params", kUrban }, { "from", from }, { "length", plen } };
    const json started = f.cmd (req);
    CHECK_MSG (started.value ("ok", false) && ! started.value ("cached", true), started.dump());
    const json done = waitJob (f, started.value ("jobId", std::string()));
    CHECK_MSG (done.value ("state", std::string()) == "done", done.dump());
    f.session.jobRunner().waitIdle();
    MEASURE ("prueba de 1,5 s calculada en %.2f s", done["result"].value ("seconds", 0.0));

    // El proyecto no cambió: ni datos, ni deshacer, ni archivos.
    CHECK (toJson (f.session.model(), false) == saved);
    CHECK (json::parse (f.session.stateJson()).value ("canUndo", false) == canUndo);
    CHECK (audioFiles (f.dir + "/p") == files && tempFiles (f.dir + "/p") == 0);
    const json st = json::parse (f.session.stateJson());
    CHECK (st.at ("vocalPreview").value ("clipId", std::string()) == clip && ! st["vocalPreview"].value ("listening", true));

    // Mismos parámetros y región: no se vuelve a calcular.
    const json again = f.cmd (req);
    CHECK (again.value ("ok", false) && again.value ("cached", false) && ! again.contains ("jobId"));

    // Antes de activar la escucha, todo suena como el original.
    CHECK (maxDiff (liveLeft (f, start, len), original, 0, len) < 1e-6);
    CHECK (f.cmd ({ { "cmd", "previewListen" }, { "clipId", clip }, { "on", true } }).value ("ok", false));
    const auto withPreview = liveLeft (f, start, len);
    const Frame edge = 480;
    CHECK (maxDiff (withPreview, original, 0, from) < 1e-6);                          // antes de la región: original exacto
    CHECK (maxDiff (withPreview, original, from + plen, len) < 1e-6);                 // después: original exacto
    CHECK (maxDiff (withPreview, original, from + edge, from + plen - edge) > 0.02);  // dentro: procesado
    // La escucha no llega a la exportación ni al proyecto.
    CHECK (f.session.exportMix (f.dir + "/durante.wav", 24, 0, &clipped).ok);
    CHECK (fileBytes (f.dir + "/durante.wav") == fileBytes (f.dir + "/antes.wav"));
    CHECK (toJson (f.session.model(), false) == saved);
    CHECK (f.cmd ({ { "cmd", "previewListen" }, { "clipId", clip }, { "on", false } }).value ("ok", false));
    CHECK (maxDiff (liveLeft (f, start, len), original, 0, len) < 1e-6);

    // Otros parámetros: se calcula de nuevo. Con la escucha activa, el resultado nuevo sustituye al anterior.
    CHECK (f.cmd ({ { "cmd", "previewListen" }, { "clipId", clip }, { "on", true } }).value ("ok", false));
    json p2 = kUrban;
    p2["semitones"] = -4;
    json req2 = req;
    req2["params"] = p2;
    const json s2 = f.cmd (req2);
    CHECK (s2.value ("ok", false) && ! s2.value ("cached", true));
    CHECK (waitJob (f, s2.value ("jobId", std::string())).value ("state", std::string()) == "done");
    f.session.jobRunner().waitIdle();
    const auto withPreview2 = liveLeft (f, start, len);
    CHECK (maxDiff (withPreview2, withPreview, from + edge, from + plen - edge) > 0.02);
    CHECK (json::parse (f.session.stateJson())["vocalPreview"].value ("listening", false));

    // La prueba se parece a esa misma región del procesado completo (mismo motor, mismos parámetros).
    CHECK (f.cmd (req).value ("ok", false));
    f.session.jobRunner().waitIdle();
    const auto pv = liveLeft (f, start, len);
    const json full = waitJob (f, f.cmd ({ { "cmd", "vocalProcess" }, { "clipId", clip }, { "params", kUrban } }).value ("jobId", std::string()));
    CHECK (full.value ("state", std::string()) == "done");
    f.session.jobRunner().waitIdle();
    CHECK (! json::parse (f.session.stateJson()).contains ("vocalPreview")); // el resultado completo retira la prueba
    const auto processed = liveLeft (f, start, len);
    double ab = 0, aa = 0, bb = 0;
    for (Frame i = from + 4800; i < from + plen - 4800; ++i)
    { ab += (double) pv[(size_t) i] * processed[(size_t) i]; aa += (double) pv[(size_t) i] * pv[(size_t) i]; bb += (double) processed[(size_t) i] * processed[(size_t) i]; }
    const double corr = ab / std::sqrt (std::max (1e-12, aa * bb));
    MEASURE ("parecido entre la prueba y el procesado completo en esa región: %.3f (1 = idénticos)", corr);
    CHECK (corr > 0.8);

    // La prueba de un clip ya procesado parte del original, y al reabrir no queda nada de ella.
    CHECK (f.cmd (req2).value ("ok", false));
    f.session.jobRunner().waitIdle();
    CHECK (json::parse (f.session.stateJson()).contains ("vocalPreview"));
    CHECK (f.session.save().ok);
    CHECK (toJson (f.session.model(), false).find ("vocalPreview") == std::string::npos);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (! json::parse (f.session.stateJson()).contains ("vocalPreview"));
}

TEST (vocalshift_preview_limits_cancel_and_stale_regions_are_safe)
{
    Fx f ("vocal_preview_edges");
    CHECK (f.session.createProject (f.dir + "/p", "x").ok);
    CHECK (f.session.addTrack ("Voz").ok);
    const std::string trk = f.session.model().tracks.back().id;
    const auto imp = f.session.importWav (writeVoice (f.dir + "/larga.wav", 12.0), trk, 0);
    CHECK (imp.ok);
    const std::string clip = imp.id;
    // Región pedida fuera de rango o enorme: se recorta al clip y al máximo de 8 s.
    const json big = f.cmd ({ { "cmd", "vocalPreview" }, { "clipId", clip }, { "params", kUrban }, { "from", 1e30 }, { "length", 5 } });
    CHECK (! big.value ("ok", true)); // número fuera de rango: rechazado con error, sin trabajo
    const json huge = f.cmd ({ { "cmd", "vocalPreview" }, { "clipId", clip }, { "params", kUrban }, { "from", 48000 }, { "length", 900000000 } });
    CHECK_MSG (huge.value ("ok", false), huge.dump());
    const json hd = waitJob (f, huge.value ("jobId", std::string()));
    CHECK (hd.value ("state", std::string()) == "done" && hd["result"].value ("length", 0) == 8 * 48000);
    // Clip inexistente y parámetros inválidos.
    CHECK (! f.cmd ({ { "cmd", "vocalPreview" }, { "clipId", "nope" }, { "params", kUrban } }).value ("ok", true));
    // Mientras se calcula una prueba, ese clip no admite otro procesado; cancelar la deja sin efecto.
    json p = kUrban;
    p["semitones"] = 3;
    const json a = f.cmd ({ { "cmd", "vocalPreview" }, { "clipId", clip }, { "params", p }, { "from", 0 }, { "length", 8 * 48000 } });
    CHECK (a.value ("ok", false));
    const json b = f.cmd ({ { "cmd", "vocalProcess" }, { "clipId", clip }, { "params", p } });
    CHECK (f.cmd ({ { "cmd", "jobCancel" }, { "jobId", a.value ("jobId", std::string()) } }).value ("ok", false));
    const json ad = waitJob (f, a.value ("jobId", std::string()));
    CHECK (ad.value ("state", std::string()) == "cancelled" || ad.value ("state", std::string()) == "done");
    if (b.value ("ok", false)) f.cmd ({ { "cmd", "jobCancel" }, { "jobId", b.value ("jobId", std::string()) } });
    f.session.jobRunner().waitIdle();

    // Recortar el clip deja la prueba obsoleta: aunque la escucha siga activa, suena el clip real recortado.
    const json c2 = f.cmd ({ { "cmd", "vocalPreview" }, { "clipId", clip }, { "params", kUrban }, { "from", 48000 }, { "length", 48000 } });
    if (c2.contains ("jobId")) waitJob (f, c2.value ("jobId", std::string()));
    f.session.jobRunner().waitIdle();
    CHECK (f.cmd ({ { "cmd", "previewListen" }, { "clipId", clip }, { "on", true } }).value ("ok", false));
    CHECK (f.session.trimClip (clip, 24000, 96000).ok);
    const auto trimmedWithStalePreview = liveLeft (f, 0, 144000);
    CHECK (f.cmd ({ { "cmd", "previewListen" }, { "clipId", clip }, { "on", false } }).value ("ok", false));
    CHECK (maxDiff (trimmedWithStalePreview, liveLeft (f, 0, 144000), 0, 144000) < 1e-6);

    // Cerrar el proyecto con una prueba calculándose: sin bloqueo y sin restos.
    const json c3 = f.cmd ({ { "cmd", "vocalPreview" }, { "clipId", clip }, { "params", p }, { "from", 0 }, { "length", 96000 } });
    CHECK (c3.value ("ok", false));
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (! json::parse (f.session.stateJson()).contains ("vocalPreview") && tempFiles (f.dir + "/p") == 0);
}

TEST (vocalshift_processes_only_the_trimmed_region_and_stereo_takes)
{
    Fx f ("vocal_trim");
    CHECK (f.session.createProject (f.dir + "/p", "x").ok);
    CHECK (f.session.addTrack ("Voz").ok);
    const std::string trk = f.session.model().tracks.back().id;
    const auto imp = f.session.importWav (writeVoice (f.dir + "/voz.wav", 3.0, 2), trk, 0);
    CHECK (imp.ok);
    CHECK (f.session.trimClip (imp.id, 24000, 48000).ok); // se queda con [0,5 s, 1,5 s)
    vocal::Params p;
    p.secondVoice = false; // sin cola: la duración debe coincidir exactamente con la región
    const auto r = f.session.vocalProcess (imp.id, p);
    CHECK (r.ok);
    f.session.jobRunner().waitIdle();
    const ClipModel c = *f.session.model().findClip (imp.id);
    CHECK (c.processed() && c.length == 48000 && c.start == 24000 && c.sourceOffset == 0);
    CHECK (c.origOffset == 24000 && c.origLength == 48000);
    // Recortar después «acepta» la versión que suena: deja de ser un clip con dos versiones.
    CHECK (f.session.trimClip (imp.id, 24000, 24000).ok);
    CHECK (! f.session.model().findClip (imp.id)->processed());
}

TEST (vocalshift_cancel_leaves_project_untouched_and_no_temp_files)
{
    Fx f ("vocal_cancel");
    CHECK (f.session.createProject (f.dir + "/p", "x").ok);
    CHECK (f.session.addTrack ("Voz").ok);
    const auto imp = f.session.importWav (writeVoice (f.dir + "/voz.wav", 60.0), f.session.model().tracks.back().id, 0);
    CHECK (imp.ok);
    const std::string beforeJson = toJson (f.session.model());
    const json s = f.cmd ({ { "cmd", "vocalProcess" }, { "clipId", imp.id }, { "params", kUrban } });
    CHECK (s.value ("ok", false));
    const std::string job = s.value ("jobId", std::string());
    // Espera a que haya progreso real y cancela a mitad.
    for (int i = 0; i < 20000; ++i)
    {
        if (f.cmd ({ { "cmd", "jobStatus" }, { "jobId", job } }).value ("progress", 0.0) > 0.02) break;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    const double progress = f.cmd ({ { "cmd", "jobStatus" }, { "jobId", job } }).value ("progress", 0.0);
    CHECK (progress > 0.0 && progress < 1.0);
    CHECK (f.cmd ({ { "cmd", "jobCancel" }, { "jobId", job } }).value ("ok", false));
    const json end = waitJob (f, job);
    CHECK_MSG (end.value ("state", std::string()) == "cancelled", end.dump());
    MEASURE ("cancelado con un %.0f %% de progreso", progress * 100);
    CHECK (tempFiles (f.dir + "/p") == 0);
    CHECK (toJson (f.session.model()) == beforeJson);
    CHECK (! f.session.canUndo() || ! f.session.model().findClip (imp.id)->processed());
}

TEST (vocalshift_result_never_lands_in_another_project)
{
    Fx f ("vocal_switch");
    CHECK (f.session.createProject (f.dir + "/b", "B", 48000, "trap").ok);
    CHECK (f.session.save().ok);
    const std::string bJson = fileBytes (f.dir + "/b/project.json");
    f.session.close();

    CHECK (f.session.createProject (f.dir + "/a", "A").ok);
    CHECK (f.session.addTrack ("Voz").ok);
    const auto imp = f.session.importWav (writeVoice (f.dir + "/voz.wav", 60.0), f.session.model().tracks.back().id, 0);
    CHECK (imp.ok);
    const auto job = f.session.vocalProcess (imp.id, vocal::Params());
    CHECK (job.ok);
    // Cambio de proyecto con el trabajo en curso: abrir B cierra A (cancela y espera) sin bloqueo mutuo.
    const auto t0 = std::chrono::steady_clock::now();
    CHECK (f.session.openProject (f.dir + "/b").ok);
    const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    MEASURE ("cambio de proyecto con un procesado en curso: %.0f ms", ms);
    JobInfo info;
    CHECK (f.session.jobRunner().info (job.id, info) && info.state == "cancelled");
    CHECK (f.session.jobRunner().pending() == 0);
    CHECK (tempFiles (f.dir + "/a") == 0);
    // B no recibió nada: ni clips nuevos ni fuentes.
    const ProjectModel b = f.session.model();
    CHECK (b.name == "B" && b.sources.empty());
    for (const auto& t : b.tracks) CHECK (t.clips.empty());
    CHECK (fileBytes (f.dir + "/b/project.json") == bJson);
    // A quedó guardado sin procesado y con su original.
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/a").ok);
    CHECK (! f.session.model().findClip (imp.id)->processed());
    CHECK (f.session.model().sources.size() == 1);
}

TEST (vocalshift_stale_temp_files_are_removed_on_open_and_missing_original_fails_cleanly)
{
    Fx f ("vocal_stale");
    CHECK (f.session.createProject (f.dir + "/p", "x").ok);
    CHECK (f.session.addTrack ("Voz").ok);
    const auto imp = f.session.importWav (writeVoice (f.dir + "/voz.wav", 1.0), f.session.model().tracks.back().id, 0);
    CHECK (imp.ok);
    const std::string file = f.dir + "/p/" + f.session.model().sources.at (0).file;
    f.session.close();
    // Resto de un cierre inesperado a mitad de un procesado.
    std::string err;
    CHECK (atomicWriteFile (f.dir + "/p/audio/src_dead.wav.vs.tmp", "RIFFbasura", err, false));
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (tempFiles (f.dir + "/p") == 0);
    CHECK (fileExists (file));
    // Original dañado: el proyecto abre, lo señala y procesar falla con un mensaje, sin trabajo en cola.
    f.session.close();
    CHECK (atomicWriteFile (file, "esto no es un wav", err, false));
    LoadReport rep;
    CHECK (f.session.openProject (f.dir + "/p", &rep).ok);
    CHECK (rep.missingSources.size() == 1);
    const auto r = f.session.vocalProcess (imp.id, vocal::Params());
    CHECK (! r.ok && r.error.find ("falta") != std::string::npos);
    CHECK (f.session.jobRunner().pending() == 0);
    CHECK (! f.session.vocalProcess ("clp_noexiste", vocal::Params()).ok);
}

// --- Contrato: plugin ausente y latencia en buses --------------------------------------------------------------------
// Comandos de plugins al azar, con argumentos válidos e inválidos, mientras hay procesados en marcha: nada debe
// colgarse ni romper el proyecto, y la toma original no cambia ni un byte.
TEST (fuzz_plugin_commands_keep_project_valid_and_original_untouched)
{
    Fx f ("vocal_fuzz");
    CHECK (f.session.createProject (f.dir + "/p", "x", 48000, "trap").ok);
    CHECK (f.session.addTrack ("Voz").ok);
    const auto imp = f.session.importWav (writeVoice (f.dir + "/voz.wav", 1.0), f.session.model().tracks.back().id, 0);
    CHECK (imp.ok);
    ProjectModel m0 = f.session.model();
    const std::string origFile = f.dir + "/p/" + m0.findSource (m0.findClip (imp.id)->sourceId)->file;
    const std::string origBytes = fileBytes (origFile);

    static const char* cmds[] = { "vocalProcess", "vocalProcess", "clipVersion", "clipVersion", "discardProcess", "jobStatus", "jobCancel",
                                  "vocalPresets", "soloProposals", "soloMelody", "soloAnalyze", "setHarmony", "undo", "redo", "save",
                                  "trimClip", "splitClip", "moveClip", "setClipGain", "state", "catalog" };
    static const char* keys[] = { "clipId", "params", "original", "match", "jobId", "keyRoot", "minor", "bars", "seed", "type", "character",
                                  "variation", "replaceTrackId", "bpm", "chords", "start", "length", "at", "gainDb", "trackId" };
    std::mt19937 rng (20261112);
    std::vector<std::string> ids, jobs;
    auto value = [&] () -> json
    {
        switch (rng() % 14)
        {
            case 0: case 1: case 2: return ids.empty() ? json ("x") : json (ids[rng() % ids.size()]);
            case 3: return jobs.empty() ? json ("job_x") : json (jobs[rng() % jobs.size()]);
            case 4: return kUrban;
            case 5: return json::object ({ { "semitones", 1e30 }, { "character", -5 }, { "secondVoice", "sí" }, { "secondGainDb", nullptr } });
            case 6: return json (true);
            case 7: return json (false);
            case 8: return json ((int) (rng() % 40) - 10);
            case 9: return json (1e30);
            case 10: return json (nullptr);
            case 11: return json (std::string (500, 'A'));
            case 12: return json::array ({ json::object ({ { "root", 9 }, { "quality", 1 } }), "x", 5 });
            default: return json ((double) (rng() % 96000));
        }
    };
    int ok = 0, rejected = 0, bad = 0, started = 0;
    const int iterations = 500;
    for (int i = 0; i < iterations; ++i)
    {
        if (i % 40 == 39) f.session.jobRunner().waitIdle(); // deja terminar algunos: así también se aplican resultados
        if (i % 10 == 0)
        {
            ids.clear();
            for (const auto& t : f.session.model().tracks) { ids.push_back (t.id); for (const auto& c : t.clips) ids.push_back (c.id); }
        }
        json j;
        j["cmd"] = cmds[rng() % (sizeof cmds / sizeof cmds[0])];
        // La mitad de las veces, un procesado bien formado sobre un clip real: así hay trabajos de verdad en marcha.
        if (j["cmd"] == "vocalProcess" && rng() % 2 == 0)
        {
            // Deshacer puede haber quitado la toma (o su pista): se vuelve a importar la misma grabación.
            std::vector<std::string> clips;
            for (const auto& t : f.session.model().tracks) for (const auto& c : t.clips) clips.push_back (c.id);
            if (clips.empty())
            {
                CHECK (f.session.addTrack ("Voz").ok);
                const auto again = f.session.importWav (f.dir + "/voz.wav", f.session.model().tracks.back().id, 0);
                CHECK (again.ok);
                clips.push_back (again.id);
            }
            j["clipId"] = clips[rng() % clips.size()];
            j["params"] = kUrban;
        }
        else
        {
            const int nargs = (int) (rng() % 5);
            for (int a = 0; a < nargs; ++a) j[keys[rng() % (sizeof keys / sizeof keys[0])]] = value();
        }
        json r;
        try { r = f.cmd (j); } catch (...) { ++bad; std::printf ("    excepción: %s\n", j.dump().substr (0, 200).c_str()); continue; }
        const bool stateLike = j["cmd"] == "state" || j["cmd"] == "catalog";
        if (! r.is_object() || (! stateLike && ! r.contains ("ok"))) { ++bad; continue; }
        if (stateLike) continue;
        (r["ok"] == true ? ok : rejected)++;
        if (r["ok"] == true && r.contains ("jobId")) { jobs.push_back (r.value ("jobId", std::string())); ++started; }
    }
    f.session.jobRunner().waitIdle();
    MEASURE ("%d comandos de plugins al azar: %d aceptados, %d rechazados, %d trabajos lanzados, %d respuestas inválidas", iterations, ok, rejected, started, bad);
    CHECK (bad == 0 && ok > 30 && rejected > 30 && started > 3);
    CHECK (fileBytes (origFile) == origBytes);
    CHECK (tempFiles (f.dir + "/p") == 0);
    const ProjectModel m = f.session.model();
    CHECK_MSG (validate (m).empty(), validate (m));
    CHECK (f.session.save().ok);
    const std::string before = toJson (f.session.model(), false);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (toJson (f.session.model(), false) == before);
}

TEST (missing_plugin_is_reported_and_project_data_is_preserved)
{
    Fx f ("missing_plugin");
    CHECK (f.session.createProject (f.dir + "/p", "x", 48000, "trap").ok);
    CHECK (f.session.save().ok);
    f.session.close();
    // Simula un proyecto guardado por una versión con un plugin que esta compilación no tiene.
    json j = json::parse (fileBytes (f.dir + "/p/project.json"));
    json ins = { { "id", "fx_future" }, { "type", "future.shimmer" }, { "bypass", false }, { "state", "{\"amount\":0.7}" } };
    j["tracks"][0]["inserts"].push_back (ins);
    std::string err;
    CHECK (atomicWriteFile (f.dir + "/p/project.json", j.dump(), err, false));
    CHECK (f.session.openProject (f.dir + "/p").ok);
    const json st = json::parse (f.session.stateJson());
    CHECK (st.at ("missingPlugins").size() == 1 && st["missingPlugins"][0] == "future.shimmer");
    // El proyecto suena (sin ese efecto), se puede editar y al guardar conserva el plugin y su estado.
    CHECK (f.session.setTempo (97, 4).ok);
    CHECK (f.session.save().ok);
    const json saved = json::parse (fileBytes (f.dir + "/p/project.json"));
    bool kept = false;
    for (const auto& i : saved["tracks"][0]["inserts"]) kept |= i.value ("type", std::string()) == "future.shimmer" && i.value ("state", std::string()) == "{\"amount\":0.7}";
    CHECK (kept);
}

TEST (no_internal_effect_adds_latency_so_buses_stay_aligned)
{
    // PDC en buses no está implementada. La regla: addInsert rechaza un efecto con latencia en un bus. Hoy ningún
    // Cauce FX tiene latencia, así que esta prueba fija esa condición: si alguien añade uno, falla y obliga a decidir.
    for (const auto& type : effectTypes())
    {
        auto fx = createProcessor (type);
        CHECK (fx != nullptr);
        fx->prepare (48000, 4096);
        CHECK_MSG (fx->latencySamples() == 0, type);
    }
    Fx f ("bus_latency");
    CHECK (f.session.createProject (f.dir + "/p", "x").ok);
    CHECK (f.session.addTrack ("Bus", "bus").ok);
    CHECK (f.session.addInsert (f.session.model().tracks.back().id, effectTypes().front()).ok);
    // Los plugins offline no son inserciones: no aparecen entre los efectos en vivo.
    for (const auto& type : effectTypes()) CHECK (type.find ("vocal") == std::string::npos);
    const json cat = f.cmd ({ { "cmd", "catalog" } });
    bool vs = false;
    for (const auto& p : cat.at ("plugins")) if (p.value ("id", std::string()) == "vocalshift") { vs = true; CHECK (p.value ("realtime", true) == false && p.value ("kind", std::string()) == "offline"); }
    CHECK (vs);
}

// Escucha continua (0.8): al mover una perilla varias veces seguidas, cada prueba nueva sustituye a la que se estaba
// calculando. No hay errores «ya se está procesando», solo se aplica el último ajuste y el proyecto no cambia.
TEST (vocalshift_continuous_listen_replaces_stale_previews_and_applies_the_last)
{
    Fx f ("vocal_continuous");
    CHECK (f.session.createProject (f.dir + "/p", "x").ok);
    CHECK (f.session.addTrack ("Voz").ok);
    const auto imp = f.session.importWav (writeVoice (f.dir + "/voz.wav", 8.0), f.session.model().tracks.back().id, 0);
    CHECK (imp.ok);
    const std::string clip = imp.id;
    const std::string saved = toJson (f.session.model(), false);
    const Frame len = f.session.model().findClip (clip)->length;
    const Frame plen = 6 * 48000;
    std::vector<std::string> ids;
    const auto t0 = std::chrono::steady_clock::now();
    for (int st : { 2, 4, 6, -3, -5 })
    {
        json p = kUrban;
        p["semitones"] = st;
        const json r = f.cmd ({ { "cmd", "vocalPreview" }, { "clipId", clip }, { "params", p }, { "from", 0 }, { "length", plen } });
        CHECK_MSG (r.value ("ok", false), r.dump());
        ids.push_back (r.value ("jobId", std::string()));
    }
    const json last = waitJob (f, ids.back());
    const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    CHECK_MSG (last.value ("state", std::string()) == "done", last.dump());
    f.session.jobRunner().waitIdle();
    int cancelled = 0;
    for (size_t i = 0; i + 1 < ids.size(); ++i)
    {
        const std::string s = waitJob (f, ids[i]).value ("state", std::string());
        CHECK_MSG (s == "cancelled" || s == "done", s);
        if (s == "cancelled") ++cancelled;
    }
    const json st = json::parse (f.session.stateJson());
    CHECK (st.at ("vocalPreview").at ("params").value ("semitones", 0.0) == -5.0);
    CHECK (toJson (f.session.model(), false) == saved);
    // Lo que suena es la última prueba: igual a pedir directamente esos ajustes.
    CHECK (f.cmd ({ { "cmd", "previewListen" }, { "clipId", clip }, { "on", true } }).value ("ok", false));
    const auto heard = liveLeft (f, 0, len);
    CHECK (f.cmd ({ { "cmd", "previewListen" }, { "clipId", clip }, { "on", false } }).value ("ok", false));
    CHECK (maxDiff (heard, liveLeft (f, 0, len), 4800, plen - 4800) > 0.02);
    MEASURE ("5 cambios seguidos: %d pruebas sustituidas sin terminar, última aplicada %.2f s después del primer cambio (x86)", cancelled, wall);
    MEASURE ("respuesta de una prueba de 6 s: %.2f s de cálculo (x86; en un teléfono de gama media se espera entre 3 y 6 veces más, sin medir)",
             last["result"].value ("seconds", 0.0));
    // Un procesado completo sigue sin poder solaparse con nada.
    const json full = f.cmd ({ { "cmd", "vocalProcess" }, { "clipId", clip }, { "params", kUrban } });
    CHECK (full.value ("ok", false));
    CHECK (! f.cmd ({ { "cmd", "vocalPreview" }, { "clipId", clip }, { "params", kUrban }, { "from", 0 }, { "length", plen } }).value ("ok", true));
    waitJob (f, full.value ("jobId", std::string()));
    f.session.jobRunner().waitIdle();
}
