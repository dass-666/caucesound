// Channel Rack (misión 0.7, prioridad 3): una pieza de batería puede ir a su propio canal del mezclador. Comprueba
// que el enrutado es real (el audio pasa por el canal: su fader, sus efectos, su mute y su solo actúan solo sobre
// esa pieza), que a ganancia unidad suena igual que sin enrutar, que en vivo suena igual que en la exportación, que
// se guarda (formato v3), se deshace entero y que un proyecto v2 se abre igual y conserva su original.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <cmath>

using namespace cs;
using nlohmann::json;

namespace
{
constexpr int kSr = 48000;
struct Fx
{
    Engine engine;
    Session session { engine };
    std::string dir;
    explicit Fx (const std::string& name) { dir = cstest::tempDir (name); engine.prepare (kSr, 512); }
    json cmd (const json& q) { return json::parse (handleCommand (session, engine, q.dump())); }
};

AudioData render (Session& s, const std::string& path)
{
    CHECK (s.exportMix (path, 32).ok);
    AudioData a; std::string err;
    CHECK (readWav (path, a, err));
    return a;
}
double maxDiff (const AudioData& a, const AudioData& b)
{
    double d = 0;
    const size_t n = std::min (a.channels[0].size(), b.channels[0].size());
    for (int c = 0; c < 2; ++c) for (size_t i = 0; i < n; ++i) d = std::max (d, (double) std::fabs (a.channels[(size_t) c][i] - b.channels[(size_t) c][i]));
    return d;
}
double rms (const AudioData& a) { double s = 0; for (float x : a.channels[0]) s += (double) x * x; return std::sqrt (s / std::max<size_t> (1, a.channels[0].size())); }

// Batería sola con bombo (carril 0) y charles (carril 3) para poder distinguirlos.
std::string drumsOnly (Fx& f)
{
    CHECK (f.session.createProject (f.dir + "/p", "x", kSr).ok);
    const auto d = f.session.addTrack ("Drums", "drums");
    const auto m = f.session.model();
    const auto& pat = m.findTrack (d.id)->patterns.front();
    for (int s = 0; s < 16; s += 4) CHECK (f.session.setPatternStep (d.id, pat.id, 0, s, 120).ok);
    for (int s = 2; s < 16; s += 2) CHECK (f.session.setPatternStep (d.id, pat.id, 3, s, 90).ok);
    // Por debajo de la protección de salida de la batería (rodilla en 0,8), que actúa sobre la suma de las piezas
    // que salen por la pista: así «enrutado» y «sin enrutar» deben ser idénticos.
    CHECK (f.session.setInstrumentParam (d.id, "volumeDb", -12).ok);
    f.session.endParamGesture();
    return d.id;
}
} // namespace

TEST (channel_rack_drum_piece_on_own_channel_is_real_routing)
{
    Fx f ("rack_route");
    const std::string drums = drumsOnly (f);
    const AudioData before = render (f.session, f.dir + "/a.wav");
    const std::string json0 = toJson (f.session.model());

    const json r = f.cmd ({ { "cmd", "routeDrumLane" }, { "trackId", drums }, { "lane", 0 }, { "own", true } });
    CHECK_MSG (r.value ("ok", false), r.dump());
    const std::string kick = r.value ("id", std::string());
    const auto m = f.session.model();
    const auto* kt = m.findTrack (kick);
    CHECK (kt && kt->kind == "bus" && kt->name == "Kick 808"); // nombre de la pieza en el kit de la pista
    CHECK (m.findTrack (drums)->laneRoutes.at (0) == kick);
    CHECK (m.tracks.at (1).id == kick); // justo después de su batería
    // Pedirlo otra vez no crea otro canal.
    CHECK (f.cmd ({ { "cmd", "routeDrumLane" }, { "trackId", drums }, { "lane", 0 }, { "own", true } }).value ("id", std::string()) == kick);

    // A ganancia unidad y sin efectos suena igual que antes (el canal es transparente).
    const AudioData routed = render (f.session, f.dir + "/b.wav");
    MEASURE ("bombo en su canal, sin tocar nada: diferencia máxima con la batería sin enrutar %.2e", maxDiff (routed, before));
    CHECK (maxDiff (routed, before) < 1e-6);

    // El fader de la batería ya no afecta al bombo; el del canal «Kick» sí, y solo a él.
    CHECK (f.session.setTrackMute (drums, true).ok);
    const AudioData onlyKick = render (f.session, f.dir + "/c.wav");
    CHECK (rms (onlyKick) > 0.01);
    CHECK (f.session.setTrackMute (drums, false).ok);
    CHECK (f.session.setTrackMute (kick, true).ok);
    const AudioData noKick = render (f.session, f.dir + "/d.wav");
    CHECK (f.session.setTrackMute (kick, false).ok);
    // bombo solo + resto sin bombo = todo (las dos partes son complementarias).
    double worst = 0;
    for (size_t i = 0; i < routed.channels[0].size() && i < onlyKick.channels[0].size() && i < noKick.channels[0].size(); ++i)
        worst = std::max (worst, (double) std::fabs (onlyKick.channels[0][i] + noKick.channels[0][i] - routed.channels[0][i]));
    MEASURE ("bombo solo + batería sin bombo - todo: %.2e", worst);
    CHECK (worst < 1e-5);

    // Un efecto en el canal «Kick» cambia el sonido; en bypass vuelve a ser idéntico.
    const auto lp = f.session.addInsert (kick, "cs.filter");
    CHECK (lp.ok && f.session.setInsertParam (lp.id, "cutoffHz", 200).ok);
    const AudioData filtered = render (f.session, f.dir + "/e.wav");
    CHECK (maxDiff (filtered, routed) > 1e-3);
    CHECK (f.session.setInsertBypass (lp.id, true).ok);
    CHECK (maxDiff (render (f.session, f.dir + "/f.wav"), routed) < 1e-6);
    CHECK (f.session.setInsertBypass (lp.id, false).ok);

    // Solo en el canal «Kick»: suena solo el bombo (como cualquier canal).
    CHECK (f.session.setTrackSolo (kick, true).ok);
    CHECK (maxDiff (render (f.session, f.dir + "/g.wav"), [&] { CHECK (f.session.setTrackSolo (kick, false).ok); CHECK (f.session.setTrackMute (drums, true).ok); auto a = render (f.session, f.dir + "/h.wav"); CHECK (f.session.setTrackMute (drums, false).ok); CHECK (f.session.setTrackSolo (kick, true).ok); return a; }()) < 1e-6);
    CHECK (f.session.setTrackSolo (kick, false).ok);

    // En vivo suena igual que en la exportación (mismo camino, bloques de 192).
    {
        f.engine.seek (0);
        f.engine.play();
        std::vector<float> buf (192 * 2);
        double d = 0;
        for (Frame done = 0; done + 192 <= 4 * kSr; done += 192)
        {
            f.engine.process (nullptr, 0, buf.data(), 2, 192);
            for (int i = 0; i < 192; ++i) d = std::max (d, (double) std::fabs (buf[(size_t) i * 2] - filtered.channels[0][(size_t) (done + i)]));
        }
        f.engine.stop();
        MEASURE ("vivo frente a exportación con el bombo en su canal: %.2e", d);
        CHECK (d < 1e-5);
    }

    // Guardar y reabrir: formato v3, la ruta y el efecto siguen ahí y suena igual.
    CHECK (f.session.save().ok);
    std::string file;
    CHECK (readFile (f.dir + "/p/project.json", file) && json::parse (file).value ("version", 0) == 3);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (f.session.model().findTrack (drums)->laneRoutes.at (0) == kick);
    CHECK (maxDiff (render (f.session, f.dir + "/i.wav"), filtered) < 1e-6);

    // Devolver la pieza a la batería borra su canal; deshacer lo recupera entero (con su efecto).
    CHECK (f.cmd ({ { "cmd", "routeDrumLane" }, { "trackId", drums }, { "lane", 0 }, { "own", false } }).value ("ok", false));
    CHECK (f.session.model().findTrack (kick) == nullptr && f.session.model().findTrack (drums)->laneRoutes.empty());
    CHECK (maxDiff (render (f.session, f.dir + "/j.wav"), before) < 1e-6);
    CHECK (f.session.undo().ok);
    CHECK (f.session.model().findTrack (kick) && f.session.model().findTrack (kick)->inserts.size() == 1);
    // Borrar el canal desde el mezclador también devuelve la pieza a su batería (no queda una ruta rota).
    CHECK (f.session.removeTrack (kick).ok);
    CHECK (f.session.model().findTrack (drums)->laneRoutes.empty());
    CHECK (validate (f.session.model()).empty());
    (void) json0;
}

TEST (channel_rack_limits_and_invalid_routes_are_rejected)
{
    Fx f ("rack_limits");
    const std::string drums = drumsOnly (f);
    CHECK (! f.cmd ({ { "cmd", "routeDrumLane" }, { "trackId", drums }, { "lane", 8 }, { "own", true } }).value ("ok", true));
    const auto synth = f.session.addTrack ("Bass", "synth");
    CHECK (! f.cmd ({ { "cmd", "routeDrumLane" }, { "trackId", synth.id }, { "lane", 0 }, { "own", true } }).value ("ok", true));
    for (int l = 0; l < 8; ++l) CHECK (f.cmd ({ { "cmd", "routeDrumLane" }, { "trackId", drums }, { "lane", l }, { "own", true } }).value ("ok", false));
    const auto extra = f.session.addTrack ("Reverb", "bus");
    CHECK (! extra.ok); // 8 canales ocupados: no hay sitio para otro bus
    // Rutas inválidas en el archivo: el proyecto se rechaza (y se recurre a la copia de seguridad), no se adivina.
    ProjectModel m = f.session.model();
    m.findTrack (drums)->laneRoutes[1] = synth.id;
    CHECK (! validate (m).empty());
}

TEST (channel_rack_v2_project_opens_unchanged_and_keeps_its_original)
{
    Fx f ("rack_v2");
    drumsOnly (f);
    CHECK (f.session.save().ok);
    const AudioData ref = render (f.session, f.dir + "/ref.wav");
    f.session.close();
    // Lo mismo escrito como versión 2 (lo que guarda la 0.6): sin laneRoutes.
    std::string file;
    CHECK (readFile (f.dir + "/p/project.json", file));
    json j = json::parse (file);
    j["version"] = 2;
    const std::string v2 = j.dump (2);
    std::string err;
    CHECK (atomicWriteFile (f.dir + "/p/project.json", v2, err, false));
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (f.session.model().version == kProjectFormatVersion);
    CHECK (maxDiff (render (f.session, f.dir + "/v2.wav"), ref) == 0.0);
    CHECK (f.session.save().ok);
    std::string backup;
    CHECK (readFile (f.dir + "/p/project.v2.json", backup) && backup == v2);
}
