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
import androidx.compose.material.icons.filled.AutoFixHigh
import androidx.compose.runtime.LaunchedEffect
import kotlinx.coroutines.delay
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import kotlin.math.max
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
    var rollMode by remember { mutableStateOf(false) }
    var page by remember(track.id, patternId) { mutableIntStateOf(0) }
    var laneSettings by remember { mutableStateOf<Int?>(null) }
    var kitMenu by remember { mutableStateOf(false) }
    var variationMenu by remember { mutableStateOf(false) }

    if (pattern == null) {
        Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
            TextButton(onClick = { actions.addPattern(track.id, null) }) { Text(tr("Crear patrón", "Create pattern")) }
        }
        return
    }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp)) {
        // --- Cabecera: kit y vista ---
        PanelHeader("Cauce Drums", track.name) {
            Box {
                PillToggle((kit?.name ?: "Kit") + " ▾", false, { kitMenu = true }, height = 40.dp)
                DropdownMenu(kitMenu, { kitMenu = false }) {
                    catalog.drumKits.forEachIndexed { i, k ->
                        DropdownMenuItem(text = { Text(k.name, fontWeight = if (i == kitIndex) FontWeight.Bold else FontWeight.Normal) },
                            onClick = { kitMenu = false; actions.setParam(track.id, "kit", i.toFloat()) })
                    }
                }
            }
            Spacer(Modifier.width(8.dp))
            Segmented(listOf(tr("Pasos", "Steps"), "Pads"), if (showPads) 1 else 0, { showPads = it == 1 }, height = 40.dp)
        }
        // Vista previa del kit: al cambiarlo (con el transporte parado) suenan bombo y caja del kit nuevo.
        var previewKit by remember(track.id) { mutableIntStateOf(kitIndex) }
        LaunchedEffect(kitIndex) {
            if (previewKit != kitIndex) {
                previewKit = kitIndex
                if (!playing) {
                    delay(120); actions.liveNote(slot, DRUM_BASE_NOTE, 110, true)
                    delay(260); actions.liveNote(slot, DRUM_BASE_NOTE + 1, 105, true)
                }
            }
        }
        Spacer(Modifier.height(8.dp))

        // --- Patrones ---
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            track.patterns.forEach { p ->
                PillToggle(p.name.ifEmpty { "?" }, p.id == pattern.id, { patternId = p.id }, minWidth = 44.dp, height = 40.dp, color = Palette.Violet)
            }
            ToolButton(Icons.Default.Add, tr("Patrón nuevo", "New pattern"), { actions.addPattern(track.id, null) })
            ToolButton(Icons.Default.LibraryAdd, tr("Duplicar patrón", "Duplicate pattern"), { actions.addPattern(track.id, pattern.id) })
            ToolButton(Icons.Default.ContentCopy, tr("Copiar", "Copy"), { onCopy(pattern.lanes) })
            ToolButton(Icons.Default.ContentPaste, tr("Pegar", "Paste"), { clipboard?.let { cb -> actions.setPatternLanes(track.id, pattern.id, fitLanes(cb, pattern.steps)) } }, enabled = clipboard != null)
            Box {
                ToolButton(Icons.Default.AutoFixHigh, tr("Variaciones", "Variations"), { variationMenu = true })
                DropdownMenu(variationMenu, { variationMenu = false }) {
                    DrumVariation.entries.forEach { v ->
                        DropdownMenuItem(text = { Column { Text(v.title()); Text(v.hint(), fontSize = 11.sp, color = Palette.TextDim) } },
                            onClick = { variationMenu = false; actions.setPatternLanes(track.id, pattern.id, v.apply(pattern.lanes)) })
                    }
                }
            }
            // Vaciar se puede deshacer (botón Deshacer de la barra superior), por eso no pide confirmación.
            ToolButton(Icons.Default.DeleteSweep, tr("Vaciar patrón", "Clear pattern"), { actions.setPatternLanes(track.id, pattern.id, List(8) { List(pattern.steps) { 0 } }) })
        }
        Spacer(Modifier.height(8.dp))
        // Modos de edición de la rejilla: pintar (por defecto), fuerza del golpe o redoble. Son excluyentes.
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Segmented(listOf("16", "32"), if (pattern.steps == 32) 1 else 0, { actions.setPatternLength(track.id, pattern.id, if (it == 1) 32 else 16) })
            PillToggle(tr("Intensidad", "Velocity"), velocityMode, { velocityMode = !velocityMode; if (velocityMode) rollMode = false }, color = Palette.Violet, height = 36.dp)
            PillToggle(tr("Redoble", "Roll"), rollMode, { rollMode = !rollMode; if (rollMode) velocityMode = false }, color = Palette.Cyan, height = 36.dp)
        }
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Text("Swing", color = Palette.TextDim, fontSize = 12.sp, modifier = Modifier.padding(start = 4.dp))
            Box(Modifier.weight(1f)) { SwingSlider(pattern.swing) { actions.setSwing(track.id, pattern.id, it) } }
            Text("${(pattern.swing * 100).toInt()}%", style = ValueText, color = Palette.Text, modifier = Modifier.width(32.dp))
        }
        if (velocityMode && !showPads) Text(tr("Arrastra arriba o abajo dentro de un paso para cambiar la fuerza del golpe.", "Drag up or down inside a step to set how hard it hits."),
            color = Palette.TextFaint, fontSize = 11.sp, modifier = Modifier.padding(bottom = 4.dp))
        if (rollMode && !showPads) Text(tr("Toca un paso para repetirlo dentro de su tiempo: 1, 2, 3 o 4 golpes (típico de los charles de trap).",
            "Tap a step to repeat it within its slot: 1, 2, 3 or 4 hits (typical of trap hi-hats)."),
            color = Palette.TextFaint, fontSize = 11.sp, modifier = Modifier.padding(bottom = 4.dp))
        Spacer(Modifier.height(4.dp))

        if (!showPads) {
            StepGrid(state, track, pattern, laneNames, position, playing, velocityMode, page, onPage = { page = it }, slot = slot, actions = actions, rollMode = rollMode,
                onLaneSettings = { laneSettings = it })
            Spacer(Modifier.height(10.dp))
            CauceButton(tr("Añadir «${pattern.name}» al final del arreglo", "Append “${pattern.name}” to the arrangement"), {
                val last = track.midiClips.maxOfOrNull { it.end } ?: 0L
                actions.addMidiClip(track.id, last, pattern.steps.toLong() / 16 * 4 * PPQ * state.beatsPerBar / 4, pattern.id)
            }, kind = ButtonKind.Ghost, icon = Icons.Default.Add)
        } else {
            DrumPads(laneNames, slot, actions)
        }
    }

    laneSettings?.let { lane ->
        LaneDialog(track, lane, laneNames.getOrElse(lane) { "?" }, actions, onDismiss = { laneSettings = null })
    }
}

/**
 * Variaciones de un patrón. Son transformaciones deterministas (el mismo patrón da siempre el mismo resultado), se
 * aplican como una edición normal (se pueden deshacer) y el resultado se edita paso a paso como cualquier patrón.
 * Carriles: 0 bombo, 1 caja, 2 palmas, 3 hat cerrado, 4 hat abierto, 5-7 percusión.
 */
enum class DrumVariation {
    DYNAMICS, FILL, DOUBLE_HATS, HALF_HATS;

    fun title() = when (this) {
        DYNAMICS -> tr("Dinámica natural", "Natural dynamics")
        FILL -> tr("Remate al final", "Fill at the end")
        DOUBLE_HATS -> tr("Hi-hats al doble", "Double-time hats")
        HALF_HATS -> tr("Hi-hats a la mitad", "Half-time hats")
    }
    fun hint() = when (this) {
        DYNAMICS -> tr("Acentúa los tiempos y suaviza el resto", "Accents the beats, softens the rest")
        FILL -> tr("Redoble de caja en el último tiempo", "Snare roll on the last beat")
        DOUBLE_HATS -> tr("Un hat cerrado en cada paso", "A closed hat on every step")
        HALF_HATS -> tr("Deja solo los hats en los tiempos pares", "Keeps hats on even steps only")
    }

    fun apply(lanes: List<List<Int>>): List<List<Int>> {
        val steps = lanes.firstOrNull()?.size ?: return lanes
        fun map(f: (lane: Int, step: Int, v: Int) -> Int) = lanes.mapIndexed { l, lane -> lane.mapIndexed { st, v -> f(l, st, v) } }
        return when (this) {
            // Intensidad según la posición en el compás: tiempo fuerte > contratiempo > semicorcheas. Bombo y caja
            // se quedan casi enteros; los hats respiran. Idempotente.
            DYNAMICS -> map { l, st, v ->
                if (v <= 0) 0 else {
                    val pos = st % 4
                    val soft = l >= 3
                    when {
                        pos == 0 -> if (soft) 112 else 120
                        pos == 2 -> if (soft) 88 else 108
                        else -> if (soft) 64 else 96
                    }
                }
            }
            FILL -> map { l, st, v ->
                val k = st - (steps - 4) // 0..3 dentro del último tiempo
                when {
                    k < 0 -> v
                    l == 1 -> intArrayOf(78, 92, 106, 122)[k]   // redoble de caja creciente
                    l == 3 -> 0                                  // los hats dejan sitio
                    l == 4 -> if (k == 3) 0 else v
                    else -> v
                }
            }
            DOUBLE_HATS -> map { l, st, v -> if (l == 3 && v <= 0) (if (st % 2 == 0) 96 else 68) else v }
            HALF_HATS -> map { l, st, v -> if (l == 3 && st % 2 == 1) 0 else v }
        }
    }
}

/** Ajusta un portapapeles de 16/32 pasos al tamaño del patrón destino. */
fun fitLanes(src: List<List<Int>>, steps: Int): List<List<Int>> =
    List(8) { l -> List(steps) { s -> src.getOrNull(l)?.let { lane -> if (lane.isEmpty()) 0 else lane[s % lane.size] } ?: 0 } }

@Composable
internal fun SwingSlider(value: Float, onCommit: (Float) -> Unit) {
    var v by remember(value) { mutableStateOf(value) }
    androidx.compose.material3.Slider(
        v, { v = it }, valueRange = 0f..1f, onValueChangeFinished = { onCommit(v) }, modifier = Modifier.height(36.dp),
        colors = androidx.compose.material3.SliderDefaults.colors(thumbColor = Palette.Text, activeTrackColor = Palette.Violet, inactiveTrackColor = Palette.SurfaceTop),
    )
}

@Composable
private fun StepGrid(
    state: ProjectState, track: TrackState, pattern: PatternState, laneNames: List<String>, position: LongState, playing: Boolean,
    velocityMode: Boolean, page: Int, onPage: (Int) -> Unit, slot: Int, actions: StudioActions, onLaneSettings: (Int) -> Unit,
    rollMode: Boolean = false,
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
            if (pages > 1) StepOverview(lanes, pattern.steps, visible, page.coerceIn(0, pages - 1), onPage, Modifier.padding(start = LANE_HEADER, bottom = 6.dp).width(gridW))
            Row {
                Column(Modifier.width(LANE_HEADER)) {
                    for (l in 0 until 8) {
                        val muted = (track.params["mute$l"] ?: 0f) >= 0.5f
                        val solo = (track.params["solo$l"] ?: 0f) >= 0.5f
                        Row(Modifier.height(CELL_H).padding(end = 6.dp, bottom = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                            Box(Modifier.width(4.dp).height(CELL_H - 12.dp).background(LANE_COLORS[l], RoundedCornerShape(2.dp)))
                            Text(
                                laneNames.getOrElse(l) { "?" }, fontSize = 11.sp, lineHeight = 12.sp, maxLines = 2, overflow = TextOverflow.Ellipsis,
                                color = if (muted) Palette.TextFaint else if (solo) Palette.Solo else Palette.Text,
                                modifier = Modifier.weight(1f).clip(RoundedCornerShape(6.dp))
                                    .clickable { actions.liveNote(slot, DRUM_BASE_NOTE + l, 110, true) }.padding(horizontal = 6.dp, vertical = 6.dp),
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
                    Modifier.width(gridW).height(CELL_H * 8).pointerInput(pattern, visible, first, velocityMode, rollMode) {
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
                            if (rollMode) {
                                // Redoble: cada toque pasa al siguiente número de golpes (1 → 2 → 3 → 4 → 1). Un paso apagado
                                // se enciende directamente con 2 golpes.
                                val (l, st) = c0
                                if (current[l][st] == 0) {
                                    actions.setStep(track.id, pattern.id, l, st, 100)
                                    actions.setRoll(track.id, pattern.id, l, st, 2)
                                } else actions.setRoll(track.id, pattern.id, l, st, pattern.rollAt(l, st) % 4 + 1)
                                do {
                                    val ev = awaitPointerEvent()
                                    val ch = ev.changes.firstOrNull { it.id == down.id } ?: break
                                    ch.consume()
                                } while (ch.pressed)
                                return@awaitEachGesture
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
                    val u = 1.dp.toPx()
                    val gap = 3 * u
                    val corner = CornerRadius(6 * u)
                    for (l in 0 until 8) for (i in 0 until visible) {
                        val s = first + i
                        val v = lanes[l].getOrElse(s) { 0 }
                        // Los tiempos (grupos de 4 pasos) se separan un poco más y alternan de tono.
                        val x = i * cellPx + (if (s % 4 == 0) u else 0f)
                        val w = cellPx - gap - (if (s % 4 == 0) u else 0f)
                        val y = l * cellHPx
                        val beatShade = if ((s / 4) % 2 == 0) Palette.SurfaceHigh else Palette.Surface
                        drawRoundRect(beatShade, Offset(x + gap / 2, y + gap / 2), Size(w, cellHPx - gap), corner)
                        if (s % 4 == 0 && v <= 0) drawCircle(Palette.GridStrong, 1.5f * u, Offset(x + gap / 2 + w / 2, y + cellHPx / 2))
                        if (v > 0) {
                            val c = LANE_COLORS[l]
                            // El relleno muestra la intensidad: más alto y más opaco = más fuerte.
                            val k = v / 127f
                            val h = (cellHPx - gap) * (0.4f + 0.6f * k)
                            drawRoundRect(c.copy(alpha = 0.22f), Offset(x + gap / 2, y + gap / 2), Size(w, cellHPx - gap), corner)
                            drawRoundRect(c.copy(alpha = 0.55f + 0.45f * k), Offset(x + gap / 2, y + gap / 2 + (cellHPx - gap - h)), Size(w, h), corner)
                            // Redoble: una marca blanca por golpe en la parte superior de la celda.
                            val roll = pattern.rollAt(l, s)
                            if (roll > 1) {
                                val mw = (w - 6 * u) / roll
                                for (r in 0 until roll) drawRoundRect(Color.White.copy(alpha = 0.9f), Offset(x + gap / 2 + 3 * u + r * mw + u, y + gap / 2 + 3 * u), Size(mw - 2 * u, 3 * u), CornerRadius(1.5f * u))
                            }
                        }
                    }
                    // Cursor del secuenciador: se lee la posición en la fase de dibujo (no recompone la cuadrícula).
                    val pos = position.longValue
                    val clip = track.midiClips.firstOrNull { c -> val t = st.framesToTicks(pos); t >= c.start && t < c.end }
                    if (clip != null && clip.patternId == pattern.id) {
                        val stp = MusicTime.activeStep(pos, st.bpm, clip.start, clip.length, pattern.steps)
                        if (stp in first until first + visible) {
                            val x = (stp - first) * cellPx
                            drawRoundRect(Palette.Cyan.copy(alpha = 0.12f), Offset(x + gap / 2, 0f), Size(cellPx - gap, size.height), corner)
                            drawRoundRect(Palette.Cyan, Offset(x + gap / 2, 0f), Size(cellPx - gap, size.height), corner, style = Stroke(1.5f * u))
                        }
                    }
                }
            }
        }
    }
}

/**
 * Vista general del patrón completo (16 o 32 pasos) con la página visible resaltada y el paso que suena. Tocar una
 * mitad cambia de página. Sustituye a los botones «1–8 / 9–16»: se ve dónde hay golpes sin cambiar de página.
 */
@Composable
private fun StepOverview(lanes: List<List<Int>>, steps: Int, visible: Int, page: Int, onPage: (Int) -> Unit, modifier: Modifier = Modifier) {
    val pages = steps / visible
    Canvas(
        modifier.height(30.dp).clip(RoundedCornerShape(8.dp)).background(Palette.Surface)
            .semantics { contentDescription = tr("Página de pasos ${page + 1} de $pages", "Step page ${page + 1} of $pages") }
            .pointerInput(pages) { detectTapGestures { o -> onPage((o.x / size.width * pages).toInt().coerceIn(0, pages - 1)) } },
    ) {
        val u = 1.dp.toPx()
        val cw = size.width / steps
        val rowH = (size.height - 6 * u) / 8
        drawRoundRect(Palette.Violet.copy(alpha = 0.16f), Offset(page * visible * cw, 0f), Size(visible * cw, size.height), CornerRadius(8 * u))
        drawRoundRect(Palette.Violet.copy(alpha = 0.7f), Offset(page * visible * cw + u / 2, u / 2), Size(visible * cw - u, size.height - u), CornerRadius(8 * u), style = Stroke(u))
        for (l in 0 until 8) for (st in 0 until steps) {
            if (lanes[l].getOrElse(st) { 0 } > 0) drawRect(LANE_COLORS[l], Offset(st * cw + u, 3 * u + l * rowH), Size(cw - 2 * u, max(u, rowH - u / 2)))
        }
        for (b in 1 until steps / 4) drawLine(Palette.GridStrong.copy(alpha = 0.6f), Offset(b * 4 * cw, 0f), Offset(b * 4 * cw, size.height), 1f)
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
        dismissButton = {
            // Vuelve a los valores de fábrica del carril (una sola entrada de deshacer).
            TextButton(onClick = {
                mapOf("vol" to 0f, "pan" to 0f, "pitch" to 0f, "decay" to 1f).forEach { (id, d) -> actions.setParam(track.id, "$id$lane", d) }
                actions.commitMixer()
            }) { Text(tr("Restablecer", "Reset")) }
        },
    )
}
