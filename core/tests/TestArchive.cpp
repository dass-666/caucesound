// Exportar/importar proyectos completos (.caucesound = ZIP sin compresión): ida y vuelta exacta y rechazo seguro de
// archivos dañados o malintencionados (rutas fuera de la carpeta, comprimidos, CRC incorrecto, truncados, sin audio).
#include "Test.h"

#include "cs/Api.h"
#include "cs/Archive.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace cs;
using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
std::string slurp (const std::string& p) { std::string s; readFile (p, s); return s; }
void spit (const std::string& p, const std::string& s) { std::ofstream (p, std::ios::binary) << s; }

uint32_t crc32 (const std::string& d)
{
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b : d) { c ^= b; for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1; }
    return ~c;
}

// ZIP de prueba con entradas arbitrarias (para construir archivos malintencionados).
struct ZipEntry { std::string name, data; uint16_t method = 0; bool badCrc = false; };
void writeZip (const std::string& path, const std::vector<ZipEntry>& es)
{
    std::string out, cd;
    auto p16 = [] (std::string& b, uint32_t v) { b += (char) (v & 0xFF); b += (char) ((v >> 8) & 0xFF); };
    auto p32 = [&] (std::string& b, uint32_t v) { p16 (b, v & 0xFFFF); p16 (b, v >> 16); };
    for (const auto& e : es)
    {
        const uint32_t crc = crc32 (e.data) ^ (e.badCrc ? 1u : 0u), off = (uint32_t) out.size();
        p32 (out, 0x04034b50); p16 (out, 20); p16 (out, 0); p16 (out, e.method); p16 (out, 0); p16 (out, 0);
        p32 (out, crc); p32 (out, (uint32_t) e.data.size()); p32 (out, (uint32_t) e.data.size()); p16 (out, (uint32_t) e.name.size()); p16 (out, 0);
        out += e.name + e.data;
        p32 (cd, 0x02014b50); p16 (cd, 20); p16 (cd, 20); p16 (cd, 0); p16 (cd, e.method); p16 (cd, 0); p16 (cd, 0);
        p32 (cd, crc); p32 (cd, (uint32_t) e.data.size()); p32 (cd, (uint32_t) e.data.size()); p16 (cd, (uint32_t) e.name.size());
        p16 (cd, 0); p16 (cd, 0); p16 (cd, 0); p16 (cd, 0); p32 (cd, 0); p32 (cd, off);
        cd += e.name;
    }
    const uint32_t cdOff = (uint32_t) out.size();
    out += cd;
    p32 (out, 0x06054b50); p16 (out, 0); p16 (out, 0); p16 (out, (uint32_t) es.size()); p16 (out, (uint32_t) es.size());
    p32 (out, (uint32_t) cd.size()); p32 (out, cdOff); p16 (out, 0);
    spit (path, out);
}

bool sameAudio (const std::string& a, const std::string& b)
{
    AudioData x, y;
    std::string err;
    if (! readWav (a, x, err) || ! readWav (b, y, err)) return false;
    return x.channels == y.channels && x.numFrames() > 0;
}
} // namespace

TEST (project_archive_roundtrip_is_exact_and_gets_a_new_id)
{
    const std::string dir = cstest::tempDir ("archive_rt");
    Engine engine;
    engine.prepare (48000, 1024);
    Session s (engine);
    CHECK (s.createProject (dir + "/projects/a", "Mi canción", 48000, "reggaeton").ok);
    // Audio importado (como una toma de voz) en la pista de voz.
    AudioData wav;
    wav.sampleRate = 48000;
    wav.allocate (1, 48000 * 3);
    for (Frame i = 0; i < wav.numFrames(); ++i) wav.channels[0][(size_t) i] = 0.3f * (float) std::sin (0.05 * (double) i);
    std::string err;
    CHECK (writeWav (dir + "/voz.wav", wav, WavFormat::Pcm24, err));
    std::string audioTrack;
    for (const auto& t : s.model().tracks) if (t.kind == "audio") audioTrack = t.id;
    CHECK (! audioTrack.empty());
    CHECK (s.importWav (dir + "/voz.wav", audioTrack, 48000).ok);
    CHECK (s.addInsert (audioTrack, "cs.reverb").ok);
    CHECK (s.save().ok);
    CHECK (s.exportMix (dir + "/before.wav", 32).ok);
    const auto original = s.model();
    s.close();

    const auto ex = exportProjectArchive (dir + "/projects/a", dir + "/copia.caucesound");
    CHECK_MSG (ex.ok, ex.error);
    CHECK (ex.files == 2); // project.json + 1 WAV
    CHECK (! fs::exists (dir + "/copia.caucesound.part"));
    MEASURE ("exportado: %d archivos, %lld bytes de datos, archivo de %lld bytes", ex.files, ex.bytes, (long long) fs::file_size (dir + "/copia.caucesound"));

    const auto im = importProjectArchive (dir + "/copia.caucesound", dir + "/projects/b");
    CHECK_MSG (im.ok, im.error);
    CHECK (im.name == "Mi canción");
    CHECK (s.openProject (dir + "/projects/b").ok);
    auto copy = s.model();
    CHECK (copy.id != original.id && ! copy.id.empty()); // id nuevo: puede convivir con el original
    copy.id = original.id;
    copy.modifiedAt = original.modifiedAt;
    CHECK (toJson (copy, false) == toJson (original, false));
    CHECK (s.exportMix (dir + "/after.wav", 32).ok);
    CHECK (sameAudio (dir + "/before.wav", dir + "/after.wav")); // suena idéntico
    s.close();

    // La carpeta de destino existente no se toca.
    const auto again = importProjectArchive (dir + "/copia.caucesound", dir + "/projects/b");
    CHECK (! again.ok);

    // Mediante la API de comandos (la que usa la app), con el proyecto abierto (se guarda antes de exportar).
    CHECK (s.openProject (dir + "/projects/a").ok);
    const auto r1 = json::parse (handleCommand (s, engine, json { { "cmd", "exportProjectArchive" }, { "root", dir + "/projects" },
        { "folder", dir + "/projects/a" }, { "out", dir + "/api.caucesound" } }.dump()));
    CHECK (r1["ok"] == true && r1["files"] == 2);
    const auto r2 = json::parse (handleCommand (s, engine, json { { "cmd", "importProjectArchive" }, { "root", dir + "/projects" },
        { "archive", dir + "/api.caucesound" }, { "dest", dir + "/projects/c" } }.dump()));
    CHECK (r2["ok"] == true);
    const auto r3 = json::parse (handleCommand (s, engine, json { { "cmd", "importProjectArchive" }, { "root", dir + "/projects" },
        { "archive", dir + "/api.caucesound" }, { "dest", dir + "/fuera" } }.dump()));
    CHECK (r3["ok"] == false); // destino fuera de la carpeta de proyectos
    CHECK (! fs::exists (dir + "/fuera"));
}

TEST (project_archive_rejects_malicious_or_damaged_files_and_leaves_nothing)
{
    const std::string dir = cstest::tempDir ("archive_bad");
    Engine engine;
    engine.prepare (48000, 1024);
    Session s (engine);
    CHECK (s.createProject (dir + "/src", "x", 48000, "trap").ok);
    CHECK (s.save().ok);
    s.close();
    const std::string pj = slurp (dir + "/src/project.json");
    CHECK (! pj.empty());
    // Proyecto que referencia un audio que no viene en el archivo.
    auto j = json::parse (pj);
    j["sources"] = json::array ({ { { "id", "src_x" }, { "file", "audio/src_x.wav" }, { "origin", "rec" }, { "channels", 1 }, { "frames", 10 } } });
    const std::string pjMissing = j.dump();

    struct Case { const char* what; std::vector<ZipEntry> entries; };
    std::vector<Case> cases {
        { "ruta con ..", { { "project.json", pj }, { "audio/../../evil.wav", "x" } } },
        { "ruta absoluta", { { "project.json", pj }, { "/tmp/evil.wav", "x" } } },
        { "archivo no permitido", { { "project.json", pj }, { "audio/run.sh", "x" } } },
        { "comprimido (deflate)", { { "project.json", pj, 8 } } },
        { "CRC incorrecto", { { "project.json", pj, 0, true } } },
        { "sin project.json", { { "audio/a.wav", "x" } } },
        { "project.json inválido", { { "project.json", "{no es json" } } },
        { "falta el audio", { { "project.json", pjMissing } } },
        { "entrada duplicada", { { "project.json", pj }, { "project.json", pj } } },
        // Un project.json enorme agotaría la memoria al leerlo: se rechaza por tamaño antes de extraer nada.
        { "project.json de 65 MB", { { "project.json", std::string ((size_t) 65 << 20, ' ') } } },
    };
    int n = 0;
    for (const auto& c : cases)
    {
        const std::string zip = dir + "/bad" + std::to_string (n) + ".caucesound", dest = dir + "/out" + std::to_string (n);
        writeZip (zip, c.entries);
        const auto r = importProjectArchive (zip, dest);
        CHECK_MSG (! r.ok, c.what);
        CHECK_MSG (! fs::exists (dest) && ! fs::exists (dest + ".importing"), std::string ("restos tras: ") + c.what);
        ++n;
    }
    CHECK (! fs::exists (dir + "/evil.wav") && ! fs::exists ("/tmp/evil.wav"));
    // Archivo truncado y basura.
    const auto ok = exportProjectArchive (dir + "/src", dir + "/good.caucesound");
    CHECK (ok.ok);
    std::string full = slurp (dir + "/good.caucesound");
    spit (dir + "/trunc.caucesound", full.substr (0, full.size() / 2));
    CHECK (! importProjectArchive (dir + "/trunc.caucesound", dir + "/t").ok && ! fs::exists (dir + "/t"));
    spit (dir + "/junk.caucesound", std::string (5000, 'z'));
    CHECK (! importProjectArchive (dir + "/junk.caucesound", dir + "/j").ok && ! fs::exists (dir + "/j"));
    CHECK (! importProjectArchive (dir + "/no-existe.caucesound", dir + "/k").ok);
    // Un byte cambiado dentro de los datos: lo detecta el CRC.
    std::string flipped = full;
    flipped[60] = (char) (flipped[60] ^ 0x20);
    spit (dir + "/flip.caucesound", flipped);
    CHECK (! importProjectArchive (dir + "/flip.caucesound", dir + "/f").ok && ! fs::exists (dir + "/f"));
    // El archivo bueno sí se importa.
    CHECK (importProjectArchive (dir + "/good.caucesound", dir + "/g").ok);
    MEASURE ("%d archivos malintencionados o dañados rechazados sin dejar restos; el válido se importa", n + 4);
}
