// Mixer Pro (misión 0.7, prioridad 2): ranuras de efectos por canal. Comprueba que la cadena visible es la que
// procesa: el orden cambia el sonido, el bypass equivale a no tener el efecto, las cadenas completas (presets) se
// aplican con sus parámetros y se deshacen enteras, todo sobrevive a guardar y reabrir, y la exportación lo incluye.
#include "Test.h"

#include "cs/Api.h"
#include "cs/FxChains.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <set>

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

// Material de prueba: acorde de senos con ataques (rico en armónicos tras saturar), 2 s.
std::string testWav (const std::string& dir)
{
    AudioData a;
    a.sampleRate = kSr;
    a.allocate (2, 2 * kSr);
    for (Frame i = 0; i < 2 * kSr; ++i)
    {
        const double t = (double) i / kSr, env = std::exp (-3.0 * std::fmod (t, 0.5));
        const float v = (float) (0.3 * env * (std::sin (2 * M_PI * 110 * t) + 0.6 * std::sin (2 * M_PI * 440 * t) + 0.3 * std::sin (2 * M_PI * 3300 * t)));
        a.channels[0][(size_t) i] = a.channels[1][(size_t) i] = v;
    }
    std::string err;
    writeWav (dir + "/src.wav", a, WavFormat::Float32, err);
    return dir + "/src.wav";
}

AudioData exportOf (Session& s, const std::string& path)
{
    CHECK (s.exportMix (path, 32).ok);
    AudioData a; std::string err;
    CHECK (readWav (path, a, err));
    return a;
}

double rmsDiff (const AudioData& a, const AudioData& b)
{
    double s = 0, e = 0; const size_t n = std::min (a.channels[0].size(), b.channels[0].size());
    for (size_t i = 0; i < n; ++i) { const double d = a.channels[0][i] - b.channels[0][i]; s += d * d; e += (double) a.channels[0][i] * a.channels[0][i]; }
    return std::sqrt (s / std::max (1e-12, e));
}
bool sameBits (const AudioData& a, const AudioData& b) { return a.channels == b.channels; }

std::vector<std::string> chainTypes (const ProjectModel& m, const std::string& trackId)
{
    std::vector<std::string> v;
    for (const auto& t : m.tracks) if (t.id == trackId) for (const auto& i : t.inserts) v.push_back (i.type + (i.bypass ? "(bypass)" : ""));
    return v;
}
} // namespace

TEST (mixer_catalog_lists_only_real_effects_with_category_description_and_valid_chains)
{
    const std::set<std::string> categories { "eq", "dynamics", "reverb", "delay", "modulation", "distortion", "vocal", "tools" };
    const auto types = effectTypes();
    CHECK (effectInfos().size() == types.size()); // cada efecto disponible tiene ficha y ninguna ficha es inventada
    for (const auto& i : effectInfos())
    {
        CHECK (std::find (types.begin(), types.end(), i.type) != types.end());
        CHECK (categories.count (i.category) == 1);
        CHECK (! i.description.empty() && ! i.descriptionEn.empty() && ! i.name.empty());
        CHECK (i.category != "modulation"); // no hay efectos de modulación: la categoría no debe aparecer
    }
    // Cada cadena: efectos disponibles y parámetros dentro de su rango (se cargan sin recortes).
    CHECK (chainPresets().size() >= 6);
    for (const auto& c : chainPresets())
    {
        CHECK (c.steps.size() >= 2 && c.steps.size() <= 8);
        for (const auto& st : c.steps)
        {
            auto fx = createProcessor (st.type);
            CHECK (fx != nullptr);
            if (! fx) continue;
            for (const auto& kv : st.params)
            {
                const std::string id = kv.first == "timeBeats" ? "timeMs" : kv.first;
                bool found = false;
                for (const auto& p : fx->parameters())
                    if (p.id == id)
                    {
                        found = true;
                        if (kv.first != "timeBeats") CHECK_MSG (kv.second >= p.minValue && kv.second <= p.maxValue, c.id + "/" + id);
                        if (fx->latencySamples() > 0) cstest::fail (__FILE__, __LINE__, "efecto con latencia en una cadena");
                    }
                CHECK_MSG (found, c.id + "/" + id);
            }
        }
    }
    // El catálogo de la API expone categoría, descripción y cadenas.
    Engine e; Session s (e);
    const json cat = json::parse (handleCommand (s, e, R"({"cmd":"catalog"})"));
    CHECK (cat.at ("effects").size() == types.size());
    for (const auto& fx : cat.at ("effects")) CHECK (fx.contains ("category") && fx.contains ("description") && fx.value ("latencySamples", -1) == 0);
    CHECK (cat.at ("fxChains").size() == chainPresets().size());
}

TEST (mixer_slot_order_changes_processing_and_bypass_equals_no_effect)
{
    Fx f ("mixer_order");
    CHECK (f.session.createProject (f.dir + "/p", "x", kSr).ok);
    const auto tr = f.session.addTrack ("Voz", "audio");
    CHECK (f.session.importWav (testWav (f.dir), tr.id, 0).ok);
    const AudioData dry = exportOf (f.session, f.dir + "/dry.wav");

    // Saturación y filtro paso bajo: saturar antes de filtrar quita los armónicos nuevos; después, no.
    const auto sat = f.session.addInsert (tr.id, "cs.saturation");
    const auto lp = f.session.addInsert (tr.id, "cs.filter");
    CHECK (sat.ok && lp.ok);
    CHECK (f.session.setInsertParam (sat.id, "drive", 0.9f).ok);
    CHECK (f.session.setInsertParam (lp.id, "cutoffHz", 600).ok);
    const AudioData satThenLp = exportOf (f.session, f.dir + "/a.wav");
    CHECK (f.session.moveInsert (lp.id, 0).ok);
    CHECK ((chainTypes (f.session.model(), tr.id) == std::vector<std::string> { "cs.filter", "cs.saturation" }));
    const AudioData lpThenSat = exportOf (f.session, f.dir + "/b.wav");
    const double orderDiff = rmsDiff (satThenLp, lpThenSat);
    MEASURE ("saturación→filtro frente a filtro→saturación: diferencia relativa %.3f", orderDiff);
    CHECK (orderDiff > 0.05);
    // Deshacer el movimiento devuelve exactamente el sonido del orden anterior.
    CHECK (f.session.undo().ok);
    CHECK (sameBits (exportOf (f.session, f.dir + "/c.wav"), satThenLp));

    // Bypass de los dos = idéntico al audio sin efectos (bit a bit).
    CHECK (f.session.setInsertBypass (sat.id, true).ok && f.session.setInsertBypass (lp.id, true).ok);
    CHECK (sameBits (exportOf (f.session, f.dir + "/d.wav"), dry));
    // Quitar el bypass recupera los parámetros (no se pierden al pasar por bypass).
    CHECK (f.session.setInsertBypass (sat.id, false).ok && f.session.setInsertBypass (lp.id, false).ok);
    CHECK (sameBits (exportOf (f.session, f.dir + "/e.wav"), satThenLp));
    // Eliminar uno: queda solo el otro y suena distinto de la cadena completa.
    CHECK (f.session.removeInsert (lp.id).ok);
    CHECK ((chainTypes (f.session.model(), tr.id) == std::vector<std::string> { "cs.saturation" }));
    CHECK (rmsDiff (exportOf (f.session, f.dir + "/f.wav"), satThenLp) > 0.05);
}

TEST (mixer_chain_preset_applies_order_and_params_undoes_whole_and_survives_reopen)
{
    Fx f ("mixer_chain");
    CHECK (f.session.createProject (f.dir + "/p", "x", kSr).ok);
    const auto tr = f.session.addTrack ("Voz", "audio");
    CHECK (f.session.importWav (testWav (f.dir), tr.id, 0).ok);
    const auto eq = f.session.addInsert (tr.id, "cs.eq");
    CHECK (f.session.setInsertParam (eq.id, "highDb", 6).ok);
    const std::string before = toJson (f.session.model());
    const AudioData beforeAudio = exportOf (f.session, f.dir + "/before.wav");

    // Añadir al final: EQ del usuario + la cadena «Voz urbana» en su orden.
    json r = f.cmd ({ { "cmd", "applyFxChain" }, { "trackId", tr.id }, { "preset", "vocal_urban" } });
    CHECK_MSG (r.value ("ok", false), r.dump());
    const auto* preset = findChainPreset ("vocal_urban");
    std::vector<std::string> expect { "cs.eq" };
    for (const auto& s : preset->steps) expect.push_back (s.type);
    CHECK (chainTypes (f.session.model(), tr.id) == expect);
    // Parámetros de la cadena aplicados de verdad (el delay en tiempo de corchea a 120 BPM = 250 ms).
    const auto m = f.session.model();
    const auto& ins = m.findTrack (tr.id)->inserts;
    const json comp = json::parse (ins[3].state), dly = json::parse (ins[5].state);
    CHECK_NEAR (comp.value ("ratio", 0.0), 5.0, 1e-4);
    CHECK_NEAR (dly.value ("timeMs", 0.0), 250.0, 0.01);
    CHECK (rmsDiff (exportOf (f.session, f.dir + "/urban.wav"), beforeAudio) > 0.05);

    // Un solo deshacer devuelve la cadena anterior entera (no es irreversible).
    CHECK (f.session.undo().ok);
    { auto a = f.session.model(), b = a; std::string e; fromJson (before, b, e); a.modifiedAt = b.modifiedAt = 0; CHECK_MSG (toJson (a) == toJson (b), toJson (a).substr (0, 0)); }
    CHECK (sameBits (exportOf (f.session, f.dir + "/undo.wav"), beforeAudio));
    CHECK (f.session.redo().ok);

    // Reemplazar: solo la cadena nueva. Pasarse de 8 se rechaza sin tocar nada.
    r = f.cmd ({ { "cmd", "applyFxChain" }, { "trackId", tr.id }, { "preset", "vocal_double" }, { "replace", true } });
    CHECK (r.value ("ok", false));
    CHECK (chainTypes (f.session.model(), tr.id).size() == findChainPreset ("vocal_double")->steps.size());
    const std::string full = toJson (f.session.model());
    r = f.cmd ({ { "cmd", "applyFxChain" }, { "trackId", tr.id }, { "preset", "vocal_urban" } });
    CHECK (! r.value ("ok", true) && r.value ("error", std::string()).find ("máximo 8") != std::string::npos);
    CHECK (toJson (f.session.model()) == full);
    CHECK (! f.cmd ({ { "cmd", "applyFxChain" }, { "trackId", tr.id }, { "preset", "no_existe" } }).value ("ok", true));

    // Guardar y reabrir: mismo orden, mismos parámetros, mismo bypass y mismo audio.
    CHECK (f.session.setInsertBypass (f.session.model().findTrack (tr.id)->inserts[1].id, true).ok);
    const AudioData saved = exportOf (f.session, f.dir + "/saved.wav");
    const auto savedTrack = *f.session.model().findTrack (tr.id);
    CHECK (f.session.save().ok);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    const auto reopened = *f.session.model().findTrack (tr.id);
    CHECK (reopened.inserts.size() == savedTrack.inserts.size());
    for (size_t i = 0; i < reopened.inserts.size() && i < savedTrack.inserts.size(); ++i)
        CHECK (reopened.inserts[i].type == savedTrack.inserts[i].type && reopened.inserts[i].state == savedTrack.inserts[i].state && reopened.inserts[i].bypass == savedTrack.inserts[i].bypass);
    CHECK (sameBits (exportOf (f.session, f.dir + "/reopened.wav"), saved));

    // También en un bus (todos los efectos de las cadenas tienen latencia 0).
    const auto bus = f.session.addTrack ("Bus", "bus");
    CHECK (f.cmd ({ { "cmd", "applyFxChain" }, { "trackId", bus.id }, { "preset", "melody_space" } }).value ("ok", false));
}

TEST (mixer_slot_edits_while_playing_keep_audio_running_and_finite)
{
    Fx f ("mixer_live");
    CHECK (f.session.createProject (f.dir + "/p", "x", kSr, "trap").ok);
    const auto m = f.session.model();
    std::vector<float> buf (512 * 2);
    f.engine.play();
    double energy = 0;
    bool finite = true;
    auto run = [&] (int blocks) { for (int b = 0; b < blocks; ++b) { f.engine.process (nullptr, 0, buf.data(), 2, 512); for (float x : buf) { finite = finite && std::isfinite (x); energy += (double) x * x; } } f.engine.collectGarbage(); };
    run (20);
    for (const auto& t : m.tracks)
    {
        if (t.kind == "bus") continue;
        const auto a = f.session.addInsert (t.id, "cs.eq");
        run (3);
        const auto b = f.session.addInsert (t.id, "cs.reverb");
        run (3);
        CHECK (f.session.moveInsert (b.id, 0).ok);
        run (3);
        CHECK (f.session.setInsertBypass (a.id, true).ok);
        run (3);
        CHECK (f.cmd ({ { "cmd", "applyFxChain" }, { "trackId", t.id }, { "preset", "drums_punch" }, { "replace", true } }).value ("ok", false));
        run (3);
        CHECK (f.session.removeInsert (f.session.model().findTrack (t.id)->inserts.front().id).ok);
        run (3);
    }
    CHECK (f.engine.isPlaying());
    CHECK (finite);
    CHECK (energy > 1.0);
    f.engine.stop();
}
