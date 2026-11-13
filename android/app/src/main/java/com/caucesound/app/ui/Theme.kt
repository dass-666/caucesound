package com.caucesound.app.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Shapes
import androidx.compose.material3.Typography
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Sistema de diseño de CAUCESOUND. Reglas (se aplican en todas las pantallas):
 *  - Fondos: carbón en cuatro niveles (Background < Surface < SurfaceHigh < SurfaceTop). Sin sombras: la profundidad
 *    se da con el nivel de superficie y un borde de 1 dp (Stroke).
 *  - Violeta = acción principal y selección (botón primario, pestaña activa, clip o pista seleccionados).
 *  - Cian = estado vivo de reproducción (cursor, bucle, metrónomo, paso que suena). Nunca para botones.
 *  - Rojo solo para grabar y borrar; ámbar solo para Solo y avisos.
 *  - El color de cada pista identifica su contenido (clips, tiras del mezclador). Tipos de pista: ver [kindColor].
 *  - Espaciado en múltiplos de 4 dp ([Space]); radios 8 / 12 / 16 / píldora ([Dims]); zona táctil mínima 44 dp y
 *    48 dp para acciones principales.
 *  - Tipografía: sans para textos, monoespaciada solo para números que cambian (posición, BPM, valores).
 */
object Palette {
    val Background = Color(0xFF0A0B0E)
    val Surface = Color(0xFF13151A)
    val SurfaceHigh = Color(0xFF1B1E26)
    val SurfaceTop = Color(0xFF272B36)
    val Grid = Color(0xFF242833)
    val GridStrong = Color(0xFF3A4150)
    val Stroke = Grid
    val Violet = Color(0xFF8B5CF6)
    val VioletSoft = Color(0xFFB79CFF)
    val Blue = Color(0xFF4C8DFF)
    val Cyan = Color(0xFF2DD4F0)
    val Accent = Cyan
    val Primary = Violet
    val Record = Color(0xFFFF4D5E)
    val Solo = Color(0xFFFFC94D)
    val Warn = Solo
    val Mute = Color(0xFF8A93A3)
    val Ok = Color(0xFF34D399)
    val Text = Color(0xFFE9ECF2)
    val TextDim = Color(0xFFA0A7B5)
    val TextFaint = Color(0xFF6F7786)
    val Playhead = Color(0xFFFFFFFF)

    val Brand: Brush get() = Brush.linearGradient(listOf(Violet, Blue, Cyan))
    val BrandHorizontal: Brush get() = Brush.horizontalGradient(listOf(Violet, Blue, Cyan))
}

object Space {
    val xs = 4.dp
    val sm = 8.dp
    val md = 12.dp
    val lg = 16.dp
    val xl = 24.dp
}

object Dims {
    val Touch = 48.dp        // zona táctil de acciones principales
    val TouchMin = 44.dp     // mínimo para acciones secundarias
    val Radius = 16.dp
    val RadiusMedium = 12.dp
    val RadiusSmall = 8.dp
}

/** Color que identifica el tipo de pista cuando no se usa el color propio de la pista (etiquetas, iconos). */
fun kindColor(kind: String): Color = when (kind) {
    "drums" -> Palette.Violet
    "synth" -> Palette.Blue
    "bus" -> Palette.Ok
    else -> Palette.Solo
}

private val Mono = FontFamily.Monospace

@Composable
fun CauceTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Palette.Violet,
            onPrimary = Color.White,
            secondary = Palette.Cyan,
            onSecondary = Color.Black,
            tertiary = Palette.Blue,
            background = Palette.Background,
            surface = Palette.Surface,
            surfaceVariant = Palette.SurfaceHigh,
            surfaceContainer = Palette.SurfaceHigh,
            surfaceContainerHigh = Palette.SurfaceTop,
            onBackground = Palette.Text,
            onSurface = Palette.Text,
            onSurfaceVariant = Palette.TextDim,
            outline = Palette.GridStrong,
            outlineVariant = Palette.Grid,
            error = Palette.Record,
        ),
        typography = Typography(
            headlineSmall = TextStyle(fontWeight = FontWeight.Bold, fontSize = 24.sp, letterSpacing = 0.2.sp),
            titleLarge = TextStyle(fontWeight = FontWeight.SemiBold, fontSize = 20.sp),
            titleMedium = TextStyle(fontWeight = FontWeight.SemiBold, fontSize = 16.sp),
            titleSmall = TextStyle(fontWeight = FontWeight.SemiBold, fontSize = 14.sp, letterSpacing = 0.3.sp),
            bodyMedium = TextStyle(fontSize = 14.sp),
            bodySmall = TextStyle(fontSize = 12.sp),
            labelLarge = TextStyle(fontWeight = FontWeight.SemiBold, fontSize = 14.sp, letterSpacing = 0.4.sp),
            labelMedium = TextStyle(fontWeight = FontWeight.Medium, fontSize = 12.sp, letterSpacing = 0.4.sp),
            labelSmall = TextStyle(fontWeight = FontWeight.Medium, fontSize = 11.sp, letterSpacing = 0.5.sp),
            displaySmall = TextStyle(fontFamily = Mono, fontWeight = FontWeight.Bold, fontSize = 22.sp),
        ),
        shapes = Shapes(
            small = RoundedCornerShape(Dims.RadiusSmall),
            medium = RoundedCornerShape(Dims.RadiusMedium),
            large = RoundedCornerShape(Dims.Radius),
            extraLarge = RoundedCornerShape(24.dp),
        ),
        content = content,
    )
}
