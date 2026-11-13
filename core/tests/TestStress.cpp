// Pruebas de carga y estabilidad: edición concurrente de efectos con el audio sonando, sesión larga en bucle
// (determinismo, sin deriva, sin reservas, sin voces acumuladas) y proyecto grande (32 pistas) con guardado,
// reapertura y presupuesto de CPU. Estas pruebas miden el motor en Linux; no sustituyen la medición en un teléfono.
#include "Test.h"

#include "cs/Engine.h"
#include "cs/Processor.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>
#include <thread>

using namespace cs;

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

int envInt (const char* name, int def)
{
    const char* v = std::getenv (name);
    return v && *v ? std::max (1, std::atoi (v)) : def;
}

std::vector<std::string> trackIds (Session& s, const char* kind = nullptr)
{
    std::vector<std::string> ids;
    for (const auto& t : s.model().tracks) if (! kind || t.kind == kind) ids.push_back (t.id);
    return ids;
}

std::vector<std::string> insertIds (Session& s)
{
    std::vector<std::string> ids;
    for (const auto& t : s.model().tracks) for (const auto& i : t.inserts) ids.push_back (i.id);
    return ids;
}

MidiNote note (int key, Tick start, Tick len, int vel = 100) { MidiNote n; n.key = key; n.start = start; n.length = len; n.velocity = vel; return n; }
} // namespace

// Un hilo de audio procesa sin parar mientras el hilo de la interfaz añade, quita, reordena, pone en bypass y ajusta
// efectos, y deshace/rehace. Bajo TSan detecta carreras; en cualquier compilación comprueba que el audio siguió
// sonando, que la salida es finita y acotada y que el modelo final es coherente.
TEST (stress_fx_chain_edits_while_playing_are_thread_safe)
{
    Fx f ("stress_fx", 256);
    CHECK (f.session.createProject (f.dir + "/p", "stress", kSr, "trap").ok);
    const auto tracks = trackIds (f.session);
    CHECK (! tracks.empty());
    const char* types[] = { "cs.gain", "cs.filter", "cs.eq", "cs.saturation", "cs.compressor", "cs.delay", "cs.reverb" };
    const char* params[] = { "gainDb", "cutoffHz", "lowDb", "drive", "thresholdDb", "timeMs", "size", "mix", "nope" };

    std::atomic<bool> stop { false }, finite { true };
    std::atomic<long> blocks { 0 };
    std::atomic<float> peak { 0.0f };
    f.engine.play();
    std::thread audio ([&]
    {
        std::vector<float> out (256 * 2);
        while (! stop.load())
        {
            f.engine.process (nullptr, 0, out.data(), 2, 256);
            float m = 0;
            for (float x : out) { if (! std::isfinite (x)) finite.store (false); m = std::max (m, std::fabs (x)); }
            if (m > peak.load()) peak.store (m);
            blocks.fetch_add (1);
        }
    });

    std::mt19937 rng (11);
    int added = 0, rejected = 0, undos = 0;
    const int iterations = envInt ("CS_STRESS_EDITS", 600);
    for (int i = 0; i < iterations; ++i)
    {
        const auto ins = insertIds (f.session);
        switch (rng() % 8)
        {
            case 0: case 1:
            {
                const auto r = f.session.addInsert (tracks[rng() % tracks.size()], types[rng() % 7]);
                (r.ok ? added : rejected)++;
                break;
            }
            case 2: if (! ins.empty()) f.session.removeInsert (ins[rng() % ins.size()]); break;
            case 3: if (! ins.empty()) f.session.moveInsert (ins[rng() % ins.size()], (int) (rng() % 9) - 1); break;
            case 4: if (! ins.empty()) f.session.setInsertBypass (ins[rng() % ins.size()], rng() % 2 == 0); break;
            case 5: case 6:
                if (! ins.empty())
                    f.session.setInsertParam (ins[rng() % ins.size()], params[rng() % 9], (float) ((int) (rng() % 40000) - 20000) / 10.0f);
                break;
            case 7: if (rng() % 2) { f.session.undo(); ++undos; } else f.session.redo(); break;
        }
    }
    // Llenar una pista hasta el límite: el noveno efecto debe rechazarse sin romper nada.
    const auto full = tracks.front();
    int extra = 0;
    while (f.session.addInsert (full, "cs.gain").ok && extra < 20) ++extra;
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    stop.store (true);
    audio.join();
    f.engine.stop();

    const auto m = f.session.model();
    size_t maxPerTrack = 0;
    for (const auto& t : m.tracks) maxPerTrack = std::max (maxPerTrack, t.inserts.size());
    MEASURE ("%d ediciones con el audio sonando: %d efectos añadidos, %d rechazados, %d deshacer; %ld bloques procesados; pico %.2f",
             iterations, added, rejected, undos, blocks.load(), peak.load());
    CHECK (finite.load());
    CHECK (blocks.load() > 20);
    CHECK (peak.load() < 16.0f); // sin explosiones (la saturación del master es otra cuestión: aquí hay efectos al azar)
    CHECK (validate (m).empty());
    CHECK (maxPerTrack == 8);
    // El estado guardado se reabre igual.
    CHECK (f.session.save().ok);
    const std::string before = toJson (f.session.model(), false);
    f.session.close();
    CHECK (f.session.openProject (f.dir + "/p").ok);
    CHECK (toJson (f.session.model(), false) == before);
}

// Sesión larga en bucle: el mismo tramo debe sonar igual en la pasada 3 y en la última (sin deriva de tiempo, sin
// voces acumuladas, sin estado que crezca) y el callback no reserva memoria en ningún momento.
TEST (stress_long_loop_session_is_deterministic_and_does_not_drift)
{
    Fx f ("stress_long", 256);
    CHECK (f.session.createProject (f.dir + "/p", "long", kSr, "trap").ok);
    const auto synths = trackIds (f.session, "synth");
    CHECK (! synths.empty());
    // Efectos con memoria (delay con realimentación y reverb larga) para que cualquier estado que crezca se note.
    const auto d = f.session.addInsert (synths.front(), "cs.delay");
    const auto r = f.session.addInsert (synths.back(), "cs.reverb");
    CHECK (d.ok && r.ok);
    f.session.setInsertParam (d.id, "feedback", 0.5f);
    f.session.setInsertParam (r.id, "size", 0.3f); // cola declarada de 2,5 s, menor que la vuelta del bucle
    CHECK (f.session.setLoop (true, 0, 16 * kPpq).ok); // 4 compases a 140 BPM ~ 6,86 s
    const Frame loopLen = f.session.model().ticksToFrames (16 * kPpq);
    const int passes = envInt ("CS_STRESS_PASSES", 30);
    MEASURE ("bucle de %lld frames (%.2f s), %d pasadas = %.1f min de audio por recorrido", (long long) loopLen, loopLen / (double) kSr,
             passes, passes * loopLen / (double) kSr / 60.0);

    struct Result { std::vector<float> pass3, last; std::vector<double> rms; bool finite = true; float peak = 0; long allocs = 0; Frame pos = 0; };
    auto render = [&]
    {
        Result res;
        res.pass3.assign ((size_t) loopLen * 2, 0.0f);
        res.last.assign ((size_t) loopLen * 2, 0.0f);
        res.rms.assign ((size_t) passes, 0.0);
        f.engine.seek (0);
        f.engine.play();
        std::vector<float> out (256 * 2);
        const Frame total = loopLen * passes;
        cstest::allocationCount.store (0);
        cstest::countAllocations.store (true);
        for (Frame done = 0; done < total; done += 256)
        {
            const int n = (int) std::min<Frame> (256, total - done);
            f.engine.process (nullptr, 0, out.data(), 2, n);
            for (int i = 0; i < n; ++i)
            {
                const Frame g = done + i, pass = g / loopLen, off = g % loopLen;
                const float l = out[(size_t) i * 2], rr = out[(size_t) i * 2 + 1];
                if (! std::isfinite (l) || ! std::isfinite (rr)) res.finite = false;
                res.peak = std::max ({ res.peak, std::fabs (l), std::fabs (rr) });
                res.rms[(size_t) pass] += (double) l * l + (double) rr * rr;
                float* dst = pass == 2 ? res.pass3.data() : pass == passes - 1 ? res.last.data() : nullptr;
                if (dst) { dst[off * 2] = l; dst[off * 2 + 1] = rr; }
            }
        }
        cstest::countAllocations.store (false);
        res.allocs = cstest::allocationCount.load();
        res.pos = f.engine.position();
        f.engine.stop();
        for (auto& v : res.rms) v = std::sqrt (v / (2.0 * (double) loopLen));
        // Dejar que suenen las colas antes del siguiente recorrido.
        std::vector<float> tail (1024 * 2);
        for (int i = 0; i < kSr * 8 / 1024; ++i) f.engine.process (nullptr, 0, tail.data(), 2, 1024);
        return res;
    };
    auto maxDiffDb = [] (const Result& r)
    {
        float m = 0;
        for (size_t i = 0; i < r.pass3.size(); ++i) m = std::max (m, std::fabs (r.pass3[i] - r.last[i]));
        return 20 * std::log10 (std::max (m, 1e-12f));
    };

    // 1) Mezcla completa. Los osciladores del synth giran libres (la fase no se reinicia al reutilizar una voz, para
    //    evitar clics), así que la forma de onda no se repite a la muestra entre pasadas; sí deben repetirse la
    //    energía y la ausencia de reservas, voces acumuladas o deriva de posición.
    const Result full = render();
    double rmsMin = 1e9, rmsMax = 0;
    for (size_t p = 2; p < full.rms.size(); ++p) { rmsMin = std::min (rmsMin, full.rms[p]); rmsMax = std::max (rmsMax, full.rms[p]); }
    const double spreadDb = 20 * std::log10 (rmsMax / std::max (rmsMin, 1e-12));
    MEASURE ("mezcla completa: RMS por pasada (3..%d) entre %.4f y %.4f (%.2f dB); pico %.2f; reservas %ld; posición final %lld",
             passes, rmsMin, rmsMax, spreadDb, full.peak, full.allocs, (long long) full.pos);
    CHECK (full.finite);
    CHECK (full.allocs == 0);
    CHECK (full.peak > 0.05f && full.peak < 1.0f);
    CHECK (spreadDb < 0.5); // la energía no crece ni decae con el tiempo
    CHECK (full.pos == 0);  // 30 vueltas exactas: la posición vuelve al inicio del bucle, sin deriva
    int voices = 0;
    for (const auto& id : synths) voices += f.session.activeVoices (id);
    MEASURE ("voces activas tras parar y dejar sonar las colas: %d", voices);
    CHECK (voices == 0);

    // 2) Solo la batería (muestras fijas): cada golpe debe caer en la misma muestra en la pasada 3 y en la última.
    for (const auto& id : synths) f.session.setTrackMute (id, true);
    const Result drums = render();
    const double drumsDiff = maxDiffDb (drums);
    MEASURE ("solo batería: pasada 3 vs pasada %d, diferencia máxima %.1f dB; pico %.2f", passes, drumsDiff, drums.peak);
    CHECK (drums.finite && drums.allocs == 0);
    CHECK (drums.peak > 0.05f);
    CHECK (drumsDiff < -120.0); // idéntico: el bucle no acumula deriva de tiempo
}

// Proyecto grande: el máximo de pistas (32) con batería, synths con acordes, audio importado y 2 efectos por pista.
// Se guarda, se reabre idéntico, rechaza la pista 33 y procesa sin reservas; mide el coste por bloque.
TEST (stress_large_project_32_tracks_saves_reopens_and_renders)
{
    Fx f ("stress_big", 256);
    CHECK (f.session.createProject (f.dir + "/p", "grande", kSr, "").ok);
    // WAV de 20 s para las pistas de audio.
    AudioData wav;
    wav.sampleRate = kSr;
    wav.allocate (2, kSr * 20);
    for (Frame i = 0; i < wav.numFrames(); ++i)
        wav.channels[0][(size_t) i] = wav.channels[1][(size_t) i] = 0.2f * (float) std::sin (2 * M_PI * 220.0 * (double) i / kSr);
    std::string err;
    CHECK (writeWav (f.dir + "/tone.wav", wav, WavFormat::Pcm24, err));

    const char* fx[] = { "cs.eq", "cs.compressor", "cs.filter", "cs.reverb", "cs.delay", "cs.saturation" };
    int made = 0;
    for (int i = 0; i < kMaxTracks; ++i)
    {
        const char* kind = i < 6 ? "drums" : i < 22 ? "synth" : "audio";
        const auto t = f.session.addTrack ("T" + std::to_string (i + 1), kind);
        if (! t.ok) break;
        ++made;
        f.session.setTrackVolumeDb (t.id, -18.0f);
        f.session.addInsert (t.id, fx[i % 6]);
        f.session.addInsert (t.id, fx[(i + 2) % 6]);
        if (std::string (kind) == "synth")
        {
            std::vector<MidiNote> notes;
            for (int bar = 0; bar < 32; ++bar)
                for (int k = 0; k < 4; ++k) notes.push_back (note (48 + (i % 12) + k * 4, (Tick) bar * 4 * kPpq, 4 * kPpq - 10, 90));
            for (const auto& tr : f.session.model().tracks)
                if (tr.id == t.id && ! tr.midiClips.empty())
                {
                    f.session.resizeMidiClip (tr.midiClips.front().id, 0, 32 * 4 * kPpq);
                    CHECK (f.session.setClipNotes (tr.midiClips.front().id, notes).ok);
                }
        }
        if (std::string (kind) == "audio") CHECK (f.session.importWav (f.dir + "/tone.wav", t.id, (Frame) (i - 22) * kSr).ok);
    }
    CHECK (made == kMaxTracks);
    CHECK (! f.session.addTrack ("sobra", "synth").ok);

    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    CHECK (f.session.save().ok);
    const double saveMs = std::chrono::duration<double, std::milli> (clock::now() - t0).count();
    const std::string before = toJson (f.session.model(), false);
    f.session.close();
    t0 = clock::now();
    LoadReport rep;
    CHECK (f.session.openProject (f.dir + "/p", &rep).ok);
    const double openMs = std::chrono::duration<double, std::milli> (clock::now() - t0).count();
    CHECK (rep.missingSources.empty());
    CHECK (toJson (f.session.model(), false) == before);

    // 20 s de reproducción en bloques de 256 (5,3 ms).
    f.engine.seek (0);
    f.engine.play();
    std::vector<float> out (256 * 2);
    std::vector<double> us;
    us.reserve ((size_t) (kSr * 20 / 256)); // la medición no debe reservar dentro del bucle
    bool finite = true;
    cstest::allocationCount.store (0);
    cstest::countAllocations.store (true);
    for (int b = 0; b < kSr * 20 / 256; ++b)
    {
        const auto s = clock::now();
        f.engine.process (nullptr, 0, out.data(), 2, 256);
        us.push_back (std::chrono::duration<double, std::micro> (clock::now() - s).count());
        for (float x : out) if (! std::isfinite (x)) finite = false;
    }
    cstest::countAllocations.store (false);
    f.engine.stop();
    std::sort (us.begin(), us.end());
    const double p99 = us[us.size() * 99 / 100], budget = 256.0 * 1e6 / kSr;
    MEASURE ("32 pistas (6 baterías, 16 synths con acordes de 4 notas, 10 de audio), 64 efectos: guardar %.1f ms, abrir %.1f ms (%zu bytes de JSON)",
             saveMs, openMs, before.size());
    MEASURE ("proceso por bloque de 256: mediana %.0f us, p99 %.0f us = %.1f %% del presupuesto de %.0f us (esta máquina, no un teléfono)",
             us[us.size() / 2], p99, 100 * p99 / budget, budget);
    CHECK (finite);
    CHECK (cstest::allocationCount.load() == 0);
}

// Colas que decaen: con los desnormales a cero en el callback, la salida nunca contiene números desnormales y el coste
// por bloque en silencio no se dispara (en x86 un desnormal cuesta decenas de veces más).
TEST (stress_decaying_tails_never_produce_denormals)
{
    Fx f ("denormals", 256);
    CHECK (f.session.createProject (f.dir + "/p", "tails", kSr, "").ok);
    AudioData wav;
    wav.sampleRate = kSr;
    wav.allocate (1, kSr / 10);
    for (Frame i = 0; i < wav.numFrames(); ++i) wav.channels[0][(size_t) i] = (i % 97 < 48 ? 0.5f : -0.5f) * (1.0f - (float) i / (float) wav.numFrames());
    std::string err;
    CHECK (writeWav (f.dir + "/burst.wav", wav, WavFormat::Float32, err));
    const auto t = f.session.addTrack ("A", "audio");
    CHECK (f.session.importWav (f.dir + "/burst.wav", t.id, 0).ok);
    for (const char* fx : { "cs.filter", "cs.eq", "cs.delay", "cs.reverb" })
    {
        const auto r = f.session.addInsert (t.id, fx);
        CHECK (r.ok);
        if (std::string (fx) == "cs.reverb") { f.session.setInsertParam (r.id, "size", 1.0f); f.session.setInsertParam (r.id, "damping", 0.0f); f.session.setInsertParam (r.id, "mix", 1.0f); }
        if (std::string (fx) == "cs.delay") f.session.setInsertParam (r.id, "feedback", 0.9f);
    }
    f.engine.seek (0);
    f.engine.play();
    std::vector<float> out (256 * 2);
    long denormals = 0;
    double early = 0, late = 0;
    using clock = std::chrono::steady_clock;
    const int blocks = kSr * 90 / 256;
    for (int b = 0; b < blocks; ++b)
    {
        const auto s = clock::now();
        f.engine.process (nullptr, 0, out.data(), 2, 256);
        const double us = std::chrono::duration<double, std::micro> (clock::now() - s).count();
        if (b >= blocks / 20 && b < blocks / 10) early += us;
        if (b >= blocks - blocks / 20) late += us;
        for (float x : out) if (x != 0.0f && std::fabs (x) < 1.17549435e-38f) ++denormals;
    }
    f.engine.stop();
    MEASURE ("90 s de colas (filtro, EQ, delay 0,9, reverb máxima): %ld muestras desnormales; coste medio por bloque al principio %.1f us, al final %.1f us",
             denormals, early / (blocks / 20), late / (blocks / 20));
    CHECK (denormals == 0);

    // Comprobación determinista del modo: un procesador de prueba mira, dentro del callback, si un desnormal se
    // convierte en cero (en vivo y en la exportación); fuera del motor el modo normal se restaura.
    struct Probe final : AudioProcessor
    {
        std::atomic<int> flushed { -1 };
        std::string typeId() const override { return "test.probe"; }
        void prepare (double, int) override {}
        void reset() override {}
        void process (float* const*, int, int) override
        {
            volatile float tiny = 1e-39f; // desnormal
            volatile float half = tiny * 0.5f;
            flushed.store (half == 0.0f ? 1 : 0);
        }
    };
    auto probe = std::make_shared<Probe>();
    Engine e;
    e.prepare (kSr, 256);
    auto g = std::make_unique<RenderGraph>();
    TrackRT tr;
    tr.slot = 0;
    tr.inserts.push_back (probe);
    g->tracks.push_back (tr);
    e.publish (std::move (g));
    e.play();
    std::vector<float> o (256 * 2);
    e.process (nullptr, 0, o.data(), 2, 256);
    const int live = probe->flushed.load();
    probe->flushed.store (-1);
    std::vector<float> l (256), r (256);
    e.renderOffline (0, l.data(), r.data(), 256);
    const int offline = probe->flushed.load();
    volatile float tiny = 1e-39f;
    volatile float outside = tiny * 0.5f;
    MEASURE ("desnormales a cero: en vivo %d, exportación %d, fuera del motor %d (1 = sí)", live, offline, outside == 0.0f ? 1 : 0);
#if defined(__SSE__) || defined(__aarch64__)
    CHECK (live == 1 && offline == 1);
    CHECK (outside != 0.0f); // el modo se restaura al salir del callback
#endif
}

// Medidor de carga DSP (lo que la app muestra en Ajustes para medir en el teléfono): coherente y sin reservas.
TEST (dsp_load_meter_reports_sane_values)
{
    Fx f ("dspload", 256);
    CHECK (f.session.createProject (f.dir + "/p", "load", kSr, "trap").ok);
    f.engine.play();
    std::vector<float> out (256 * 2);
    for (int b = 0; b < 400; ++b) f.engine.process (nullptr, 0, out.data(), 2, 256);
    const float avg = f.engine.dspLoadAverage();
    const float peak = f.engine.takeDspLoadPeak();
    MEASURE ("carga DSP de la plantilla trap en esta máquina: media %.2f %%, pico %.2f %%, bloques fuera de tiempo %ld",
             100 * avg, 100 * peak, f.engine.blocksOverBudget());
    CHECK (avg > 0.0f && avg < 1.0f);
    CHECK (peak >= avg);
    CHECK (f.engine.takeDspLoadPeak() == 0.0f); // leer el pico lo reinicia
    cstest::allocationCount.store (0);
    cstest::countAllocations.store (true);
    for (int b = 0; b < 200; ++b) f.engine.process (nullptr, 0, out.data(), 2, 256);
    cstest::countAllocations.store (false);
    CHECK (cstest::allocationCount.load() == 0);
    f.engine.stop();
}
