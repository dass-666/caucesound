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
 * Identidad visual de CAUCESOUND: fondos carbón, acento violeta para acciones principales, azul eléctrico y cian
 * para estados activos y medidores. El color fuerte se reserva para lo importante (transporte, selección, pistas).
 */
object Palette {
    val Background = Color(0xFF0B0C10)
    val Surface = Color(0xFF14161C)
    val SurfaceHigh = Color(0xFF1C1F27)
    val SurfaceTop = Color(0xFF262A35)
    val Grid = Color(0xFF262B35)
    val GridStrong = Color(0xFF39404E)
    val Violet = Color(0xFF8B5CF6)
    val Blue = Color(0xFF3B82F6)
    val Cyan = Color(0xFF22D3EE)
    val Accent = Cyan
    val Primary = Violet
    val Record = Color(0xFFFF4D5E)
    val Solo = Color(0xFFFFC94D)
    val Mute = Color(0xFF7D8590)
    val Ok = Color(0xFF34D399)
    val Text = Color(0xFFE8EAF0)
    val TextDim = Color(0xFF9AA1B0)
    val TextFaint = Color(0xFF6B7280)
    val Playhead = Color(0xFFFFFFFF)

    val Brand: Brush get() = Brush.linearGradient(listOf(Violet, Blue, Cyan))
    val BrandHorizontal: Brush get() = Brush.horizontalGradient(listOf(Violet, Blue, Cyan))
}

object Dims {
    val Touch = 48.dp        // zona táctil mínima de acciones importantes
    val Radius = 14.dp
    val RadiusSmall = 8.dp
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
            medium = RoundedCornerShape(12.dp),
            large = RoundedCornerShape(Dims.Radius),
            extraLarge = RoundedCornerShape(24.dp),
        ),
        content = content,
    )
}
