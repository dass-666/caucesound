#include "cs/Project.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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
MidiClipModel* ProjectModel::findMidiClip (const std::string& cid, TrackModel** owner)
{
    for (auto& t : tracks)
        for (auto& c : t.midiClips)
            if (c.id == cid) { if (owner) *owner = &t; return &c; }
    return nullptr;
}
const DrumPattern* TrackModel::findPattern (const std::string& pid) const
{
    for (const auto& p : patterns) if (p.id == pid) return &p;
    return nullptr;
}
DrumPattern* TrackModel::findPattern (const std::string& pid)
{
    for (auto& p : patterns) if (p.id == pid) return &p;
    return nullptr;
}
Frame ProjectModel::contentEnd() const
{
    Frame e = 0;
    for (const auto& t : tracks)
    {
        for (const auto& c : t.clips) e = std::max (e, c.start + c.length);
        for (const auto& c : t.midiClips) e = std::max (e, ticksToFrames (c.start + c.length));
    }
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
    j["template"] = p.templateId;
    if (! p.harmony.empty()) { json h = json::parse (p.harmony, nullptr, false); if (! h.is_discarded()) j["harmony"] = h; }
    if (! p.music.empty()) { json m = json::parse (p.music, nullptr, false); if (! m.is_discarded()) j["music"] = m; }
    j["loop"] = { { "enabled", p.loopEnabled }, { "start", p.loopStart }, { "end", p.loopEnd } };
    j["ppq"] = kPpq;
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
        {
            json jc = { { "id", c.id }, { "sourceId", c.sourceId }, { "name", c.name }, { "start", c.start },
                        { "sourceOffset", c.sourceOffset }, { "length", c.length }, { "fadeIn", c.fadeIn },
                        { "fadeOut", c.fadeOut }, { "gainDb", c.gainDb } };
            if (c.processed())
            {
                json pp = json::parse (c.procParams, nullptr, false);
                jc["process"] = { { "plugin", c.procPlugin }, { "version", c.procVersion }, { "params", pp.is_discarded() ? json::object() : pp },
                                  { "origSourceId", c.origSourceId }, { "origOffset", c.origOffset }, { "origLength", c.origLength },
                                  { "procSourceId", c.procSourceId }, { "procLength", c.procLength },
                                  { "levelDb", c.procLevelDb } };
            }
            jt["clips"].push_back (jc);
        }
        jt["inserts"] = json::array();
        for (const auto& i : t.inserts)
            jt["inserts"].push_back ({ { "id", i.id }, { "type", i.type }, { "state", i.state }, { "bypass", i.bypass } });
        jt["kind"] = t.kind;
        if (! t.sends.empty()) jt["sends"] = t.sends;
        if (! t.laneRoutes.empty())
        {
            json lr = json::object();
            for (const auto& [lane, bus] : t.laneRoutes) lr[std::to_string (lane)] = bus;
            jt["laneRoutes"] = lr;
        }
        if (! t.instrument.type.empty())
            jt["instrument"] = { { "type", t.instrument.type }, { "state", t.instrument.state }, { "preset", t.instrument.preset } };
        jt["patterns"] = json::array();
        for (const auto& pt : t.patterns)
        {
            json jp = { { "id", pt.id }, { "name", pt.name }, { "steps", pt.steps }, { "swing", pt.swing }, { "lanes", pt.lanes } };
            bool anyRoll = false;
            for (const auto& l : pt.rolls) for (int v : l) anyRoll |= v > 1;
            if (anyRoll) jp["rolls"] = pt.rolls;
            jt["patterns"].push_back (jp);
        }
        jt["midiClips"] = json::array();
        for (const auto& c : t.midiClips)
        {
            json jc = { { "id", c.id }, { "name", c.name }, { "patternId", c.patternId }, { "start", c.start }, { "length", c.length } };
            json notes = json::array();
            for (const auto& n : c.notes) notes.push_back ({ n.key, n.velocity, n.channel, n.start, n.length });
            jc["notes"] = notes; // [tecla, velocidad, canal, inicio, duración] (compacto: un clip puede tener miles)
            jt["midiClips"].push_back (jc);
        }
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
        p.templateId = j.value ("template", std::string());
        if (j.contains ("harmony") && j.at ("harmony").is_object()) p.harmony = j.at ("harmony").dump();
        if (j.contains ("music") && j.at ("music").is_object()) p.music = j.at ("music").dump();
        if (j.contains ("loop") && j["loop"].is_object())
        {
            p.loopEnabled = j["loop"].value ("enabled", false);
            p.loopStart = j["loop"].value ("start", (Tick) 0);
            p.loopEnd = j["loop"].value ("end", (Tick) 16 * kPpq);
        }
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
                if (jc.contains ("process") && jc["process"].is_object())
                {
                    const auto& jp = jc["process"];
                    c.procPlugin = jp.value ("plugin", std::string());
                    c.procVersion = jp.value ("version", std::string());
                    c.procParams = jp.contains ("params") ? jp["params"].dump() : std::string ("{}");
                    c.origSourceId = jp.value ("origSourceId", std::string());
                    c.procSourceId = jp.value ("procSourceId", std::string());
                    c.origOffset = jp.value ("origOffset", (Frame) 0);
                    c.origLength = jp.value ("origLength", (Frame) 0);
                    c.procLength = jp.value ("procLength", (Frame) 0);
                    const float lv = jp.value ("levelDb", 0.0f);
                    c.procLevelDb = std::isfinite (lv) ? std::clamp (lv, -24.0f, 24.0f) : 0.0f;
                    // "match" (0.5.0) se ignora a propósito: el igualado ya no es un dato del proyecto.
                }
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
            t.kind = jt.value ("kind", std::string ("audio"));
            if (jt.contains ("sends") && jt["sends"].is_object())
                for (const auto& [busId, level] : jt["sends"].items()) t.sends[busId] = level.get<float>();
            if (jt.contains ("laneRoutes") && jt["laneRoutes"].is_object())
                for (const auto& [lane, bus] : jt["laneRoutes"].items())
                {
                    const int l = std::atoi (lane.c_str());
                    if (lane != std::to_string (l)) throw std::runtime_error ("carril no válido en laneRoutes");
                    t.laneRoutes[l] = bus.get<std::string>();
                }
            if (jt.contains ("instrument") && jt["instrument"].is_object())
            {
                t.instrument.type = jt["instrument"].at ("type").get<std::string>();
                t.instrument.state = jt["instrument"].value ("state", std::string());
                t.instrument.preset = jt["instrument"].value ("preset", std::string());
            }
            if (jt.contains ("patterns"))
                for (const auto& jp : jt.at ("patterns"))
                {
                    DrumPattern pt;
                    pt.id = jp.at ("id").get<std::string>();
                    pt.name = jp.value ("name", std::string());
                    pt.steps = jp.at ("steps").get<int>();
                    pt.swing = jp.value ("swing", 0.0f);
                    pt.lanes = jp.at ("lanes").get<std::vector<std::vector<int>>>();
                    if (jp.contains ("rolls")) pt.rolls = jp.at ("rolls").get<std::vector<std::vector<int>>>();
                    t.patterns.push_back (pt);
                }
            if (jt.contains ("midiClips"))
                for (const auto& jc : jt.at ("midiClips"))
                {
                    MidiClipModel c;
                    c.id = jc.at ("id").get<std::string>();
                    c.name = jc.value ("name", std::string());
                    c.patternId = jc.value ("patternId", std::string());
                    c.start = jc.at ("start").get<Tick>();
                    c.length = jc.at ("length").get<Tick>();
                    for (const auto& jn : jc.at ("notes"))
                    {
                        MidiNote n;
                        n.key = jn.at (0).get<int>(); n.velocity = jn.at (1).get<int>(); n.channel = jn.at (2).get<int>();
                        n.start = jn.at (3).get<Tick>(); n.length = jn.at (4).get<Tick>();
                        c.notes.push_back (n);
                    }
                    t.midiClips.push_back (c);
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
    {
        int buses = 0;
        for (const auto& t : p.tracks) buses += t.kind == "bus";
        if (buses > 8) return "demasiados buses"; // kMaxBuses (0.7: 8; antes 4)
    }
    std::set<std::string> routedBuses;
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
            if (c.processed())
            {
                const SourceRef* o = p.findSource (c.origSourceId);
                const SourceRef* b = p.findSource (c.procSourceId);
                if (! o || ! b) return "clip procesado sin su audio original o procesado";
                if (c.origOffset < 0 || c.origLength <= 0 || c.origOffset + c.origLength > o->frames) return "original del clip procesado fuera de límites";
                if (c.procLength <= 0 || c.procLength > b->frames) return "versión procesada fuera de límites";
                if (c.sourceId != c.origSourceId && c.sourceId != c.procSourceId) return "clip procesado incoherente";
            }
            if (! std::isfinite (c.gainDb) || c.gainDb > 24) return "ganancia de clip no válida";
        }
        for (const auto& i : t.inserts)
            if (i.id.empty() || ! ids.insert (i.id).second || i.type.empty()) return "inserción no válida";
        if (t.kind != "audio" && t.kind != "synth" && t.kind != "drums" && t.kind != "bus") return "tipo de pista desconocido";
        if (t.kind == "bus" && (! t.clips.empty() || ! t.midiClips.empty() || ! t.instrument.type.empty() || ! t.sends.empty()))
            return "un bus no tiene clips, instrumento ni envíos";
        for (const auto& [busId, level] : t.sends)
        {
            const TrackModel* b = p.findTrack (busId);
            if (! b || b->kind != "bus") return "envío a un bus inexistente";
            if (! std::isfinite (level) || level < 0 || level > 1) return "nivel de envío no válido";
        }
        if (! t.laneRoutes.empty() && t.kind != "drums") return "solo una batería puede enviar piezas a su propio canal";
        for (const auto& [lane, busId] : t.laneRoutes)
        {
            const TrackModel* b = p.findTrack (busId);
            if (lane < 0 || lane >= 8) return "carril de batería no válido";
            if (! b || b->kind != "bus") return "canal propio de una pieza que no es un bus";
            if (! routedBuses.insert (busId).second) return "un canal recibe dos piezas de batería";
        }
        if (t.kind == "synth" && t.instrument.type != "cs.synth") return "pista de sintetizador sin instrumento";
        if (t.kind == "drums" && t.instrument.type != "cs.drums") return "pista de batería sin instrumento";
        if (t.patterns.size() > 64) return "demasiados patrones";
        for (const auto& pt : t.patterns)
        {
            if (! ids.insert (pt.id).second) return "id de patrón duplicado";
            const std::string e = validatePattern (pt);
            if (! e.empty()) return e;
        }
        if (t.midiClips.size() > 512) return "demasiados clips MIDI";
        for (const auto& c : t.midiClips)
        {
            if (c.id.empty() || ! ids.insert (c.id).second) return "id de clip MIDI duplicado o vacío";
            if (c.start < 0 || c.length <= 0 || c.length > (Tick) kPpq * 4 * 1000) return "clip MIDI fuera de rango";
            if (! c.patternId.empty() && ! t.findPattern (c.patternId)) return "clip que referencia un patrón inexistente";
            if (c.notes.size() > 20000) return "demasiadas notas en un clip";
            for (const auto& n : c.notes) { const std::string e = validateNote (n); if (! e.empty()) return e; }
        }
    }
    if (p.loopStart < 0 || p.loopEnd < 0) return "bucle no válido";
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
