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
