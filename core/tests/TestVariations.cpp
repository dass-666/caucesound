// Hotfix 0.9.2 — «Variar y Regenerar no hacen nada en el teléfono». Estas pruebas recorren el mismo camino que la
// app: comandos JSON por cs::handleCommand (lo que envía DawViewModel por JNI), el motor en tiempo real
// (Engine::process por bloques de 1024 como el callback de Oboe) y el estado que lee la interfaz. No se conforman con
// que el compositor devuelva ok: miden cuánto cambian las notas, cuánto cambia el audio por encima de 300 Hz (lo que
// reproduce el altavoz de un teléfono), que el candado se respete, que deshacer/rehacer comparen versiones y que el
// resultado se guarde. Las cifras se imprimen con «measure:» y se resumen en docs/HOTFIX_0.9.2.md.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Session.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <set>

using namespace cs;
using nlohmann::json;

namespace
{
const std::vector<std::string> kStyles = { "trap", "reggaeton", "hiphop", "boombap", "jazzrap", "darkhiphop", "pop", "electronic" };
const std::vector<std::string> kParts = { "drums", "bass", "chords", "melody", "pad" };

struct Rig
{
    Engine engine;
    Session session { engine };
    std::string dir;
    explicit Rig (const std::string& name) { dir = cstest::tempDir (name); engine.prepare (48000, 1024); }
    json cmd (const json& q) { return json::parse (handleCommand (session, engine, q.dump())); }
    json create (const std::string& style) { return cmd ({ { "cmd", "createProject" }, { "folder", dir + "/p" }, { "name", "x" }, { "template", style } }); }
    json state() { return cmd ({ { "cmd", "state" } }); }
    // Mezcla mono de n bloques de 1024 frames, igual que el callback de audio del teléfono.
    std::vector<float> run (int blocks)
    {
        std::vector<float> out, buf (2048), in (2048, 0.0f);
        out.reserve ((size_t) blocks * 1024);
        for (int b = 0; b < blocks; ++b)
        {
            engine.process (in.data(), 1, buf.data(), 2, 1024);
            for (int i = 0; i < 1024; ++i) out.push_back (0.5f * (buf[(size_t) 2 * i] + buf[(size_t) 2 * i + 1]));
        }
        return out;
    }
};

// Huella musical de una pista tal como la ve la interfaz (state): notas de los clips y golpes de los patrones.
std::set<std::string> trackEvents (const json& st, const std::string& trackId)
{
    std::set<std::string> e;
    for (const auto& t : st.at ("project").at ("tracks"))
    {
        if (t.value ("id", std::string()) != trackId) continue;
        if (t.contains ("midiClips"))
            for (const auto& c : t.at ("midiClips"))
                if (c.contains ("notes"))
                    for (const auto& n : c.at ("notes")) e.insert (std::to_string ((long long) c.value ("start", 0LL)) + "|" + std::to_string (n[3].get<long long>()) + "|" + std::to_string (n[0].get<int>()));
        if (t.contains ("patterns"))
            for (const auto& p : t.at ("patterns"))
                if (p.contains ("lanes"))
                {
                    const auto& lanes = p.at ("lanes");
                    for (size_t l = 0; l < lanes.size(); ++l)
                        for (size_t s = 0; s < lanes[l].size(); ++s)
                            if (lanes[l][s].get<int>() > 0)
                            {
                                // Un redoble (varios golpes dentro del paso) es otro sonido: cuenta como cambio.
                                int roll = 0;
                                if (p.contains ("rolls") && l < p.at ("rolls").size() && s < p.at ("rolls")[l].size()) roll = p.at ("rolls")[l][s].get<int>();
                                e.insert (p.value ("id", std::string()) + "|" + std::to_string (l) + "|" + std::to_string (s) + "|" + std::to_string (roll));
                            }
                }
    }
    return e;
}
double change (const std::set<std::string>& a, const std::set<std::string>& b)
{
    if (a.empty() && b.empty()) return 0;
    size_t i = 0;
    for (const auto& x : a) i += b.count (x);
    return 1.0 - (double) i / (double) (a.size() + b.size() - i);
}
std::map<std::string, std::string> rolesOf (const json& st)
{
    std::map<std::string, std::string> r;
    const json& p = st.at ("project");
    if (p.contains ("music") && p.at ("music").contains ("roles")) r = p.at ("music").at ("roles").get<std::map<std::string, std::string>>();
    return r;
}
int revisionOf (const json& st) { const json& p = st.at ("project"); return p.contains ("music") ? p.at ("music").value ("revision", 1) : 0; }

// Contenido musical (sin fechas ni mezcla) para comparar versiones tras deshacer y rehacer.
std::string content (const json& st)
{
    std::string s;
    for (const auto& [id, role] : rolesOf (st))
        for (const auto& e : trackEvents (st, id)) s += role + e + ";";
    return s;
}

std::vector<float> highPass (std::vector<float> x, double fc)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        const double w = 2 * M_PI * fc / 48000, c = std::cos (w), a = std::sin (w) / (2 * 0.7071);
        const double b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + a, a1 = -2 * c, a2 = 1 - a;
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : x)
        {
            const double in = v, o = (b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0;
            x2 = x1; x1 = in; y2 = y1; y1 = o; v = (float) o;
        }
    }
    return x;
}
double rms (const std::vector<float>& a) { double s = 0; for (float v : a) s += (double) v * v; return a.empty() ? 0 : std::sqrt (s / (double) a.size()); }
} // namespace

// La secuencia completa de cada botón del panel en los ocho estilos: el comando llega, devuelve éxito, la versión
// sube, las notas que ve la interfaz cambian de forma significativa, deshacer vuelve exactamente a la anterior y
// rehacer a la nueva.
TEST (variations_every_button_changes_music_in_all_styles)
{
    struct Op { std::string label; json q; double minChange; };
    std::vector<Op> ops;
    ops.push_back ({ "variar_todo", { { "cmd", "mutatePart" }, { "part", "all" }, { "seed", 3735928559LL } }, 0.15 });
    for (const auto& p : kParts) ops.push_back ({ "variar_" + p, { { "cmd", "mutatePart" }, { "part", p }, { "seed", 2654435761LL } }, 0.15 });
    ops.push_back ({ "regenerar", { { "cmd", "regenerateBeat" }, { "seed", 4093564999LL } }, 0.40 });
    ops.push_back ({ "rehacer_todas", { { "cmd", "varyPart" }, { "part", "all" }, { "seed", 1234567891LL } }, 0.25 });
    for (const auto& p : kParts) ops.push_back ({ "rehacer_" + p, { { "cmd", "varyPart" }, { "part", p }, { "seed", 987654321LL } }, 0.25 });

    for (const auto& style : kStyles)
    {
        Rig r ("var092_" + style);
        CHECK_MSG (r.create (style).value ("ok", false), style);
        CHECK (r.cmd ({ { "cmd", "save" } }).value ("ok", false));
        std::string line;
        for (const auto& op : ops)
        {
            const json before = r.state();
            const auto roles = rolesOf (before);
            const std::string part = op.q.value ("part", std::string ("all"));
            bool has = part == "all";
            for (const auto& [id, role] : roles) has = has || role == part;
            const json res = r.cmd (op.q);
            if (! has) { CHECK_MSG (! res.value ("ok", true) && ! res.value ("error", std::string()).empty(), style + "/" + op.label); continue; }
            CHECK_MSG (res.value ("ok", false), style + "/" + op.label + ": " + res.dump());
            if (! res.value ("ok", false)) continue;
            CHECK_MSG (res.contains ("variation"), style + "/" + op.label + ": falta el informe");
            const json& rep = res.at ("variation");
            const json after = r.state();
            CHECK_MSG (revisionOf (after) == revisionOf (before) + 1, style + "/" + op.label + ": la versión no subió");
            CHECK (rep.value ("revision", 0) == revisionOf (after));
            CHECK_MSG (after.value ("canUndo", false), style + "/" + op.label);
            // El cambio medido desde fuera (estado JSON, lo mismo que dibuja la interfaz).
            double sum = 0; int n = 0;
            for (const auto& [id, role] : roles)
            {
                if (part != "all" && role != part) continue;
                const double c = change (trackEvents (before, id), trackEvents (after, id));
                sum += c; ++n;
                if (part != "all") CHECK_MSG (c >= op.minChange * 0.999, style + "/" + op.label + "/" + role + ": cambio " + std::to_string (c));
            }
            const double mean = n ? sum / n : 0;
            CHECK_MSG (mean >= op.minChange * 0.999, style + "/" + op.label + ": cambio medio " + std::to_string (mean));
            CHECK_MSG (rep.value ("significant", false), style + "/" + op.label);
            char buf[64];
            std::snprintf (buf, sizeof buf, " %s %.0f%%(x%d)", op.label.c_str(), mean * 100, rep.value ("attempts", 0));
            line += buf;
            // Comparar: deshacer = versión anterior exacta; rehacer = la nueva.
            CHECK (r.cmd ({ { "cmd", "undo" } }).value ("ok", false));
            const json undone = r.state();
            CHECK_MSG (content (undone) == content (before), style + "/" + op.label + ": deshacer no vuelve a la anterior");
            CHECK (revisionOf (undone) == revisionOf (before));
            CHECK (r.cmd ({ { "cmd", "redo" } }).value ("ok", false));
            CHECK_MSG (content (r.state()) == content (after), style + "/" + op.label + ": rehacer no vuelve a la nueva");
        }
        MEASURE ("%s:%s", style.c_str(), line.c_str());
    }
}

// Lo que suena: dos motores idénticos reproducen la misma canción; en uno se pulsa el botón DURANTE la reproducción.
// El audio debe cambiar en seguida y de forma audible también por encima de 300 Hz (el altavoz del teléfono casi no
// reproduce graves: si el cambio fuera solo en el bombo y el sub, en el Samsung «no pasaría nada»).
TEST (variations_are_heard_during_playback_above_300hz)
{
    for (const std::string op : { "mutatePart", "regenerateBeat" })
        for (const auto& style : kStyles)
        {
            Rig a ("var092_pa_" + style), b ("var092_pb_" + style);
            CHECK (a.create (style).value ("ok", false));
            CHECK (b.create (style).value ("ok", false));
            CHECK (a.cmd ({ { "cmd", "play" } }).value ("ok", false));
            CHECK (b.cmd ({ { "cmd", "play" } }).value ("ok", false));
            const auto a0 = a.run (240), b0 = b.run (240); // ~5 s (pasada la intro)
            double same = 0;
            for (size_t i = 0; i < a0.size(); ++i) same += std::fabs (a0[i] - b0[i]);
            CHECK_MSG (same == 0.0, style + ": los dos motores deberían sonar igual antes del botón");
            json q = { { "cmd", op }, { "seed", 3735928559LL } };
            if (op == "mutatePart") q["part"] = "all";
            CHECK (b.cmd (q).value ("ok", false));
            const auto a1 = a.run (900), b1 = b.run (900); // ~19 s después de pulsar
            std::vector<float> d (a1.size());
            for (size_t i = 0; i < d.size(); ++i) d[i] = a1[i] - b1[i];
            const double full = rms (d) / std::max (1e-9, rms (a1));
            const double high = rms (highPass (d, 300)) / std::max (1e-9, rms (highPass (a1, 300)));
            MEASURE ("%s %s: diferencia de audio %.2f (por encima de 300 Hz %.2f)", style.c_str(), op.c_str(), full, high);
            CHECK_MSG (high >= (op == "mutatePart" ? 0.25 : 0.40), style + "/" + op + ": cambio poco audible en el altavoz: " + std::to_string (high));
        }
}

TEST (variations_respect_locks_and_explain_failures)
{
    Rig r ("var092_lock");
    CHECK (r.create ("electronic").value ("ok", false));
    const json s0 = r.state();
    std::string drums;
    for (const auto& [id, role] : rolesOf (s0)) if (role == "drums") drums = id;
    CHECK (! drums.empty());
    CHECK (r.cmd ({ { "cmd", "setPartLocked" }, { "trackId", drums }, { "on", true } }).value ("ok", false));
    for (const json& q : { json { { "cmd", "mutatePart" }, { "part", "all" }, { "seed", 5 } }, json { { "cmd", "regenerateBeat" }, { "seed", 6 } },
                           json { { "cmd", "varyPart" }, { "part", "all" }, { "seed", 7 } } })
    {
        const json before = r.state();
        const json res = r.cmd (q);
        CHECK_MSG (res.value ("ok", false), res.dump());
        CHECK (trackEvents (r.state(), drums) == trackEvents (before, drums)); // la batería bloqueada no se toca
        const auto skipped = res.at ("variation").at ("skippedLocked");
        CHECK (skipped.size() == 1 && skipped[0] == "Drums");
        for (const auto& p : res.at ("variation").at ("parts")) CHECK (p.value ("trackId", std::string()) != drums);
    }
    // Solo la parte bloqueada: mensaje comprensible y sin paso de deshacer vacío.
    const json before = r.state();
    const json res = r.cmd ({ { "cmd", "mutatePart" }, { "part", "drums" }, { "seed", 9 } });
    CHECK (! res.value ("ok", true));
    CHECK_MSG (res.value ("error", std::string()).find ("bloqueadas") != std::string::npos, res.dump());
    CHECK (content (r.state()) == content (before));
    CHECK (revisionOf (r.state()) == revisionOf (before));
    // Todo bloqueado: Regenerar también lo explica.
    for (const auto& [id, role] : rolesOf (s0)) r.cmd ({ { "cmd", "setPartLocked" }, { "trackId", id }, { "on", true } });
    const json res2 = r.cmd ({ { "cmd", "regenerateBeat" }, { "seed", 11 } });
    CHECK (! res2.value ("ok", true) && res2.value ("error", std::string()).find ("bloqueadas") != std::string::npos);
    // Sin proyecto: error, nunca un «ok» silencioso.
    Rig empty ("var092_none");
    CHECK (! empty.cmd ({ { "cmd", "mutatePart" }, { "part", "all" }, { "seed", 1 } }).value ("ok", true));
}

// La nueva versión se guarda: tras guardar, cerrar y reabrir siguen las notas nuevas y el número de versión. Los
// proyectos de la 0.9.1 (sin "revision") se abren como versión 1.
TEST (variations_persist_and_old_plans_still_open)
{
    Rig r ("var092_persist");
    CHECK (r.create ("electronic").value ("ok", false));
    CHECK (r.cmd ({ { "cmd", "regenerateBeat" }, { "seed", 77 } }).value ("ok", false));
    CHECK (r.cmd ({ { "cmd", "mutatePart" }, { "part", "melody" }, { "seed", 78 } }).value ("ok", false));
    const json st = r.state();
    CHECK (revisionOf (st) == 3);
    CHECK (r.cmd ({ { "cmd", "save" } }).value ("ok", false));
    CHECK (r.cmd ({ { "cmd", "closeProject" } }).value ("ok", false));
    CHECK (r.cmd ({ { "cmd", "openProject" }, { "folder", r.dir + "/p" } }).value ("ok", false));
    const json re = r.state();
    CHECK (content (re) == content (st));
    CHECK (revisionOf (re) == 3);
    // Plan de la 0.9.1: sin campo revision.
    MusicPlan p;
    CHECK (MusicPlan::fromJson (R"({"genre":"electronic","keyRoot":5,"minor":true,"progression":[0,5,2,6],"seeds":{"drums":11}})", p));
    CHECK (p.revision == 1);
    CHECK (MusicPlan::fromJson (p.toJson(), p) && p.toJson().find ("revision") == std::string::npos); // no cambia el formato si no se usa
}

// Hotfix 0.9.2 — «Grabar sin acompañamiento»: con setRecordDuck(-120) el beat y el metrónomo callan SOLO durante la
// toma (la salida queda en silencio), la cuenta previa sigue sonando, la toma se graba y, al parar, todo vuelve a
// sonar con la mezcla intacta.
TEST (record_without_backing_silences_beat_and_click_only_during_the_take)
{
    Rig r ("rec092_solo");
    CHECK (r.create ("electronic").value ("ok", false));
    const json st = r.state();
    std::string voice;
    for (const auto& t : st.at ("project").at ("tracks")) if (t.value ("kind", std::string()) == "audio") voice = t.value ("id", std::string());
    CHECK (! voice.empty());
    const std::string mixBefore = [&] { std::string m; for (const auto& t : st.at ("project").at ("tracks")) m += std::to_string (t.value ("volumeDb", 0.0)) + "/" + std::to_string (t.value ("mute", false)) + ";"; return m; }();
    CHECK (r.cmd ({ { "cmd", "setMetronome" }, { "on", true } }).value ("ok", false));
    CHECK (r.cmd ({ { "cmd", "setRecordDuck" }, { "db", -120 } }).value ("ok", false));
    CHECK (r.cmd ({ { "cmd", "startRecording" }, { "trackId", voice }, { "channels", 1 }, { "countInBars", 1 } }).value ("ok", false));
    // Cuenta previa (1 compás a 124 BPM ≈ 1,94 s): los clics se oyen.
    const auto countIn = r.run (60);
    CHECK_MSG (rms (countIn) > 1e-3, "la cuenta previa debe oírse");
    r.run (40); // termina la cuenta y la rampa de 20 ms
    const auto take = r.run (200);
    MEASURE ("salida durante la toma sin acompañamiento: RMS %.2e", rms (take));
    CHECK_MSG (rms (take) < 1e-6, "durante la toma no debe sonar el beat ni el metrónomo");
    CHECK (r.cmd ({ { "cmd", "stop" } }).value ("ok", false));
    const json after = r.state();
    bool hasClip = false;
    for (const auto& t : after.at ("project").at ("tracks")) if (t.value ("id", std::string()) == voice) hasClip = ! t.at ("clips").empty();
    CHECK_MSG (hasClip, "la toma debe quedar grabada");
    const std::string mixAfter = [&] { std::string m; for (const auto& t : after.at ("project").at ("tracks")) m += std::to_string (t.value ("volumeDb", 0.0)) + "/" + std::to_string (t.value ("mute", false)) + ";"; return m; }();
    CHECK (mixAfter == mixBefore); // la mezcla no se tocó
    // Reproducir (sin grabar) suena con normalidad aunque el ajuste siga puesto: solo actúa durante la grabación.
    CHECK (r.cmd ({ { "cmd", "seek" }, { "frame", 4 * 48000 } }).value ("ok", false));
    CHECK (r.cmd ({ { "cmd", "play" } }).value ("ok", false));
    const auto play = r.run (120);
    CHECK_MSG (rms (play) > 1e-3, "después de grabar, el beat vuelve a sonar");
    CHECK (r.cmd ({ { "cmd", "stop" } }).value ("ok", false));
}

// 0.10 — La CI encontró que «Variar» una sola parte podía cambiar menos del 15 % con algunas semillas (la interfaz
// las elige al azar; las pruebas anteriores usaban semillas fijas). En la 0.9.2 pasaba en 38 de 400 pulsaciones en
// Reggaetón, casi siempre la batería: ocho intentos de una sola pasada no bastaban. Aquí se recorre la secuencia de la
// interfaz (Variar todo, Regenerar, y cada botón de Variar y Rehacer) con muchas semillas en los ocho estilos.
TEST (variations_reach_the_minimum_change_with_many_seeds)
{
    uint32_t rng = 12345;
    auto next = [&] { rng = rng * 1664525u + 1013904223u; return (long long) (rng % 0xFFFFFFFEu) + 1; };
    int total = 0, deep = 0;
    for (const auto& style : kStyles)
    {
        int fails = 0;
        for (int rep = 0; rep < 12; ++rep)
        {
            Rig r ("seeds010_" + style);
            CHECK (r.create (style).value ("ok", false));
            r.cmd ({ { "cmd", "mutatePart" }, { "part", "all" }, { "seed", next() } });
            r.cmd ({ { "cmd", "regenerateBeat" }, { "seed", next() } });
            for (const char* c : { "mutatePart", "varyPart" })
                for (const auto& p : kParts)
                {
                    const json before = r.state();
                    std::string id;
                    for (const auto& [i, role] : rolesOf (before)) if (role == p) id = i;
                    if (id.empty()) continue;
                    const long long seed = next();
                    const json res = r.cmd ({ { "cmd", c }, { "part", p }, { "seed", seed } });
                    const double ch = change (trackEvents (before, id), trackEvents (r.state(), id));
                    ++total;
                    if (res.value ("variation", json::object()).value ("attempts", 1) > 8) ++deep;
                    if (ch < 0.15) { ++fails; CHECK_MSG (false, style + "/" + c + "/" + p + " semilla " + std::to_string (seed) + ": cambio " + std::to_string (ch)); }
                }
        }
        CHECK_MSG (fails == 0, style);
    }
    MEASURE ("%d pulsaciones con semillas distintas, todas con cambio >= 15 %%; %d necesitaron más de una pasada", total, deep);
}
