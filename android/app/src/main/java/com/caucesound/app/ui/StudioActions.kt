package com.caucesound.app.ui

import com.caucesound.app.data.NoteState

/**
 * Acciones del estudio. Las pantallas reciben el estado (ProjectState) y esta interfaz, así se pueden dibujar
 * sin el motor nativo (vistas previas y capturas en la JVM). DawViewModel la implementa contra el motor.
 * Todas tienen implementación vacía por defecto.
 */
interface StudioActions {
    // Transporte
    fun play() {}
    fun stop() {}
    fun record() {}
    fun seek(frame: Long) {}
    fun setTempo(bpm: Double, beatsPerBar: Int) {}
    fun setMetronome(on: Boolean) {}
    fun setLoop(on: Boolean, startTick: Long, endTick: Long) {}
    fun undo() {}
    fun redo() {}

    // Selección (estado de interfaz)
    fun selectTrack(id: String) {}
    fun selectClip(id: String?) {}

    // Pistas y mezcla
    fun addTrack(name: String, kind: String) {}
    fun setMute(id: String, on: Boolean) {}
    fun setSolo(id: String, on: Boolean) {}
    fun setArmed(id: String, on: Boolean) {}
    fun setVolumeLive(id: String, db: Float) {}
    fun setPanLive(id: String, pan: Float) {}
    fun setMasterLive(db: Float) {}
    fun commitMixer() {}

    // Clips de audio
    fun moveClip(id: String, start: Long, trackId: String?) {}
    fun trimClip(id: String, start: Long, length: Long) {}

    // Clips MIDI y notas
    fun addMidiClip(trackId: String, startTick: Long, lengthTick: Long, patternId: String? = null) {}
    fun moveMidiClip(id: String, startTick: Long, trackId: String?) {}
    fun resizeMidiClip(id: String, startTick: Long, lengthTick: Long) {}
    fun deleteMidiClip(id: String) {}
    fun duplicateMidiClip(id: String) {}
    fun setClipPattern(id: String, patternId: String) {}
    fun setClipNotes(clipId: String, notes: List<NoteState>) {}
    fun quantizeClip(clipId: String, gridTicks: Long) {}

    // Batería
    fun setStep(trackId: String, patternId: String, lane: Int, step: Int, velocity: Int) {}
    fun addPattern(trackId: String, copyFrom: String?) {}
    fun setPatternLength(trackId: String, patternId: String, steps: Int) {}
    fun setSwing(trackId: String, patternId: String, swing: Float) {}
    fun setPatternLanes(trackId: String, patternId: String, lanes: List<List<Int>>) {}

    // Instrumentos
    fun setParam(trackId: String, param: String, value: Float) {}
    fun loadPreset(trackId: String, presetId: String) {}

    // Efectos de inserción (Cauce FX)
    fun addInsert(trackId: String, type: String) {}
    fun removeInsert(insertId: String) {}
    fun setInsertParam(insertId: String, param: String, value: Float) {}
    fun setInsertBypass(insertId: String, bypass: Boolean) {}
    fun moveInsert(insertId: String, index: Int) {}

    /** MIDI en vivo (pads, teclado, vista previa). slot = índice de la pista. */
    fun liveNote(slot: Int, key: Int, velocity: Int, on: Boolean) {}

    fun openPanel(panel: Panel) {}
}

enum class Panel { ARRANGE, DRUMS, PIANO, SYNTH, MIXER, FX }
