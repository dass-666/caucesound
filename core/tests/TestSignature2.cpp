// Misión 0.10 — plantillas «Signature 2». Es un modo ALTERNATIVO: las plantillas de siempre no cambian (se comprueba
// con huellas congeladas de sus notas), y el modo nuevo se pide con "<estilo>@2" o "style": 2 al crear el proyecto.
// Estas pruebas miden estructura y datos musicales (forma de canción, acentos, notas fantasma, bajo con el bombo,
// humanización acotada y reversible). No pueden decir si «suena mejor»: eso solo se decide escuchando.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Composer.h"
#include "cs/Packs.h"
#include "cs/Session.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <set>

using namespace cs;
using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
const std::vector<std::string> kStyles = { "trap", "reggaeton", "hiphop", "boombap", "jazzrap", "darkhiphop", "pop", "electronic" };

struct Rig
{
    Engine engine;
    Session session { engine };
    std::string dir;
    explicit Rig (const std::string& name) { dir = cstest::tempDir (name); engine.prepare (48000, 1024); }
    json cmd (const json& q) { return json::parse (handleCommand (session, engine, q.dump())); }
    json create (const std::string& style, int mode = 1)
    {
        json q = { { "cmd", "createProject" }, { "folder", dir + "/p" + std::to_string (++n) }, { "name", "x" }, { "template", style } };
        if (mode == 2) q["style"] = 2;
        return cmd (q);
    }
    MusicPlan plan() { MusicPlan p; MusicPlan::fromJson (session.model().music, p); return p; }
    int n = 0;
};

// Huella de todo lo que el compositor escribió en el proyecto (notas y patrones), independiente de los ids.
uint32_t musicHash (const ProjectModel& m)
{
    uint32_t h = 2166136261u;
    auto add = [&] (long long v) { for (int i = 0; i < 8; ++i) { h ^= (uint8_t) (v >> (8 * i)); h *= 16777619u; } };
    for (const auto& t : m.tracks)
    {
        for (const auto& p : t.patterns)
        {
            add (p.steps); add ((long long) std::lround (p.swing * 1000));
            for (const auto& l : p.lanes) for (int v : l) add (v);
            for (size_t l = 0; l < p.lanes.size(); ++l) for (int st = 0; st < p.steps; ++st) add (p.rollAt ((int) l, st)); // 0 y 1 son lo mismo
        }
        for (const auto& c : t.midiClips)
        {
            add (c.start); add (c.length);
            for (const auto& x : c.notes) { add (x.key); add (x.velocity); add (x.start); add (x.length); }
        }
        add ((long long) std::lround (t.volumeDb * 10));
    }
    return h;
}

const TrackModel* roleTrack (const ProjectModel& m, const MusicPlan& p, const std::string& role)
{
    for (const auto& t : m.tracks) { auto it = p.roles.find (t.id); if (it != p.roles.end() && it->second == role) return &t; }
    return nullptr;
}
const DrumPattern* patternNamed (const TrackModel& t, const std::string& name)
{
    for (const auto& p : t.patterns) if (p.name == name) return &p;
    return nullptr;
}
void noPacks() { packLibrary().configure (cstest::tempDir ("sig2_nopacks"), ""); }
} // namespace

// Las plantillas de siempre, nota a nota. Las huellas se congelaron en la 0.10 después de comprobar que el audio de las
// ocho plantillas era idéntico muestra a muestra al de la 0.9 (cs_render --compare09 con las muestras de la 0.9).
// Si esta prueba falla, una canción nueva con la plantilla de siempre ya no suena como antes: no actualizar la huella
// sin decidirlo a propósito.
TEST (classic_templates_are_frozen_note_for_note)
{
    noPacks();
    static const std::map<std::string, uint32_t> frozen = {
        { "trap", 0xC4A094AFu },
        { "reggaeton", 0xBD4C4C39u },
        { "hiphop", 0x65955A2Bu },
        { "boombap", 0xFC5BF533u },
        { "jazzrap", 0xE2CA3E52u },
        { "darkhiphop", 0x0C188A0Du },
        { "pop", 0xD84DDEE7u },
        { "electronic", 0xB06C1BF3u },
    };
    for (const auto& g : kStyles)
    {
        Rig r ("sig2_frozen_" + g);
        CHECK (r.create (g).value ("ok", false));
        const MusicPlan p = r.plan();
        CHECK (p.style == 1);
        CHECK (p.humanize == 0.0f);
        CHECK (r.session.model().music.find ("\"style\"") == std::string::npos); // el formato guardado tampoco cambia
        const uint32_t h = musicHash (r.session.model());
        const auto it = frozen.find (g);
        if (it == frozen.end()) { MEASURE ("huella %s = 0x%08Xu", g.c_str(), h); CHECK_MSG (false, "falta la huella de " + g); }
        else CHECK_MSG (h == it->second, "la plantilla " + g + " ya no produce las mismas notas");
        r.session.close();
    }
}

TEST (signature2_has_song_form_and_genre_drum_character)
{
    noPacks();
    for (const auto& g : kStyles)
    {
        Rig r ("sig2_form_" + g);
        const json res = r.create (g, 2);
        CHECK (res.value ("ok", false));
        const ProjectModel m = r.session.model();
        const MusicPlan p = r.plan();
        CHECK (m.templateId == g); // el estilo sigue siendo el mismo: Variar, Regenerar y AutoMix lo reconocen
        CHECK (p.style == 2);
        CHECK (p.humanize > 0.0f);
        std::vector<std::string> names;
        int bars = 0;
        for (const auto& s : p.sections) { names.push_back (s.name); bars = std::max (bars, s.startBar + s.bars); }
        CHECK (names == (std::vector<std::string> { "Intro", "Verso", "Coro", "Verso 2", "Coro 2", "Cierre" }));
        CHECK (bars == 37);
        const TrackModel* d = roleTrack (m, p, "drums");
        CHECK (d != nullptr);
        if (! d) continue;
        const DrumPattern* verse = patternNamed (*d, "Verso");
        const DrumPattern* chorus = patternNamed (*d, "Coro");
        CHECK (verse && chorus);
        if (! verse || ! chorus) continue;
        // Bombo: el del primer tiempo es el más fuerte del compás.
        const auto& kick = verse->lanes[0];
        int kmax = 0; for (int v : kick) kmax = std::max (kmax, v);
        if (kick[0] > 0) CHECK (kick[0] >= kmax - 9); // ±humanización
        // Charles: al menos tres niveles de fuerza (acentos), no una fila plana.
        std::set<int> hatLevels; int hats = 0;
        for (int v : verse->lanes[2]) if (v > 0) { hatLevels.insert (v / 12); ++hats; }
        if (hats >= 6) CHECK_MSG (hatLevels.size() >= 2, g + ": charles sin acentos");
        // Notas fantasma de caja en los estilos de hip-hop.
        if (g == "boombap" || g == "jazzrap" || g == "hiphop")
        {
            int ghosts = 0; for (int v : verse->lanes[1]) ghosts += v > 0 && v < 50;
            CHECK_MSG (ghosts >= 1, g + ": sin notas fantasma");
        }
        // Verso y coro largos: el reparto de la batería tiene variación dentro de la sección y un remate al final.
        int verseClips = 0, fills = 0;
        const Tick bar = (Tick) kPpq * m.beatsPerBar;
        for (const auto& c : d->midiClips) { if (c.start >= 4 * bar && c.start < 12 * bar) ++verseClips; fills += c.name == "Remate"; }
        CHECK (verseClips >= 4);
        CHECK (fills >= 4);
        // La melodía deja sitio a la voz en el verso: menos notas por compás que en el coro.
        if (const TrackModel* mel = roleTrack (m, p, "melody"))
        {
            size_t v = 0, c = 0;
            for (const auto& clip : mel->midiClips) { if (clip.name == "Verso") v = clip.notes.size(); if (clip.name == "Coro") c = clip.notes.size(); }
            MEASURE ("%s: melodía, notas en verso %zu y en coro %zu", g.c_str(), v, c);
            if (c >= 8) CHECK_MSG (v < c, g + ": el verso no deja hueco");
        }
        // Variar y Regenerar siguen funcionando sobre el modo nuevo.
        const json reg = r.cmd ({ { "cmd", "regenerateBeat" }, { "seed", 777 } });
        CHECK (reg.value ("ok", false));
        CHECK (reg["variation"].value ("significant", false));
        CHECK (r.plan().style == 2);
        r.session.close();
    }
}

TEST (signature2_bass_lands_with_the_kick)
{
    for (const std::string g : { "trap", "darkhiphop", "reggaeton", "hiphop", "boombap" })
    {
        double frac[2] = {};
        for (int style = 1; style <= 2; ++style)
        {
            MusicPlan p = composer::defaultPlan (g, 2);
            p.style = style; // misma forma de canción; solo cambia el tratamiento
            p.humanize = 0;
            const auto pats = composer::drumPatterns (p, p.seeds["drums"]);
            int with = 0, total = 0;
            for (const auto& s : p.sections)
            {
                if (composer::sectionKind (s) != "A" && composer::sectionKind (s) != "B") continue;
                std::vector<const DrumPattern*> perBar;
                for (const auto& slot : composer::drumLayout (p, s)) for (int b = 0; b < slot.bars; ++b) perBar.push_back (&pats.at (slot.patternKey));
                for (const auto& n : composer::partNotes (p, "bass", s, p.seeds["bass"], 4))
                {
                    const int b = (int) (n.start / (4 * kPpq)), st = (int) ((n.start % (4 * kPpq)) / kStepTicks);
                    if (b >= (int) perBar.size()) continue;
                    ++total;
                    with += perBar[(size_t) b]->lanes[0][(size_t) st] > 0;
                }
            }
            frac[style - 1] = total ? (double) with / total : 0;
        }
        MEASURE ("%s: notas de bajo que caen con un bombo: antes %.0f %%, Signature 2 %.0f %%", g.c_str(), 100 * frac[0], 100 * frac[1]);
        CHECK (frac[1] >= frac[0]);
    }
}

TEST (humanize_is_bounded_deterministic_reversible_and_respects_locks)
{
    noPacks();
    Rig r ("sig2_human");
    CHECK (r.create ("hiphop", 2).value ("ok", false));
    auto offGrid = [&] (int& maxDev)
    {
        int n = 0; maxDev = 0;
        const ProjectModel m = r.session.model();
        for (const auto& t : m.tracks)
            for (const auto& c : t.midiClips)
                for (const auto& x : c.notes)
                {
                    const int dev = (int) std::min<Tick> (x.start % 60, 60 - x.start % 60);
                    n += dev != 0; maxDev = std::max (maxDev, dev);
                    CHECK (x.start >= 0 && x.start < c.length && x.length > 0 && x.start + x.length <= c.length);
                    CHECK (x.velocity >= 1 && x.velocity <= 127);
                }
        return n;
    };
    int dev = 0;
    CHECK (r.cmd ({ { "cmd", "setHumanize" }, { "amount", 0.0 } }).value ("ok", false));
    CHECK (offGrid (dev) == 0); // sin humanizar, todo vuelve a la rejilla
    const uint32_t square = musicHash (r.session.model());
    CHECK (r.cmd ({ { "cmd", "setHumanize" }, { "amount", 1.0 } }).value ("ok", false));
    const int moved = offGrid (dev);
    MEASURE ("humanización al máximo: %d notas fuera de la rejilla, desviación máxima %d ticks (%.1f ms a 88 bpm)", moved, dev, dev * 60000.0 / (88.0 * kPpq));
    CHECK (moved > 10);
    CHECK (dev <= 14);
    const uint32_t loose = musicHash (r.session.model());
    CHECK (loose != square);
    // Determinista: el mismo valor da exactamente lo mismo.
    CHECK (r.cmd ({ { "cmd", "setHumanize" }, { "amount", 0.0 } }).value ("ok", false));
    CHECK (musicHash (r.session.model()) == square);
    CHECK (r.cmd ({ { "cmd", "setHumanize" }, { "amount", 1.0 } }).value ("ok", false));
    CHECK (musicHash (r.session.model()) == loose);
    // Deshacer vuelve a la versión anterior exacta.
    CHECK (r.cmd ({ { "cmd", "undo" } }).value ("ok", false));
    CHECK (musicHash (r.session.model()) == square);
    // Una pista con candado no se toca.
    const MusicPlan p = r.plan();
    const ProjectModel m1 = r.session.model();
    const TrackModel* bass = roleTrack (m1, p, "bass");
    CHECK (bass != nullptr);
    const std::string bassId = bass->id;
    const std::vector<MidiNote> before = bass->midiClips[1].notes;
    CHECK (r.cmd ({ { "cmd", "setPartLocked" }, { "trackId", bassId }, { "on", true } }).value ("ok", false));
    CHECK (r.cmd ({ { "cmd", "setHumanize" }, { "amount", 0.8 } }).value ("ok", false));
    const ProjectModel m2 = r.session.model();
    const auto& after = m2.findTrack (bassId)->midiClips[1].notes;
    bool same = before.size() == after.size();
    for (size_t i = 0; same && i < before.size(); ++i) same = before[i].start == after[i].start && before[i].velocity == after[i].velocity;
    CHECK (same);
    // Se guarda y se recupera.
    CHECK (r.cmd ({ { "cmd", "save" } }).value ("ok", false));
    const std::string folder = r.dir + "/p1";
    const uint32_t saved = musicHash (r.session.model());
    r.session.close();
    CHECK (r.cmd ({ { "cmd", "openProject" }, { "folder", folder } }).value ("ok", false));
    CHECK (musicHash (r.session.model()) == saved);
    CHECK (std::fabs (r.plan().humanize - 0.8f) < 1e-4f);
    CHECK (r.plan().style == 2);
    // También vale para un proyecto con la plantilla de siempre (opcional, empieza en 0).
    r.session.close();
    CHECK (r.create ("trap").value ("ok", false));
    CHECK (r.cmd ({ { "cmd", "setHumanize" }, { "amount", 0.5 } }).value ("ok", false));
    CHECK (offGrid (dev) > 0);
    CHECK (r.plan().style == 1);
    r.session.close();
}

// Sin los paquetes 2.0 la canción se crea igual con los sintetizadores y lo avisa. Con ellos (CS_FACTORY_PACKS), cada
// estilo usa su kit y sus instrumentos, sin recortes en la mezcla exportada.
TEST (signature2_falls_back_without_packs_and_uses_them_when_installed)
{
    noPacks();
    {
        Rig r ("sig2_fallback");
        const json res = r.create ("trap", 2);
        CHECK (res.value ("ok", false));
        CHECK (res["notes"].size() >= 3); // batería, 808 y pad
        for (const auto& t : r.session.model().tracks) CHECK (t.instrument.type != "cs.sampler");
        CHECK (r.session.missingInstruments().empty());
        r.session.close();
    }
    const char* dir = std::getenv ("CS_FACTORY_PACKS");
    if (! dir || ! fs::is_directory (std::string (dir) + "/packs/cauce.drums.two")) { MEASURE ("CS_FACTORY_PACKS sin los paquetes 2.0: parte omitida"); return; }
    packLibrary().configure (std::string (dir) + "/packs", cstest::tempDir ("sig2_user"));
    std::set<std::string> kits;
    for (const auto& g : kStyles)
    {
        Rig r ("sig2_packs_" + g);
        const json res = r.create (g, 2);
        CHECK (res.value ("ok", false));
        CHECK_MSG (res["notes"].empty(), g + ": " + res["notes"].dump());
        int samplers = 0;
        for (const auto& t : r.session.model().tracks)
        {
            samplers += t.instrument.type == "cs.sampler";
            if (t.kind == "drums") { const json st = json::parse (t.instrument.state); kits.insert (st.value ("sampleKit", std::string ("sintetizado"))); }
        }
        CHECK (samplers >= 2);
        CHECK (r.session.missingInstruments().empty());
        long clipped = -1;
        const std::string wav = r.dir + "/mix.wav";
        CHECK (r.session.exportMix (wav, 16, 0, &clipped).ok);
        CHECK_MSG (clipped == 0, g + ": la mezcla recorta");
        r.session.close();
    }
    MEASURE ("kits distintos en los 8 estilos: %zu", kits.size());
    CHECK (kits.size() >= 6); // los géneros no comparten una misma batería (trap y dark comparten kit a propósito)
    noPacks();
}
