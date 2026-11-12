package com.caucesound.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.awaitTouchSlopOrCancellation
import androidx.compose.foundation.gestures.calculateZoom
import androidx.compose.foundation.gestures.drag
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.input.pointer.PointerEventPass
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextMeasurer
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.WAVE_BLOCK
import com.caucesound.app.data.ClipState
import com.caucesound.app.data.MidiClipState
import com.caucesound.app.data.MusicTime
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.STEP_TICKS
import com.caucesound.app.data.SAMPLE_RATE
import com.caucesound.app.data.TrackState
import kotlin.math.max
import kotlin.math.min

val TRACK_HEIGHT: Dp = 84.dp
val RULER_HEIGHT: Dp = 30.dp

/** Conversión píxel <-> frame del timeline. Pura y probada en test/ (TimelineGeometryTest). */
data class TimelineGeometry(val pxPerSecond: Float) {
    val pxPerFrame: Float get() = pxPerSecond / SAMPLE_RATE
    fun x(frame: Long): Float = (frame.toDouble() * pxPerSecond / SAMPLE_RATE).toFloat()
    // Redondeo en doble precisión: truncar en float perdía 1 frame (detectado por TimelineGeometryTest).
    fun frame(x: Float): Long = Math.round(x.toDouble() * SAMPLE_RATE / pxPerSecond).coerceAtLeast(0)
}

enum class DragMode { MOVE, TRIM_START, TRIM_END }

/** Un clip del arreglo, de audio o MIDI, en frames (para tocar, arrastrar y dibujar con la misma lógica). */
private data class ClipHit(val trackIndex: Int, val id: String, val start: Long, val end: Long, val midi: Boolean, val minStart: Long, val maxEnd: Long)

private data class DragPreview(val clipId: String, val mode: DragMode, val start: Long, val length: Long, val trackIndex: Int)

/**
 * Timeline multipista: regla con bucle, pistas de audio (forma de onda) y de instrumento (clips MIDI y patrones).
 * Gestos: tocar vacío = mover el cursor; tocar clip = seleccionar; doble toque en un clip MIDI = abrir su editor;
 * arrastrar un clip seleccionado = moverlo (también a otra pista del mismo tipo); arrastrar sus bordes = recortar;
 * dos dedos = zoom. Con un clip sin seleccionar, arrastrar desplaza la vista.
 */
@Composable
fun Timeline(
    state: ProjectState,
    waveforms: Map<String, FloatArray>,
    geometry: TimelineGeometry,
    hScroll: ScrollState,
    selectedClip: String?,
    snap: Boolean,
    position: () -> Long,
    onSeek: (Long) -> Unit,
    onSelect: (String?) -> Unit,
    onMove: (clipId: String, start: Long, trackId: String?) -> Unit,
    onTrim: (clipId: String, start: Long, length: Long) -> Unit,
    onMoveMidi: (clipId: String, startTick: Long, trackId: String?) -> Unit = { _, _, _ -> },
    onResizeMidi: (clipId: String, startTick: Long, lengthTick: Long) -> Unit = { _, _, _ -> },
    onOpenClip: (clipId: String) -> Unit = {},
    onZoom: (factor: Float, focusX: Float) -> Unit = { _, _ -> },
) {
    val density = LocalDensity.current
    val trackH = with(density) { TRACK_HEIGHT.toPx() }
    val rulerH = with(density) { RULER_HEIGHT.toPx() }
    val handlePx = with(density) { 22.dp.toPx() }
    val totalFrames = max(state.contentEnd + 60L * SAMPLE_RATE, 180L * SAMPLE_RATE)
    val widthDp = with(density) { geometry.x(totalFrames).toDp() }
    val heightDp = RULER_HEIGHT + TRACK_HEIGHT * state.tracks.size
    val text = rememberTextMeasurer()
    var preview by remember { mutableStateOf<DragPreview?>(null) }
    val st by rememberUpdatedState(state)
    val sel by rememberUpdatedState(selectedClip)
    val geo by rememberUpdatedState(geometry)
    val snapOn by rememberUpdatedState(snap)
    val tap = remember { longArrayOf(0L) }
    val tapId = remember { arrayOf<String?>(null) }

    fun hits(ti: Int, t: TrackState): List<ClipHit> =
        t.clips.map { c ->
            val src = st.sources[c.sourceId]
            ClipHit(ti, c.id, c.start, c.end, false, max(0, c.start - c.sourceOffset), c.start - c.sourceOffset + (src?.frames ?: c.end))
        } + t.midiClips.map { c -> ClipHit(ti, c.id, st.ticksToFrames(c.start), st.ticksToFrames(c.end), true, 0, Long.MAX_VALUE / 4) }

    fun hit(pos: Offset): ClipHit? {
        if (pos.y < rulerH) return null
        val ti = ((pos.y - rulerH) / trackH).toInt()
        val t = st.tracks.getOrNull(ti) ?: return null
        val f = geo.frame(pos.x)
        return hits(ti, t).lastOrNull { f >= it.start && f < it.end }
    }

    fun snapped(f: Long) = if (snapOn) MusicTime.snapToBeat(f, st.bpm) else f

    Box(
        Modifier.pointerInput(Unit) {
            // Zoom con dos dedos (antes que el desplazamiento y la edición).
            awaitEachGesture {
                awaitFirstDown(requireUnconsumed = false)
                do {
                    val ev = awaitPointerEvent(PointerEventPass.Initial)
                    val pressed = ev.changes.filter { it.pressed }
                    if (pressed.size >= 2) {
                        val z = ev.calculateZoom()
                        if (z != 1f) onZoom(z, pressed.map { it.position.x }.average().toFloat())
                        ev.changes.forEach { it.consume() }
                    }
                } while (ev.changes.any { it.pressed })
            }
        }.horizontalScroll(hScroll),
    ) {
        Canvas(
            Modifier.width(widthDp).height(heightDp).pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    val h = hit(down.position)
                    if (h == null || h.id != sel) {
                        // Toque simple: seleccionar o mover el cursor. Si se arrastra, lo gestiona el scroll.
                        val up = waitForUpOrCancellation() ?: return@awaitEachGesture
                        if ((up.position - down.position).getDistance() > viewConfiguration.touchSlop) return@awaitEachGesture
                        if (h != null) {
                            val now = System.currentTimeMillis()
                            if (h.midi && tapId[0] == h.id && now - tap[0] < 350) onOpenClip(h.id)
                            tap[0] = now; tapId[0] = h.id
                            onSelect(h.id)
                        } else { onSelect(null); onSeek(snapped(geo.frame(down.position.x))) }
                        return@awaitEachGesture
                    }
                    val xs = geo.x(h.start)
                    val xe = geo.x(h.end)
                    val mode = when {
                        down.position.x - xs < handlePx && xe - xs > handlePx * 3 -> DragMode.TRIM_START
                        xe - down.position.x < handlePx && xe - xs > handlePx * 3 -> DragMode.TRIM_END
                        else -> DragMode.MOVE
                    }
                    var released = false
                    val first = awaitTouchSlopOrCancellation(down.id) { c, _ -> c.consume() }
                    if (first == null) {
                        // Toque sobre el clip ya seleccionado: doble toque abre el editor.
                        val now = System.currentTimeMillis()
                        if (h.midi && tapId[0] == h.id && now - tap[0] < 350) onOpenClip(h.id)
                        tap[0] = now; tapId[0] = h.id
                        released = true
                    }
                    if (released || first == null) return@awaitEachGesture
                    var total = first.position - down.position
                    val minLen = if (h.midi) st.ticksToFrames(STEP_TICKS.toLong()) else SAMPLE_RATE / 100L
                    val srcKind = st.tracks[h.trackIndex].kind
                    fun update() {
                        val df = (total.x / geo.pxPerFrame).toLong()
                        preview = when (mode) {
                            DragMode.MOVE -> {
                                var ti = (h.trackIndex + Math.round(total.y / trackH)).coerceIn(0, st.tracks.size - 1)
                                if (st.tracks[ti].kind != srcKind) ti = h.trackIndex // solo a pistas del mismo tipo
                                DragPreview(h.id, mode, snapped(h.start + df).coerceAtLeast(0), h.end - h.start, ti)
                            }
                            DragMode.TRIM_START -> {
                                val ns = snapped(h.start + df).coerceIn(h.minStart, h.end - minLen)
                                DragPreview(h.id, mode, ns, h.end - ns, h.trackIndex)
                            }
                            DragMode.TRIM_END -> {
                                val ne = snapped(h.end + df).coerceIn(h.start + minLen, h.maxEnd)
                                DragPreview(h.id, mode, h.start, ne - h.start, h.trackIndex)
                            }
                        }
                    }
                    update()
                    drag(first.id) { c -> total += c.positionChange(); c.consume(); update() }
                    val p = preview
                    preview = null
                    if (p != null) {
                        val newTrack = st.tracks[p.trackIndex].id.takeIf { p.trackIndex != h.trackIndex }
                        if (h.midi) {
                            when (mode) {
                                DragMode.MOVE -> if (p.start != h.start || newTrack != null) onMoveMidi(h.id, st.framesToTicks(p.start), newTrack)
                                else -> if (p.start != h.start || p.length != h.end - h.start) {
                                    val s0 = st.framesToTicks(p.start)
                                    onResizeMidi(h.id, s0, max(STEP_TICKS.toLong(), st.framesToTicks(p.start + p.length) - s0))
                                }
                            }
                        } else when (mode) {
                            DragMode.MOVE -> if (p.start != h.start || newTrack != null) onMove(h.id, p.start, newTrack)
                            else -> if (p.start != h.start || p.length != h.end - h.start) onTrim(h.id, p.start, p.length)
                        }
                    }
                }
            },
        ) {
            val visL = hScroll.value.toFloat()
            val visR = visL + hScroll.viewportSize.toFloat().coerceAtLeast(1f)
            drawGrid(st, geo, rulerH, visL, visR, text)
            st.tracks.forEachIndexed { ti, t ->
                val top = rulerH + ti * trackH
                if (ti % 2 == 1) drawRect(Color.White.copy(alpha = 0.015f), Offset(visL, top), Size(visR - visL, trackH))
                drawLine(Palette.Grid, Offset(visL, top + trackH), Offset(visR, top + trackH), 1f)
                val color = Color(t.color.toInt())
                for (c in t.clips) {
                    val pv = preview?.takeIf { it.clipId == c.id }
                    if (pv != null && pv.trackIndex != ti) continue
                    drawClip(c, pv, color, top, trackH, geo, waveforms[c.sourceId], c.id == sel, visL, visR, text)
                }
                for (c in t.midiClips) {
                    val pv = preview?.takeIf { it.clipId == c.id }
                    if (pv != null && pv.trackIndex != ti) continue
                    drawMidiClip(st, t, c, pv, color, top, trackH, geo, c.id == sel, visL, visR, text)
                }
                preview?.let { pv ->
                    if (pv.trackIndex == ti) {
                        st.clip(pv.clipId)?.let { (owner, c) -> if (owner.id != t.id) drawClip(c, pv, color, top, trackH, geo, waveforms[c.sourceId], true, visL, visR, text) }
                        st.midiClip(pv.clipId)?.let { (owner, c) -> if (owner.id != t.id) drawMidiClip(st, owner, c, pv, color, top, trackH, geo, true, visL, visR, text) }
                    }
                }
            }
        }
        // Cursor de reproducción en su propia capa: se redibuja a 60 fps sin repintar las formas de onda.
        Canvas(Modifier.width(widthDp).height(heightDp)) {
            val x = geo.x(position())
            drawLine(Palette.Playhead, Offset(x, 0f), Offset(x, size.height), 2f)
            drawPath(Path().apply { moveTo(x - 9f, 0f); lineTo(x + 9f, 0f); lineTo(x, 14f); close() }, Palette.Playhead)
        }
    }
}

private fun DrawScope.drawGrid(s: ProjectState, g: TimelineGeometry, rulerH: Float, visL: Float, visR: Float, text: TextMeasurer) {
    drawRect(Palette.Surface, Offset(visL, 0f), Size(visR - visL, rulerH))
    // Bucle
    val lx0 = g.x(s.ticksToFrames(s.loopStart))
    val lx1 = g.x(s.ticksToFrames(s.loopEnd))
    if (lx1 > lx0) {
        if (s.loopEnabled) {
            drawRect(Palette.Cyan.copy(alpha = 0.28f), Offset(lx0, 0f), Size(lx1 - lx0, rulerH))
            drawRect(Palette.Cyan.copy(alpha = 0.05f), Offset(lx0, rulerH), Size(lx1 - lx0, size.height - rulerH))
            drawLine(Palette.Cyan, Offset(lx0, 0f), Offset(lx0, size.height), 1.5f)
            drawLine(Palette.Cyan, Offset(lx1, 0f), Offset(lx1, size.height), 1.5f)
        } else drawRect(Palette.TextFaint.copy(alpha = 0.25f), Offset(lx0, rulerH - 5f), Size(lx1 - lx0, 5f))
    }
    val fpb = MusicTime.framesPerBeat(s.bpm)
    val pxPerBeat = (fpb * g.pxPerFrame).toFloat()
    val barEvery = when { pxPerBeat * s.beatsPerBar > 60 -> 1; pxPerBeat * s.beatsPerBar > 25 -> 2; pxPerBeat * s.beatsPerBar > 10 -> 4; else -> 8 }
    val firstBeat = max(0L, (visL / pxPerBeat).toLong() - 1)
    val lastBeat = (visR / pxPerBeat).toLong() + 1
    for (b in firstBeat..lastBeat) {
        val x = b * pxPerBeat
        val isBar = b % s.beatsPerBar == 0L
        val bar = b / s.beatsPerBar
        if (isBar) {
            val labeled = bar % barEvery == 0L
            drawLine(if (labeled) Palette.GridStrong else Palette.Grid, Offset(x, if (labeled) 0f else rulerH * 0.5f), Offset(x, size.height), 1f)
            if (labeled) drawText(text, "${bar + 1}", Offset(x + 5f, 6f), TextStyle(color = Palette.TextDim, fontSize = 11.sp))
        } else if (pxPerBeat > 12) {
            drawLine(Palette.Grid.copy(alpha = 0.5f), Offset(x, rulerH * 0.7f), Offset(x, size.height), 1f)
        }
    }
}

private fun DrawScope.drawMidiClip(
    s: ProjectState, t: TrackState, c: MidiClipState, pv: DragPreview?, color: Color, top: Float, trackH: Float, g: TimelineGeometry,
    selected: Boolean, visL: Float, visR: Float, text: TextMeasurer,
) {
    val start = pv?.start ?: s.ticksToFrames(c.start)
    val length = pv?.length ?: (s.ticksToFrames(c.end) - s.ticksToFrames(c.start))
    val x0 = g.x(start)
    val x1 = g.x(start + length)
    if (x1 < visL || x0 > visR) return
    val y0 = top + 4f
    val h = trackH - 8f
    drawRoundRect(color.copy(alpha = 0.22f), Offset(x0, y0), Size(x1 - x0, h), CornerRadius(10f))
    drawRoundRect(color.copy(alpha = 0.6f), Offset(x0, y0), Size(x1 - x0, 18f * 1.2f), CornerRadius(10f))
    clipRect(max(x0, visL), y0, min(x1, visR), y0 + h) {
        val bodyTop = y0 + 24f
        val bodyH = h - 28f
        val pxPerTick = (g.x(s.ticksToFrames(c.start) + 1_000_000) - g.x(s.ticksToFrames(c.start))) / s.framesToTicks(1_000_000).coerceAtLeast(1)
        // El contenido se dibuja desde el inicio del clip (al recortar por la izquierda se ve el efecto al soltar).
        if (c.isPattern) {
            val p = t.pattern(c.patternId)
            if (p != null) {
                val patTicks = p.steps.toLong() * STEP_TICKS
                val rowH = bodyH / 8f
                var rep = 0L
                while (rep < c.length) {
                    for (l in 0 until 8) for (st in 0 until p.steps) {
                        if (p.lanes[l][st] <= 0) continue
                        val tk = rep + st * STEP_TICKS.toLong()
                        if (tk >= c.length) continue
                        val x = x0 + tk * pxPerTick
                        if (x < visL - 10 || x > visR + 10) continue
                        drawRect(LANE_COLORS[l].copy(alpha = 0.9f), Offset(x, bodyTop + l * rowH), Size(max(2f, STEP_TICKS * pxPerTick * 0.7f), max(2f, rowH - 1f)))
                    }
                    if (rep > 0) drawLine(Color.White.copy(alpha = 0.25f), Offset(x0 + rep * pxPerTick, bodyTop), Offset(x0 + rep * pxPerTick, bodyTop + bodyH), 1f)
                    rep += patTicks
                }
            }
        } else if (c.notes.isNotEmpty()) {
            val lo = c.notes.minOf { it.key }
            val hi = c.notes.maxOf { it.key }
            val range = max(12, hi - lo + 1)
            val rowH = bodyH / range
            for (n in c.notes) {
                if (n.start >= c.length) continue
                val x = x0 + n.start * pxPerTick
                val w = max(2f, min(n.length, c.length - n.start) * pxPerTick)
                val y = bodyTop + (hi - n.key) * rowH + (range - (hi - lo + 1)) * rowH / 2
                drawRoundRect(Color.White.copy(alpha = 0.85f), Offset(x, y), Size(w, max(2f, rowH - 1f)), CornerRadius(2f))
            }
        }
        val label = if (c.isPattern) "▦ ${t.pattern(c.patternId)?.name ?: c.name}" else "♪ ${c.name}"
        drawText(text, label, Offset(max(x0, visL) + 8f, y0 + 2f), TextStyle(color = Color.White, fontSize = 11.sp), maxLines = 1)
    }
    if (selected) {
        drawRoundRect(Color.White, Offset(x0, y0), Size(x1 - x0, h), CornerRadius(10f), style = Stroke(3f))
        drawRoundRect(Color.White, Offset(x0 + 2f, y0 + h / 2 - 18f), Size(6f, 36f), CornerRadius(3f))
        drawRoundRect(Color.White, Offset(x1 - 8f, y0 + h / 2 - 18f), Size(6f, 36f), CornerRadius(3f))
    }
}

private fun DrawScope.drawClip(
    c: ClipState, pv: DragPreview?, color: Color, top: Float, trackH: Float, g: TimelineGeometry,
    wave: FloatArray?, selected: Boolean, visL: Float, visR: Float, text: TextMeasurer,
) {
    val start = pv?.start ?: c.start
    val length = pv?.length ?: c.length
    // Al recortar por la izquierda, el contenido no se desplaza: el offset de la fuente cambia con el inicio.
    val srcOffset = if (pv?.mode == DragMode.TRIM_START) c.sourceOffset + (pv.start - c.start) else c.sourceOffset
    val x0 = g.x(start)
    val x1 = g.x(start + length)
    if (x1 < visL || x0 > visR) return
    val y0 = top + 4f
    val h = trackH - 8f
    drawRoundRect(color.copy(alpha = if (c.missing) 0.12f else 0.24f), Offset(x0, y0), Size(x1 - x0, h), CornerRadius(10f))
    clipRect(max(x0, visL), y0, min(x1, visR), y0 + h) {
        if (wave != null && !c.missing) {
            val mid = y0 + h / 2 + 6f
            val amp = (h / 2 - 8f) * 0.95f
            val step = 2f
            var x = max(x0, visL)
            val buckets = wave.size / 2
            while (x < min(x1, visR)) {
                val f0 = srcOffset + ((x - x0) / g.pxPerFrame).toLong()
                val f1 = srcOffset + ((x + step - x0) / g.pxPerFrame).toLong()
                var b0 = (f0 / WAVE_BLOCK).toInt().coerceIn(0, buckets - 1)
                val b1 = (f1 / WAVE_BLOCK).toInt().coerceIn(b0, buckets - 1)
                var mn = 0f
                var mx = 0f
                while (b0 <= b1) { mn = min(mn, wave[b0 * 2]); mx = max(mx, wave[b0 * 2 + 1]); b0++ }
                drawLine(color, Offset(x, mid - mx * amp), Offset(x, mid - mn * amp + 0.5f), step)
                x += step
            }
        }
        // Fundidos
        val shade = Color.Black.copy(alpha = 0.35f)
        if (c.fadeIn > 0) {
            val fx = g.x(start + c.fadeIn)
            drawPath(Path().apply { moveTo(x0, y0); lineTo(fx, y0); lineTo(x0, y0 + h); close() }, shade)
            drawLine(Color.White.copy(alpha = 0.7f), Offset(x0, y0 + h), Offset(fx, y0), 2f)
        }
        if (c.fadeOut > 0) {
            val fx = g.x(start + length - c.fadeOut)
            drawPath(Path().apply { moveTo(x1, y0); lineTo(fx, y0); lineTo(x1, y0 + h); close() }, shade)
            drawLine(Color.White.copy(alpha = 0.7f), Offset(fx, y0), Offset(x1, y0 + h), 2f)
        }
        val label = if (c.missing) "⚠ ${c.name}" else c.name
        drawText(text, label, Offset(max(x0, visL) + 8f, y0 + 4f), TextStyle(color = if (c.missing) Palette.Record else Palette.Text, fontSize = 11.sp), maxLines = 1)
    }
    if (selected) {
        drawRoundRect(Color.White, Offset(x0, y0), Size(x1 - x0, h), CornerRadius(10f), style = Stroke(3f))
        drawRoundRect(Color.White, Offset(x0 + 2f, y0 + h / 2 - 18f), Size(6f, 36f), CornerRadius(3f))
        drawRoundRect(Color.White, Offset(x1 - 8f, y0 + h / 2 - 18f), Size(6f, 36f), CornerRadius(3f))
    }
}
