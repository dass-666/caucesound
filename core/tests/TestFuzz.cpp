// Fuzzing determinista (semilla fija) de las dos entradas que vienen de fuera del motor:
//  1. la API de comandos JSON que usa la interfaz (comandos y argumentos al azar, tipos equivocados, ids reales y
//     falsos, valores extremos), con el hilo de audio procesando a la vez;
//  2. project.json dañado al azar (bytes cambiados, recortes, duplicados) al abrir un proyecto.
// Criterio: nunca un fallo (ASan/UBSan/TSan lo detectan), siempre una respuesta JSON con "ok", y el proyecto que
// queda es válido, se guarda y se reabre igual.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Session.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

using namespace cs;
using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
int envInt (const char* name, int def)
{
    const char* v = std::getenv (name);
    return v && *v ? std::max (1, std::atoi (v)) : def;
}

const char* kCommands[] = {
    "addInsert", "addMidiClip", "addPattern", "addTrack", "catalog", "deleteClip", "deleteMidiClip", "duplicateClip",
    "duplicateMidiClip", "endParamGesture", "export", "insertMeter", "loadPreset", "moveClip", "moveInsert",
    "moveMidiClip", "play", "quantizeClip", "redo", "removeInsert", "removeTrack", "renamePattern", "renameTrack",
    "resizeMidiClip", "save", "seek", "setArmed", "setClipGain", "setClipNotes", "setClipPattern", "setFades",
    "setInsertBypass", "setInsertParam", "setInstrumentParam", "setLatencyOffset", "setLoop", "setMaster",
    "setMetronome", "setMute", "setPan", "setPatternLanes", "setPatternLength", "setSolo", "setStep", "setSwing",
    "setTempo", "setVolume", "splitClip", "state", "stop", "trimClip", "undo", "noExiste", "",
};
const char* kKeys[] = { "trackId", "clipId", "patternId", "insertId", "copyFrom", "name", "type", "preset", "param",
                        "value", "db", "pan", "start", "length", "index", "lane", "step", "velocity", "swing", "on",
                        "bpm", "beatsPerBar", "frames", "at", "fadeIn", "fadeOut", "gainDb", "grid", "strength",
                        "notes", "lanes", "steps", "kind", "bitDepth", "path", "end", "id" };
} // namespace

TEST (fuzz_command_api_never_crashes_and_leaves_a_valid_project)
{
    const std::string dir = cstest::tempDir ("fuzz_api");
    Engine engine;
    engine.prepare (48000, 256);
    Session s (engine);
    auto cmd = [&] (const json& j) { return handleCommand (s, engine, j.dump()); };
    CHECK (json::parse (cmd ({ { "cmd", "createProject" }, { "folder", dir + "/p" }, { "name", "fuzz" }, { "template", "trap" } }))["ok"] == true);

    std::atomic<bool> stop { false }, finite { true };
    std::thread audio ([&]
    {
        std::vector<float> out (256 * 2);
        while (! stop.load())
        {
            engine.process (nullptr, 0, out.data(), 2, 256);
            for (float x : out) if (! std::isfinite (x)) finite.store (false);
        }
    });

    std::mt19937 rng (2026);
    auto pick = [&] (size_t n) { return (size_t) (rng() % n); };
    // Valores candidatos: ids reales del proyecto (se refrescan), basura, tipos equivocados y extremos.
    std::vector<std::string> ids;
    auto refreshIds = [&]
    {
        ids.clear();
        const auto m = s.model();
        for (const auto& t : m.tracks)
        {
            ids.push_back (t.id);
            for (const auto& c : t.clips) ids.push_back (c.id);
            for (const auto& c : t.midiClips) ids.push_back (c.id);
            for (const auto& p : t.patterns) ids.push_back (p.id);
            for (const auto& i : t.inserts) ids.push_back (i.id);
        }
    };
    auto value = [&] () -> json
    {
        switch (rng() % 16)
        {
            case 0: return ids.empty() ? json ("x") : json (ids[pick (ids.size())]);
            case 1: return ids.empty() ? json ("x") : json (ids[pick (ids.size())]);
            case 2: return json ("../../etc/passwd");
            case 3: return json (std::string (2000, 'A'));
            case 4: return json (nullptr);
            case 5: return json (true);
            case 6: return json ((int) (rng() % 200) - 50);
            case 7: return json (1e30);
            case 8: return json (-1e30);
            case 9: return json ((double) (rng() % 100000) / 100.0);
            case 10: return json::array ({ json::array ({ 60, 100, 0, 0, 960 }), json::array ({ 200, -5, 99, -100, 0 }) });
            case 11: return json::array ({ json::array ({ 1, 2, 3 }), "x", nullptr });
            case 12: return json::object ({ { "a", 1 } });
            case 13: return json (std::vector<int> (40, 127));
            case 14: { static const char* t[] = { "cs.reverb", "cs.delay", "cs.eq", "cs.synth", "synth", "drums", "audio", "pad", "lead", "cutoffHz", "mix", "kit", "nope" }; return json (t[pick (13)]); }
            default: return json (dir + "/fuzz_out_" + std::to_string (rng() % 3) + ".wav");
        }
    };

    const int iterations = envInt ("CS_FUZZ_ITERATIONS", 4000);
    int ok = 0, rejected = 0, badReply = 0;
    for (int i = 0; i < iterations; ++i)
    {
        if (i % 25 == 0) refreshIds();
        json j;
        j["cmd"] = kCommands[pick (sizeof kCommands / sizeof kCommands[0])];
        const int nargs = (int) (rng() % 6);
        for (int a = 0; a < nargs; ++a) j[kKeys[pick (sizeof kKeys / sizeof kKeys[0])]] = value();
        std::string reply;
        try { reply = cmd (j); } catch (...) { ++badReply; std::printf ("    excepción: %s\n", j.dump().substr (0, 200).c_str()); continue; }
        const auto r = json::parse (reply, nullptr, false);
        const bool stateLike = j["cmd"] == "state" || j["cmd"] == "catalog"; // devuelven el estado, sin "ok"
        if (r.is_discarded() || ! r.is_object() || (! stateLike && ! r.contains ("ok")))
        {
            if (badReply++ < 5) std::printf ("    respuesta inválida a %s: %s\n", j.dump().substr (0, 160).c_str(), reply.substr (0, 160).c_str());
            continue;
        }
        if (! stateLike) (r["ok"] == true ? ok : rejected)++;
    }
    // También texto que no es JSON.
    for (const char* raw : { "", "{", "[]", "null", "{\"cmd\":5}", "{\"cmd\":\"setTempo\",\"bpm\":\"rápido\"}", "\xff\xfe" })
    {
        const auto r = json::parse (handleCommand (s, engine, raw), nullptr, false);
        if (r.is_discarded() || ! r.contains ("ok") || r["ok"] != false) { ++badReply; std::printf ("    texto crudo mal respondido: %s\n", raw); }
    }
    stop.store (true);
    audio.join();
    engine.stop();

    MEASURE ("%d comandos al azar con el audio sonando: %d aceptados, %d rechazados con error, %d respuestas inválidas",
             iterations, ok, rejected, badReply);
    CHECK (badReply == 0);
    CHECK (finite.load());
    CHECK (ok > 100 && rejected > 100); // el fuzzing ejercita ambos caminos
    const auto m = s.model();
    CHECK_MSG (validate (m).empty(), validate (m));
    CHECK (s.save().ok);
    const std::string before = toJson (s.model(), false);
    s.close();
    CHECK (s.openProject (dir + "/p").ok);
    CHECK (toJson (s.model(), false) == before);
}

TEST (fuzz_damaged_project_json_is_rejected_or_opened_safely)
{
    const std::string dir = cstest::tempDir ("fuzz_json");
    Engine engine;
    engine.prepare (48000, 256);
    Session s (engine);
    CHECK (s.createProject (dir + "/base", "base", 48000, "reggaeton").ok);
    CHECK (s.save().ok);
    s.close();
    std::string original;
    CHECK (readFile (dir + "/base/project.json", original));

    std::mt19937 rng (77);
    const int iterations = envInt ("CS_FUZZ_JSON", 400);
    int opened = 0, refused = 0;
    for (int i = 0; i < iterations; ++i)
    {
        std::string text = original;
        const int edits = 1 + (int) (rng() % 4);
        for (int e = 0; e < edits && ! text.empty(); ++e)
        {
            const size_t at = rng() % text.size();
            switch (rng() % 5)
            {
                case 0: text[at] = (char) (rng() % 256); break;                              // byte cambiado
                case 1: text.erase (at, std::min<size_t> (text.size() - at, rng() % 40)); break; // recorte
                case 2: text.insert (at, text.substr (at, std::min<size_t> (text.size() - at, rng() % 60))); break;
                case 3: { const char* d = "0123456789-"; text[at] = d[rng() % 11]; break; } // números raros
                default: text = text.substr (0, at); break;                                 // archivo truncado
            }
        }
        const std::string p = dir + "/m" + std::to_string (i);
        fs::create_directories (p + "/audio");
        std::ofstream (p + "/project.json", std::ios::binary) << text;
        LoadReport rep;
        const auto r = s.openProject (p, &rep);
        if (r.ok)
        {
            ++opened;
            const auto m = s.model();
            CHECK_MSG (validate (m).empty(), "abierto pero inválido en la iteración " + std::to_string (i));
            std::vector<float> out (256 * 2);
            engine.play();
            for (int b = 0; b < 20; ++b) engine.process (nullptr, 0, out.data(), 2, 256);
            engine.stop();
            for (float x : out) CHECK (std::isfinite (x));
            s.close();
        }
        else ++refused;
        fs::remove_all (p);
    }
    MEASURE ("%d project.json dañados: %d abiertos (válidos y reproducibles), %d rechazados con error", iterations, opened, refused);
    CHECK (opened + refused == iterations);
}
