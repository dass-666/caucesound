package com.caucesound.app

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.caucesound.app.audio.AudioDecoder
import com.caucesound.app.audio.AudioGuard
import com.caucesound.app.audio.NativeEngine
import com.caucesound.app.data.Cmd
import com.caucesound.app.data.LiveBench
import com.caucesound.app.data.CommandResult
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.ProjectSummary
import com.caucesound.app.data.StateParser
import com.caucesound.app.data.Catalog
import com.caucesound.app.data.SAMPLE_RATE
import com.caucesound.app.data.NoteState
import com.caucesound.app.ui.tr
import com.caucesound.app.ui.Panel
import com.caucesound.app.ui.StudioActions
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.io.File

/**
 * Estado de la app y único punto que envía comandos al motor. Todos los comandos pasan por un hilo de
 * control único ([control]) para respetar el contrato de cs::Session; el hilo de audio es independiente.
 */
@OptIn(ExperimentalCoroutinesApi::class)
const val WAVE_BLOCK = 256L

class DawViewModel(app: Application) : AndroidViewModel(app), StudioActions {
    private val control = Dispatchers.IO.limitedParallelism(1)
    val projectsRoot = File(app.filesDir, "projects").apply { mkdirs() }

    private val _projects = MutableStateFlow<List<ProjectSummary>>(emptyList())
    val projects: StateFlow<List<ProjectSummary>> = _projects.asStateFlow()
    private val _state = MutableStateFlow<ProjectState?>(null)
    val state: StateFlow<ProjectState?> = _state.asStateFlow()
    private val _selectedClip = MutableStateFlow<String?>(null)
    val selectedClip: StateFlow<String?> = _selectedClip.asStateFlow()
    private val _catalog = MutableStateFlow(Catalog.EMPTY)
    val catalog: StateFlow<Catalog> = _catalog.asStateFlow()
    private val _selectedTrack = MutableStateFlow<String?>(null)
    val selectedTrack: StateFlow<String?> = _selectedTrack.asStateFlow()
    private val _panel = MutableStateFlow(Panel.ARRANGE)
    val panel: StateFlow<Panel> = _panel.asStateFlow()
    private val _messages = MutableStateFlow<String?>(null)
    val messages: StateFlow<String?> = _messages.asStateFlow()
    private val _busy = MutableStateFlow<String?>(null)
    val busy: StateFlow<String?> = _busy.asStateFlow()
    private val _audioStatus = MutableStateFlow<JSONObject?>(null)
    val audioStatus: StateFlow<JSONObject?> = _audioStatus.asStateFlow()
    private val _route = MutableStateFlow(AudioGuard.Route.SPEAKER)
    val route: StateFlow<AudioGuard.Route> = _route.asStateFlow()
    private val _monitoring = MutableStateFlow(false)
    val monitoring: StateFlow<Boolean> = _monitoring.asStateFlow()

    // Plugins (misión 0.5): presets de VocalShift, trabajo offline en curso, último análisis y familia de Solo AI.
    private val _plugins = MutableStateFlow(com.caucesound.app.data.PluginUi())
    val plugins: StateFlow<com.caucesound.app.data.PluginUi> = _plugins.asStateFlow()
    private var jobPoll: Job? = null

    private var autosave: Job? = null
    private var audioOk = false

    val guard = AudioGuard(app, onInterrupt = { reason ->
        viewModelScope.launch {
            val wasRecording = _state.value?.recording == true
            val r = call(Cmd.stop())
            refresh()
            if (wasRecording) toast(if (r.ok) str(R.string.msg_recording_saved_interrupt) else (r.error ?: ""))
            else if (reason == "noisy") toast(str(R.string.msg_headphones_removed))
        }
    }, onDevicesChanged = { _route.value = currentRoute(); refreshAudioStatus() })

    init {
        guard.register()
        _route.value = currentRoute()
        startAudio()
        refreshProjects()
        viewModelScope.launch {
            setupPacks()
            _catalog.value = withContext(control) { runCatching { Catalog.parse(NativeEngine.command(Cmd.catalog())) }.getOrDefault(Catalog.EMPTY) }
            if (_catalog.value.hasPlugin("vocalshift")) {
                val presets = withContext(control) { runCatching { com.caucesound.app.data.PluginParser.vocalPresets(NativeEngine.command(Cmd.vocalPresets())) }.getOrDefault(emptyList()) }
                _plugins.value = _plugins.value.copy(vocalPresets = presets)
            }
        }
    }

    private fun currentRoute(): AudioGuard.Route = runCatching { guard.outputRoute() }.getOrDefault(AudioGuard.Route.SPEAKER)
    private fun str(id: Int, vararg args: Any) = getApplication<Application>().getString(id, *args)
    fun toast(msg: String) { _messages.value = msg }
    fun consumeMessage() { _messages.value = null }

    private suspend fun call(cmd: String): CommandResult = withContext(control) { StateParser.result(NativeEngine.command(cmd)) }

    private suspend fun refresh() {
        val s = withContext(control) { StateParser.state(NativeEngine.command(Cmd.state())) }
        _state.value = s
        if (s != null) loadWaveforms(s)
        if (s != null && _selectedClip.value != null && s.clip(_selectedClip.value!!) == null) _selectedClip.value = null
    }

    /** Ejecuta un comando de edición, informa del error y refresca el estado. */
    private fun edit(cmd: String, onOk: (CommandResult) -> Unit = {}) {
        viewModelScope.launch {
            val r = call(cmd)
            if (!r.ok && r.error != null) toast(r.error)
            refresh()
            if (r.ok) onOk(r)
        }
    }

    // --- Audio --------------------------------------------------------------------------------------------------
    fun startAudio() {
        viewModelScope.launch(control) {
            audioOk = NativeEngine.audioStart()
            if (!audioOk) toast(str(R.string.msg_audio_failed))
        }
        refreshAudioStatus()
    }

    fun stopAudioIfIdle() {
        viewModelScope.launch(control) {
            if (_state.value?.recording != true && !NativeEngine.isPlaying()) NativeEngine.audioStop()
        }
    }

    fun refreshAudioStatus() {
        viewModelScope.launch(control) { _audioStatus.value = runCatching { JSONObject(NativeEngine.audioStatus()) }.getOrNull() }
    }

    fun setMonitoring(on: Boolean) {
        viewModelScope.launch {
            if (on) withContext(control) { NativeEngine.setInputEnabled(true) }
            call(Cmd.setMonitoring(on))
            _monitoring.value = on
        }
    }

    // --- Proyectos ----------------------------------------------------------------------------------------------
    fun refreshProjects() {
        viewModelScope.launch {
            _projects.value = withContext(control) { StateParser.projects(NativeEngine.command(Cmd.listProjects(projectsRoot.absolutePath))) }
        }
    }

    /** template: "trap", "reggaeton", "hiphop", "pop", "electronic" o "empty" (definidas en el motor). */
    fun createProject(name: String, template: String, defaultTrackName: (Int) -> String) {
        viewModelScope.launch {
            val safe = name.trim().ifEmpty { "Proyecto" }
            val folder = File(projectsRoot, "p_" + System.currentTimeMillis())
            val r = call(Cmd.createProject(folder.absolutePath, safe, template))
            if (!r.ok) { toast(r.error ?: ""); return@launch }
            // 0.10: si a «Signature 2» le faltaba algún sonido de paquete, el motor usó el sintetizador y lo dice.
            r.raw.optJSONArray("notes")?.takeIf { it.length() > 0 }?.let {
                toast(tr("Faltan sonidos de Signature 2: se usaron los sintetizadores. ", "Signature 2 sounds missing: synths were used. ") + it.optString(0))
            }
            if (template == "empty") for (i in 1..4) call(Cmd.addTrack(defaultTrackName(i)))
            call(Cmd.save())
            onProjectOpened()
        }
    }

    /**
     * «Idea rápida»: un proyecto vacío con una sola pista de audio ya armada, para capturar una voz o un tarareo sin
     * pasos previos. Es un proyecto normal: después se le pueden añadir batería, Solo AI o VocalShift. [then] se llama
     * con el proyecto abierto (la Activity pide ahí el permiso del micrófono y empieza a grabar); si el permiso se
     * niega, el proyecto queda listo y se graba con el botón de siempre.
     */
    fun createQuickIdea(name: String, trackName: String, then: () -> Unit) {
        viewModelScope.launch {
            val folder = File(projectsRoot, "p_" + System.currentTimeMillis())
            val r = call(Cmd.createProject(folder.absolutePath, name.trim().ifEmpty { "Idea" }, "empty"))
            if (!r.ok) { toast(r.error ?: ""); return@launch }
            val track = call(Cmd.addTrack(trackName))
            track.id?.let { call(Cmd.setArmed(it, true)) }
            call(Cmd.save())
            onProjectOpened()
            track.id?.let { _selectedTrack.value = it }
            if (track.ok && track.id != null) then()
        }
    }

    fun renameProject(folder: String, name: String) {
        viewModelScope.launch {
            val r = call(Cmd.renameProject(projectsRoot.absolutePath, folder, name.trim().ifEmpty { "Proyecto" }))
            if (!r.ok) toast(r.error ?: "")
            refreshProjects()
        }
    }

    fun duplicateProject(folder: String, name: String) {
        viewModelScope.launch {
            val dest = File(projectsRoot, "p_" + System.currentTimeMillis()).absolutePath
            val r = call(Cmd.duplicateProject(projectsRoot.absolutePath, folder, dest, name))
            if (!r.ok) toast(r.error ?: "")
            refreshProjects()
        }
    }

    /**
     * Proyecto completo a un archivo .caucesound (ZIP) elegido por el usuario con el selector del sistema: el motor lo
     * escribe en la caché de la app y aquí se copia al destino. Nada sale del dispositivo salvo a donde el usuario elija.
     */
    fun exportProjectArchive(folder: String, target: android.net.Uri) {
        viewModelScope.launch {
            val app = getApplication<Application>()
            val tmp = File(app.cacheDir, "export_${System.currentTimeMillis()}.caucesound")
            val r = call(Cmd.exportProjectArchive(projectsRoot.absolutePath, folder, tmp.absolutePath))
            if (!r.ok) { tmp.delete(); toast(r.error ?: ""); return@launch }
            val copied = withContext(Dispatchers.IO) {
                runCatching {
                    app.contentResolver.openOutputStream(target, "w")!!.use { out -> tmp.inputStream().use { it.copyTo(out, 1 shl 16) } }
                }.isSuccess.also { tmp.delete() }
            }
            toast(if (copied) tr("Proyecto exportado (${r.raw.optInt("files")} archivos)", "Project exported (${r.raw.optInt("files")} files)")
                  else tr("No se pudo escribir el archivo elegido", "Could not write the chosen file"))
        }
    }

    /** Importa un .caucesound como proyecto nuevo (id nuevo; no sustituye a ninguno existente). */
    fun importProjectArchive(source: android.net.Uri) {
        viewModelScope.launch {
            val app = getApplication<Application>()
            val tmp = File(app.cacheDir, "import_${System.currentTimeMillis()}.caucesound")
            val copied = withContext(Dispatchers.IO) {
                runCatching { app.contentResolver.openInputStream(source)!!.use { inp -> tmp.outputStream().use { inp.copyTo(it, 1 shl 16) } } }.isSuccess
            }
            if (!copied) { tmp.delete(); toast(tr("No se pudo leer el archivo", "Could not read the file")); return@launch }
            val dest = File(projectsRoot, "p_" + System.currentTimeMillis()).absolutePath
            val r = call(Cmd.importProjectArchive(projectsRoot.absolutePath, tmp.absolutePath, dest))
            withContext(Dispatchers.IO) { tmp.delete() }
            toast(if (r.ok) tr("Proyecto importado: ", "Project imported: ") + r.raw.optString("name") else (r.error ?: ""))
            refreshProjects()
        }
    }

    fun openProject(folder: String) {
        viewModelScope.launch {
            val r = call(Cmd.openProject(folder))
            if (!r.ok) { toast(r.error ?: ""); return@launch }
            val recovered = r.raw.optJSONArray("recoveredRecordings")?.length() ?: 0
            val missing = r.raw.optJSONArray("missingSources")?.length() ?: 0
            when {
                recovered > 0 -> toast(str(R.string.msg_recovered, recovered))
                r.raw.optBoolean("usedBackup") -> toast(str(R.string.msg_used_backup))
                missing > 0 -> toast(str(R.string.msg_missing_audio, missing))
            }
            onProjectOpened()
        }
    }

    private suspend fun onProjectOpened() {
        clearPluginUi()
        _waveforms.value = emptyMap()
        _selectedClip.value = null
        _panel.value = Panel.ARRANGE
        refresh()
        _selectedTrack.value = _state.value?.tracks?.firstOrNull { !it.isAudio }?.id ?: _state.value?.tracks?.firstOrNull()?.id
        autosave?.cancel()
        autosave = viewModelScope.launch {
            while (isActive) {
                delay(15_000)
                if (_state.value?.dirty == true && _state.value?.recording != true) call(Cmd.save()).let { if (it.ok) refresh() }
            }
        }
    }

    override fun closeProject() {
        autosave?.cancel()
        _autoMixRevertable.value = false
        _autoMix.value = com.caucesound.app.data.AutoMixUi()
        clearPluginUi() // el motor cancela los trabajos del proyecto al cerrarlo; aquí se deja de consultarlos
        viewModelScope.launch {
            call(Cmd.closeProject()) // guarda si hay cambios, detiene grabación/transporte y vuelve la posición a 0
            guard.abandonFocus()
            _state.value = null
            refreshProjects()
        }
    }

    fun deleteProject(folder: String) {
        viewModelScope.launch {
            withContext(Dispatchers.IO) {
                val f = File(folder)
                // Solo borramos dentro de la carpeta de proyectos de la app.
                if (f.canonicalPath.startsWith(projectsRoot.canonicalPath + File.separator)) f.deleteRecursively()
            }
            refreshProjects()
        }
    }

    fun saveIfDirty() { if (_state.value?.dirty == true) viewModelScope.launch { call(Cmd.save()); refresh() } }

    override fun saveNow() = edit(Cmd.save()) { toast(str(R.string.msg_saved)) }

    // --- Transporte ---------------------------------------------------------------------------------------------
    override fun play() {
        if (!guard.requestFocus()) { toast(str(R.string.msg_focus_denied)); return }
        viewModelScope.launch {
            // Comprueba que el stream de salida sigue vivo (p. ej. tras un error de Oboe) antes de arrancar.
            val audio = withContext(control) { NativeEngine.audioEnsure() }
            audioOk = audio
            if (!audio) { toast(str(R.string.msg_audio_failed)); refreshAudioStatus(); return@launch }
            val r = call(Cmd.play())
            if (!r.ok && r.error != null) toast(r.error)
            refresh()
        }
    }

    override fun stop() {
        val wasRecording = _state.value?.recording == true
        edit(Cmd.stop()) { if (wasRecording) toast(str(R.string.msg_recording_saved)) }
    }

    override fun seek(frame: Long) { viewModelScope.launch { call(Cmd.seek(frame.coerceAtLeast(0))) } }

    /** Requiere el permiso RECORD_AUDIO (lo pide la Activity) y una pista armada. */
    override fun record() {
        viewModelScope.launch {
            val s = _state.value ?: return@launch
            val armed = s.tracks.firstOrNull { it.armed && it.isAudio } ?: s.tracks.firstOrNull { it.armed } ?: run { toast(str(R.string.msg_arm_track)); return@launch }
            applyRecordModeNow() // perfil del micrófono y atenuación antes de abrir la entrada
            if (!withContext(control) { NativeEngine.setInputEnabled(true) }) { toast(str(R.string.msg_mic_failed)); return@launch }
            guard.requestFocus()
            val r = call(Cmd.startRecording(armed.id, _countInBars.value))
            if (!r.ok) toast(r.error ?: "")
            refresh()
            refreshAudioStatus()
        }
    }

    override fun setTempo(bpm: Double, beatsPerBar: Int) = edit(Cmd.setTempo(bpm, beatsPerBar))
    override fun setMetronome(on: Boolean) = edit(Cmd.setMetronome(on))
    fun setLatencyOffsetMs(ms: Double) = edit(Cmd.setLatencyOffset(Math.round(ms * 48.0)))
    override fun undo() = edit(Cmd.undo())
    override fun redo() = edit(Cmd.redo())

    // --- Pistas -------------------------------------------------------------------------------------------------
    fun addTrack(name: String) = edit(Cmd.addTrack(name))
    override fun addTrack(name: String, kind: String) = edit(Cmd.addTrack(name, kind)) { r -> r.id?.let { _selectedTrack.value = it } }
    override fun selectTrack(id: String) { _selectedTrack.value = id }
    override fun openPanel(panel: Panel) { _panel.value = panel }
    override fun setMasterLive(db: Float) { viewModelScope.launch { call(Cmd.setMaster(db)) } }
    override fun setLoop(on: Boolean, startTick: Long, endTick: Long) = edit(Cmd.setLoop(on, startTick, endTick))

    // --- MIDI y batería ---------------------------------------------------------------------------------------
    override fun addMidiClip(trackId: String, startTick: Long, lengthTick: Long, patternId: String?) =
        edit(Cmd.addMidiClip(trackId, startTick, lengthTick, patternId)) { r -> _selectedClip.value = r.id }
    override fun moveMidiClip(id: String, startTick: Long, trackId: String?) = edit(Cmd.moveMidiClip(id, startTick, trackId))
    override fun resizeMidiClip(id: String, startTick: Long, lengthTick: Long) = edit(Cmd.resizeMidiClip(id, startTick, lengthTick))
    override fun deleteMidiClip(id: String) = edit(Cmd.deleteMidiClip(id)) { if (_selectedClip.value == id) _selectedClip.value = null }
    override fun duplicateMidiClip(id: String) = edit(Cmd.duplicateMidiClip(id)) { r -> _selectedClip.value = r.id }
    override fun setClipPattern(id: String, patternId: String) = edit(Cmd.setClipPattern(id, patternId))
    override fun setClipNotes(clipId: String, notes: List<NoteState>) = edit(Cmd.setClipNotes(clipId, notes))
    override fun quantizeClip(clipId: String, gridTicks: Long) = edit(Cmd.quantizeClip(clipId, gridTicks))
    override fun setStep(trackId: String, patternId: String, lane: Int, step: Int, velocity: Int) = edit(Cmd.setStep(trackId, patternId, lane, step, velocity))
    override fun addPattern(trackId: String, copyFrom: String?) = edit(Cmd.addPattern(trackId, copyFrom))
    override fun setPatternLength(trackId: String, patternId: String, steps: Int) = edit(Cmd.setPatternLength(trackId, patternId, steps))
    override fun setSwing(trackId: String, patternId: String, swing: Float) = edit(Cmd.setSwing(trackId, patternId, swing))
    override fun setPatternLanes(trackId: String, patternId: String, lanes: List<List<Int>>) = edit(Cmd.setPatternLanes(trackId, patternId, lanes))
    override fun setRoll(trackId: String, patternId: String, lane: Int, step: Int, roll: Int) = edit(Cmd.setRoll(trackId, patternId, lane, step, roll))
    override fun varyPart(part: String, seed: Long, energy: Float?) =
        variation(Cmd.varyPart(part, seed, energy), if (energy != null) tr("Cambiando la intensidad…", "Changing intensity…") else tr("Rehaciendo…", "Redoing…"))
    override fun setPartLocked(trackId: String, locked: Boolean) = edit(Cmd.setPartLocked(trackId, locked))
    override fun setHumanize(amount: Float) = edit(Cmd.setHumanize(amount))

    /** 0.10: resultado de la medición de VocalShift en vivo (null = sin medir, "…" = midiendo). Solo diagnóstico. */
    private val _liveBench = MutableStateFlow<String?>(null)
    val liveBench: StateFlow<String?> = _liveBench
    fun runLiveBench() {
        if (_liveBench.value == "…") return
        _liveBench.value = "…"
        viewModelScope.launch {
            val r = call(Cmd.benchLiveVocal(5))
            _liveBench.value = if (!r.ok) (r.error ?: "error") else LiveBench.describe(r.raw)
        }
    }
    override fun mutatePart(part: String, seed: Long) = variation(Cmd.mutatePart(part, seed), tr("Creando una variación…", "Creating a variation…"))
    override fun regenerateBeat(seed: Long) = variation(Cmd.regenerateBeat(seed), tr("Componiendo otra idea…", "Composing another idea…"))

    /**
     * Hotfix 0.9.2: Variar/Regenerar ya no son una edición muda. Se muestra que se está trabajando, el motor devuelve
     * un informe (versión y partes cambiadas) que la interfaz enseña y se confirma con un mensaje. Si el motor no pudo
     * crear una versión distinta (todo bloqueado, parte sin cambios posibles) el mensaje lo explica.
     */
    private fun variation(cmd: String, working: String) {
        if (_plugins.value.variationWorking != null) return // ignora pulsaciones repetidas mientras trabaja
        _plugins.value = _plugins.value.copy(variationWorking = working)
        viewModelScope.launch {
            try {
                val r = call(cmd)
                refresh()
                if (!r.ok) { toast(r.error ?: tr("No se pudo crear la variación", "Could not create the variation")); return@launch }
                val rep = com.caucesound.app.data.PluginParser.variation(r.raw)
                _plugins.value = _plugins.value.copy(variation = rep)
                val names = rep?.changed?.joinToString(", ") { it.name } ?: ""
                val playing = withContext(control) { NativeEngine.isPlaying() }
                toast(
                    tr("Versión ${rep?.revision ?: ""} lista", "Version ${rep?.revision ?: ""} ready") + (if (names.isNotEmpty()) tr(": cambió $names", ": changed $names") else "") +
                        if (playing) tr(". Ya la estás escuchando.", ". You're hearing it now.") else tr(". Pulsa Escuchar.", ". Tap Listen.")
                )
            } finally {
                _plugins.value = _plugins.value.copy(variationWorking = null)
            }
        }
    }

    override fun listenVariation() {
        viewModelScope.launch {
            val s = _state.value ?: return@launch
            if (withContext(control) { NativeEngine.isPlaying() }) return@launch
            // Desde el comienzo de la sección A (compás 3): la intro tiene pocas partes y se oiría poco el cambio.
            val bars = if (s.music != null) 2 else 0
            val frame = (bars * s.beatsPerBar * 60.0 * SAMPLE_RATE / s.bpm).toLong()
            call(Cmd.seek(frame))
            play()
        }
    }

    // --- Plugins: VocalShift y Solo AI ---------------------------------------------------------------------------
    private fun clearPluginUi() {
        jobPoll?.cancel()
        _plugins.value = com.caucesound.app.data.PluginUi(vocalPresets = _plugins.value.vocalPresets)
    }

    /** Sigue un trabajo offline hasta su estado final. El progreso es el real que publica el motor. */
    private fun followJob(jobId: String, onDone: suspend (JSONObject) -> Unit) {
        jobPoll?.cancel()
        jobPoll = viewModelScope.launch {
            while (isActive) {
                val r = call(Cmd.jobStatus(jobId))
                if (!r.ok) { _plugins.value = _plugins.value.copy(job = null); return@launch }
                val job = com.caucesound.app.data.PluginParser.job(r.raw)
                _plugins.value = _plugins.value.copy(job = job)
                when (job.state) {
                    "done" -> { onDone(r.raw.optJSONObject("result") ?: JSONObject()); return@launch }
                    "failed" -> { toast(job.error ?: tr("El proceso falló", "The process failed")); refresh(); return@launch }
                    "cancelled" -> { toast(tr("Proceso cancelado: el clip queda como estaba", "Process cancelled: the clip is unchanged")); refresh(); return@launch }
                }
                delay(120)
            }
        }
    }

    override fun vocalProcess(clipId: String, params: com.caucesound.app.data.VocalParams) {
        viewModelScope.launch {
            val r = call(Cmd.vocalProcess(clipId, params))
            if (!r.ok) { toast(r.error ?: ""); return@launch }
            _plugins.value = _plugins.value.copy(job = com.caucesound.app.data.JobUi(r.raw.optString("jobId"), "vocalshift", clipId, "queued", 0f), vocalResult = null)
            followJob(r.raw.optString("jobId")) { res ->
                _plugins.value = _plugins.value.copy(vocalResult = com.caucesound.app.data.PluginParser.vocalResult(res))
                // La comparación empieza a volumen igualado (solo de escucha; se apaga al salir de la pantalla).
                call(Cmd.clipVersion(clipId, false, true))
                refresh()
                toast(tr("Voz procesada. Compara original y procesada.", "Voice processed. Compare original and processed."))
            }
        }
    }

    override fun vocalPreview(clipId: String, params: com.caucesound.app.data.VocalParams, from: Long, length: Long) {
        viewModelScope.launch {
            val r = call(Cmd.vocalPreview(clipId, params, from, length))
            if (!r.ok) { toast(r.error ?: ""); return@launch }
            suspend fun listen() {
                val on = call(Cmd.previewListen(clipId, true))
                if (!on.ok) { toast(on.error ?: ""); refresh(); return }
                refresh()
                val c = _state.value?.clip(clipId)?.second ?: return
                call(Cmd.stop()); call(Cmd.seek(c.start + from)); play()
            }
            if (r.raw.optBoolean("cached")) { listen(); return@launch } // nada cambió: no se repite el cálculo
            _plugins.value = _plugins.value.copy(job = com.caucesound.app.data.JobUi(r.raw.optString("jobId"), "vocalPreview", clipId, "queued", 0f))
            followJob(r.raw.optString("jobId")) { listen() }
        }
    }
    override fun previewListen(clipId: String, on: Boolean) = edit(Cmd.previewListen(clipId, on))

    override fun cancelJob() { _plugins.value.job?.takeIf { it.active }?.let { j -> viewModelScope.launch { call(Cmd.jobCancel(j.id)) } } }
    override fun setClipVersion(clipId: String, original: Boolean, match: Boolean?) = edit(Cmd.clipVersion(clipId, original, match))
    override fun discardProcess(clipId: String) = edit(Cmd.discardProcess(clipId)) { _plugins.value = _plugins.value.copy(vocalResult = null) }
    override fun auditionClip(clipId: String) {
        val c = _state.value?.clip(clipId)?.second ?: return
        viewModelScope.launch { call(Cmd.stop()); call(Cmd.seek(c.start)); play() }
    }

    override fun soloAnalyze(clipId: String) {
        viewModelScope.launch {
            val r = call(Cmd.soloAnalyze(clipId))
            if (!r.ok) { toast(r.error ?: ""); return@launch }
            _plugins.value = _plugins.value.copy(job = com.caucesound.app.data.JobUi(r.raw.optString("jobId"), "soloAnalyze", clipId, "queued", 0f), analysis = null)
            followJob(r.raw.optString("jobId")) { res -> _plugins.value = _plugins.value.copy(analysis = com.caucesound.app.data.BeatAnalysis.parse(res)) }
        }
    }

    override fun confirmAnalysis(keyRoot: Int, minor: Boolean, bpm: Double?, useChords: Boolean) {
        viewModelScope.launch {
            val a = _plugins.value.analysis
            val s = _state.value ?: return@launch
            // El tempo solo cambia si el usuario lo pidió expresamente (bpm != null).
            if (bpm != null && bpm != s.bpm) { val t = call(Cmd.setTempo(bpm, s.beatsPerBar)); if (!t.ok) { toast(t.error ?: ""); refresh(); return@launch } }
            val chords = if (useChords && a != null && bpm != null) a.chords else emptyList()
            val r = call(Cmd.setHarmony(keyRoot, minor, chords, bpm ?: 0.0, if (a != null) "analysis" else "manual"))
            if (!r.ok) toast(r.error ?: "") else toast(tr("Tonalidad guardada: ", "Key saved: ") + com.caucesound.app.data.KeyNames.name(keyRoot) + if (minor) tr(" menor", " minor") else tr(" mayor", " major"))
            refresh()
        }
    }

    override fun dismissAnalysis() { _plugins.value = _plugins.value.copy(analysis = null) }

    override fun soloProposals(request: com.caucesound.app.data.SoloRequest) {
        viewModelScope.launch {
            val r = call(Cmd.soloProposals(request))
            if (!r.ok) { toast(if (r.error == "key_required") tr("Elige primero la tonalidad de la canción", "Pick the song key first") else (r.error ?: "")); return@launch }
            val fam = com.caucesound.app.data.SoloFamily.parse(request, r.raw)
            // La alternativa A entra en una pista nueva para poder escucharla; las demás la sustituyen al elegirlas.
            val a = call(Cmd.soloPick(request, 0, 0, null))
            if (!a.ok) { toast(a.error ?: ""); return@launch }
            _plugins.value = _plugins.value.copy(family = fam.copy(selected = 0, trackId = a.id))
            refresh()
            a.id?.let { _selectedTrack.value = it }
        }
    }

    private fun soloApply(index: Int, variation: Long) {
        viewModelScope.launch {
            val fam = _plugins.value.family ?: return@launch
            // Si la pista se borró o se deshizo, se crea de nuevo en vez de fallar.
            val target = fam.trackId?.takeIf { id -> _state.value?.tracks?.any { it.id == id } == true }
            val r = call(fam.pickCommand(index, variation, _state.value?.tracks?.map { it.id }?.toSet() ?: emptySet()))
            if (!r.ok) { toast(r.error ?: ""); refresh(); return@launch }
            _plugins.value = _plugins.value.copy(family = fam.copy(selected = index, trackId = r.id ?: target, variation = variation))
            refresh()
        }
    }
    private fun soloTrack(): String? = _plugins.value.family?.trackId?.takeIf { id -> _state.value?.tracks?.any { it.id == id } == true }
    override fun soloTransform(op: String) { soloTrack()?.let { id -> edit(Cmd.soloTransform(id, op)) } }
    override fun soloNewEnding() {
        val fam = _plugins.value.family?.takeIf { it.selected >= 0 } ?: return
        val id = soloTrack() ?: return
        edit(Cmd.soloEnding(fam.request, fam.selected, fam.variation, id, kotlin.random.Random.nextLong(1, 0x7FFFFFFFL)))
    }
    override fun soloPick(index: Int) = soloApply(index, 0)
    override fun applyFxChain(trackId: String, presetId: String, replace: Boolean) = edit(Cmd.applyFxChain(trackId, presetId, replace))
    override fun routeDrumLane(trackId: String, lane: Int, own: Boolean) = edit(Cmd.routeDrumLane(trackId, lane, own))
    override fun soloVary() { _plugins.value.family?.takeIf { it.selected >= 0 }?.let { soloApply(it.selected, kotlin.random.Random.nextLong(1, 0x7FFFFFFFL)) } }

    /** Parámetros de instrumento: se aplican al instante; el estado (y la interfaz) se refresca al terminar. */
    override fun setParam(trackId: String, param: String, value: Float) {
        viewModelScope.launch {
            val r = call(Cmd.setInstrumentParam(trackId, param, value))
            if (!r.ok && r.error != null) toast(r.error)
            // Botones (kit, mute, solo de carril): cada pulsación es su propia entrada de deshacer.
            if (param == "kit" || param.startsWith("mute") || param.startsWith("solo")) { call(Cmd.endParamGesture()); refresh() }
        }
    }
    override fun addInsert(trackId: String, type: String) = edit(Cmd.addInsert(trackId, type))
    override fun removeInsert(insertId: String) = edit(Cmd.removeInsert(insertId))
    override fun setInsertParam(insertId: String, param: String, value: Float) {
        viewModelScope.launch { val r = call(Cmd.setInsertParam(insertId, param, value)); if (!r.ok && r.error != null) toast(r.error) }
    }
    override fun setInsertBypass(insertId: String, bypass: Boolean) = edit(Cmd.setInsertBypass(insertId, bypass))
    override fun moveInsert(insertId: String, index: Int) = edit(Cmd.moveInsert(insertId, index))
    fun commitParams() { viewModelScope.launch { refresh() } }
    override fun loadPreset(trackId: String, presetId: String) = edit(Cmd.loadPreset(trackId, presetId))

    // Favoritos de presets: preferencia del usuario en este dispositivo (no viaja con el proyecto ni cambia los presets).
    private val prefs = app.getSharedPreferences("cauce_prefs", android.content.Context.MODE_PRIVATE)
    private val _favoritePresets = MutableStateFlow(prefs.getStringSet("favoritePresets", emptySet())!!.toSet())
    val favoritePresets: StateFlow<Set<String>> = _favoritePresets
    // Cuenta previa antes de grabar (0, 1 o 2 compases): preferencia del dispositivo, no del proyecto.
    private val _countInBars = MutableStateFlow(prefs.getInt("countInBars", 1).coerceIn(0, 2))
    val countInBars: StateFlow<Int> = _countInBars
    fun setCountInBars(bars: Int) { _countInBars.value = bars.coerceIn(0, 2); prefs.edit().putInt("countInBars", _countInBars.value).apply() }

    // 0.8 — Modo de grabación (preferencia del dispositivo, no del proyecto).
    //  Estudio (audífonos): micrófono sin procesar (VoicePerformance), sin atenuar nada.
    //  Bocinas: mientras se graba, la salida en vivo baja duckDb (no los faders ni la exportación), la monitorización
    //  se apaga (evita acoples) y, si se pide, el teléfono aplica su cancelación de eco (VoiceCommunication).
    private val _speakerMode = MutableStateFlow(prefs.getBoolean("speakerMode", false))
    val speakerMode: StateFlow<Boolean> = _speakerMode
    private val _echoCancel = MutableStateFlow(prefs.getBoolean("echoCancel", false))
    val echoCancel: StateFlow<Boolean> = _echoCancel
    private val _duckDb = MutableStateFlow(prefs.getFloat("duckDb", -10f).coerceIn(-24f, 0f))
    val duckDb: StateFlow<Float> = _duckDb
    fun setSpeakerMode(on: Boolean) {
        _speakerMode.value = on
        prefs.edit().putBoolean("speakerMode", on).apply()
        if (on && _monitoring.value) setMonitoring(false)
        applyRecordMode()
    }
    fun setEchoCancel(on: Boolean) { _echoCancel.value = on; prefs.edit().putBoolean("echoCancel", on).apply(); applyRecordMode() }
    fun setDuckDb(db: Float) { _duckDb.value = db.coerceIn(-24f, 0f); prefs.edit().putFloat("duckDb", _duckDb.value).apply(); applyRecordMode() }
    // 0.9.2 — «Grabar sin acompañamiento»: el beat y el metrónomo se callan SOLO durante la toma (setRecordDuck -120
    // en el motor). No cambia faders, mutes ni el proyecto; la cuenta previa sigue sonando. Preferencia del teléfono.
    private val _recordSolo = MutableStateFlow(prefs.getBoolean("recordSolo", false))
    val recordSolo: StateFlow<Boolean> = _recordSolo
    fun setRecordSolo(on: Boolean) { _recordSolo.value = on; prefs.edit().putBoolean("recordSolo", on).apply() }
    private suspend fun applyRecordModeNow() {
        withContext(control) { NativeEngine.setEchoCancel(_speakerMode.value && _echoCancel.value) }
        call(Cmd.setRecordDuck(if (_recordSolo.value) -120.0 else if (_speakerMode.value) _duckDb.value.toDouble() else 0.0))
    }

    /** ¿Hay una pista de audio armada? Si no, la interfaz ofrece crearla (grabación con un toque, 0.9.2). */
    fun hasArmedTrack(): Boolean = _state.value?.tracks?.any { it.armed && it.isAudio } == true

    /**
     * Grabación con un toque (0.9.2, mismo enfoque que «Idea rápida»): sin pista armada, crea una pista de voz armada
     * (o arma la elegida) y empieza a grabar. solo = sin acompañamiento. El modo multipista no cambia: con una pista ya
     * armada, el botón rojo graba directamente como siempre.
     */
    fun recordQuick(existingTrackId: String?, solo: Boolean, trackName: String) {
        setRecordSolo(solo)
        viewModelScope.launch {
            val s = _state.value ?: return@launch
            val id = existingTrackId ?: run {
                val n = s.tracks.count { it.isAudio } + 1
                val r = call(Cmd.addTrack(if (n <= 1) trackName else "$trackName $n", "audio"))
                if (!r.ok || r.id == null) { toast(r.error ?: tr("No se pudo crear la pista", "Could not create the track")); refresh(); return@launch }
                r.id
            }
            call(Cmd.setArmed(id, true))
            refresh()
            _selectedTrack.value = id
            record()
        }
    }
    private fun applyRecordMode() { viewModelScope.launch { applyRecordModeNow() } }
    override fun toggleFavoritePreset(presetId: String) {
        val next = _favoritePresets.value.let { if (presetId in it) it - presetId else it + presetId }
        _favoritePresets.value = next
        prefs.edit().putStringSet("favoritePresets", next).apply()
    }

    /** Directo al motor (sin pasar por el hilo de control): la latencia de los pads es la del búfer de audio. */

    // --- AutoMix (0.9, prototipo por reglas) -------------------------------------------------------------------
    private val _autoMix = MutableStateFlow(com.caucesound.app.data.AutoMixUi())
    val autoMix: StateFlow<com.caucesound.app.data.AutoMixUi> = _autoMix.asStateFlow()
    override fun autoMixSuggest() {
        _autoMix.value = _autoMix.value.copy(open = true, analyzing = true)
        viewModelScope.launch { _autoMix.value = com.caucesound.app.data.AutoMixParser.parse(withContext(control) { NativeEngine.command(Cmd.autoMixSuggest()) }) }
    }
    fun autoMixApply() {
        val ch = _autoMix.value.suggestions.filter { it.change }
        _autoMix.value = com.caucesound.app.data.AutoMixUi()
        if (ch.isNotEmpty()) edit(Cmd.autoMixApply(ch)) { toast(tr("AutoMix aplicado. Puedes revertirlo desde el Mixer.", "AutoMix applied. You can revert it from the Mixer.")); _autoMixRevertable.value = true }
    }
    private val _autoMixRevertable = MutableStateFlow(false)
    val autoMixRevertable: StateFlow<Boolean> = _autoMixRevertable.asStateFlow()
    override fun autoMixRevert() = edit(Cmd.autoMixRevert()) { _autoMixRevertable.value = false; toast(tr("Mezcla anterior recuperada", "Previous mix restored")) }
    override fun autoMixCompare() = edit(Cmd.autoMixCompare()) { r ->
        toast(if (r.raw.optString("showing") == "before") tr("Suena la mezcla ANTERIOR", "Playing the PREVIOUS mix") else tr("Suena la mezcla de AUTOMIX", "Playing the AUTOMIX mix"))
    }
    fun dismissAutoMix() { _autoMix.value = com.caucesound.app.data.AutoMixUi() }

    // --- Sonidos Cauce (misión 0.9) ------------------------------------------------------------------------------
    private val packsDir = File(app.filesDir, "packs")
    private val _sounds = MutableStateFlow(com.caucesound.app.data.SoundUi(favorites = prefs.getStringSet("favoriteSounds", emptySet())!!.toSet()))
    val sounds: StateFlow<com.caucesound.app.data.SoundUi> = _sounds.asStateFlow()
    private var previewJob: Job? = null

    /** Copia los paquetes de fábrica (si cambiaron) y registra las carpetas en el motor. Antes de abrir proyectos. */
    private suspend fun setupPacks() {
        val app = getApplication<Application>()
        val factory = File(packsDir, "factory")
        val user = File(packsDir, "user").apply { mkdirs() }
        withContext(Dispatchers.IO) { runCatching { com.caucesound.app.audio.FactoryPacks.install(app.assets, factory) } }
        val json = withContext(control) { NativeEngine.command(Cmd.configurePacks(factory.absolutePath, user.absolutePath)) }
        val packs = com.caucesound.app.data.PackParser.packs(json)
        // La compilación declara qué sonidos trae: si no están (APK mal construido, copia interrumpida, poco espacio)
        // se dice con claridad en vez de dejar al usuario con una biblioteca vacía sin explicación.
        val missing = com.caucesound.app.data.FactoryPackCheck.missing(com.caucesound.app.BuildConfig.FACTORY_PACKS, packs)
        _sounds.value = _sounds.value.copy(packs = packs, missingFactory = missing)
        if (missing.isNotEmpty()) toast(tr("Faltan sonidos de fábrica (${missing.size}). Reinstala la app o libera espacio.", "Factory sounds are missing (${missing.size}). Reinstall the app or free up space."))
    }

    private suspend fun refreshPacks() {
        val json = withContext(control) { NativeEngine.command(Cmd.packs()) }
        _sounds.value = _sounds.value.copy(packs = com.caucesound.app.data.PackParser.packs(json))
    }

    override fun previewSound(key: String) {
        previewJob?.cancel()
        previewJob = viewModelScope.launch {
            val r = call(Cmd.previewInstrument(key))
            val slot = r.raw.optInt("slot", -1)
            if (!r.ok || slot < 0) { toast(r.error ?: tr("No se pudo escuchar", "Could not preview")); return@launch }
            _sounds.value = _sounds.value.copy(previewing = key)
            val info = _sounds.value.instrument(key)
            try {
                // Frase corta y fija para comparar sonidos en igualdad: kit = bombo/charles/caja; melódico = arpegio.
                val steps: List<Pair<List<Int>, Int>> = if (info?.isDrumKit == true)
                    listOf(listOf(36, 42) to 100, listOf(42) to 70, listOf(38, 42) to 110, listOf(42) to 70, listOf(36, 42) to 100, listOf(36) to 85, listOf(38, 42) to 115, listOf(46) to 80)
                else {
                    val k = (info?.previewKey ?: 60).coerceIn(12, 108)
                    if (info?.mono == true) listOf(listOf(k) to 100, listOf(k) to 70, listOf(k + 7) to 95, listOf(k + 12) to 110, listOf(k + 10) to 90, listOf(k + 7) to 80, listOf(k + 3) to 95, listOf(k) to 115)
                    else listOf(listOf(k, k + 4, k + 7) to 95, listOf(k + 12) to 80, listOf(k - 3, k + 4, k + 9) to 90, listOf(k + 7) to 75, listOf(k - 7, k, k + 5) to 100, listOf(k + 9) to 80, listOf(k - 5, k + 2, k + 7) to 110)
                }
                for ((keys, vel) in steps) {
                    keys.forEach { liveNote(slot, it, vel, true) }
                    delay(if (info?.isDrumKit == true) 180 else 330)
                    keys.forEach { liveNote(slot, it, 0, false) }
                }
                delay(900)
            } finally {
                if (_sounds.value.previewing == key) _sounds.value = _sounds.value.copy(previewing = null)
            }
        }
    }

    override fun stopSoundPreview() {
        previewJob?.cancel()
        viewModelScope.launch { call(Cmd.previewInstrument("")); _sounds.value = _sounds.value.copy(previewing = null) }
    }

    override fun loadSound(trackId: String, key: String) {
        val t = _state.value?.tracks?.firstOrNull { it.id == trackId } ?: return
        val cmd = if (t.isDrums) Cmd.setDrumSampleKit(trackId, key) else Cmd.setSamplerInstrument(trackId, key)
        edit(cmd) { toast(if (key.isEmpty()) tr("Kit sintetizado restaurado", "Synth kit restored") else tr("Sonido cargado: ", "Sound loaded: ") + (_sounds.value.instrument(key)?.name ?: key)) }
    }

    override fun toggleFavoriteSound(key: String) {
        val next = _sounds.value.favorites.let { if (key in it) it - key else it + key }
        _sounds.value = _sounds.value.copy(favorites = next)
        prefs.edit().putStringSet("favoriteSounds", next).apply()
    }

    override fun askRemovePack(packId: String) {
        val p = _sounds.value.packs.firstOrNull { it.id == packId } ?: return
        viewModelScope.launch {
            val used = com.caucesound.app.data.PackParser.usage(withContext(control) { NativeEngine.command(Cmd.packUsage(projectsRoot.absolutePath, packId)) })
            _sounds.value = _sounds.value.copy(removeAsk = p to used)
        }
    }

    fun dismissRemovePack() { _sounds.value = _sounds.value.copy(removeAsk = null) }

    fun confirmRemovePack() {
        val p = _sounds.value.removeAsk?.first ?: return
        _sounds.value = _sounds.value.copy(removeAsk = null)
        viewModelScope.launch {
            val r = call(Cmd.removePack(p.id))
            toast(if (r.ok) tr("Paquete borrado", "Pack deleted") else (r.error ?: ""))
            refreshPacks()
            refresh()
        }
    }

    fun importPack(source: android.net.Uri) {
        viewModelScope.launch {
            val app = getApplication<Application>()
            _sounds.value = _sounds.value.copy(importing = true)
            val tmp = File(app.cacheDir, "pack_${System.currentTimeMillis()}.zip")
            try {
                val copied = withContext(Dispatchers.IO) {
                    runCatching { app.contentResolver.openInputStream(source)!!.use { inp -> tmp.outputStream().use { inp.copyTo(it, 1 shl 16) } } }.isSuccess
                }
                if (!copied) { toast(tr("No se pudo leer el archivo", "Could not read the file")); return@launch }
                var res = com.caucesound.app.data.PackParser.import(withContext(control) { NativeEngine.command(Cmd.importPack(tmp.absolutePath, false)) })
                // Misma versión o anterior ya instalada: se ofrece sustituirla solo si es una versión nueva (el motor lo decide).
                if (!res.ok && res.error?.contains("ya está instalad") == true) res = com.caucesound.app.data.PackParser.import(withContext(control) { NativeEngine.command(Cmd.importPack(tmp.absolutePath, true)) })
                toast(if (res.ok) tr("Paquete instalado: ", "Pack installed: ") + "${res.packId} ${res.version}" else tr("No se instaló: ", "Not installed: ") + (res.error ?: ""))
                refreshPacks()
                refresh() // un proyecto con sonidos que faltaban los recupera al reabrir
            } finally {
                withContext(Dispatchers.IO) { tmp.delete() }
                _sounds.value = _sounds.value.copy(importing = false)
            }
        }
    }

    override fun liveNote(slot: Int, key: Int, velocity: Int, on: Boolean) {
        NativeEngine.liveMidi(slot, if (on) 0x90 else 0x80, key, if (on) velocity.coerceIn(1, 127) else 0)
    }
    fun removeTrack(id: String) = edit(Cmd.removeTrack(id))
    fun renameTrack(id: String, name: String) = edit(Cmd.renameTrack(id, name))
    override fun setMute(id: String, on: Boolean) = edit(Cmd.setMute(id, on))
    override fun setSolo(id: String, on: Boolean) = edit(Cmd.setSolo(id, on))
    override fun setArmed(id: String, on: Boolean) = edit(Cmd.setArmed(id, on))

    /** Deslizadores: se aplican al instante; el estado se refresca al soltar. */
    override fun setVolumeLive(id: String, db: Float) { viewModelScope.launch { call(Cmd.setVolume(id, db)) } }
    override fun setPanLive(id: String, pan: Float) { viewModelScope.launch { call(Cmd.setPan(id, pan)) } }
    override fun setSendLive(trackId: String, busId: String, level: Float) {
        viewModelScope.launch { call(Cmd.setSend(trackId, busId, level)) }
    }
    override fun addReverbBus() {
        viewModelScope.launch {
            val bus = call(Cmd.addTrack(str(R.string.bus_reverb_name), "bus"))
            if (!bus.ok || bus.id == null) { toast(bus.error ?: ""); refresh(); return@launch }
            val rev = call(Cmd.addInsert(bus.id, "cs.reverb"))
            if (rev.ok && rev.id != null) call(Cmd.setInsertParam(rev.id, "mix", 1f))
            call(Cmd.endParamGesture())
            refresh()
        }
    }
    /** Al soltar una perilla o deslizador: cierra el gesto (una entrada de deshacer por giro) y refresca. */
    override fun commitMixer() { viewModelScope.launch { call(Cmd.endParamGesture()); refresh() } }

    // --- Clips --------------------------------------------------------------------------------------------------
    override fun selectClip(id: String?) { _selectedClip.value = id }
    override fun moveClip(id: String, start: Long, trackId: String?) = edit(Cmd.moveClip(id, start, trackId))
    override fun trimClip(id: String, start: Long, length: Long) = edit(Cmd.trimClip(id, start, length))
    override fun splitSelectedAt(frame: Long) { _selectedClip.value?.let { id -> edit(Cmd.splitClip(id, frame)) { r -> _selectedClip.value = r.id } } }
    override fun deleteSelected() { _selectedClip.value?.let { id -> edit(Cmd.deleteClip(id)) { _selectedClip.value = null } } }
    override fun duplicateSelected() { _selectedClip.value?.let { id -> edit(Cmd.duplicateClip(id)) { r -> _selectedClip.value = r.id } } }
    fun setFades(id: String, fadeIn: Long, fadeOut: Long) = edit(Cmd.setFades(id, fadeIn, fadeOut))
    fun setClipGain(id: String, db: Float) = edit(Cmd.setClipGain(id, db))

    // --- Importar / exportar ------------------------------------------------------------------------------------
    fun importAudio(uri: Uri, trackId: String, displayName: String) {
        viewModelScope.launch {
            _busy.value = str(R.string.busy_importing)
            val tmp = File(getApplication<Application>().cacheDir, "import_${System.nanoTime()}.wav")
            try {
                withContext(Dispatchers.IO) { AudioDecoder.toWav(getApplication(), uri, tmp) }
                val r = call(Cmd.importWav(tmp.absolutePath, trackId, NativeEngine.position(), displayName))
                if (!r.ok) toast(r.error ?: "") else _selectedClip.value = r.id
                refresh()
            } catch (e: Exception) {
                toast(str(R.string.msg_import_failed, e.message ?: e.javaClass.simpleName))
            } finally {
                tmp.delete()
                _busy.value = null
            }
        }
    }

    fun exportWav(uri: Uri, bits: Int) {
        viewModelScope.launch {
            _busy.value = str(R.string.busy_exporting)
            val tmp = File(getApplication<Application>().cacheDir, "export_${System.nanoTime()}.wav")
            try {
                val r = call(Cmd.export(tmp.absolutePath, bits))
                if (!r.ok) { toast(r.error ?: ""); return@launch }
                withContext(Dispatchers.IO) {
                    getApplication<Application>().contentResolver.openOutputStream(uri, "w")!!.use { out -> tmp.inputStream().use { it.copyTo(out) } }
                }
                val clipped = r.raw.optLong("clippedSamples")
                toast(if (clipped > 0) str(R.string.msg_export_clipped, clipped) else str(R.string.msg_exported))
            } catch (e: Exception) {
                toast(str(R.string.msg_export_failed, e.message ?: ""))
            } finally {
                tmp.delete()
                _busy.value = null
            }
        }
    }

    /**
     * Paquete para DAW. El motor lo crea en segundo plano en la caché de la app (no bloquea la interfaz ni el audio) y,
     * cuando termina, se copia al documento que eligió el usuario. Si se cancela o falla, se borra ese documento para
     * no dejar un ZIP vacío. El proyecto no se modifica.
     */
    private var exportPoll: Job? = null
    fun exportDaw(uri: Uri, wet: Boolean) {
        if (_plugins.value.export?.active == true) return
        exportPoll = viewModelScope.launch {
            val app = getApplication<Application>()
            fun setExport(e: com.caucesound.app.data.DawExportUi?) { _plugins.value = _plugins.value.copy(export = e) }
            fun dropTarget() { runCatching { android.provider.DocumentsContract.deleteDocument(app.contentResolver, uri) } }
            val dir = File(app.cacheDir, "daw-export")
            withContext(Dispatchers.IO) { dir.deleteRecursively(); dir.mkdirs() }
            val tmp = File(dir, "package.zip")
            try {
                // Espacio: los WAV temporales, el ZIP y la copia final conviven un momento (24 bits estéreo por pista).
                val s = _state.value
                if (s != null) {
                    val need = (s.contentEnd + 10L * com.caucesound.app.data.SAMPLE_RATE) * 6L * (s.tracks.size + 1) * 3L
                    val free = withContext(Dispatchers.IO) { app.cacheDir.usableSpace }
                    if (free < need) {
                        dropTarget()
                        setExport(com.caucesound.app.data.DawExportUi("failed", wet = wet, error = tr(
                            "No hay espacio suficiente: hacen falta unos ${need / 1048576} MB libres y hay ${free / 1048576} MB.",
                            "Not enough space: about ${need / 1048576} MB are needed and ${free / 1048576} MB are free.")))
                        return@launch
                    }
                }
                val r = call(Cmd.exportDaw(tmp.absolutePath, wet))
                if (!r.ok) { dropTarget(); setExport(com.caucesound.app.data.DawExportUi("failed", wet = wet, error = r.error)); return@launch }
                val jobId = r.raw.optString("jobId")
                setExport(com.caucesound.app.data.DawExportUi("running", jobId, 0f, wet))
                var result: JSONObject? = null
                while (isActive && result == null) {
                    val st = call(Cmd.jobStatus(jobId))
                    val job = if (st.ok) com.caucesound.app.data.PluginParser.job(st.raw) else null
                    when (job?.state) {
                        "done" -> result = st.raw.optJSONObject("result") ?: JSONObject()
                        "cancelled" -> { dropTarget(); setExport(com.caucesound.app.data.DawExportUi("cancelled", wet = wet)); return@launch }
                        null, "failed" -> { dropTarget(); setExport(com.caucesound.app.data.DawExportUi("failed", wet = wet, error = job?.error)); return@launch }
                        else -> { setExport(com.caucesound.app.data.DawExportUi("running", jobId, job.progress, wet)); delay(150) }
                    }
                }
                val res = result ?: return@launch
                setExport(com.caucesound.app.data.DawExportUi("saving", jobId, 1f, wet))
                withContext(Dispatchers.IO) { app.contentResolver.openOutputStream(uri, "w")!!.use { out -> tmp.inputStream().use { it.copyTo(out, 1 shl 16) } } }
                setExport(com.caucesound.app.data.DawExportUi("done", jobId, 1f, wet, res.optInt("stems"), res.optInt("midiNotes"), res.optInt("files"),
                    res.optLong("bytes"), res.optLong("clippedSamples")))
            } catch (e: kotlinx.coroutines.CancellationException) {
                throw e
            } catch (e: Exception) {
                dropTarget()
                setExport(com.caucesound.app.data.DawExportUi("failed", wet = wet, error = e.message ?: e.javaClass.simpleName))
            } finally {
                withContext(kotlinx.coroutines.NonCancellable + Dispatchers.IO) { dir.deleteRecursively() }
            }
        }
    }
    fun cancelExport() { _plugins.value.export?.takeIf { it.phase == "running" }?.let { e -> viewModelScope.launch { call(Cmd.jobCancel(e.jobId)) } } }
    fun dismissExport() { if (_plugins.value.export?.active != true) _plugins.value = _plugins.value.copy(export = null) }

    fun exportMidi(uri: Uri) {
        viewModelScope.launch {
            val app = getApplication<Application>()
            val tmp = File(app.cacheDir, "export_${System.nanoTime()}.mid")
            try {
                val r = call(Cmd.exportMidi(tmp.absolutePath))
                if (!r.ok) { runCatching { android.provider.DocumentsContract.deleteDocument(app.contentResolver, uri) }; toast(r.error ?: ""); return@launch }
                withContext(Dispatchers.IO) { app.contentResolver.openOutputStream(uri, "w")!!.use { out -> tmp.inputStream().use { it.copyTo(out) } } }
                toast(tr("MIDI exportado (${r.raw.optInt("notes")} notas)", "MIDI exported (${r.raw.optInt("notes")} notes)"))
            } catch (e: Exception) {
                toast(str(R.string.msg_export_failed, e.message ?: ""))
            } finally {
                tmp.delete()
            }
        }
    }

    // --- Formas de onda -----------------------------------------------------------------------------------------
    // Resumen fijo por fuente (mín/máx cada WAVE_BLOCK frames), pedido en el hilo de control y dibujado
    // localmente: la interfaz nunca espera al motor mientras pinta.
    private val _waveforms = MutableStateFlow<Map<String, FloatArray>>(emptyMap())
    val waveforms: StateFlow<Map<String, FloatArray>> = _waveforms.asStateFlow()

    private suspend fun loadWaveforms(s: ProjectState) {
        val have = _waveforms.value
        val missing = s.sources.values.filter { it.id !in have && it.frames > 0 }
        if (missing.isEmpty()) return
        val loaded = withContext(control) {
            missing.associate { src ->
                val buckets = ((src.frames + WAVE_BLOCK - 1) / WAVE_BLOCK).toInt().coerceAtLeast(1)
                src.id to NativeEngine.peaks(src.id, 0, src.frames, buckets)
            }
        }
        _waveforms.value = have + loaded
    }

    override fun onCleared() {
        guard.unregister()
        // viewModelScope ya está cancelado aquí: cerramos (y guardamos) en un hilo propio.
        Thread { NativeEngine.command(Cmd.closeProject()) }.start()
        super.onCleared()
    }
}
