package com.caucesound.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.ScrollState
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.awaitTouchSlopOrCancellation
import androidx.compose.foundation.gestures.drag
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxHeight
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
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.WAVE_BLOCK
import com.caucesound.app.data.ClipState
import com.caucesound.app.data.MusicTime
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.SAMPLE_RATE
import kotlin.math.abs
import kotlin.math.max
import kotlin.math.min

val TRACK_HEIGHT: Dp = 92.dp
val RULER_HEIGHT: Dp = 28.dp

/** Conversión píxel <-> frame del timeline. Pura y probada en test/ (TimelineGeometryTest). */
data class TimelineGeometry(val pxPerSecond: Float) {
    val pxPerFrame: Float get() = pxPerSecond / SAMPLE_RATE
    fun x(frame: Long): Float = (frame.toDouble() * pxPerSecond / SAMPLE_RATE).toFloat()
    // Redondeo en doble precisión: truncar en float perdía 1 frame (detectado por TimelineGeometryTest).
    fun frame(x: Float): Long = Math.round(x.toDouble() * SAMPLE_RATE / pxPerSecond).coerceAtLeast(0)
}

enum class DragMode { MOVE, TRIM_START, TRIM_END }

private data class DragPreview(val clipId: String, val mode: DragMode, val start: Long, val length: Long, val trackIndex: Int)

/**
 * Timeline multipista: regla, pistas, clips con forma de onda y fundidos.
 * Gestos: tocar vacío = mover el cursor; tocar clip = seleccionar; arrastrar un clip seleccionado = moverlo
 * (también a otra pista); arrastrar sus bordes = recortar. Con un clip sin seleccionar, arrastrar desplaza la vista.
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

    fun hit(pos: Offset): Pair<Int, ClipState>? {
        if (pos.y < rulerH) return null
        val ti = ((pos.y - rulerH) / trackH).toInt()
        val t = st.tracks.getOrNull(ti) ?: return null
        val f = geo.frame(pos.x)
        return t.clips.lastOrNull { f >= it.start && f < it.end }?.let { ti to it }
    }

    fun snapped(f: Long) = if (snapOn) MusicTime.snapToBeat(f, st.bpm) else f

    Box(Modifier.horizontalScroll(hScroll)) {
        Canvas(
            Modifier.width(widthDp).height(heightDp).pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown(requireUnconsumed = false)
                    val h = hit(down.position)
                    if (h == null || h.second.id != sel) {
                        // Toque simple: seleccionar o mover el cursor. Si se arrastra, lo gestiona el scroll.
                        val up = waitForUpOrCancellation() ?: return@awaitEachGesture
                        if ((up.position - down.position).getDistance() > viewConfiguration.touchSlop) return@awaitEachGesture
                        if (h != null) onSelect(h.second.id)
                        else { onSelect(null); onSeek(snapped(geo.frame(down.position.x))) }
                        return@awaitEachGesture
                    }
                    val (trackIndex, clip) = h
                    val xs = geo.x(clip.start)
                    val xe = geo.x(clip.end)
                    val mode = when {
                        down.position.x - xs < handlePx && xe - xs > handlePx * 3 -> DragMode.TRIM_START
                        xe - down.position.x < handlePx && xe - xs > handlePx * 3 -> DragMode.TRIM_END
                        else -> DragMode.MOVE
                    }
                    val first = awaitTouchSlopOrCancellation(down.id) { c, _ -> c.consume() } ?: return@awaitEachGesture
                    var total = first.position - down.position
                    val src = st.sources[clip.sourceId]
                    val maxEnd = clip.start - clip.sourceOffset + (src?.frames ?: clip.end)
                    val minLen = SAMPLE_RATE / 100L
                    fun update() {
                        val df = (total.x / geo.pxPerFrame).toLong()
                        preview = when (mode) {
                            DragMode.MOVE -> {
                                val ti = (trackIndex + Math.round(total.y / trackH)).coerceIn(0, st.tracks.size - 1)
                                DragPreview(clip.id, mode, snapped(clip.start + df).coerceAtLeast(0), clip.length, ti)
                            }
                            DragMode.TRIM_START -> {
                                val ns = snapped(clip.start + df).coerceIn(max(0, clip.start - clip.sourceOffset), clip.end - minLen)
                                DragPreview(clip.id, mode, ns, clip.end - ns, trackIndex)
                            }
                            DragMode.TRIM_END -> {
                                val ne = snapped(clip.end + df).coerceIn(clip.start + minLen, maxEnd)
                                DragPreview(clip.id, mode, clip.start, ne - clip.start, trackIndex)
                            }
                        }
                    }
                    update()
                    drag(first.id) { c -> total += c.positionChange(); c.consume(); update() }
                    val p = preview
                    preview = null
                    if (p != null) when (mode) {
                        DragMode.MOVE -> if (p.start != clip.start || p.trackIndex != trackIndex)
                            onMove(clip.id, p.start, st.tracks[p.trackIndex].id.takeIf { p.trackIndex != trackIndex })
                        else -> if (p.start != clip.start || p.length != clip.length) onTrim(clip.id, p.start, p.length)
                    }
                }
            },
        ) {
            val visL = hScroll.value.toFloat()
            val visR = visL + hScroll.viewportSize.toFloat().coerceAtLeast(1f)
            drawGrid(st, geo, rulerH, visL, visR, text)
            st.tracks.forEachIndexed { ti, t ->
                val top = rulerH + ti * trackH
                drawLine(Palette.Grid, Offset(visL, top + trackH), Offset(visR, top + trackH), 1f)
                for (c in t.clips) {
                    val pv = preview?.takeIf { it.clipId == c.id }
                    if (pv != null && pv.trackIndex != ti) continue
                    drawClip(c, pv, Color(t.color.toInt()), top, trackH, geo, waveforms[c.sourceId], c.id == sel, visL, visR, text)
                }
                preview?.let { pv ->
                    if (pv.trackIndex == ti) {
                        val c = st.clip(pv.clipId)?.second
                        if (c != null && t.clips.none { it.id == c.id }) drawClip(c, pv, Color(t.color.toInt()), top, trackH, geo, waveforms[c.sourceId], true, visL, visR, text)
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

private fun DrawScope.drawGrid(s: ProjectState, g: TimelineGeometry, rulerH: Float, visL: Float, visR: Float, text: androidx.compose.ui.text.TextMeasurer) {
    drawRect(Palette.Surface, Offset(visL, 0f), Size(visR - visL, rulerH))
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
            if (labeled) drawText(text, "${bar + 1}", Offset(x + 4f, 4f), TextStyle(color = Palette.TextDim, fontSize = 11.sp))
        } else if (pxPerBeat > 12) {
            drawLine(Palette.Grid.copy(alpha = 0.5f), Offset(x, rulerH * 0.7f), Offset(x, size.height), 1f)
        }
    }
}

private fun DrawScope.drawClip(
    c: ClipState, pv: DragPreview?, color: Color, top: Float, trackH: Float, g: TimelineGeometry,
    wave: FloatArray?, selected: Boolean, visL: Float, visR: Float, text: androidx.compose.ui.text.TextMeasurer,
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
    drawRoundRect(color.copy(alpha = if (c.missing) 0.15f else 0.28f), Offset(x0, y0), Size(x1 - x0, h), CornerRadius(8f))
    clipRect(max(x0, visL), y0, min(x1, visR), y0 + h) {
        if (wave != null && !c.missing) {
            val mid = y0 + h / 2
            val amp = h / 2 * 0.92f
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
        drawRoundRect(Color.White, Offset(x0, y0), Size(x1 - x0, h), CornerRadius(8f), style = Stroke(3f))
        // asas de recorte
        drawRoundRect(Color.White, Offset(x0 + 2f, y0 + h / 2 - 18f), Size(6f, 36f), CornerRadius(3f))
        drawRoundRect(Color.White, Offset(x1 - 8f, y0 + h / 2 - 18f), Size(6f, 36f), CornerRadius(3f))
    }
    if (abs(x1 - x0) < 1f) return
}
