package com.caucesound.app.ui

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.FiberManualRecord
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.FileUpload
import androidx.compose.material.icons.filled.GridOn
import androidx.compose.material.icons.filled.Gradient
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Timer
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material.icons.filled.ZoomIn
import androidx.compose.material.icons.filled.ZoomOut
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.DawViewModel
import com.caucesound.app.R
import com.caucesound.app.audio.AudioGuard
import com.caucesound.app.audio.NativeEngine
import com.caucesound.app.data.MusicTime
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.SAMPLE_RATE
import com.caucesound.app.data.TrackState
import kotlin.math.log10
import kotlin.math.max

private val HEADER_WIDTH = 176.dp

@Composable
fun EditorScreen(vm: DawViewModel, state: ProjectState, requestMic: (() -> Unit) -> Unit) {
    val selected by vm.selectedClip.collectAsState()
    val waveforms by vm.waveforms.collectAsState()
    val message by vm.messages.collectAsState()
    val busy by vm.busy.collectAsState()
    val snackbar = remember { SnackbarHostState() }
    val hScroll = rememberScrollState()
    val vScroll = rememberScrollState()
    var pxPerSecond by remember { mutableFloatStateOf(80f) }
    var snap by remember { mutableStateOf(true) }
    var playing by remember { mutableStateOf(false) }
    val position = remember { mutableLongStateOf(0L) }
    var meters by remember { mutableStateOf(FloatArray(0)) }
    var dialog by remember { mutableStateOf<String?>(null) }
    var importTrack by remember { mutableStateOf<String?>(null) }
    var exportBits by remember { mutableStateOf(24) }

    LaunchedEffect(message) { message?.let { snackbar.showSnackbar(it); vm.consumeMessage() } }
    // Bucle de interfaz a la frecuencia de la pantalla: posición, estado y medidores (lecturas atómicas).
    LaunchedEffect(state.tracks.size) {
        while (true) {
            withFrameNanos { }
            position.longValue = NativeEngine.position()
            playing = NativeEngine.isPlaying()
            meters = NativeEngine.meters(state.tracks.size)
        }
    }
    BackHandler { vm.closeProject() }

    val importer = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val t = importTrack
        if (uri != null && t != null) vm.importAudio(uri, t, uri.lastPathSegment?.substringAfterLast('/') ?: "audio")
        importTrack = null
    }
    val exporter = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("audio/wav")) { uri ->
        if (uri != null) vm.exportWav(uri, exportBits)
    }

    Scaffold(containerColor = Palette.Background, snackbarHost = { SnackbarHost(snackbar) }) { pad ->
        Column(Modifier.fillMaxSize().padding(pad)) {
            // --- Barra superior ---
            Row(Modifier.fillMaxWidth().background(Palette.Surface).padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = { vm.closeProject() }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back)) }
                Column(Modifier.weight(1f)) {
                    Text(state.name, maxLines = 1, overflow = TextOverflow.Ellipsis, fontWeight = FontWeight.SemiBold)
                    Text(if (state.dirty) stringResource(R.string.unsaved) else stringResource(R.string.saved), color = Palette.TextDim, fontSize = 11.sp)
                }
                IconButton(onClick = vm::undo, enabled = state.canUndo && !state.recording) { Icon(Icons.AutoMirrored.Filled.Undo, stringResource(R.string.undo)) }
                IconButton(onClick = vm::redo, enabled = state.canRedo && !state.recording) { Icon(Icons.AutoMirrored.Filled.Redo, stringResource(R.string.redo)) }
                IconButton(onClick = vm::saveNow) { Icon(Icons.Default.Save, stringResource(R.string.save)) }
                IconButton(onClick = { dialog = "export" }, enabled = state.contentEnd > 0 && !state.recording) { Icon(Icons.Default.FileDownload, stringResource(R.string.export)) }
                IconButton(onClick = { vm.refreshAudioStatus(); dialog = "settings" }) { Icon(Icons.Default.Settings, stringResource(R.string.settings)) }
            }
            // --- Transporte ---
            Transport(state, playing, position.longValue, onRewind = { vm.seek(0) }, onPlay = vm::play, onStop = vm::stop,
                onRecord = { requestMic { vm.record() } }, onTempo = { dialog = "tempo" }, onMetronome = { vm.setMetronome(!state.metronome) })
            busy?.let { LinearProgressIndicator(Modifier.fillMaxWidth()) }

            // --- Pistas + timeline ---
            Row(Modifier.weight(1f).fillMaxWidth().verticalScroll(vScroll)) {
                Column(Modifier.width(HEADER_WIDTH)) {
                    Spacer(Modifier.height(RULER_HEIGHT).fillMaxWidth().background(Palette.Surface))
                    state.tracks.forEachIndexed { i, t ->
                        TrackHeader(t, meters.getOrElse(i + 1) { 0f }, vm, onImport = { importTrack = t.id; importer.launch(arrayOf("audio/*")) },
                            onRename = { dialog = "rename:${t.id}" })
                    }
                    if (state.tracks.size < 32) {
                        val trackName = stringResource(R.string.default_track_name)
                        TextButton(onClick = { vm.addTrack("$trackName ${state.tracks.size + 1}") }, modifier = Modifier.padding(4.dp)) {
                            Icon(Icons.Default.Add, null); Spacer(Modifier.width(4.dp)); Text(stringResource(R.string.add_track))
                        }
                    }
                }
                Box(Modifier.weight(1f)) {
                    Timeline(
                        state, waveforms, TimelineGeometry(pxPerSecond), hScroll, selected, snap,
                        position = { position.longValue },
                        onSeek = vm::seek, onSelect = vm::selectClip,
                        onMove = vm::moveClip, onTrim = vm::trimClip,
                    )
                }
            }

            // --- Herramientas ---
            Row(Modifier.fillMaxWidth().background(Palette.Surface).navigationBarsPadding().padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = { pxPerSecond = max(5f, pxPerSecond / 1.5f) }) { Icon(Icons.Default.ZoomOut, stringResource(R.string.zoom_out)) }
                IconButton(onClick = { pxPerSecond = (pxPerSecond * 1.5f).coerceAtMost(2000f) }) { Icon(Icons.Default.ZoomIn, stringResource(R.string.zoom_in)) }
                ToggleIcon(Icons.Default.GridOn, stringResource(R.string.snap), snap) { snap = !snap }
                Spacer(Modifier.weight(1f))
                val sel = selected?.let { state.clip(it) }
                if (sel != null && !state.recording) {
                    IconButton(onClick = { vm.splitSelectedAt(position.longValue) }, enabled = position.longValue > sel.second.start && position.longValue < sel.second.end) {
                        Icon(Icons.Default.ContentCut, stringResource(R.string.split))
                    }
                    IconButton(onClick = vm::duplicateSelected) { Icon(Icons.Default.ContentCopy, stringResource(R.string.duplicate)) }
                    IconButton(onClick = { dialog = "fades" }) { Icon(Icons.Default.Gradient, stringResource(R.string.fades)) }
                    IconButton(onClick = { dialog = "gain" }) { Icon(Icons.Default.VolumeUp, stringResource(R.string.clip_gain)) }
                    IconButton(onClick = vm::deleteSelected) { Icon(Icons.Default.Delete, stringResource(R.string.delete), tint = Palette.Record) }
                } else {
                    Text(stringResource(R.string.hint_select_clip), color = Palette.TextDim, fontSize = 12.sp, modifier = Modifier.padding(end = 8.dp))
                }
            }
        }
    }

    // --- Diálogos ---
    when (val d = dialog) {
        "tempo" -> TempoDialog(state, onDismiss = { dialog = null }) { bpm, bpb -> vm.setTempo(bpm, bpb); dialog = null }
        "fades" -> selected?.let { state.clip(it) }?.let { (_, c) ->
            FadesDialog(c.length, c.fadeIn, c.fadeOut, onDismiss = { dialog = null }) { i, o -> vm.setFades(c.id, i, o); dialog = null }
        } ?: run { dialog = null }
        "gain" -> selected?.let { state.clip(it) }?.let { (_, c) ->
            GainDialog(c.gainDb, onDismiss = { dialog = null }) { db -> vm.setClipGain(c.id, db); dialog = null }
        } ?: run { dialog = null }
        "export" -> ExportDialog(onDismiss = { dialog = null }) { bits ->
            exportBits = bits; dialog = null
            exporter.launch("${state.name.ifBlank { "mix" }}.wav")
        }
        "settings" -> SettingsDialog(vm, state, requestMic, onDismiss = { dialog = null })
        null -> {}
        else -> if (d.startsWith("rename:")) {
            val id = d.removePrefix("rename:")
            val t = state.tracks.firstOrNull { it.id == id }
            if (t == null) dialog = null else RenameDialog(t.name, onDismiss = { dialog = null }, onDelete = { vm.removeTrack(id); dialog = null }) { vm.renameTrack(id, it); dialog = null }
        }
    }
}

@Composable
private fun Transport(
    s: ProjectState, playing: Boolean, pos: Long, onRewind: () -> Unit, onPlay: () -> Unit, onStop: () -> Unit,
    onRecord: () -> Unit, onTempo: () -> Unit, onMetronome: () -> Unit,
) {
    Row(Modifier.fillMaxWidth().background(Palette.Background).padding(horizontal = 8.dp, vertical = 6.dp), verticalAlignment = Alignment.CenterVertically) {
        IconButton(onClick = onRewind) { Icon(Icons.Default.SkipPrevious, stringResource(R.string.rewind)) }
        if (playing || s.recording) IconButton(onClick = onStop) { Icon(Icons.Default.Stop, stringResource(R.string.stop), Modifier.size(32.dp)) }
        else IconButton(onClick = onPlay) { Icon(Icons.Default.PlayArrow, stringResource(R.string.play), Modifier.size(32.dp)) }
        IconButton(onClick = onRecord, enabled = !s.recording) {
            Icon(Icons.Default.FiberManualRecord, stringResource(R.string.record), tint = if (s.recording) Palette.Record else Palette.Record.copy(alpha = 0.8f), modifier = Modifier.size(28.dp))
        }
        Column(Modifier.padding(horizontal = 8.dp)) {
            Text(MusicTime.barsBeats(pos, s.bpm, s.beatsPerBar), fontFamily = FontFamily.Monospace, fontSize = 18.sp, fontWeight = FontWeight.Bold)
            Text(MusicTime.clock(pos), fontFamily = FontFamily.Monospace, fontSize = 11.sp, color = Palette.TextDim)
        }
        Spacer(Modifier.weight(1f))
        if (s.recording) Text("● REC", color = Palette.Record, fontWeight = FontWeight.Bold, modifier = Modifier.padding(end = 8.dp))
        Text(
            "%.1f BPM  %d/4".format(s.bpm, s.beatsPerBar), fontFamily = FontFamily.Monospace, fontSize = 13.sp,
            modifier = Modifier.clip8().clickable(enabled = !s.recording, onClick = onTempo).padding(8.dp),
        )
        ToggleIcon(Icons.Default.Timer, stringResource(R.string.metronome), s.metronome, onMetronome)
    }
}

private fun Modifier.clip8() = this.border(1.dp, Palette.Grid, RoundedCornerShape(8.dp))

@Composable
private fun ToggleIcon(icon: ImageVector, desc: String, on: Boolean, onClick: () -> Unit) {
    IconButton(onClick = onClick, colors = IconButtonDefaults.iconButtonColors(contentColor = if (on) Palette.Accent else Palette.TextDim)) { Icon(icon, desc) }
}

@Composable
private fun TrackHeader(t: TrackState, meter: Float, vm: DawViewModel, onImport: () -> Unit, onRename: () -> Unit) {
    var vol by remember(t.id, t.volumeDb) { mutableFloatStateOf(t.volumeDb.coerceIn(-60f, 6f)) }
    var pan by remember(t.id, t.pan) { mutableFloatStateOf(t.pan) }
    Row(Modifier.width(HEADER_WIDTH).height(TRACK_HEIGHT).background(Palette.Surface).border(0.5.dp, Palette.Grid)) {
        Box(Modifier.width(4.dp).fillMaxHeight().background(Color(t.color.toInt())))
        Column(Modifier.weight(1f).padding(horizontal = 6.dp, vertical = 2.dp), verticalArrangement = Arrangement.SpaceEvenly) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(t.name, Modifier.weight(1f).clickable(onClick = onRename), maxLines = 1, overflow = TextOverflow.Ellipsis, fontSize = 13.sp)
                Icon(Icons.Default.FileUpload, stringResource(R.string.import_audio), Modifier.size(20.dp).clickable(onClick = onImport), tint = Palette.TextDim)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                SmallToggle("M", t.mute, Palette.Mute) { vm.setMute(t.id, !t.mute) }
                SmallToggle("S", t.solo, Palette.Solo) { vm.setSolo(t.id, !t.solo) }
                SmallToggle("R", t.armed, Palette.Record) { vm.setArmed(t.id, !t.armed) }
                // medidor de pico
                val db = if (meter > 1e-5f) 20 * log10(meter) else -100f
                val frac = ((db + 60f) / 60f).coerceIn(0f, 1f)
                Box(Modifier.weight(1f).height(22.dp).background(Palette.Background, RoundedCornerShape(3.dp))) {
                    Box(Modifier.fillMaxHeight().fillMaxWidth(frac).background(if (db > -1f) Palette.Record else Palette.Accent.copy(alpha = 0.8f), RoundedCornerShape(3.dp)))
                }
            }
            Slider(vol, { vol = it; vm.setVolumeLive(t.id, it) }, valueRange = -60f..6f, onValueChangeFinished = vm::commitMixer,
                modifier = Modifier.height(18.dp), colors = SliderDefaults.colors(thumbColor = Palette.Text, activeTrackColor = Palette.Accent))
            Slider(pan, { pan = it; vm.setPanLive(t.id, it) }, valueRange = -1f..1f, onValueChangeFinished = vm::commitMixer,
                modifier = Modifier.height(18.dp), colors = SliderDefaults.colors(thumbColor = Palette.Solo, activeTrackColor = Palette.Grid, inactiveTrackColor = Palette.Grid))
        }
    }
}

@Composable
private fun SmallToggle(label: String, on: Boolean, color: Color, onClick: () -> Unit) {
    Box(
        Modifier.size(26.dp, 22.dp).background(if (on) color else Palette.SurfaceHigh, RoundedCornerShape(4.dp)).clickable(onClick = onClick),
        contentAlignment = Alignment.Center,
    ) { Text(label, fontSize = 11.sp, fontWeight = FontWeight.Bold, color = if (on) Color.Black else Palette.TextDim) }
}

// ----------------------------------------------------------------------------------------------------------------
@Composable
private fun TempoDialog(s: ProjectState, onDismiss: () -> Unit, onOk: (Double, Int) -> Unit) {
    var bpm by remember { mutableStateOf("%.1f".format(s.bpm).replace(',', '.')) }
    var bpb by remember { mutableStateOf(s.beatsPerBar.toString()) }
    val bpmVal = bpm.replace(',', '.').toDoubleOrNull()
    val bpbVal = bpb.toIntOrNull()
    val valid = bpmVal != null && bpmVal in 20.0..400.0 && bpbVal != null && bpbVal in 1..16
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.tempo)) },
        text = {
            Column {
                OutlinedTextField(bpm, { bpm = it.take(6) }, label = { Text("BPM (20–400)") }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal))
                OutlinedTextField(bpb, { bpb = it.take(2) }, label = { Text(stringResource(R.string.beats_per_bar)) }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
                Text(stringResource(R.string.tempo_note), fontSize = 12.sp, color = Palette.TextDim)
            }
        },
        confirmButton = { TextButton(enabled = valid, onClick = { onOk(bpmVal!!, bpbVal!!) }) { Text("OK") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}

@Composable
private fun FadesDialog(length: Long, fadeIn: Long, fadeOut: Long, onDismiss: () -> Unit, onOk: (Long, Long) -> Unit) {
    val maxMs = (MusicTime.framesToMs(length) / 2).coerceAtMost(10_000.0).toFloat()
    var i by remember { mutableFloatStateOf(MusicTime.framesToMs(fadeIn).toFloat().coerceAtMost(maxMs)) }
    var o by remember { mutableFloatStateOf(MusicTime.framesToMs(fadeOut).toFloat().coerceAtMost(maxMs)) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.fades)) },
        text = {
            Column {
                Text(stringResource(R.string.fade_in_ms, i.toInt())); Slider(i, { i = it }, valueRange = 0f..maxMs)
                Text(stringResource(R.string.fade_out_ms, o.toInt())); Slider(o, { o = it }, valueRange = 0f..maxMs)
            }
        },
        confirmButton = { TextButton(onClick = { onOk(MusicTime.msToFrames(i.toDouble()), MusicTime.msToFrames(o.toDouble())) }) { Text("OK") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}

@Composable
private fun GainDialog(gainDb: Float, onDismiss: () -> Unit, onOk: (Float) -> Unit) {
    var g by remember { mutableFloatStateOf(gainDb.coerceIn(-24f, 12f)) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.clip_gain)) },
        text = { Column { Text("%+.1f dB".format(g)); Slider(g, { g = it }, valueRange = -24f..12f) } },
        confirmButton = { TextButton(onClick = { onOk(g) }) { Text("OK") } },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}

@Composable
private fun ExportDialog(onDismiss: () -> Unit, onOk: (Int) -> Unit) {
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.export_wav)) },
        text = { Text(stringResource(R.string.export_note)) },
        confirmButton = {
            Row {
                TextButton(onClick = { onOk(16) }) { Text("16 bit") }
                TextButton(onClick = { onOk(24) }) { Text("24 bit") }
                TextButton(onClick = { onOk(32) }) { Text("32 float") }
            }
        },
        dismissButton = { TextButton(onClick = onDismiss) { Text(stringResource(R.string.cancel)) } },
    )
}

@Composable
private fun RenameDialog(name: String, onDismiss: () -> Unit, onDelete: () -> Unit, onOk: (String) -> Unit) {
    var n by remember { mutableStateOf(name) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.track)) },
        text = { OutlinedTextField(n, { n = it.take(40) }, singleLine = true) },
        confirmButton = { TextButton(onClick = { onOk(n.trim().ifEmpty { name }) }) { Text("OK") } },
        dismissButton = { TextButton(onClick = onDelete) { Text(stringResource(R.string.delete_track), color = Palette.Record) } },
    )
}

@Composable
private fun SettingsDialog(vm: DawViewModel, s: ProjectState, requestMic: (() -> Unit) -> Unit, onDismiss: () -> Unit) {
    val status by vm.audioStatus.collectAsState()
    val route by vm.route.collectAsState()
    val monitoring by vm.monitoring.collectAsState()
    var offsetMs by remember { mutableFloatStateOf((s.recordLatencyOffset * 1000f / SAMPLE_RATE)) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(stringResource(R.string.settings)) },
        text = {
            Column(Modifier.verticalScroll(rememberScrollState())) {
                Text(stringResource(R.string.output_route) + ": " + when (route) {
                    AudioGuard.Route.SPEAKER -> stringResource(R.string.route_speaker)
                    AudioGuard.Route.WIRED -> stringResource(R.string.route_wired)
                    AudioGuard.Route.USB -> "USB"
                    AudioGuard.Route.BLUETOOTH -> stringResource(R.string.route_bt)
                })
                if (route == AudioGuard.Route.BLUETOOTH) Text(stringResource(R.string.bt_warning), color = Palette.Solo, fontSize = 12.sp)
                Spacer(Modifier.height(8.dp))
                status?.let { st ->
                    Text(stringResource(R.string.audio_status), fontWeight = FontWeight.SemiBold)
                    Text(
                        "API ${st.optString("outApi", "?")} · ${st.optInt("outRate")} Hz · burst ${st.optInt("outBurst")} · buffer ${st.optInt("outBuffer")}\n" +
                            "${stringResource(R.string.exclusive)} ${st.optBoolean("outExclusive")}\n" +
                            "${stringResource(R.string.out_latency)}: ${"%.1f".format(st.optDouble("outLatencyMs", -1.0))} ms" +
                            (if (st.has("inLatencyMs")) "\n${stringResource(R.string.in_latency)}: ${"%.1f".format(st.optDouble("inLatencyMs", -1.0))} ms" else "") +
                            "\nXRuns: ${st.optInt("outXruns")} · ${stringResource(R.string.reopens)}: ${st.optInt("reopenCount")}",
                        fontSize = 12.sp, fontFamily = FontFamily.Monospace, color = Palette.TextDim,
                    )
                    Text(stringResource(R.string.latency_estimate_note), fontSize = 11.sp, color = Palette.TextDim)
                }
                Spacer(Modifier.height(8.dp))
                Text(stringResource(R.string.record_offset, offsetMs.toInt()))
                Slider(offsetMs, { offsetMs = it }, valueRange = 0f..500f, onValueChangeFinished = { vm.setLatencyOffsetMs(offsetMs.toDouble()) })
                Text(stringResource(R.string.record_offset_note), fontSize = 11.sp, color = Palette.TextDim)
                Spacer(Modifier.height(8.dp))
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.monitoring), Modifier.weight(1f))
                    Switch(monitoring, { on -> if (on) requestMic { vm.setMonitoring(true) } else vm.setMonitoring(false) })
                }
                Text(stringResource(R.string.monitoring_note), fontSize = 11.sp, color = Palette.TextDim)
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("OK") } },
    )
}
