#include "cs/Project.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <random>
#include <set>

#if defined(_WIN32)
 #include <io.h>
#else
 #include <fcntl.h>
 #include <unistd.h>
#endif

namespace cs
{
using nlohmann::json;
namespace fs = std::filesystem;

const SourceRef* ProjectModel::findSource (const std::string& sid) const
{
    for (const auto& s : sources) if (s.id == sid) return &s;
    return nullptr;
}
TrackModel* ProjectModel::findTrack (const std::string& tid)
{
    for (auto& t : tracks) if (t.id == tid) return &t;
    return nullptr;
}
const TrackModel* ProjectModel::findTrack (const std::string& tid) const
{
    for (const auto& t : tracks) if (t.id == tid) return &t;
    return nullptr;
}
ClipModel* ProjectModel::findClip (const std::string& cid, TrackModel** owner)
{
    for (auto& t : tracks)
        for (auto& c : t.clips)
            if (c.id == cid) { if (owner) *owner = &t; return &c; }
    return nullptr;
}
Frame ProjectModel::contentEnd() const
{
    Frame e = 0;
    for (const auto& t : tracks) for (const auto& c : t.clips) e = std::max (e, c.start + c.length);
    return e;
}

float dbToGain (float db) { return db <= -144.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }

std::string newId (const char* prefix)
{
    static thread_local std::mt19937_64 rng { std::random_device {}() ^ (uint64_t) std::chrono::steady_clock::now().time_since_epoch().count() };
    static const char* hex = "0123456789abcdef";
    std::string s = prefix;
    s += '_';
    uint64_t v = rng();
    for (int i = 0; i < 12; ++i) { s += hex[v & 15]; v >>= 4; }
    return s;
}

// --- JSON --------------------------------------------------------------------------------------------------------
std::string toJson (const ProjectModel& p, bool pretty)
{
    json j;
    j["format"] = "caucesound-project";
    j["version"] = p.version;
    j["id"] = p.id;
    j["name"] = p.name;
    j["sampleRate"] = p.sampleRate;
    j["bpm"] = p.bpm;
    j["beatsPerBar"] = p.beatsPerBar;
    j["metronome"] = p.metronome;
    j["masterDb"] = p.masterDb;
    j["recordLatencyOffset"] = p.recordLatencyOffset;
    j["createdAt"] = p.createdAt;
    j["modifiedAt"] = p.modifiedAt;
    j["sources"] = json::array();
    for (const auto& s : p.sources)
        j["sources"].push_back ({ { "id", s.id }, { "file", s.file }, { "origin", s.origin }, { "channels", s.channels }, { "frames", s.frames } });
    j["tracks"] = json::array();
    for (const auto& t : p.tracks)
    {
        json jt = { { "id", t.id }, { "name", t.name }, { "volumeDb", t.volumeDb }, { "pan", t.pan }, { "mute", t.mute },
                    { "solo", t.solo }, { "armed", t.armed }, { "color", t.color } };
        jt["clips"] = json::array();
        for (const auto& c : t.clips)
            jt["clips"].push_back ({ { "id", c.id }, { "sourceId", c.sourceId }, { "name", c.name }, { "start", c.start },
                                     { "sourceOffset", c.sourceOffset }, { "length", c.length }, { "fadeIn", c.fadeIn },
                                     { "fadeOut", c.fadeOut }, { "gainDb", c.gainDb } });
        jt["inserts"] = json::array();
        for (const auto& i : t.inserts)
            jt["inserts"].push_back ({ { "id", i.id }, { "type", i.type }, { "state", i.state }, { "bypass", i.bypass } });
        j["tracks"].push_back (jt);
    }
    return j.dump (pretty ? 2 : -1);
}

bool fromJson (const std::string& text, ProjectModel& out, std::string& error)
{
    const json j = json::parse (text, nullptr, false);
    if (j.is_discarded() || ! j.is_object()) { error = "JSON no válido"; return false; }
    try
    {
        if (j.value ("format", std::string()) != "caucesound-project") { error = "no es un proyecto de CAUCESOUND"; return false; }
        ProjectModel p;
        p.version = j.at ("version").get<int>();
        if (p.version > kProjectFormatVersion) { error = "proyecto creado con una versión más nueva de la app"; return false; }
        if (p.version < 1) { error = "versión de proyecto no válida"; return false; }
        p.id = j.at ("id").get<std::string>();
        p.name = j.at ("name").get<std::string>();
        p.sampleRate = j.at ("sampleRate").get<int>();
        p.bpm = j.at ("bpm").get<double>();
        p.beatsPerBar = j.at ("beatsPerBar").get<int>();
        p.metronome = j.value ("metronome", false);
        p.masterDb = j.value ("masterDb", 0.0f);
        p.recordLatencyOffset = j.value ("recordLatencyOffset", (Frame) 0);
        p.createdAt = j.value ("createdAt", (int64_t) 0);
        p.modifiedAt = j.value ("modifiedAt", (int64_t) 0);
        for (const auto& s : j.at ("sources"))
        {
            SourceRef r;
            r.id = s.at ("id").get<std::string>();
            r.file = s.at ("file").get<std::string>();
            r.origin = s.value ("origin", std::string());
            r.channels = s.at ("channels").get<int>();
            r.frames = s.at ("frames").get<Frame>();
            p.sources.push_back (r);
        }
        for (const auto& jt : j.at ("tracks"))
        {
            TrackModel t;
            t.id = jt.at ("id").get<std::string>();
            t.name = jt.value ("name", std::string());
            t.volumeDb = jt.value ("volumeDb", 0.0f);
            t.pan = jt.value ("pan", 0.0f);
            t.mute = jt.value ("mute", false);
            t.solo = jt.value ("solo", false);
            t.armed = jt.value ("armed", false);
            t.color = jt.value ("color", (uint32_t) 0xFF4FC3F7);
            for (const auto& jc : jt.at ("clips"))
            {
                ClipModel c;
                c.id = jc.at ("id").get<std::string>();
                c.sourceId = jc.at ("sourceId").get<std::string>();
                c.name = jc.value ("name", std::string());
                c.start = jc.at ("start").get<Frame>();
                c.sourceOffset = jc.at ("sourceOffset").get<Frame>();
                c.length = jc.at ("length").get<Frame>();
                c.fadeIn = jc.value ("fadeIn", (Frame) 0);
                c.fadeOut = jc.value ("fadeOut", (Frame) 0);
                c.gainDb = jc.value ("gainDb", 0.0f);
                t.clips.push_back (c);
            }
            if (jt.contains ("inserts"))
                for (const auto& ji : jt.at ("inserts"))
                {
                    InsertModel i;
                    i.id = ji.at ("id").get<std::string>();
                    i.type = ji.at ("type").get<std::string>();
                    i.state = ji.value ("state", std::string());
                    i.bypass = ji.value ("bypass", false);
                    t.inserts.push_back (i);
                }
            p.tracks.push_back (t);
        }
        const std::string problem = validate (p);
        if (! problem.empty()) { error = problem; return false; }
        out = std::move (p);
        return true;
    }
    catch (const std::exception& e)
    {
        error = std::string ("campo ausente o de tipo incorrecto: ") + e.what();
        return false;
    }
}

std::string validate (const ProjectModel& p)
{
    if (p.sampleRate < 8000 || p.sampleRate > 192000) return "frecuencia de muestreo fuera de rango";
    if (! std::isfinite (p.bpm) || p.bpm < 20 || p.bpm > 400) return "tempo fuera de rango (20-400 BPM)";
    if (p.beatsPerBar < 1 || p.beatsPerBar > 16) return "compás no válido";
    if (! std::isfinite (p.masterDb) || p.masterDb > 24) return "volumen maestro no válido";
    if (p.tracks.size() > (size_t) 32) return "demasiadas pistas";
    std::set<std::string> ids;
    for (const auto& s : p.sources)
    {
        if (s.id.empty() || ! ids.insert (s.id).second) return "id de fuente duplicado o vacío";
        if (s.file.empty() || s.file.find ("..") != std::string::npos || s.file[0] == '/') return "ruta de fuente no válida";
        if (s.channels < 1 || s.channels > 2 || s.frames < 0) return "fuente con formato no válido";
    }
    for (const auto& t : p.tracks)
    {
        if (t.id.empty() || ! ids.insert (t.id).second) return "id de pista duplicado o vacío";
        if (! std::isfinite (t.volumeDb) || t.volumeDb > 24 || ! std::isfinite (t.pan) || t.pan < -1 || t.pan > 1) return "mezcla de pista fuera de rango";
        for (const auto& c : t.clips)
        {
            if (c.id.empty() || ! ids.insert (c.id).second) return "id de clip duplicado o vacío";
            const SourceRef* s = p.findSource (c.sourceId);
            if (! s) return "clip que referencia una fuente inexistente";
            if (c.start < 0 || c.length <= 0 || c.sourceOffset < 0 || c.sourceOffset + c.length > s->frames) return "clip fuera de los límites de su audio";
            if (c.fadeIn < 0 || c.fadeOut < 0 || c.fadeIn + c.fadeOut > c.length) return "fundidos no válidos";
            if (! std::isfinite (c.gainDb) || c.gainDb > 24) return "ganancia de clip no válida";
        }
        for (const auto& i : t.inserts)
            if (i.id.empty() || ! ids.insert (i.id).second || i.type.empty()) return "inserción no válida";
    }
    return {};
}

// --- Archivos ------------------------------------------------------------------------------------------------------
bool readFile (const std::string& path, std::string& out)
{
    std::FILE* f = std::fopen (path.c_str(), "rb");
    if (! f) return false;
    std::string s;
    char buf[65536];
    size_t n;
    while ((n = std::fread (buf, 1, sizeof buf, f)) > 0) s.append (buf, n);
    const bool ok = ! std::ferror (f);
    std::fclose (f);
    if (ok) out = std::move (s);
    return ok;
}

bool fileExists (const std::string& path) { std::error_code ec; return fs::exists (path, ec); }
bool makeDirs (const std::string& path) { std::error_code ec; fs::create_directories (path, ec); return fs::is_directory (path, ec); }
bool removeFile (const std::string& path) { std::error_code ec; return fs::remove (path, ec); }

std::vector<std::string> listDir (const std::string& dir)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator (dir, ec)) names.push_back (e.path().filename().string());
    return names;
}

static void syncDir (const std::string& dir)
{
#if ! defined(_WIN32)
    const int fd = ::open (dir.c_str(), O_RDONLY);
    if (fd >= 0) { ::fsync (fd); ::close (fd); }
#else
    (void) dir;
#endif
}

bool atomicWriteFile (const std::string& path, const std::string& content, std::string& error, bool keepBackup)
{
    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen (tmp.c_str(), "wb");
    if (! f) { error = "no se pudo crear " + tmp; return false; }
    bool ok = std::fwrite (content.data(), 1, content.size(), f) == content.size();
    ok = std::fflush (f) == 0 && ok;
#if defined(_WIN32)
    _commit (_fileno (f));
#else
    ok = ::fsync (fileno (f)) == 0 && ok;
#endif
    ok = std::fclose (f) == 0 && ok;
    if (! ok) { error = "error de escritura (¿disco lleno?)"; std::remove (tmp.c_str()); return false; }

    std::error_code ec;
    if (keepBackup && fs::exists (path, ec))
    {
        fs::rename (path, path + ".bak", ec);
        if (ec) { error = "no se pudo crear la copia de seguridad"; return false; }
    }
    fs::rename (tmp, path, ec);
    if (ec) { error = "no se pudo reemplazar el archivo"; return false; }
    syncDir (fs::path (path).parent_path().string());
    return true;
}
} // namespace cs
