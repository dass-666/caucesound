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
    /** Envío post-fader de una pista a un bus (0..1). Se aplica al instante; el estado se refresca con commitMixer. */
    fun setSendLive(trackId: String, busId: String, level: Float) {}
    /** Crea un bus «Reverb» con una reverb 100 % húmeda, listo para recibir envíos. */
    fun addReverbBus() {}
    /** Marca o desmarca un preset como favorito (preferencia del usuario; no modifica el preset ni el proyecto). */
    fun toggleFavoritePreset(presetId: String) {}
    fun setMasterLive(db: Float) {}
    fun commitMixer() {}

    // Proyecto
    fun saveNow() {}
    fun closeProject() {}

    // Clips de audio
    fun splitSelectedAt(frame: Long) {}
    fun duplicateSelected() {}
    fun deleteSelected() {}
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
    /** Redoble de un paso: 1 golpe (normal) a 4 golpes dentro del paso. */
    fun setRoll(trackId: String, patternId: String, lane: Int, step: Int, roll: Int) {}

    // Variaciones del compositor local (misión 0.4). Cada una es un solo paso de deshacer.
    /** Nueva versión de una parte (o de todas con "all"); seed 0 = recomponer con la semilla guardada. */
    fun varyPart(part: String, seed: Long, energy: Float? = null) {}
    fun setPartLocked(trackId: String, locked: Boolean) {}

    // --- Plugins integrados (misión 0.5). Procesos offline: nunca en el hilo de audio. ---
    /** VocalShift: procesa el clip con estos parámetros. El resultado entra como versión procesada (no destructivo). */
    fun vocalProcess(clipId: String, params: com.caucesound.app.data.VocalParams) {}
    fun cancelJob() {}
    /** Comparación A/B: elige qué versión del clip suena, sin mover nada ni reiniciar la reproducción. */
    /** match: null no cambia; true/false activa o desactiva la comparación a volumen igualado. */
    fun setClipVersion(clipId: String, original: Boolean, match: Boolean? = null) {}
    /** Procesa en segundo plano una región corta (frames relativos al clip) y la reproduce en su sitio. No cambia el proyecto. */
    fun vocalPreview(clipId: String, params: com.caucesound.app.data.VocalParams, from: Long, length: Long) {}
    fun previewListen(clipId: String, on: Boolean) {}
    fun discardProcess(clipId: String) {}
    /** Reproduce desde el inicio del clip (para escuchar el resultado). */
    fun auditionClip(clipId: String) {}
    /** Solo AI: analiza un clip de audio (BPM, tonalidad, acordes con confianza). No cambia el proyecto. */
    fun soloAnalyze(clipId: String) {}
    /** Aplica lo que el usuario confirmó del análisis. bpm null = no tocar el tempo del proyecto. */
    fun confirmAnalysis(keyRoot: Int, minor: Boolean, bpm: Double?, useChords: Boolean) {}
    fun dismissAnalysis() {}
    /** Solo AI: pide las cuatro alternativas A-D y pone a sonar la primera en una pista nueva. */
    fun soloProposals(request: com.caucesound.app.data.SoloRequest) {}
    /** Cambia la alternativa que suena (sustituye las notas de la pista de Solo AI; un paso de deshacer). */
    fun soloPick(index: Int) {}
    /** Variación relacionada de la alternativa elegida. */
    fun soloVary() {}
    /** Cambia las notas de la melodía de Solo AI: simpler, intense, soft, octaveUp, octaveDown. Con deshacer. */
    fun soloTransform(op: String) {}
    /** Recompone solo la frase final (los dos últimos compases) y conserva el resto. */
    fun soloNewEnding() {}

    // Instrumentos
    fun setParam(trackId: String, param: String, value: Float) {}
    fun loadPreset(trackId: String, presetId: String) {}

    // Efectos de inserción (Cauce FX)
    fun addInsert(trackId: String, type: String) {}
    fun removeInsert(insertId: String) {}
    fun setInsertParam(insertId: String, param: String, value: Float) {}
    fun setInsertBypass(insertId: String, bypass: Boolean) {}
    fun moveInsert(insertId: String, index: Int) {}
    /** Mixer Pro: cadena completa (FxChains del motor). replace = true sustituye la cadena actual; un deshacer la recupera. */
    fun applyFxChain(trackId: String, presetId: String, replace: Boolean) {}
    /** Channel Rack: pieza de batería a su propio canal del mezclador (own = true) o de vuelta a la batería. */
    fun routeDrumLane(trackId: String, lane: Int, own: Boolean) {}

    /** MIDI en vivo (pads, teclado, vista previa). slot = índice de la pista. */
    fun liveNote(slot: Int, key: Int, velocity: Int, on: Boolean) {}

    fun openPanel(panel: Panel) {}
}

enum class Panel { ARRANGE, RACK, DRUMS, PIANO, SYNTH, MIXER, FX, VARY, VOCAL, SOLO }
