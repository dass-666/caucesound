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
    fun resized(n: NoteState, newEnd: Long, grid: Long) = n.copy(length = max(max(1L, grid / 2), newEnd - n.start))
    fun transpose(notes: List<NoteState>, sel: Set<Int>, semis: Int) = notes.mapIndexed { i, n -> if (i in sel) n.copy(key = (n.key + semis).coerceIn(0, 127)) else n }
    /** Duplica la selección justo detrás de sí misma (edición por grupos). Devuelve la lista nueva y los índices de las copias. */
    fun duplicate(notes: List<NoteState>, sel: Set<Int>): Pair<List<NoteState>, Set<Int>> {
        if (sel.isEmpty()) return notes to sel
        val chosen = sel.map { notes[it] }
        val span = chosen.maxOf { it.end } - chosen.minOf { it.start }
        val copies = chosen.map { it.copy(start = it.start + span) }
        return (notes + copies) to (notes.size until notes.size + copies.size).toSet()
    }
    fun hit(notes: List<NoteState>, tick: Long, key: Int): Int = notes.indexOfLast { it.key == key && tick >= it.start && tick < it.end }
}

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
        // --- Herramientas ---
        Row(Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()).padding(horizontal = 8.dp, vertical = 6.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            val grids = listOf(PPQ.toLong(), PPQ / 2L, PPQ / 4L, PPQ / 8L)
            Segmented(listOf("1/4", "1/8", "1/16", "1/32"), grids.indexOf(grid).coerceAtLeast(0), { grid = grids[it] })
            PillToggle("", drawMode, { drawMode = true }, icon = Icons.Default.Edit, color = Palette.Cyan, height = 40.dp, minWidth = 44.dp)
            PillToggle("", !drawMode, { drawMode = false }, icon = Icons.Default.TabUnselected, color = Palette.Cyan, height = 40.dp, minWidth = 44.dp)
            IconButton(onClick = { actions.quantizeClip(clip.id, grid) }) { Icon(Icons.Default.GridOn, tr("Cuantizar", "Quantize"), tint = Palette.Text) }
            IconButton(onClick = { selection = notes.indices.toSet() }) { Icon(Icons.Default.SelectAll, tr("Seleccionar todo", "Select all"), tint = Palette.Text) }
            IconButton(onClick = { val (l, s) = PianoRollOps.duplicate(notes, selection); selection = s; commit(l) }, enabled = selection.isNotEmpty()) {
                Icon(Icons.Default.ContentCopy, tr("Duplicar selección", "Duplicate selection"), tint = if (selection.isEmpty()) Palette.TextFaint else Palette.Text)
            }
            IconButton(onClick = { commit(PianoRollOps.transpose(notes, selection, 1)) }, enabled = selection.isNotEmpty()) { Icon(Icons.Default.KeyboardArrowUp, "+1", tint = Palette.Text) }
            IconButton(onClick = { commit(PianoRollOps.transpose(notes, selection, -1)) }, enabled = selection.isNotEmpty()) { Icon(Icons.Default.KeyboardArrowDown, "-1", tint = Palette.Text) }
            IconButton(onClick = { val keep = notes.filterIndexed { i, _ -> i !in selection }; selection = emptySet(); commit(keep) }, enabled = selection.isNotEmpty()) {
                Icon(Icons.Default.Delete, tr("Borrar", "Delete"), tint = if (selection.isEmpty()) Palette.TextFaint else Palette.Record)
            }
            IconButton(onClick = { pxPerTick = (pxPerTick / 1.4f).coerceAtLeast(0.005f) }) { Icon(Icons.Default.ZoomOut, tr("Alejar", "Zoom out"), tint = Palette.Text) }
            IconButton(onClick = { pxPerTick = (pxPerTick * 1.4f).coerceAtMost(1.5f) }) { Icon(Icons.Default.ZoomIn, tr("Acercar", "Zoom in"), tint = Palette.Text) }
            TextButton(onClick = { actions.resizeMidiClip(clip.id, clip.start, clip.length + 4L * PPQ * state.beatsPerBar / 4) }) { Text(tr("+1 compás", "+1 bar")) }
        }
        if (selection.isNotEmpty()) {
            val avg = selection.mapNotNull { notes.getOrNull(it)?.velocity }.average().toFloat().takeIf { !it.isNaN() } ?: 100f
            var vel by remember(selection, clip) { mutableFloatStateOf(avg) }
            Row(Modifier.padding(horizontal = 12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(tr("Velocidad", "Velocity") + " ${vel.toInt()}", fontSize = 12.sp, color = Palette.TextDim, modifier = Modifier.width(96.dp))
                Slider(vel, { vel = it }, valueRange = 1f..127f, modifier = Modifier.weight(1f),
                    onValueChangeFinished = { commit(notes.mapIndexed { i, x -> if (i in selection) x.copy(velocity = vel.toInt()) else x }) },
                    colors = SliderDefaults.colors(thumbColor = Palette.Text, activeTrackColor = Palette.Cyan, inactiveTrackColor = Palette.SurfaceTop))
            }
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
                    drawText(text, "${b + 1 + (clip.start / (PPQ * state.beatsPerBar)).toInt()}", Offset(x + 4f, 4f), TextStyle(color = Palette.TextDim, fontSize = 11.sp))
                    b++
                }
                val pos = st.framesToTicks(position.longValue) - clip.start
                if (pos in 0..clip.length) drawLine(Palette.Cyan, Offset(pos * pxPerTick - offX, 0f), Offset(pos * pxPerTick - offX, size.height), 3f)
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
                        drawRect(if (black) Color(0xFF15171D) else Color(0xFFD9DCE3), Offset(0f, y), Size(if (black) size.width * 0.62f else size.width, rowH - 1f))
                        if (black) drawRect(Color(0xFFD9DCE3), Offset(size.width * 0.62f, y), Size(size.width * 0.38f, rowH - 1f))
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
                                preview(note.key, note.velocity)
                                do {
                                    val ev = awaitPointerEvent()
                                    val c = ev.changes.firstOrNull { it.id == down.id } ?: break
                                    if (c.isConsumed || ev.changes.count { it.pressed } > 1) { cancelled = true; break }
                                    if ((c.position - down.position).getDistance() > viewConfiguration.touchSlop) movedFar = true
                                    if (movedFar && c.pressed) {
                                        val dt = MusicTime.snapTicks(tickAt(c.position.x) - t0, grid)
                                        work = if (resize) before.mapIndexed { i, x -> if (i in group) PianoRollOps.resized(x, MusicTime.snapTicks(x.end + dt, grid), grid) else x }
                                        else before.mapIndexed { i, x -> if (i in group) PianoRollOps.moved(x, dt, keyAt(c.position.y) - k0) else x }
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
                                        val len = max(grid, MusicTime.snapTicks(tickAt(c.position.x) - start + grid / 2, grid))
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
                    drawRect(if (MusicTime.isBlack(key)) Color(0xFF101217) else Color(0xFF15181E), Offset(0f, y), Size(size.width, rowH))
                    drawLine(if (key % 12 == 0) Palette.GridStrong else Palette.Grid.copy(alpha = 0.6f), Offset(0f, y + rowH), Offset(size.width, y + rowH), 1f)
                }
                // Rejilla vertical
                val gridPx = grid * pxPerTick
                if (gridPx > 6f) {
                    var g = (offX / gridPx).toInt()
                    while (g * gridPx - offX < size.width) {
                        val x = g * gridPx - offX
                        val tick = (g * grid)
                        val c = when { tick % (PPQ * state.beatsPerBar) == 0L -> Palette.GridStrong; tick % PPQ == 0L -> Palette.Grid; else -> Palette.Grid.copy(alpha = 0.35f) }
                        drawLine(c, Offset(x, 0f), Offset(x, size.height), if (tick % (PPQ * state.beatsPerBar) == 0L) 2f else 1f)
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
                    val nw = max(4f, x.length * pxPerTick)
                    if (nx > size.width || nx + nw < 0) return@forEachIndexed
                    val y = row * rowH - offY
                    val selected = i in selection
                    val alpha = 0.45f + 0.55f * x.velocity / 127f
                    drawRoundRect((if (selected) Palette.Cyan else color).copy(alpha = alpha), Offset(nx, y + 2f), Size(nw, rowH - 4f), CornerRadius(6f))
                    drawRoundRect(if (selected) Color.White else color, Offset(nx, y + 2f), Size(nw, rowH - 4f), CornerRadius(6f), style = Stroke(if (selected) 3f else 1.5f))
                    if (nw > 18f) drawRect(Color.White.copy(alpha = 0.5f), Offset(nx + nw - 6f, y + rowH * 0.3f), Size(2f, rowH * 0.4f))
                }
                rect?.let { r -> drawRect(Palette.Cyan.copy(alpha = 0.15f), r.topLeft, r.size); drawRect(Palette.Cyan, r.topLeft, r.size, style = Stroke(2f)) }
                // Cursor de reproducción (fase de dibujo: no recompone)
                val pos = st.framesToTicks(position.longValue) - clip.start
                if (pos in 0..clip.length) {
                    val x = pos * pxPerTick - offX
                    drawLine(Palette.Playhead, Offset(x, 0f), Offset(x, size.height), 2f)
                }
                if (bar <= 0f) return@Canvas
            }
        }
    }
    LaunchedEffect(clip.id) { selection = emptySet() }
}

// Doble toque (estado compartido del gesto; solo interfaz)
private var lastTapIndex = -1
private var lastTapTime = 0L
