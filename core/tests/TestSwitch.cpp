// Regresión del fallo de Play al cambiar de proyecto (0.3.0, observado en Samsung):
//   Proyecto A -> Play -> cerrar -> Proyecto B -> Play -> cerrar -> Proyecto A -> Play.
// Se maneja el motor exactamente como la app: comandos JSON (cs::handleCommand) en el hilo de control y
// Engine::process() en un hilo aparte que imita el callback de Oboe. Se comprueba audio real (RMS), posición,
// transporte, voces, guardado y que no queden instancias de grafos antiguos.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <thread>

using namespace cs;
using nlohmann::json;

namespace
{
json cmd (Session& s, Engine& e, json q)
{
    return json::parse (handleCommand (s, e, q.dump()));
}

// Renderiza 'frames' como el callback de audio y devuelve el RMS.
double pump (Engine& e, Frame frames, int block = 192)
{
    std::vector<float> out ((size_t) block * 2);
    double sum = 0;
    Frame done = 0;
    while (done < frames)
    {
        const int n = (int) std::min<Frame> (block, frames - done);
        e.process (nullptr, 0, out.data(), 2, n);
        for (int i = 0; i < n * 2; ++i) sum += (double) out[(size_t) i] * out[(size_t) i];
        done += n;
    }
    return std::sqrt (sum / (double) (frames * 2));
}

struct Rig
{
    Engine engine;
    Session session { engine };
    std::string dir;
    explicit Rig (const std::string& name) { dir = cstest::tempDir (name); engine.prepare (48000, 1024); }
    json operator() (json q) { return cmd (session, engine, std::move (q)); }
    std::string create (const std::string& sub, const std::string& tpl)
    {
        const std::string f = dir + "/" + sub;
        const json r = (*this) ({ { "cmd", "createProject" }, { "folder", f }, { "name", sub }, { "template", tpl } });
        CHECK_MSG (r["ok"].get<bool>(), r.dump());
        return f;
    }
};
} // namespace

// Umbral de «suena»: las plantillas de la 0.4 empiezan con una Intro suave (RMS ≈ 0,005 en el primer segundo), y el
// fallo que buscamos da silencio exacto (RMS 0,0000). 0,002 (≈ -54 dBFS) separa ambos casos con margen.
constexpr double kAudible = 0.002;

// Reproduce la secuencia del informe con el motor en el mismo hilo (determinista).
TEST (switch_projects_play_after_long_playback)
{
    Rig r ("switch_long");
    const std::string a = r.create ("A", "trap");
    const std::string b = r.create ("B", "reggaeton");
    r ({ { "cmd", "closeProject" } });

    // A: Play durante 30 s (más que las 8 barras de la plantilla) y salir sin pulsar Stop.
    CHECK (r ({ { "cmd", "openProject" }, { "folder", a } })["ok"].get<bool>());
    CHECK (r ({ { "cmd", "play" } })["ok"].get<bool>());
    pump (r.engine, 30 * 48000);
    r ({ { "cmd", "closeProject" } });
    pump (r.engine, 4800);

    // B: al abrir, el transporte debe estar en reposo y en el inicio; Play debe sonar.
    CHECK (r ({ { "cmd", "openProject" }, { "folder", b } })["ok"].get<bool>());
    CHECK_MSG (r.engine.position() == 0, "posición heredada de A: " + std::to_string (r.engine.position()));
    CHECK (! r.engine.isPlaying());
    CHECK (r ({ { "cmd", "play" } })["ok"].get<bool>());
    const double rmsB = pump (r.engine, 2 * 48000);
    MEASURE ("B tras A: RMS %.4f, posición %lld", rmsB, (long long) r.engine.position());
    CHECK_MSG (rmsB > kAudible, "B no suena tras cerrar A: RMS " + std::to_string (rmsB));
    r ({ { "cmd", "closeProject" } });

    // Vuelta a A: también debe sonar.
    CHECK (r ({ { "cmd", "openProject" }, { "folder", a } })["ok"].get<bool>());
    CHECK (r.engine.position() == 0);
    r ({ { "cmd", "play" } });
    const double rmsA = pump (r.engine, 2 * 48000);
    MEASURE ("A de nuevo: RMS %.4f", rmsA);
    CHECK_MSG (rmsA > kAudible, "A no suena al volver: RMS " + std::to_string (rmsA));
}

// Bucle activo cuya región termina antes de la posición actual: el transporte debe volver a la región.
TEST (loop_region_behind_playhead_wraps_instead_of_playing_silence)
{
    Rig r ("switch_loop");
    r.create ("L", "hiphop");
    const json st = json::parse (r.session.stateJson());
    (void) st;
    r ({ { "cmd", "setLoop" }, { "on", true }, { "start", 0 }, { "end", 4 * 4 * 960 } });
    r ({ { "cmd", "seek" }, { "frame", 48000 * 60 } }); // muy por detrás de la región
    r ({ { "cmd", "play" } });
    pump (r.engine, 4800);
    const Frame loopEnd = r.session.model().ticksToFrames (4 * 4 * 960);
    CHECK_MSG (r.engine.position() < loopEnd, "posición fuera del bucle: " + std::to_string (r.engine.position()));
    const double rms = pump (r.engine, 48000);
    CHECK_MSG (rms > kAudible, "el bucle no suena: " + std::to_string (rms));
}

namespace
{
// Hilo que imita el callback de Oboe: llama a process() sin parar (sin esperas, para que la prueba sea rápida),
// con una entrada de micrófono sintética (seno). Mide energía de salida y frames desde la última petición.
struct FakeOboe
{
    Engine& e;
    std::atomic<bool> quit { false }, pauseReq { false }, pausedAck { false };
    std::atomic<int> epoch { 0 };
    std::atomic<double> energy { 0 };
    std::atomic<long long> frames { 0 };
    std::thread th;
    explicit FakeOboe (Engine& en) : e (en)
    {
        th = std::thread ([this] {
            const int block = 192;
            std::vector<float> in ((size_t) block), out ((size_t) block * 2);
            int seen = 0;
            double sum = 0; long long n = 0; long long phase = 0;
            while (! quit.load())
            {
                if (pauseReq.load()) { pausedAck.store (true); std::this_thread::yield(); continue; }
                pausedAck.store (false);
                const int ep = epoch.load();
                if (ep != seen) { seen = ep; sum = 0; n = 0; }
                for (int i = 0; i < block; ++i) in[(size_t) i] = 0.3f * (float) std::sin (2 * 3.14159265 * 440.0 * (double) (phase + i) / 48000.0);
                phase += block;
                e.process (in.data(), 1, out.data(), 2, block);
                for (float v : out) sum += (double) v * v;
                n += block;
                energy.store (sum); frames.store (n);
                if (epoch.load() == seen) {} // nada
                std::this_thread::sleep_for (std::chrono::microseconds (50)); // deja respirar al hilo de disco
            }
        });
    }
    ~FakeOboe() { quit.store (true); th.join(); }
    // Empieza una medición nueva y espera 'count' frames; devuelve el RMS.
    double measure (long long count)
    {
        epoch.fetch_add (1);
        // Espera a que el hilo vea la nueva época (frames vuelve a contar desde 0).
        while (frames.load() >= count) std::this_thread::yield();
        while (frames.load() < count) std::this_thread::yield();
        const double s = energy.load(); const long long n = frames.load();
        return std::sqrt (s / (double) (2 * std::max<long long> (1, n)));
    }
    void pause() { pauseReq.store (true); while (! pausedAck.load()) std::this_thread::yield(); }
    void resume() { pauseReq.store (false); }
};
} // namespace

// 120 cambios de sesión con el «callback» en otro hilo: plantillas con instrumentos, loops, efectos, bus de reverb,
// proyecto guardado y reabierto, grabación real y aperturas rápidas del mismo proyecto. En cada apertura se exige
// transporte parado en el inicio, audio audible al pulsar Play, posición que avanza, ninguna voz atascada (también
// tras una nota en vivo sin Note Off del proyecto anterior) y ninguna instancia de instrumento de grafos antiguos.
TEST (switch_120_sessions_with_audio_thread)
{
    Rig r ("switch_120");
    std::vector<std::string> folders;
    for (const char* t : { "trap", "reggaeton", "hiphop", "pop", "electronic" }) folders.push_back (r.create (t, t));

    // Proyecto con efectos de inserción y bus de reverb con envíos.
    const std::string fx = r.create ("fx", "trap");
    {
        const json st = json::parse (r.session.stateJson());
        std::string synthId;
        for (const auto& t : st["project"]["tracks"]) if (t["kind"] == "synth") { synthId = t["id"]; break; }
        CHECK (! synthId.empty());
        CHECK (r ({ { "cmd", "addInsert" }, { "trackId", synthId }, { "type", "cs.delay" } })["ok"].get<bool>());
        CHECK (r ({ { "cmd", "addInsert" }, { "trackId", synthId }, { "type", "cs.compressor" } })["ok"].get<bool>());
        const json bus = r ({ { "cmd", "addTrack" }, { "name", "Rev" }, { "kind", "bus" } });
        CHECK (bus["ok"].get<bool>());
        CHECK (r ({ { "cmd", "addInsert" }, { "trackId", bus["id"] }, { "type", "cs.reverb" } })["ok"].get<bool>());
        CHECK (r ({ { "cmd", "setSend" }, { "trackId", synthId }, { "busId", bus["id"] }, { "level", 0.6 } })["ok"].get<bool>());
        CHECK (r ({ { "cmd", "save" } })["ok"].get<bool>());
    }
    folders.push_back (fx);
    // Proyecto con un bucle activo de 2 compases.
    const std::string lp = r.create ("loop", "hiphop");
    CHECK (r ({ { "cmd", "setLoop" }, { "on", true }, { "start", 0 }, { "end", 2 * 4 * 960 } })["ok"].get<bool>());
    CHECK (r ({ { "cmd", "save" } })["ok"].get<bool>());
    folders.push_back (lp);
    // Proyecto de audio: se grabará en él durante la prueba (la toma debe sobrevivir a los cambios).
    const std::string rec = r.create ("rec", "pop");
    folders.push_back (rec);
    r ({ { "cmd", "closeProject" } });

    FakeOboe audio (r.engine);
    int opens = 0, recordedClips = 0, audibleFails = 0;
    double minRms = 1e9;
    std::string recTrack;
    for (int i = 0; i < 120; ++i)
    {
        const std::string& f = folders[(size_t) i % folders.size()];
        const int variant = (i / (int) folders.size()) % 4;
        if (variant == 2)
            for (int k = 0; k < 3; ++k) // abrir y cerrar rápido el mismo proyecto (sin esperar al audio)
            {
                CHECK (r ({ { "cmd", "openProject" }, { "folder", f } })["ok"].get<bool>());
                r ({ { "cmd", "play" } });
                r ({ { "cmd", "closeProject" } });
                ++opens;
            }
        const json o = r ({ { "cmd", "openProject" }, { "folder", f } });
        CHECK_MSG (o["ok"].get<bool>(), o.dump());
        ++opens;
        audio.measure (384); // el hilo de audio ha adoptado el grafo nuevo
        CHECK_MSG (! r.engine.isPlaying(), "transporte en marcha al abrir");
        CHECK_MSG (r.engine.position() == 0, "posición al abrir: " + std::to_string (r.engine.position()));

        // Ninguna voz sonando al abrir (aunque el ciclo anterior dejara una nota en vivo sin Note Off).
        const json st = json::parse (r.session.stateJson());
        audio.pause();
        for (const auto& t : st["project"]["tracks"]) CHECK_MSG (r.session.activeVoices (t["id"]) == 0, "voz atascada en " + t["name"].get<std::string>());
        audio.resume();

        if (variant == 3 && f == lp) r ({ { "cmd", "seek" }, { "frame", 48000 * 90 } }); // con bucle, debe volver a la región
        if (variant == 3 && f != lp) { audio.pause(); std::this_thread::sleep_for (std::chrono::milliseconds (20)); audio.resume(); } // stream parado y reanudado
        CHECK (r ({ { "cmd", "play" } })["ok"].get<bool>());
        const Frame before = r.engine.position();
        const double rms = audio.measure (24000);
        minRms = std::min (minRms, rms);
        if (rms < kAudible) { ++audibleFails; CHECK_MSG (false, "sin audio al reproducir " + f + " (ciclo " + std::to_string (i) + "): RMS " + std::to_string (rms)); }
        CHECK (r.engine.isPlaying());
        if (variant != 3 && f != lp) CHECK_MSG (r.engine.position() > before, "la posición no avanza");

        if (f == rec && variant != 2)
        {
            // Graba 0,5 s en la pista armada, para y vuelve al inicio (como tras grabar en la app).
            r ({ { "cmd", "stop" } });
            if (recTrack.empty()) for (const auto& t : st["project"]["tracks"]) if (t.value ("armed", false)) recTrack = t["id"];
            const json sr = r ({ { "cmd", "startRecording" }, { "trackId", recTrack } });
            CHECK_MSG (sr["ok"].get<bool>(), sr.dump());
            audio.measure (24000);
            const json stp = r ({ { "cmd", "stop" } });
            CHECK_MSG (stp["ok"].get<bool>(), stp.dump());
            ++recordedClips;
        }

        // Nota en vivo sin Note Off en la pista 1 (un pad o tecla pulsado al salir).
        r.engine.sendLive (1, 0x90, 60, 100);
        if (variant == 1) { r ({ { "cmd", "stop" } }); audio.measure (4800); }
        if (i % 10 == 9)
        {
            const json t = r ({ { "cmd", "setTempo" }, { "bpm", 80 + i % 40 }, { "beatsPerBar", 4 } });
            CHECK (t["ok"].get<bool>());
        }
        r ({ { "cmd", "closeProject" } });
        // Tras cerrar: silencio y ninguna instancia de instrumento viva (grafos antiguos liberados).
        audio.measure (384);
        const double after = audio.measure (960);
        CHECK_MSG (after < 1e-6, "suena tras cerrar: " + std::to_string (after));
        audio.pause();
        r.engine.collectGarbage();
        CHECK_MSG (Instrument::liveInstances() == 0, "instrumentos vivos tras cerrar: " + std::to_string (Instrument::liveInstances()));
        audio.resume();
    }
    MEASURE ("aperturas %d, RMS mínimo al reproducir %.4f, fallos de audio %d, tomas grabadas %d", opens, minRms, audibleFails, recordedClips);
    CHECK (opens >= 100);

    // Guardado: las tomas grabadas y el tempo cambiado siguen ahí al reabrir.
    CHECK (r ({ { "cmd", "openProject" }, { "folder", rec } })["ok"].get<bool>());
    const json st = json::parse (r.session.stateJson());
    int clips = 0;
    for (const auto& t : st["project"]["tracks"]) if (t["id"] == recTrack) clips = (int) t["clips"].size();
    MEASURE ("clips de audio grabados en la pista armada: %d", clips);
    CHECK_MSG (clips == recordedClips, "se esperaban " + std::to_string (recordedClips) + " clips, hay " + std::to_string (clips));
    r ({ { "cmd", "closeProject" } });
}
