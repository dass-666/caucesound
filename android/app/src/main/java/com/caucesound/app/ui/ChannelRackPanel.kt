package com.caucesound.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
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
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.GridOn
import androidx.compose.material.icons.filled.LibraryMusic
import androidx.compose.material.icons.filled.Piano
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LongState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
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
import com.caucesound.app.data.Catalog
import com.caucesound.app.data.DRUM_BASE_NOTE
import com.caucesound.app.data.MusicTime
import com.caucesound.app.data.PPQ
import com.caucesound.app.data.PatternState
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.TrackState

/**
 * Channel Rack (misión 0.7): una fila por instrumento real del proyecto. No es un segundo secuenciador: los pasos
 * son los del patrón de Cauce Drums (los mismos comandos que la rejilla de batería) y las filas melódicas abren el
 * Piano Roll existente. Cada fila dice a qué canal del Mixer va, y una pieza de batería puede tener canal propio
 * (routeDrumLane del motor: un bus real con fader, mute/solo, medidor e inserciones).
 */
@Composable
fun ChannelRackPanel(state: ProjectState, catalog: Catalog, position: LongState, playing: Boolean, actions: StudioActions) {
    var addMenu by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 10.dp, vertical = 8.dp).testTag("channel_rack")) {
        PanelHeader("Channel Rack", tr("Cada fila es un instrumento real y va a un canal del Mixer", "Each row is a real instrument routed to a Mixer channel")) {
            Box {
                CauceButton(tr("Instrumento", "Instrument"), { addMenu = true }, icon = Icons.Default.Add, height = 40.dp, modifier = Modifier.testTag("rack_add"))
                DropdownMenu(addMenu, { addMenu = false }) {
                    DropdownMenuItem({ Text("Cauce Drums") }, leadingIcon = { Icon(Icons.Default.GridOn, null) },
                        onClick = { addMenu = false; actions.addTrack(tr("Batería", "Drums"), "drums") })
                    DropdownMenuItem({ Text("Cauce Synth") }, leadingIcon = { Icon(Icons.Default.Piano, null) },
                        onClick = { addMenu = false; actions.addTrack("Synth", "synth") })
                }
            }
        }
        Spacer(Modifier.height(6.dp))
        val instruments = state.tracks.filter { it.isDrums || it.isSynth }
        if (instruments.isEmpty()) {
            EmptyState(Icons.Default.Piano, tr("Sin instrumentos", "No instruments"),
                tr("Añade una batería o un sintetizador para empezar un patrón.", "Add drums or a synth to start a pattern."))
        }
        state.tracks.forEach { t ->
            when {
                t.isDrums -> DrumRackGroup(state, t, catalog, position, playing, actions)
                t.isSynth -> SynthRackRow(state, t, catalog, position, actions)
                t.isAudio -> AudioRackRow(state, t, actions)
                else -> {} // Los buses son canales del Mixer, no instrumentos; los de pieza aparecen bajo su batería.
            }
            Spacer(Modifier.height(8.dp))
        }
        Text(
            tr("Los cambios suenan al momento, también con la reproducción en marcha. Todo se puede deshacer.",
                "Changes are heard immediately, also while playing. Everything can be undone."),
            fontSize = 11.sp, color = Palette.TextFaint, modifier = Modifier.padding(vertical = 8.dp),
        )
    }
}

/** Nombre del canal del Mixer que recibe la señal de esta pista (o de esta pieza de batería). */
internal fun mixerChannelOf(state: ProjectState, t: TrackState, lane: Int? = null): String {
    val busId = lane?.let { t.laneRoutes[it] }
    val ch = busId?.let { id -> state.tracks.firstOrNull { it.id == id } } ?: t
    val n = state.trackIndex(ch.id) + 1
    return "Mixer $n · ${ch.name}"
}

private fun openMixer(actions: StudioActions, trackId: String) { actions.selectTrack(trackId); actions.openPanel(Panel.MIXER) }

@Composable
private fun RackCard(t: TrackState, content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(14.dp)).background(Palette.Surface).border(1.dp, Palette.Grid, RoundedCornerShape(14.dp)).padding(8.dp)) {
        content()
    }
}

@Composable
private fun RackTitle(t: TrackState, kind: String, channel: String, onMixer: () -> Unit, trailing: @Composable () -> Unit = {}) {
    Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.width(5.dp).height(34.dp).clip(RoundedCornerShape(3.dp)).background(Color(t.color)))
        Column(Modifier.weight(1f).padding(horizontal = 8.dp)) {
            Text(t.name, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text("$kind  →  $channel", fontSize = 11.sp, color = Palette.TextDim, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        trailing()
        ToolButton(Icons.Default.GraphicEq, tr("Abrir en el Mixer", "Open in Mixer"), onMixer, tint = Palette.Text)
    }
}

// --------------------------------------------------------------------------------------------------------------------
// Batería: una fila por pieza, con los 16 (o 32) pasos del patrón y su canal del Mixer.

private val VELOCITY_CYCLE = listOf(127, 100, 70, 40)

@OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)
@Composable
private fun DrumRackGroup(state: ProjectState, track: TrackState, catalog: Catalog, position: LongState, playing: Boolean, actions: StudioActions) {
    val slot = state.trackIndex(track.id)
    val kitIndex = (track.params["kit"] ?: 0f).toInt().coerceIn(0, (catalog.drumKits.size - 1).coerceAtLeast(0))
    val synthKit = catalog.drumKits.getOrNull(kitIndex)
    // 0.9: con un kit de muestras cargado manda el paquete (nombre y carriles); si falta, se dice en el nombre.
    val sampleKit = track.soundKey.takeIf { it.isNotEmpty() }?.let { com.caucesound.app.data.LocalSounds.current.instrument(it) }
    val kit = when {
        sampleKit != null -> com.caucesound.app.data.KitInfo(sampleKit.key, sampleKit.name, sampleKit.lanes)
        track.soundKey.isNotEmpty() -> com.caucesound.app.data.KitInfo(track.soundKey, tr("Kit no instalado", "Kit not installed"), synthKit?.lanes ?: emptyList())
        else -> synthKit
    }
    val laneNames = kit?.lanes?.takeIf { it.size >= 8 } ?: listOf("Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Perc 1", "Perc 2", "Perc 3")
    var patternId by remember(track.id) { mutableStateOf(track.patterns.firstOrNull()?.id ?: "") }
    val pattern = track.pattern(patternId) ?: track.patterns.firstOrNull()
    var velocityMode by remember { mutableStateOf(false) }
    var page by remember(track.id, patternId) { mutableIntStateOf(0) }
    var expanded by remember(track.id) { mutableStateOf<Int?>(null) }

    RackCard(track) {
        RackTitle(track, "Cauce Drums · ${kit?.name ?: "Kit"}", mixerChannelOf(state, track), { openMixer(actions, track.id) }) {
            ToolButton(Icons.Default.LibraryMusic, tr("Kits de muestras Cauce", "Cauce sample kits"), { actions.selectTrack(track.id); actions.openPanel(Panel.SOUNDS) }, tint = Palette.Cyan,
                modifier = Modifier.testTag("rack_sounds_${track.id}"))
            ToolButton(Icons.Default.GridOn, tr("Rejilla completa de batería", "Full drum grid"), { actions.selectTrack(track.id); actions.openPanel(Panel.DRUMS) }, tint = Palette.Text)
        }
        if (pattern == null) {
            CauceButton(tr("Crear patrón", "Create pattern"), { actions.addPattern(track.id, null) }, icon = Icons.Default.Add, height = 40.dp)
            return@RackCard
        }
        Row(Modifier.fillMaxWidth().padding(top = 4.dp).horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            track.patterns.forEach { p -> PillToggle(p.name.ifEmpty { "?" }, p.id == pattern.id, { patternId = p.id }, minWidth = 40.dp, height = 36.dp) }
            ToolButton(Icons.Default.Add, tr("Patrón nuevo", "New pattern"), { actions.addPattern(track.id, null) })
        }
        // Mute/solo de la pista (los mismos comandos que el Mixer), longitud, intensidad y swing: siempre a la vista.
        // FlowRow: una sola fila en horizontal (deja sitio a los pasos) y varias en un teléfono estrecho.
        FlowRow(Modifier.fillMaxWidth().padding(top = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            PillToggle(tr("Silenciar", "Mute"), track.mute, { actions.setMute(track.id, !track.mute) }, color = Palette.Mute, height = 36.dp, modifier = Modifier.testTag("rack_mute_${track.id}"))
            PillToggle("Solo", track.solo, { actions.setSolo(track.id, !track.solo) }, color = Palette.Solo, height = 36.dp)
            Segmented(listOf("16", "32"), if (pattern.steps == 32) 1 else 0, { actions.setPatternLength(track.id, pattern.id, if (it == 1) 32 else 16) }, height = 36.dp)
            PillToggle(tr("Intensidad", "Velocity"), velocityMode, { velocityMode = !velocityMode }, height = 36.dp, modifier = Modifier.testTag("rack_velocity"))
            Row(Modifier.width(260.dp).height(36.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Swing", color = Palette.TextDim, fontSize = 12.sp, modifier = Modifier.padding(start = 4.dp, end = 8.dp))
                Box(Modifier.weight(1f)) { SwingSlider(pattern.swing) { actions.setSwing(track.id, pattern.id, it) } }
                Text("${(pattern.swing * 100).toInt()}%", style = ValueText, color = Palette.Text, modifier = Modifier.width(36.dp))
            }
        }
        if (velocityMode) Text(tr("Toca un paso encendido para cambiar su fuerza: 127 → 100 → 70 → 40.", "Tap a lit step to change how hard it hits: 127 → 100 → 70 → 40."),
            fontSize = 11.sp, color = Palette.TextFaint)
        Spacer(Modifier.height(4.dp))
        StepRows(state, track, pattern, laneNames, position, velocityMode, page, { page = it }, slot, actions, expanded) { expanded = if (expanded == it) null else it }
        Spacer(Modifier.height(6.dp))
        CauceButton(tr("Añadir «${pattern.name}» al final del arreglo", "Append “${pattern.name}” to the arrangement"), {
            val last = track.midiClips.maxOfOrNull { it.end } ?: 0L
            actions.addMidiClip(track.id, last, pattern.steps.toLong() / 16 * 4 * PPQ * state.beatsPerBar / 4, pattern.id)
        }, kind = ButtonKind.Ghost, icon = Icons.Default.Add, height = 40.dp)
    }
}

private val RACK_NAME_W = 92.dp
private val RACK_CELL_H = 40.dp

@Composable
private fun StepRows(
    state: ProjectState, track: TrackState, pattern: PatternState, laneNames: List<String>, position: LongState, velocityMode: Boolean,
    page: Int, onPage: (Int) -> Unit, slot: Int, actions: StudioActions, expanded: Int?, onExpand: (Int) -> Unit,
) {
    BoxWithConstraints(Modifier.fillMaxWidth()) {
        val gridW = maxWidth - RACK_NAME_W
        // Celdas de al menos 28 dp para el dedo: en un teléfono en vertical se ven 8 pasos por página.
        val visible = when { gridW / pattern.steps >= 28.dp -> pattern.steps; gridW / 16 >= 28.dp -> 16; else -> 8 }
        val pages = (pattern.steps / visible).coerceAtLeast(1)
        val pg = page.coerceIn(0, pages - 1)
        val first = pg * visible
        Column {
            if (pages > 1) Row(Modifier.padding(start = RACK_NAME_W, bottom = 4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                for (p in 0 until pages) PillToggle("${p * visible + 1}–${(p + 1) * visible}", p == pg, { onPage(p) }, minWidth = 56.dp, height = 30.dp,
                    modifier = Modifier.testTag("rack_page_$p"))
            }
            // Cursor del paso que suena: se lee la posición al dibujar, así la rejilla no se recompone en cada fotograma.
            Canvas(Modifier.padding(start = RACK_NAME_W).width(gridW).height(4.dp)) {
                val pos = position.longValue
                val tk = state.framesToTicks(pos)
                val clip = track.midiClips.firstOrNull { tk >= it.start && tk < it.end }
                if (clip != null && clip.patternId == pattern.id) {
                    val stp = MusicTime.activeStep(pos, state.bpm, clip.start, clip.length, pattern.steps)
                    if (stp in first until first + visible) {
                        val cw = size.width / visible
                        drawRoundRect(Palette.Cyan, Offset((stp - first) * cw + 2f, 0f), Size(cw - 4f, size.height), CornerRadius(2f))
                    }
                }
            }
            for (l in 0 until 8) {
                val muted = (track.params["mute$l"] ?: 0f) >= 0.5f
                val solo = (track.params["solo$l"] ?: 0f) >= 0.5f
                val own = track.laneRoutes.containsKey(l)
                Row(Modifier.height(RACK_CELL_H).padding(bottom = 3.dp), verticalAlignment = Alignment.CenterVertically) {
                    Row(Modifier.width(RACK_NAME_W).clip(RoundedCornerShape(6.dp)).clickable { onExpand(l) }.padding(end = 4.dp).testTag("rack_lane_$l"),
                        verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.width(4.dp).height(RACK_CELL_H - 10.dp).background(LANE_COLORS[l], RoundedCornerShape(2.dp)))
                        Column(Modifier.padding(start = 5.dp)) {
                            Text(laneNames.getOrElse(l) { "?" }, fontSize = 11.sp, lineHeight = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                                color = if (muted) Palette.TextFaint else if (solo) Palette.Solo else Palette.Text)
                            if (own) Text(tr("canal propio", "own channel"), fontSize = 9.sp, lineHeight = 10.sp, color = Palette.VioletSoft, maxLines = 1)
                        }
                    }
                    Row(Modifier.width(gridW)) {
                        for (i in 0 until visible) {
                            val s = first + i
                            val v = pattern.lanes.getOrNull(l)?.getOrElse(s) { 0 } ?: 0
                            val beat = (s / 4) % 2 == 0
                            Box(
                                Modifier.weight(1f).fillMaxSize().padding(horizontal = 1.5.dp).clip(RoundedCornerShape(5.dp))
                                    .background(if (v > 0) LANE_COLORS[l].copy(alpha = 0.35f + 0.65f * v / 127f) else if (beat) Palette.SurfaceHigh else Palette.SurfaceTop)
                                    .clickable {
                                        val nv = when {
                                            !velocityMode -> if (v > 0) 0 else 100
                                            v <= 0 -> 100
                                            else -> VELOCITY_CYCLE[(VELOCITY_CYCLE.indexOfFirst { it <= v }.let { if (it < 0) 0 else it } + 1) % VELOCITY_CYCLE.size]
                                        }
                                        actions.setStep(track.id, pattern.id, l, s, nv)
                                    }
                                    .testTag("rack_step_${l}_$s").semantics { contentDescription = "${laneNames.getOrElse(l) { "?" }} ${s + 1}: $v" },
                            )
                        }
                    }
                }
                if (expanded == l) LaneDetails(state, track, l, laneNames.getOrElse(l) { "?" }, muted, solo, slot, actions)
            }
        }
    }
}

/** Opciones de una pieza: escucharla, mute/solo propios y, sobre todo, su canal en el Mixer. */
@Composable
private fun LaneDetails(state: ProjectState, track: TrackState, lane: Int, name: String, muted: Boolean, solo: Boolean, slot: Int, actions: StudioActions) {
    val busId = track.laneRoutes[lane]
    Column(Modifier.fillMaxWidth().padding(start = 8.dp, bottom = 6.dp).clip(RoundedCornerShape(10.dp)).background(Palette.SurfaceHigh).padding(8.dp).testTag("rack_lane_details_$lane")) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            PillToggle(tr("Oír", "Hear"), false, { actions.liveNote(slot, DRUM_BASE_NOTE + lane, 110, true) }, icon = Icons.Default.VolumeUp, height = 36.dp)
            PillToggle("M", muted, { actions.setParam(track.id, "mute$lane", if (muted) 0f else 1f) }, color = Palette.Mute, height = 36.dp, minWidth = 40.dp)
            PillToggle("S", solo, { actions.setParam(track.id, "solo$lane", if (solo) 0f else 1f) }, color = Palette.Solo, height = 36.dp, minWidth = 40.dp)
            Spacer(Modifier.weight(1f))
            ToolButton(Icons.Default.Tune, tr("Ajustes de la pieza", "Piece settings"), { actions.selectTrack(track.id); actions.openPanel(Panel.DRUMS) })
        }
        Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(tr("Canal propio en el Mixer", "Own Mixer channel"), fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                Text(
                    if (busId != null) tr("«$name» sale por ${mixerChannelOf(state, track, lane)}: fader, efectos y medidor propios.",
                        "“$name” goes to ${mixerChannelOf(state, track, lane)}: its own fader, effects and meter.")
                    else tr("Ahora suena dentro de ${mixerChannelOf(state, track)}. Actívalo para mezclarla aparte.",
                        "It now plays inside ${mixerChannelOf(state, track)}. Turn this on to mix it separately."),
                    fontSize = 11.sp, color = Palette.TextDim,
                )
            }
            Switch(busId != null, { on -> actions.routeDrumLane(track.id, lane, on) }, Modifier.testTag("rack_own_channel_$lane"),
                colors = SwitchDefaults.colors(checkedTrackColor = Palette.Violet))
        }
        if (busId != null) CauceButton(tr("Abrir su canal en el Mixer", "Open its Mixer channel"), { openMixer(actions, busId) },
            icon = Icons.Default.GraphicEq, height = 40.dp, modifier = Modifier.padding(top = 4.dp).testTag("rack_open_lane_mixer_$lane"))
    }
}

// --------------------------------------------------------------------------------------------------------------------
// Filas melódicas y de audio.

@OptIn(androidx.compose.foundation.layout.ExperimentalLayoutApi::class)
@Composable
private fun SynthRackRow(state: ProjectState, t: TrackState, catalog: Catalog, position: LongState, actions: StudioActions) {
    val preset = catalog.synthPresets.firstOrNull { it.id == t.preset }?.name ?: t.preset.ifEmpty { "Cauce Synth" }
    val notes = t.midiClips.sumOf { it.notes.size }
    RackCard(t) {
        RackTitle(t, "Synth · $preset", mixerChannelOf(state, t), { openMixer(actions, t.id) })
        Text(tr("${t.midiClips.size} clip(s) · $notes nota(s)", "${t.midiClips.size} clip(s) · $notes note(s)"), fontSize = 11.sp, color = Palette.TextFaint, modifier = Modifier.padding(start = 13.dp, top = 2.dp))
        // FlowRow: en pantallas estrechas los botones pasan a otra línea en vez de quedar ocultos fuera de la vista.
        FlowRow(Modifier.fillMaxWidth().padding(top = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            CauceButton("Piano roll", { t.midiClips.firstOrNull()?.let { actions.selectClip(it.id) }; actions.selectTrack(t.id); actions.openPanel(Panel.PIANO) },
                icon = Icons.Default.Piano, height = 40.dp, enabled = t.midiClips.isNotEmpty(), modifier = Modifier.testTag("rack_piano_${t.id}"))
            CauceButton(tr("Sonido", "Sound"), { actions.selectTrack(t.id); actions.openPanel(Panel.SYNTH) }, icon = Icons.Default.Tune, height = 40.dp)
            CauceButton(tr("Crear patrón", "New pattern"), {
                // Clip de 4 compases en el compás donde está el cursor; el estudio lo selecciona y lo abre el Piano Roll.
                val bar = PPQ.toLong() * state.beatsPerBar
                val start = MusicTime.floorTicks(state.framesToTicks(position.longValue), bar)
                actions.addMidiClip(t.id, start, bar * 4, null)
                actions.selectTrack(t.id)
                actions.openPanel(Panel.PIANO)
            }, icon = Icons.Default.Add, height = 40.dp, modifier = Modifier.testTag("rack_new_clip_${t.id}"))
            PillToggle(tr("Silenciar", "Mute"), t.mute, { actions.setMute(t.id, !t.mute) }, color = Palette.Mute, height = 36.dp)
            PillToggle("Solo", t.solo, { actions.setSolo(t.id, !t.solo) }, color = Palette.Solo, height = 36.dp)
        }
    }
}

@Composable
private fun AudioRackRow(state: ProjectState, t: TrackState, actions: StudioActions) {
    RackCard(t) {
        RackTitle(t, "Audio · ${t.clips.size} clip(s)", mixerChannelOf(state, t), { openMixer(actions, t.id) })
    }
}
