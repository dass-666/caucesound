package com.caucesound.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.ExpandLess
import androidx.compose.material.icons.filled.ExpandMore
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.LibraryMusic
import androidx.compose.material.icons.filled.RecordVoiceOver
import androidx.compose.material3.Icon
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.data.Catalog
import com.caucesound.app.data.EffectInfo
import com.caucesound.app.data.FxChainInfo
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.TrackState

/*
 * Mixer Pro (misión 0.7). Las ranuras son las inserciones reales del motor (Session::addInsert…): el orden visible es
 * el orden de procesamiento, el bypass es real y todo se guarda con el proyecto. Aquí no hay listas decorativas.
 */

/**
 * Orden y nombres de las categorías del explorador de plugins (0.9.2: nombres pedidos por Diego). Solo se muestran las
 * que tienen algún efecto en el catálogo del motor: hoy no hay efectos de modulación, así que esa categoría no aparece.
 * Un efecto con una categoría nueva que la app no conozca va a «Otros» (el catálogo es ampliable desde el motor).
 */
val FxCategories: List<Pair<String, String>> get() = listOf(
    "eq" to tr("EQ y filtros", "EQ & filters"),
    "dynamics" to tr("Compresión y dinámica", "Compression & dynamics"),
    "reverb" to tr("Reverb y espacios", "Reverb & spaces"),
    "delay" to "Delay",
    "distortion" to tr("Saturación y distorsión", "Saturation & distortion"),
    "modulation" to tr("Modulación", "Modulation"),
    "vocal" to tr("Procesamiento vocal", "Vocal processing"),
    "tools" to tr("Herramientas musicales", "Music tools"),
)

fun EffectInfo.label(): String = (if (isEnglish()) nameEn else name).ifBlank { effectName(type) }
fun EffectInfo.text(): String = if (isEnglish()) descriptionEn.ifBlank { description } else description
fun FxChainInfo.label(): String = if (isEnglish()) nameEn.ifBlank { name } else name
fun FxChainInfo.text(): String = if (isEnglish()) descriptionEn.ifBlank { description } else description

/** A dónde va la señal de un canal, en palabras (lo que de verdad hace el motor). */
fun routingLabel(state: ProjectState, t: TrackState): String {
    if (t.isBus) {
        val src = state.tracks.firstOrNull { d -> d.laneRoutes.values.contains(t.id) }
        if (src != null) {
            val lane = src.laneRoutes.entries.first { it.value == t.id }.key
            return tr("Pieza ${lane + 1} de «${src.name}» → Master", "Piece ${lane + 1} of “${src.name}” → Master")
        }
        val feeders = state.tracks.count { (it.sends[t.id] ?: 0f) > 0f }
        return tr("Bus · $feeders envío(s) → Master", "Bus · $feeders send(s) → Master")
    }
    val sends = t.sends.filter { it.value > 0f }.mapNotNull { (id, v) -> state.tracks.firstOrNull { it.id == id }?.let { "${it.name} ${(v * 100).toInt()}%" } }
    val own = if (t.isDrums && t.laneRoutes.isNotEmpty()) tr(" · ${t.laneRoutes.size} pieza(s) en canal propio", " · ${t.laneRoutes.size} piece(s) on own channel") else ""
    return "→ Master" + (if (sends.isNotEmpty()) tr(" · envío a ", " · send to ") + sends.joinToString() else "") + own
}

/**
 * Rack de inserciones del canal seleccionado: «INSERT — nombre», ranuras numeradas en el orden en que procesan,
 * activar/desactivar, subir/bajar, quitar, abrir controles y «+ Agregar efecto». Cadenas completas con deshacer.
 */
@Composable
fun InsertRack(state: ProjectState, track: TrackState, catalog: Catalog, actions: StudioActions, modifier: Modifier = Modifier) {
    var open by rememberSaveable(track.id) { mutableStateOf<String?>(null) }
    var picker by remember { mutableStateOf(false) }
    var chains by remember { mutableStateOf(false) }
    // 0.9.2: al agregar un plugin se abren sus controles en seguida (antes había que buscar la ranura y tocarla).
    var knownInserts by remember(track.id) { mutableStateOf(track.inserts.map { it.id }.toSet()) }
    androidx.compose.runtime.LaunchedEffect(track.id, track.inserts.map { it.id }) {
        val added = track.inserts.firstOrNull { it.id !in knownInserts }
        if (added != null) open = added.id
        knownInserts = track.inserts.map { it.id }.toSet()
    }
    val color = Color(track.color.toInt())
    Column(modifier.verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp).testTag("insert_rack"), verticalArrangement = Arrangement.spacedBy(6.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Box(Modifier.size(10.dp).clip(CircleShape).background(color))
            Spacer(Modifier.width(8.dp))
            Column(Modifier.weight(1f)) {
                Text(tr("EFECTOS — ${track.name.uppercase()}", "EFFECTS — ${track.name.uppercase()}"), fontWeight = FontWeight.Bold, fontSize = 14.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text(routingLabel(state, track), color = Palette.TextFaint, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.testTag("routing_label"))
            }
            if (catalog.fxChains.isNotEmpty())
                CauceButton(tr("Cadenas", "Chains"), { chains = true }, kind = ButtonKind.Ghost, icon = Icons.Default.LibraryMusic, height = 40.dp, modifier = Modifier.testTag("fx_chains"))
        }
        // Envíos post-fader a los buses compartidos (antes era una perilla en la tira).
        if (!track.isBus) state.tracks.filter { it.isBus && !isLaneChannel(state, it) }.forEach { b -> SendRow(track, b, actions) }
        if (track.inserts.isEmpty())
            Text(tr("Sin efectos todavía. Pulsa «+ Agregar plugin»: se procesan en orden, de la ranura 1 hacia abajo, y se oyen al momento.",
                "No effects yet. Tap “+ Add plugin”: they run in order, from slot 1 down, and you hear them right away."), color = Palette.TextDim, fontSize = 12.sp)
        track.inserts.forEachIndexed { i, ins ->
            val info = catalog.effect(ins.type)
            val expanded = open == ins.id
            Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.RadiusMedium)).background(if (expanded) Palette.SurfaceHigh else Palette.Surface)
                .border(1.dp, if (expanded) color.copy(alpha = 0.7f) else Palette.Grid, RoundedCornerShape(Dims.RadiusMedium))) {
                Row(Modifier.fillMaxWidth().heightIn(min = 52.dp).clickable { open = if (expanded) null else ins.id }.padding(start = 10.dp, end = 2.dp)
                    .testTag("slot_$i").semantics { contentDescription = "Slot ${i + 1} ${effectName(ins.type)}" },
                    verticalAlignment = Alignment.CenterVertically) {
                    Text("${i + 1}", color = Palette.TextFaint, fontSize = 12.sp, modifier = Modifier.width(18.dp))
                    Column(Modifier.weight(1f)) {
                        Text(info?.label() ?: effectName(ins.type), fontWeight = FontWeight.SemiBold, fontSize = 14.sp, maxLines = 1,
                            color = if (ins.bypass) Palette.TextFaint else Palette.Text)
                        Text(if (ins.bypass) tr("Bypass: no procesa", "Bypassed") else summary(ins.type, ins.params, catalog), color = Palette.TextFaint, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                    PillToggle(if (ins.bypass) "Off" else "On", !ins.bypass, { actions.setInsertBypass(ins.id, !ins.bypass) }, color = color, height = 34.dp, minWidth = 48.dp,
                        modifier = Modifier.testTag("slot_${i}_bypass"))
                    ToolButton(Icons.Default.KeyboardArrowUp, tr("Subir", "Move up"), { actions.moveInsert(ins.id, i - 1) }, enabled = i > 0, size = 40.dp)
                    ToolButton(Icons.Default.KeyboardArrowDown, tr("Bajar", "Move down"), { actions.moveInsert(ins.id, i + 1) }, enabled = i < track.inserts.size - 1, size = 40.dp)
                    ToolButton(Icons.Default.Delete, tr("Quitar", "Remove"), { if (open == ins.id) open = null; actions.removeInsert(ins.id) }, tint = Palette.Record, size = 40.dp)
                    Icon(if (expanded) Icons.Default.ExpandLess else Icons.Default.ExpandMore, null, tint = Palette.TextDim)
                }
                if (expanded) Box(Modifier.padding(start = 6.dp, end = 6.dp, bottom = 8.dp)) { InsertControls(ins, catalog, actions, color) }
            }
        }
        if (track.inserts.size < 8)
            Row(Modifier.fillMaxWidth().heightIn(min = 52.dp).clip(RoundedCornerShape(Dims.RadiusMedium)).border(1.dp, Palette.GridStrong, RoundedCornerShape(Dims.RadiusMedium))
                .clickable { picker = true }.padding(horizontal = 12.dp).testTag("add_effect"), verticalAlignment = Alignment.CenterVertically) {
                Icon(Icons.Default.Add, null, tint = Palette.VioletSoft)
                Spacer(Modifier.width(8.dp))
                Text(tr("AGREGAR PLUGIN", "ADD PLUGIN"), fontWeight = FontWeight.Bold, color = Palette.VioletSoft, fontSize = 13.sp, modifier = Modifier.weight(1f))
                Text(tr("${track.inserts.size} de 8", "${track.inserts.size} of 8"), color = Palette.TextFaint, fontSize = 11.sp)
            }
        else Text(tr("8 de 8 ranuras ocupadas.", "All 8 slots in use."), color = Palette.TextFaint, fontSize = 11.sp)
        // 0.9.2: VocalShift a la vista en las pistas de audio, separado de las ranuras porque NO es una inserción en vivo.
        if (track.isAudio && catalog.hasPlugin("vocalshift")) ClipProcessorRow(track) { clipId ->
            actions.selectTrack(track.id); actions.selectClip(clipId); actions.openPanel(Panel.VOCAL)
        }
    }
    if (picker) EffectPicker(track, catalog, onDismiss = { picker = false }, onPick = { type -> picker = false; actions.addInsert(track.id, type) },
        onVocal = { clipId -> picker = false; actions.selectTrack(track.id); actions.selectClip(clipId); actions.openPanel(Panel.VOCAL) },
        onChains = if (catalog.fxChains.isNotEmpty()) ({ picker = false; chains = true }) else null,
        onTool = { panel -> picker = false; actions.openPanel(panel) })
    if (chains) ChainDialog(track, catalog, onDismiss = { chains = false }) { id, replace -> chains = false; actions.applyFxChain(track.id, id, replace) }
}

private fun summary(type: String, params: Map<String, Float>, catalog: Catalog): String {
    val info = catalog.effect(type) ?: return ""
    return info.params.take(3).joinToString(" · ") { p -> "${p.name.substringBefore(" (")} ${formatParam(p.id, params[p.id] ?: p.default, p)}" }
}

/**
 * VocalShift en el rack de una pista de audio (0.9.2). Es un «procesador de clip»: trabaja sobre la grabación fuera del
 * hilo de audio (latencia propia de 150 ms) y deja una versión B que se compara con la original. Se dice tal cual.
 */
@Composable
private fun ClipProcessorRow(track: TrackState, onOpen: (String) -> Unit) {
    val clip = track.clips.lastOrNull()
    Column(Modifier.fillMaxWidth().padding(top = 6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        Text(tr("PROCESAR LA GRABACIÓN (NO EN VIVO)", "PROCESS THE RECORDING (NOT LIVE)"), color = Palette.TextFaint, fontSize = 11.sp, fontWeight = FontWeight.Bold)
        PickerRow(
            "VocalShift",
            if (clip == null) tr("Graba o importa una voz en esta pista para procesarla.", "Record or import a voice on this track to process it.")
            else if (clip.process != null) tr("«${clip.name}» ya tiene versión VocalShift. Abrir para ajustar o comparar A/B.", "“${clip.name}” already has a VocalShift version. Open to adjust or compare A/B.")
            else tr("Afinación, segunda voz y carácter sobre «${clip.name}». Crea una versión B; el original no se toca.",
                "Tuning, second voice and character on “${clip.name}”. Creates a B version; the original is kept."),
            clip != null, "rack_vocalshift", icon = true,
        ) { clip?.let { onOpen(it.id) } }
    }
}

/**
 * Explorador de plugins (0.9.2; antes «Agregar efecto»). Tres familias que no se mezclan:
 *  - Efectos de inserción: suenan en vivo en la ranura y se ajustan escuchando. Solo los que publica el motor.
 *  - Procesamiento vocal (pistas de audio): VocalShift trabaja sobre la grabación, no en vivo; se dice así.
 *  - Herramientas generativas: crean notas nuevas (Solo AI); no son efectos.
 * Más cadenas completas (presets de varias ranuras). No se anuncia nada que no esté integrado.
 */
@Composable
fun EffectPicker(
    track: TrackState, catalog: Catalog, onDismiss: () -> Unit, onPick: (String) -> Unit, onVocal: (String) -> Unit,
    onChains: (() -> Unit)? = null, onTool: ((Panel) -> Unit)? = null,
) {
    var filter by remember { mutableStateOf("all") }
    val known = FxCategories.map { it.first }.toSet()
    val sections = FxCategories.filter { (cat, _) -> catalog.effects.any { it.category == cat } } +
        (if (catalog.effects.any { it.category !in known }) listOf("other" to tr("Otros", "Other")) else emptyList())
    val vocalOk = catalog.hasPlugin("vocalshift") && track.isAudio
    val soloOk = catalog.hasPlugin("solo.ai") && onTool != null && !track.isBus
    val chips = buildList {
        add("all" to tr("Todo", "All"))
        sections.forEach { add(it) }
        if (vocalOk) add("vocal_proc" to tr("Voz", "Voice"))
        if (onChains != null) add("chains" to tr("Cadenas", "Chains"))
        if (soloOk) add("gen" to tr("Generativas", "Generative"))
    }
    CauceDialog(tr("Agregar plugin a «${track.name}»", "Add plugin to “${track.name}”"), onDismiss, confirm = null, dismiss = tr("Cancelar", "Cancel")) {
        androidx.compose.foundation.layout.FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp),
            modifier = Modifier.padding(bottom = 6.dp).testTag("plugin_filters")) {
            chips.forEach { (id, label) -> PillToggle(label, filter == id, { filter = id }, height = 32.dp, modifier = Modifier.testTag("plugin_filter_$id")) }
        }
        Column(Modifier.heightIn(max = 460.dp).verticalScroll(rememberScrollState()).testTag("effect_picker"), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            val showFx = filter == "all" || sections.any { it.first == filter }
            if (showFx) SectionTitle(tr("EFECTOS EN VIVO · se oyen al momento", "LIVE EFFECTS · heard right away"))
            sections.forEach { (cat, catName) ->
                if (filter != "all" && filter != cat) return@forEach
                val items = catalog.effects.filter { if (cat == "other") it.category !in known else it.category == cat }
                Text(catName, color = Palette.TextDim, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, modifier = Modifier.padding(top = 4.dp).testTag("plugin_cat_$cat"))
                items.forEach { e ->
                    // Sin compensación de latencia en buses: un efecto con latencia no se ofrece ahí.
                    val blocked = track.isBus && e.latencySamples > 0
                    PickerRow(e.label(), if (blocked) tr("Tiene latencia: no disponible en un bus.", "Has latency: not available on a bus.") else e.text(), !blocked, "pick_${e.type}") { onPick(e.type) }
                }
            }
            if (vocalOk && (filter == "all" || filter == "vocal_proc")) {
                SectionTitle(tr("PROCESAMIENTO VOCAL · sobre la grabación, no en vivo", "VOCAL PROCESSING · on the recording, not live"))
                val clip = track.clips.lastOrNull()
                PickerRow(tr("VocalShift · Procesar voz", "VocalShift · Process voice"),
                    if (clip == null) tr("Graba o importa una voz en esta pista primero.", "Record or import a voice on this track first.")
                    else tr("No es un efecto en vivo: procesa la grabación (offline) y crea una versión alternativa. El original no se toca.",
                        "Not a live effect: it processes the recording (offline) and creates an alternative version. The original is kept."),
                    clip != null, "pick_vocalshift", icon = true) { clip?.let { onVocal(it.id) } }
            }
            if (onChains != null && (filter == "all" || filter == "chains")) {
                SectionTitle(tr("CADENAS COMPLETAS · varias ranuras de una vez", "FULL CHAINS · several slots at once"))
                PickerRow(tr("Cadenas de efectos (${catalog.fxChains.size})", "Effect chains (${catalog.fxChains.size})"),
                    catalog.fxChains.take(4).joinToString(" · ") { it.label() } + if (catalog.fxChains.size > 4) "…" else "", true, "pick_chains") { onChains() }
            }
            if (soloOk && (filter == "all" || filter == "gen")) {
                SectionTitle(tr("HERRAMIENTAS GENERATIVAS · crean notas, no son efectos", "GENERATIVE TOOLS · create notes, not effects"))
                PickerRow("Solo AI", tr("Cuatro melodías o solos sobre la canción, en una pista MIDI nueva y editable.",
                    "Four melodies or solos over the song, on a new editable MIDI track."), true, "pick_solo_ai") { onTool?.invoke(Panel.SOLO) }
            }
        }
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(text, color = Palette.TextFaint, fontSize = 11.sp, fontWeight = FontWeight.Bold, modifier = Modifier.padding(top = 8.dp))
}

@Composable
private fun PickerRow(title: String, text: String, enabled: Boolean, tag: String, icon: Boolean = false, onClick: () -> Unit) {
    Row(Modifier.fillMaxWidth().heightIn(min = 52.dp).clip(RoundedCornerShape(Dims.RadiusMedium)).background(Palette.Surface)
        .clickable(enabled = enabled, onClick = onClick).padding(horizontal = 10.dp, vertical = 8.dp).testTag(tag), verticalAlignment = Alignment.CenterVertically) {
        if (icon) { Icon(Icons.Default.RecordVoiceOver, null, tint = Palette.Cyan); Spacer(Modifier.width(8.dp)) }
        Column(Modifier.weight(1f)) {
            Text(title, fontWeight = FontWeight.SemiBold, fontSize = 14.sp, color = if (enabled) Palette.Text else Palette.TextFaint)
            Text(text, color = Palette.TextDim, fontSize = 12.sp)
        }
        if (enabled && !icon) Icon(Icons.Default.Add, null, tint = Palette.VioletSoft)
    }
}

/** Cadenas completas: se añaden al final o reemplazan la cadena actual (un solo deshacer la recupera). */
@Composable
fun ChainDialog(track: TrackState, catalog: Catalog, onDismiss: () -> Unit, onApply: (String, Boolean) -> Unit) {
    var chosen by remember { mutableStateOf<String?>(null) }
    val c = catalog.fxChains.firstOrNull { it.id == chosen }
    val fits = c != null && track.inserts.size + c.steps.size <= 8
    CauceDialog(tr("Cadenas de efectos", "Effect chains"), onDismiss, confirm = null, dismiss = tr("Cerrar", "Close")) {
        // La lista se desplaza; los botones quedan siempre a la vista debajo (no hay que buscarlos al final).
        Column(Modifier.heightIn(max = 380.dp).verticalScroll(rememberScrollState()).testTag("chain_dialog"), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(tr("Solo usan efectos de CAUCESOUND. Guardan el orden y los parámetros.", "They only use CAUCESOUND effects. Order and parameters are kept."), fontSize = 12.sp, color = Palette.TextDim)
            catalog.fxChains.forEach { ch ->
                val on = ch.id == chosen
                Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.RadiusMedium)).background(if (on) Palette.Violet.copy(alpha = 0.16f) else Palette.Surface)
                    .border(1.dp, if (on) Palette.Violet else Color.Transparent, RoundedCornerShape(Dims.RadiusMedium))
                    .clickable { chosen = ch.id }.padding(10.dp).testTag("chain_${ch.id}")) {
                    Text(ch.label(), fontWeight = FontWeight.SemiBold)
                    Text(ch.text(), fontSize = 12.sp, color = Palette.TextDim)
                    Text(ch.steps.mapIndexed { i, s -> "${i + 1}. ${catalog.effect(s)?.label() ?: effectName(s)}" }.joinToString("  "), fontSize = 11.sp, color = Palette.TextFaint)
                }
            }
        }
        Column(Modifier.padding(top = 8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            if (c == null) Text(tr("Elige una cadena.", "Pick a chain."), fontSize = 11.sp, color = Palette.TextFaint)
            if (c != null) Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                CauceButton(tr("Añadir al final", "Append"), { onApply(c.id, false) }, Modifier.weight(1f).testTag("chain_append"), enabled = fits)
                CauceButton(tr("Reemplazar", "Replace"), { onApply(c.id, true) }, Modifier.weight(1f).testTag("chain_replace"), kind = ButtonKind.Primary)
            }
            if (c != null && !fits) Text(tr("No caben al final (máximo 8 ranuras): usa «Reemplazar».", "They don't fit at the end (8 slots max): use Replace."), fontSize = 11.sp, color = Palette.Warn)
            if (c != null) Text(tr("«Reemplazar» sustituye la cadena actual. Deshacer la recupera entera.", "Replace swaps the current chain. Undo brings it back."), fontSize = 11.sp, color = Palette.TextFaint)
        }
    }
}

@Composable
private fun SendRow(track: TrackState, bus: TrackState, actions: StudioActions) {
    val level = track.sends[bus.id] ?: 0f
    var v by remember(track.id, bus.id, level) { mutableStateOf(level) }
    Row(Modifier.fillMaxWidth().testTag("send_${bus.id}"), verticalAlignment = Alignment.CenterVertically) {
        Text(tr("Envío a «${bus.name}»", "Send to “${bus.name}”"), fontSize = 12.sp, color = Palette.TextDim, modifier = Modifier.width(120.dp), maxLines = 1, overflow = TextOverflow.Ellipsis)
        androidx.compose.material3.Slider(v, { v = it; actions.setSendLive(track.id, bus.id, it) }, Modifier.weight(1f).testTag("send_slider_${bus.id}"), onValueChangeFinished = actions::commitMixer,
            colors = androidx.compose.material3.SliderDefaults.colors(thumbColor = Palette.Text, activeTrackColor = Palette.Violet, inactiveTrackColor = Palette.SurfaceTop))
        Text("${(v * 100).toInt()}%", fontSize = 12.sp, color = Palette.TextDim, modifier = Modifier.width(40.dp))
    }
}
