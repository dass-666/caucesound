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
