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
import androidx.compose.ui.platform.testTag
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
/** Estado editable del diálogo de tempo (fuera del diálogo para poder probar su contenido en la JVM). */
internal class TempoForm(s: ProjectState, countInBars: Int) {
    var bpm by mutableStateOf("%.1f".format(s.bpm).replace(',', '.'))
    var bpb by mutableStateOf(s.beatsPerBar.toString())
    var bars by mutableStateOf(countInBars.coerceIn(0, 2))
    var taps by mutableStateOf(0)
    private val tapper = com.caucesound.app.data.TapTempo()
    val bpmVal get() = bpm.replace(',', '.').toDoubleOrNull()
    val bpbVal get() = bpb.toIntOrNull()
    val valid get() = bpmVal.let { it != null && it in 20.0..400.0 } && bpbVal.let { it != null && it in 1..16 }
    fun tap(nowMs: Long) { val r = tapper.tap(nowMs); taps = tapper.count; if (r != null) bpm = "%.1f".format(r).replace(',', '.') }
}

@Composable
internal fun TempoDialog(s: ProjectState, countInBars: Int = 0, onDismiss: () -> Unit, onOk: (Double, Int, Int) -> Unit) {
    val form = remember { TempoForm(s, countInBars) }
    CauceDialog(stringResource(R.string.tempo), onDismiss, confirm = "OK", onConfirm = { onOk(form.bpmVal!!, form.bpbVal!!, form.bars) }, dismiss = stringResource(R.string.cancel), confirmEnabled = form.valid) {
        TempoFormContent(form)
    }
}

@Composable
internal fun TempoFormContent(form: TempoForm, now: () -> Long = { android.os.SystemClock.elapsedRealtime() }) {
    Column {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            OutlinedTextField(form.bpm, { form.bpm = it.take(6) }, label = { Text("BPM (20–400)") }, singleLine = true, modifier = Modifier.weight(1f), keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Decimal))
            // Tap tempo: toca al ritmo; el BPM sale de la mediana de los últimos toques.
            CauceButton(if (form.taps < 2) tr("Tocar", "Tap") else tr("Tocar", "Tap") + " · ${form.taps}", { form.tap(now()) }, kind = ButtonKind.Secondary, height = 56.dp)
        }
        Spacer(Modifier.height(8.dp))
        OutlinedTextField(form.bpb, { form.bpb = it.take(2) }, label = { Text(stringResource(R.string.beats_per_bar)) }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number))
        Spacer(Modifier.height(12.dp))
        Text(tr("Cuenta previa al grabar", "Count-in before recording"), fontSize = 12.sp, color = Palette.TextDim)
        Spacer(Modifier.height(4.dp))
        Segmented(listOf(tr("No", "Off"), tr("1 compás", "1 bar"), tr("2 compases", "2 bars")), form.bars, { form.bars = it })
        Spacer(Modifier.height(8.dp))
        Text(stringResource(R.string.tempo_note), fontSize = 12.sp, color = Palette.TextDim)
    }
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
                RecordModeSection(vm, route)
                Spacer(Modifier.height(8.dp))
                val speakers by vm.speakerMode.collectAsState()
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(stringResource(R.string.monitoring), Modifier.weight(1f))
                    Switch(monitoring && !speakers, { on -> if (on) requestMic { vm.setMonitoring(true) } else vm.setMonitoring(false) }, enabled = !speakers)
                }
                Text(if (speakers) tr("Con bocinas la monitorización se apaga: el micrófono volvería a sonar por el altavoz y se acoplaría.",
                    "With speakers, monitoring is off: the microphone would play through the speaker and feed back.")
                    else stringResource(R.string.monitoring_note), fontSize = 11.sp, color = Palette.TextDim)
                // 0.10: medición para decidir si VocalShift podrá funcionar durante la reproducción en este teléfono.
                // No activa nada: procesa unos segundos de una voz sintética y enseña cuánto procesador gasta.
                Spacer(Modifier.height(12.dp))
                val bench by vm.liveBench.collectAsState()
                Text(tr("Diagnóstico (experimental)", "Diagnostics (experimental)"), fontWeight = FontWeight.SemiBold)
                Text(tr("Mide cuánto procesador gastaría VocalShift en vivo en este teléfono. No cambia tu proyecto ni activa el efecto. Hazlo con la música parada.",
                    "Measures how much CPU live VocalShift would use on this phone. It doesn't change your project or enable the effect. Run it with playback stopped."),
                    fontSize = 11.sp, color = Palette.TextDim)
                TextButton(onClick = { vm.runLiveBench() }, enabled = bench != "…", modifier = Modifier.testTag("live_bench")) {
                    Text(tr("Medir VocalShift en vivo (5 s)", "Measure live VocalShift (5 s)"))
                }
                bench?.let { Text(it, fontSize = 12.sp, fontFamily = FontFamily.Monospace, color = Palette.TextDim, modifier = Modifier.testTag("live_bench_result")) }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("OK") } },
    )
}

/**
 * Grabación de voz (0.8). El motor ya graba solo el micrófono (comprobado: la mezcla interna no entra en la toma);
 * lo que se oye de fondo en una toma con bocinas es sonido que el micrófono capta por el aire. Este bloque reduce esa
 * filtración sin prometer eliminarla.
 */
@Composable
internal fun RecordModeSection(vm: DawViewModel, route: AudioGuard.Route) {
    val speakers by vm.speakerMode.collectAsState()
    val echo by vm.echoCancel.collectAsState()
    val duck by vm.duckDb.collectAsState()
    var duckUi by remember(duck) { mutableFloatStateOf(duck) }
    Column(Modifier.fillMaxWidth().testTag("record_mode")) {
        Text(tr("Grabación de voz", "Voice recording"), fontWeight = FontWeight.SemiBold)
        Segmented(listOf(tr("Con audífonos", "Headphones"), tr("Con bocinas", "Speakers")), if (speakers) 1 else 0, { vm.setSpeakerMode(it == 1) },
            Modifier.fillMaxWidth().testTag("record_mode_switch"), height = 40.dp)
        if (!speakers) {
            Text(tr("Modo estudio: el micrófono se graba tal cual, sin cancelación ni atenuación. Es la mejor calidad de voz.",
                "Studio mode: the microphone is recorded as is, with no cancelling or ducking. Best voice quality."), fontSize = 11.sp, color = Palette.TextDim)
            if (route == AudioGuard.Route.SPEAKER) Text(tr("Ahora mismo suena por el altavoz: el micrófono captará el beat. Conecta audífonos o elige «Con bocinas».",
                "Sound is going to the speaker right now: the microphone will pick up the beat. Plug in headphones or choose “Speakers”."), fontSize = 11.sp, color = Palette.Solo)
        } else {
            Text(tr("Bajar el beat mientras grabas: %.0f dB".format(duckUi), "Lower the beat while recording: %.0f dB".format(duckUi)), fontSize = 13.sp)
            Slider(duckUi, { duckUi = it }, valueRange = -24f..0f, onValueChangeFinished = { vm.setDuckDb(duckUi) }, modifier = Modifier.testTag("record_duck"))
            Text(tr("Solo lo que oyes durante la toma. Los faders, la mezcla y la exportación no cambian.",
                "Only what you hear during the take. Faders, mix and export are unchanged."), fontSize = 11.sp, color = Palette.TextDim)
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(tr("Cancelación de eco del teléfono", "Phone echo cancelling"), Modifier.weight(1f), fontSize = 13.sp)
                Switch(echo, { vm.setEchoCancel(it) }, modifier = Modifier.testTag("record_echo"))
            }
            Text(tr("Experimental y distinta en cada teléfono: puede quitar parte del beat, pero también apagar o metalizar la voz, y se aplica al grabar (la toma no guarda una copia sin procesar). Pruébala con una toma corta.",
                "Experimental and different on every phone: it may remove some of the beat but can also dull or warp the voice, and it is applied while recording (no unprocessed copy is kept). Try it on a short take."),
                fontSize = 11.sp, color = Palette.TextDim)
        }
        // 0.9.2: grabar sin acompañamiento (también se elige en el diálogo del botón rojo).
        val solo by vm.recordSolo.collectAsState()
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(tr("Grabar sin acompañamiento", "Record without backing"), Modifier.weight(1f), fontSize = 13.sp)
            Switch(solo, { vm.setRecordSolo(it) }, modifier = Modifier.testTag("record_solo"))
        }
        Text(tr("Silencia el beat y el metrónomo solo mientras grabas. La mezcla no cambia.", "Mutes the beat and click only while recording. The mix is unchanged."),
            fontSize = 11.sp, color = Palette.TextDim)
        Text(tr("Para comprobar una toma: pulsa S en la pista de voz y escúchala sola.", "To check a take: press S on the voice track and listen to it alone."),
            fontSize = 11.sp, color = Palette.TextFaint)
    }
}
