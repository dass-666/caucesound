package com.caucesound.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.calculatePan
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.material.icons.filled.Close
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.GridOn
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material.icons.filled.SelectAll
import androidx.compose.material.icons.filled.TabUnselected
import androidx.compose.material.icons.filled.ZoomIn
import androidx.compose.material.icons.filled.ZoomOut
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Slider
import androidx.compose.material3.SliderDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.LongState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clipToBounds
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.data.MidiClipState
import com.caucesound.app.data.MusicTime
import com.caucesound.app.data.NoteState
import com.caucesound.app.data.PPQ
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.TrackState
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import androidx.compose.runtime.rememberCoroutineScope
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

private val KEYS_W = 56.dp
private val RULER_H = 28.dp

/** Operaciones puras del piano roll (probadas en la JVM). Ticks relativos al clip. */
object PianoRollOps {
    fun moved(n: NoteState, dTicks: Long, dKeys: Int) = n.copy(start = max(0, n.start + dTicks), key = (n.key + dKeys).coerceIn(0, 127))
    fun resized(n: NoteState, newEnd: Long, grid: Long) = n.copy(length = max(minLength(grid), newEnd - n.start))
    /** Duración mínima de una nota: media celda de la rejilla, o una fusa de 1/64 sin rejilla. */
    fun minLength(grid: Long) = if (grid > 0) max(1L, grid / 2) else max(1L, PPQ / 16L)
    /** Redondea a la rejilla con signo (a diferencia de MusicTime.snapTicks, que no baja de 0). grid ≤ 0 = sin imán. */
    fun snapSigned(t: Long, grid: Long): Long = if (grid <= 0) t else Math.round(t.toDouble() / grid) * grid

    /**
     * Arrastre de notas (0.8). Corrige el fallo «no se puede mover hacia atrás»: antes el desplazamiento se redondeaba
     * con MusicTime.snapTicks, que recorta a 0 los valores negativos, así que cualquier arrastre hacia la izquierda
     * valía 0. Ahora:
     *  - el imán se aplica a la posición final de la nota que tiene el dedo (ancla), no al desplazamiento;
     *  - el grupo se mueve con el mismo desplazamiento, sin bajar de 0 ni salir de 0..127 (el grupo conserva su forma);
     *  - la duración, la intensidad y el canal no cambian.
     * rawTicks es el desplazamiento horizontal del dedo sin redondear (puede ser negativo).
     */
    fun dragMove(notes: List<NoteState>, group: Set<Int>, anchor: Int, rawTicks: Long, dKeys: Int, grid: Long): List<NoteState> {
        if (group.isEmpty() || anchor !in notes.indices) return notes
        val a = notes[anchor]
        var dt = snapSigned(a.start + rawTicks, grid) - a.start
        val minStart = group.minOf { notes[it].start }
        if (minStart + dt < 0) dt = -minStart
        val lo = group.minOf { notes[it].key }; val hi = group.maxOf { notes[it].key }
        val dk = dKeys.coerceIn(-lo, 127 - hi)
        return notes.mapIndexed { i, x -> if (i in group) x.copy(start = x.start + dt, key = x.key + dk) else x }
    }

    /** Cambio de duración arrastrando el borde derecho (alarga y acorta). El final del ancla va al imán. */
    fun dragResize(notes: List<NoteState>, group: Set<Int>, anchor: Int, rawTicks: Long, grid: Long): List<NoteState> {
        if (group.isEmpty() || anchor !in notes.indices) return notes
        val a = notes[anchor]
        val dEnd = snapSigned(a.end + rawTicks, grid) - a.end
        val minLen = minLength(grid)
        return notes.mapIndexed { i, x -> if (i in group) x.copy(length = max(minLen, x.length + dEnd)) else x }
    }
    fun transpose(notes: List<NoteState>, sel: Set<Int>, semis: Int) = notes.mapIndexed { i, n -> if (i in sel) n.copy(key = (n.key + semis).coerceIn(0, 127)) else n }
    /** Duplica la selección justo detrás de sí misma (edición por grupos). Devuelve la lista nueva y los índices de las copias. */
    fun duplicate(notes: List<NoteState>, sel: Set<Int>): Pair<List<NoteState>, Set<Int>> {
        if (sel.isEmpty()) return notes to sel
        val chosen = sel.map { notes[it] }
        val span = chosen.maxOf { it.end } - chosen.minOf { it.start }
        val copies = chosen.map { it.copy(start = it.start + span) }
        return (notes + copies) to (notes.size until notes.size + copies.size).toSet()
    }
    /** Repite todo el contenido del clip a continuación (el clip pasa a durar el doble). */
    fun doubled(notes: List<NoteState>, clipLength: Long): List<NoteState> =
        notes + notes.filter { it.start < clipLength }.map { it.copy(start = it.start + clipLength) }
    /** Índice de la nota cuyo inicio está más cerca de 'tick' (para el carril de intensidad), o -1. */
    fun nearestByStart(notes: List<NoteState>, tick: Long, maxDistance: Long): Int {
        var best = -1
        var bestD = Long.MAX_VALUE
        notes.forEachIndexed { i, n -> val d = kotlin.math.abs(n.start - tick); if (d < bestD || (d == bestD && best >= 0 && n.velocity > notes[best].velocity)) { best = i; bestD = d } }
        return if (bestD <= maxDistance) best else -1
    }
    fun hit(notes: List<NoteState>, tick: Long, key: Int): Int = notes.indexOfLast { it.key == key && tick >= it.start && tick < it.end }
}

/** Escalas para resaltar filas del piano roll (ayuda visual: no impide escribir notas fuera de la escala). */
enum class Scale(val label: String, val steps: Set<Int>) {
    NONE("—", emptySet()),
    MAJOR("Mayor", setOf(0, 2, 4, 5, 7, 9, 11)),
    MINOR("Menor", setOf(0, 2, 3, 5, 7, 8, 10)),
    MINOR_PENTA("Pentatónica menor", setOf(0, 3, 5, 7, 10)),
    HARMONIC_MINOR("Menor armónica", setOf(0, 2, 3, 5, 7, 8, 11));

    fun contains(key: Int, root: Int) = this == NONE || ((key - root) % 12 + 12) % 12 in steps
    fun title() = when (this) { NONE -> tr("Sin escala", "No scale"); MAJOR -> tr("Mayor", "Major"); MINOR -> tr("Menor", "Minor")
        MINOR_PENTA -> tr("Pentatónica menor", "Minor pentatonic"); HARMONIC_MINOR -> tr("Menor armónica", "Harmonic minor") }
}
val NOTE_NAMES = listOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")

private val VEL_LANE_H = 64.dp

@Composable
fun PianoRollPanel(state: ProjectState, track: TrackState, clip: MidiClipState, position: LongState, playing: Boolean, actions: StudioActions) {
    val density = LocalDensity.current
    val slot = state.trackIndex(track.id)
    var notes by remember(clip) { mutableStateOf(clip.notes) }
    var selection by remember(clip) { mutableStateOf(setOf<Int>()) }
    var grid by remember { mutableLongStateOf(PPQ / 4L) }
    var drawMode by remember { mutableStateOf(true) }
    var defaultLen by remember { mutableLongStateOf(PPQ / 2L) }
    var pxPerTick by remember { mutableFloatStateOf(with(density) { 64.dp.toPx() } / PPQ) } // 64 dp por negra
    val rowH = with(density) { 26.dp.toPx() }
    var offX by remember { mutableFloatStateOf(0f) }
    var offY by remember(clip.id) {
        // La nota más aguda queda dos filas bajo el borde superior (sin notas: C5 arriba, C4 visible).
        val top = clip.notes.maxOfOrNull { it.key }?.let { it + 2 } ?: 74
        mutableFloatStateOf(((127 - top) * rowH).coerceAtLeast(0f))
    }
    var rect by remember { mutableStateOf<Rect?>(null) }
    var scale by rememberSaveable { mutableStateOf(Scale.NONE) }
    var scaleRoot by rememberSaveable { mutableIntStateOf(0) }
    var scaleMenu by remember { mutableStateOf(false) }
    var gridHeight by remember { mutableFloatStateOf(0f) }
    fun scrollOctave(up: Boolean) { offY = (offY + (if (up) -12 else 12) * rowH).coerceIn(0f, max(0f, 128 * rowH - gridHeight)) }
    val text = rememberTextMeasurer()
    val scope = rememberCoroutineScope()
    val n by rememberUpdatedState(notes)
    val sel by rememberUpdatedState(selection)
    val st by rememberUpdatedState(state)

    fun commit(list: List<NoteState>) { notes = list; actions.setClipNotes(clip.id, list) }
    fun preview(key: Int, vel: Int = 100) {
        if (playing) return
        actions.liveNote(slot, key, vel, true)
        scope.launch { delay(220); actions.liveNote(slot, key, 0, false) }
    }

    Column(Modifier.fillMaxSize()) {
        // --- Herramientas: fila fija (rejilla, modo, escala, zoom) y fila de acciones según haya selección ---
        val fixedTools: @Composable () -> Unit = {
            // «Libre» desactiva el imán: las notas se colocan y se mueven sin ajustarse a la rejilla.
            val grids = listOf(PPQ.toLong(), PPQ / 2L, PPQ / 4L, PPQ / 8L, 0L)
            Segmented(listOf("1/4", "1/8", "1/16", "1/32", tr("Libre", "Free")), grids.indexOf(grid).coerceAtLeast(0), { grid = grids[it] }, height = 40.dp)
            ToolButton(Icons.Default.Edit, tr("Dibujar notas", "Draw notes"), { drawMode = true }, active = drawMode)
            ToolButton(Icons.Default.TabUnselected, tr("Seleccionar con rectángulo", "Rectangle select"), { drawMode = false }, active = !drawMode)
            Box {
                PillToggle(if (scale == Scale.NONE) tr("Escala", "Scale") else "${NOTE_NAMES[scaleRoot]} ${scale.title()}", scale != Scale.NONE, { scaleMenu = true }, height = 40.dp)
                DropdownMenu(scaleMenu, { scaleMenu = false }) {
                    Row(Modifier.padding(horizontal = 12.dp, vertical = 4.dp).horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        NOTE_NAMES.forEachIndexed { i, nn -> PillToggle(nn, i == scaleRoot, { scaleRoot = i; if (scale == Scale.NONE) scale = Scale.MINOR }, minWidth = 40.dp, height = 40.dp) }
                    }
                    Scale.entries.forEach { sc ->
                        DropdownMenuItem(text = { Text(sc.title(), fontWeight = if (sc == scale) FontWeight.Bold else FontWeight.Normal) }, onClick = { scale = sc; scaleMenu = false })
                    }
                }
            }
            ToolButton(Icons.Default.ZoomOut, tr("Alejar", "Zoom out"), { pxPerTick = (pxPerTick / 1.4f).coerceAtLeast(0.005f) })
            ToolButton(Icons.Default.ZoomIn, tr("Acercar", "Zoom in"), { pxPerTick = (pxPerTick * 1.4f).coerceAtMost(1.5f) })
            ToolButton(Icons.Default.KeyboardArrowUp, tr("Octava arriba", "Octave up"), { scrollOctave(true) })
            ToolButton(Icons.Default.KeyboardArrowDown, tr("Octava abajo", "Octave down"), { scrollOctave(false) })
        }
        val contextTools: @Composable () -> Unit = {
            if (selection.isNotEmpty()) {
                Text(tr("${selection.size} sel.", "${selection.size} sel."), style = ValueText, color = Palette.VioletSoft, modifier = Modifier.padding(horizontal = 4.dp))
                CauceButton(tr("Duplicar", "Duplicate"), { val (l, sl) = PianoRollOps.duplicate(notes, selection); selection = sl; commit(l) }, icon = Icons.Default.ContentCopy, height = 40.dp)
                ToolButton(Icons.Default.KeyboardArrowUp, tr("Subir un semitono", "Up one semitone"), { commit(PianoRollOps.transpose(notes, selection, 1)) }, tint = Palette.Text)
                ToolButton(Icons.Default.KeyboardArrowDown, tr("Bajar un semitono", "Down one semitone"), { commit(PianoRollOps.transpose(notes, selection, -1)) }, tint = Palette.Text)
                ToolButton(Icons.Default.Delete, tr("Borrar", "Delete"), { val keep = notes.filterIndexed { i, _ -> i !in selection }; selection = emptySet(); commit(keep) }, tint = Palette.Record)
                ToolButton(Icons.Default.Close, tr("Quitar selección", "Clear selection"), { selection = emptySet() })
            } else {
                CauceButton(tr("Cuantizar", "Quantize"), { actions.quantizeClip(clip.id, if (grid > 0) grid else PPQ / 4L) }, icon = Icons.Default.GridOn, height = 40.dp)
                CauceButton(tr("Todo", "All"), { selection = notes.indices.toSet() }, icon = Icons.Default.SelectAll, height = 40.dp)
                // Repetir el clip entero: la forma rápida de pasar de 2 a 4 compases (o de 4 a 8).
                CauceButton(tr("Repetir ×2", "Repeat ×2"), {
                    actions.resizeMidiClip(clip.id, clip.start, clip.length * 2)
                    commit(PianoRollOps.doubled(notes, clip.length))
                }, icon = Icons.Default.ContentCopy, height = 40.dp)
                CauceButton(tr("+1 compás", "+1 bar"), { actions.resizeMidiClip(clip.id, clip.start, clip.length + 4L * PPQ * state.beatsPerBar / 4) }, height = 40.dp)
            }
        }
        // En horizontal (poca altura) las dos filas se funden en una sola, desplazable, para dejar sitio a las notas.
        val compact = androidx.compose.ui.platform.LocalConfiguration.current.screenHeightDp < 500
        if (compact) {
            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 8.dp, vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) { fixedTools(); contextTools() }
        } else {
            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 8.dp, vertical = 4.dp),
                verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) { fixedTools() }
            Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 8.dp).padding(bottom = 4.dp),
                verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) { contextTools() }
        }

        // --- Regla ---
        Row(Modifier.fillMaxWidth().height(RULER_H)) {
            Box(Modifier.width(KEYS_W).height(RULER_H).background(Palette.Surface))
            Canvas(Modifier.weight(1f).height(RULER_H).background(Palette.Surface).pointerInput(clip.id) {
                awaitEachGesture {
                    val d = awaitFirstDown()
                    val t = ((d.position.x + offX) / pxPerTick).toLong()
                    actions.seek(st.ticksToFrames(clip.start + MusicTime.floorTicks(t, grid)))
                }
            }) {
                val beat = PPQ * pxPerTick
                val bar = beat * state.beatsPerBar
                var b = (offX / bar).toInt()
                while (b * bar - offX < size.width) {
                    val x = b * bar - offX
                    drawLine(Palette.GridStrong, Offset(x, 0f), Offset(x, size.height), 1f)
                    drawText(text, "${b + 1 + (clip.start / (PPQ * state.beatsPerBar)).toInt()}", Offset(x + 4.dp.toPx(), 5.dp.toPx()), TextStyle(color = Palette.TextDim, fontSize = 11.sp, fontWeight = FontWeight.Medium))
                    b++
                }
                val pos = st.framesToTicks(position.longValue) - clip.start
                if (pos in 0..clip.length) drawLine(Palette.Cyan, Offset(pos * pxPerTick - offX, 0f), Offset(pos * pxPerTick - offX, size.height), 2.dp.toPx())
            }
        }

        Row(Modifier.fillMaxWidth().weight(1f).clipToBounds()) {
            // --- Teclado: toca para oír, arrastra para desplazarte entre octavas ---
            Canvas(Modifier.width(KEYS_W).fillMaxSize().clipToBounds().pointerInput(clip.id) {
                awaitEachGesture {
                    val d = awaitFirstDown()
                    val key = 127 - ((d.position.y + offY) / rowH).toInt()
                    var moved = false
                    var last = d.position
                    do {
                        val ev = awaitPointerEvent()
                        val c = ev.changes.firstOrNull { it.id == d.id } ?: break
                        val dy = c.position.y - last.y
                        if (abs(c.position.y - d.position.y) > viewConfiguration.touchSlop) moved = true
                        if (moved) { offY = (offY - dy).coerceIn(0f, max(0f, 128 * rowH - size.height)); c.consume() }
                        last = c.position
                    } while (c.pressed)
                    if (!moved) preview(key.coerceIn(0, 127))
                }
            }) {
                clipRect {
                    val first = (offY / rowH).toInt()
                    for (r in first..min(127, first + (size.height / rowH).toInt() + 1)) {
                        val key = 127 - r
                        val y = r * rowH - offY
                        val black = MusicTime.isBlack(key)
                        val inScale = scale.contains(key, scaleRoot)
                        val white = if (inScale) Color(0xFFDDE0E7) else Color(0xFF9A9FAB)
                        drawRect(if (black) Color(0xFF15171D) else white, Offset(0f, y), Size(if (black) size.width * 0.62f else size.width, rowH - 1f))
                        if (black) drawRect(white, Offset(size.width * 0.62f, y), Size(size.width * 0.38f, rowH - 1f))
                        if (scale != Scale.NONE && ((key - scaleRoot) % 12 + 12) % 12 == 0) drawRect(Palette.Violet, Offset(0f, y), Size(3.dp.toPx(), rowH - 1f))
                        if (key % 12 == 0) drawText(text, MusicTime.noteName(key), Offset(size.width * 0.6f, y + rowH * 0.22f), TextStyle(color = Color(0xFF3A3F4B), fontSize = 10.sp, fontWeight = FontWeight.Bold))
                    }
                }
            }

            // --- Rejilla de notas ---
            Canvas(
                Modifier.weight(1f).fillMaxSize().background(Palette.Background)
                    // Dos dedos: desplazar y hacer zoom. Se procesa antes que la edición y la cancela.
                    .pointerInput(clip.id) {
                        awaitEachGesture {
                            awaitFirstDown(requireUnconsumed = false)
                            do {
                                val ev = awaitPointerEvent(PointerEventPass.Initial)
                                val pressed = ev.changes.count { it.pressed }
                                if (pressed >= 2) {
                                    val zoom = ev.calculateZoom()
                                    val pan = ev.calculatePan()
                                    val newPx = (pxPerTick * zoom).coerceIn(0.005f, 1.5f)
                                    val cx = ev.changes.filter { it.pressed }.map { it.position.x }.average().toFloat()
                                    offX = ((offX + cx) * newPx / pxPerTick - cx - pan.x).coerceAtLeast(0f)
                                    pxPerTick = newPx
                                    offY = (offY - pan.y).coerceIn(0f, max(0f, 128 * rowH - size.height))
                                    ev.changes.forEach { it.consume() }
                                }
                            } while (ev.changes.any { it.pressed })
                        }
                    }
                    .pointerInput(clip.id, drawMode, grid) {
                        awaitEachGesture {
                            val down = awaitFirstDown()
                            fun tickAt(x: Float) = ((x + offX) / pxPerTick).toLong().coerceAtLeast(0)
                            fun keyAt(y: Float) = (127 - ((y + offY) / rowH).toInt()).coerceIn(0, 127)
                            val t0 = tickAt(down.position.x)
                            val k0 = keyAt(down.position.y)
                            val before = n
                            val hitIdx = PianoRollOps.hit(before, t0, k0)
                            val edgePx = max(with(density) { 14.dp.toPx() }, 0f)
                            var work = before
                            var cancelled = false
                            var movedFar = false
                            if (hitIdx >= 0) {
                                val note = before[hitIdx]
                                val resize = (note.end * pxPerTick - offX) - down.position.x < edgePx && note.length * pxPerTick > edgePx * 1.5f
                                val group = if (hitIdx in sel) sel else setOf(hitIdx)
                                var lastHeard = note.key
                                preview(note.key, note.velocity)
                                do {
                                    val ev = awaitPointerEvent()
                                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                                    if (c.isConsumed || ev.changes.count { it.pressed } > 1) { cancelled = true; break }
                                    if ((c.position - down.position).getDistance() > viewConfiguration.touchSlop) movedFar = true
                                    if (movedFar && c.pressed) {
                                        // Desplazamiento del dedo en píxeles → ticks, con signo (izquierda = negativo).
                                        val raw = ((c.position.x - down.position.x) / pxPerTick).toLong()
                                        val dKeys = -Math.floorDiv((c.position.y - down.position.y + rowH / 2).toInt(), rowH.toInt().coerceAtLeast(1))
                                        work = if (resize) PianoRollOps.dragResize(before, group, hitIdx, raw, grid)
                                        else PianoRollOps.dragMove(before, group, hitIdx, raw, dKeys, grid)
                                        // Al cambiar de tono durante el arrastre se oye la nota nueva (si no está sonando la canción).
                                        if (!resize && work[hitIdx].key != lastHeard) { lastHeard = work[hitIdx].key; preview(lastHeard, note.velocity) }
                                        notes = work
                                        c.consume()
                                    }
                                } while (c.pressed)
                                if (cancelled) { notes = before; return@awaitEachGesture }
                                if (movedFar) {
                                    if (resize) defaultLen = work[hitIdx].length
                                    commit(work)
                                } else {
                                    // Toque: selecciona / deselecciona. Doble toque rápido sobre la misma nota la borra.
                                    val now = System.currentTimeMillis()
                                    if (lastTapIndex == hitIdx && now - lastTapTime < 350) {
                                        selection = emptySet()
                                        commit(before.filterIndexed { i, _ -> i != hitIdx })
                                        lastTapIndex = -1
                                    } else {
                                        selection = if (drawMode) setOf(hitIdx) else if (hitIdx in sel) sel - hitIdx else sel + hitIdx
                                        lastTapIndex = hitIdx; lastTapTime = now
                                    }
                                }
                            } else if (drawMode) {
                                val start = MusicTime.floorTicks(t0, grid)
                                val created = NoteState(k0, 100, 0, start, defaultLen)
                                work = before + created
                                notes = work
                                preview(k0)
                                do {
                                    val ev = awaitPointerEvent()
                                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                                    if (c.isConsumed || ev.changes.count { it.pressed } > 1) { cancelled = true; break }
                                    if ((c.position - down.position).getDistance() > viewConfiguration.touchSlop) movedFar = true
                                    if (movedFar && c.pressed) {
                                        val len = max(PianoRollOps.minLength(grid) * 2, MusicTime.snapTicks(tickAt(c.position.x) - start + grid / 2, grid))
                                        work = before + created.copy(length = len)
                                        notes = work
                                        c.consume()
                                    }
                                } while (c.pressed)
                                if (cancelled) { notes = before; return@awaitEachGesture }
                                if (movedFar) defaultLen = work.last().length
                                selection = emptySet()
                                commit(work)
                            } else {
                                // Selección por rectángulo
                                var r = Rect(down.position, down.position)
                                do {
                                    val ev = awaitPointerEvent()
                                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                                    if (c.isConsumed || ev.changes.count { it.pressed } > 1) { cancelled = true; break }
                                    r = Rect(Offset(min(down.position.x, c.position.x), min(down.position.y, c.position.y)), Offset(max(down.position.x, c.position.x), max(down.position.y, c.position.y)))
                                    rect = r
                                    c.consume()
                                } while (c.pressed)
                                rect = null
                                if (cancelled) return@awaitEachGesture
                                val tA = tickAt(r.left); val tB = tickAt(r.right); val kA = keyAt(r.bottom); val kB = keyAt(r.top)
                                selection = before.indices.filter { i -> val x = before[i]; x.end > tA && x.start < tB && x.key in kA..kB }.toSet()
                            }
                        }
                    },
            ) {
                val beat = PPQ * pxPerTick
                val bar = beat * state.beatsPerBar
                val first = (offY / rowH).toInt()
                val lastRow = min(127, first + (size.height / rowH).toInt() + 1)
                for (r in first..lastRow) {
                    val key = 127 - r
                    val y = r * rowH - offY
                    val inScale = scale.contains(key, scaleRoot)
                    val isRoot = scale != Scale.NONE && ((key - scaleRoot) % 12 + 12) % 12 == 0
                    drawRect(when {
                        !inScale -> Color(0xFF0B0C10)                                   // fuera de la escala: se apaga
                        isRoot -> Color(0xFF1D1A2E)                                     // tónica
                        MusicTime.isBlack(key) && scale == Scale.NONE -> Color(0xFF101217)
                        else -> Color(0xFF161920)
                    }, Offset(0f, y), Size(size.width, rowH))
                    drawLine(if (key % 12 == 0) Palette.GridStrong else Palette.Grid.copy(alpha = 0.6f), Offset(0f, y + rowH), Offset(size.width, y + rowH), 1f)
                }
                // Rejilla vertical
                val gridPx = grid * pxPerTick
                gridHeight = size.height
                val u = 1.dp.toPx()
                if (gridPx > 2 * u) {
                    var g = (offX / gridPx).toInt()
                    while (g * gridPx - offX < size.width) {
                        val x = g * gridPx - offX
                        val tick = (g * grid)
                        val c = when { tick % (PPQ * state.beatsPerBar) == 0L -> Palette.GridStrong; tick % PPQ == 0L -> Palette.Grid; else -> Palette.Grid.copy(alpha = 0.35f) }
                        drawLine(c, Offset(x, 0f), Offset(x, size.height), if (tick % (PPQ * state.beatsPerBar) == 0L) u else 1f)
                        g++
                    }
                }
                // Fuera del clip: sombreado (esas notas no suenan)
                val endX = clip.length * pxPerTick - offX
                if (endX < size.width) drawRect(Color.Black.copy(alpha = 0.45f), Offset(max(0f, endX), 0f), Size(size.width - max(0f, endX), size.height))
                // Notas
                val color = Color(track.color.toInt())
                notes.forEachIndexed { i, x ->
                    val row = 127 - x.key
                    if (row < first - 1 || row > lastRow + 1) return@forEachIndexed
                    val nx = x.start * pxPerTick - offX
                    val nw = max(2 * u, x.length * pxPerTick - u)
                    if (nx > size.width || nx + nw < 0) return@forEachIndexed
                    val y = row * rowH - offY
                    val selected = i in selection
                    // Relleno según la intensidad; borde siempre visible para distinguir notas contiguas.
                    val k = x.velocity / 127f
                    val fill = if (selected) Color(color.red + (1 - color.red) * 0.55f, color.green + (1 - color.green) * 0.55f, color.blue + (1 - color.blue) * 0.55f) else color
                    val r = CornerRadius(3 * u)
                    drawRoundRect(fill.copy(alpha = 0.4f + 0.6f * k), Offset(nx, y + u), Size(nw, rowH - 2 * u), r)
                    drawRoundRect(if (selected) Color.White else Color.Black.copy(alpha = 0.45f), Offset(nx, y + u), Size(nw, rowH - 2 * u), r, style = Stroke(if (selected) 1.5f * u else u))
                    if (nw > 14 * u) drawRoundRect(Color.White.copy(alpha = if (selected) 0.9f else 0.55f), Offset(nx + nw - 4 * u, y + rowH * 0.3f), Size(1.5f * u, rowH * 0.4f), CornerRadius(u))
                }
                rect?.let { r -> drawRect(Palette.Violet.copy(alpha = 0.16f), r.topLeft, r.size); drawRect(Palette.VioletSoft, r.topLeft, r.size, style = Stroke(u)) }
                // Cursor de reproducción (fase de dibujo: no recompone)
                val pos = st.framesToTicks(position.longValue) - clip.start
                if (pos in 0..clip.length) {
                    val x = pos * pxPerTick - offX
                    drawLine(Palette.Playhead, Offset(x, 0f), Offset(x, size.height), 1.5f * u)
                }
                if (bar <= 0f) return@Canvas
            }
        }
        VelocityLane(notes, selection, Color(track.color.toInt()), pxPerTick, offX, clip.length, onPreview = { notes = it }, onCommit = { commit(it) })
    }
    LaunchedEffect(clip.id) { selection = emptySet() }
}

/**
 * Carril de intensidad (velocity): una barra por nota en la posición de su inicio. Arrastrar en vertical cerca de
 * una barra cambia la intensidad de esa nota (o de toda la selección si la nota está seleccionada).
 */
@Composable
private fun VelocityLane(
    notes: List<NoteState>, selection: Set<Int>, color: Color, pxPerTick: Float, offX: Float, clipLength: Long,
    onPreview: (List<NoteState>) -> Unit, onCommit: (List<NoteState>) -> Unit,
) {
    val n by rememberUpdatedState(notes)
    val sel by rememberUpdatedState(selection)
    val px by rememberUpdatedState(pxPerTick)
    val ox by rememberUpdatedState(offX)
    Row(Modifier.fillMaxWidth().height(if (androidx.compose.ui.platform.LocalConfiguration.current.screenHeightDp < 500) 44.dp else VEL_LANE_H).background(Palette.Surface)) {
        Box(Modifier.width(KEYS_W).fillMaxHeight(), contentAlignment = Alignment.Center) {
            Text(tr("Fuerza", "Velocity"), fontSize = 10.sp, color = Palette.TextFaint)
        }
        Canvas(
            Modifier.weight(1f).fillMaxHeight().clipToBounds().semantics { contentDescription = tr("Carril de intensidad", "Velocity lane") }
                .pointerInput(Unit) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        val before = n
                        val idx = PianoRollOps.nearestByStart(before, ((down.position.x + ox) / px).toLong(), (16.dp.toPx() / px).toLong())
                        if (idx < 0) return@awaitEachGesture
                        val group = if (idx in sel) sel else setOf(idx)
                        fun at(y: Float): List<NoteState> {
                            val v = (127 - (y / size.height) * 126).toInt().coerceIn(1, 127)
                            return before.mapIndexed { i, x -> if (i in group) x.copy(velocity = v) else x }
                        }
                        var work = at(down.position.y)
                        onPreview(work)
                        do {
                            val ev = awaitPointerEvent()
                            val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                            if (c.pressed) { work = at(c.position.y); onPreview(work); c.consume() }
                        } while (c.pressed)
                        if (work != before) onCommit(work)
                    }
                },
        ) {
            val u = 1.dp.toPx()
            drawLine(Palette.Stroke, Offset(0f, 0f), Offset(size.width, 0f), 1f)
            drawLine(Palette.Grid.copy(alpha = 0.6f), Offset(0f, size.height / 2), Offset(size.width, size.height / 2), 1f)
            notes.forEachIndexed { i, x ->
                if (x.start >= clipLength) return@forEachIndexed
                val nx = x.start * pxPerTick - offX
                if (nx < -4 * u || nx > size.width) return@forEachIndexed
                val h = (size.height - 4 * u) * x.velocity / 127f
                val c = if (i in selection) Color.White else color
                drawRoundRect(c.copy(alpha = 0.85f), Offset(nx, size.height - h), Size(3 * u, h), CornerRadius(1.5f * u))
                drawCircle(c, 3 * u, Offset(nx + 1.5f * u, size.height - h))
            }
        }
    }
}

// Doble toque (estado compartido del gesto; solo interfaz)
private var lastTapIndex = -1
private var lastTapTime = 0L
