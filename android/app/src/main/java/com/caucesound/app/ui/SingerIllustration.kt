package com.caucesound.app.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.MutableFloatState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.drawWithCache
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.BlendMode
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Canvas
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.Paint
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.asAndroidPath
import androidx.compose.ui.graphics.drawscope.CanvasDrawScope
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.drawscope.clipPath
import androidx.compose.ui.graphics.drawscope.drawIntoCanvas
import androidx.compose.ui.graphics.drawscope.withTransform
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.graphics.nativeCanvas
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.graphics.vector.PathParser
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.LayoutDirection
import kotlinx.coroutines.delay
import java.util.concurrent.atomic.AtomicInteger
import kotlin.math.abs
import kotlin.math.log10
import kotlin.math.min

/*
 * Ilustración central de VocalShift: la cantante holográfica (arte vectorial original de VocalShift AI Studio).
 *
 * Rendimiento (requisito de la especificación):
 *  - La parte estática (fondo del panel, rostro, cabello) se dibuja UNA vez en un mapa de bits dentro de
 *    drawWithCache; solo se regenera si cambia el tamaño.
 *  - La capa reactiva va en su propio graphicsLayer encima y solo se repinta cuando el nivel suavizado cambia más de
 *    0,003. El bucle que lee el nivel corre a 30 Hz como máximo, solo mientras suena el transporte o el brillo se está
 *    apagando; en silencio no se repinta nada y al cerrar la pantalla el bucle se cancela con la composición.
 *  - No hay sincronía labial: el nivel es el pico de la pista que suena (el motor ya lo publica para los medidores).
 */

internal sealed interface SingerPaint
internal data class SingerSolid(val argb: Long) : SingerPaint
internal enum class SingerGrad : SingerPaint { SKIN, HAIRG, HAIRMASS, FRONTG, CHEEK, JAW, LIPS, LOCKG, SHADOW }

/** Una operación de dibujo del SVG original (ver tools/svg_to_singer_kt.py). */
internal class SingerOp(
    val path: Int = -1, val oval: FloatArray? = null, val rect: Boolean = false,
    val fill: SingerPaint?, val fillAlpha: Float, val stroke: SingerPaint?, val strokeAlpha: Float, val width: Float,
    val round: Boolean, val blur: Boolean, val fade: Boolean, val clipSilhouette: Boolean = false,
)

/** Contadores para las pruebas: demuestran que en silencio no se repinta y que al cerrar no queda nada corriendo. */
object SingerDebug {
    val staticRenders = AtomicInteger()
    val reactiveDraws = AtomicInteger()
    val loopsRunning = AtomicInteger()
    /** Cambios del nivel mostrado: cada uno invalida (repinta) solo la capa reactiva. */
    val levelUpdates = AtomicInteger()
}

/** Tamaño de diseño y encaje en el panel (especificación §4). */
internal object SingerFit {
    const val W = 650f
    const val H = 1150f
    val ANCHOR = Offset(495f, 60f)
    val HEAD = Offset(530f, 400f)
    val CHEEK = Offset(640f, 425f)
    val MOUTH = Offset(735f, 492f)
    fun scale(size: Size) = min(size.width / W, size.height / H)
    /** Traslación para que el punto de diseño (495, 60) caiga en (ancho × 0,5, alto × 0,04). */
    fun origin(size: Size, s: Float) = Offset(size.width * 0.5f - ANCHOR.x * s, size.height * 0.04f - ANCHOR.y * s)
    fun toPanel(size: Size, p: Offset): Offset { val s = scale(size); val o = origin(size, s); return Offset(o.x + p.x * s, o.y + p.y * s) }
}

private object SingerPaths {
    val all: List<Path> by lazy { SingerArt.D.map { PathParser().parsePathString(it).toPath() } }
}

/** Nivel suavizado a partir del pico (especificación §4): sube rápido, cae suave. Lógica pura, probada en la JVM. */
internal object SingerLevel {
    fun target(peak: Float): Float {
        val db = if (peak > 0f) 20f * log10(peak) else -100f
        return ((db + 48f) / 48f).coerceIn(0f, 1f)
    }
    fun next(level: Float, target: Float): Float {
        val n = if (target > level) level + 0.5f * (target - level) else level * 0.9f
        return if (n < 0.002f) 0f else n
    }
    /** ¿Hay que repintar? Solo si el cambio supera 0,003 o si el brillo se acaba de apagar del todo. */
    fun shouldRepaint(shown: Float, next: Float) = abs(next - shown) > 0.003f || (next == 0f && shown != 0f)
}

/**
 * La ilustración completa. [peak] devuelve el pico lineal (0..1+) de lo que suena; [playing] dice si el transporte
 * está en marcha. Sin señal no hay animación.
 */
@Composable
fun SingerIllustration(peak: () -> Float, playing: Boolean, modifier: Modifier = Modifier, levelState: MutableFloatState? = null) {
    val level = levelState ?: remember { mutableFloatStateOf(0f) }
    val readPeak by rememberUpdatedState(peak)
    LaunchedEffect(playing) {
        if (!playing && level.floatValue == 0f) return@LaunchedEffect
        SingerDebug.loopsRunning.incrementAndGet()
        try {
            var l = level.floatValue
            while (true) {
                val t = if (playing) SingerLevel.target(readPeak()) else 0f
                val n = SingerLevel.next(l, t)
                if (SingerLevel.shouldRepaint(level.floatValue, n)) { level.floatValue = n; SingerDebug.levelUpdates.incrementAndGet() }
                l = n
                if (!playing && n == 0f) break
                delay(33) // 30 fps como máximo
            }
        } finally { SingerDebug.loopsRunning.decrementAndGet() }
    }
    Box(modifier.semantics { contentDescription = "VocalShift" }.testTag("vocal_singer")) {
        // Capa estática: un mapa de bits que solo se rehace al cambiar el tamaño.
        Spacer(Modifier.fillMaxSize().graphicsLayer().drawWithCache {
            val w = size.width.toInt().coerceAtLeast(1)
            val h = size.height.toInt().coerceAtLeast(1)
            val bmp = renderSingerStatic(w, h, this)
            onDrawBehind { drawImage(bmp) }
        })
        // Capa reactiva: solo lee el nivel en la fase de dibujo.
        Spacer(Modifier.fillMaxSize().graphicsLayer().drawBehind { drawSingerReactive(level.floatValue) }.testTag("vocal_singer_glow"))
    }
}

/** Dibuja la parte estática en un mapa de bits nuevo (medido en las pruebas). */
internal fun renderSingerStatic(w: Int, h: Int, density: Density): ImageBitmap {
    SingerDebug.staticRenders.incrementAndGet()
    val bmp = ImageBitmap(w, h)
    CanvasDrawScope().draw(density, LayoutDirection.Ltr, Canvas(bmp), Size(w.toFloat(), h.toFloat())) {
        drawPanelBackground()
        val s = SingerFit.scale(size)
        val o = SingerFit.origin(size, s)
        withTransform({ translate(o.x, o.y); scale(s, s, Offset.Zero) }) { drawSingerArt() }
        drawScanlines()
        // Fundido inferior hacia el fondo para que el selector de ruta se lea bien.
        drawRect(Brush.verticalGradient(0f to Color.Transparent, 1f to VS.Bg, startY = size.height * 0.72f, endY = size.height), Offset(0f, size.height * 0.72f), Size(size.width, size.height * 0.28f))
    }
    return bmp
}

private fun DrawScope.drawPanelBackground() {
    drawRect(Brush.verticalGradient(listOf(VS.VioletDeep, VS.Bg)))
    // Luz lateral violeta desde la izquierda y rosa desde la derecha.
    drawRect(Brush.horizontalGradient(0f to VS.Violet.copy(alpha = 0.20f), 0.45f to Color.Transparent))
    drawRect(Brush.horizontalGradient(0.55f to Color.Transparent, 1f to VS.Rose.copy(alpha = 0.16f)))
    // Cinco anillos tenues detrás de la cabeza, centrados en el punto de diseño (530, 400).
    val c = SingerFit.toPanel(size, SingerFit.HEAD)
    val base = min(size.width, size.height) * 0.46f
    for (k in 0 until 5) drawCircle(VS.Violet.copy(alpha = 0.07f), base * (0.30f + 0.16f * k), c, style = Stroke(1.2f * density))
}

private fun DrawScope.drawScanlines() {
    val step = 4f * density
    var y = 0f
    val c = Color.White.copy(alpha = 0.02f)
    while (y < size.height) { drawLine(c, Offset(0f, y), Offset(size.width, y), strokeWidth = density); y += step }
}

private fun brushFor(p: SingerPaint, alpha: Float, path: Path?): Brush = when (p) {
    is SingerSolid -> Brush.linearGradient(listOf(Color(p.argb).copy(alpha = alpha), Color(p.argb).copy(alpha = alpha)))
    SingerGrad.SKIN -> Brush.linearGradient(listOf(Color(0xF2241238), Color(0xD9B8337A)), Offset(350f, 400f), Offset(780f, 430f))
    SingerGrad.HAIRG -> Brush.linearGradient(0f to Color(0xFFFFC0DF), 0.42f to VS.Rose, 0.72f to VS.Violet, 1f to VS.Violet.copy(alpha = 0f), start = Offset(600f, 120f), end = Offset(180f, 1070f))
    SingerGrad.HAIRMASS -> Brush.linearGradient(listOf(Color(0xF25A2A7A), Color(0xE61A0E2A)), Offset(600f, 120f), Offset(180f, 1000f))
    SingerGrad.FRONTG -> Brush.linearGradient(listOf(VS.Violet, VS.Rose), Offset(600f, 180f), Offset(700f, 640f))
    SingerGrad.CHEEK -> Brush.radialGradient(listOf(VS.RoseSoft.copy(alpha = 0.32f), VS.RoseSoft.copy(alpha = 0f)), Offset(640f, 425f), 100f)
    SingerGrad.JAW -> Brush.radialGradient(listOf(VS.Bg.copy(alpha = 0.55f), VS.Bg.copy(alpha = 0f)), Offset(630f, 625f), 90f)
    SingerGrad.LIPS -> { val b = path?.getBounds() ?: Rect.Zero; Brush.linearGradient(listOf(VS.Rose, VS.RoseSoft), b.topLeft, b.bottomRight) }
    SingerGrad.LOCKG -> Brush.linearGradient(0f to Color(0xFF7A3AA0), 0.6f to Color(0xFFC03A8E), 1f to VS.Rose.copy(alpha = 0.85f), start = Offset(560f, 160f), end = Offset(640f, 320f))
    SingerGrad.SHADOW -> Brush.radialGradient(listOf(VS.Rose.copy(alpha = 0.3f), VS.Rose.copy(alpha = 0f)), Offset(660f, 360f), 30f)
}

/** El SVG, operación por operación, en unidades de diseño (la transformación la pone quien llama). */
private fun DrawScope.drawSingerArt() {
    val paths = SingerPaths.all
    for (op in SingerArt.OPS) {
        val path = if (op.path >= 0) paths[op.path] else null
        if (op.clipSilhouette && op.fill != null) {
            clipPath(paths[SingerArt.SILHOUETTE]) { drawRect(brushFor(op.fill, op.fillAlpha, null), Offset.Zero, Size(1000f, 1200f), alpha = op.fillAlpha) }
            continue
        }
        if (op.fade && path != null && op.fill != null) {
            // Máscara del SVG: el bloque de cabello se desvanece entre y = 640 y y = 1040.
            drawIntoCanvas { cv ->
                cv.saveLayer(Rect(0f, 0f, 1000f, 1200f), Paint())
                drawPath(path, brushFor(op.fill, op.fillAlpha, path), alpha = op.fillAlpha)
                drawRect(Brush.verticalGradient(0f to Color.White, 1f to Color.Transparent, startY = 640f, endY = 1040f), Offset.Zero, Size(1000f, 1200f), blendMode = BlendMode.DstIn)
                cv.restore()
            }
            continue
        }
        if (op.fill != null) {
            val b = brushFor(op.fill, op.fillAlpha, path)
            val a = if (op.fill is SingerSolid) 1f else op.fillAlpha
            when {
                path != null -> drawPath(path, b, alpha = a)
                op.oval != null -> drawOval(b, Offset(op.oval[0] - op.oval[2], op.oval[1] - op.oval[3]), Size(op.oval[2] * 2, op.oval[3] * 2), alpha = a)
            }
        }
        if (op.stroke != null) {
            if (op.blur && path != null && op.stroke is SingerSolid) {
                // Desenfoque gaussiano del SVG (stdDeviation 6) con un MaskFilter nativo: solo en la capa en caché.
                drawIntoCanvas { cv ->
                    val p = android.graphics.Paint(android.graphics.Paint.ANTI_ALIAS_FLAG).apply {
                        style = android.graphics.Paint.Style.STROKE
                        strokeWidth = op.width
                        color = Color(op.stroke.argb).copy(alpha = op.strokeAlpha).toArgb()
                        maskFilter = android.graphics.BlurMaskFilter(9.5f, android.graphics.BlurMaskFilter.Blur.NORMAL)
                    }
                    cv.nativeCanvas.drawPath(path.asAndroidPath(), p)
                }
                continue
            }
            val b = brushFor(op.stroke, op.strokeAlpha, path)
            val a = if (op.stroke is SingerSolid) 1f else op.strokeAlpha
            val st = Stroke(op.width, cap = if (op.round) StrokeCap.Round else StrokeCap.Butt)
            when {
                path != null -> drawPath(path, b, alpha = a, style = st)
                op.oval != null -> drawOval(b, Offset(op.oval[0] - op.oval[2], op.oval[1] - op.oval[3]), Size(op.oval[2] * 2, op.oval[3] * 2), alpha = a, style = st)
            }
        }
    }
}

/** Capa reactiva a la voz (especificación §4). [level] ya viene suavizado (0..1). */
internal fun DrawScope.drawSingerReactive(level: Float) {
    SingerDebug.reactiveDraws.incrementAndGet()
    val s = SingerFit.scale(size)
    val o = SingerFit.origin(size, s)
    // Anillos del halo: siempre presentes (tenues en silencio), crecen y se iluminan con la voz.
    val c = SingerFit.toPanel(size, SingerFit.HEAD)
    val base = min(size.width, size.height) * 0.46f
    for (k in 0 until 4) {
        val r = base * (0.45f + 0.17f * k) + level * base * 0.05f * (k + 1)
        val a = ((0.10f + 0.45f * level) * (1f - 0.22f * k)).coerceIn(0f, 1f)
        drawCircle((if (k % 2 == 0) VS.Rose else VS.Violet).copy(alpha = a), r, c, style = Stroke(1.5f * density))
    }
    if (level < 0.002f) return
    val paths = SingerPaths.all
    withTransform({ translate(o.x, o.y); scale(s, s, Offset.Zero) }) {
        // Contorno del perfil.
        drawPath(paths[SingerArt.CONTOUR], VS.Rose.copy(alpha = 0.22f * level), style = Stroke(6f + 8f * level, cap = StrokeCap.Round))
        // Mejilla.
        drawCircle(Brush.radialGradient(listOf(VS.RoseSoft.copy(alpha = 0.16f * level), Color.Transparent), SingerFit.CHEEK, 70f), 70f, SingerFit.CHEEK)
        // Labios.
        for (i in SingerArt.LIPS) drawPath(paths[i], VS.RoseSoft.copy(alpha = 0.22f * level))
        // Ondas de voz frente a la boca: arcos entre 62° y 118° (0° = arriba, sentido horario).
        for (k in 0 until 3) {
            val r = 26f + 18f * k + 6f * level
            val a = (level * (0.45f - 0.12f * k)).coerceIn(0f, 1f)
            val tl = Offset(SingerFit.MOUTH.x - r, SingerFit.MOUTH.y - r)
            drawArc(VS.Rose.copy(alpha = a * 0.45f), -28f, 56f, false, tl, Size(r * 2, r * 2), style = Stroke(7f, cap = StrokeCap.Round))
            drawArc(VS.RoseSoft.copy(alpha = a), -28f, 56f, false, tl, Size(r * 2, r * 2), style = Stroke(1.6f, cap = StrokeCap.Round))
        }
    }
}
