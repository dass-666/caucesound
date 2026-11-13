package com.caucesound.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.em
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
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.foundation.border
import kotlinx.coroutines.delay
import com.caucesound.app.data.JobUi
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
 *
 * Interfaz «Dark Rose / Neon Violet» (rediseño 0.8.1): vistas Fácil y Pro con la ilustración de la cantante. Solo
 * cambia la interfaz: cada control escribe exactamente el mismo campo de [VocalParams] que antes. Los parámetros de
 * VocalShift que el adaptador móvil todavía no expone aparecen con su valor real fijo y deshabilitados (ver
 * docs/VOCALSHIFT_UI.md).
 */
/** Duración de la escucha previa de VocalShift (el motor admite hasta 8 s). */
private const val PREVIEW_SECONDS = 6

/** Ajuste fino (cents) que lleva dentro el tono: el adaptador del motor guarda tono + cents en un solo valor. */
internal fun vocalPitchSplit(semitones: Float): Pair<Int, Int> {
    val st = semitones.roundToInt()
    return st to ((semitones - st) * 100f).roundToInt()
}
internal fun vocalPitchJoin(st: Int, cents: Int): Float = ((st * 100 + cents) / 100f).coerceIn(-24f, 24f)

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun VocalShiftPanel(
    state: ProjectState, selectedClip: String?, plugins: PluginUi, waveforms: Map<String, FloatArray>, actions: StudioActions,
    trackPeak: (String) -> Float = { 0f }, playing: Boolean = false,
) {
    val sel = selectedClip?.let { state.clip(it) }
    if (sel == null) { VocalClipChooser(state, actions); return }
    val (track, clip) = sel
    val initial = clip.process?.params ?: plugins.vocalPresets.firstOrNull { it.id == "urban_spatial" }?.params ?: VocalParams()
    var p by remember(clip.id, clip.process?.params) { mutableStateOf(initial) }
    var pro by remember { mutableStateOf(false) }
    val job = plugins.job?.takeIf { it.type == "vocalshift" && it.targetId == clip.id && it.active }
    val process = clip.process
    fun edit(next: VocalParams) { p = next }
    // «Igualar volumen» es solo para comparar: al salir de la pantalla (o cambiar de clip) se apaga siempre.
    val matchOn by rememberUpdatedState(process?.match == true)
    val previewOn by rememberUpdatedState(state.vocalPreview?.let { it.clipId == clip.id && it.listening } == true)
    DisposableEffect(clip.id) {
        onDispose {
            if (matchOn) actions.setClipVersion(clip.id, false, false)
            if (previewOn) actions.previewListen(clip.id, false)
        }
    }
    val previewJob = plugins.job?.takeIf { it.type == "vocalPreview" && it.targetId == clip.id && it.active }
    val baseLength = process?.origLength ?: clip.length
    val previewLength = minOf(baseLength, PREVIEW_SECONDS * SAMPLE_RATE.toLong())
    var previewFrom by remember(clip.id) { mutableStateOf(state.vocalPreview?.takeIf { it.clipId == clip.id && baseLength > previewLength }?.from ?: 0L) }
    val peak = remember(track.id) { { trackPeak(track.id) } }

    val top: @Composable () -> Unit = {
        VsTopBar(clip.name.ifBlank { tr("Clip de audio", "Audio clip") } + " · " + track.name, p, plugins, { edit(it) }, pro, { pro = it }, process, actions, clip.id)
    }
    val illustration: @Composable (Modifier) -> Unit = { m ->
        VsStage(m, peak, playing, p.secondVoice) { edit(p.copy(secondVoice = it, preset = "")) }
    }
    val controls: @Composable () -> Unit = {
        if (pro) VsProControls(p, plugins, process) { edit(it) } else VsEasyControls(p, plugins) { edit(it) }
    }
    val workflow: @Composable () -> Unit = {
        VsWorkflow(state, clip, track, p, process, job, previewJob, previewFrom, { previewFrom = it }, baseLength, previewLength, waveforms, plugins, actions)
    }

    // Interlineado compacto para textos pequeños (el estilo base del tema trae 24 sp de interlínea).
    androidx.compose.runtime.CompositionLocalProvider(androidx.compose.material3.LocalTextStyle provides androidx.compose.material3.LocalTextStyle.current.copy(lineHeight = 1.3.em)) {
    BoxWithConstraints(Modifier.fillMaxSize().background(VS.Bg).testTag("vocal_screen")) {
        val wide = maxWidth >= 600.dp && maxWidth > maxHeight
        if (wide) {
            Row(Modifier.fillMaxSize().padding(horizontal = 12.dp, vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Column(Modifier.weight(0.42f).fillMaxHeight(), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    top()
                    illustration(Modifier.fillMaxWidth().weight(1f))
                }
                Column(Modifier.weight(0.58f).fillMaxHeight().verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    controls()
                    workflow()
                    Spacer(Modifier.height(4.dp))
                }
            }
        } else {
            val stage = (maxHeight * 0.38f).coerceIn(220.dp, 480.dp)
            Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                top()
                illustration(Modifier.fillMaxWidth().height(stage))
                controls()
                workflow()
                Spacer(Modifier.height(4.dp))
            }
        }
    }
    }
}

/** Barra superior: nombre, Bypass, selector de presets y Fácil | Pro. */
@Composable
private fun VsTopBar(subtitle: String, p: VocalParams, plugins: PluginUi, onPreset: (VocalParams) -> Unit, pro: Boolean, onPro: (Boolean) -> Unit,
                     process: com.caucesound.app.data.ClipProcess?, actions: StudioActions, clipId: String) {
    var sheet by remember { mutableStateOf(false) }
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("VOCALSHIFT", fontSize = 20.sp, fontWeight = FontWeight.Bold, letterSpacing = 0.14.em, color = VS.Text, maxLines = 1)
                Text("AI STUDIO · $subtitle", fontSize = VS.Secondary, fontWeight = FontWeight.SemiBold, letterSpacing = 0.12.em, color = VS.Rose, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            // Bypass = escuchar la toma original (A). Solo existe cuando hay una versión procesada: el efecto no corre
            // en directo, así que no hay nada que puentear antes de procesar.
            Box(Modifier.width(118.dp)) {
                VsSwitch("Bypass", process?.playingOriginal == true, { process?.let { actions.setClipVersion(clipId, !it.playingOriginal) } },
                    enabled = process != null, tag = "vocal_bypass")
            }
            ToolButton(Icons.Default.SwapHoriz, tr("Elegir otro clip", "Choose another clip"), { actions.selectClip(null) }, tint = VS.Text)
        }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            val name = plugins.vocalPresets.firstOrNull { it.id == p.preset }?.name ?: tr("Personalizado", "Custom")
            Row(
                Modifier.weight(1f).height(44.dp).clip(RoundedCornerShape(10.dp)).background(Color(0xFF07060C)).border(1.dp, VS.PanelEdge, RoundedCornerShape(10.dp))
                    .clickable(enabled = plugins.vocalPresets.isNotEmpty()) { sheet = true }.padding(horizontal = 12.dp).testTag("vocal_preset_picker")
                    .semantics { contentDescription = tr("Preset: ", "Preset: ") + name },
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Text(name, Modifier.weight(1f), fontSize = 14.sp, color = VS.Text, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Canvas(Modifier.size(12.dp, 8.dp)) {
                    val path = androidx.compose.ui.graphics.Path().apply { moveTo(0f, 0f); lineTo(size.width, 0f); lineTo(size.width / 2, size.height); close() }
                    drawPath(path, VS.Rose)
                }
            }
            VsSegmented(listOf(tr("Fácil", "Easy"), "Pro"), if (pro) 1 else 0, { onPro(it == 1) }, Modifier.width(150.dp), tag = "vocal_view")
        }
    }
    if (sheet) VsPresetSheet(plugins.vocalPresets, p.preset, { sheet = false }) { onPreset(it.params); sheet = false }
}

/** Hoja inferior con la lista de presets de fábrica (del motor). */
@OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)
@Composable
private fun VsPresetSheet(presets: List<com.caucesound.app.data.VocalPreset>, current: String, onDismiss: () -> Unit, onPick: (com.caucesound.app.data.VocalPreset) -> Unit) {
    androidx.compose.material3.ModalBottomSheet(onDismiss, containerColor = VS.Panel, contentColor = VS.Text) {
        Column(Modifier.fillMaxWidth().verticalScroll(rememberScrollState()).padding(horizontal = 16.dp).padding(bottom = 24.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(tr("PRESETS DE FÁBRICA", "FACTORY PRESETS"), style = VS.Title)
            presets.forEach { pr ->
                val on = pr.id == current
                Column(
                    Modifier.fillMaxWidth().defaultMinSize(minHeight = 52.dp).clip(RoundedCornerShape(10.dp)).background(if (on) VS.PanelHi else Color.Transparent)
                        .border(1.dp, if (on) VS.Rose else VS.PanelEdge, RoundedCornerShape(10.dp)).clickable { onPick(pr) }.padding(horizontal = 12.dp, vertical = 8.dp)
                        .testTag("vocal_preset_${pr.id}"),
                ) {
                    Text(pr.name, fontSize = 15.sp, fontWeight = if (on) FontWeight.Bold else FontWeight.Medium, color = if (on) VS.RoseSoft else VS.Text)
                    if (pr.description.isNotBlank()) Text(pr.description, fontSize = VS.Secondary, color = VS.Dim)
                }
            }
        }
    }
}

/** Ilustración con los medidores IN / OUT en los laterales y el selector de ruta flotando abajo. */
@Composable
private fun VsStage(modifier: Modifier, peak: () -> Float, playing: Boolean, secondVoice: Boolean, onRoute: (Boolean) -> Unit) {
    Box(modifier.clip(RoundedCornerShape(VS.PanelRadius)).border(1.dp, VS.PanelEdge, RoundedCornerShape(VS.PanelRadius))) {
        SingerIllustration(peak, playing, Modifier.fillMaxSize())
        // IN: VocalShift procesa offline, así que no hay una entrada en directo que medir; se muestra apagado.
        VsMeter({ 0f }, "IN", Modifier.align(Alignment.CenterStart).fillMaxHeight().padding(vertical = 10.dp), enabled = false, tag = "vocal_meter_in")
        // OUT: pico real de la pista que suena (el mismo que publica el motor para el mezclador).
        VsMeter(peak, "OUT", Modifier.align(Alignment.CenterEnd).fillMaxHeight().padding(vertical = 10.dp), tag = "vocal_meter_out")
        VsSegmented(listOf(tr("Transformar", "Transform"), tr("Segunda voz", "Second voice")), if (secondVoice) 1 else 0, { onRoute(it == 1) },
            Modifier.align(Alignment.BottomCenter).padding(horizontal = 44.dp, vertical = 12.dp).fillMaxWidth(), tag = "vocal_second")
    }
}

private fun modeText(p: VocalParams): String {
    val route = if (p.secondVoice) tr("Segunda voz: tu voz queda al frente sin cambios; detrás, una voz procesada ancha y con espacio (Magia).",
        "Second voice: your voice stays in front, unchanged; behind it, a wide processed voice with space (Magic).")
    else tr("Transformar: toda la voz pasa por VocalShift.", "Transform: the whole voice goes through VocalShift.")
    val mode = if (p.radical) tr("Radical: los formantes siguen al tono y el carácter cambia por completo.", "Radical: formants follow the pitch and the character changes completely.")
    else tr("Identidad: los formantes se mantienen cerca de los originales.", "Identity: formants stay close to the originals.")
    return "$route $mode"
}

private val PitchFormat = VsFormat { v -> "%d st".format(v.roundToInt()).replace('-', '−') }
private val PercentFormat = VsFormat { v -> "%d %%".format(v.roundToInt()) }

@Composable
private fun PitchKnob(p: VocalParams, edit: (VocalParams) -> Unit, size: Dp) {
    val (st, ct) = vocalPitchSplit(p.semitones)
    VsKnob("Pitch", st.toFloat(), -24f, 24f, 7f, { edit(p.copy(semitones = vocalPitchJoin(it.roundToInt(), ct), preset = "")) }, PitchFormat,
        size = size, scale = listOf(-24f to "−24", -12f to "−12", 0f to "0", 12f to "+12", 24f to "+24"), unit = "st", tag = "vocal_pitch", valueSize = 22.sp,
        description = tr("Altura de la voz procesada (semitonos).", "Pitch of the processed voice (semitones)."))
}

/** Vista Fácil: PITCH grande en el centro con los atajos, Carácter y Magia a los lados. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun VsEasyControls(p: VocalParams, plugins: PluginUi, edit: (VocalParams) -> Unit) {
    VsPanel(null) {
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp, Alignment.CenterHorizontally)) {
            val (st, ct) = vocalPitchSplit(p.semitones)
            listOf(-12, 12).forEach { v ->
                Box(Modifier.width(76.dp).height(44.dp).clip(CircleShape).background(if (st == v) VS.PanelHi else Color.Transparent).border(1.dp, if (st == v) VS.Rose else VS.PanelEdge, CircleShape)
                    .clickable { edit(p.copy(semitones = vocalPitchJoin(v, ct), preset = "")) }.testTag("vocal_pitch_$v"), contentAlignment = Alignment.Center) {
                    Text(if (v < 0) "−12" else "+12", fontSize = 16.sp, color = VS.Text)
                }
            }
        }
        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween, verticalAlignment = Alignment.Top) {
            VsKnob(tr("Carácter", "Character"), p.character, 0f, 100f, 65f, { edit(p.copy(character = it.roundToInt().toFloat(), preset = "")) }, PercentFormat,
                Modifier.weight(1f), size = 76.dp, unit = "%", tag = "vocal_character", description = tr("Cuerpo, presencia, color y textura.", "Body, presence, colour and texture."))
            Box(Modifier.weight(1.7f), contentAlignment = Alignment.TopCenter) { PitchKnob(p, edit, 144.dp) }
            VsKnob(tr("Magia", "Magic"), p.magic, 0f, 100f, 70f, { edit(p.copy(magic = it.roundToInt().toFloat(), preset = "")) }, PercentFormat,
                Modifier.weight(1f), size = 76.dp, unit = "%", tag = "vocal_magic", description = tr("Espacio y acabado: anchura, reverb, eco.", "Space and finish: width, reverb, echo."))
        }
        VsSegmented(listOf(tr("Identidad", "Identity"), "Radical"), if (p.radical) 1 else 0, { edit(p.copy(radical = it == 1, preset = "")) }, Modifier.fillMaxWidth(), tag = "vocal_mode")
        Text(modeText(p), fontSize = VS.Secondary, color = VS.Dim)
    }
    if (p.secondVoice) VsPanel(tr("2.ª voz", "2nd voice")) { VsFader(p.secondGainDb, { edit(p.copy(secondGainDb = it, preset = "")) }, presetGain(p, plugins)) }
    VsPanel(null) {
        VsSwitch(tr("Espacio (2.ª voz)", "Space (2nd voice)"), p.spatial, { edit(p.copy(spatial = !p.spatial, preset = "")) }, enabled = p.secondVoice, tag = "vocal_spatial",
            note = if (p.secondVoice) null else tr("Solo actúa con la ruta Segunda voz.", "Only works with the Second voice route."))
        LockedSwitches(listOf(tr("Nivel automático", "Auto level") to true, tr("Protección de picos", "Peak protection") to true))
    }
    EffectiveLine(p)
}

private fun presetGain(p: VocalParams, plugins: PluginUi) = plugins.vocalPresets.firstOrNull { it.id == p.preset }?.params?.secondGainDb ?: VocalParams().secondGainDb

/** Parámetros de VocalShift que el adaptador móvil fija: se ve su valor real, sin poder cambiarlo. */
@Composable
private fun LockedSwitches(items: List<Pair<String, Boolean>>) {
    items.forEach { (label, on) -> VsSwitch(label, on, {}, enabled = false, note = tr("Fijo en el móvil", "Fixed on mobile")) }
}

@Composable
private fun EffectiveLine(p: VocalParams) {
    val parts = mutableListOf(
        tr("tono %+.2f st", "pitch %+.2f st").format(p.semitones),
        tr("carácter %d %%", "character %d %%").format(p.character.roundToInt()),
        tr("magia %d %%", "magic %d %%").format(p.magic.roundToInt()),
        tr("formantes %+.1f st", "formants %+.1f st").format(p.formantOffset),
        tr("brillo %+.1f dB", "brightness %+.1f dB").format(p.brightnessOffset),
    )
    if (p.secondVoice) parts += tr("2.ª voz %s · pan %+d · desfase %d ms", "2nd voice %s · pan %+d · offset %d ms").format(vsDb(p.secondGainDb), p.pan.roundToInt(), p.offsetMs.roundToInt())
    else parts += tr("mezcla %d %%", "mix %d %%").format(p.mixPercent.roundToInt())
    Text(tr("Efectivo: ", "Effective: ") + parts.joinToString(" · ").replace('-', '−'), fontSize = VS.Secondary, color = VS.Dim, modifier = Modifier.testTag("vocal_effective"))
}

/** Vista Pro: los mismos paneles que el escritorio, en una columna. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun VsProControls(p: VocalParams, plugins: PluginUi, process: com.caucesound.app.data.ClipProcess?, edit: (VocalParams) -> Unit) {
    val fixed = tr("Fijo en el móvil", "Fixed on mobile")
    VsSegmented(listOf(tr("Identidad", "Identity"), "Radical"), if (p.radical) 1 else 0, { edit(p.copy(radical = it == 1, preset = "")) }, Modifier.fillMaxWidth(), tag = "vocal_mode")
    Text(modeText(p), fontSize = VS.Secondary, color = VS.Dim)
    val knob = 84.dp
    VsPanel("Pitch") {
        FlowRow(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
            PitchKnob(p, edit, 132.dp)
            val (st, ct) = vocalPitchSplit(p.semitones)
            VsKnob(tr("Ajuste fino", "Fine"), ct.toFloat(), -100f, 100f, 0f, { edit(p.copy(semitones = vocalPitchJoin(st, it.roundToInt()), preset = "")) },
                { "%+d ct".format(it.roundToInt()).replace('-', '−') }, size = knob, unit = "ct", tag = "vocal_fine")
        }
    }
    VsPanel(tr("Timbre", "Timbre")) {
        FlowRow(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
            VsKnob(tr("Carácter", "Character"), p.character, 0f, 100f, 65f, { edit(p.copy(character = it.roundToInt().toFloat(), preset = "")) }, PercentFormat, size = knob, unit = "%", tag = "vocal_character")
            VsKnob(tr("Formantes", "Formants"), p.formantOffset, -12f, 12f, 0f, { edit(p.copy(formantOffset = (it * 2).roundToInt() / 2f, preset = "")) },
                { "%+.1f st".format(it).replace('-', '−') }, size = knob, unit = "st", tag = "vocal_formant")
            VsKnob(tr("Brillo", "Brightness"), p.brightnessOffset, -12f, 12f, 0f, { edit(p.copy(brightnessOffset = (it * 2).roundToInt() / 2f, preset = "")) },
                { "%+.1f dB".format(it).replace('-', '−') }, size = knob, unit = "dB", tag = "vocal_brightness")
        }
    }
    VsPanel(tr("Dinámica", "Dynamics")) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            VsKnob(tr("Magia", "Magic"), p.magic, 0f, 100f, 70f, { edit(p.copy(magic = it.roundToInt().toFloat(), preset = "")) }, PercentFormat, size = knob, unit = "%", tag = "vocal_magic")
            Column(Modifier.weight(1f).padding(start = 8.dp)) { LockedSwitches(listOf(tr("Nivel automático", "Auto level") to true, tr("Protección de picos", "Peak protection") to true)) }
        }
    }
    VsPanel(tr("Espacio", "Space")) {
        FlowRow(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
            listOf(tr("Anchura", "Width"), "Reverb", tr("Eco corto", "Short echo")).forEach { l ->
                VsKnob(l, 100f, 0f, 100f, 100f, {}, PercentFormat, size = 76.dp, enabled = false, disabledNote = fixed)
            }
        }
        VsSwitch(tr("Espacio (2.ª voz)", "Space (2nd voice)"), p.spatial, { edit(p.copy(spatial = !p.spatial, preset = "")) }, enabled = p.secondVoice, tag = "vocal_spatial",
            note = if (p.secondVoice) null else tr("Solo actúa con la ruta Segunda voz.", "Only works with the Second voice route."))
    }
    VsPanel(tr("Mezcla", "Mix")) {
        FlowRow(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly, verticalArrangement = Arrangement.spacedBy(8.dp)) {
            val onlyT = tr("Solo con Transformar", "Transform only")
            val onlyS = tr("Solo con Segunda voz", "Second voice only")
            VsKnob(tr("Mezcla", "Mix"), p.mixPercent, 0f, 100f, 100f, { edit(p.copy(mixPercent = it.roundToInt().toFloat(), preset = "")) }, PercentFormat,
                size = knob, unit = "%", tag = "vocal_mix", enabled = !p.secondVoice, disabledNote = onlyT)
            VsKnob(tr("Pan 2.ª voz", "2nd voice pan"), p.pan, -100f, 100f, 0f, { edit(p.copy(pan = it.roundToInt().toFloat(), preset = "")) },
                { "%+d".format(it.roundToInt()).replace('-', '−') }, size = knob, tag = "vocal_pan", enabled = p.secondVoice, disabledNote = onlyS)
            VsKnob(tr("Desfase", "Offset"), p.offsetMs, 0f, 100f, 0f, { edit(p.copy(offsetMs = it.roundToInt().toFloat(), preset = "")) },
                { "%d ms".format(it.roundToInt()) }, size = knob, unit = "ms", tag = "vocal_offset", enabled = p.secondVoice, disabledNote = onlyS)
            VsKnob(tr("Ganancia salida", "Output gain"), 0f, -24f, 12f, 0f, {}, { "%+.1f dB".format(it) }, size = knob, enabled = false, disabledNote = fixed)
        }
    }
    if (p.secondVoice) VsPanel(tr("2.ª voz", "2nd voice")) { VsFader(p.secondGainDb, { edit(p.copy(secondGainDb = it, preset = "")) }, presetGain(p, plugins)) }
    VsPanel(tr("Calidad y diagnóstico", "Quality and diagnostics")) {
        VsSwitch(tr("DSP mejorado", "Improved DSP"), true, {}, enabled = false, note = fixed)
        VsSwitch(tr("Motor experimental PSOLA", "Experimental PSOLA engine"), false, {}, enabled = false,
            note = tr("No incluido en la versión de VocalShift integrada.", "Not included in the integrated VocalShift version."))
        Text(tr("Motor: %s, procesado offline en el teléfono (no apto para monitoreo en vivo: 150 ms de latencia, compensada al procesar).",
            "Engine: %s, rendered offline on the phone (not for live monitoring: 150 ms latency, compensated when rendering).")
            .format(process?.version?.takeIf { it.isNotBlank() } ?: "vocalshift-ai-studio@10e679c"), fontSize = VS.Secondary, color = VS.Dim)
    }
    EffectiveLine(p)
}

/** El flujo de trabajo de siempre (forma de onda, Ajustar escuchando, prueba, procesar y comparar) con la nueva piel. */
@Composable
private fun VsWorkflow(
    state: ProjectState, clip: ClipState, track: TrackState, p: VocalParams, process: com.caucesound.app.data.ClipProcess?, job: JobUi?, previewJob: JobUi?,
    previewFrom: Long, setPreviewFrom: (Long) -> Unit, baseLength: Long, previewLength: Long, waveforms: Map<String, FloatArray>, plugins: PluginUi, actions: StudioActions,
) {
    Text(tr("Se calcula en el teléfono, sin conexión. «Ajustar escuchando» recalcula un fragmento al mover cada control; no es un efecto en directo sobre el micrófono. Tu grabación original no se modifica.",
        "Rendered on the phone, offline. “Adjust while listening” re-renders a section each time you move a control; it is not a live microphone effect. Your original recording is never modified."),
        color = VS.Dim, fontSize = VS.Secondary)

    // Forma de onda de la versión que suena, con el avance real del proceso encima.
    ClipWave(clip, waveforms[clip.sourceId], Color(track.color.toInt()), job?.progress)

    // 0.8 — Ajustar escuchando: la región suena en bucle y cada cambio de perilla se recalcula solo en segundo plano.
    if (job == null) ContinuousListen(state, clip, p, previewFrom, previewLength, previewJob, actions)

    // Escucha previa: solo una región corta, calculada en segundo plano. No es monitorización en directo.
    if (job == null) {
        VsPanel(null) {
            Text(tr("Prueba rápida", "Quick test"), fontWeight = FontWeight.Bold, color = VS.Text)
            Text(tr("Procesa solo %.0f s de la toma para oír estos ajustes antes de procesarla entera. Se calcula primero y suena después.",
                "Processes only %.0f s of the take so you can hear these settings before processing all of it. It renders first, then plays.")
                .format(previewLength / SAMPLE_RATE.toDouble()), color = VS.Dim, fontSize = VS.Secondary)
            if (baseLength > previewLength) {
                val maxStart = (baseLength - previewLength) / SAMPLE_RATE.toFloat()
                LabeledSlider(tr("Inicio de la prueba", "Test start"), previewFrom / SAMPLE_RATE.toFloat(), 0f, maxStart, "%.1f s", "vocal_preview_from") {
                    setPreviewFrom((it * SAMPLE_RATE).toLong().coerceIn(0L, baseLength - previewLength))
                }
            }
            if (previewJob != null) {
                Text(tr("Calculando la prueba… ${(previewJob.progress * 100).roundToInt()} %", "Rendering the test… ${(previewJob.progress * 100).roundToInt()} %"),
                    fontWeight = FontWeight.SemiBold, color = VS.Text, modifier = Modifier.testTag("vocal_preview_progress"))
                LinearProgressIndicator(progress = { previewJob.progress.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth(), color = VS.Rose, trackColor = VS.Groove)
                CauceButton(tr("Cancelar", "Cancel"), actions::cancelJob, Modifier.fillMaxWidth().testTag("vocal_preview_cancel"), icon = Icons.Default.Close)
            } else {
                CauceButton(tr("Escuchar prueba", "Hear a test"), { actions.vocalPreview(clip.id, p, previewFrom, previewLength) },
                    Modifier.fillMaxWidth().testTag("vocal_preview"), icon = Icons.Default.PlayArrow, enabled = !state.recording && !clip.missing)
                state.vocalPreview?.takeIf { it.clipId == clip.id }?.let { pv ->
                    VsSegmented(listOf(tr("Original", "Original"), tr("Prueba", "Test")), if (pv.listening) 1 else 0,
                        { actions.previewListen(clip.id, it == 1) }, Modifier.fillMaxWidth(), tag = "vocal_preview_ab")
                    val stale = pv.params.copy(preset = "") != p.copy(preset = "") || (baseLength > previewLength && pv.from != previewFrom)
                    Text(if (stale) tr("Has cambiado los ajustes o el inicio: pulsa «Escuchar prueba» para oírlos.", "Settings or start changed: tap “Hear a test” to hear them.")
                        else tr("La prueba suena en su sitio dentro de la toma; el resto es el original. No cambia el proyecto ni lo que se exporta.",
                            "The test plays in place inside the take; the rest is the original. The project and exports are not changed."),
                        color = if (stale) VS.Warn else VS.Dim, fontSize = VS.Secondary)
                }
            }
        }
    }

    // Procesar, con progreso real y cancelación.
    if (job != null) {
        VsPanel(null) {
            Text(tr("Procesando… ${(job.progress * 100).roundToInt()} %", "Processing… ${(job.progress * 100).roundToInt()} %"), fontWeight = FontWeight.Bold, color = VS.Text,
                modifier = Modifier.testTag("vocal_progress"))
            LinearProgressIndicator(progress = { job.progress.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth(), color = VS.Rose, trackColor = VS.Groove)
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
        VsPanel(tr("Comparar", "Compare")) {
            VsSegmented(listOf(tr("A · Original", "A · Original"), tr("B · Procesada", "B · Processed")), if (process.playingOriginal) 0 else 1,
                { actions.setClipVersion(clip.id, it == 0) }, Modifier.fillMaxWidth(), tag = "vocal_ab")
            Text(tr("Pulsa Escuchar y cambia entre A y B: la música sigue sonando en el mismo punto.", "Press Listen and switch between A and B: playback keeps going at the same point."),
                color = VS.Dim, fontSize = VS.Secondary)
            // Volumen igualado: lo más fuerte suele parecer mejor, así que B se compensa mientras se compara.
            if (abs(process.levelDb) >= 0.1f) {
                VsSwitch(tr("Igualar volumen", "Match volume"), process.match, { actions.setClipVersion(clip.id, process.playingOriginal, !process.match) }, tag = "vocal_match",
                    note = (if (process.match) tr("Solo para comparar: B se escucha a %+.1f dB. No cambia el proyecto ni lo que se exporta.", "For comparing only: B is heard at %+.1f dB. The project and exports are not changed.")
                        else tr("B es %+.1f dB respecto a A (volumen real).", "B is %+.1f dB relative to A (real level)."))
                        .format(if (process.match) -process.levelDb else process.levelDb))
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
                    .format(r.latencySamples * 1000.0 / SAMPLE_RATE, r.tailSeconds, r.inputPeak, r.outputPeak, r.seconds), color = VS.Dim, fontSize = VS.Secondary)
            }
        }
    }
}

/**
 * «Ajustar escuchando» (0.8). Lo más cerca de un plugin en directo que permite un motor con 150 ms de latencia y
 * coste alto: la región elegida suena en bucle, cada cambio se recalcula en segundo plano (250 ms después del último
 * movimiento, y la prueba anterior se cancela) y el resultado sustituye al anterior sin parar la reproducción.
 * Entre mover la perilla y oírla pasa el tiempo de cálculo de la región (medido y mostrado). No toca el proyecto ni la
 * exportación: para que quede guardado hay que pulsar «Procesar voz».
 */
@Composable
private fun ContinuousListen(state: ProjectState, clip: ClipState, p: VocalParams, from: Long, length: Long, previewJob: JobUi?, actions: StudioActions) {
    var on by remember(clip.id) { mutableStateOf(false) }
    var wantProcessed by remember(clip.id) { mutableStateOf(true) }
    var savedLoop by remember(clip.id) { mutableStateOf<Triple<Boolean, Long, Long>?>(null) }
    val pv = state.vocalPreview?.takeIf { it.clipId == clip.id }
    val st by rememberUpdatedState(state)
    fun loopTicks(): Pair<Long, Long> = st.framesToTicks(clip.start + from) to st.framesToTicks(clip.start + from + length)
    fun stopListening() {
        on = false
        savedLoop?.let { (e, a, b) -> actions.setLoop(e, a, b) }
        savedLoop = null
        actions.previewListen(clip.id, false)
    }
    // Cada cambio de ajustes (o de región) con la escucha activa pide una prueba nueva tras una pausa corta.
    LaunchedEffect(on, p, from, length) {
        if (!on) return@LaunchedEffect
        delay(250)
        actions.vocalPreview(clip.id, p, from, length)
    }
    // En cuanto llega la primera prueba, se escucha (si el usuario no eligió «Original»).
    LaunchedEffect(on, pv != null, wantProcessed) {
        if (on && pv != null && pv.listening != wantProcessed) actions.previewListen(clip.id, wantProcessed)
    }
    DisposableEffect(clip.id) { onDispose { if (on) stopListening() } }
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.Radius)).background(if (on) VS.PanelHi else VS.Panel)
        .border(1.dp, if (on) VS.Rose else VS.PanelEdge, RoundedCornerShape(Dims.Radius)).padding(12.dp).testTag("vocal_continuous"),
        verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(tr("Ajustar escuchando", "Adjust while listening"), fontWeight = FontWeight.Bold, color = VS.Text)
                Text(tr("%.0f s en bucle; cada cambio se oye cuando termina de calcularse.", "%.0f s looped; each change is heard once it finishes rendering.")
                    .format(length / SAMPLE_RATE.toDouble()), color = VS.Dim, fontSize = 12.sp)
            }
            PillToggle(if (on) tr("Activado", "On") else tr("Activar", "Turn on"), on, {
                if (on) stopListening() else {
                    on = true
                    savedLoop = Triple(st.loopEnabled, st.loopStart, st.loopEnd)
                    val (a, b) = loopTicks()
                    actions.setLoop(true, a, b)
                    actions.seek(clip.start + from)
                    actions.play()
                }
            }, color = VS.Rose, height = 44.dp, modifier = Modifier.testTag("vocal_continuous_toggle"))
        }
        if (on) {
            VsSegmented(listOf(tr("Original", "Original"), tr("Con VocalShift", "With VocalShift")), if (wantProcessed) 1 else 0,
                { wantProcessed = it == 1; if (pv != null) actions.previewListen(clip.id, wantProcessed) }, Modifier.fillMaxWidth(), tag = "vocal_continuous_ab")
            val status = when {
                previewJob != null -> tr("Calculando tus ajustes… ${(previewJob.progress * 100).roundToInt()} %", "Rendering your settings… ${(previewJob.progress * 100).roundToInt()} %")
                pv == null -> tr("Preparando la primera prueba…", "Preparing the first test…")
                pv.params.copy(preset = "") != p.copy(preset = "") -> tr("Ajustes cambiados: se recalculan en un momento.", "Settings changed: re-rendering shortly.")
                else -> tr("Suena con estos ajustes. No cambia el proyecto: pulsa «Procesar voz» para guardarlos.", "Playing with these settings. The project is unchanged: tap “Process voice” to keep them.")
            }
            Text(status, color = if (previewJob != null) VS.Rose else VS.Dim, fontSize = 12.sp, modifier = Modifier.testTag("vocal_continuous_status"))
            pv?.seconds?.takeIf { it > 0 }?.let { sec ->
                Text(tr("Último cálculo: %.1f s para %.0f s de voz.", "Last render: %.1f s for %.0f s of voice.").format(sec, length / SAMPLE_RATE.toDouble()), color = VS.Dim, fontSize = 12.sp)
            }
        }
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
                Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.RadiusMedium)).background(VS.PanelHi).clickable { actions.selectTrack(t.id); actions.selectClip(c.id) }
                    .padding(horizontal = 12.dp, vertical = 10.dp).testTag("vocal_clip_${c.id}").semantics { contentDescription = c.name },
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.size(10.dp).clip(RoundedCornerShape(3.dp)).background(Color(t.color.toInt())))
                Column(Modifier.weight(1f).padding(horizontal = 10.dp)) {
                    Text(c.name.ifBlank { tr("Clip de audio", "Audio clip") }, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text("${t.name} · %.1f s".format(c.length / SAMPLE_RATE.toDouble()) + if (c.process != null) tr(" · procesado", " · processed") else "", color = VS.Dim, fontSize = 12.sp)
                }
                androidx.compose.material3.Icon(Icons.Default.GraphicEq, null, tint = VS.Dim)
            }
        }
    }
}

@Composable
internal fun LabeledSlider(label: String, value: Float, min: Float, max: Float, format: String, tag: String, onChange: (Float) -> Unit) {
    Column {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(label, Modifier.weight(1f), fontSize = 13.sp, color = VS.Dim)
            Text(format.format(value), fontSize = 13.sp, fontWeight = FontWeight.Bold)
        }
        Slider(
            value.coerceIn(min, max), onChange, valueRange = min..max, modifier = Modifier.fillMaxWidth().height(40.dp).testTag(tag).semantics { contentDescription = label },
            colors = SliderDefaults.colors(thumbColor = Palette.Text, activeTrackColor = VS.Violet, inactiveTrackColor = VS.Groove),
        )
    }
}

/** Forma de onda del clip (resumen mín/máx del motor) con una banda de progreso opcional. */
@Composable
private fun ClipWave(clip: ClipState, peaks: FloatArray?, color: Color, progress: Float?) {
    val label = when { clip.process == null -> tr("Original", "Original"); clip.process.playingOriginal -> tr("Suena: A · Original", "Playing: A · Original"); else -> tr("Suena: B · Procesada", "Playing: B · Processed") }
    Box(Modifier.fillMaxWidth().height(76.dp).clip(RoundedCornerShape(Dims.RadiusMedium)).background(VS.PanelHi).testTag("vocal_wave")) {
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
            } else drawLine(VS.PanelEdge, Offset(0f, mid), Offset(size.width, mid), strokeWidth = 1f)
            if (progress != null) drawRect(VS.Violet.copy(alpha = 0.28f), Offset.Zero, Size(size.width * progress.coerceIn(0f, 1f), size.height))
        }
        Text(label, Modifier.padding(8.dp), fontSize = 12.sp, color = Palette.Text, fontWeight = FontWeight.Bold)
        Text("%.1f s".format(clip.length / SAMPLE_RATE.toDouble()), Modifier.align(Alignment.BottomEnd).padding(8.dp), fontSize = 12.sp, color = VS.Dim)
    }
}
