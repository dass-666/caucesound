// cauce: herramienta de línea de órdenes de escritorio (Windows, macOS, Linux). Misión 0.6.
//
// Es la primera pieza real de CauceSound Studio y de Cauce Link: demuestra que el motor, el modelo de proyecto y los
// instrumentos de CAUCESOUND funcionan en un ordenador sin Android y sin interfaz gráfica. Abre un proyecto
// .caucesound (el mismo archivo que exporta el teléfono) y lo renderiza con el mismo código que suena en el móvil.
// No es un DAW: no reproduce en directo ni tiene ventanas.
//
//   cauce info    <proyecto>                      resumen del proyecto (pistas, tempo, duración)
//   cauce render  <proyecto> <mezcla.wav> [--bits 16|24|32]
//   cauce daw     <proyecto> <paquete.zip> [--dry] [--bits 16|24|32]   paquete para DAW (stems + MIDI + manifiesto)
//   cauce midi    <proyecto> <cancion.mid>
//   cauce verify  <paquete.zip>                   comprueba el paquete contra los hashes de su manifiesto
//   cauce new     <plantilla> <salida.caucesound> [--solo]   crea una canción de ejemplo (para pruebas)
//
// <proyecto> es un archivo .caucesound o una carpeta de proyecto. El proyecto de origen nunca se modifica: un
// .caucesound se extrae a una carpeta temporal que se borra al terminar.
#include "cs/Api.h"
#include "cs/Archive.h"
#include "cs/Hash.h"
#include "cs/Session.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace cs;
using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
struct TempDir
{
    std::string path;
    TempDir()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = (fs::temp_directory_path() / ("cauce-" + std::to_string (stamp))).string();
        fs::create_directories (path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all (path, ec); }
};

int die (const std::string& msg) { std::fprintf (stderr, "error: %s\n", msg.c_str()); return 1; }

std::string absolute (const std::string& p) { return fs::absolute (fs::path (p)).lexically_normal().string(); }

struct Opened
{
    Engine engine;
    Session session { engine };
    TempDir tmp;
    std::string error;
    json cmd (const json& q) { return json::parse (handleCommand (session, engine, q.dump())); }

    bool open (const std::string& input)
    {
        engine.prepare (48000, 1024);
        std::string folder = absolute (input);
        if (! fs::is_directory (folder))
        {
            const ArchiveResult ar = importProjectArchive (folder, tmp.path + "/project");
            if (! ar.ok) { error = "no se pudo abrir " + input + ": " + ar.error; return false; }
            folder = tmp.path + "/project";
        }
        else
        {
            // Copia de trabajo: abrir un proyecto puede reparar o limpiar archivos; el original no se toca.
            std::error_code ec;
            fs::copy (folder, tmp.path + "/project", fs::copy_options::recursive, ec);
            if (ec) { error = "no se pudo copiar el proyecto: " + ec.message(); return false; }
            folder = tmp.path + "/project";
        }
        const OpResult r = session.openProject (folder);
        if (! r.ok) { error = r.error; return false; }
        return true;
    }

    json wait (const std::string& jobId, const char* what)
    {
        int last = -1;
        for (;;)
        {
            const json j = cmd ({ { "cmd", "jobStatus" }, { "jobId", jobId } });
            const std::string st = j.value ("state", std::string ("failed"));
            const int pct = (int) (j.value ("progress", 0.0) * 100.0);
            if (pct != last) { std::fprintf (stderr, "\r%s: %3d %%", what, pct); std::fflush (stderr); last = pct; }
            if (st == "done" || st == "failed" || st == "cancelled") { std::fprintf (stderr, "\n"); return j; }
            std::this_thread::sleep_for (std::chrono::milliseconds (50));
        }
    }
};

int optInt (const std::vector<std::string>& a, const char* name, int fallback)
{
    for (size_t i = 0; i + 1 < a.size(); ++i) if (a[i] == name) return std::atoi (a[i + 1].c_str());
    return fallback;
}
bool flag (const std::vector<std::string>& a, const char* name)
{
    for (const auto& s : a) if (s == name) return true;
    return false;
}

int info (const std::string& input)
{
    Opened o;
    if (! o.open (input)) return die (o.error);
    const ProjectModel m = o.session.model();
    std::printf ("Canción: %s\nTempo: %.2f BPM   Compás: %d/4   Frecuencia: %d Hz   Formato de proyecto: %d\nDuración del contenido: %.2f s\n",
                 m.name.c_str(), m.bpm, m.beatsPerBar, m.sampleRate, m.version, (double) m.contentEnd() / m.sampleRate);
    std::printf ("Pistas (%zu):\n", m.tracks.size());
    for (const auto& t : m.tracks)
    {
        int notes = 0;
        for (const auto& c : t.midiClips) notes += (int) c.notes.size();
        std::printf ("  %-8s %-24s vol %6.1f dB  pan %+.2f%s%s  clips audio %zu, clips MIDI %zu (%d notas), efectos %zu\n", t.kind.c_str(), t.name.c_str(),
                     t.volumeDb, t.pan, t.mute ? "  [silenciada]" : "", t.solo ? "  [solo]" : "", t.clips.size(), t.midiClips.size(), notes, t.inserts.size());
    }
    return 0;
}

int render (const std::string& input, const std::string& out, int bits)
{
    Opened o;
    if (! o.open (input)) return die (o.error);
    const auto t0 = std::chrono::steady_clock::now();
    long clipped = 0;
    const OpResult r = o.session.exportMix (absolute (out), bits, 0, &clipped);
    if (! r.ok) return die (r.error);
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    long long size = 0;
    const std::string h = sha256File (absolute (out), &size);
    std::printf ("Mezcla: %s (%lld bytes, %d bits, %ld muestras recortadas) en %.2f s\nSHA-256: %s\n", out.c_str(), size, bits, clipped, secs, h.c_str());
    return 0;
}

int daw (const std::string& input, const std::string& out, bool dry, int bits)
{
    Opened o;
    if (! o.open (input)) return die (o.error);
    const json started = o.cmd ({ { "cmd", "exportDaw" }, { "path", absolute (out) }, { "mode", dry ? "dry" : "wet" }, { "bits", bits } });
    if (! started.value ("ok", false)) return die (started.value ("error", std::string ("no se pudo exportar")));
    const json done = o.wait (started.value ("jobId", std::string()), "Exportando para DAW");
    if (done.value ("state", std::string()) != "done") return die (done.value ("error", std::string ("la exportación no terminó")));
    const json r = done["result"];
    std::printf ("Paquete: %s\n  %d archivos, %.1f MB, %d stems, %d pistas MIDI (%d notas), modo %s, %ld muestras recortadas\n  %.2f s de trabajo para %.1f s de canción\nSHA-256: %s\n",
                 out.c_str(), r.value ("files", 0), r.value ("bytes", 0LL) / 1048576.0, r.value ("stems", 0), r.value ("midiTracks", 0), r.value ("midiNotes", 0),
                 r.value ("mode", std::string()).c_str(), r.value ("clippedSamples", 0L), r.value ("seconds", 0.0), r.value ("songSeconds", 0.0),
                 r.value ("sha256", std::string()).c_str());
    return 0;
}

int midi (const std::string& input, const std::string& out)
{
    Opened o;
    if (! o.open (input)) return die (o.error);
    const json r = o.cmd ({ { "cmd", "exportMidi" }, { "path", absolute (out) } });
    if (! r.value ("ok", false)) return die (r.value ("error", std::string ("no se pudo exportar el MIDI")));
    std::printf ("MIDI: %s (%d notas)\n", out.c_str(), r.value ("notes", 0));
    return 0;
}

// Comprueba un paquete para DAW: estructura del ZIP (CRC), y cada recurso contra el hash y el tamaño del manifiesto.
int verify (const std::string& zip)
{
    std::vector<ZipInfo> entries;
    std::string err;
    if (! listStoredZip (absolute (zip), entries, err)) return die ("el ZIP no es válido: " + err);
    json manifest;
    for (const auto& e : entries)
        if (e.name == "manifest.json")
        {
            std::string b;
            if (! readStoredZipEntry (absolute (zip), e, b)) return die ("no se pudo leer manifest.json");
            manifest = json::parse (b, nullptr, false);
        }
    if (! manifest.is_object() || manifest.value ("format", std::string()) != "cauce-daw") return die ("no es un paquete cauce-daw (falta manifest.json)");
    int ok = 0, bad = 0;
    auto check = [&] (const json& r)
    {
        if (! r.is_object()) return;
        const std::string file = r.value ("file", std::string());
        for (const auto& e : entries)
        {
            if (e.name != file) continue;
            std::string b;
            const bool good = readStoredZipEntry (absolute (zip), e, b) && sha256Hex (b) == r.value ("sha256", std::string()) && (long long) b.size() == r.value ("bytes", -1LL);
            std::printf ("  %s  %s\n", good ? "OK   " : "FALLO", file.c_str());
            good ? ++ok : ++bad;
            return;
        }
        std::printf ("  FALTA  %s\n", file.c_str());
        ++bad;
    };
    for (const auto& s : manifest["stems"]) check (s);
    check (manifest["mix"]); check (manifest["midi"]); check (manifest["project"]);
    const json song = manifest["song"];
    std::printf ("%s: %s, %.2f BPM, %d Hz, %.2f s, modo %s. %d recursos correctos, %d con problemas.\n", zip.c_str(), song.value ("name", std::string()).c_str(),
                 song.value ("bpm", 0.0), song.value ("sampleRate", 0), song.value ("seconds", 0.0), manifest.value ("mode", std::string()).c_str(), ok, bad);
    return bad == 0 ? 0 : 2;
}

int create (const std::string& templ, const std::string& out, bool solo)
{
    Engine engine;
    Session session (engine);
    engine.prepare (48000, 1024);
    TempDir tmp;
    const OpResult c = session.createProject (tmp.path + "/p", "Ejemplo " + templ, 48000, templ);
    if (! c.ok) return die (c.error);
    if (solo)
    {
        const json r = json::parse (handleCommand (session, engine, json { { "cmd", "soloMelody" }, { "startBar", 1 }, { "bars", 8 }, { "seed", 7 }, { "proposal", 0 } }.dump()));
        if (! r.value ("ok", false)) return die (r.value ("error", std::string ("Solo AI no disponible")));
    }
    if (! session.save().ok) return die ("no se pudo guardar");
    session.close();
    const ArchiveResult ar = exportProjectArchive (tmp.path + "/p", absolute (out));
    if (! ar.ok) return die (ar.error);
    std::printf ("Proyecto: %s (%d archivos, %lld bytes)\n", out.c_str(), ar.files, ar.bytes);
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    std::vector<std::string> a (argv + 1, argv + argc);
    const std::string c = a.empty() ? "" : a[0];
    if (c == "info" && a.size() >= 2) return info (a[1]);
    if (c == "render" && a.size() >= 3) return render (a[1], a[2], optInt (a, "--bits", 24));
    if (c == "daw" && a.size() >= 3) return daw (a[1], a[2], flag (a, "--dry"), optInt (a, "--bits", 24));
    if (c == "midi" && a.size() >= 3) return midi (a[1], a[2]);
    if (c == "verify" && a.size() >= 2) return verify (a[1]);
    if (c == "new" && a.size() >= 3) return create (a[1], a[2], flag (a, "--solo"));
    std::fprintf (stderr,
        "cauce - CAUCESOUND en el escritorio (sin interfaz)\n\n"
        "  cauce info    <proyecto>\n"
        "  cauce render  <proyecto> <mezcla.wav> [--bits 16|24|32]\n"
        "  cauce daw     <proyecto> <paquete.zip> [--dry] [--bits 16|24|32]\n"
        "  cauce midi    <proyecto> <cancion.mid>\n"
        "  cauce verify  <paquete.zip>\n"
        "  cauce new     <plantilla> <salida.caucesound> [--solo]\n\n"
        "<proyecto> es un archivo .caucesound o una carpeta de proyecto. El original no se modifica.\n");
    return c.empty() ? 0 : 1;
}
