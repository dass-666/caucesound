package com.caucesound.app.data

import org.json.JSONArray
import org.json.JSONObject

/** Construye las peticiones JSON para cs::handleCommand. */
object Cmd {
    private fun c(name: String, vararg kv: Pair<String, Any?>): String {
        val o = JSONObject().put("cmd", name)
        for ((k, v) in kv) if (v != null) o.put(k, v)
        return o.toString()
    }

    fun state() = c("state")
    fun catalog() = c("catalog")
    fun createProject(folder: String, name: String, template: String) = c("createProject", "folder" to folder, "name" to name, "template" to template)
    fun renameProject(root: String, folder: String, name: String) = c("renameProject", "root" to root, "folder" to folder, "name" to name)
    fun duplicateProject(root: String, folder: String, dest: String, name: String) = c("duplicateProject", "root" to root, "folder" to folder, "dest" to dest, "name" to name)
    fun addTrack(name: String, kind: String) = c("addTrack", "name" to name, "kind" to kind)
    fun setInstrumentParam(trackId: String, param: String, value: Float) = c("setInstrumentParam", "trackId" to trackId, "param" to param, "value" to value.toDouble())
    fun loadPreset(trackId: String, preset: String) = c("loadPreset", "trackId" to trackId, "preset" to preset)
    fun addPattern(trackId: String, copyFrom: String?) = c("addPattern", "trackId" to trackId, "copyFrom" to copyFrom)
    fun setStep(trackId: String, patternId: String, lane: Int, step: Int, velocity: Int) =
        c("setStep", "trackId" to trackId, "patternId" to patternId, "lane" to lane, "step" to step, "velocity" to velocity)
    fun setPatternLength(trackId: String, patternId: String, steps: Int) = c("setPatternLength", "trackId" to trackId, "patternId" to patternId, "steps" to steps)
    fun setSwing(trackId: String, patternId: String, swing: Float) = c("setSwing", "trackId" to trackId, "patternId" to patternId, "swing" to swing.toDouble())
    fun setPatternLanes(trackId: String, patternId: String, lanes: List<List<Int>>) =
        c("setPatternLanes", "trackId" to trackId, "patternId" to patternId, "lanes" to JSONArray(lanes.map { JSONArray(it) }))
    fun addMidiClip(trackId: String, start: Long, length: Long, patternId: String?) =
        c("addMidiClip", "trackId" to trackId, "start" to start, "length" to length, "patternId" to patternId)
    fun moveMidiClip(id: String, start: Long, trackId: String?) = c("moveMidiClip", "clipId" to id, "start" to start, "trackId" to trackId)
    fun resizeMidiClip(id: String, start: Long, length: Long) = c("resizeMidiClip", "clipId" to id, "start" to start, "length" to length)
    fun deleteMidiClip(id: String) = c("deleteMidiClip", "clipId" to id)
    fun duplicateMidiClip(id: String) = c("duplicateMidiClip", "clipId" to id)
    fun setClipPattern(id: String, patternId: String) = c("setClipPattern", "clipId" to id, "patternId" to patternId)
    fun setClipNotes(id: String, notes: List<NoteState>) =
        c("setClipNotes", "clipId" to id, "notes" to JSONArray(notes.map { JSONArray(listOf(it.key, it.velocity, it.channel, it.start, it.length)) }))
    fun quantizeClip(id: String, grid: Long) = c("quantizeClip", "clipId" to id, "grid" to grid, "strength" to 1.0)
    fun addInsert(trackId: String, type: String) = c("addInsert", "trackId" to trackId, "type" to type)
    fun removeInsert(insertId: String) = c("removeInsert", "insertId" to insertId)
    fun setInsertParam(insertId: String, param: String, value: Float) = c("setInsertParam", "insertId" to insertId, "param" to param, "value" to value.toDouble())
    fun setInsertBypass(insertId: String, bypass: Boolean) = c("setInsertBypass", "insertId" to insertId, "bypass" to bypass)
    fun moveInsert(insertId: String, index: Int) = c("moveInsert", "insertId" to insertId, "index" to index)
    fun setLoop(on: Boolean, start: Long, end: Long) = c("setLoop", "on" to on, "start" to start, "end" to end)
    fun setMaster(db: Float) = c("setMaster", "db" to db.toDouble())
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
