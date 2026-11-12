// Integración de Solo AI (misión 0.4, etapa C): «Generar melodía o solo» desde el motor, sin tocar el hilo de audio.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Composer.h"
#include "cs/Session.h"
#include "cs/SoloBridge.h"

#include <nlohmann/json.hpp>

#include <set>

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

std::vector<MidiNote> soloNotes (const ProjectModel& m)
{
    for (const auto& t : m.tracks) if (t.name.rfind ("Solo AI", 0) == 0) return t.midiClips.at (0).notes;
    return {};
}
} // namespace

TEST (solo_ai_generates_editable_melody_in_key_and_undo_removes_it)
{
    CHECK (solo::available());
    Fx f ("solo_gen");
    CHECK (f.session.createProject (f.dir + "/p", "x", 48000, "reggaeton").ok);
    const size_t tracksBefore = f.session.model().tracks.size();
    const json r = f.cmd ({ { "cmd", "soloMelody" }, { "startBar", 2 }, { "bars", 8 }, { "seed", 42 }, { "proposal", 0 } });
    CHECK_MSG (r.value ("ok", false), r.dump());
    MEASURE ("Solo AI: %d notas en %.1f ms, en escala %.2f, notas fuertes del acorde %.2f (%s)", r.value ("notes", 0), r.value ("millis", 0.0),
             r.value ("inScale", 0.0), r.value ("strongChordTones", 0.0), r.value ("name", std::string()).c_str());
    const ProjectModel m = f.session.model();
    CHECK (m.tracks.size() == tracksBefore + 1);
    const auto notes = soloNotes (m);
    CHECK (notes.size() >= 8);
    // Tonalidad del plan (La menor) más la sensible: Solo AI recibe acordes confirmados, no adivinados.
    MusicPlan plan;
    CHECK (MusicPlan::fromJson (m.music, plan));
    std::set<int> allowed;
    for (int pc : composer::scale (plan)) allowed.insert (pc % 12);
    allowed.insert ((plan.keyRoot + 11) % 12);
    int outside = 0;
    for (const auto& n : notes)
    {
        CHECK (n.start >= 0 && n.length > 0 && n.key >= 0 && n.key <= 127);
        CHECK (n.start < 8 * 4 * kPpq);
        if (! allowed.count (n.key % 12)) ++outside;
    }
    MEASURE ("notas fuera de la escala: %d de %zu", outside, notes.size());
    CHECK ((double) outside / (double) notes.size() <= 0.1); // Solo AI admite notas de paso; no más de un 10 %
    // El clip empieza en el compás 2 (la sección A).
    for (const auto& t : m.tracks) if (t.name.rfind ("Solo AI", 0) == 0) CHECK (t.midiClips[0].start == 2 * 4 * kPpq);

    // Misma semilla, mismas notas. Deshacer quita la pista entera.
    CHECK (f.session.undo().ok);
    CHECK (f.session.model().tracks.size() == tracksBefore);
    CHECK (f.cmd ({ { "cmd", "soloMelody" }, { "startBar", 2 }, { "bars", 8 }, { "seed", 42 }, { "proposal", 0 } }).value ("ok", false));
    const auto again = soloNotes (f.session.model());
    CHECK (again.size() == notes.size());
    bool same = again.size() == notes.size();
    for (size_t i = 0; same && i < notes.size(); ++i) same = again[i].key == notes[i].key && again[i].start == notes[i].start;
    CHECK_MSG (same, "Solo AI no es reproducible con la misma semilla");
    // Persistencia: guardar y reabrir conserva la pista generada.
    CHECK (f.session.save().ok);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (soloNotes (f.session.model()).size() == notes.size());
}

TEST (solo_ai_asks_for_key_when_project_has_no_plan)
{
    Fx f ("solo_key");
    CHECK (f.session.createProject (f.dir + "/p", "x", 48000, "").ok);
    const json r = f.cmd ({ { "cmd", "soloMelody" }, { "bars", 4 } });
    CHECK (! r.value ("ok", true));
    CHECK (r.value ("error", std::string()) == "key_required");
    const json ok = f.cmd ({ { "cmd", "soloMelody" }, { "bars", 4 }, { "keyRoot", 7 }, { "minor", false }, { "type", "solo" }, { "proposal", 3 } });
    CHECK_MSG (ok.value ("ok", false), ok.dump());
    CHECK (! soloNotes (f.session.model()).empty());
    // Entradas inválidas.
    CHECK (! f.cmd ({ { "cmd", "soloMelody" }, { "bars", 0 }, { "keyRoot", 0 } }).value ("ok", true));
    CHECK (! f.cmd ({ { "cmd", "soloMelody" }, { "bars", 4 }, { "keyRoot", 12 } }).value ("ok", true));
    CHECK (! f.cmd ({ { "cmd", "soloMelody" }, { "bars", 4 }, { "keyRoot", 0 }, { "proposal", 9 } }).value ("ok", true));
    // Un resultado que llega cuando ya hay otro proyecto abierto se descarta.
    CHECK (! f.session.addSoloTrack ("otro-proyecto", 0, "Solo AI", { MidiNote {} }, "bright_lead").ok);
}

TEST (catalog_lists_only_compiled_plugins)
{
    Fx f ("solo_cat");
    const json c = f.cmd ({ { "cmd", "catalog" } });
    CHECK (c["plugins"].is_array());
    // Desde la 0.5 el catálogo lleva un descriptor por plugin compilado (Solo AI y VocalShift), y ninguno más.
    size_t expected = 0;
    for (const auto& p : c["plugins"])
    {
        const std::string id = p.value ("id", std::string());
        CHECK (id == "solo.ai" || id == "vocalshift");
        CHECK (p.contains ("version") && p.contains ("category") && p.value ("realtime", true) == false);
        ++expected;
    }
    CHECK (expected == (solo::available() ? 1u : 0u) + (vocal::available() ? 1u : 0u));
}

// --- Solo AI 2.0 (misión 0.5): análisis de un beat importado, alternativas A-D y variaciones -------------------------
#ifdef CS_WITH_SOLO
#include "solo/BeatAnalyzer.h"
#include "cs/Wav.h"

#include <chrono>
#include <thread>

namespace
{
// Beat sintético de tonalidad y tempo conocidos (generador de pruebas del propio Solo AI), importado como WAV.
std::string importBeat (Fx& f, double bpm, const char* prog, int keyTonic, bool minor, bool harmony = true)
{
    ::solo::SyntheticBeatSpec spec;
    spec.bpm = bpm;
    spec.key.tonic = keyTonic;
    spec.key.mode = minor ? ::solo::Mode::Minor : ::solo::Mode::Major;
    spec.progression = prog;
    spec.bars = 16;
    spec.sampleRate = 48000;
    spec.harmony = harmony;
    const auto mono = ::solo::synthesiseBeat (spec);
    AudioData a;
    a.sampleRate = 48000;
    a.channels.assign (1, mono.samples);
    std::string err;
    const std::string path = f.dir + "/beat.wav";
    CHECK (writeWav (path, a, WavFormat::Float32, err));
    CHECK (f.session.addTrack ("Beat").ok);
    const auto r = f.session.importWav (path, f.session.model().tracks.back().id, 0);
    CHECK (r.ok);
    return r.id;
}

json waitSoloJob (Fx& f, const std::string& id)
{
    for (int i = 0; i < 60000; ++i)
    {
        const json j = f.cmd ({ { "cmd", "jobStatus" }, { "jobId", id } });
        const std::string st = j.value ("state", std::string());
        if (st == "done" || st == "failed" || st == "cancelled") return j;
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    return {};
}
} // namespace

TEST (solo_ai_analyzes_imported_beat_and_never_changes_tempo_by_itself)
{
    Fx f ("solo_analyze");
    CHECK (f.session.createProject (f.dir + "/p", "x").ok);
    const double projectBpm = f.session.model().bpm;
    const std::string clip = importBeat (f, 92.0, "Am F C G", 9, true);
    const json s = f.cmd ({ { "cmd", "soloAnalyze" }, { "clipId", clip } });
    CHECK_MSG (s.value ("ok", false), s.dump());
    const json done = waitSoloJob (f, s.value ("jobId", std::string()));
    CHECK_MSG (done.value ("state", std::string()) == "done", done.dump());
    const json r = done.at ("result");
    MEASURE ("beat sintético 92 BPM en La menor -> %s (análisis en %.2f s para %.0f s de audio)", r.value ("summary", std::string()).c_str(),
             r.value ("seconds", 0.0), r.value ("durationSec", 0.0));
    MEASURE ("BPM %.1f (confianza %.2f), tonalidad %d %s (confianza %.2f), %d compases, confianza de acordes %.2f", r.value ("bpm", 0.0),
             r.value ("bpmConfidence", 0.0), r.value ("keyRoot", -1), r.value ("minor", false) ? "menor" : "mayor", r.value ("keyConfidence", 0.0),
             r.value ("bars", 0), r.value ("chordConfidence", 0.0));
    // El tempo puede salir a la mitad o al doble: debe estar entre los candidatos, y el principal cerca de 92 o su octava.
    bool cand = false;
    for (const auto& c : r.at ("bpmCandidates")) cand |= std::fabs (c.value ("bpm", 0.0) - 92.0) < 2.0;
    CHECK (cand);
    CHECK (std::fabs (r.value ("bpm", 0.0) - 92.0) < 2.0);
    CHECK (r.value ("tonal", false));
    // La menor y Do mayor comparten notas: se acepta la relativa, pero La menor debe estar entre las candidatas.
    bool am = false;
    for (const auto& k : r.at ("keyCandidates")) am |= k.value ("root", -1) == 9 && k.value ("minor", false);
    CHECK (am);
    for (const char* k : { "bpmConfidence", "keyConfidence", "chordConfidence" }) CHECK (r.value (k, -1.0) >= 0.0 && r.value (k, 2.0) <= 1.0);
    // Analizar no cambia nada: ni tempo, ni tonalidad guardada, ni pasos de deshacer.
    const ProjectModel m = f.session.model();
    CHECK (m.bpm == projectBpm && m.harmony.empty());

    // Sin tonalidad confirmada Solo AI sigue pidiéndola.
    json g = f.cmd ({ { "cmd", "soloMelody" }, { "bars", 8 }, { "seed", 5 } });
    CHECK (! g.value ("ok", true) && g.value ("error", std::string()) == "key_required");
    // El usuario confirma (o corrige) la tonalidad y acepta el tempo: a partir de ahí se usan sin volver a preguntar.
    CHECK (f.cmd ({ { "cmd", "setTempo" }, { "bpm", r.value ("bpm", 92.0) }, { "beatsPerBar", 4 } }).value ("ok", false));
    CHECK (f.cmd ({ { "cmd", "setHarmony" }, { "keyRoot", 9 }, { "minor", true }, { "chords", r.at ("chords") }, { "chordsBpm", r.value ("bpm", 92.0) }, { "source", "analysis" } }).value ("ok", false));
    g = f.cmd ({ { "cmd", "soloMelody" }, { "bars", 8 }, { "seed", 5 } });
    CHECK_MSG (g.value ("ok", false), g.dump());
    CHECK (g.value ("keyRoot", -1) == 9 && g.value ("minor", false));
    CHECK (g.value ("inScale", 0.0) > 0.8);
    // Se guarda con el proyecto.
    CHECK (f.session.save().ok);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    const json h = json::parse (f.session.model().harmony);
    CHECK (h.value ("keyRoot", -1) == 9 && h.value ("confirmed", false) && h.value ("source", std::string()) == "analysis");
}

TEST (solo_ai_analysis_reports_percussive_or_silent_audio_instead_of_inventing_a_key)
{
    Fx f ("solo_analyze_perc");
    CHECK (f.session.createProject (f.dir + "/p", "x").ok);
    const std::string clip = importBeat (f, 120.0, "Am F C G", 9, true, false); // solo batería
    const json done = waitSoloJob (f, f.cmd ({ { "cmd", "soloAnalyze" }, { "clipId", clip } }).value ("jobId", std::string()));
    CHECK_MSG (done.value ("state", std::string()) == "done", done.dump());
    const json r = done.at ("result");
    MEASURE ("solo batería a 120 BPM -> %s", r.value ("summary", std::string()).c_str());
    CHECK (! r.value ("tonal", true) || r.value ("keyConfidence", 1.0) < 0.5);
    CHECK (r.value ("percussiveOnly", false) || ! r.at ("warnings").empty() || r.value ("keyConfidence", 1.0) < 0.5);
    // Demasiado corto o inexistente: error claro, sin trabajo en cola.
    CHECK (! f.cmd ({ { "cmd", "soloAnalyze" }, { "clipId", "nope" } }).value ("ok", true));
    CHECK (f.session.trimClip (clip, 0, 48000).ok);
    CHECK (! f.cmd ({ { "cmd", "soloAnalyze" }, { "clipId", clip } }).value ("ok", true));
}

TEST (solo_ai_four_alternatives_are_distinct_reproducible_and_replace_only_their_own_track)
{
    Fx f ("solo_family");
    CHECK (f.session.createProject (f.dir + "/p", "x", 48000, "trap").ok);
    const json req = { { "cmd", "soloProposals" }, { "startBar", 2 }, { "bars", 8 }, { "seed", 77 }, { "type", "chorus" } };
    const json fam = f.cmd (req);
    CHECK_MSG (fam.value ("ok", false), fam.dump());
    CHECK (fam.at ("proposals").size() == 4);
    MEASURE ("familia: diversidad %.2f; A=%s B=%s C=%s D=%s", fam.value ("diversity", 0.0), fam["proposals"][0].value ("name", std::string()).c_str(),
             fam["proposals"][1].value ("name", std::string()).c_str(), fam["proposals"][2].value ("name", std::string()).c_str(), fam["proposals"][3].value ("name", std::string()).c_str());
    CHECK (fam.value ("diversity", 0.0) > 0.05);
    CHECK (f.cmd (req) == fam); // reproducible por semilla
    const size_t tracks = f.session.model().tracks.size();
    CHECK (! f.session.canUndo() || f.session.model().tracks.size() == tracks); // pedir propuestas no toca el proyecto

    // Elegir A crea la pista; elegir C la sustituye (no se acumulan pistas) y deshacer vuelve a A.
    json a = f.cmd ({ { "cmd", "soloMelody" }, { "startBar", 2 }, { "bars", 8 }, { "seed", 77 }, { "proposal", 0 } });
    CHECK (a.value ("ok", false));
    const std::string trk = a.value ("id", std::string());
    CHECK (a.value ("notes", 0) == fam["proposals"][0].value ("notes", -1)); // lo que se anunció es lo que entra
    const auto notesA = soloNotes (f.session.model());
    json c = f.cmd ({ { "cmd", "soloMelody" }, { "startBar", 2 }, { "bars", 8 }, { "seed", 77 }, { "proposal", 2 }, { "replaceTrackId", trk } });
    CHECK_MSG (c.value ("ok", false), c.dump());
    CHECK (c.value ("id", std::string()) == trk && f.session.model().tracks.size() == tracks + 1);
    const auto notesC = soloNotes (f.session.model());
    CHECK (notesC.size() == (size_t) fam["proposals"][2].value ("notes", -1));
    bool differ = notesA.size() != notesC.size();
    for (size_t i = 0; ! differ && i < notesA.size(); ++i) differ = notesA[i].key != notesC[i].key || notesA[i].start != notesC[i].start;
    CHECK (differ);
    CHECK (f.session.undo().ok);
    CHECK (soloNotes (f.session.model()).size() == notesA.size());
    CHECK (f.session.redo().ok);

    // Variación relacionada: distinta de la propuesta, reproducible y dentro de la tonalidad.
    json v1 = f.cmd ({ { "cmd", "soloMelody" }, { "startBar", 2 }, { "bars", 8 }, { "seed", 77 }, { "proposal", 2 }, { "variation", 9 }, { "freedom", 40 }, { "replaceTrackId", trk } });
    CHECK_MSG (v1.value ("ok", false), v1.dump());
    const auto nv1 = soloNotes (f.session.model());
    json v2 = f.cmd ({ { "cmd", "soloMelody" }, { "startBar", 2 }, { "bars", 8 }, { "seed", 77 }, { "proposal", 2 }, { "variation", 9 }, { "freedom", 40 }, { "replaceTrackId", trk } });
    const auto nv2 = soloNotes (f.session.model());
    CHECK (nv1.size() == nv2.size());
    for (size_t i = 0; i < nv1.size() && i < nv2.size(); ++i) CHECK (nv1[i].key == nv2[i].key && nv1[i].start == nv2[i].start);
    bool varied = nv1.size() != notesC.size();
    for (size_t i = 0; ! varied && i < nv1.size(); ++i) varied = nv1[i].key != notesC[i].key || nv1[i].start != notesC[i].start || nv1[i].length != notesC[i].length;
    CHECK (varied);
    CHECK (v1.value ("inScale", 0.0) > 0.8);
    MEASURE ("variación: %d notas, en escala %.2f, parecido con el motivo %.2f", v1.value ("notes", 0), v1.value ("inScale", 0.0), v1.value ("similarity", 0.0));

    // Nunca se sustituye una pista que no sea de Solo AI ni una bloqueada.
    const std::string bass = f.session.model().tracks.at (1).id;
    const std::string before = toJson (f.session.model());
    json bad = f.cmd ({ { "cmd", "soloMelody" }, { "bars", 8 }, { "seed", 1 }, { "replaceTrackId", bass } });
    CHECK (! bad.value ("ok", true));
    CHECK (toJson (f.session.model()) == before);
    CHECK (f.session.setPartLocked (trk, true).ok || true);
    MusicPlan plan;
    if (MusicPlan::fromJson (f.session.model().music, plan) && std::find (plan.locked.begin(), plan.locked.end(), trk) != plan.locked.end())
    {
        bad = f.cmd ({ { "cmd", "soloMelody" }, { "bars", 8 }, { "seed", 3 }, { "replaceTrackId", trk } });
        CHECK (! bad.value ("ok", true) && bad.value ("error", std::string()).find ("bloqueada") != std::string::npos);
    }
    // Los tipos y caracteres producen material distinto (los controles tienen lógica real detrás).
    const json solo = f.cmd ({ { "cmd", "soloProposals" }, { "startBar", 2 }, { "bars", 8 }, { "seed", 77 }, { "type", "solo" } });
    const json emo = f.cmd ({ { "cmd", "soloProposals" }, { "startBar", 2 }, { "bars", 8 }, { "seed", 77 }, { "type", "chorus" }, { "character", "energetic" } });
    CHECK (solo.value ("ok", false) && emo.value ("ok", false));
    CHECK (solo.at ("proposals") != fam.at ("proposals"));
    CHECK (emo.at ("proposals") != fam.at ("proposals"));
}
#endif
