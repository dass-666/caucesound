package com.caucesound.app.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

object Palette {
    val Background = Color(0xFF0E1013)
    val Surface = Color(0xFF171A1F)
    val SurfaceHigh = Color(0xFF20242B)
    val Grid = Color(0xFF2A2F37)
    val GridStrong = Color(0xFF3A414C)
    val Accent = Color(0xFF4FC3F7)
    val Record = Color(0xFFFF4D4D)
    val Solo = Color(0xFFFFC94D)
    val Mute = Color(0xFF7D8590)
    val Text = Color(0xFFE6E8EB)
    val TextDim = Color(0xFF9AA3AE)
    val Playhead = Color(0xFFFFFFFF)
}

@Composable
fun CauceTheme(content: @Composable () -> Unit) {
    MaterialTheme(
        colorScheme = darkColorScheme(
            primary = Palette.Accent,
            onPrimary = Color.Black,
            secondary = Palette.Solo,
            background = Palette.Background,
            surface = Palette.Surface,
            surfaceVariant = Palette.SurfaceHigh,
            onBackground = Palette.Text,
            onSurface = Palette.Text,
            onSurfaceVariant = Palette.TextDim,
            error = Palette.Record,
        ),
        content = content,
    )
}
