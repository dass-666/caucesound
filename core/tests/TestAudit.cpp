// Auditoría de la 0.10 (versión 0.10.1): pruebas de los defectos encontrados. Cada una se escribió primero para
// reproducir el defecto con el código de la 0.10 y después se comprobó que pasa con la corrección.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Session.h"
#include "cs/Utf.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
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
    explicit Rig (const std::string& name) { dir = cstest::tempDir (name); engine.prepare (48000, 1024); }
    json operator() (json q) { return json::parse (handleCommand (session, engine, q.dump())); }
    std::string create (const std::string& sub, const std::string& tpl)
    {
        const std::string f = dir + "/" + sub;
        const json r = (*this) ({ { "cmd", "createProject" }, { "folder", f }, { "name", sub }, { "template", tpl } });
        CHECK_MSG (r["ok"].get<bool>(), r.dump());
        return f;
    }
};

// WAV PCM de 8 bits mono a 8 kHz con 'seconds' de silencio: el archivo más pequeño posible para una duración dada.
std::string writeLongWav (const std::string& path, int seconds)
{
    const uint32_t rate = 8000, data = rate * (uint32_t) seconds;
    std::FILE* f = std::fopen (path.c_str(), "wb");
    auto w32 = [&] (uint32_t v) { std::fwrite (&v, 4, 1, f); };
    auto w16 = [&] (uint16_t v) { std::fwrite (&v, 2, 1, f); };
    std::fwrite ("RIFF", 1, 4, f); w32 (36 + data); std::fwrite ("WAVEfmt ", 1, 8, f); w32 (16);
    w16 (1); w16 (1); w32 (rate); w32 (rate); w16 (1); w16 (8);
    std::fwrite ("data", 1, 4, f); w32 (data);
    std::vector<uint8_t> chunk (rate, 128);
    for (int s = 0; s < seconds; ++s) std::fwrite (chunk.data(), 1, chunk.size(), f);
    std::fclose (f);
    return path;
}
} // namespace

// La interfaz lee los medidores una vez por fotograma (~16,7 ms) y el audio procesa cada ~4 ms. Con «pico del último
// bloque» se perdían 3 de cada 4 bloques: el medidor marcaba de menos y el indicador de saturación podía no encenderse.
TEST (audit_meters_hold_the_peak_between_reads)
{
    Rig r ("audit_meter");
    r.create ("A", "trap");
    r ({ { "cmd", "seek" }, { "frame", 4 * 48000 } });
    r ({ { "cmd", "play" } });
    const int tracks = (int) r.session.model().tracks.size();
    std::vector<float> out (192 * 2);
    float truePeak = 0;
    int reads = 0, lastBlockLow = 0, heldLow = 0, trackReads = 0;
    for (int b = 0; b < 2000; ++b)
    {
        r.engine.process (nullptr, 0, out.data(), 2, 192);
        for (float v : out) truePeak = std::max (truePeak, std::fabs (v));
        if (b % 4 != 3) continue;
        const float last = r.engine.masterPeak(), held = r.engine.takeMasterPeak();
        if (last < truePeak * 0.5f) ++lastBlockLow;       // lo que veía la interfaz de la 0.10
        if (std::fabs (held - truePeak) > 1e-6f) ++heldLow; // ahora: exactamente el pico real del intervalo
        for (int t = 0; t < tracks; ++t) if (r.engine.takeTrackPeak (t) > 0.0f) ++trackReads;
        truePeak = 0;
        ++reads;
    }
    MEASURE ("%d lecturas (1 de cada 4 bloques): el último bloque marcaba menos de la mitad del pico real en %d; el pico retenido falla en %d",
             reads, lastBlockLow, heldLow);
    CHECK (heldLow == 0);
    CHECK (lastBlockLow > 0); // el defecto existía: si esto deja de cumplirse la prueba ya no demuestra nada
    CHECK (trackReads > reads); // los medidores por pista también retienen
    // Leer pone el medidor a cero: sin audio nuevo no queda un valor antiguo «pegado».
    CHECK (r.engine.takeMasterPeak() == 0.0f);
}

// Un clip arrastrado por error muy lejos hacía que «Exportar mezcla» renderizara la canción entera en memoria: con un
// clip a 45 minutos, 1,3 GB y 22 s con el motor bloqueado (medido en la 0.10). Ahora falla al instante con un mensaje.
TEST (audit_mix_export_refuses_absurd_length_before_allocating)
{
    Rig r ("audit_far");
    r.create ("A", "trap");
    std::string clip;
    const ProjectModel pm = r.session.model();
    for (const auto& t : pm.tracks) if (! t.midiClips.empty()) { clip = t.midiClips[0].id; break; }
    CHECK (! clip.empty());
    const std::string out = r.dir + "/far.wav";
    json ex = r ({ { "cmd", "export" }, { "path", out }, { "bits", 16 } });
    CHECK_MSG (ex["ok"].get<bool>(), ex.dump()); // la canción normal se exporta igual que antes
    std::filesystem::remove (out);

    const double bpm = pm.bpm, ticksPerSecond = kPpq * bpm / 60.0;
    CHECK (r ({ { "cmd", "moveMidiClip" }, { "clipId", clip }, { "start", std::floor (ticksPerSecond * 60 * 45) } })["ok"].get<bool>());
    cstest::allocationCount = 0;
    cstest::countAllocations = true;
    ex = r ({ { "cmd", "export" }, { "path", out }, { "bits", 16 } });
    cstest::countAllocations = false;
    MEASURE ("exportar con un clip a 45 min: %s", ex.dump().c_str());
    CHECK (! ex["ok"].get<bool>());
    CHECK (ex["error"].get<std::string>().find ("20 minutos") != std::string::npos);
    CHECK (! std::filesystem::exists (out));
    // Deshacer devuelve el clip a su sitio y la exportación vuelve a funcionar: el proyecto no se tocó.
    CHECK (r ({ { "cmd", "undo" } })["ok"].get<bool>());
    CHECK (r ({ { "cmd", "export" }, { "path", out }, { "bits", 16 } })["ok"].get<bool>());
}

// Importar un audio muy largo lo cargaba entero (archivo + muestras en float + remuestreo). Ahora se mira la cabecera.
TEST (audit_import_refuses_very_long_audio_from_the_header)
{
    Rig r ("audit_import");
    r.create ("A", "empty");
    const std::string track = r ({ { "cmd", "addTrack" }, { "name", "Voz" } })["id"].get<std::string>();
    const std::string longWav = writeLongWav (r.dir + "/largo.wav", 21 * 60);
    WavInfo info;
    std::string err;
    CHECK (probeWav (longWav, info, err));
    CHECK (info.sampleRate == 8000 && info.channels == 1 && info.frames == (Frame) 8000 * 21 * 60);

    cstest::allocationCount = 0;
    const json bad = r ({ { "cmd", "importWav" }, { "path", longWav }, { "trackId", track }, { "at", 0 } });
    MEASURE ("importar 21 min: %s", bad.dump().c_str());
    CHECK (! bad["ok"].get<bool>());
    CHECK (bad["error"].get<std::string>().find ("20 minutos") != std::string::npos);
    CHECK (r.session.model().sources.empty());
    CHECK (! r.session.canUndo() || r.session.model().tracks[0].clips.empty());

    // Un audio normal entra igual que antes, y la duración de la cabecera coincide con la real.
    const std::string okWav = writeLongWav (r.dir + "/corto.wav", 3);
    CHECK (probeWav (okWav, info, err) && info.frames == 24000);
    const json good = r ({ { "cmd", "importWav" }, { "path", okWav }, { "trackId", track }, { "at", 0 } });
    CHECK_MSG (good["ok"].get<bool>(), good.dump());
    CHECK (r.session.model().sources.size() == 1);
    CHECK (r.session.model().sources[0].frames == 3 * 48000);

    // La cabecera de algo que no es WAV se rechaza sin excepciones.
    std::FILE* f = std::fopen ((r.dir + "/no.wav").c_str(), "wb");
    std::fwrite ("esto no es audio", 1, 16, f);
    std::fclose (f);
    CHECK (! probeWav (r.dir + "/no.wav", info, err));
    CHECK (! probeWav (r.dir + "/no-existe.wav", info, err));
}

// El puente JNI usaba «UTF-8 modificado»: un emoji llegaba como par subrogado en dos secuencias de 3 bytes y el motor
// rechazaba la petición entera. La conversión propia desde y hacia UTF-16 da UTF-8 estricto en los dos sentidos.
TEST (audit_utf16_bridge_round_trips_emoji_and_survives_garbage)
{
    Rig r ("audit_utf");
    // Así llegaba «Beat 🔥» con GetStringUTFChars: sigue siendo una petición no válida (el motor exige UTF-8 real).
    const std::string modified = std::string ("{\"cmd\":\"createProject\",\"folder\":\"") + r.dir + "/M\",\"name\":\"Beat \xED\xA0\xBD\xED\xB4\xA5\",\"template\":\"empty\"}";
    CHECK (! json::parse (handleCommand (r.session, r.engine, modified))["ok"].get<bool>());

    // Lo mismo desde UTF-16, como lo hace ahora el puente.
    const std::u16string name16 = u"Beat \U0001F525 canción 日本";
    const std::string name8 = utf16ToUtf8 (name16.data(), name16.size());
    CHECK (name8 == "Beat \xF0\x9F\x94\xA5 canci\xC3\xB3n \xE6\x97\xA5\xE6\x9C\xAC");
    const json made = r ({ { "cmd", "createProject" }, { "folder", r.dir + "/E" }, { "name", name8 }, { "template", "empty" } });
    CHECK_MSG (made["ok"].get<bool>(), made.dump());
    const std::string state = handleCommand (r.session, r.engine, "{\"cmd\":\"state\"}");
    const std::u16string back = utf8ToUtf16 (state);
    CHECK (back.find (name16) != std::u16string::npos);       // el nombre vuelve intacto a la interfaz
    CHECK (utf16ToUtf8 (back.data(), back.size()) == state);  // ida y vuelta exacta

    // Entradas rotas: nunca fallan ni producen UTF-8 / UTF-16 inválido; lo ilegible pasa a U+FFFD.
    const char16_t lone[] = { u'a', 0xD83D, u'b', 0xDD25, u'c', 0xD83D };
    const std::string fixed = utf16ToUtf8 (lone, 6);
    CHECK (fixed == "a\xEF\xBF\xBD" "b\xEF\xBF\xBD" "c\xEF\xBF\xBD");
    CHECK (! json::parse ("\"" + fixed + "\"", nullptr, false).is_discarded());
    const std::string broken = std::string ("ok \xC0\x80 \xED\xA0\xBD \xF8\x88 \xE2\x82 fin\xF0\x9F", 22);
    const std::u16string u = utf8ToUtf16 (broken);
    for (size_t i = 0; i < u.size(); ++i)
    {
        const bool hi = u[i] >= 0xD800 && u[i] <= 0xDBFF, lo = u[i] >= 0xDC00 && u[i] <= 0xDFFF;
        CHECK (! lo);
        if (hi) { CHECK (i + 1 < u.size() && u[i + 1] >= 0xDC00 && u[i + 1] <= 0xDFFF); ++i; }
    }
    CHECK (u.find (u"ok ") == 0 && u.find (u" fin") != std::u16string::npos);
    CHECK (utf8ToUtf16 ("").empty() && utf16ToUtf8 (nullptr, 0).empty());
}
