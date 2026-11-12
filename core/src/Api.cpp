#include "cs/Api.h"

#include "cs/Instruments.h"
#include "cs/Project.h"

#include <filesystem>

#include <nlohmann/json.hpp>

namespace cs
{
using nlohmann::json;

namespace
{
json result (const OpResult& r)
{
    json j = { { "ok", r.ok } };
    if (! r.id.empty()) j["id"] = r.id;
    if (! r.error.empty()) j["error"] = r.error;
    return j;
}

json listProjects (const std::string& root)
{
    json arr = json::array();
    for (const auto& name : listDir (root))
    {
        const std::string folder = root + "/" + name;
        std::string text;
        if (! readFile (folder + "/project.json", text) && ! readFile (folder + "/project.json.bak", text)) continue;
        ProjectModel p;
        std::string err;
        json item = { { "folder", folder } };
        if (fromJson (text, p, err))
        {
            item["name"] = p.name;
            item["id"] = p.id;
            item["modifiedAt"] = p.modifiedAt;
            item["tracks"] = p.tracks.size();
            item["bpm"] = p.bpm;
            item["template"] = p.templateId;
            // Miniatura: por pista, color y tramos ocupados (fracción 0..1 de la duración del proyecto).
            const double total = (double) std::max<Frame> (p.contentEnd(), 1);
            json thumb = json::array();
            for (size_t ti = 0; ti < p.tracks.size() && ti < 8; ++ti)
            {
                const auto& t = p.tracks[ti];
                json spans = json::array();
                for (const auto& c : t.clips) if (spans.size() < 16) spans.push_back ({ c.start / total, (c.start + c.length) / total });
                for (const auto& c : t.midiClips)
                    if (spans.size() < 16) spans.push_back ({ p.ticksToFrames (c.start) / total, p.ticksToFrames (c.start + c.length) / total });
                thumb.push_back ({ { "color", t.color }, { "kind", t.kind }, { "spans", spans } });
            }
            item["thumb"] = thumb;
        }
        else
        {
            item["name"] = name;
            item["error"] = err;
        }
        arr.push_back (item);
    }
    return arr;
}
} // namespace

namespace
{
json catalog()
{
    json presets = json::array();
    for (const auto& p : synthPresets()) presets.push_back ({ { "id", p.id }, { "name", p.name } });
    json kits = json::array();
    for (const auto& k : drumKits()) kits.push_back ({ { "id", k.id }, { "name", k.name }, { "lanes", k.laneNames } });
    auto params = [] (const char* type)
    {
        json arr = json::array();
        if (auto p = createProcessor (type))
            for (const auto& i : p->parameters())
                arr.push_back ({ { "id", i.id }, { "name", i.name }, { "min", i.minValue }, { "max", i.maxValue }, { "default", i.defaultValue } });
        return arr;
    };
    json fx = json::array();
    for (const auto& t : effectTypes()) fx.push_back ({ { "type", t }, { "params", params (t.c_str()) } });
    json tpl = json::array();
    for (const auto& t : projectTemplates()) tpl.push_back ({ { "id", t.id }, { "bpm", t.bpm }, { "kit", t.kitId }, { "tracks", t.tracks } });
    return { { "ok", true }, { "ppq", kPpq }, { "synthPresets", presets }, { "drumKits", kits }, { "synthParams", params ("cs.synth") },
             { "drumParams", params ("cs.drums") }, { "effects", fx }, { "templates", tpl } };
}

std::vector<MidiNote> parseNotes (const json& arr)
{
    std::vector<MidiNote> notes;
    if (! arr.is_array()) return notes;
    for (const auto& jn : arr)
    {
        MidiNote n;
        if (jn.is_array()) { n.key = jn.at (0); n.velocity = jn.at (1); n.channel = jn.at (2); n.start = jn.at (3); n.length = jn.at (4); }
        else { n.key = jn.at ("key"); n.velocity = jn.value ("velocity", 100); n.channel = jn.value ("channel", 0); n.start = jn.at ("start"); n.length = jn.at ("length"); }
        notes.push_back (n);
    }
    return notes;
}

bool insideRoot (const std::string& root, const std::string& folder)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    const auto r = fs::weakly_canonical (root, ec).string(), f = fs::weakly_canonical (folder, ec).string();
    return ! r.empty() && f.size() > r.size() + 1 && f.compare (0, r.size(), r) == 0 && f[r.size()] == '/';
}
} // namespace

std::string handleCommand (Session& s, Engine& e, const std::string& request)
{
    const json q = json::parse (request, nullptr, false);
    if (q.is_discarded() || ! q.is_object() || ! q.contains ("cmd") || ! q["cmd"].is_string())
        return json { { "ok", false }, { "error", "petición no válida" } }.dump();
    try
    {
        const std::string cmd = q["cmd"].get<std::string>();
        auto str = [&] (const char* k) { return q.value (k, std::string()); };
        auto num = [&] (const char* k, double d = 0) { return q.contains (k) && q[k].is_number() ? q[k].get<double>() : d; };
        auto frame = [&] (const char* k, Frame d = 0) { return q.contains (k) && q[k].is_number() ? (Frame) q[k].get<double>() : d; };
        auto flag = [&] (const char* k) { return q.value (k, false); };

        if (cmd == "state") return s.stateJson();
        if (cmd == "listProjects") return json { { "ok", true }, { "projects", listProjects (str ("root")) } }.dump();
        if (cmd == "createProject") return result (s.createProject (str ("folder"), str ("name"), 48000, str ("template"))).dump();
        if (cmd == "catalog") return catalog().dump();
        if (cmd == "renameProject" || cmd == "duplicateProject")
        {
            // Opera sobre proyectos cerrados (desde la pantalla de inicio). root delimita lo que se puede tocar.
            const std::string root = str ("root"), folder = str ("folder");
            if (! insideRoot (root, folder)) return result (OpResult::fail ("carpeta fuera de los proyectos")).dump();
            if (s.isOpen() && s.folder() == folder) s.close();
            std::string text, err;
            if (! readFile (folder + "/project.json", text)) return result (OpResult::fail ("no se encontró project.json")).dump();
            json pj = json::parse (text, nullptr, false);
            if (pj.is_discarded()) return result (OpResult::fail ("proyecto dañado")).dump();
            if (cmd == "renameProject")
            {
                pj["name"] = str ("name");
                if (! atomicWriteFile (folder + "/project.json", pj.dump (2), err, true)) return result (OpResult::fail (err)).dump();
                return json { { "ok", true } }.dump();
            }
            const std::string dest = str ("dest");
            if (! insideRoot (root, dest) || fileExists (dest)) return result (OpResult::fail ("destino no válido")).dump();
            std::error_code ec;
            std::filesystem::copy (folder, dest, std::filesystem::copy_options::recursive, ec);
            if (ec) { std::filesystem::remove_all (dest, ec); return result (OpResult::fail ("no se pudo copiar el proyecto (¿espacio?)")).dump(); }
            pj["name"] = str ("name");
            pj["id"] = newId ("prj");
            if (! atomicWriteFile (dest + "/project.json", pj.dump (2), err, false)) return result (OpResult::fail (err)).dump();
            removeFile (dest + "/project.json.bak");
            return json { { "ok", true }, { "folder", dest } }.dump();
        }
        if (cmd == "openProject")
        {
            LoadReport rep;
            json j = result (s.openProject (str ("folder"), &rep));
            j["missingSources"] = rep.missingSources;
            j["recoveredRecordings"] = rep.recoveredRecordings;
            j["usedBackup"] = rep.usedBackup;
            return j.dump();
        }
        if (cmd == "closeProject") { s.close(); return json { { "ok", true } }.dump(); }
        if (cmd == "save") return result (s.save()).dump();
        if (cmd == "addTrack") return result (s.addTrack (str ("name"), q.value ("kind", std::string ("audio")))).dump();
        if (cmd == "addInsert") return result (s.addInsert (str ("trackId"), str ("type"))).dump();
        if (cmd == "removeInsert") return result (s.removeInsert (str ("insertId"))).dump();
        if (cmd == "setInsertParam") return result (s.setInsertParam (str ("insertId"), str ("param"), (float) num ("value"))).dump();
        if (cmd == "setInsertBypass") return result (s.setInsertBypass (str ("insertId"), q.value ("bypass", false))).dump();
        if (cmd == "moveInsert") return result (s.moveInsert (str ("insertId"), (int) num ("index"))).dump();
        if (cmd == "insertMeter") return json { { "ok", true }, { "grDb", s.insertMeter (str ("insertId")) } }.dump();
        if (cmd == "setInstrumentParam") return result (s.setInstrumentParam (str ("trackId"), str ("param"), (float) num ("value"))).dump();
        if (cmd == "loadPreset") return result (s.loadSynthPreset (str ("trackId"), str ("preset"))).dump();
        if (cmd == "addPattern") return result (s.addPattern (str ("trackId"), str ("copyFrom"))).dump();
        if (cmd == "setStep") return result (s.setPatternStep (str ("trackId"), str ("patternId"), (int) num ("lane"), (int) num ("step"), (int) num ("velocity"))).dump();
        if (cmd == "setPatternLength") return result (s.setPatternLength (str ("trackId"), str ("patternId"), (int) num ("steps", 16))).dump();
        if (cmd == "setSwing") return result (s.setPatternSwing (str ("trackId"), str ("patternId"), (float) num ("swing"))).dump();
        if (cmd == "setPatternLanes") return result (s.setPatternLanes (str ("trackId"), str ("patternId"), q.at ("lanes").get<std::vector<std::vector<int>>>())).dump();
        if (cmd == "renamePattern") return result (s.renamePattern (str ("trackId"), str ("patternId"), str ("name"))).dump();
        if (cmd == "addMidiClip") return result (s.addMidiClip (str ("trackId"), (Tick) num ("start"), (Tick) num ("length", 4 * kPpq * 4), str ("patternId"))).dump();
        if (cmd == "moveMidiClip") return result (s.moveMidiClip (str ("clipId"), (Tick) num ("start"), str ("trackId"))).dump();
        if (cmd == "resizeMidiClip") return result (s.resizeMidiClip (str ("clipId"), (Tick) num ("start"), (Tick) num ("length"))).dump();
        if (cmd == "deleteMidiClip") return result (s.deleteMidiClip (str ("clipId"))).dump();
        if (cmd == "duplicateMidiClip") return result (s.duplicateMidiClip (str ("clipId"))).dump();
        if (cmd == "setClipPattern") return result (s.setMidiClipPattern (str ("clipId"), str ("patternId"))).dump();
        if (cmd == "setClipNotes") return result (s.setClipNotes (str ("clipId"), parseNotes (q.value ("notes", json::array())))).dump();
        if (cmd == "quantizeClip") return result (s.quantizeClip (str ("clipId"), (Tick) num ("grid", kStepTicks), (float) num ("strength", 1.0), flag ("lengths"))).dump();
        if (cmd == "setLoop") return result (s.setLoop (flag ("on"), (Tick) num ("start"), (Tick) num ("end", 16 * kPpq))).dump();
        if (cmd == "removeTrack") return result (s.removeTrack (str ("trackId"))).dump();
        if (cmd == "renameTrack") return result (s.renameTrack (str ("trackId"), str ("name"))).dump();
        if (cmd == "setVolume") return result (s.setTrackVolumeDb (str ("trackId"), (float) num ("db"))).dump();
        if (cmd == "setPan") return result (s.setTrackPan (str ("trackId"), (float) num ("pan"))).dump();
        if (cmd == "setMute") return result (s.setTrackMute (str ("trackId"), flag ("on"))).dump();
        if (cmd == "setSolo") return result (s.setTrackSolo (str ("trackId"), flag ("on"))).dump();
        if (cmd == "setArmed") return result (s.setTrackArmed (str ("trackId"), flag ("on"))).dump();
        if (cmd == "setMaster") return result (s.setMasterDb ((float) num ("db"))).dump();
        if (cmd == "importWav") return result (s.importWav (str ("path"), str ("trackId"), frame ("at", -1), str ("name"))).dump();
        if (cmd == "moveClip") return result (s.moveClip (str ("clipId"), frame ("start"), str ("trackId"))).dump();
        if (cmd == "trimClip") return result (s.trimClip (str ("clipId"), frame ("start"), frame ("length"))).dump();
        if (cmd == "splitClip") return result (s.splitClip (str ("clipId"), frame ("at"))).dump();
        if (cmd == "deleteClip") return result (s.deleteClip (str ("clipId"))).dump();
        if (cmd == "duplicateClip") return result (s.duplicateClip (str ("clipId"))).dump();
        if (cmd == "setFades") return result (s.setClipFades (str ("clipId"), frame ("fadeIn"), frame ("fadeOut"))).dump();
        if (cmd == "setClipGain") return result (s.setClipGain (str ("clipId"), (float) num ("db"))).dump();
        if (cmd == "undo") return result (s.undo()).dump();
        if (cmd == "redo") return result (s.redo()).dump();
        if (cmd == "setTempo") return result (s.setTempo (num ("bpm", 120), (int) num ("beatsPerBar", 4))).dump();
        if (cmd == "setMetronome") return result (s.setMetronome (flag ("on"))).dump();
        if (cmd == "setLatencyOffset") return result (s.setRecordLatencyOffset (frame ("frames"))).dump();
        if (cmd == "play") { if (! s.isOpen()) return result (OpResult::fail ("no hay proyecto abierto")).dump(); e.play(); return json { { "ok", true } }.dump(); }
        if (cmd == "stop")
        {
            if (s.isRecording()) return result (s.stopRecording()).dump();
            e.stop();
            return json { { "ok", true } }.dump();
        }
        if (cmd == "seek") { e.seek (frame ("frame")); return json { { "ok", true } }.dump(); }
        if (cmd == "setMonitoring") { e.setMonitoring (flag ("on"), (float) num ("gain", 1.0)); return json { { "ok", true } }.dump(); }
        if (cmd == "startRecording") return result (s.startRecording (str ("trackId"), (int) num ("channels", 1), true)).dump();
        if (cmd == "stopRecording") return result (s.stopRecording()).dump();
        if (cmd == "export")
        {
            long clipped = 0;
            json j = result (s.exportMix (str ("path"), (int) num ("bits", 24), frame ("tail"), &clipped));
            j["clippedSamples"] = clipped;
            return j.dump();
        }
        return json { { "ok", false }, { "error", "comando desconocido: " + cmd } }.dump();
    }
    catch (const std::exception& ex)
    {
        return json { { "ok", false }, { "error", std::string ("parámetros no válidos: ") + ex.what() } }.dump();
    }
}
} // namespace cs
