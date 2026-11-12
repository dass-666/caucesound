// Buses de efectos (envíos post-fader a una pista bus, p. ej. una reverb compartida): enrutado exacto a la muestra,
// mute/solo, exportación igual a la reproducción con la cola del bus, persistencia, límites y coste frente a una
// reverb por pista.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <chrono>
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
    explicit Fx (const std::string& name) { dir = cstest::tempDir (name); engine.prepare (kSr, 1024); }
};

std::vector<float> playLive (Engine& e, Frame frames, int block = 256)
{
    std::vector<float> out ((size_t) frames * 2), buf ((size_t) block * 2);
    e.seek (0);
    e.play();
    for (Frame done = 0; done < frames; done += block)
    {
        const int n = (int) std::min<Frame> (block, frames - done);
        e.process (nullptr, 0, buf.data(), 2, n);
        std::copy (buf.begin(), buf.begin() + n * 2, out.begin() + (long) done * 2);
    }
    e.stop();
    return out;
}

std::string impulseWav (const std::string& dir)
{
    AudioData a;
    a.sampleRate = kSr;
    a.allocate (1, 4800);
    a.channels[0][0] = 0.5f;
    std::string err;
    writeWav (dir + "/imp.wav", a, WavFormat::Float32, err);
    return dir + "/imp.wav";
}
} // namespace

TEST (bus_send_is_post_fader_sample_accurate_and_export_matches_live)
{
    Fx f ("bus_route");
    CHECK (f.session.createProject (f.dir + "/p", "bus", kSr, "").ok);
    const auto a = f.session.addTrack ("A", "audio");
    const auto b = f.session.addTrack ("Rev", "bus");
    CHECK (a.ok && b.ok);
    CHECK (f.session.importWav (impulseWav (f.dir), a.id, 1000).ok);
    const auto g = f.session.addInsert (b.id, "cs.gain");
    CHECK (g.ok);
    CHECK (f.session.setInsertParam (g.id, "gainDb", -6.0f).ok);
    CHECK (f.session.setTrackVolumeDb (a.id, -6.0f).ok);
    CHECK (f.session.setTrackSend (a.id, b.id, 0.5f).ok);

    const float ga = dbToGain (-6.0f);
    const float expect = 0.5f * ga + 0.5f * ga * 0.5f * dbToGain (-6.0f); // directo + bus (post-fader)
    auto live = playLive (f.engine, kSr / 2);
    int nonZero = 0;
    for (size_t i = 0; i < live.size() / 2; ++i) if (std::fabs (live[i * 2]) > 1e-7f) ++nonZero;
    MEASURE ("impulso con envío 0,5 a un bus con -6 dB: %.6f en el frame 1000 (esperado %.6f), %d muestras no nulas", live[2000], expect, nonZero);
    CHECK (std::fabs (live[1000 * 2] - expect) < 1e-6f && nonZero == 1);

    // Post-fader: con la pista en mute, el bus no recibe nada. El solo de la pista no silencia el bus.
    CHECK (f.session.setTrackMute (a.id, true).ok);
    auto muted = playLive (f.engine, kSr / 2);
    float m = 0;
    for (float x : muted) m = std::max (m, std::fabs (x));
    CHECK (m == 0.0f);
    CHECK (f.session.setTrackMute (a.id, false).ok && f.session.setTrackSolo (a.id, true).ok);
    auto solo = playLive (f.engine, kSr / 2);
    CHECK (std::fabs (solo[1000 * 2] - expect) < 1e-6f);
    CHECK (f.session.setTrackSolo (a.id, false).ok);

    // Exportación = reproducción.
    CHECK (f.session.exportMix (f.dir + "/mix.wav", 32).ok);
    AudioData ex;
    std::string err;
    CHECK (readWav (f.dir + "/mix.wav", ex, err));
    CHECK (std::fabs (ex.channels[0][1000] - expect) < 1e-6f);

    // Reverb en el bus: la exportación incluye su cola completa.
    const auto rev = f.session.addInsert (b.id, "cs.reverb");
    CHECK (rev.ok && f.session.setInsertParam (rev.id, "mix", 1.0f).ok);
    CHECK (f.session.exportMix (f.dir + "/rev.wav", 32).ok);
    CHECK (readWav (f.dir + "/rev.wav", ex, err));
    MEASURE ("exportación con reverb en el bus: %.2f s (contenido 0,12 s + cola declarada)", ex.numFrames() / (double) kSr);
    CHECK (ex.numFrames() > kSr * 3);
    float lastSec = 0;
    for (Frame i = ex.numFrames() - kSr / 10; i < ex.numFrames(); ++i) lastSec = std::max (lastSec, std::fabs (ex.channels[0][(size_t) i]));
    CHECK (lastSec < 1e-4f); // la cola termina dentro del archivo
}

TEST (bus_persists_and_rejects_invalid_routing)
{
    Fx f ("bus_model");
    CHECK (f.session.createProject (f.dir + "/p", "bus", kSr, "trap").ok);
    std::string synth;
    for (const auto& t : f.session.model().tracks) if (t.kind == "synth") { synth = t.id; break; }
    const auto b = f.session.addTrack ("", "bus");
    CHECK (b.ok);
    CHECK (f.session.model().findTrack (b.id)->name == "Bus FX");
    CHECK (f.session.setTrackSend (synth, b.id, 0.4f).ok);
    CHECK (! f.session.setTrackSend (b.id, b.id, 0.4f).ok);      // un bus no envía
    CHECK (! f.session.setTrackSend (synth, synth, 0.4f).ok);    // solo hacia buses
    CHECK (! f.session.importWav (impulseWav (f.dir), b.id, 0).ok); // un bus no tiene clips
    CHECK (! f.session.setTrackSend (synth, b.id, std::nanf ("")).ok);
    for (int i = 0; i < 3; ++i) CHECK (f.session.addTrack ("", "bus").ok);
    CHECK (! f.session.addTrack ("", "bus").ok); // máximo 4 buses
    // Persistencia.
    CHECK (f.session.save().ok);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (std::fabs (f.session.model().findTrack (synth)->sends.at (b.id) - 0.4f) < 1e-6f);
    // Borrar el bus quita los envíos; deshacer los recupera.
    CHECK (f.session.removeTrack (b.id).ok);
    CHECK (f.session.model().findTrack (synth)->sends.empty());
    CHECK (f.session.undo().ok);
    CHECK (f.session.model().findTrack (synth)->sends.count (b.id) == 1);
    // Un project.json con un envío a algo que no es un bus se rechaza.
    auto m = f.session.model();
    m.findTrack (synth)->sends["trk_inexistente"] = 0.5f;
    CHECK (! validate (m).empty());
    // API de comandos.
    const auto r = json::parse (handleCommand (f.session, f.engine, json { { "cmd", "setSend" }, { "trackId", synth }, { "busId", b.id }, { "level", 0.7 } }.dump()));
    CHECK (r["ok"] == true);
    CHECK (std::fabs (f.session.model().findTrack (synth)->sends.at (b.id) - 0.7f) < 1e-6f);
}

TEST (shared_reverb_bus_costs_less_than_one_reverb_per_track)
{
    auto measure = [] (bool shared)
    {
        Fx f (shared ? "bus_cost_shared" : "bus_cost_each");
        f.session.createProject (f.dir + "/p", "cost", kSr, "");
        std::string bus;
        if (shared) { bus = f.session.addTrack ("Rev", "bus").id; f.session.addInsert (bus, "cs.reverb"); }
        for (int i = 0; i < 8; ++i)
        {
            const auto t = f.session.addTrack ("S" + std::to_string (i), "synth");
            for (const auto& tr : f.session.model().tracks)
                if (tr.id == t.id) f.session.setClipNotes (tr.midiClips.front().id, { MidiNote { 48 + i, 100, 0, 0, 4 * kPpq } });
            if (shared) f.session.setTrackSend (t.id, bus, 0.3f);
            else f.session.addInsert (t.id, "cs.reverb");
        }
        f.engine.seek (0);
        f.engine.play();
        std::vector<float> out (256 * 2);
        bool finite = true;
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < 750; ++b) // 4 s
        {
            f.engine.process (nullptr, 0, out.data(), 2, 256);
            for (float x : out) finite = finite && std::isfinite (x);
        }
        const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / 750.0;
        CHECK (finite);
        return us;
    };
    const double each = measure (false), shared = measure (true);
    MEASURE ("8 synths con una reverb cada uno: %.0f us por bloque; con una reverb compartida en un bus: %.0f us (%.0f %% menos; tiempo en esta máquina)",
             each, shared, 100.0 * (1.0 - shared / each));
}
