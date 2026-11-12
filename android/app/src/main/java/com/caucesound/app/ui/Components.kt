package com.caucesound.app.ui

import androidx.compose.ui.graphics.drawscope.clipRect

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.drag
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.log10
import kotlin.math.max
import kotlin.math.min

/** Logotipo: cinco barras de onda con el degradado de marca + nombre. Dibujado en código (sin recursos externos). */
@Composable
fun CauceLogo(modifier: Modifier = Modifier, height: Dp = 28.dp, showText: Boolean = true) {
    Row(modifier, verticalAlignment = Alignment.CenterVertically) {
        Canvas(Modifier.size(height * 1.1f, height)) {
            val bars = floatArrayOf(0.35f, 0.7f, 1f, 0.6f, 0.3f)
            val w = size.width / (bars.size * 2 - 1)
            bars.forEachIndexed { i, h ->
                val bh = size.height * h
                drawRoundRect(
                    Palette.Brand, Offset(i * 2 * w, (size.height - bh) / 2), Size(w, bh),
                    androidx.compose.ui.geometry.CornerRadius(w / 2),
                )
            }
        }
        if (showText) {
            Spacer(Modifier.width(10.dp))
            Text("CAUCE", fontWeight = FontWeight.Black, fontSize = (height.value * 0.62f).sp, letterSpacing = 2.sp, color = Palette.Text)
            Text("SOUND", fontWeight = FontWeight.Light, fontSize = (height.value * 0.62f).sp, letterSpacing = 2.sp,
                style = TextStyle(brush = Palette.BrandHorizontal))
        }
    }
}

/** Botón circular grande (transporte). */
@Composable
fun RoundButton(
    icon: ImageVector, description: String, onClick: () -> Unit, modifier: Modifier = Modifier,
    size: Dp = 52.dp, background: Color = Palette.SurfaceTop, tint: Color = Palette.Text, enabled: Boolean = true,
    glow: Boolean = false,
) {
    val bg by animateColorAsState(if (enabled) background else background.copy(alpha = 0.4f), tween(150), label = "bg")
    Box(
        modifier.size(size).clip(CircleShape)
            .background(if (glow) Brush.radialGradient(listOf(bg, bg.copy(alpha = 0.75f))) else Brush.linearGradient(listOf(bg, bg)))
            .clickable(enabled = enabled, onClick = onClick)
            .semantics { contentDescription = description },
        contentAlignment = Alignment.Center,
    ) { Icon(icon, null, tint = if (enabled) tint else tint.copy(alpha = 0.4f), modifier = Modifier.size(size * 0.5f)) }
}

/** Interruptor tipo píldora (mute, solo, armar, modos). Altura mínima táctil de 36-48 dp. */
@Composable
fun PillToggle(
    text: String, on: Boolean, onClick: () -> Unit, modifier: Modifier = Modifier, color: Color = Palette.Violet,
    minWidth: Dp = 40.dp, height: Dp = 36.dp, icon: ImageVector? = null,
) {
    val bg by animateColorAsState(if (on) color else Palette.SurfaceTop, tween(120), label = "pill")
    Row(
        modifier.defaultMinSize(minWidth = minWidth, minHeight = height).clip(RoundedCornerShape(height / 2)).background(bg)
            .clickable(onClick = onClick).padding(horizontal = 10.dp).semantics { contentDescription = text },
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.Center,
    ) {
        if (icon != null) { Icon(icon, null, Modifier.size(18.dp), tint = if (on) Color.Black else Palette.TextDim); if (text.isNotEmpty()) Spacer(Modifier.width(6.dp)) }
        if (text.isNotEmpty()) Text(text, fontSize = 13.sp, fontWeight = FontWeight.Bold, color = if (on) Color.Black else Palette.TextDim, maxLines = 1)
    }
}

/** Selector segmentado (p. ej. rejilla 1/8 · 1/16 · 1/32). */
@Composable
fun Segmented(options: List<String>, selected: Int, onSelect: (Int) -> Unit, modifier: Modifier = Modifier, height: Dp = 36.dp) {
    Row(modifier.clip(RoundedCornerShape(height / 2)).background(Palette.SurfaceHigh).padding(3.dp)) {
        options.forEachIndexed { i, o ->
            val on = i == selected
            val bg by animateColorAsState(if (on) Palette.SurfaceTop else Color.Transparent, tween(150), label = "seg")
            Box(
                Modifier.height(height - 6.dp).clip(RoundedCornerShape(height / 2)).background(bg).clickable { onSelect(i) }.padding(horizontal = 12.dp),
                contentAlignment = Alignment.Center,
            ) { Text(o, fontSize = 13.sp, fontWeight = if (on) FontWeight.Bold else FontWeight.Medium, color = if (on) Palette.Text else Palette.TextDim, maxLines = 1) }
        }
    }
}

/** Tarjeta de sección con título discreto. */
@Composable
fun SectionCard(title: String, modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.Radius)).background(Palette.Surface).border(1.dp, Palette.Grid, RoundedCornerShape(Dims.Radius)).padding(12.dp)) {
        Text(title.uppercase(), style = MaterialTheme.typography.labelSmall, color = Palette.TextFaint)
        Spacer(Modifier.height(8.dp))
        content()
    }
}

/**
 * Perilla giratoria: arrastre vertical u horizontal para cambiar el valor, doble toque para volver al valor por
 * defecto. log = escala logarítmica (frecuencias, tiempos). onChange en vivo; onEnd al soltar.
 */
@Composable
fun Knob(
    label: String, value: Float, min: Float, max: Float, default: Float, onChange: (Float) -> Unit, onEnd: () -> Unit = {},
    modifier: Modifier = Modifier, size: Dp = 56.dp, log: Boolean = false, color: Color = Palette.Cyan,
    format: (Float) -> String = { if (it >= 100) "%.0f".format(it) else "%.2f".format(it) },
) {
    fun toNorm(v: Float): Float = if (log && min > 0) (kotlin.math.ln(v / min) / kotlin.math.ln(max / min)).coerceIn(0f, 1f) else ((v - min) / (max - min)).coerceIn(0f, 1f)
    fun fromNorm(n: Float): Float = if (log && min > 0) (min * Math.pow((max / min).toDouble(), n.toDouble())).toFloat() else min + (max - min) * n
    var norm by remember(value, min, max) { mutableFloatStateOf(toNorm(value)) }
    val change by rememberUpdatedState(onChange)
    val end by rememberUpdatedState(onEnd)
    Column(modifier.width(size + 16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Canvas(
            Modifier.size(size).semantics { contentDescription = "$label ${format(fromNorm(norm))}" }
                .pointerInput(min, max, default) {
                    detectTapGestures(onDoubleTap = { norm = toNorm(default); change(default); end() })
                }
                .pointerInput(min, max) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        var acc = norm
                        drag(down.id) { c ->
                            val d = c.positionChange()
                            acc = (acc + (d.x - d.y) / (this.size.height * 3.5f)).coerceIn(0f, 1f)
                            norm = acc
                            change(fromNorm(acc))
                            c.consume()
                        }
                        end()
                    }
                },
        ) {
            val stroke = this.size.minDimension * 0.1f
            val r = this.size.minDimension / 2 - stroke
            val start = 135f
            val sweep = 270f
            drawArc(Palette.SurfaceTop, start, sweep, false, Offset(stroke, stroke), Size(r * 2, r * 2), style = Stroke(stroke, cap = StrokeCap.Round))
            drawArc(color, start, sweep * norm, false, Offset(stroke, stroke), Size(r * 2, r * 2), style = Stroke(stroke, cap = StrokeCap.Round))
            val a = Math.toRadians((start + sweep * norm).toDouble())
            val c = Offset(this.size.width / 2, this.size.height / 2)
            drawCircle(Palette.SurfaceHigh, r * 0.68f, c)
            drawLine(Palette.Text, c + Offset((kotlin.math.cos(a) * r * 0.2f).toFloat(), (kotlin.math.sin(a) * r * 0.2f).toFloat()),
                c + Offset((kotlin.math.cos(a) * r * 0.62f).toFloat(), (kotlin.math.sin(a) * r * 0.62f).toFloat()), stroke * 0.7f, cap = StrokeCap.Round)
        }
        Text(label, fontSize = 11.sp, color = Palette.TextDim, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(format(fromNorm(norm)), fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = Palette.Text, maxLines = 1)
    }
}

/**
 * Medidor de pico. 'level' se lee en la fase de dibujo: el medidor se repinta a la frecuencia de la pantalla sin
 * recomponer la interfaz. Caída suave solo visual (no afecta al audio).
 */
@Composable
fun LevelMeter(level: () -> Float, modifier: Modifier = Modifier, vertical: Boolean = true) {
    val hold = remember { FloatArray(1) }
    Canvas(modifier.clip(RoundedCornerShape(3.dp)).background(Palette.Background)) {
        val l = level()
        val db = if (l > 1e-5f) 20 * log10(l) else -100f
        val target = ((db + 60f) / 60f).coerceIn(0f, 1f)
        hold[0] = max(target, hold[0] - 0.025f)
        val shown = hold[0]
        val grad = if (vertical) Brush.verticalGradient(listOf(Palette.Record, Palette.Solo, Palette.Cyan, Palette.Blue))
                   else Brush.horizontalGradient(listOf(Palette.Blue, Palette.Cyan, Palette.Solo, Palette.Record))
        if (vertical) {
            val h = size.height * shown
            clipRectSafe(0f, size.height - h, size.width, size.height) { drawRect(grad) }
        } else {
            clipRectSafe(0f, 0f, size.width * shown, size.height) { drawRect(grad) }
        }
    }
}

private fun androidx.compose.ui.graphics.drawscope.DrawScope.clipRectSafe(l: Float, t: Float, r: Float, b: Float, block: androidx.compose.ui.graphics.drawscope.DrawScope.() -> Unit) {
    if (r > l && b > t) clipRect(l, t, r, b) { block() }
}

/** Fader vertical de volumen en dB (-60..+6). */
@Composable
fun VerticalFader(db: Float, onChange: (Float) -> Unit, onEnd: () -> Unit, modifier: Modifier = Modifier, color: Color = Palette.Violet) {
    fun norm(d: Float) = ((d.coerceIn(-60f, 6f) + 60f) / 66f).let { it * it }.let { kotlin.math.sqrt(it) }
    var n by remember(db) { mutableFloatStateOf(norm(db)) }
    val change by rememberUpdatedState(onChange)
    val end by rememberUpdatedState(onEnd)
    Canvas(
        modifier.semantics { contentDescription = "Fader %.1f dB".format(-60f + n * 66f) }
            .pointerInput(Unit) { detectTapGestures(onDoubleTap = { n = norm(0f); change(0f); end() }) }
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    var acc = n
                    drag(down.id) { c ->
                        acc = (acc - c.positionChange().y / (size.height * 1.2f)).coerceIn(0f, 1f)
                        n = acc
                        change(-60f + acc * 66f)
                        c.consume()
                    }
                    end()
                }
            },
    ) {
        val cx = size.width / 2
        val top = 10f
        val bottom = size.height - 10f
        drawLine(Palette.SurfaceTop, Offset(cx, top), Offset(cx, bottom), 6f, cap = StrokeCap.Round)
        val y = bottom - (bottom - top) * n
        drawLine(color, Offset(cx, y), Offset(cx, bottom), 6f, cap = StrokeCap.Round)
        val zeroY = bottom - (bottom - top) * norm(0f)
        drawLine(Palette.TextFaint, Offset(cx - 14f, zeroY), Offset(cx + 14f, zeroY), 2f)
        drawRoundRect(Palette.Text, Offset(cx - size.width * 0.34f, y - 12f), Size(size.width * 0.68f, 24f), androidx.compose.ui.geometry.CornerRadius(6f))
        drawLine(Palette.Background, Offset(cx - size.width * 0.22f, y), Offset(cx + size.width * 0.22f, y), 2f)
    }
}

fun dbLabel(db: Float): String = if (db <= -59.9f) "-∞" else "%+.1f".format(db)
fun Float.clampDb() = max(-60f, min(6f, this))
