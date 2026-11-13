// Pruebas de la fase 1.2: MIDI, Cauce Synth, Cauce Drums, bucle, plantillas, formato v2 y compatibilidad con v1.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Instruments.h"
#include "cs/Midi.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <random>
#include <thread>

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
    explicit Fx (const std::string& name, int maxBlock = 1024)
    {
        dir = cstest::tempDir (name);
        engine.prepare (kSr, maxBlock);
    }
};

// Bloques fijos (block > 0) o aleatorios (block <= 0). El transporte no se toca.
std::vector<float> run (Engine& e, Frame frames, int block, uint32_t seed = 3)
{
    std::vector<float> out ((size_t) frames * 2);
    std::mt19937 rng (seed);
    Frame done = 0;
    while (done < frames)
    {
        int n = block > 0 ? block : (int) (rng() % 1100) + 1;
        n = (int) std::min<Frame> (n, frames - done);
        e.process (nullptr, 0, out.data() + done * 2, 2, n);
        done += n;
    }
    return out;
}

std::vector<float> playFrom (Engine& e, Frame from, Frame frames, int block)
{
    e.seek (from);
    e.play();
    auto out = run (e, frames, block);
    e.stop();
    return out;
}

// Deja que terminen las colas (release) con el transporte parado.
void settle (Engine& e) { run (e, kSr * 12, 1024); }

Frame firstNonZero (const std::vector<float>& st, float thr = 1e-7f)
{
    for (size_t i = 0; i < st.size(); ++i) if (std::fabs (st[i]) > thr) return (Frame) (i / 2);
    return -1;
}

float peakOf (const std::vector<float>& st, size_t from = 0, size_t to = SIZE_MAX)
{
    float m = 0;
    for (size_t i = from * 2; i < std::min (st.size(), to * 2); ++i) m = std::max (m, std::fabs (st[i]));
    return m;
}

bool allFinite (const std::vector<float>& v) { for (float x : v) if (! std::isfinite (x)) return false; return true; }

std::string onlyTrackOfKind (Session& s, const std::string& kind)
{
    for (const auto& t : s.model().tracks) if (t.kind == kind) return t.id;
    return {};
}

std::string firstMidiClip (Session& s, const std::string& trackId)
{
    for (const auto& t : s.model().tracks) if (t.id == trackId && ! t.midiClips.empty()) return t.midiClips.front().id;
    return {};
}

MidiNote note (int key, Tick start, Tick len, int vel = 100) { MidiNote n; n.key = key; n.start = start; n.length = len; n.velocity = vel; return n; }
} // namespace

// =================================================================================================================
TEST (midi_ticks_frames_quantize_and_swing)
{
    CHECK (ticksToFrames (kPpq, 120, kSr) == 24000);          // una negra a 120 BPM = 0,5 s
    CHECK (ticksToFrames (kPpq * 4, 140, kSr) == 82286);      // 4 negras a 140 BPM = 1,714 s (redondeado)
    CHECK (framesToTicks (24000, 120, kSr) == kPpq);
    // Sin deriva acumulada: el tick N siempre redondea desde el origen, no sumando bloques.
    for (Tick t = 0; t < kPpq * 4 * 200; t += 37) CHECK (std::llabs (ticksToFrames (t, 133, kSr) - (Frame) std::llround (t * 60.0 * kSr / (133.0 * kPpq))) == 0);

    std::vector<MidiNote> ns = { note (60, 250, 200), note (62, 470, 500), note (64, 10, 10) };
    quantizeNotes (ns, kStepTicks, 1.0f, true);
    CHECK (ns[0].start == 0 && ns[0].length == kStepTicks);           // 10 -> 0, duración mínima = rejilla
    CHECK (ns[1].start == 240 && ns[1].length == 240);                // 250 -> 240
    CHECK (ns[2].start == 480 && ns[2].length == 480);                // 470 -> 480, 500 -> 480
    std::vector<MidiNote> half = { note (60, 100, 100) };
    quantizeNotes (half, kStepTicks, 0.5f, false);
    CHECK (half[0].start == 50 && half[0].length == 100);             // fuerza 50 %: a mitad de camino

    CHECK (stepTick (0, 0.5f) == 0 && stepTick (2, 0.5f) == 2 * kStepTicks); // los pasos impares (0-based pares) no se mueven
    CHECK (stepTick (1, 0.0f) == kStepTicks);
    CHECK (stepTick (1, 0.5f) == kStepTicks + kStepTicks / 4);
    CHECK (stepTick (3, 1.0f) == 3 * kStepTicks + kStepTicks / 2);
}

TEST (pattern_expands_to_events_with_repeats_swing_and_32_steps)
{
    DrumPattern p;
    p.id = "p";
    p.lanes[0][0] = 127;   // bombo en el paso 1
    p.lanes[3][1] = 80;    // charles en el paso 2 (afectado por el swing)
    p.swing = 1.0f;
    MidiClipModel c;
    c.start = 4 * kPpq;    // compás 2
    c.length = 8 * kPpq;   // 2 compases -> el patrón de 1 compás se repite 2 veces
    c.patternId = "p";
    std::vector<SchedEvent> ev;
    appendClipEvents (c, &p, 120, kSr, ev);
    sortEvents (ev);
    CHECK (ev.size() == 8); // 2 golpes x 2 repeticiones x (on + off)
    std::vector<Frame> kicks, hats;
    for (const auto& e : ev)
        if ((e.status & 0xF0) == 0x90) (e.data1 == kDrumBaseNote ? kicks : hats).push_back (e.frame);
    CHECK (kicks.size() == 2 && kicks[0] == 96000 && kicks[1] == 192000);
    CHECK (hats.size() == 2 && hats[0] == 96000 + ticksToFrames (kStepTicks + kStepTicks / 2, 120, kSr));
    CHECK (ev[0].status == 0x99); // canal 10 (batería)

    // 32 pasos: el paso 17 cae en el 2.º compás
    p.steps = 32;
    for (auto& l : p.lanes) l.assign (32, 0);
    p.lanes[1][16] = 100;
    p.swing = 0;
    ev.clear();
    c.length = 4 * kPpq; // el clip corta el patrón: el paso 17 queda fuera
    appendClipEvents (c, &p, 120, kSr, ev);
    CHECK (ev.empty());
    c.length = 8 * kPpq;
    appendClipEvents (c, &p, 120, kSr, ev);
    CHECK (ev.size() == 2 && ev[0].frame == 96000 + 96000);
    CHECK (validatePattern (p).empty());
    p.lanes[2].resize (31);
    CHECK (! validatePattern (p).empty());
}

TEST (synth_note_is_sample_accurate_for_any_block_size)
{
    std::vector<std::vector<float>> outs;
    for (int block : { 64, 1000, 4096, 0 })
    {
        Fx f ("syn_acc", 1024);
        CHECK (f.session.createProject (f.dir + "/p", "t").ok);
        const auto r = f.session.addTrack ("S", "synth");
        CHECK (r.ok);
        CHECK (f.session.loadSynthPreset (r.id, "lead").ok);
        // Nota en el tiempo 2 (tick 960 = frame 24000 a 120 BPM), otra que empieza justo en el límite de un bloque
        CHECK (f.session.setClipNotes (firstMidiClip (f.session, r.id), { note (60, kPpq, kPpq), note (67, kPpq * 2 + 1, kPpq / 3) }).ok);
        auto out = playFrom (f.engine, 0, kSr * 3, block);
        CHECK (allFinite (out));
        CHECK_MSG (firstNonZero (out) == 24000, std::to_string (firstNonZero (out)));
        outs.push_back (std::move (out));
    }
    for (size_t i = 1; i < outs.size(); ++i) CHECK (outs[i] == outs[0]); // idéntico bit a bit
}

TEST (note_off_stop_seek_and_edits_never_leave_stuck_notes)
{
    Fx f ("stuck", 512);
    CHECK (f.session.createProject (f.dir + "/p", "t").ok);
    const auto tr = f.session.addTrack ("S", "synth");
    const std::string clip = firstMidiClip (f.session, tr.id);
    CHECK (f.session.loadSynthPreset (tr.id, "pad").ok); // release largo (1,6 s)
    CHECK (f.session.setClipNotes (clip, { note (60, 0, kPpq), note (64, 0, kPpq), note (67, 0, kPpq) }).ok);

    // 1) Note Off normal: tras el release no queda nada sonando.
    auto out = playFrom (f.engine, 0, kSr * 14, 512);
    CHECK (f.session.activeVoices (tr.id) == 0);
    CHECK (peakOf (out, (size_t) kSr * 13) < 1e-6f);
    CHECK (peakOf (out, (size_t) kSr / 4, (size_t) kSr / 2) > 0.01f);

    // 2) Stop a mitad de una nota larga: las voces pasan a release y se apagan.
    CHECK (f.session.setClipNotes (clip, { note (60, 0, kPpq * 16) }).ok);
    f.engine.seek (0);
    f.engine.play();
    run (f.engine, kSr, 512);
    CHECK (f.session.activeVoices (tr.id) == 1);
    f.engine.stop();
    run (f.engine, kSr * 13, 512);
    CHECK (f.session.activeVoices (tr.id) == 0);

    // 3) Seek con la nota sonando.
    f.engine.seek (0);
    f.engine.play();
    run (f.engine, kSr, 512);
    f.engine.seek (kSr * 20);
    run (f.engine, kSr * 13, 512);
    CHECK (f.session.activeVoices (tr.id) == 0);
    f.engine.stop();

    // 4) Borrar la nota mientras suena (su Note Off desaparece del plan): no queda atascada.
    f.engine.seek (0);
    f.engine.play();
    run (f.engine, kSr, 512);
    CHECK (f.session.activeVoices (tr.id) == 1);
    CHECK (f.session.setClipNotes (clip, {}).ok);
    run (f.engine, kSr * 13, 512);
    CHECK (f.session.activeVoices (tr.id) == 0);
    f.engine.stop();
}

TEST (loop_wraps_at_the_exact_frame_and_never_accumulates_voices)
{
    Fx f ("loop", 1024);
    CHECK (f.session.createProject (f.dir + "/p", "t").ok);
    const auto d = f.session.addTrack ("D", "drums");
    const auto m = f.session.model();
    const auto& dt = *m.findTrack (d.id);
    CHECK (f.session.setPatternStep (d.id, dt.patterns[0].id, 0, 0, 127).ok); // bombo en el 1
    const auto s = f.session.addTrack ("S", "synth");
    CHECK (f.session.loadSynthPreset (s.id, "lead").ok);
    // Nota que empieza dentro del bucle y termina fuera de él (cruza el reinicio).
    CHECK (f.session.setClipNotes (firstMidiClip (f.session, s.id), { note (72, kPpq * 3, kPpq * 3) }).ok);
    CHECK (f.session.setLoop (true, 0, 4 * kPpq).ok); // 1 compás = 96000 frames

    const Frame loopLen = 96000;
    f.engine.seek (0);
    f.engine.play();
    std::vector<float> out ((size_t) loopLen * 5 * 2);
    Frame done = 0;
    std::mt19937 rng (9);
    int maxVoices = 0;
    while (done < loopLen * 5)
    {
        const int n = (int) std::min<Frame> ((int) (rng() % 1500) + 1, loopLen * 5 - done);
        f.engine.process (nullptr, 0, out.data() + done * 2, 2, n);
        done += n;
        CHECK (f.engine.position() >= 0 && f.engine.position() < loopLen);
        maxVoices = std::max (maxVoices, f.session.activeVoices (s.id));
    }
    f.engine.stop();
    CHECK (maxVoices <= 1);
    // Cada repetición idéntica a la segunda (la primera parte del silencio; las demás, de las colas del bucle).
    for (int k = 2; k < 5; ++k)
        CHECK (std::equal (out.begin() + loopLen * 2 * k, out.begin() + loopLen * 2 * (k + 1), out.begin() + loopLen * 2));
    // El bombo cae exactamente en el inicio de cada vuelta.
    for (int k = 0; k < 5; ++k) CHECK (std::fabs (out[(size_t) (loopLen * k) * 2 + 2]) > 1e-4f);
    MEASURE ("bucle de %lld frames, 5 vueltas con bloques aleatorios, voces máx. del synth = %d", (long long) loopLen, maxVoices);
}

TEST (tempo_change_moves_midi_events_but_not_audio)
{
    Fx f ("tempo", 1024);
    CHECK (f.session.createProject (f.dir + "/p", "t").ok);
    const auto s = f.session.addTrack ("S", "synth");
    CHECK (f.session.setClipNotes (firstMidiClip (f.session, s.id), { note (60, kPpq * 2, kPpq) }).ok);
    CHECK (firstNonZero (playFrom (f.engine, 0, kSr * 2, 256)) == 48000);  // 120 BPM: tiempo 3 = 1 s
    settle (f.engine);
    CHECK (f.session.setTempo (60, 4).ok);
    CHECK (firstNonZero (playFrom (f.engine, 0, kSr * 3, 256)) == 96000);  // 60 BPM: 2 s
    settle (f.engine);
    CHECK (f.session.setTempo (140, 4).ok);
    CHECK (firstNonZero (playFrom (f.engine, 0, kSr * 2, 256)) == ticksToFrames (kPpq * 2, 140, kSr));
}

TEST (synth_voice_limit_levels_and_extreme_parameters_are_safe)
{
    Fx f ("voices", 512);
    CHECK (f.session.createProject (f.dir + "/p", "t").ok);
    const auto s = f.session.addTrack ("S", "synth");
    for (const auto& preset : synthPresets())
    {
        CHECK (f.session.loadSynthPreset (s.id, preset.id).ok);
        CHECK (f.session.setInstrumentParam (s.id, "polyphony", 16).ok);
        for (int k = 0; k < 64; ++k) f.engine.sendLive (0, 0x90, (uint8_t) (24 + k), 127);
        auto out = run (f.engine, kSr / 2, 512);
        CHECK (f.session.activeVoices (s.id) <= kSynthMaxVoices);
        CHECK (allFinite (out));
        CHECK_MSG (peakOf (out) < 1.0f, preset.id + " pico " + std::to_string (peakOf (out)));
        for (int k = 0; k < 64; ++k) f.engine.sendLive (0, 0x80, (uint8_t) (24 + k), 0);
        run (f.engine, kSr * 12, 1024);
        CHECK_MSG (f.session.activeVoices (s.id) == 0, preset.id);
    }
    // Parámetros extremos (resonancia máxima, corte en los límites, drive máximo, glide)
    for (float cutoff : { 40.0f, 18000.0f })
    {
        f.session.setInstrumentParam (s.id, "cutoffHz", cutoff);
        f.session.setInstrumentParam (s.id, "resonance", 0.95f);
        f.session.setInstrumentParam (s.id, "drive", 1.0f);
        f.session.setInstrumentParam (s.id, "filterEnv", 1.0f);
        f.session.setInstrumentParam (s.id, "octave", 3);
        f.engine.sendLive (0, 0x90, 127, 127);
        f.engine.sendLive (0, 0x90, 0, 127);
        auto out = run (f.engine, kSr, 333);
        CHECK (allFinite (out));
        CHECK (peakOf (out) < 1.0f);
        f.engine.sendLive (0, 0xB0, 120, 0); // All Sound Off
        run (f.engine, 1024, 512);
    }
    CHECK (! f.session.setInstrumentParam (s.id, "noExiste", 1).ok);
    CHECK (! f.session.setInstrumentParam (s.id, "cutoffHz", std::nanf ("")).ok);
    // Los valores fuera de rango se acotan
    CHECK (f.session.setInstrumentParam (s.id, "resonance", 50).ok);
    CHECK (json::parse (f.session.model().findTrack (s.id)->instrument.state)["resonance"].get<float>() <= 0.95f);
}

TEST (mono_synth_glides_and_returns_to_held_note)
{
    Fx f ("mono", 512);
    CHECK (f.session.createProject (f.dir + "/p", "t").ok);
    const auto s = f.session.addTrack ("S", "synth");
    CHECK (f.session.loadSynthPreset (s.id, "bass_808").ok);
    f.engine.sendLive (0, 0x90, 36, 100);
    f.engine.sendLive (0, 0x90, 43, 100);
    run (f.engine, 4800, 512);
    CHECK (f.session.activeVoices (s.id) == 1); // monofónico
    f.engine.sendLive (0, 0x80, 43, 0);
    run (f.engine, 4800, 512);
    CHECK (f.session.activeVoices (s.id) == 1); // vuelve a la nota 36 que sigue pulsada
    f.engine.sendLive (0, 0x80, 36, 0);
    run (f.engine, kSr * 4, 512);
    CHECK (f.session.activeVoices (s.id) == 0);
}

TEST (drum_kits_have_distinct_sounds_and_safe_levels)
{
    Fx f ("kits", 512);
    CHECK (f.session.createProject (f.dir + "/p", "t").ok);
    const auto d = f.session.addTrack ("D", "drums");
    std::vector<std::vector<float>> kicksPerKit;
    for (int kit = 0; kit < (int) drumKits().size(); ++kit)
    {
        CHECK (f.session.setInstrumentParam (d.id, "kit", (float) kit).ok);
        std::vector<std::vector<float>> lanes;
        for (int lane = 0; lane < kDrumLanes; ++lane)
        {
            run (f.engine, 1024, 512); // aplica el nuevo grafo
            f.engine.sendLive (0, 0x99, (uint8_t) (kDrumBaseNote + lane), 127);
            auto out = run (f.engine, kSr / 2, 512);
            CHECK (allFinite (out));
            CHECK_MSG (peakOf (out) > 0.05f && peakOf (out) < 1.0f, "kit " + std::to_string (kit) + " carril " + std::to_string (lane));
            std::vector<float> mono (out.size() / 2);
            for (size_t i = 0; i < mono.size(); ++i) mono[i] = out[i * 2];
            lanes.push_back (mono);
            run (f.engine, kSr * 3, 1024); // deja terminar las colas
        }
        // Ningún par de carriles es el mismo sonido (correlación normalizada < 0,9).
        for (size_t a = 0; a < lanes.size(); ++a)
            for (size_t b = a + 1; b < lanes.size(); ++b)
            {
                double ab = 0, aa = 0, bb = 0;
                for (size_t i = 0; i < lanes[a].size(); ++i) { ab += lanes[a][i] * lanes[b][i]; aa += lanes[a][i] * lanes[a][i]; bb += lanes[b][i] * lanes[b][i]; }
                const double corr = std::fabs (ab) / std::sqrt (aa * bb + 1e-12);
                CHECK_MSG (corr < 0.9, "kit " + std::to_string (kit) + " carriles " + std::to_string (a) + "/" + std::to_string (b) + " corr " + std::to_string (corr));
            }
        kicksPerKit.push_back (lanes[0]);
    }
    for (size_t a = 0; a < kicksPerKit.size(); ++a)
        for (size_t b = a + 1; b < kicksPerKit.size(); ++b) CHECK (kicksPerKit[a] != kicksPerKit[b]);
}

TEST (drum_lane_mute_solo_volume_and_hat_choke)
{
    Fx f ("lanes", 512);
    CHECK (f.session.createProject (f.dir + "/p", "t").ok);
    const auto d = f.session.addTrack ("D", "drums");
    run (f.engine, 512, 512);
    auto hit = [&] (int lane) { f.engine.sendLive (0, 0x99, (uint8_t) (kDrumBaseNote + lane), 127); return run (f.engine, kSr / 4, 512); };
    const float loud = peakOf (hit (1));
    run (f.engine, kSr, 1024);
    CHECK (f.session.setInstrumentParam (d.id, "mute1", 1).ok);
    CHECK (peakOf (hit (1)) == 0.0f);
    CHECK (f.session.setInstrumentParam (d.id, "mute1", 0).ok);
    CHECK (f.session.setInstrumentParam (d.id, "solo0", 1).ok);
    CHECK (peakOf (hit (1)) == 0.0f); // otro carril en solo
    CHECK (f.session.setInstrumentParam (d.id, "solo0", 0).ok);
    CHECK (f.session.setInstrumentParam (d.id, "vol1", -12).ok);
    run (f.engine, kSr, 1024);
    CHECK_NEAR (peakOf (hit (1)) / loud, std::pow (10.0, -12.0 / 20.0), 0.02);
    // Choke: el charles cerrado corta al abierto en ~3 ms.
    run (f.engine, kSr * 2, 1024);
    f.engine.sendLive (0, 0x99, kDrumBaseNote + 4, 127);
    run (f.engine, 4800, 512);
    f.engine.sendLive (0, 0x99, kDrumBaseNote + 3, 127);
    run (f.engine, kSr / 2, 512);
    CHECK (f.session.activeVoices (d.id) == 0);
}

TEST (live_midi_plays_while_transport_is_stopped)
{
    Fx f ("live", 512);
    CHECK (f.session.createProject (f.dir + "/p", "t").ok);
    f.session.addTrack ("D", "drums");
    run (f.engine, 512, 512);
    CHECK (! f.engine.isPlaying());
    CHECK (f.engine.sendLive (0, 0x99, kDrumBaseNote, 110));
    auto out = run (f.engine, 2048, 256);
    CHECK (firstNonZero (out) == 0); // suena en el primer bloque tras el evento
    CHECK (f.engine.position() == 0);
}

TEST (patterns_and_midi_clips_persist_and_reopen_identically)
{
    Fx f ("persist", 1024);
    const std::string p = f.dir + "/p";
    CHECK (f.session.createProject (p, "persist").ok);
    const auto d = f.session.addTrack ("D", "drums");
    auto m = f.session.model();
    const std::string pa = m.findTrack (d.id)->patterns[0].id;
    for (int st = 0; st < 16; st += 4) CHECK (f.session.setPatternStep (d.id, pa, 0, st, 120).ok);
    CHECK (f.session.setPatternStep (d.id, pa, 3, 2, 64).ok);
    CHECK (f.session.setPatternSwing (d.id, pa, 0.4f).ok);
    const auto pb = f.session.addPattern (d.id, pa);
    CHECK (pb.ok);
    CHECK (f.session.setPatternLength (d.id, pb.id, 32).ok);
    CHECK (f.session.setPatternStep (d.id, pb.id, 1, 28, 127).ok);
    CHECK (f.session.addMidiClip (d.id, 16 * kPpq, 8 * kPpq, pb.id).ok); // encadenado: A (4 compases) + B
    const auto s = f.session.addTrack ("S", "synth");
    CHECK (f.session.loadSynthPreset (s.id, "pluck").ok);
    CHECK (f.session.setInstrumentParam (s.id, "cutoffHz", 1234).ok);
    CHECK (f.session.setClipNotes (firstMidiClip (f.session, s.id), { note (60, 0, 480, 90), note (63, 480, 480, 70), note (67, 960, 960, 127) }).ok);
    CHECK (f.session.setLoop (true, 4 * kPpq, 12 * kPpq).ok);
    CHECK (f.session.setTempo (97.5, 4).ok);
    const std::string before = toJson (f.session.model());
    const std::string out1 = f.dir + "/a.wav", out2 = f.dir + "/b.wav";
    CHECK (f.session.exportMix (out1, 32).ok);
    CHECK (f.session.save().ok);
    f.session.close();

    LoadReport rep;
    CHECK (f.session.openProject (p, &rep).ok);
    CHECK (! rep.usedBackup);
    CHECK (toJson (f.session.model()) == before);
    const auto pm = f.session.model();
    CHECK (pm.loopEnabled && pm.loopStart == 4 * kPpq && pm.loopEnd == 12 * kPpq);
    CHECK (json::parse (pm.findTrack (s.id)->instrument.state)["cutoffHz"].get<float>() == 1234.0f);
    CHECK (pm.findTrack (s.id)->instrument.preset == "pluck");
    CHECK (f.session.exportMix (out2, 32).ok);
    std::string a, b;
    CHECK (readFile (out1, a) && readFile (out2, b) && a == b && a.size() > 1000);

    // Deshacer también cubre patrones y notas.
    CHECK (f.session.setPatternStep (d.id, pa, 7, 15, 127).ok);
    CHECK (f.session.undo().ok);
    CHECK (toJson (f.session.model()) .find ("\"modifiedAt\"") != std::string::npos);
    CHECK (f.session.model().findTrack (d.id)->findPattern (pa)->lanes[7][15] == 0);
}

TEST (v1_project_opens_unchanged_and_is_migrated_with_backup)
{
    Fx f ("v1", 1024);
    const std::string p = f.dir + "/old";
    makeDirs (p + "/audio");
    AudioData a;
    a.sampleRate = kSr;
    a.allocate (1, kSr);
    for (Frame i = 0; i < kSr; ++i) a.channels[0][(size_t) i] = 0.3f * (float) std::sin (2 * 3.14159265 * 220 * (double) i / kSr);
    std::string err;
    CHECK (writeWav (p + "/audio/rec_aaaa.wav", a, WavFormat::Float32, err));
    // project.json exactamente con la forma que escribe la fase 1 (versión 1, sin campos nuevos).
    const std::string v1 = R"({
  "beatsPerBar": 4, "bpm": 100.0, "createdAt": 1762920000000, "format": "caucesound-project", "id": "prj_0123456789ab",
  "masterDb": -1.5, "metronome": true, "modifiedAt": 1762920000000, "name": "Voz de prueba", "recordLatencyOffset": 960,
  "sampleRate": 48000,
  "sources": [ { "channels": 1, "file": "audio/rec_aaaa.wav", "frames": 48000, "id": "rec_aaaa", "origin": "recording" } ],
  "tracks": [
    { "armed": true, "clips": [ { "fadeIn": 0, "fadeOut": 480, "gainDb": -2.0, "id": "clp_1", "length": 40000, "name": "Track 1",
        "sourceId": "rec_aaaa", "sourceOffset": 1000, "start": 24000 } ],
      "color": 4283417591, "id": "trk_1", "inserts": [], "mute": false, "name": "Track 1", "pan": -0.25, "solo": false, "volumeDb": -3.0 },
    { "armed": false, "clips": [], "color": 4294948685, "id": "trk_2", "inserts": [], "mute": false, "name": "Track 2", "pan": 0.0, "solo": false, "volumeDb": 0.0 }
  ],
  "version": 1
})";
    CHECK (atomicWriteFile (p + "/project.json", v1, err, false));
    LoadReport rep;
    CHECK (f.session.openProject (p, &rep).ok);
    CHECK (rep.missingSources.empty() && ! rep.usedBackup);
    const auto m = f.session.model();
    CHECK (m.name == "Voz de prueba" && m.bpm == 100.0 && m.masterDb == -1.5f && m.metronome && m.recordLatencyOffset == 960);
    CHECK (m.tracks.size() == 2 && m.tracks[0].kind == "audio" && m.tracks[0].clips.size() == 1);
    const auto& c = m.tracks[0].clips[0];
    CHECK (c.start == 24000 && c.length == 40000 && c.sourceOffset == 1000 && c.fadeOut == 480 && c.gainDb == -2.0f);
    CHECK (m.tracks[0].pan == -0.25f && m.tracks[0].volumeDb == -3.0f && m.tracks[0].armed);
    CHECK (! m.loopEnabled && m.version == kProjectFormatVersion);
    std::string backup;
    CHECK (readFile (p + "/project.v1.json", backup) && backup == v1); // el original, intacto
    const std::string e1 = f.dir + "/e1.wav", e2 = f.dir + "/e2.wav";
    CHECK (f.session.exportMix (e1, 32).ok);
    CHECK (f.session.save().ok);
    f.session.close();
    std::string saved;
    CHECK (readFile (p + "/project.json", saved) && json::parse (saved)["version"] == kProjectFormatVersion);
    CHECK (f.session.openProject (p).ok);
    CHECK (f.session.exportMix (e2, 32).ok);
    std::string x, y;
    CHECK (readFile (e1, x) && readFile (e2, y) && x == y);
    // El proyecto migrado admite pistas nuevas sin tocar las de audio.
    CHECK (f.session.addTrack ("Beat", "drums").ok);
    CHECK (f.session.model().tracks[0].clips[0].start == 24000);
}

TEST (future_format_and_broken_music_data_are_rejected)
{
    ProjectModel p;
    std::string err;
    CHECK (! fromJson (R"({"format":"caucesound-project","version":4,"id":"x","name":"n","sampleRate":48000,"bpm":120,"beatsPerBar":4,"sources":[],"tracks":[]})", p, err));
    const std::string bad[] = {
        R"({"format":"caucesound-project","version":2,"id":"x","name":"n","sampleRate":48000,"bpm":120,"beatsPerBar":4,"sources":[],"tracks":[{"id":"t","clips":[],"kind":"synth"}]})",
        R"({"format":"caucesound-project","version":2,"id":"x","name":"n","sampleRate":48000,"bpm":120,"beatsPerBar":4,"sources":[],"tracks":[{"id":"t","clips":[],"kind":"drums","instrument":{"type":"cs.drums"},"patterns":[{"id":"p","steps":12,"lanes":[]}]}]})",
        R"({"format":"caucesound-project","version":2,"id":"x","name":"n","sampleRate":48000,"bpm":120,"beatsPerBar":4,"sources":[],"tracks":[{"id":"t","clips":[],"kind":"synth","instrument":{"type":"cs.synth"},"midiClips":[{"id":"c","start":0,"length":960,"notes":[[200,100,0,0,10]]}]}]})",
        R"({"format":"caucesound-project","version":2,"id":"x","name":"n","sampleRate":48000,"bpm":120,"beatsPerBar":4,"sources":[],"tracks":[{"id":"t","clips":[],"kind":"drums","instrument":{"type":"cs.drums"},"midiClips":[{"id":"c","patternId":"nope","start":0,"length":960,"notes":[]}]}]})",
        R"({"format":"caucesound-project","version":2,"id":"x","name":"n","sampleRate":48000,"bpm":120,"beatsPerBar":4,"sources":[],"tracks":[{"id":"t","clips":[],"kind":"theremin"}]})",
    };
    for (const auto& b : bad) CHECK_MSG (! fromJson (b, p, err), b.substr (0, 60));
}

TEST (templates_create_real_playable_content)
{
    for (const char* tpl : { "trap", "reggaeton", "hiphop", "pop", "electronic", "empty" })
    {
        Fx f (std::string ("tpl_") + tpl, 1024);
        CHECK (f.session.createProject (f.dir + "/p", tpl, 48000, tpl).ok);
        const auto m = f.session.model();
        CHECK (! f.session.canUndo());
        if (std::string (tpl) == "empty") { CHECK (m.tracks.empty() && m.templateId == "empty"); continue; }
        CHECK (m.templateId == tpl);
        int drums = 0, synths = 0, audio = 0;
        for (const auto& t : m.tracks)
        {
            drums += t.kind == "drums"; synths += t.kind == "synth"; audio += t.kind == "audio";
            if (t.kind == "synth")
            {
                CHECK (! t.instrument.preset.empty() && ! synthPresetState (t.instrument.preset).empty());
                // Arreglo de 17 compases: un clip por sección (Intro, A, B, Break, Final, Cierre), notas válidas y dentro del clip.
                CHECK (t.midiClips.size() == 6);
                size_t notes = 0;
                for (const auto& c : t.midiClips)
                {
                    notes += c.notes.size();
                    for (const auto& n : c.notes) CHECK (validateNote (n).empty() && n.start >= 0 && n.start + n.length <= c.length);
                }
                CHECK (notes >= 16);
            }
            if (t.kind == "drums")
            {
                int hits = 0;
                for (const auto& l : t.patterns[0].lanes) for (int v : l) hits += v > 0;
                CHECK (hits >= 6);
            }
        }
        CHECK (drums == 1 && synths >= 2 && audio == 1);
        if (std::string (tpl) == "trap") CHECK (m.bpm == 140);
        if (std::string (tpl) == "reggaeton") CHECK (m.bpm == 95);
        // Suena: el beat de partida produce audio desde el primer compás.
        auto out = playFrom (f.engine, 0, kSr * 8, 512);
        std::printf ("    measure: plantilla %s, pico del máster en 8 s = %.3f\n", tpl, peakOf (out));
        CHECK (peakOf (out) > 0.05f && peakOf (out) < 1.0f && allFinite (out));
        CHECK (f.session.save().ok);
    }
}

TEST (export_includes_instruments_tails_and_matches_live_playback)
{
    Fx f ("export_inst", 1024);
    CHECK (f.session.createProject (f.dir + "/p", "e", 48000, "trap").ok);
    const auto m = f.session.model();
    std::string synthId;
    for (const auto& t : m.tracks) if (t.kind == "synth") { synthId = t.id; break; }
    // Última nota justo al final del clip de 8 compases: su release debe aparecer completo en el archivo.
    const Tick clipLen = 8 * 4 * kPpq;
    CHECK (f.session.setClipNotes (firstMidiClip (f.session, synthId), { note (60, 0, kPpq), note (64, clipLen - kPpq, kPpq) }).ok);
    const std::string out = f.dir + "/mix.wav";
    long clipped = -1;
    CHECK (f.session.exportMix (out, 32, 0, &clipped).ok);
    AudioData a;
    std::string err;
    CHECK (readWav (out, a, err));
    const Frame end = f.session.model().contentEnd();
    CHECK (a.numFrames() > end + kSr / 10); // con cola
    CHECK (clipped == 0);
    // La cola termina en silencio (no se corta una nota sonando).
    float tailPeak = 0;
    for (Frame i = a.numFrames() - 480; i < a.numFrames(); ++i) tailPeak = std::max (tailPeak, std::fabs (a.channels[0][(size_t) i]));
    CHECK_MSG (tailPeak < 1e-3f, std::to_string (tailPeak));
    // Exportación = reproducción en vivo (mismo motor), muestra a muestra.
    auto live = playFrom (f.engine, 0, end, 512);
    double maxDiff = 0;
    for (Frame i = 0; i < end; ++i)
        maxDiff = std::max<double> (maxDiff, std::max (std::fabs (live[(size_t) i * 2] - a.channels[0][(size_t) i]), std::fabs (live[(size_t) i * 2 + 1] - a.channels[1][(size_t) i])));
    MEASURE ("exportación %lld frames, diferencia máxima con la reproducción en vivo = %g", (long long) a.numFrames(), maxDiff);
    CHECK (maxDiff < 1e-6);
}

TEST (audio_drums_and_synth_tracks_play_in_sync)
{
    Fx f ("sync", 1024);
    CHECK (f.session.createProject (f.dir + "/p", "s").ok);
    // Pista de audio con un impulso en el tiempo 3 (frame 48000 a 120 BPM)
    const std::string wav = f.dir + "/imp.wav";
    AudioData imp;
    imp.allocate (1, 100);
    imp.channels[0][0] = 0.5f;
    std::string err;
    CHECK (writeWav (wav, imp, WavFormat::Float32, err));
    const auto at = f.session.addTrack ("A", "audio");
    CHECK (f.session.importWav (wav, at.id, 48000).ok);
    const auto d = f.session.addTrack ("D", "drums");
    CHECK (f.session.setPatternStep (d.id, f.session.model().findTrack (d.id)->patterns[0].id, 0, 8, 127).ok); // paso 9 = tiempo 3
    const auto s = f.session.addTrack ("S", "synth");
    CHECK (f.session.setClipNotes (firstMidiClip (f.session, s.id), { note (60, 2 * kPpq, kPpq) }).ok);
    for (const char* solo : { "A", "D", "S" })
    {
        for (const auto& t : f.session.model().tracks) f.session.setTrackSolo (t.id, t.name == solo);
        settle (f.engine);
        CHECK_MSG (firstNonZero (playFrom (f.engine, 0, kSr * 2, 0)) == 48000, solo);
    }
}

TEST (engine_with_instruments_loop_and_live_input_does_not_allocate)
{
    Fx f ("noalloc_inst", 256);
    CHECK (f.session.createProject (f.dir + "/p", "n", 48000, "electronic").ok);
    for (const auto& t : f.session.model().tracks)
        if (t.kind == "synth") f.session.setClipNotes (firstMidiClip (f.session, t.id), { note (48, 0, 240), note (55, 240, 2000), note (60, 3000, 100) });
    CHECK (f.session.setLoop (true, 0, 4 * kPpq).ok);
    f.engine.setMetronome (true);
    run (f.engine, 1024, 256); // adopta el grafo
    f.engine.play();
    std::vector<float> out (512 * 2);
    cstest::allocationCount.store (0);
    cstest::countAllocations.store (true);
    for (int i = 0; i < 3000; ++i)
    {
        if (i % 50 == 0) { f.engine.sendLive (0, 0x99, kDrumBaseNote + (i / 50) % 8, 100); f.engine.sendLive (1, 0x90, 60, 100); }
        if (i % 50 == 25) f.engine.sendLive (1, 0x80, 60, 0);
        if (i == 1500) f.engine.seek (1000);
        f.engine.process (nullptr, 0, out.data(), 2, 256);
    }
    cstest::countAllocations.store (false);
    f.engine.stop();
    MEASURE ("reservas en 3000 bloques con batería, 3 synths, bucle, metrónomo y MIDI en vivo: %ld", cstest::allocationCount.load());
    CHECK (cstest::allocationCount.load() == 0);
}

TEST (editing_patterns_while_playing_is_thread_safe)
{
    Fx f ("edit_rt", 256);
    CHECK (f.session.createProject (f.dir + "/p", "n", 48000, "trap").ok);
    std::string drumsId, patId, synthId;
    for (const auto& t : f.session.model().tracks)
    {
        if (t.kind == "drums") { drumsId = t.id; patId = t.patterns[0].id; }
        if (t.kind == "synth" && synthId.empty()) synthId = t.id;
    }
    std::atomic<bool> stop { false }, finite { true };
    std::atomic<long> blocks { 0 };
    f.engine.play();
    std::thread audio ([&]
    {
        std::vector<float> out (256 * 2);
        while (! stop.load())
        {
            f.engine.process (nullptr, 0, out.data(), 2, 256);
            for (float x : out) if (! std::isfinite (x)) finite.store (false);
            blocks.fetch_add (1);
        }
    });
    std::mt19937 rng (5);
    for (int i = 0; i < 400; ++i)
    {
        f.session.setPatternStep (drumsId, patId, (int) (rng() % 8), (int) (rng() % 16), (int) (rng() % 128));
        f.session.setInstrumentParam (synthId, "cutoffHz", 200.0f + (float) (rng() % 8000));
        if (i % 40 == 0) f.session.setInstrumentParam (drumsId, "kit", (float) (rng() % 4));
        if (i % 7 == 0) f.session.setClipNotes (firstMidiClip (f.session, synthId), { note (40 + (int) (rng() % 30), 0, 960) });
        f.engine.sendLive (0, 0x99, kDrumBaseNote, 100);
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (20));
    stop.store (true);
    audio.join();
    f.engine.stop();
    // Bajo TSan, cualquier carrera falla la ejecución. En cualquier compilación: el audio siguió procesando, la salida
    // es finita y el modelo final es coherente.
    CHECK (finite.load());
    CHECK (blocks.load() > 0);
    CHECK (validate (f.session.model()).empty());
}

TEST (api_music_commands_end_to_end)
{
    Fx f ("api_music", 1024);
    auto cmd = [&] (const json& j) { return json::parse (handleCommand (f.session, f.engine, j.dump())); };
    const std::string root = f.dir;
    CHECK (cmd ({ { "cmd", "createProject" }, { "folder", f.dir + "/p" }, { "name", "API" }, { "template", "reggaeton" } })["ok"] == true);
    const auto cat = cmd ({ { "cmd", "catalog" } });
    CHECK (cat["ok"] == true && cat["synthPresets"].size() == 22 && cat["drumKits"].size() == 11 && cat["ppq"] == kPpq);
    CHECK (cat["synthParams"].size() == 25 && cat["drumParams"].size() == 50);
    const auto add = cmd ({ { "cmd", "addTrack" }, { "name", "Lead" }, { "kind", "synth" } });
    CHECK (add["ok"] == true);
    const std::string tid = add["id"];
    const auto st = json::parse (f.session.stateJson());
    std::string clipId;
    for (const auto& t : st["project"]["tracks"]) if (t["id"] == tid) clipId = t["midiClips"][0]["id"];
    CHECK (! clipId.empty());
    CHECK (cmd ({ { "cmd", "setClipNotes" }, { "clipId", clipId }, { "notes", json::array ({ json::array ({ 60, 100, 0, 250, 200 }), { { "key", 64 }, { "start", 470 }, { "length", 500 } } }) } })["ok"] == true);
    CHECK (cmd ({ { "cmd", "quantizeClip" }, { "clipId", clipId }, { "grid", 240 }, { "strength", 1.0 } })["ok"] == true);
    const auto notes = f.session.model().findMidiClip (clipId)->notes;
    CHECK (notes.size() == 2 && notes[0].start == 240 && notes[1].start == 480 && notes[1].velocity == 100);
    CHECK (cmd ({ { "cmd", "setClipNotes" }, { "clipId", clipId }, { "notes", json::array ({ json::array ({ 300, 100, 0, 0, 10 }) }) } })["ok"] == false);
    CHECK (cmd ({ { "cmd", "loadPreset" }, { "trackId", tid }, { "preset", "pad" } })["ok"] == true);
    CHECK (cmd ({ { "cmd", "setInstrumentParam" }, { "trackId", tid }, { "param", "attackMs" }, { "value", 25 } })["ok"] == true);
    CHECK (cmd ({ { "cmd", "setLoop" }, { "on", true }, { "start", 0 }, { "end", 3840 } })["ok"] == true);
    CHECK (f.engine.loopEnabled());
    CHECK (cmd ({ { "cmd", "moveMidiClip" }, { "clipId", clipId }, { "start", 3840 } })["ok"] == true);
    CHECK (cmd ({ { "cmd", "resizeMidiClip" }, { "clipId", clipId }, { "start", 3840 }, { "length", 7680 } })["ok"] == true);
    const auto dup = cmd ({ { "cmd", "duplicateMidiClip" }, { "clipId", clipId } });
    CHECK (dup["ok"] == true && f.session.model().findMidiClip (dup["id"])->start == 3840 + 7680);
    CHECK (cmd ({ { "cmd", "deleteMidiClip" }, { "clipId", dup["id"] } })["ok"] == true);
    std::string drumsId, patId;
    for (const auto& t : f.session.model().tracks) if (t.kind == "drums") { drumsId = t.id; patId = t.patterns[0].id; }
    CHECK (cmd ({ { "cmd", "setStep" }, { "trackId", drumsId }, { "patternId", patId }, { "lane", 2 }, { "step", 4 }, { "velocity", 90 } })["ok"] == true);
    CHECK (cmd ({ { "cmd", "setStep" }, { "trackId", drumsId }, { "patternId", patId }, { "lane", 9 }, { "step", 4 }, { "velocity", 90 } })["ok"] == false);
    const auto np = cmd ({ { "cmd", "addPattern" }, { "trackId", drumsId }, { "copyFrom", patId } });
    CHECK (np["ok"] == true);
    json lanes = json::array();
    for (int l = 0; l < 8; ++l) lanes.push_back (std::vector<int> (16, l == 0 ? 127 : 0));
    CHECK (cmd ({ { "cmd", "setPatternLanes" }, { "trackId", drumsId }, { "patternId", np["id"] }, { "lanes", lanes } })["ok"] == true);
    CHECK (cmd ({ { "cmd", "setSwing" }, { "trackId", drumsId }, { "patternId", np["id"] }, { "swing", 0.3 } })["ok"] == true);
    CHECK (cmd ({ { "cmd", "setPatternLength" }, { "trackId", drumsId }, { "patternId", np["id"] }, { "steps", 32 } })["ok"] == true);
    const auto dc = cmd ({ { "cmd", "addMidiClip" }, { "trackId", drumsId }, { "start", 32 * kPpq }, { "length", 8 * kPpq }, { "patternId", np["id"] } });
    CHECK (dc["ok"] == true);
    CHECK (cmd ({ { "cmd", "setClipPattern" }, { "clipId", dc["id"] }, { "patternId", patId } })["ok"] == true);
    CHECK (cmd ({ { "cmd", "save" } })["ok"] == true);
    // Proyectos cerrados: listado con miniatura, renombrar y duplicar dentro de la raíz.
    CHECK (cmd ({ { "cmd", "closeProject" } })["ok"] == true);
    const auto list = cmd ({ { "cmd", "listProjects" }, { "root", root } });
    CHECK (list["projects"].size() == 1 && list["projects"][0]["thumb"].size() >= 4 && list["projects"][0]["template"] == "reggaeton");
    CHECK (cmd ({ { "cmd", "renameProject" }, { "root", root }, { "folder", f.dir + "/p" }, { "name", "Nuevo nombre" } })["ok"] == true);
    CHECK (cmd ({ { "cmd", "duplicateProject" }, { "root", root }, { "folder", f.dir + "/p" }, { "dest", f.dir + "/p2" }, { "name", "Copia" } })["ok"] == true);
    CHECK (cmd ({ { "cmd", "duplicateProject" }, { "root", root }, { "folder", f.dir + "/p" }, { "dest", "/tmp/fuera" }, { "name", "x" } })["ok"] == false);
    const auto list2 = cmd ({ { "cmd", "listProjects" }, { "root", root } });
    CHECK (list2["projects"].size() == 2);
    CHECK (cmd ({ { "cmd", "openProject" }, { "folder", f.dir + "/p2" } })["ok"] == true);
    CHECK (f.session.model().name == "Copia");
    CHECK (f.session.model().findMidiClip (clipId) != nullptr);
}

// Genera los datos de prueba de la interfaz Android (JSON real del motor, no escrito a mano).
// CS_FIXTURE_DIR=android/app/src/test/resources/fixtures ./build/cs_core_tests ui_fixtures
TEST (ui_fixtures_from_engine)
{
    Fx f ("ui_fixtures", 1024);
    auto cmd = [&] (const json& j) { return handleCommand (f.session, f.engine, j.dump()); };
    CHECK (json::parse (cmd ({ { "cmd", "createProject" }, { "folder", f.dir + "/beat" }, { "name", "Beat trap 140" }, { "template", "trap" } }))["ok"] == true);
    const auto st = json::parse (f.session.stateJson());
    std::string synthClip;
    for (const auto& t : st["project"]["tracks"])
        if (t["kind"] == "synth" && ! t["midiClips"].empty() && synthClip.empty()) synthClip = t["midiClips"][0]["id"];
    CHECK (! synthClip.empty());
    // Cadena de efectos en la pista Melodía: EQ + reverb con la mezcla al 35 %.
    for (const auto& t : st["project"]["tracks"])
        if (t["name"] == "Melodía")
        {
            const auto eq = json::parse (cmd ({ { "cmd", "addInsert" }, { "trackId", t["id"] }, { "type", "cs.eq" } }));
            const auto rv = json::parse (cmd ({ { "cmd", "addInsert" }, { "trackId", t["id"] }, { "type", "cs.reverb" } }));
            CHECK (eq["ok"] == true && rv["ok"] == true);
            CHECK (json::parse (cmd ({ { "cmd", "setInsertParam" }, { "insertId", rv["id"] }, { "param", "mix" }, { "value", 0.35 } }))["ok"] == true);
        }
    CHECK (json::parse (cmd ({ { "cmd", "save" } }))["ok"] == true);
    const std::string state = f.session.stateJson();
    // Variante con un bus de reverb y un envío del primer synth (para el mezclador).
    const auto bus = json::parse (cmd ({ { "cmd", "addTrack" }, { "name", "Reverb" }, { "kind", "bus" } }));
    CHECK (bus["ok"] == true);
    CHECK (json::parse (cmd ({ { "cmd", "addInsert" }, { "trackId", bus["id"] }, { "type", "cs.reverb" } }))["ok"] == true);
    for (const auto& t : st["project"]["tracks"])
        if (t["kind"] == "synth") { CHECK (json::parse (cmd ({ { "cmd", "setSend" }, { "trackId", t["id"] }, { "busId", bus["id"] }, { "level", 0.25 } }))["ok"] == true); break; }
    const std::string stateBus = f.session.stateJson();
    // Variante Channel Rack / Mixer Pro (0.7): el bombo en su propio canal del Mixer y una cadena en la voz.
    for (const auto& t : st["project"]["tracks"])
    {
        if (t["kind"] == "drums") CHECK (json::parse (cmd ({ { "cmd", "routeDrumLane" }, { "trackId", t["id"] }, { "lane", 0 }, { "own", true } }))["ok"] == true);
        if (t["kind"] == "audio") CHECK (json::parse (cmd ({ { "cmd", "applyFxChain" }, { "trackId", t["id"] }, { "preset", "vocal_urban" }, { "replace", true } }))["ok"] == true);
    }
    const std::string stateRack = f.session.stateJson();
    const std::string catalog = cmd ({ { "cmd", "catalog" } });
    CHECK (json::parse (cmd ({ { "cmd", "createProject" }, { "folder", f.dir + "/vacio" }, { "name", "Idea vacía" }, { "template", "empty" } }))["ok"] == true);
    CHECK (json::parse (cmd ({ { "cmd", "save" } }))["ok"] == true);
    const std::string projects = cmd ({ { "cmd", "listProjects" }, { "root", f.dir } });
    CHECK (json::parse (projects)["projects"].size() == 2);
    if (const char* out = std::getenv ("CS_FIXTURE_DIR"))
    {
        std::filesystem::create_directories (out);
        auto write = [&] (const char* name, const std::string& s) { std::ofstream (std::filesystem::path (out) / name) << json::parse (s).dump (1) << "\n"; };
        write ("state_trap.json", state);
        write ("state_bus.json", stateBus);
        write ("state_rack.json", stateRack);
        write ("catalog.json", catalog);
        write ("projects.json", projects);
    }
}

// Flujo completo del §8 de la misión, de punta a punta en el motor (sin interfaz):
// proyecto 140 BPM → beat trap → bucle → pista synth → melodía → grabar voz sobre la instrumental → mezclar →
// guardar → cerrar → reabrir → exportar WAV (con colas) = reproducción en vivo.
TEST (full_flow_beat_melody_vocal_mix_save_reopen_export)
{
    const std::string dir = cstest::tempDir ("full_flow");
    const std::string folder = dir + "/cancion";
    std::string voiceTrack, leadTrack, leadClip, drumsTrack, patternId;
    std::vector<MidiNote> melody;
    for (int i = 0; i < 16; ++i) melody.push_back ({ 64 + (i * 5) % 12, 90, 0, (Tick) i * kPpq / 2, kPpq / 2 - 60 });
    {
        Engine engine;
        engine.prepare (kSr, 1024);
        Session s (engine);
        CHECK (s.createProject (folder, "Canción", 48000, "trap").ok);
        CHECK (s.model().bpm == 140);
        for (const auto& t : s.model().tracks)
        {
            if (t.kind == "audio") voiceTrack = t.id;
            if (t.kind == "drums") { drumsTrack = t.id; patternId = t.patterns.front().id; }
        }
        // Beat: añade un clap en el paso 13 del patrón A.
        CHECK (s.setPatternStep (drumsTrack, patternId, 2, 12, 110).ok);
        // Bucle de 4 compases mientras se compone: la reproducción vuelve al inicio exactamente.
        const Tick bar = 4 * kPpq;
        CHECK (s.setLoop (true, 0, 4 * bar).ok);
        const Frame loopEnd = s.model().ticksToFrames (4 * bar);
        auto looped = playFrom (engine, loopEnd - 4800, 9600, 256);
        CHECK (engine.position() < loopEnd && engine.position() > 0);
        CHECK (allFinite (looped));
        engine.stop();
        // Pista synth nueva con melodía.
        auto add = s.addTrack ("Lead", "synth");
        CHECK (add.ok);
        leadTrack = add.id;
        CHECK (s.loadSynthPreset (leadTrack, "lead").ok);
        leadClip = firstMidiClip (s, leadTrack);
        CHECK (s.setClipNotes (leadClip, melody).ok);
        // Grabar la voz sobre la instrumental (el bucle se ignora durante la grabación).
        CHECK (s.setLoop (false, 0, 4 * bar).ok);
        engine.seek (0);
        CHECK (s.setTrackArmed (voiceTrack, true).ok);
        CHECK (s.startRecording ("", 1, true).ok);
        const Frame recFrames = kSr * 4;
        std::vector<float> in (256), out (512);
        float monitorPeak = 0;
        for (Frame done = 0; done < recFrames; done += 256)
        {
            for (int i = 0; i < 256; ++i) in[(size_t) i] = 0.3f * std::sin (2.0 * M_PI * 220.0 * (double) (done + i) / kSr);
            engine.process (in.data(), 1, out.data(), 2, 256);
            for (float x : out) monitorPeak = std::max (monitorPeak, std::fabs (x));
            std::this_thread::sleep_for (std::chrono::microseconds (150));
        }
        auto rec = s.stopRecording();
        CHECK_MSG (rec.ok, rec.error);
        CHECK (monitorPeak > 0.05f); // la instrumental sonaba mientras se grababa
        // Mezcla.
        CHECK (s.setTrackVolumeDb (voiceTrack, -3.0f).ok);
        CHECK (s.setTrackPan (leadTrack, -0.3f).ok);
        CHECK (s.setMasterDb (-1.0f).ok);
        CHECK (s.save().ok);
        s.close();
    }
    // Reabrir en un motor nuevo, como tras reiniciar la app.
    Engine engine;
    engine.prepare (kSr, 1024);
    Session s (engine);
    LoadReport report;
    CHECK (s.openProject (folder, &report).ok);
    auto m = s.model();
    CHECK (m.bpm == 140 && ! m.loopEnabled);
    TrackModel* voice = m.findTrack (voiceTrack);
    CHECK (voice != nullptr && voice->clips.size() == 1 && std::fabs (voice->volumeDb + 3.0f) < 1e-6f);
    if (voice && ! voice->clips.empty()) CHECK (voice->clips[0].start == 0 && voice->clips[0].length > kSr * 3);
    const MidiClipModel* lc = m.findMidiClip (leadClip);
    CHECK (lc != nullptr && lc->notes.size() == melody.size());
    const DrumPattern* pat = m.findTrack (drumsTrack) ? m.findTrack (drumsTrack)->findPattern (patternId) : nullptr;
    CHECK (pat != nullptr && pat->lanes[2][12] == 110);
    CHECK (m.findTrack (leadTrack)->instrument.preset == "lead");
    // Exportar y comparar con la reproducción en vivo.
    const std::string wav = dir + "/cancion.wav";
    long clipped = -1;
    CHECK (s.exportMix (wav, 32, 0, &clipped).ok);
    AudioData a;
    std::string err;
    CHECK (readWav (wav, a, err));
    const Frame end = m.contentEnd();
    CHECK (a.numFrames() > end);
    CHECK (clipped == 0);
    auto live = playFrom (engine, 0, end, 512);
    double maxDiff = 0;
    float voicePart = 0;
    for (Frame i = 0; i < end; ++i)
    {
        maxDiff = std::max<double> (maxDiff, std::max (std::fabs (live[(size_t) i * 2] - a.channels[0][(size_t) i]), std::fabs (live[(size_t) i * 2 + 1] - a.channels[1][(size_t) i])));
        voicePart = std::max (voicePart, std::fabs (a.channels[0][(size_t) i]));
    }
    MEASURE ("flujo completo: %lld frames exportados, pico %.3f, diferencia con la reproducción en vivo = %g", (long long) a.numFrames(), voicePart, maxDiff);
    CHECK (maxDiff < 1e-6 && voicePart > 0.05f && voicePart < 1.0f);
}
