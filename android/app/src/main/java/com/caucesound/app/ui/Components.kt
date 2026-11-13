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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material3.AlertDialog
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.Stable
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.semantics.Role
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
import androidx.compose.ui.graphics.luminance
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
    val onText = if (color.luminance() < 0.3f) Color.White else Color(0xFF0A0B0E)
    Row(
        modifier.defaultMinSize(minWidth = minWidth, minHeight = height).clip(RoundedCornerShape(height / 2)).background(bg)
            .clickable(onClick = onClick).padding(horizontal = 10.dp).semantics { contentDescription = text },
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.Center,
    ) {
        if (icon != null) { Icon(icon, null, Modifier.size(18.dp), tint = if (on) onText else Palette.TextDim); if (text.isNotEmpty()) Spacer(Modifier.width(6.dp)) }
        if (text.isNotEmpty()) Text(text, fontSize = 13.sp, fontWeight = FontWeight.Bold, color = if (on) onText else Palette.TextDim, maxLines = 1)
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
 * Valor de un control arrastrable (perilla o fader) sincronizado con el valor real.
 *
 * Mientras el dedo está abajo manda el valor local. Al soltar se sigue mostrando el valor local hasta que el valor
 * externo cambia (llega el estado del motor); desde ese momento manda siempre el externo. Así el control no salta
 * al soltar y, tras un deshacer o un cambio de preset, muestra el valor del motor y el siguiente arrastre parte de
 * él (antes partía del último valor arrastrado: el gesto leía un estado antiguo).
 */
@Stable
class DragValue internal constructor() {
    internal var local by mutableStateOf<Float?>(null)
    internal var base by mutableFloatStateOf(0f)
    internal var dragging by mutableStateOf(false)
    internal var external = 0f

    /** Valor normalizado que se debe mostrar ahora. */
    fun shown(externalNorm: Float): Float {
        external = externalNorm
        val l = local
        return if (l != null && (dragging || externalNorm == base)) l else externalNorm
    }
    internal fun begin(): Float { val start = shown(external); base = external; local = start; dragging = true; return start }
    internal fun set(v: Float) { local = v }
    internal fun end() { dragging = false }
    /** Tras recomponer: si el valor externo ya cambió, el local deja de mandar para siempre. */
    internal fun settle(externalNorm: Float) { if (!dragging && local != null && externalNorm != base) local = null }
}

@Composable
fun rememberDragValue(): DragValue = remember { DragValue() }

/**
 * Perilla giratoria: arrastre vertical u horizontal para cambiar el valor (recorrido completo en 180 dp, igual para
 * todos los tamaños), doble toque para volver al valor por defecto. log = escala logarítmica (frecuencias,
 * tiempos). onChange en vivo; onEnd al soltar. La zona táctil es de al menos 48 dp aunque el dibujo sea menor.
 */
@Composable
fun Knob(
    label: String, value: Float, min: Float, max: Float, default: Float, onChange: (Float) -> Unit, onEnd: () -> Unit = {},
    modifier: Modifier = Modifier, size: Dp = 52.dp, log: Boolean = false, color: Color = Palette.Violet,
    format: (Float) -> String = { if (it >= 100) "%.0f".format(it) else "%.2f".format(it) },
) {
    fun toNorm(v: Float): Float = if (log && min > 0) (kotlin.math.ln(v.coerceAtLeast(min) / min) / kotlin.math.ln(max / min)).coerceIn(0f, 1f) else ((v - min) / (max - min)).coerceIn(0f, 1f)
    fun fromNorm(n: Float): Float = if (log && min > 0) (min * Math.pow((max / min).toDouble(), n.toDouble())).toFloat() else min + (max - min) * n
    val drag = rememberDragValue()
    val ext = toNorm(value)
    val norm = drag.shown(ext)
    SideEffect { drag.settle(ext) }
    val change by rememberUpdatedState(onChange)
    val end by rememberUpdatedState(onEnd)
    val active by animateFloatAsState(if (drag.dragging) 1f else 0f, tween(120), label = "knob")
    val touch = if (size < Dims.Touch) Dims.Touch else size
    val knobSize = size
    Column(modifier.width(touch + 16.dp), horizontalAlignment = Alignment.CenterHorizontally) {
        Canvas(
            Modifier.size(touch).semantics { contentDescription = "$label ${format(fromNorm(norm))}" }
                .pointerInput(min, max, default, log) {
                    detectTapGestures(onDoubleTap = { val d = toNorm(default); drag.begin(); drag.set(d); change(default); drag.end(); end() })
                }
                .pointerInput(min, max, log) {
                    val travel = 180.dp.toPx()
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        var acc = drag.begin()
                        drag(down.id) { c ->
                            val d = c.positionChange()
                            acc = (acc + (d.x - d.y) / travel).coerceIn(0f, 1f)
                            drag.set(acc)
                            change(fromNorm(acc))
                            c.consume()
                        }
                        drag.end()
                        end()
                    }
                },
        ) {
            val d = knobSize.toPx()
            val stroke = d * 0.085f
            val r = d / 2 - stroke
            val c = Offset(this.size.width / 2, this.size.height / 2)
            val tl = Offset(c.x - r, c.y - r)
            val start = 135f
            val sweep = 270f
            drawArc(Palette.SurfaceTop, start, sweep, false, tl, Size(r * 2, r * 2), style = Stroke(stroke, cap = StrokeCap.Round))
            if (norm > 0.001f) drawArc(color, start, sweep * norm, false, tl, Size(r * 2, r * 2), style = Stroke(stroke, cap = StrokeCap.Round))
            // Cuerpo: disco con un degradado suave y un anillo que se ilumina mientras se arrastra.
            val body = r * 0.72f
            drawCircle(Brush.verticalGradient(listOf(Palette.SurfaceTop, Palette.SurfaceHigh), c.y - body, c.y + body), body, c)
            drawCircle(color.copy(alpha = 0.15f + 0.45f * active), body, c, style = Stroke(1.dp.toPx()))
            val a = Math.toRadians((start + sweep * norm).toDouble())
            val dir = Offset(kotlin.math.cos(a).toFloat(), kotlin.math.sin(a).toFloat())
            drawLine(Palette.Text, c + dir * (body * 0.35f), c + dir * (body * 0.86f), stroke * 0.75f, cap = StrokeCap.Round)
        }
        Text(label, style = MaterialTheme.typography.labelSmall, color = Palette.TextDim, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(format(fromNorm(norm)), style = ValueText, color = if (drag.dragging) color else Palette.Text, maxLines = 1)
    }
}

/** Números que cambian (valores de perillas, dB): monoespaciado compacto, sin el espaciado ancho de las etiquetas. */
val ValueText = TextStyle(fontFamily = FontFamily.Monospace, fontSize = 11.sp, letterSpacing = (-0.3).sp, fontWeight = FontWeight.Medium)

/**
 * Medidor de pico. 'level' se lee en la fase de dibujo: el medidor se repinta a la frecuencia de la pantalla sin
 * recomponer la interfaz. Caída suave solo visual (no afecta al audio). Escala de -60 a 0 dB; el tramo rojo empieza
 * en -3 dB.
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
        val stops = arrayOf(0f to Palette.Blue, 0.55f to Palette.Cyan, 0.85f to Palette.Solo, 0.95f to Palette.Record, 1f to Palette.Record)
        if (vertical) {
            val grad = Brush.verticalGradient(*stops, startY = size.height, endY = 0f)
            val h = size.height * shown
            clipRectSafe(0f, size.height - h, size.width, size.height) { drawRect(grad) }
        } else {
            val grad = Brush.horizontalGradient(*stops)
            clipRectSafe(0f, 0f, size.width * shown, size.height) { drawRect(grad) }
        }
    }
}

private fun androidx.compose.ui.graphics.drawscope.DrawScope.clipRectSafe(l: Float, t: Float, r: Float, b: Float, block: androidx.compose.ui.graphics.drawscope.DrawScope.() -> Unit) {
    if (r > l && b > t) clipRect(l, t, r, b) { block() }
}

/** Fader vertical de volumen en dB (-60..+6), con marcas en 0, -12, -24 y -48 dB. Doble toque = 0 dB. */
@Composable
fun VerticalFader(db: Float, onChange: (Float) -> Unit, onEnd: () -> Unit, modifier: Modifier = Modifier, color: Color = Palette.Violet) {
    fun norm(d: Float) = (d.coerceIn(-60f, 6f) + 60f) / 66f
    val drag = rememberDragValue()
    val ext = norm(db)
    val n = drag.shown(ext)
    SideEffect { drag.settle(ext) }
    val change by rememberUpdatedState(onChange)
    val end by rememberUpdatedState(onEnd)
    Canvas(
        modifier.semantics { contentDescription = "Fader %.1f dB".format(-60f + n * 66f) }
            .pointerInput(Unit) { detectTapGestures(onDoubleTap = { drag.begin(); drag.set(norm(0f)); change(0f); drag.end(); end() }) }
            .pointerInput(Unit) {
                awaitEachGesture {
                    val down = awaitFirstDown()
                    var acc = drag.begin()
                    drag(down.id) { c ->
                        acc = (acc - c.positionChange().y / (size.height * 1.1f)).coerceIn(0f, 1f)
                        drag.set(acc)
                        change(-60f + acc * 66f)
                        c.consume()
                    }
                    drag.end()
                    end()
                }
            },
    ) {
        val cx = size.width / 2
        val thumbH = 22.dp.toPx()
        val top = thumbH / 2
        val bottom = size.height - thumbH / 2
        val trackW = 4.dp.toPx()
        fun y(v: Float) = bottom - (bottom - top) * v
        drawLine(Palette.SurfaceTop, Offset(cx, top), Offset(cx, bottom), trackW, cap = StrokeCap.Round)
        drawLine(color, Offset(cx, y(n)), Offset(cx, bottom), trackW, cap = StrokeCap.Round)
        for (mark in floatArrayOf(0f, -12f, -24f, -48f)) {
            val my = y(norm(mark))
            val half = if (mark == 0f) size.width * 0.42f else size.width * 0.22f
            drawLine(if (mark == 0f) Palette.TextDim else Palette.GridStrong, Offset(cx - half, my), Offset(cx - trackW * 1.5f, my), 1.dp.toPx())
            drawLine(if (mark == 0f) Palette.TextDim else Palette.GridStrong, Offset(cx + trackW * 1.5f, my), Offset(cx + half, my), 1.dp.toPx())
        }
        val tw = min(size.width, 34.dp.toPx())
        val ty = y(n)
        val corner = androidx.compose.ui.geometry.CornerRadius(6.dp.toPx())
        drawRoundRect(Brush.verticalGradient(listOf(Color(0xFFF4F6FA), Color(0xFFC3C9D6)), ty - thumbH / 2, ty + thumbH / 2), Offset(cx - tw / 2, ty - thumbH / 2), Size(tw, thumbH), corner)
        drawLine(if (drag.dragging) color else Palette.Background, Offset(cx - tw * 0.3f, ty), Offset(cx + tw * 0.3f, ty), 2.dp.toPx(), cap = StrokeCap.Round)
    }
}

fun dbLabel(db: Float): String = if (db <= -59.9f) "-∞" else "%+.1f".format(db)
fun Float.clampDb() = max(-60f, min(6f, this))

// ---------------------------------------------------------------------------------------------------------------
// Botones, cabeceras, estados y diálogos comunes
// ---------------------------------------------------------------------------------------------------------------

enum class ButtonKind { Primary, Secondary, Ghost, Danger }

/** Botón de texto del sistema de diseño: 48 dp de alto, esquinas de 12 dp. Primary = violeta (una por pantalla). */
@Composable
fun CauceButton(
    text: String, onClick: () -> Unit, modifier: Modifier = Modifier, kind: ButtonKind = ButtonKind.Secondary,
    icon: ImageVector? = null, enabled: Boolean = true, height: Dp = Dims.Touch,
) {
    val (bg, fg) = when (kind) {
        ButtonKind.Primary -> Palette.Violet to Color.White
        ButtonKind.Secondary -> Palette.SurfaceTop to Palette.Text
        ButtonKind.Ghost -> Color.Transparent to Palette.VioletSoft
        ButtonKind.Danger -> Palette.Record.copy(alpha = 0.16f) to Palette.Record
    }
    Row(
        modifier.defaultMinSize(minHeight = height).clip(RoundedCornerShape(Dims.RadiusMedium)).background(if (enabled) bg else bg.copy(alpha = bg.alpha * 0.4f))
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick).padding(horizontal = if (kind == ButtonKind.Ghost) 8.dp else 16.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.Center,
    ) {
        val c = if (enabled) fg else fg.copy(alpha = 0.4f)
        if (icon != null) { Icon(icon, null, Modifier.size(20.dp), tint = c); Spacer(Modifier.width(8.dp)) }
        Text(text, style = MaterialTheme.typography.labelLarge, color = c, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

/** Botón de icono de barra de herramientas: zona táctil de 44 dp, activo = violeta sobre fondo tenue. */
@Composable
fun ToolButton(
    icon: ImageVector, description: String, onClick: () -> Unit, modifier: Modifier = Modifier, active: Boolean = false,
    enabled: Boolean = true, tint: Color = Palette.TextDim, activeColor: Color = Palette.Violet, size: Dp = Dims.TouchMin,
) {
    Box(
        modifier.size(size).clip(RoundedCornerShape(Dims.RadiusMedium)).background(if (active) activeColor.copy(alpha = 0.18f) else Color.Transparent)
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick).semantics { contentDescription = description },
        contentAlignment = Alignment.Center,
    ) { Icon(icon, null, Modifier.size(22.dp), tint = if (!enabled) Palette.TextFaint.copy(alpha = 0.5f) else if (active) activeColor.lighten() else tint) }
}

private fun Color.lighten(): Color = Color(red + (1 - red) * 0.35f, green + (1 - green) * 0.35f, blue + (1 - blue) * 0.35f, alpha)

/** Cabecera de panel (Batería, Synth, FX…): título, subtítulo y acciones a la derecha. */
@Composable
fun PanelHeader(title: String, subtitle: String? = null, modifier: Modifier = Modifier, trailing: @Composable () -> Unit = {}) {
    Row(modifier.fillMaxWidth().heightIn(min = 44.dp), verticalAlignment = Alignment.CenterVertically) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
            if (subtitle != null) Text(subtitle, style = MaterialTheme.typography.bodySmall, color = Palette.TextDim, maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
        trailing()
    }
}

/** Estado vacío o de error: icono, explicación y, si procede, una acción. */
@Composable
fun EmptyState(icon: ImageVector, title: String, text: String? = null, modifier: Modifier = Modifier, action: String? = null, onAction: () -> Unit = {}) {
    Column(modifier.padding(Space.xl), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
        Box(Modifier.size(64.dp).clip(CircleShape).background(Palette.SurfaceHigh), contentAlignment = Alignment.Center) {
            Icon(icon, null, Modifier.size(30.dp), tint = Palette.VioletSoft)
        }
        Spacer(Modifier.height(Space.md))
        Text(title, style = MaterialTheme.typography.titleMedium, textAlign = TextAlign.Center)
        if (text != null) { Spacer(Modifier.height(Space.xs)); Text(text, style = MaterialTheme.typography.bodySmall, color = Palette.TextDim, textAlign = TextAlign.Center) }
        if (action != null) { Spacer(Modifier.height(Space.lg)); CauceButton(action, onAction, kind = ButtonKind.Primary, icon = Icons.Default.Add) }
    }
}

/** Diálogo con el estilo de la app: superficie alta, esquinas de 24 dp, botón principal a la derecha. */
@Composable
fun CauceDialog(
    title: String, onDismiss: () -> Unit, confirm: String? = "OK", onConfirm: () -> Unit = onDismiss, dismiss: String? = null,
    danger: Boolean = false, confirmEnabled: Boolean = true, content: @Composable ColumnScope.() -> Unit,
) {
    AlertDialog(
        onDismissRequest = onDismiss, containerColor = Palette.SurfaceHigh, titleContentColor = Palette.Text, textContentColor = Palette.TextDim,
        shape = RoundedCornerShape(24.dp),
        title = { Text(title, style = MaterialTheme.typography.titleLarge) },
        text = { Column(content = content) },
        confirmButton = { if (confirm != null) CauceButton(confirm, onConfirm, kind = if (danger) ButtonKind.Danger else ButtonKind.Primary, enabled = confirmEnabled, height = 44.dp) },
        dismissButton = { if (dismiss != null) CauceButton(dismiss, onDismiss, kind = ButtonKind.Ghost, height = 44.dp) },
    )
}
