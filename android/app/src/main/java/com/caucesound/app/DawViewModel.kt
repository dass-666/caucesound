package com.caucesound.app

import android.app.Application
import android.net.Uri
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.caucesound.app.audio.AudioDecoder
import com.caucesound.app.audio.AudioGuard
import com.caucesound.app.audio.NativeEngine
import com.caucesound.app.data.Cmd
import com.caucesound.app.data.CommandResult
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.ProjectSummary
import com.caucesound.app.data.StateParser
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

class DawViewModel(app: Application) : AndroidViewModel(app) {
    private val control = Dispatchers.IO.limitedParallelism(1)
    val projectsRoot = File(app.filesDir, "projects").apply { mkdirs() }

    private val _projects = MutableStateFlow<List<ProjectSummary>>(emptyList())
    val projects: StateFlow<List<ProjectSummary>> = _projects.asStateFlow()
    private val _state = MutableStateFlow<ProjectState?>(null)
    val state: StateFlow<ProjectState?> = _state.asStateFlow()
    private val _selectedClip = MutableStateFlow<String?>(null)
    val selectedClip: StateFlow<String?> = _selectedClip.asStateFlow()
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

    fun createProject(name: String, defaultTrackName: (Int) -> String) {
        viewModelScope.launch {
            val safe = name.trim().ifEmpty { "Proyecto" }
            val folder = File(projectsRoot, "p_" + System.currentTimeMillis())
            val r = call(Cmd.createProject(folder.absolutePath, safe))
            if (!r.ok) { toast(r.error ?: ""); return@launch }
            for (i in 1..4) call(Cmd.addTrack(defaultTrackName(i)))
            call(Cmd.save())
            onProjectOpened()
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
        _waveforms.value = emptyMap()
        _selectedClip.value = null
        refresh()
        autosave?.cancel()
        autosave = viewModelScope.launch {
            while (isActive) {
                delay(15_000)
                if (_state.value?.dirty == true && _state.value?.recording != true) call(Cmd.save()).let { if (it.ok) refresh() }
            }
        }
    }

    fun closeProject() {
        autosave?.cancel()
        viewModelScope.launch {
            call(Cmd.closeProject()) // guarda si hay cambios y detiene grabación/transporte
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

    fun saveNow() = edit(Cmd.save()) { toast(str(R.string.msg_saved)) }

    // --- Transporte ---------------------------------------------------------------------------------------------
    fun play() {
        guard.requestFocus()
        edit(Cmd.play())
    }

    fun stop() {
        val wasRecording = _state.value?.recording == true
        edit(Cmd.stop()) { if (wasRecording) toast(str(R.string.msg_recording_saved)) }
    }

    fun seek(frame: Long) { viewModelScope.launch { call(Cmd.seek(frame.coerceAtLeast(0))) } }

    /** Requiere el permiso RECORD_AUDIO (lo pide la Activity) y una pista armada. */
    fun record() {
        viewModelScope.launch {
            val s = _state.value ?: return@launch
            val armed = s.tracks.firstOrNull { it.armed } ?: run { toast(str(R.string.msg_arm_track)); return@launch }
            if (!withContext(control) { NativeEngine.setInputEnabled(true) }) { toast(str(R.string.msg_mic_failed)); return@launch }
            guard.requestFocus()
            val r = call(Cmd.startRecording(armed.id))
            if (!r.ok) toast(r.error ?: "")
            refresh()
            refreshAudioStatus()
        }
    }

    fun setTempo(bpm: Double, beatsPerBar: Int) = edit(Cmd.setTempo(bpm, beatsPerBar))
    fun setMetronome(on: Boolean) = edit(Cmd.setMetronome(on))
    fun setLatencyOffsetMs(ms: Double) = edit(Cmd.setLatencyOffset(Math.round(ms * 48.0)))
    fun undo() = edit(Cmd.undo())
    fun redo() = edit(Cmd.redo())

    // --- Pistas -------------------------------------------------------------------------------------------------
    fun addTrack(name: String) = edit(Cmd.addTrack(name))
    fun removeTrack(id: String) = edit(Cmd.removeTrack(id))
    fun renameTrack(id: String, name: String) = edit(Cmd.renameTrack(id, name))
    fun setMute(id: String, on: Boolean) = edit(Cmd.setMute(id, on))
    fun setSolo(id: String, on: Boolean) = edit(Cmd.setSolo(id, on))
    fun setArmed(id: String, on: Boolean) = edit(Cmd.setArmed(id, on))

    /** Deslizadores: se aplican al instante; el estado se refresca al soltar. */
    fun setVolumeLive(id: String, db: Float) { viewModelScope.launch { call(Cmd.setVolume(id, db)) } }
    fun setPanLive(id: String, pan: Float) { viewModelScope.launch { call(Cmd.setPan(id, pan)) } }
    fun commitMixer() { viewModelScope.launch { refresh() } }

    // --- Clips --------------------------------------------------------------------------------------------------
    fun selectClip(id: String?) { _selectedClip.value = id }
    fun moveClip(id: String, start: Long, trackId: String?) = edit(Cmd.moveClip(id, start, trackId))
    fun trimClip(id: String, start: Long, length: Long) = edit(Cmd.trimClip(id, start, length))
    fun splitSelectedAt(frame: Long) { _selectedClip.value?.let { id -> edit(Cmd.splitClip(id, frame)) { r -> _selectedClip.value = r.id } } }
    fun deleteSelected() { _selectedClip.value?.let { id -> edit(Cmd.deleteClip(id)) { _selectedClip.value = null } } }
    fun duplicateSelected() { _selectedClip.value?.let { id -> edit(Cmd.duplicateClip(id)) { r -> _selectedClip.value = r.id } } }
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
