// Misión 0.10 — AutoMix 2: además de los faders por papel (0.9), propone el equilibrio bombo/caja, deja sitio a la voz,
// avisa de choques de graves y evita recortes. Todo por reglas medibles (render offline + filtros): sin IA. Estas
// pruebas provocan cada desequilibrio y comprueban que la propuesta va en la dirección correcta, que nada cambia hasta
// aplicar, y que se puede comparar A/B y revertir de forma exacta. No pueden decir si la mezcla «suena mejor».
#include "Test.h"

#include "cs/Api.h"
#include "cs/Composer.h"
#include "cs/Packs.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>

using namespace cs;
using nlohmann::json;

namespace
{
struct Rig
{
    Engine engine;
    Session session { engine };
    std::string dir;
    explicit Rig (const std::string& name, const std::string& style)
    {
        packLibrary().configure (cstest::tempDir ("am2_nopacks"), "");
        dir = cstest::tempDir (name);
        engine.prepare (48000, 512);
        CHECK (session.createProject (dir + "/p", "x", 48000, style).ok);
    }
    std::string role (const std::string& r)
    {
        MusicPlan p;
        MusicPlan::fromJson (session.model().music, p);
        for (const auto& [tid, x] : p.roles) if (x == r) return tid;
        return {};
    }
    const AutoMixSuggestion* find (const std::vector<AutoMixSuggestion>& v, const std::string& id) { for (const auto& x : v) if (x.trackId == id) return &x; return nullptr; }
};
std::vector<Session::MixChange> changesOf (const std::vector<AutoMixSuggestion>& v)
{
    std::vector<Session::MixChange> ch;
    for (const auto& x : v) if (x.change) ch.push_back ({ x.trackId, x.suggestedVolumeDb, x.suggestedPan });
    return ch;
}
// Una «voz» sintética (armónicos de 196 Hz con vibrato y sílabas) de 'seconds' a nivel 'gain'.
std::string voiceWav (const std::string& dir, double seconds, float gain)
{
    AudioData a;
    a.sampleRate = 48000;
    a.allocate (1, (Frame) (seconds * 48000));
    double ph = 0;
    for (size_t i = 0; i < a.channels[0].size(); ++i)
    {
        const double t = (double) i / 48000.0;
        ph += 2.0 * M_PI * 196.0 * (1.0 + 0.01 * std::sin (2 * M_PI * 5.0 * t)) / 48000.0;
        double s = 0;
        for (int h = 1; h <= 10; ++h) s += std::sin (ph * h) / h;
        a.channels[0][i] = (float) (gain * s * (0.5 - 0.5 * std::cos (2 * M_PI * std::fmod (t, 0.6) / 0.6)));
    }
    std::string err;
    const std::string path = dir + "/voz.wav";
    writeWav (path, a, WavFormat::Float32, err);
    return path;
}
} // namespace

TEST (automix2_untouched_templates_get_no_changes_in_any_style)
{
    for (const std::string g : { "trap", "reggaeton", "hiphop", "boombap", "jazzrap", "darkhiphop", "pop", "electronic" })
    {
        Rig r ("am2_clean_" + g, g);
        const auto sug = r.session.autoMixSuggest (16);
        bool snare = false, master = false;
        for (const auto& x : sug)
        {
            CHECK_MSG (! x.change, g + " / " + x.name + ": " + x.reason);
            snare = snare || x.role == "snare";
            master = master || x.role == "master";
        }
        CHECK (snare && master); // las reglas nuevas se evaluaron (y no encontraron nada que cambiar)
        r.session.close();
    }
}

TEST (automix2_balances_kick_and_snare_for_the_style_and_compares_a_b)
{
    Rig r ("am2_snare", "boombap");
    const std::string drums = r.role ("drums");
    CHECK (r.session.setInstrumentParam (drums, "vol1", -12.0f).ok); // la caja, 8 dB por debajo de la plantilla (-4)
    const std::string snapshot = toJson (r.session.model());
    const auto sug = r.session.autoMixSuggest (16);
    CHECK (toJson (r.session.model()) == snapshot); // analizar no cambia nada
    const AutoMixSuggestion* s = r.find (sug, drums + "#snare");
    CHECK (s != nullptr);
    if (! s) return;
    MEASURE ("caja a -12 dB: caja %.1f dB frente a bombo %.1f dB (objetivo %.1f); propone %.1f -> %.1f (%s)", s->measuredDb, s->referenceDb, s->targetDb, s->volumeDb, s->suggestedVolumeDb, s->reason.c_str());
    CHECK (s->change && s->suggestedVolumeDb > s->volumeDb + 1.9f && s->suggestedVolumeDb <= s->volumeDb + 4.01f); // sube, con tope de 4 dB
    auto snareVol = [&] { return json::parse (r.session.model().findTrack (drums)->instrument.state).value ("vol1", 0.0f); };
    CHECK (r.session.applyAutoMix (changesOf (sug)).ok);
    const float applied = snareVol();
    CHECK (std::fabs (applied - s->suggestedVolumeDb) < 0.01f);
    // Comparar A/B: alterna sin perder ninguna de las dos mezclas.
    OpResult c = r.session.compareAutoMix();
    CHECK (c.ok && c.id == "before");
    CHECK (std::fabs (snareVol() + 12.0f) < 0.01f);
    c = r.session.compareAutoMix();
    CHECK (c.ok && c.id == "automix");
    CHECK (std::fabs (snareVol() - applied) < 0.01f);
    // Revertir deja exactamente lo de antes, se esté escuchando A o B.
    CHECK (r.session.compareAutoMix().ok); // suena «antes»
    CHECK (r.session.revertAutoMix().ok);
    CHECK (std::fabs (snareVol() + 12.0f) < 0.01f);
    CHECK (! r.session.canRevertAutoMix());
    CHECK (! r.session.compareAutoMix().ok);
    // Con otro kit no hay referencia: no se inventa una propuesta.
    CHECK (r.session.setInstrumentParam (drums, "vol1", -4.0f).ok);
    r.session.close();
}

TEST (automix2_prevents_clipping_with_the_master_and_export_confirms_it)
{
    Rig r ("am2_clip", "trap");
    CHECK (r.session.setMasterDb (9.0f).ok); // mezcla empujada: recorta
    long clipped = 0;
    CHECK (r.session.exportMix (r.dir + "/antes.wav", 16, 0, &clipped).ok);
    CHECK (clipped > 0);
    const auto sug = r.session.autoMixSuggest (120);
    const AutoMixSuggestion* m = r.find (sug, "@master");
    CHECK (m != nullptr);
    if (! m) return;
    MEASURE ("máster a +9 dB: pico %.1f dBFS, %ld muestras recortadas; propone máster %.1f -> %.1f dB", m->measuredDb, clipped, m->volumeDb, m->suggestedVolumeDb);
    CHECK (m->change && m->suggestedVolumeDb < m->volumeDb);
    CHECK (r.session.applyAutoMix (changesOf (sug)).ok);
    CHECK (std::fabs (r.session.model().masterDb - m->suggestedVolumeDb) < 0.01f);
    CHECK (r.session.exportMix (r.dir + "/despues.wav", 16, 0, &clipped).ok);
    CHECK (clipped == 0);
    AudioData a; std::string err;
    CHECK (readWav (r.dir + "/despues.wav", a, err));
    float peak = 0;
    for (const auto& ch : a.channels) for (float v : ch) peak = std::max (peak, std::fabs (v));
    MEASURE ("tras aplicar: pico de la exportación %.2f dBFS, 0 muestras recortadas", 20 * std::log10 (peak));
    CHECK (peak <= 0.8913f * 1.01f && peak > 0.7f); // -1 dBFS (y sin pasarse de frenada)
    CHECK (r.session.revertAutoMix().ok);
    CHECK (std::fabs (r.session.model().masterDb - 9.0f) < 0.01f);
    r.session.close();
}

TEST (automix2_makes_room_for_a_recorded_vocal_only_when_it_is_buried)
{
    for (const bool buried : { true, false })
    {
        Rig r (buried ? "am2_vocal_buried" : "am2_vocal_clear", "jazzrap"); // el estilo con los teclados más presentes
        std::string voz;
        for (const auto& t : r.session.model().tracks) if (t.kind == "audio") voz = t.id;
        CHECK (r.session.importWav (voiceWav (r.dir, 12.0, buried ? 0.004f : 0.25f), voz, 48000 * 5).ok);
        const std::string snapshot = toJson (r.session.model());
        const auto sug = r.session.autoMixSuggest (20);
        CHECK (toJson (r.session.model()) == snapshot);
        int room = 0;
        for (const auto& x : sug)
            if (x.reason.find ("voz") != std::string::npos)
            {
                ++room;
                MEASURE ("voz %s: %s %.1f -> %.1f dB (%s)", buried ? "tapada" : "clara", x.name.c_str(), x.volumeDb, x.suggestedVolumeDb, x.reason.c_str());
                CHECK (x.role == "chords" || x.role == "pad" || x.role == "melody"); // nunca la batería ni el bajo
                CHECK (x.change && x.suggestedVolumeDb < x.volumeDb && x.suggestedVolumeDb >= x.volumeDb - 6.01f);
            }
        CHECK (room == (buried ? 1 : 0));
        r.session.close();
    }
}

TEST (automix2_flags_low_end_fighting_the_bass_without_touching_eq)
{
    Rig r ("am2_low", "hiphop");
    const std::string chords = r.role ("chords");
    // Los acordes pasan a tocar con el sonido del sub y en el registro del bajo: casi todo por debajo de 150 Hz.
    CHECK (r.session.loadSynthPreset (chords, "cauce_sub").ok);
    const ProjectModel m0 = r.session.model();
    for (const auto& c : m0.findTrack (chords)->midiClips)
    {
        std::vector<MidiNote> low;
        for (auto n : c.notes)
        {
            n.key = 36 + n.key % 12;
            bool dup = false;
            for (const auto& o : low) dup = dup || (o.key == n.key && o.start < n.start + n.length && n.start < o.start + o.length);
            if (! dup) low.push_back (n);
        }
        const OpResult set = r.session.setClipNotes (c.id, low);
        CHECK_MSG (set.ok, set.error);
    }
    const std::string snapshot = toJson (r.session.model());
    const auto sug = r.session.autoMixSuggest (16);
    const AutoMixSuggestion* c = r.find (sug, chords);
    CHECK (c != nullptr);
    if (! c) return;
    MEASURE ("acordes con sonido de sub: %s", c->reason.c_str());
    CHECK (c->reason.find ("graves") != std::string::npos);
    CHECK (toJson (r.session.model()) == snapshot); // solo es un aviso: no hay EQ automático
    // El comando de la app devuelve lo mismo y admite los ids nuevos.
    const json j = json::parse (handleCommand (r.session, r.engine, R"({"cmd":"autoMixSuggest","seconds":8})"));
    CHECK (j.value ("ok", false));
    bool master = false;
    for (const auto& x : j["suggestions"]) master = master || x.value ("trackId", std::string()) == "@master";
    CHECK (master);
    CHECK (! json::parse (handleCommand (r.session, r.engine, R"({"cmd":"autoMixCompare"})")).value ("ok", true)); // nada aplicado
    r.session.close();
}
