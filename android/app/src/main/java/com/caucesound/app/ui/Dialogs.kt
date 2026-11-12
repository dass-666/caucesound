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

/** Diálogos del estudio (tempo, fundidos, ganancia, exportación, pista, ajustes de audio). */
// ----------------------------------------------------------------------------------------------------------------
@Composable
internal fun TempoDialog(s: ProjectState, onDismiss: () -> Unit, onOk: (Double, Int) -> Unit) {
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
internal fun FadesDialog(length: Long, fadeIn: Long, fadeOut: Long, onDismiss: () -> Unit, onOk: (Long, Long) -> Unit) {
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
internal fun GainDialog(gainDb: Float, onDismiss: () -> Unit, onOk: (Float) -> Unit) {
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
internal fun ExportDialog(onDismiss: () -> Unit, onOk: (Int) -> Unit) {
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
internal fun RenameDialog(name: String, onDismiss: () -> Unit, onDelete: () -> Unit, onOk: (String) -> Unit) {
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
internal fun SettingsDialog(vm: DawViewModel, s: ProjectState, requestMic: (() -> Unit) -> Unit, onDismiss: () -> Unit) {
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
                            "\nXRuns: ${st.optInt("outXruns")} · ${stringResource(R.string.reopens)}: ${st.optInt("reopenCount")}" +
                            "\n" + tr("Carga DSP", "DSP load") + ": " + tr("media", "avg") + " ${"%.0f".format(100 * st.optDouble("dspLoadAvg", 0.0))} % · " +
                            tr("pico", "peak") + " ${"%.0f".format(100 * st.optDouble("dspLoadPeak", 0.0))} % · " +
                            tr("bloques tarde", "late blocks") + " ${st.optLong("dspOverBudget")}",
                        fontSize = 12.sp, fontFamily = FontFamily.Monospace, color = Palette.TextDim,
                    )
                    Text(stringResource(R.string.latency_estimate_note), fontSize = 11.sp, color = Palette.TextDim)
                    TextButton(onClick = { vm.refreshAudioStatus() }) { Text(tr("Actualizar estado", "Refresh status")) }
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
