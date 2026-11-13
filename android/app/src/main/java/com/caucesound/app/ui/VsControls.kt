package com.caucesound.app.ui

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
import androidx.compose.foundation.layout.defaultMinSize
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.input.pointer.positionChange
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.disabled
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.drawText
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.em
import androidx.compose.ui.unit.sp
import kotlin.math.cos
import kotlin.math.log10
import kotlin.math.sin

/**
 * Identidad visual «Dark Rose / Neon Violet» de VocalShift AI Studio (solo en su pantalla). El neón se reserva para lo
 * que está activo o muestra un valor; fondos y paneles quedan oscuros.
 */
object VS {
    val Bg = Color(0xFF090811)
    val Panel = Color(0xFF171322)
    val PanelHi = Color(0xFF201A2E)
    val PanelEdge = Color(0xFF2A2238)
    val VioletDeep = Color(0xFF28183D)
    val Groove = Color(0xFF241C33)
    val Rose = Color(0xFFFF4CA8)
    val Violet = Color(0xFFA56CFF)
    val RoseSoft = Color(0xFFFFC0DF)
    val Text = Color(0xFFF4EFFA)
    val Dim = Color(0xFF9A90AE)
    val Warn = Color(0xFFFFB347)
    val Red = Color(0xFFFF4D5E)
    val Accent: Brush get() = Brush.horizontalGradient(listOf(Violet, Rose))
    val PanelRadius = 12.dp
    /** Tamaños mínimos legibles en móvil. */
    val Secondary = 12.sp
    val Label = 13.sp
    val Value = 16.sp
    /** Títulos en mayúsculas con espaciado amplio (0,12–0,14 em). */
    val Title = TextStyle(fontWeight = FontWeight.Bold, fontSize = 13.sp, letterSpacing = 0.14.em, color = Text)
}

/** Panel grafito con bisel de 1 dp y título en mayúsculas con subrayado de acento. */
@Composable
fun VsPanel(title: String?, modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(
        modifier.fillMaxWidth().clip(RoundedCornerShape(VS.PanelRadius)).background(VS.Panel).border(1.dp, VS.PanelEdge, RoundedCornerShape(VS.PanelRadius)).padding(12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        if (title != null) Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
            Text(title.uppercase(), style = VS.Title)
            Box(Modifier.padding(top = 4.dp).width(28.dp).height(2.dp).background(VS.Accent))
        }
        content()
    }
}

/** Botones de selección segmentados: el activo con degradado violeta → rosa; el inactivo grafito. */
@Composable
fun VsSegmented(options: List<String>, selected: Int, onSelect: (Int) -> Unit, modifier: Modifier = Modifier, tag: String? = null, height: Dp = 44.dp, enabled: Boolean = true) {
    Row(modifier.let { if (tag != null) it.testTag(tag) else it }.alpha(if (enabled) 1f else 0.45f), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
        options.forEachIndexed { i, o ->
            val on = i == selected
            val shape = RoundedCornerShape(height / 2)
            Box(
                Modifier.weight(1f).height(height).clip(shape)
                    .then(if (on) Modifier.background(VS.Accent) else Modifier.background(VS.PanelHi).border(1.dp, VS.PanelEdge, shape))
                    .clickable(enabled = enabled) { onSelect(i) }.padding(horizontal = 8.dp)
                    .semantics { role = Role.Tab; stateDescription = if (on) "on" else "off" }
                    .let { if (tag != null) it.testTag("${tag}_$i") else it },
                contentAlignment = Alignment.Center,
            ) { Text(o, fontSize = 14.sp, fontWeight = if (on) FontWeight.Bold else FontWeight.Medium, color = if (on) Color.White else VS.Dim, maxLines = 1, overflow = TextOverflow.Ellipsis) }
        }
    }
}

/**
 * Interruptor tipo pastilla. [locked] = el parámetro existe en VocalShift pero el móvil lo fija (se muestra su valor
 * real, sin poder cambiarlo). [enabled] = false: no aplica ahora.
 */
@Composable
fun VsSwitch(label: String, on: Boolean, onToggle: () -> Unit, modifier: Modifier = Modifier, note: String? = null, enabled: Boolean = true, tag: String? = null) {
    Row(
        modifier.fillMaxWidth().defaultMinSize(minHeight = 44.dp).clip(RoundedCornerShape(8.dp)).clickable(enabled = enabled, onClick = onToggle)
            .semantics(mergeDescendants = true) { role = Role.Switch; stateDescription = if (on) "on" else "off"; contentDescription = label; if (!enabled) disabled() }
            .let { if (tag != null) it.testTag(tag) else it }.alpha(if (enabled) 1f else 0.5f),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Box(Modifier.size(48.dp, 28.dp).clip(CircleShape).then(if (on) Modifier.background(VS.Accent) else Modifier.background(VS.Groove).border(1.dp, VS.PanelEdge, CircleShape)).padding(3.dp),
            contentAlignment = if (on) Alignment.CenterEnd else Alignment.CenterStart) {
            Box(Modifier.size(22.dp).clip(CircleShape).background(if (on) Color.White else VS.Dim))
        }
        Column(Modifier.padding(start = 10.dp).weight(1f)) {
            Text(label, fontSize = VS.Label, color = VS.Text)
            if (note != null) Text(note, fontSize = VS.Secondary, color = VS.Dim)
        }
    }
}

/** Formato de los valores dentro de las perillas. */
fun interface VsFormat { fun format(v: Float): String }

/**
 * Perilla de VocalShift. Arrastre vertical para cambiar el valor (recorrido completo en 200 dp), doble toque para
 * volver al valor por defecto y mantener pulsado para escribir el valor exacto. El valor va DENTRO de la perilla.
 * [scale] = marcas con texto alrededor (PITCH grande). [bipolar] = el arco de valor sale del cero.
 */
@Composable
fun VsKnob(
    label: String?, value: Float, min: Float, max: Float, default: Float, onChange: (Float) -> Unit, format: VsFormat,
    modifier: Modifier = Modifier, size: Dp = 84.dp, description: String? = null, enabled: Boolean = true,
    scale: List<Pair<Float, String>> = emptyList(), unit: String = "", tag: String? = null, valueSize: androidx.compose.ui.unit.TextUnit = VS.Value,
    disabledNote: String? = null,
) {
    fun toNorm(v: Float) = ((v - min) / (max - min)).coerceIn(0f, 1f)
    fun fromNorm(n: Float) = min + (max - min) * n
    val drag = rememberDragValue()
    val ext = toNorm(value)
    val norm = drag.shown(ext)
    SideEffect { drag.settle(ext) }
    val change by rememberUpdatedState(onChange)
    var typing by remember { mutableStateOf(false) }
    val measurer = rememberTextMeasurer()
    val bipolar = min < 0f && max > 0f
    val shownValue = fromNorm(norm)
    Column(modifier.alpha(if (enabled) 1f else 0.45f), horizontalAlignment = Alignment.CenterHorizontally) {
        Canvas(
            Modifier.size(size).semantics {
                contentDescription = (label ?: "") + " " + format.format(shownValue)
                if (!enabled) disabled()
            }.let { if (tag != null) it.testTag(tag) else it }
                .pointerInput(enabled, min, max, default) {
                    if (!enabled) return@pointerInput
                    detectTapGestures(
                        onDoubleTap = { drag.begin(); drag.set(toNorm(default)); change(default); drag.end() },
                        onLongPress = { typing = true },
                    )
                }
                .pointerInput(enabled, min, max) {
                    if (!enabled) return@pointerInput
                    val travel = 200.dp.toPx()
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        var acc = drag.begin()
                        var moved = false
                        drag(down.id) { c ->
                            val d = c.positionChange()
                            if (!moved && kotlin.math.abs(d.y) < 0.5f) return@drag
                            moved = true
                            acc = (acc - d.y / travel).coerceIn(0f, 1f)
                            drag.set(acc)
                            change(fromNorm(acc))
                            c.consume()
                        }
                        drag.end()
                    }
                },
        ) {
            val d = this.size.minDimension
            val hasScale = scale.isNotEmpty()
            val outer = if (hasScale) d * 0.36f else d * 0.44f
            val stroke = outer * 0.13f
            val c = Offset(this.size.width / 2, this.size.height / 2)
            val start = 135f
            val sweep = 270f
            val tl = Offset(c.x - outer, c.y - outer)
            val arc = Size(outer * 2, outer * 2)
            // Pista y marcas.
            drawArc(VS.Groove, start, sweep, false, tl, arc, style = Stroke(stroke, cap = StrokeCap.Round))
            val ticks = if (hasScale) 24 else 20
            for (i in 0..ticks) {
                val a = Math.toRadians((start + sweep * i / ticks).toDouble())
                val dir = Offset(cos(a).toFloat(), sin(a).toFloat())
                val major = hasScale && i % 6 == 0
                drawLine(if (major) VS.Dim else VS.PanelEdge, c + dir * (outer + stroke * 0.9f), c + dir * (outer + stroke * (if (major) 2.0f else 1.5f)), strokeWidth = if (major) 2f else 1.2f)
            }
            // Arco de valor con degradado violeta → rosa y halo suave.
            val from = if (bipolar) toNorm(0f) else 0f
            val a0 = start + sweep * minOf(from, norm)
            val sw = sweep * kotlin.math.abs(norm - from)
            if (sw > 0.5f) {
                val grad = Brush.linearGradient(listOf(VS.Violet, VS.Rose), Offset(c.x - outer, c.y + outer), Offset(c.x + outer, c.y - outer))
                drawArc(grad, a0, sw, false, tl, arc, style = Stroke(stroke * 2.4f, cap = StrokeCap.Round), alpha = 0.18f)
                drawArc(grad, a0, sw, false, tl, arc, style = Stroke(stroke, cap = StrokeCap.Round))
            }
            // Cuerpo metálico oscuro con aro interior y punto en la posición actual.
            val body = outer - stroke * 1.4f
            drawCircle(Brush.verticalGradient(listOf(Color(0xFF2E2640), Color(0xFF110D19)), c.y - body, c.y + body), body, c)
            drawCircle(Color.Black.copy(alpha = 0.35f), body, c, style = Stroke(2f))
            drawCircle(VS.Rose.copy(alpha = if (drag.dragging) 0.6f else 0.28f), body * 0.70f, c, style = Stroke(1.5f))
            val a = Math.toRadians((start + sweep * norm).toDouble())
            val dir = Offset(cos(a).toFloat(), sin(a).toFloat())
            drawCircle(VS.RoseSoft, stroke * 0.45f, c + dir * (body * 0.84f))
            // Valor dentro de la perilla.
            val txt = measurer.measure(format.format(shownValue), TextStyle(color = VS.Text, fontSize = valueSize, fontWeight = FontWeight.Bold, textAlign = TextAlign.Center))
            drawText(txt, topLeft = Offset(c.x - txt.size.width / 2f, c.y - txt.size.height / 2f))
            // Escala exterior (−24, −12, 0, +12, +24).
            for ((v, s) in scale) {
                val av = Math.toRadians((start + sweep * toNorm(v)).toDouble())
                val p = c + Offset(cos(av).toFloat(), sin(av).toFloat()) * (outer + stroke * 3.3f)
                val t = measurer.measure(s, TextStyle(color = VS.Dim, fontSize = 12.sp, fontWeight = FontWeight.SemiBold))
                drawText(t, topLeft = Offset(p.x - t.size.width / 2f, p.y - t.size.height / 2f))
            }
        }
        if (label != null) Text(label.uppercase(), style = VS.Title, maxLines = 1)
        if (description != null) Text(description, fontSize = VS.Secondary, color = VS.Dim, textAlign = TextAlign.Center, modifier = Modifier.width(size + 24.dp))
        if (!enabled && disabledNote != null) Text(disabledNote, fontSize = VS.Secondary, color = VS.Warn, textAlign = TextAlign.Center, modifier = Modifier.width(size + 24.dp))
    }
    if (typing) ValueEntryDialog(label ?: "", value, min, max, unit, { typing = false }) { change(it) }
}

/** Escribir el valor exacto (mantener pulsada una perilla). Se acota al rango del parámetro. */
@Composable
fun ValueEntryDialog(title: String, current: Float, min: Float, max: Float, unit: String, onDismiss: () -> Unit, onDone: (Float) -> Unit) {
    var text by remember { mutableStateOf(if (current % 1f == 0f) "%.0f".format(current) else "%.2f".format(current)) }
    val parsed = text.replace(',', '.').replace('−', '-').trim().toFloatOrNull()
    AlertDialog(
        onDismissRequest = onDismiss, containerColor = VS.Panel,
        title = { Text(title, color = VS.Text) },
        text = {
            Column {
                OutlinedTextField(text, { text = it }, singleLine = true, keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number), suffix = { Text(unit) }, modifier = Modifier.testTag("vs_value_entry"))
                Text(tr("Entre %s y %s %s", "Between %s and %s %s").format(fmtNum(min), fmtNum(max), unit), fontSize = VS.Secondary, color = VS.Dim)
            }
        },
        confirmButton = { TextButton({ parsed?.let { onDone(it.coerceIn(min, max)) }; onDismiss() }, enabled = parsed != null) { Text("OK") } },
        dismissButton = { TextButton(onDismiss) { Text(tr("Cancelar", "Cancel")) } },
    )
}

private fun fmtNum(v: Float) = if (v % 1f == 0f) "%.0f".format(v) else "%.1f".format(v)

/** dB del fader de la 2.ª voz con −60 = −∞ (silencio), como en VocalShift. */
fun vsDb(db: Float) = if (db <= -59.95f) "−∞ dB" else "%+.1f dB".format(db).replace('-', '−')

/**
 * Fader de la 2.ª voz (−60 = −∞ … +6 dB, lineal en dB como en el escritorio). Pista grafito con relleno rosa → violeta
 * hasta la posición, perilla tipo hardware con una línea rosa, escala en dB y el valor debajo. Doble toque = valor del
 * preset. [vertical] = false lo dibuja en horizontal (teléfono en vertical).
 */
@Composable
fun VsFader(db: Float, onChange: (Float) -> Unit, presetDb: Float, modifier: Modifier = Modifier, vertical: Boolean = false, tag: String = "vocal_second_level") {
    fun norm(d: Float) = (d.coerceIn(-60f, 6f) + 60f) / 66f
    val drag = rememberDragValue()
    val ext = norm(db)
    val n = drag.shown(ext)
    SideEffect { drag.settle(ext) }
    val change by rememberUpdatedState(onChange)
    val measurer = rememberTextMeasurer()
    val shown = -60f + 66f * n
    val marks = listOf(6f to "+6", 0f to "0", -12f to "−12", -24f to "−24", -36f to "−36", -60f to "−∞")
    Column(modifier, horizontalAlignment = Alignment.CenterHorizontally) {
        Canvas(
            (if (vertical) Modifier.width(72.dp).fillMaxHeight() else Modifier.fillMaxWidth().height(64.dp))
                .semantics { contentDescription = tr("Nivel de la segunda voz", "Second voice level") + " " + vsDb(shown) }.testTag(tag)
                .pointerInput(presetDb) { detectTapGestures(onDoubleTap = { drag.begin(); drag.set(norm(presetDb)); change(presetDb); drag.end() }) }
                .pointerInput(vertical) {
                    awaitEachGesture {
                        val down = awaitFirstDown()
                        val len = (if (vertical) size.height else size.width) - 40.dp.toPx()
                        var acc = drag.begin()
                        drag(down.id) { c ->
                            val d = c.positionChange()
                            acc = (acc + (if (vertical) -d.y else d.x) / len).coerceIn(0f, 1f)
                            drag.set(acc)
                            change((-60f + 66f * acc).let { (it * 2).toInt() / 2f })
                            c.consume()
                        }
                        drag.end()
                    }
                },
        ) {
            val pad = 20.dp.toPx()
            val trackW = 6.dp.toPx()
            if (vertical) {
                val x = size.width * 0.35f
                val top = pad; val bottom = size.height - pad
                fun y(v: Float) = bottom - (bottom - top) * norm(v)
                drawLine(VS.Groove, Offset(x, top), Offset(x, bottom), trackW, StrokeCap.Round)
                drawLine(Brush.verticalGradient(listOf(VS.Rose, VS.Violet), y(shown), bottom), Offset(x, y(shown)), Offset(x, bottom), trackW, StrokeCap.Round)
                for ((v, s) in marks) {
                    drawLine(VS.Dim, Offset(x + 14.dp.toPx(), y(v)), Offset(x + 20.dp.toPx(), y(v)), 1.5f)
                    val t = measurer.measure(s, TextStyle(color = VS.Dim, fontSize = 12.sp))
                    drawText(t, topLeft = Offset(x + 23.dp.toPx(), y(v) - t.size.height / 2f))
                }
                val cy = y(shown)
                val kw = 34.dp.toPx(); val kh = 18.dp.toPx()
                drawRoundRect(Brush.verticalGradient(listOf(Color(0xFF3A3150), Color(0xFF15111E)), cy - kh / 2, cy + kh / 2), Offset(x - kw / 2, cy - kh / 2), Size(kw, kh), androidx.compose.ui.geometry.CornerRadius(4.dp.toPx()))
                drawLine(VS.Rose, Offset(x - kw / 2 + 5.dp.toPx(), cy), Offset(x + kw / 2 - 5.dp.toPx(), cy), 2.5f.dp.toPx(), StrokeCap.Round)
            } else {
                val yMid = size.height * 0.32f
                val left = pad; val right = size.width - pad
                fun x(v: Float) = left + (right - left) * norm(v)
                drawLine(VS.Groove, Offset(left, yMid), Offset(right, yMid), trackW, StrokeCap.Round)
                drawLine(Brush.horizontalGradient(listOf(VS.Violet, VS.Rose), left, x(shown)), Offset(left, yMid), Offset(x(shown), yMid), trackW, StrokeCap.Round)
                for ((v, s) in marks) {
                    drawLine(VS.Dim, Offset(x(v), yMid + 12.dp.toPx()), Offset(x(v), yMid + 17.dp.toPx()), 1.5f)
                    val t = measurer.measure(s, TextStyle(color = VS.Dim, fontSize = 12.sp))
                    drawText(t, topLeft = Offset(x(v) - t.size.width / 2f, yMid + 19.dp.toPx()))
                }
                val cx = x(shown)
                val kw = 18.dp.toPx(); val kh = 30.dp.toPx()
                drawRoundRect(Brush.horizontalGradient(listOf(Color(0xFF3A3150), Color(0xFF15111E)), cx - kw / 2, cx + kw / 2), Offset(cx - kw / 2, yMid - kh / 2), Size(kw, kh), androidx.compose.ui.geometry.CornerRadius(4.dp.toPx()))
                drawLine(VS.Rose, Offset(cx, yMid - kh / 2 + 5.dp.toPx()), Offset(cx, yMid + kh / 2 - 5.dp.toPx()), 2.5f.dp.toPx(), StrokeCap.Round)
            }
        }
        Text(vsDb(shown), fontSize = VS.Value, fontWeight = FontWeight.Bold, color = VS.Text)
    }
}

/**
 * Medidor de LED segmentados (violeta abajo, rosa arriba) con indicador de saturación rojo arriba; tocarlo lo borra.
 * [level] se lee en la fase de dibujo. [enabled] = false: no hay señal que medir (se muestra apagado).
 */
@Composable
fun VsMeter(level: () -> Float, label: String, modifier: Modifier = Modifier, enabled: Boolean = true, tag: String? = null) {
    var clip by remember { mutableStateOf(false) }
    val hold = remember { FloatArray(1) }
    Column(
        modifier.width(44.dp).clickable(enabled = enabled) { clip = false }
            .semantics { contentDescription = label + if (clip) " clip" else "" }.let { if (tag != null) it.testTag(tag) else it },
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Canvas(Modifier.width(10.dp).weight(1f)) {
            val l = if (enabled) level() else 0f
            if (l >= 0.999f && !clip) clip = true
            val db = if (l > 1e-5f) 20 * log10(l) else -100f
            hold[0] = maxOf(((db + 60f) / 60f).coerceIn(0f, 1f), hold[0] - 0.025f)
            val segs = 28
            val clipH = 6.dp.toPx()
            val gap = 2.dp.toPx()
            val h = (size.height - clipH - gap) / segs
            drawRect(if (clip) VS.Red else VS.Groove, Offset.Zero, Size(size.width, clipH))
            for (i in 0 until segs) {
                val y = size.height - (i + 1) * h
                val lit = enabled && (i + 0.5f) / segs <= hold[0]
                val col = if (i >= segs * 0.72f) VS.Rose else VS.Violet
                drawRect(if (lit) col else VS.Groove, Offset(0f, y + 1f), Size(size.width, h - 2f))
            }
        }
        Spacer(Modifier.height(4.dp))
        Text(label, fontSize = VS.Secondary, fontWeight = FontWeight.Bold, color = if (enabled) VS.Dim else VS.Dim.copy(alpha = 0.5f), letterSpacing = 0.1.em)
    }
}
