package com.caucesound.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.DeleteOutline
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.SwapHoriz
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.WAVE_BLOCK
import com.caucesound.app.data.ClipState
import com.caucesound.app.data.PluginUi
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.SAMPLE_RATE
import com.caucesound.app.data.TrackState
import com.caucesound.app.data.VocalParams
import kotlin.math.abs
import kotlin.math.roundToInt

/**
 * VocalShift (plugin integrado como submódulo). Procesado OFFLINE y no destructivo de un clip de audio: se calcula en
 * el teléfono, sin red, y el resultado es una segunda versión del clip. La grabación original no se modifica nunca.
 * No es monitorización en directo: el motor tiene 150 ms de latencia, que al procesar queda compensada.
 * Todos los controles corresponden a parámetros reales del motor de VocalShift.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun VocalShiftPanel(state: ProjectState, selectedClip: String?, plugins: PluginUi, waveforms: Map<String, FloatArray>, actions: StudioActions) {
    val sel = selectedClip?.let { state.clip(it) }
    if (sel == null) { VocalClipChooser(state, actions); return }
    val (track, clip) = sel
    val initial = clip.process?.params ?: plugins.vocalPresets.firstOrNull { it.id == "urban_spatial" }?.params ?: VocalParams()
    var p by remember(clip.id, clip.process?.params) { mutableStateOf(initial) }
    var advanced by remember { mutableIntStateOf(0) }
    val job = plugins.job?.takeIf { it.type == "vocalshift" && it.targetId == clip.id && it.active }
    val process = clip.process
    fun edit(next: VocalParams) { p = next }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        PanelHeader("VocalShift", "${clip.name.ifBlank { tr("Clip de audio", "Audio clip") }} · ${track.name}") {
            ToolButton(Icons.Default.SwapHoriz, tr("Elegir otro clip", "Choose another clip"), { actions.selectClip(null) }, tint = Palette.Text)
        }
        Text(tr("Proceso sin conexión: primero se calcula y después se escucha. No es efecto en directo. Tu grabación original no se modifica.",
            "Offline process: it renders first, then you listen. It is not a live effect. Your original recording is never modified."),
            color = Palette.TextDim, fontSize = 12.sp)

        // Forma de onda de la versión que suena, con el avance real del proceso encima.
        ClipWave(clip, waveforms[clip.sourceId], Color(track.color.toInt()), job?.progress)

        // Presets de fábrica de VocalShift (del motor).
        if (plugins.vocalPresets.isNotEmpty()) {
            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                plugins.vocalPresets.forEach { pr ->
                    PillToggle(pr.name, p.preset == pr.id, { edit(pr.params) }, color = Palette.Violet, height = 40.dp, modifier = Modifier.testTag("vocal_preset_${pr.id}"))
                }
            }
        }
        Segmented(listOf(tr("Fácil", "Easy"), tr("Avanzado", "Advanced")), advanced, { advanced = it }, Modifier.fillMaxWidth())

        // Los tres controles principales.
        Row(Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.Radius)).background(Palette.Surface).padding(vertical = 12.dp), horizontalArrangement = Arrangement.SpaceEvenly) {
            val range = if (advanced == 1) 24f else 12f
            Knob("Pitch", p.semitones.coerceIn(-range, range), -range, range, 0f, { edit(p.copy(semitones = if (advanced == 1) (it * 10).roundToInt() / 10f else it.roundToInt().toFloat(), preset = "")) },
                size = 84.dp, color = Palette.Violet, format = { (if (it > 0) "+" else "") + (if (advanced == 1) "%.1f".format(it) else "%.0f".format(it)) + " st" }, modifier = Modifier.testTag("vocal_pitch"))
            Knob(tr("Carácter", "Character"), p.character, 0f, 100f, 65f, { edit(p.copy(character = it.roundToInt().toFloat(), preset = "")) },
                size = 84.dp, color = Palette.Blue, format = { "%.0f %%".format(it) }, modifier = Modifier.testTag("vocal_character"))
            Knob(tr("Magia", "Magic"), p.magic, 0f, 100f, 70f, { edit(p.copy(magic = it.roundToInt().toFloat(), preset = "")) },
                size = 84.dp, color = Palette.Cyan, format = { "%.0f %%".format(it) }, modifier = Modifier.testTag("vocal_magic"))
        }

        // Segunda voz: la original queda delante y la procesada se añade detrás con su propio nivel.
        Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.Radius)).background(Palette.Surface).padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(tr("Segunda voz", "Second voice"), fontWeight = FontWeight.Bold)
                    Text(if (p.secondVoice) tr("Tu voz original delante y la procesada detrás.", "Your original voice in front, the processed one behind.")
                        else tr("Se transforma la voz entera.", "The whole voice is transformed."), color = Palette.TextDim, fontSize = 12.sp)
                }
                PillToggle(if (p.secondVoice) tr("Activada", "On") else tr("Desactivada", "Off"), p.secondVoice, { edit(p.copy(secondVoice = !p.secondVoice, preset = "")) },
                    color = Palette.Cyan, height = 40.dp, modifier = Modifier.testTag("vocal_second"))
            }
            if (p.secondVoice) LabeledSlider(tr("Nivel de la segunda voz", "Second voice level"), p.secondGainDb, -30f, 6f, "%+.0f dB", "vocal_second_level") { edit(p.copy(secondGainDb = it.roundToInt().toFloat(), preset = "")) }
        }

        if (advanced == 1) {
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.Radius)).background(Palette.Surface).padding(12.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text(tr("Ajustes finos del motor", "Engine fine tuning"), fontWeight = FontWeight.Bold)
                Segmented(listOf(tr("Conservar identidad", "Keep identity"), tr("Transformación radical", "Radical transform")), if (p.radical) 1 else 0, { edit(p.copy(radical = it == 1, preset = "")) }, Modifier.fillMaxWidth())
                LabeledSlider(tr("Formantes (timbre)", "Formants (timbre)"), p.formantOffset, -12f, 12f, "%+.1f st", "vocal_formant") { edit(p.copy(formantOffset = (it * 2).roundToInt() / 2f, preset = "")) }
                LabeledSlider(tr("Brillo", "Brightness"), p.brightnessOffset, -12f, 12f, "%+.1f dB", "vocal_brightness") { edit(p.copy(brightnessOffset = (it * 2).roundToInt() / 2f, preset = "")) }
                if (p.secondVoice) {
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(tr("Espacio (reverb y eco de la segunda voz)", "Space (second voice reverb and echo)"), Modifier.weight(1f), fontSize = 13.sp)
                        PillToggle(if (p.spatial) tr("Sí", "On") else "No", p.spatial, { edit(p.copy(spatial = !p.spatial, preset = "")) }, color = Palette.Cyan)
                    }
                    LabeledSlider(tr("Panorama de la segunda voz", "Second voice pan"), p.pan, -100f, 100f, "%+.0f", "vocal_pan") { edit(p.copy(pan = it.roundToInt().toFloat(), preset = "")) }
                    LabeledSlider(tr("Retardo de la segunda voz", "Second voice delay"), p.offsetMs, 0f, 100f, "%.0f ms", "vocal_offset") { edit(p.copy(offsetMs = it.roundToInt().toFloat(), preset = "")) }
                } else {
                    LabeledSlider(tr("Mezcla original / procesada", "Dry / wet mix"), p.mixPercent, 0f, 100f, "%.0f %%", "vocal_mix") { edit(p.copy(mixPercent = it.roundToInt().toFloat(), preset = "")) }
                }
            }
        }

        // Procesar, con progreso real y cancelación.
        if (job != null) {
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.Radius)).background(Palette.SurfaceHigh).padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(tr("Procesando… ${(job.progress * 100).roundToInt()} %", "Processing… ${(job.progress * 100).roundToInt()} %"), fontWeight = FontWeight.Bold,
                    modifier = Modifier.testTag("vocal_progress"))
                LinearProgressIndicator(progress = { job.progress.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth(), color = Palette.Violet, trackColor = Palette.SurfaceTop)
                CauceButton(tr("Cancelar", "Cancel"), actions::cancelJob, Modifier.fillMaxWidth().testTag("vocal_cancel"), icon = Icons.Default.Close)
            }
        } else {
            val changed = process != null && process.params.copy(preset = "") != p.copy(preset = "")
            CauceButton(
                when { process == null -> tr("Procesar voz", "Process voice"); changed -> tr("Volver a procesar con estos ajustes", "Process again with these settings"); else -> tr("Volver a procesar", "Process again") },
                { actions.vocalProcess(clip.id, p) }, Modifier.fillMaxWidth().testTag("vocal_process"), kind = ButtonKind.Primary, icon = Icons.Default.AutoFixHigh, enabled = !state.recording && !clip.missing,
            )
        }

        // Comparar, aceptar o descartar (solo si existe una versión procesada).
        if (process != null) {
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.Radius)).background(Palette.Surface).padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(tr("Comparar", "Compare"), fontWeight = FontWeight.Bold)
                Segmented(listOf(tr("A · Original", "A · Original"), tr("B · Procesada", "B · Processed")), if (process.playingOriginal) 0 else 1,
                    { actions.setClipVersion(clip.id, it == 0) }, Modifier.fillMaxWidth().testTag("vocal_ab"), height = 44.dp)
                Text(tr("Pulsa Escuchar y cambia entre A y B: la música sigue sonando en el mismo punto.", "Press Listen and switch between A and B: playback keeps going at the same point."),
                    color = Palette.TextDim, fontSize = 12.sp)
                // Volumen igualado: lo más fuerte suele parecer mejor, así que B se compensa mientras se compara.
                if (abs(process.levelDb) >= 0.1f) {
                    Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                        PillToggle(tr("Igualar volumen", "Match volume"), process.match, { actions.setClipVersion(clip.id, process.playingOriginal, !process.match) },
                            color = Palette.Cyan, height = 40.dp, modifier = Modifier.testTag("vocal_match"))
                        Text((if (process.match) tr("B suena %+.1f dB para comparar con justicia. Al aceptar vuelve a su volumen real.", "B plays at %+.1f dB for a fair comparison. Accepting restores its real level.")
                            else tr("B es %+.1f dB respecto a A (volumen real).", "B is %+.1f dB relative to A (real level)."))
                            .format(if (process.match) -process.levelDb else process.levelDb), color = Palette.TextDim, fontSize = 11.sp, modifier = Modifier.weight(1f))
                    }
                }
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    CauceButton(tr("Escuchar", "Listen"), { actions.auditionClip(clip.id) }, Modifier.weight(1f).testTag("vocal_listen"), icon = Icons.Default.PlayArrow)
                    CauceButton(tr("Aceptar", "Accept"), { if (process.playingOriginal || process.match) actions.setClipVersion(clip.id, false, false); actions.openPanel(Panel.ARRANGE) },
                        Modifier.weight(1f).testTag("vocal_accept"), kind = ButtonKind.Primary, icon = Icons.Default.Check)
                }
                CauceButton(tr("Descartar el procesado", "Discard processing"), { actions.discardProcess(clip.id) }, Modifier.fillMaxWidth().testTag("vocal_discard"),
                    kind = ButtonKind.Danger, icon = Icons.Default.DeleteOutline)
                plugins.vocalResult?.takeIf { it.clipId == clip.id }?.let { r ->
                    Text(tr("Medido por el motor: latencia de %.0f ms compensada, cola de %.1f s, pico de entrada %.2f y de salida %.2f, calculado en %.1f s.",
                        "Measured by the engine: %.0f ms latency compensated, %.1f s tail, input peak %.2f, output peak %.2f, rendered in %.1f s.")
                        .format(r.latencySamples * 1000.0 / SAMPLE_RATE, r.tailSeconds, r.inputPeak, r.outputPeak, r.seconds), color = Palette.TextFaint, fontSize = 11.sp)
                }
            }
        }
        Spacer(Modifier.height(4.dp))
    }
}

/** Sin clip elegido: lista de clips de audio del proyecto (o cómo conseguir uno). */
@Composable
private fun VocalClipChooser(state: ProjectState, actions: StudioActions) {
    val clips: List<Pair<TrackState, ClipState>> = state.tracks.filter { it.isAudio }.flatMap { t -> t.clips.map { t to it } }
    if (clips.isEmpty()) {
        EmptyState(Icons.Default.Mic, tr("VocalShift necesita una voz", "VocalShift needs a voice"),
            tr("Graba o importa un audio en una pista de audio y vuelve aquí para procesarlo.", "Record or import audio on an audio track, then come back to process it."),
            Modifier.fillMaxSize(), action = tr("Ir al arreglo", "Go to the arrangement")) { actions.openPanel(Panel.ARRANGE) }
        return
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        PanelHeader("VocalShift", tr("Elige el clip de voz que quieres procesar", "Choose the voice clip to process"))
        clips.forEach { (t, c) ->
            Row(
                Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.RadiusMedium)).background(Palette.SurfaceHigh).clickable { actions.selectTrack(t.id); actions.selectClip(c.id) }
                    .padding(horizontal = 12.dp, vertical = 10.dp).testTag("vocal_clip_${c.id}").semantics { contentDescription = c.name },
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.size(10.dp).clip(RoundedCornerShape(3.dp)).background(Color(t.color.toInt())))
                Column(Modifier.weight(1f).padding(horizontal = 10.dp)) {
                    Text(c.name.ifBlank { tr("Clip de audio", "Audio clip") }, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text("${t.name} · %.1f s".format(c.length / SAMPLE_RATE.toDouble()) + if (c.process != null) tr(" · procesado", " · processed") else "", color = Palette.TextDim, fontSize = 12.sp)
                }
                androidx.compose.material3.Icon(Icons.Default.GraphicEq, null, tint = Palette.TextDim)
            }
        }
    }
}

@Composable
internal fun LabeledSlider(label: String, value: Float, min: Float, max: Float, format: String, tag: String, onChange: (Float) -> Unit) {
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, Modifier.weight(1f), fontSize = 13.sp, color = Palette.TextDim)
            Text(format.format(value), fontSize = 13.sp, fontWeight = FontWeight.Bold)
        }
        Slider(
            value.coerceIn(min, max), onChange, valueRange = min..max, modifier = Modifier.fillMaxWidth().height(40.dp).testTag(tag).semantics { contentDescription = label },
            colors = SliderDefaults.colors(thumbColor = Palette.Text, activeTrackColor = Palette.Violet, inactiveTrackColor = Palette.SurfaceTop),
        )
    }
}

/** Forma de onda del clip (resumen mín/máx del motor) con una banda de progreso opcional. */
@Composable
private fun ClipWave(clip: ClipState, peaks: FloatArray?, color: Color, progress: Float?) {
    val label = when { clip.process == null -> tr("Original", "Original"); clip.process.playingOriginal -> tr("Suena: A · Original", "Playing: A · Original"); else -> tr("Suena: B · Procesada", "Playing: B · Processed") }
    Box(Modifier.fillMaxWidth().height(76.dp).clip(RoundedCornerShape(Dims.RadiusMedium)).background(Palette.SurfaceHigh).testTag("vocal_wave")) {
        Canvas(Modifier.fillMaxSize()) {
            val mid = size.height / 2
            if (peaks != null && peaks.size >= 2) {
                val first = (clip.sourceOffset / WAVE_BLOCK).toInt()
                val count = ((clip.length + WAVE_BLOCK - 1) / WAVE_BLOCK).toInt().coerceAtLeast(1)
                val cols = size.width.toInt().coerceAtLeast(1)
                for (x in 0 until cols step 2) {
                    val b0 = first + (x.toLong() * count / cols).toInt()
                    val b1 = (first + ((x + 2).toLong() * count / cols).toInt()).coerceAtLeast(b0 + 1)
                    var lo = 0f; var hi = 0f
                    for (b in b0 until b1) { val i = b * 2; if (i + 1 < peaks.size) { lo = minOf(lo, peaks[i]); hi = maxOf(hi, peaks[i + 1]) } }
                    drawLine(color, Offset(x.toFloat(), mid - hi * mid * 0.92f), Offset(x.toFloat(), mid - lo * mid * 0.92f + 1f), strokeWidth = 1.5f)
                }
            } else drawLine(Palette.GridStrong, Offset(0f, mid), Offset(size.width, mid), strokeWidth = 1f)
            if (progress != null) drawRect(Palette.Violet.copy(alpha = 0.28f), Offset.Zero, Size(size.width * progress.coerceIn(0f, 1f), size.height))
        }
        Text(label, Modifier.padding(8.dp), fontSize = 11.sp, color = Palette.Text, fontWeight = FontWeight.Bold)
        Text("%.1f s".format(clip.length / SAMPLE_RATE.toDouble()), Modifier.align(Alignment.BottomEnd).padding(8.dp), fontSize = 11.sp, color = Palette.TextDim)
    }
}
