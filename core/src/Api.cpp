#include "cs/Api.h"

#include "cs/Project.h"

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
        if (cmd == "createProject") return result (s.createProject (str ("folder"), str ("name"))).dump();
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
        if (cmd == "addTrack") return result (s.addTrack (str ("name"))).dump();
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
