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
const std::vector<std::string> kGenres = { "trap", "reggaeton", "hiphop", "boombap", "jazzrap", "darkhiphop", "pop", "electronic" };

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
                        // Jazz rap (0.8): el bajo que camina se acerca a la fundamental siguiente en el cuarto tiempo.
                        const Tick from = g == "jazzrap" ? 12 * kStepTicks : 14 * kStepTicks, maxLen = g == "jazzrap" ? 4 * kStepTicks : 2 * kStepTicks;
                        const bool isApproach = part == "bass" && n.start % (4 * kPpq) >= from && n.length <= maxLen && (d == 1 || d == 11);
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
    CHECK (bar == 17); // 16 compases + el compás de cierre (misión 0.6)
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

// --- Misión 0.6: mezcla 3.0 y final de canción ------------------------------------------------------------------
namespace
{
struct Lp2 // paso bajo de 2.º orden (RBJ)
{
    double b0, b1, b2, a1, a2, z1 = 0, z2 = 0;
    Lp2 (double sr, double hz)
    {
        const double w = 2 * 3.14159265358979 * hz / sr, c = std::cos (w), al = std::sin (w) / (2 * 0.7071), a0 = 1 + al;
        b0 = (1 - c) / 2 / a0; b1 = (1 - c) / a0; b2 = b0; a1 = -2 * c / a0; a2 = (1 - al) / a0;
    }
    double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

AudioData renderMix (Fx& f, const std::string& name)
{
    AudioData a;
    std::string err;
    CHECK (f.session.exportMix (f.dir + "/" + name, 32).ok);
    CHECK (readWav (f.dir + "/" + name, a, err));
    return a;
}

double rmsDb (const AudioData& a, Frame from, Frame to)
{
    double s = 0;
    for (Frame i = from; i < to; ++i) s += 0.5 * ((double) a.channels[0][(size_t) i] * a.channels[0][(size_t) i] + (double) a.channels[1][(size_t) i] * a.channels[1][(size_t) i]);
    return 10.0 * std::log10 (s / std::max<Frame> (1, to - from) + 1e-30);
}
} // namespace

// Lo que se midió en la 0.5 con la exportación por pistas: la batería sonaba ~25 dB por encima de la melodía y entre
// el 88 y el 94 % de la energía estaba por debajo de 250 Hz. Esta prueba fija el equilibrio nuevo con márgenes
// amplios: no dice que «suene bien» (eso se escucha), sino que la melodía no vuelve a quedar enterrada.
TEST (templates_mix_keeps_melody_audible_and_low_end_under_control)
{
    for (const std::string g : { "trap", "reggaeton", "hiphop" })
    {
        Fx f ("mix3_" + g);
        CHECK (f.session.createProject (f.dir + "/p", "x", kSr, g).ok);
        const ProjectModel m = f.session.model();
        MusicPlan plan;
        CHECK (MusicPlan::fromJson (m.music, plan));
        const Frame end = m.contentEnd();
        const AudioData mix = renderMix (f, "mix.wav");
        Lp2 l1 (kSr, 250), l2 (kSr, 250);
        double low = 0, all = 0;
        long over = 0;
        for (Frame i = 0; i < mix.numFrames(); ++i)
        {
            const double x = 0.5 * (mix.channels[0][(size_t) i] + mix.channels[1][(size_t) i]);
            const double y = l2.run (l1.run (x));
            low += y * y; all += x * x;
            over += std::fabs (mix.channels[0][(size_t) i]) >= 0.999f || std::fabs (mix.channels[1][(size_t) i]) >= 0.999f;
        }
        std::string drumsId, melodyId;
        for (const auto& [id, role] : plan.roles) { if (role == "drums") drumsId = id; if (role == "melody") melodyId = id; }
        CHECK (! drumsId.empty() && ! melodyId.empty());
        CHECK (f.session.setTrackSolo (drumsId, true).ok);
        const double drums = rmsDb (renderMix (f, "d.wav"), 0, end);
        CHECK (f.session.setTrackSolo (drumsId, false).ok && f.session.setTrackSolo (melodyId, true).ok);
        const double melody = rmsDb (renderMix (f, "m.wav"), 0, end);
        CHECK (f.session.setTrackSolo (melodyId, false).ok);
        MEASURE ("%s: energía por debajo de 250 Hz %.0f %%; melodía %.1f dB respecto a la batería; muestras al límite %ld", g.c_str(), 100.0 * low / all, melody - drums, over);
        CHECK_MSG (low / all < 0.86 && low / all > 0.45, g + ": equilibrio de graves fuera de margen");
        CHECK_MSG (melody - drums > -20.0 && melody - drums < -6.0, g + ": melodía fuera de margen respecto a la batería");
        CHECK (over == 0);
    }
}

TEST (templates_end_with_a_closing_bar_on_the_tonic)
{
    for (const std::string g : { "trap", "reggaeton", "hiphop", "pop", "electronic" })
    {
        Fx f ("end_" + g);
        CHECK (f.session.createProject (f.dir + "/p", "x", kSr, g).ok);
        const ProjectModel m = f.session.model();
        MusicPlan plan;
        CHECK (MusicPlan::fromJson (m.music, plan));
        CHECK (plan.sections.back().name == "Cierre" && plan.sections.back().startBar == 16 && plan.sections.back().bars == 1);
        const Tick bar = 4 * kPpq;
        for (const auto& t : m.tracks)
        {
            auto it = plan.roles.find (t.id);
            if (it == plan.roles.end()) continue;
            const MidiClipModel& last = t.midiClips.back();
            CHECK_MSG (last.name == "Cierre" && last.start == 16 * bar && last.length == bar, g + ": " + t.name);
            if (it->second == "drums") { CHECK (! last.patternId.empty()); continue; }
            CHECK_MSG (! last.notes.empty(), g + ": " + t.name + " no cierra");
            // Bajo y melodía se posan en la tónica; todo empieza en el primer tiempo y se deja sonar.
            for (const auto& n : last.notes) { CHECK (n.start == 0); CHECK (n.length >= bar / 2); }
            if (it->second == "bass" || it->second == "melody") CHECK (last.notes.size() == 1 && last.notes[0].key % 12 == plan.keyRoot);
        }
        // El audio se apaga: el último medio segundo del archivo está muy por debajo del cuerpo de la canción.
        const AudioData mix = renderMix (f, "mix.wav");
        const double body = rmsDb (mix, 0, m.contentEnd()), tail = rmsDb (mix, mix.numFrames() - kSr / 2, mix.numFrames());
        MEASURE ("%s: cuerpo %.1f dB, último medio segundo %.1f dB", g.c_str(), body, tail);
        CHECK (tail < body - 40.0);
    }
}

// La melodía del Final repite el ritmo del motivo de A (reconocible) y lo refuerza con octavas en los tiempos fuertes.
TEST (final_section_restates_the_motif_with_octave_doubling)
{
    for (const std::string g : { "trap", "reggaeton", "hiphop" })
    {
        const MusicPlan plan = composer::defaultPlan (g);
        SongSection a, fin;
        for (const auto& s : plan.sections) { if (s.name == "A") a = s; if (s.name == "Final") fin = s; }
        const auto na = composer::partNotes (plan, "melody", a, plan.seeds.at ("melody"), 4);
        const auto nf = composer::partNotes (plan, "melody", fin, plan.seeds.at ("melody"), 4);
        // Todas las notas de A tienen su nota en el mismo sitio del Final: el ritmo del motivo es el mismo. (Las
        // alturas se adaptan a los acordes, que caen en otro punto de la progresión; el Final añade adornos y octavas.)
        std::set<Tick> startsF;
        for (const auto& n : nf) startsF.insert (n.start);
        int same = 0;
        for (const auto& n : na) same += (int) startsF.count (n.start);
        CHECK_MSG (same == (int) na.size(), g + ": el Final no repite el ritmo del motivo de A (" + std::to_string (same) + " de " + std::to_string (na.size()) + ")");
        int doubled = 0;
        for (const auto& n : nf) for (const auto& o : nf) doubled += o.start == n.start && o.key == n.key + 12;
        MEASURE ("%s: A %zu notas, Final %zu notas, %d dobladas a la octava", g.c_str(), na.size(), nf.size(), doubled);
        CHECK (doubled >= 2 && nf.size() > na.size());
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Misión 0.8: «Variar» conserva la identidad y «Regenerar» propone otra composición. Se mide con el parecido entre
// versiones (índice de Jaccard sobre las notas o los golpes: 1 = idénticas, 0 = nada en común).
namespace
{
std::set<std::string> events (const TrackModel& t)
{
    std::set<std::string> e;
    for (const auto& c : t.midiClips)
        for (const auto& n : c.notes) e.insert (c.name + "|" + std::to_string (n.start) + "|" + std::to_string (n.key));
    for (const auto& p : t.patterns)
        for (size_t l = 0; l < p.lanes.size(); ++l)
            for (size_t s = 0; s < p.lanes[l].size(); ++s) if (p.lanes[l][s] > 0) e.insert (p.name + "|" + std::to_string (l) + "|" + std::to_string (s));
    return e;
}
double jaccard (const std::set<std::string>& a, const std::set<std::string>& b)
{
    if (a.empty() && b.empty()) return 1.0;
    size_t inter = 0;
    for (const auto& x : a) inter += b.count (x);
    return (double) inter / (double) (a.size() + b.size() - inter);
}
double partSimilarity (const ProjectModel& a, const ProjectModel& b, const std::string& role)
{
    const TrackModel* x = byRole (a, role);
    const TrackModel* y = x ? b.findTrack (x->id) : nullptr;
    return x && y ? jaccard (events (*x), events (*y)) : -1.0;
}
} // namespace

TEST (vary_keeps_identity_and_regenerate_changes_the_composition)
{
    double sumVar = 0, sumRegen = 0;
    int n = 0;
    for (const auto& g : kGenres)
    {
        Fx f ("comp_var_regen_" + g);
        CHECK (f.session.createProject (f.dir + "/p", "x", kSr, g).ok);
        const ProjectModel base = f.session.model();
        MusicPlan p0;
        CHECK (MusicPlan::fromJson (base.music, p0));
        CHECK_MSG (f.session.mutatePart ("all", 777).ok, g);
        const ProjectModel varied = f.session.model();
        CHECK (f.session.undo().ok);
        {
            ProjectModel undone = f.session.model();
            undone.modifiedAt = base.modifiedAt; // la fecha de modificación sí avanza; el contenido debe ser el mismo
            CHECK (toJson (undone, false) == toJson (base, false)); // un solo paso de deshacer
        }
        CHECK_MSG (f.session.regenerateBeat (777).ok, g);
        const ProjectModel regen = f.session.model();
        MusicPlan p1;
        CHECK (MusicPlan::fromJson (regen.music, p1));
        CHECK_MSG (p1.progression != p0.progression, g + ": regenerar debe cambiar la progresión");
        CHECK (p1.keyRoot == p0.keyRoot && p1.minor == p0.minor && regen.bpm == base.bpm); // mismo estilo, tono y tempo
        std::string line;
        for (const std::string role : { "drums", "bass", "chords", "melody" })
        {
            const double v = partSimilarity (base, varied, role), r = partSimilarity (base, regen, role);
            if (v < 0) continue;
            char buf[96];
            std::snprintf (buf, sizeof buf, " %s var %.2f / regen %.2f;", role.c_str(), v, r);
            line += buf;
            CHECK_MSG (v < 0.999, g + "/" + role + ": variar no cambió nada");
            CHECK_MSG (v > r, g + "/" + role + ": la variación se aleja más que la regeneración");
            sumVar += v; sumRegen += r; ++n;
        }
        MEASURE ("%s:%s", g.c_str(), line.c_str());
        // Variar es reproducible (misma semilla, mismo resultado) y sobrevive a guardar y reabrir.
        CHECK (f.session.undo().ok);
        CHECK (f.session.mutatePart ("melody", 4242).ok);
        const std::string fp = fpOf (f.session, byRole (f.session.model(), "melody")->id);
        CHECK (f.session.undo().ok);
        CHECK (f.session.mutatePart ("melody", 4242).ok);
        CHECK (fpOf (f.session, byRole (f.session.model(), "melody")->id) == fp);
        CHECK (f.session.save().ok);
        f.session.close();
        CHECK (f.session.openProject (f.dir + "/p").ok);
        CHECK (fpOf (f.session, byRole (f.session.model(), "melody")->id) == fp);
    }
    MEASURE ("media de %d partes: parecido tras variar %.2f, tras regenerar %.2f", n, sumVar / n, sumRegen / n);
    CHECK (sumVar / n > 0.55);
    CHECK (sumRegen / n < 0.45);
}

TEST (regenerate_keeps_locked_parts_and_their_harmony)
{
    Fx f ("comp_regen_lock");
    CHECK (f.session.createProject (f.dir + "/p", "x", kSr, "boombap").ok);
    const ProjectModel base = f.session.model();
    const TrackModel* chords = byRole (base, "chords");
    const TrackModel* drums = byRole (base, "drums");
    CHECK (chords && drums);
    const std::string chordsFp = fingerprint (*chords), drumsFp = fingerprint (*drums);
    MusicPlan p0;
    CHECK (MusicPlan::fromJson (base.music, p0));
    CHECK (f.session.setPartLocked (chords->id, true).ok);
    CHECK (f.session.regenerateBeat (99).ok);
    MusicPlan p1;
    CHECK (MusicPlan::fromJson (f.session.model().music, p1));
    CHECK (p1.progression == p0.progression);                 // los acordes bloqueados siguen encajando
    CHECK (fpOf (f.session, chords->id) == chordsFp);         // la pista bloqueada no se toca
    CHECK (fpOf (f.session, drums->id) != drumsFp);           // el resto sí cambia
    // Todo bloqueado: no hay nada que regenerar y no se crea un paso de deshacer vacío.
    MusicPlan p;
    CHECK (MusicPlan::fromJson (f.session.model().music, p));
    for (const auto& [id, role] : p.roles) CHECK (f.session.setPartLocked (id, true).ok);
    CHECK (! f.session.regenerateBeat (5).ok);
    CHECK (! f.session.mutatePart ("all", 5).ok);
    CHECK (! f.session.mutatePart ("kazoo", 5).ok);
}

TEST (hiphop_styles_are_really_different)
{
    // No es «el mismo patrón con otros sonidos»: swing, densidad, bajo y armonía distintos (medidos).
    struct Row { std::string g; float swing; int hats, kicks, ghost; double bassLen; int chordSize; };
    std::vector<Row> rows;
    for (const std::string g : { "hiphop", "boombap", "jazzrap", "darkhiphop" })
    {
        const MusicPlan p = composer::defaultPlan (g);
        const auto pats = composer::drumPatterns (p, p.seeds.at ("drums"));
        const DrumPattern& m = pats.at ("main");
        Row r { g, m.swing, 0, 0, 0, 0, 0 };
        for (int s = 0; s < m.steps; ++s)
        {
            r.hats += m.lanes[3][(size_t) s] > 0;
            r.kicks += m.lanes[0][(size_t) s] > 0;
            r.ghost += m.lanes[1][(size_t) s] > 0 && m.lanes[1][(size_t) s] < 60;
        }
        const SongSection a = p.sections[1];
        const auto bass = composer::partNotes (p, "bass", a, p.seeds.at ("bass"), 4);
        for (const auto& n : bass) r.bassLen += (double) n.length / kStepTicks;
        r.bassLen /= std::max<size_t> (1, bass.size());
        const auto ch = composer::partNotes (p, "chords", a, p.seeds.at ("chords"), 4);
        std::map<Tick, int> stacks;
        for (const auto& n : ch) stacks[n.start / kStepTicks]++;
        for (const auto& [t, c] : stacks) r.chordSize = std::max (r.chordSize, c);
        rows.push_back (r);
        MEASURE ("%-10s swing %.2f · charles %2d · bombos %d · fantasmas %d · nota media del bajo %.1f semicorcheas · notas por acorde %d",
                 g.c_str(), r.swing, r.hats, r.kicks, r.ghost, r.bassLen, r.chordSize);
    }
    const Row& hh = rows[0]; const Row& bb = rows[1]; const Row& jz = rows[2]; const Row& dk = rows[3];
    CHECK (bb.swing > hh.swing && jz.swing > hh.swing && dk.swing < hh.swing); // crudo y jazz con más swing; oscuro más recto
    CHECK (dk.hats < hh.hats);                 // el oscuro deja espacio
    CHECK (jz.ghost >= hh.ghost);              // el jazz tiene más notas fantasma
    CHECK (bb.ghost <= 1);                     // el crudo no adorna la caja
    CHECK (bb.bassLen < hh.bassLen);           // bajo más seco
    CHECK (dk.bassLen > bb.bassLen);           // sub largo
    CHECK (jz.chordSize >= 5);                 // acordes con novena
}
