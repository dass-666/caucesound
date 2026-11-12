package com.caucesound.app.ui

import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
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
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.ContentPaste
import androidx.compose.material.icons.filled.DeleteSweep
import androidx.compose.material.icons.filled.LibraryAdd
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LongState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
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

val LANE_COLORS = listOf(
    Color(0xFF8B5CF6), Color(0xFF3B82F6), Color(0xFF22D3EE), Color(0xFF34D399),
    Color(0xFFA3E635), Color(0xFFF59E0B), Color(0xFFFB923C), Color(0xFFF472B6),
)

private val CELL_H = 44.dp
private val LANE_HEADER = 96.dp

@Composable
fun DrumsPanel(
    state: ProjectState, track: TrackState, catalog: Catalog, position: LongState, playing: Boolean,
    actions: StudioActions, clipboard: List<List<Int>>?, onCopy: (List<List<Int>>) -> Unit,
) {
    val slot = state.trackIndex(track.id)
    val kitIndex = (track.params["kit"] ?: 0f).toInt().coerceIn(0, (catalog.drumKits.size - 1).coerceAtLeast(0))
    val kit = catalog.drumKits.getOrNull(kitIndex)
    val laneNames = kit?.lanes ?: listOf("Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Perc 1", "Perc 2", "Perc 3")
    var patternId by remember(track.id) { mutableStateOf(track.patterns.firstOrNull()?.id ?: "") }
    val pattern = track.pattern(patternId) ?: track.patterns.firstOrNull()?.also { patternId = it.id }
    var showPads by remember { mutableStateOf(false) }
    var velocityMode by remember { mutableStateOf(false) }
    var page by remember(track.id, patternId) { mutableIntStateOf(0) }
    var laneSettings by remember { mutableStateOf<Int?>(null) }
    var kitMenu by remember { mutableStateOf(false) }

    if (pattern == null) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            TextButton(onClick = { actions.addPattern(track.id, null) }) { Text(tr("Crear patrón", "Create pattern")) }
        }
        return
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp)) {
        // --- Cabecera: kit y vista ---
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("Cauce Drums", fontWeight = FontWeight.Bold, fontSize = 18.sp)
                Box {
                    Text(
                        (kit?.name ?: "Kit") + " ▾", color = Palette.Cyan, fontSize = 13.sp, fontWeight = FontWeight.SemiBold,
                        modifier = Modifier.clip(RoundedCornerShape(8.dp)).clickable { kitMenu = true }.padding(vertical = 6.dp),
                    )
                    DropdownMenu(kitMenu, { kitMenu = false }) {
                        catalog.drumKits.forEachIndexed { i, k ->
                            DropdownMenuItem(text = { Text(k.name) }, onClick = { kitMenu = false; actions.setParam(track.id, "kit", i.toFloat()) })
                        }
                    }
                }
            }
            Segmented(listOf(tr("Pasos", "Steps"), "Pads"), if (showPads) 1 else 0, { showPads = it == 1 })
        }
        Spacer(Modifier.height(8.dp))

        // --- Patrones ---
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            track.patterns.forEach { p ->
                PillToggle(p.name.ifEmpty { "?" }, p.id == pattern.id, { patternId = p.id }, minWidth = 44.dp, height = 40.dp, color = Palette.Violet)
            }
            IconButton(onClick = { actions.addPattern(track.id, null) }) { Icon(Icons.Default.Add, tr("Patrón nuevo", "New pattern"), tint = Palette.TextDim) }
            IconButton(onClick = { actions.addPattern(track.id, pattern.id) }) { Icon(Icons.Default.LibraryAdd, tr("Duplicar patrón", "Duplicate pattern"), tint = Palette.TextDim) }
            IconButton(onClick = { onCopy(pattern.lanes) }) { Icon(Icons.Default.ContentCopy, tr("Copiar", "Copy"), tint = Palette.TextDim) }
            IconButton(onClick = { clipboard?.let { cb -> actions.setPatternLanes(track.id, pattern.id, fitLanes(cb, pattern.steps)) } }, enabled = clipboard != null) {
                Icon(Icons.Default.ContentPaste, tr("Pegar", "Paste"), tint = if (clipboard != null) Palette.TextDim else Palette.TextFaint)
            }
            IconButton(onClick = { actions.setPatternLanes(track.id, pattern.id, List(8) { List(pattern.steps) { 0 } }) }) {
                Icon(Icons.Default.DeleteSweep, tr("Vaciar patrón", "Clear pattern"), tint = Palette.TextDim)
            }
        }
        Spacer(Modifier.height(6.dp))
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            Segmented(listOf("16", "32"), if (pattern.steps == 32) 1 else 0, { actions.setPatternLength(track.id, pattern.id, if (it == 1) 32 else 16) })
            PillToggle("Vel", velocityMode, { velocityMode = !velocityMode }, color = Palette.Cyan, height = 36.dp)
            Spacer(Modifier.weight(1f))
            Text("Swing ${(pattern.swing * 100).toInt()}%", color = Palette.TextDim, fontSize = 13.sp)
        }
        SwingSlider(pattern.swing) { actions.setSwing(track.id, pattern.id, it) }
        Spacer(Modifier.height(8.dp))

        if (!showPads) {
            StepGrid(state, track, pattern, laneNames, position, playing, velocityMode, page, onPage = { page = it }, slot = slot, actions = actions,
                onLaneSettings = { laneSettings = it })
            Spacer(Modifier.height(10.dp))
            TextButton(onClick = {
                val last = track.midiClips.maxOfOrNull { it.end } ?: 0L
                actions.addMidiClip(track.id, last, pattern.steps.toLong() / 16 * 4 * PPQ * state.beatsPerBar / 4, pattern.id)
            }) {
                Icon(Icons.Default.Add, null); Spacer(Modifier.width(6.dp))
                Text(tr("Añadir «${pattern.name}» al final del arreglo", "Append “${pattern.name}” to the arrangement"))
            }
        } else {
            DrumPads(laneNames, slot, actions)
        }
    }

    laneSettings?.let { lane ->
        LaneDialog(track, lane, laneNames.getOrElse(lane) { "?" }, actions, onDismiss = { laneSettings = null })
    }
}

/** Ajusta un portapapeles de 16/32 pasos al tamaño del patrón destino. */
fun fitLanes(src: List<List<Int>>, steps: Int): List<List<Int>> =
    List(8) { l -> List(steps) { s -> src.getOrNull(l)?.let { lane -> if (lane.isEmpty()) 0 else lane[s % lane.size] } ?: 0 } }

@Composable
private fun SwingSlider(value: Float, onCommit: (Float) -> Unit) {
    var v by remember(value) { mutableStateOf(value) }
    androidx.compose.material3.Slider(
        v, { v = it }, valueRange = 0f..1f, onValueChangeFinished = { onCommit(v) },
        colors = androidx.compose.material3.SliderDefaults.colors(thumbColor = Palette.Text, activeTrackColor = Palette.Violet, inactiveTrackColor = Palette.SurfaceTop),
    )
}

@Composable
private fun StepGrid(
    state: ProjectState, track: TrackState, pattern: PatternState, laneNames: List<String>, position: LongState, playing: Boolean,
    velocityMode: Boolean, page: Int, onPage: (Int) -> Unit, slot: Int, actions: StudioActions, onLaneSettings: (Int) -> Unit,
) {
    var lanes by remember(pattern) { mutableStateOf(pattern.lanes) }
    val st by rememberUpdatedState(state)
    BoxWithConstraints(Modifier.fillMaxWidth()) {
        val gridW = maxWidth - LANE_HEADER
        // Celdas de al menos 36 dp: en vertical se ven 8 pasos por página; en horizontal o tableta, 16 o 32.
        val visible = when {
            gridW / pattern.steps >= 30.dp -> pattern.steps
            gridW / 16 >= 30.dp -> 16
            else -> 8
        }
        val pages = pattern.steps / visible
        val first = (page.coerceIn(0, pages - 1)) * visible
        Column {
            if (pages > 1) {
                Segmented((0 until pages).map { "${it * visible + 1}–${(it + 1) * visible}" }, page.coerceIn(0, pages - 1), onPage, Modifier.padding(start = LANE_HEADER, bottom = 6.dp), height = 34.dp)
            }
            Row {
                Column(Modifier.width(LANE_HEADER)) {
                    for (l in 0 until 8) {
                        val muted = (track.params["mute$l"] ?: 0f) >= 0.5f
                        val solo = (track.params["solo$l"] ?: 0f) >= 0.5f
                        Row(Modifier.height(CELL_H).padding(end = 6.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                            Box(Modifier.width(4.dp).height(CELL_H - 12.dp).background(LANE_COLORS[l], RoundedCornerShape(2.dp)))
                            Text(
                                laneNames.getOrElse(l) { "?" }, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                                color = if (muted) Palette.TextFaint else if (solo) Palette.Solo else Palette.Text,
                                modifier = Modifier.weight(1f).clip(RoundedCornerShape(6.dp))
                                    .clickable { actions.liveNote(slot, DRUM_BASE_NOTE + l, 110, true) }.padding(horizontal = 6.dp, vertical = 12.dp),
                            )
                            Icon(Icons.Default.Tune, tr("Ajustes del carril", "Lane settings"), tint = Palette.TextFaint,
                                modifier = Modifier.size(28.dp).clip(RoundedCornerShape(6.dp)).clickable { onLaneSettings(l) }.padding(4.dp))
                        }
                    }
                }
                val density = LocalDensity.current
                val cellPx = with(density) { (gridW / visible).toPx() }
                val cellHPx = with(density) { CELL_H.toPx() }
                Canvas(
                    Modifier.width(gridW).height(CELL_H * 8).pointerInput(pattern.id, visible, first, velocityMode) {
                        awaitEachGesture {
                            val down = awaitFirstDown()
                            fun cell(o: Offset): Pair<Int, Int>? {
                                val l = (o.y / cellHPx).toInt(); val s = (o.x / cellPx).toInt()
                                return if (l in 0..7 && s in 0 until visible) l to (first + s) else null
                            }
                            val c0 = cell(down.position) ?: return@awaitEachGesture
                            val current = lanes
                            fun set(l: Int, s: Int, v: Int) {
                                if (lanes[l][s] == v) return
                                lanes = lanes.mapIndexed { li, lane -> if (li != l) lane else lane.mapIndexed { si, x -> if (si == s) v else x } }
                            }
                            if (velocityMode) {
                                // Arrastre vertical dentro de la celda: arriba = fuerte.
                                fun velAt(y: Float): Int = (127 - ((y - c0.first * cellHPx) / cellHPx * 127)).toInt().coerceIn(1, 127)
                                set(c0.first, c0.second, velAt(down.position.y))
                                do {
                                    val ev = awaitPointerEvent()
                                    val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                                    if (ch.pressed) { set(c0.first, c0.second, velAt(ch.position.y)); ch.consume() }
                                } while (ch.pressed)
                            } else {
                                // Pintar: el primer toque decide encender o apagar y se aplica a las celdas que se recorran.
                                val paint = if (current[c0.first][c0.second] > 0) 0 else 100
                                set(c0.first, c0.second, paint)
                                if (paint > 0 && !playing) actions.liveNote(slot, DRUM_BASE_NOTE + c0.first, paint, true)
                                do {
                                    val ev = awaitPointerEvent()
                                    val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                                    if (ch.pressed) { cell(ch.position)?.let { (l, s) -> set(l, s, paint) }; ch.consume() }
                                } while (ch.pressed)
                            }
                            if (lanes != current) actions.setPatternLanes(track.id, pattern.id, lanes)
                        }
                    },
                ) {
                    val gap = 4f
                    for (l in 0 until 8) for (i in 0 until visible) {
                        val s = first + i
                        val v = lanes[l].getOrElse(s) { 0 }
                        val x = i * cellPx
                        val y = l * cellHPx
                        val beatShade = if ((s / 4) % 2 == 0) Palette.SurfaceHigh else Palette.Surface
                        drawRoundRect(beatShade, Offset(x + gap / 2, y + gap / 2), Size(cellPx - gap, cellHPx - gap), CornerRadius(10f))
                        if (v > 0) {
                            val c = LANE_COLORS[l]
                            val a = 0.35f + 0.65f * v / 127f
                            // La altura del relleno muestra la velocidad.
                            val h = (cellHPx - gap) * (0.35f + 0.65f * v / 127f)
                            drawRoundRect(c.copy(alpha = a), Offset(x + gap / 2, y + gap / 2 + (cellHPx - gap - h)), Size(cellPx - gap, h), CornerRadius(10f))
                        }
                    }
                    // Cursor del secuenciador: se lee la posición en la fase de dibujo (no recompone la cuadrícula).
                    val pos = position.longValue
                    val clip = track.midiClips.firstOrNull { c -> val t = st.framesToTicks(pos); t >= c.start && t < c.end }
                    if (clip != null && clip.patternId == pattern.id) {
                        val stp = MusicTime.activeStep(pos, st.bpm, clip.start, clip.length, pattern.steps)
                        if (stp in first until first + visible) {
                            val x = (stp - first) * cellPx
                            drawRoundRect(Palette.Cyan, Offset(x + 1f, 0f), Size(cellPx - 2f, size.height), CornerRadius(10f), style = Stroke(3f))
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun DrumPads(laneNames: List<String>, slot: Int, actions: StudioActions) {
    // 2 filas x 4 pads; cada pad tiene su propio detector: varios dedos a la vez, un disparo por toque.
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        for (row in listOf(listOf(4, 5, 6, 7), listOf(0, 1, 2, 3))) {
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                for (l in row) Pad(laneNames.getOrElse(l) { "?" }, LANE_COLORS[l], Modifier.weight(1f)) { vel -> actions.liveNote(slot, DRUM_BASE_NOTE + l, vel, true) }
            }
        }
        Text(tr("Toca más arriba en el pad para un golpe más fuerte.", "Tap higher on a pad for a harder hit."), color = Palette.TextFaint, fontSize = 11.sp)
    }
}

@Composable
private fun Pad(name: String, color: Color, modifier: Modifier, onHit: (Int) -> Unit) {
    var pressed by remember { mutableStateOf(false) }
    val glow by animateFloatAsState(if (pressed) 1f else 0f, tween(if (pressed) 30 else 220), label = "pad")
    val hit by rememberUpdatedState(onHit)
    Box(
        modifier.height(112.dp).scale(1f - 0.04f * glow).clip(RoundedCornerShape(18.dp))
            .background(Brush.verticalGradient(listOf(color.copy(alpha = 0.25f + 0.55f * glow), color.copy(alpha = 0.08f + 0.3f * glow))))
            .border(1.5.dp, color.copy(alpha = 0.45f + 0.55f * glow), RoundedCornerShape(18.dp))
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    val vel = (127 - (down.position.y / size.height) * 60).toInt().coerceIn(40, 127)
                    hit(vel)
                    pressed = true
                    down.consume()
                    waitForUpOrCancellation()
                    pressed = false
                }
            },
        contentAlignment = Alignment.BottomStart,
    ) { Text(name, Modifier.padding(10.dp), fontSize = 12.sp, fontWeight = FontWeight.SemiBold, color = Palette.Text, maxLines = 1, overflow = TextOverflow.Ellipsis) }
}

@Composable
private fun LaneDialog(track: TrackState, lane: Int, name: String, actions: StudioActions, onDismiss: () -> Unit) {
    fun p(id: String, d: Float) = track.params["$id$lane"] ?: d
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(name) },
        text = {
            Column {
                Row(horizontalArrangement = Arrangement.SpaceBetween, modifier = Modifier.fillMaxWidth()) {
                    Knob(tr("Volumen", "Volume"), p("vol", 0f), -60f, 6f, 0f, { actions.setParam(track.id, "vol$lane", it) }, onEnd = actions::commitMixer, format = { dbLabel(it) + " dB" })
                    Knob("Pan", p("pan", 0f), -1f, 1f, 0f, { actions.setParam(track.id, "pan$lane", it) }, onEnd = actions::commitMixer, format = { if (kotlin.math.abs(it) < 0.02f) "C" else if (it < 0) "L${(-it * 100).toInt()}" else "R${(it * 100).toInt()}" })
                    Knob(tr("Tono", "Pitch"), p("pitch", 0f), -12f, 12f, 0f, { actions.setParam(track.id, "pitch$lane", it) }, onEnd = actions::commitMixer, format = { "%+.1f st".format(it) })
                }
                Spacer(Modifier.height(8.dp))
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
                    Knob(tr("Duración", "Decay"), p("decay", 1f), 0.1f, 1f, 1f, { actions.setParam(track.id, "decay$lane", it) }, onEnd = actions::commitMixer, format = { "${(it * 100).toInt()}%" })
                    PillToggle("M", p("mute", 0f) >= 0.5f, { actions.setParam(track.id, "mute$lane", if (p("mute", 0f) >= 0.5f) 0f else 1f) }, color = Palette.Mute, height = 44.dp, minWidth = 48.dp)
                    PillToggle("S", p("solo", 0f) >= 0.5f, { actions.setParam(track.id, "solo$lane", if (p("solo", 0f) >= 0.5f) 0f else 1f) }, color = Palette.Solo, height = 44.dp, minWidth = 48.dp)
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text("OK") } },
    )
}
