// Pruebas de la misión 0.4 (etapa B): compositor local, plantillas «Cauce Signature Sound», variaciones con semilla,
// bloqueo de partes, deshacer, redobles y nivel del render. Todo es determinista: mismas entradas, mismas notas.
#include "Test.h"

#include "cs/Composer.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <algorithm>
#include <cmath>
#include <set>

using namespace cs;

namespace
{
constexpr int kSr = 48000;
const std::vector<std::string> kGenres = { "trap", "reggaeton", "hiphop", "pop", "electronic" };

struct Fx
{
    Engine engine;
    Session session { engine };
    std::string dir;
    explicit Fx (const std::string& name) { dir = cstest::tempDir (name); engine.prepare (kSr, 512); }
};

// Huella de las notas y patrones de una pista (sin ids, que son aleatorios).
std::string fingerprint (const TrackModel& t)
{
    std::string s;
    for (const auto& c : t.midiClips)
    {
        s += c.name + "@" + std::to_string (c.start) + "/" + std::to_string (c.length) + ":";
        for (const auto& n : c.notes) s += std::to_string (n.key) + "," + std::to_string (n.velocity) + "," + std::to_string (n.start) + "," + std::to_string (n.length) + ";";
    }
    for (const auto& p : t.patterns)
    {
        s += "P" + p.name + std::to_string (p.steps) + ":";
        for (const auto& lane : p.lanes) for (int v : lane) s += std::to_string (v) + ".";
        for (const auto& lane : p.rolls) for (int v : lane) s += std::to_string (v) + "r";
    }
    return s;
}

const TrackModel* byName (const ProjectModel& m, const std::string& name)
{
    for (const auto& t : m.tracks) if (t.name == name) return &t;
    return nullptr;
}

const TrackModel* byRole (const ProjectModel& m, const std::string& role)
{
    MusicPlan plan;
    if (! MusicPlan::fromJson (m.music, plan)) return nullptr;
    for (const auto& [id, r] : plan.roles) if (r == role) return m.findTrack (id);
    return nullptr;
}

std::string fpOf (const Session& s, const std::string& id)
{
    const ProjectModel m = s.model();
    const auto* t = m.findTrack (id);
    return t ? fingerprint (*t) : std::string ("<sin pista>");
}

size_t noteCount (const TrackModel& t)
{
    size_t n = 0;
    for (const auto& c : t.midiClips) n += c.notes.size();
    return n;
}
} // namespace

TEST (composer_is_deterministic_per_template)
{
    for (const auto& g : kGenres)
    {
        Fx a ("comp_det_a_" + g), b ("comp_det_b_" + g);
        CHECK (a.session.createProject (a.dir + "/p", "x", kSr, g).ok);
        CHECK (b.session.createProject (b.dir + "/p", "x", kSr, g).ok);
        const ProjectModel ma = a.session.model(); // model() devuelve una copia: guardarla antes de tomar punteros
        const ProjectModel mb = b.session.model();
        CHECK (ma.tracks.size() == mb.tracks.size());
        for (const auto& t : ma.tracks)
        {
            const auto* o = byName (mb, t.name);
            CHECK_MSG (o && fingerprint (t) == fingerprint (*o), g + ": la pista " + t.name + " cambia entre dos creaciones");
        }
        CHECK (! ma.music.empty());
    }
}

TEST (composer_notes_stay_in_key)
{
    for (const auto& g : kGenres)
    {
        const MusicPlan plan = composer::defaultPlan (g);
        CHECK (! plan.genre.empty());
        std::set<int> allowed;
        for (int pc : composer::scale (plan)) allowed.insert (pc % 12);
        if (plan.minor) allowed.insert ((plan.keyRoot + 11) % 12); // sensible de la menor armónica (acorde de V mayor)
        int outside = 0, total = 0, approach = 0;
        for (const std::string part : { "bass", "chords", "melody", "pad" })
            for (const auto& s : plan.sections)
                for (const auto& n : composer::partNotes (plan, part, s, plan.seeds.count (part) ? plan.seeds.at (part) : 1u, 4))
                {
                    ++total;
                    if (! allowed.count (n.key % 12))
                    {
                        // Única excepción permitida: nota de paso cromática del bajo (recurso del boom bap) al final del
                        // compás, corta y a un semitono de la fundamental del acorde siguiente.
                        const int absBar = s.startBar + (int) (n.start / (4 * kPpq));
                        const auto sc = composer::scale (plan); // chordAt da notas con conducción de voces (no siempre la fundamental abajo)
                        const int nextRoot = sc[(size_t) plan.progression[(size_t) (absBar + 1) % plan.progression.size()]] % 12;
                        const int d = ((n.key - nextRoot) % 12 + 12) % 12;
                        const bool isApproach = part == "bass" && n.start % (4 * kPpq) >= 14 * kStepTicks && n.length <= 2 * kStepTicks && (d == 1 || d == 11);
                        if (isApproach) ++approach; else ++outside;
                    }
                    CHECK (n.key >= 0 && n.key <= 127 && n.velocity >= 1 && n.velocity <= 127 && n.length > 0 && n.start >= 0);
                    CHECK_MSG (n.start < (Tick) s.bars * 4 * kPpq, g + ": nota fuera de su sección");
                }
        MEASURE ("%s: %d notas, %d fuera de la escala, %d notas de paso cromáticas del bajo", g.c_str(), total, outside, approach);
        CHECK (total > 50);
        CHECK_MSG (outside == 0, g + ": hay notas fuera de la tonalidad");
    }
}

TEST (composer_sections_cover_sixteen_bars_and_energy_changes_density)
{
    const MusicPlan plan = composer::defaultPlan ("reggaeton");
    int bar = 0;
    for (const auto& s : plan.sections) { CHECK (s.startBar == bar); bar += s.bars; }
    CHECK (bar == 16);
    MusicPlan calm = plan, hot = plan;
    calm.energy = -1;
    hot.energy = 1;
    size_t nCalm = 0, nHot = 0;
    for (const auto& s : plan.sections)
    {
        nCalm += composer::partNotes (calm, "melody", s, 7, 4).size();
        nHot += composer::partNotes (hot, "melody", s, 7, 4).size();
    }
    MEASURE ("melodía: %zu notas (calma) vs %zu (energía)", nCalm, nHot);
    CHECK (nHot > nCalm);
}

TEST (vary_part_changes_only_that_part_and_undo_restores)
{
    Fx f ("comp_vary");
    CHECK (f.session.createProject (f.dir + "/p", "x", kSr, "trap").ok);
    const ProjectModel before = f.session.model();
    const auto* mel = byRole (before, "melody");
    const auto* bass = byRole (before, "bass");
    const auto* drums = byRole (before, "drums");
    CHECK (mel && bass && drums);
    const std::string drumsId = drums->id;
    const std::string melId = mel->id, bassFp = fingerprint (*bass), drumsFp = fingerprint (*drums), melFp = fingerprint (*mel);

    CHECK (f.session.varyPart ("melody", 12345).ok);
    const ProjectModel m1 = f.session.model();
    CHECK_MSG (fingerprint (*m1.findTrack (melId)) != melFp, "la melodía no cambió");
    CHECK (fingerprint (*m1.findTrack (bass->id)) == bassFp);
    CHECK (fingerprint (*m1.findTrack (drums->id)) == drumsFp);
    const std::string variantFp = fingerprint (*m1.findTrack (melId));

    // Misma semilla, mismo resultado (reproducible). Un solo paso de deshacer.
    CHECK (f.session.undo().ok);
    CHECK (fpOf (f.session, melId) == melFp);
    CHECK (f.session.redo().ok);
    CHECK (fpOf (f.session, melId) == variantFp);
    CHECK (f.session.undo().ok);
    CHECK (f.session.varyPart ("melody", 12345).ok);
    CHECK (fpOf (f.session, melId) == variantFp);

    // Persistencia del plan: al reabrir, la variación sigue y se puede seguir variando.
    CHECK (f.session.save().ok);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (fpOf (f.session, melId) == variantFp);
    CHECK (f.session.varyPart ("drums", 99).ok);
    CHECK (fpOf (f.session, drumsId) != drumsFp);
}

TEST (locked_parts_are_not_varied)
{
    Fx f ("comp_lock");
    CHECK (f.session.createProject (f.dir + "/p", "x", kSr, "hiphop").ok);
    const ProjectModel m0 = f.session.model();
    const auto* bass = byRole (m0, "bass");
    CHECK (bass);
    const std::string id = bass->id, fp = fingerprint (*bass);
    CHECK (f.session.setPartLocked (id, true).ok);
    CHECK (! f.session.varyPart ("bass", 777).ok); // la única pista de bajo está bloqueada
    CHECK (f.session.varyPart ("all", 777).ok);    // el resto sí cambia
    CHECK (fpOf (f.session, id) == fp);
    CHECK (f.session.setPartLocked (id, false).ok);
    CHECK (f.session.varyPart ("bass", 777).ok);
    CHECK (fpOf (f.session, id) != fp);
    // Entradas inválidas.
    CHECK (! f.session.varyPart ("kazoo", 1).ok);
    CHECK (! f.session.varyPart ("all", 1, std::nanf (""), true).ok);
    CHECK (! f.session.setPartLocked ("no-existe", true).ok);
}

TEST (projects_without_plan_reject_variations)
{
    Fx f ("comp_noplan");
    CHECK (f.session.createProject (f.dir + "/p", "x", kSr, "").ok);
    CHECK (f.session.model().music.empty());
    CHECK (! f.session.varyPart ("all", 1).ok);
}

TEST (drum_rolls_persist_and_expand)
{
    Fx f ("comp_roll");
    CHECK (f.session.createProject (f.dir + "/p", "x", kSr, "trap").ok);
    const ProjectModel m0 = f.session.model();
    const auto* d = byRole (m0, "drums");
    CHECK (d && ! d->patterns.empty());
    bool hasRoll = false;
    for (const auto& p : d->patterns) for (const auto& lane : p.rolls) for (int v : lane) hasRoll |= v > 1;
    CHECK_MSG (hasRoll, "la plantilla trap debería traer redobles de charles");
    const std::string id = d->id, pid = d->patterns[0].id;
    CHECK (f.session.setPatternRoll (id, pid, 2, 3, 4).ok);
    CHECK (! f.session.setPatternRoll (id, pid, 2, 3, 5).ok);
    // Apagar el paso borra su redoble; encenderlo de nuevo da un golpe normal.
    CHECK (f.session.setPatternStep (id, pid, 5, 9, 100).ok);
    CHECK (f.session.setPatternRoll (id, pid, 5, 9, 3).ok);
    CHECK (f.session.setPatternStep (id, pid, 5, 9, 0).ok);
    CHECK (f.session.setPatternStep (id, pid, 5, 9, 100).ok);
    { const ProjectModel m = f.session.model(); CHECK (m.findTrack (id)->patterns[0].rollAt (5, 9) == 1); }
    CHECK (f.session.save().ok);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    const ProjectModel m1 = f.session.model();
    CHECK (m1.findTrack (id)->patterns[0].rollAt (2, 3) == 4);
}

TEST (template_renders_have_headroom_and_no_clipping)
{
    for (const auto& g : kGenres)
    {
        Fx f ("comp_render_" + g);
        CHECK (f.session.createProject (f.dir + "/p", "x", kSr, g).ok);
        long clipped = -1;
        const std::string out = f.dir + "/mix.wav";
        CHECK (f.session.exportMix (out, 32, kSr * 2, &clipped).ok);
        AudioData a;
        std::string err;
        CHECK (readWav (out, a, err));
        float peak = 0;
        double sumL = 0, sumS = 0;
        for (size_t i = 0; i < a.channels[0].size(); ++i)
        {
            const float l = a.channels[0][i], r = a.channels.size() > 1 ? a.channels[1][i] : l;
            CHECK (std::isfinite (l) && std::isfinite (r));
            peak = std::max ({ peak, std::fabs (l), std::fabs (r) });
            sumL += (double) (l + r) * (l + r);
            sumS += (double) (l - r) * (l - r);
        }
        const double peakDb = 20 * std::log10 (std::max (1e-9f, peak));
        const double sideDb = 10 * std::log10 ((sumS + 1e-12) / (sumL + 1e-12));
        MEASURE ("%s: pico %.1f dBFS, lado/centro %.1f dB, %ld muestras recortadas", g.c_str(), peakDb, sideDb, clipped);
        CHECK (clipped == 0);
        CHECK_MSG (peakDb < -1.0 && peakDb > -12.0, g + ": pico fuera de -12..-1 dBFS");
        CHECK_MSG (sideDb > -40.0, g + ": la mezcla sigue siendo mono");
    }
}
