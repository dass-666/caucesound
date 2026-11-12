package com.caucesound.app.data

import org.json.JSONObject

/** Construye las peticiones JSON para cs::handleCommand. */
object Cmd {
    private fun c(name: String, vararg kv: Pair<String, Any?>): String {
        val o = JSONObject().put("cmd", name)
        for ((k, v) in kv) if (v != null) o.put(k, v)
        return o.toString()
    }

    fun state() = c("state")
    fun listProjects(root: String) = c("listProjects", "root" to root)
    fun createProject(folder: String, name: String) = c("createProject", "folder" to folder, "name" to name)
    fun openProject(folder: String) = c("openProject", "folder" to folder)
    fun closeProject() = c("closeProject")
    fun save() = c("save")
    fun addTrack(name: String) = c("addTrack", "name" to name)
    fun removeTrack(id: String) = c("removeTrack", "trackId" to id)
    fun renameTrack(id: String, name: String) = c("renameTrack", "trackId" to id, "name" to name)
    fun setVolume(id: String, db: Float) = c("setVolume", "trackId" to id, "db" to db.toDouble())
    fun setPan(id: String, pan: Float) = c("setPan", "trackId" to id, "pan" to pan.toDouble())
    fun setMute(id: String, on: Boolean) = c("setMute", "trackId" to id, "on" to on)
    fun setSolo(id: String, on: Boolean) = c("setSolo", "trackId" to id, "on" to on)
    fun setArmed(id: String, on: Boolean) = c("setArmed", "trackId" to id, "on" to on)
    fun importWav(path: String, trackId: String, at: Long, name: String) =
        c("importWav", "path" to path, "trackId" to trackId, "at" to at, "name" to name)
    fun moveClip(id: String, start: Long, trackId: String?) = c("moveClip", "clipId" to id, "start" to start, "trackId" to trackId)
    fun trimClip(id: String, start: Long, length: Long) = c("trimClip", "clipId" to id, "start" to start, "length" to length)
    fun splitClip(id: String, at: Long) = c("splitClip", "clipId" to id, "at" to at)
    fun deleteClip(id: String) = c("deleteClip", "clipId" to id)
    fun duplicateClip(id: String) = c("duplicateClip", "clipId" to id)
    fun setFades(id: String, fadeIn: Long, fadeOut: Long) = c("setFades", "clipId" to id, "fadeIn" to fadeIn, "fadeOut" to fadeOut)
    fun setClipGain(id: String, db: Float) = c("setClipGain", "clipId" to id, "db" to db.toDouble())
    fun undo() = c("undo")
    fun redo() = c("redo")
    fun setTempo(bpm: Double, beatsPerBar: Int) = c("setTempo", "bpm" to bpm, "beatsPerBar" to beatsPerBar)
    fun setMetronome(on: Boolean) = c("setMetronome", "on" to on)
    fun setLatencyOffset(frames: Long) = c("setLatencyOffset", "frames" to frames)
    fun play() = c("play")
    fun stop() = c("stop")
    fun seek(frame: Long) = c("seek", "frame" to frame)
    fun setMonitoring(on: Boolean) = c("setMonitoring", "on" to on, "gain" to 1.0)
    fun startRecording(trackId: String) = c("startRecording", "trackId" to trackId, "channels" to 1)
    fun export(path: String, bits: Int) = c("export", "path" to path, "bits" to bits, "tail" to SAMPLE_RATE / 2)
}
