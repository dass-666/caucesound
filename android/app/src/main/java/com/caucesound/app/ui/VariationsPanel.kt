package com.caucesound.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.LockOpen
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.data.ProjectState
import kotlin.random.Random

fun partName(part: String): String = when (part) {
    "drums" -> tr("Batería", "Drums")
    "bass" -> tr("Bajo", "Bass")
    "chords" -> tr("Acordes", "Chords")
    "melody" -> tr("Melodía", "Melody")
    "pad" -> "Pad"
    else -> tr("Todo", "Everything")
}

/**
 * Variaciones del compositor local (misión 0.4). Todo se calcula en el teléfono con reglas musicales y una semilla:
 * sin red ni modelos. El resultado son notas y patrones normales, editables en el Piano roll y la rejilla.
 * «Comparar» usa deshacer/rehacer: cada variación es un solo paso.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun VariationsPanel(state: ProjectState, actions: StudioActions, soloAvailable: Boolean = false) {
    val plan = state.music
    if (plan == null && soloAvailable) {
        Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Text(tr("Las variaciones funcionan en canciones creadas desde un estilo con la versión 0.4 o posterior. Aquí puedes usar Solo AI.",
                "Variations work on songs created from a style with version 0.4 or later. You can use Solo AI here."), color = Palette.TextDim, fontSize = 12.sp)
            SoloAiEntry(actions)
        }
        return
    }
    if (plan == null) {
        EmptyState(Icons.Default.AutoAwesome, tr("Este proyecto no tiene variaciones", "This project has no variations"),
            tr("Las variaciones funcionan en canciones creadas desde un estilo con la versión 0.4 o posterior. Tus proyectos anteriores siguen igual.",
                "Variations work on songs created from a style with version 0.4 or later. Your older projects are unchanged."), Modifier.fillMaxSize())
        return
    }
    var lastSeed by remember { mutableLongStateOf(0L) }
    fun newSeed(): Long = Random.nextLong(1, 0xFFFFFFFFL).also { lastSeed = it }
    fun vary(part: String, energy: Float? = null) {
        val seed = if (energy != null) 0L else newSeed()
        actions.varyPart(part, seed, energy)
    }
    val parts = listOf("drums", "bass", "chords", "melody", "pad").filter { plan.hasPart(it) }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(tr("Variaciones", "Variations"), fontWeight = FontWeight.Bold, fontSize = 18.sp)
        Text(tr("Todo queda como notas y patrones que puedes editar. Solo cambian las pistas sin candado: bloquea las que hayas retocado a mano.",
            "Everything stays as notes and patterns you can edit. Only unlocked tracks change: lock the ones you edited by hand."), color = Palette.TextDim, fontSize = 12.sp)

        // Misión 0.8: dos acciones con intención distinta. VARIAR retoca la idea actual (misma progresión y motivo,
        // con cambios pequeños); REGENERAR propone otra idea del mismo estilo, tono y tempo.
        Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(Palette.Surface).padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(tr("Variar", "Vary"), fontWeight = FontWeight.Bold)
            Text(tr("Conserva tu idea: mismos acordes y motivo, con detalles nuevos (adornos, golpes, inversiones).",
                "Keeps your idea: same chords and motif, with new details (fills, hits, voicings)."), color = Palette.TextDim, fontSize = 12.sp)
            CauceButton(tr("Variar todo", "Vary everything"), { actions.mutatePart("all", newSeed()) }, Modifier.fillMaxWidth().testTag("mutate_all"), kind = ButtonKind.Primary, icon = Icons.Default.AutoAwesome)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                parts.forEach { part ->
                    PillToggle(partName(part), false, { actions.mutatePart(part, newSeed()) }, color = Palette.Violet, height = 40.dp, modifier = Modifier.testTag("mutate_$part"))
                }
            }
        }
        Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(Palette.Surface).padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Text(tr("Regenerar", "Regenerate"), fontWeight = FontWeight.Bold)
            Text(tr("Otra idea del mismo estilo, tono y tempo: acordes, groove, bajo y melodía nuevos. Lo bloqueado se queda (y si bloqueas una parte armónica, los acordes no cambian).",
                "Another idea in the same style, key and tempo: new chords, groove, bass and melody. Locked parts stay (and if a harmonic part is locked, the chords don't change)."), color = Palette.TextDim, fontSize = 12.sp)
            CauceButton(tr("Regenerar beat", "Regenerate beat"), { actions.regenerateBeat(newSeed()) }, Modifier.fillMaxWidth().testTag("regenerate_beat"), icon = Icons.Default.AutoAwesome)
            Text(tr("Rehacer una sola parte (mismos acordes)", "Redo one part only (same chords)"), color = Palette.TextFaint, fontSize = 12.sp)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                PillToggle(tr("Todas", "All"), false, { vary("all") }, color = Palette.Violet, height = 40.dp, modifier = Modifier.testTag("vary_all"))
                parts.forEach { part ->
                    PillToggle(partName(part), false, { vary(part) }, color = Palette.Violet, height = 40.dp, modifier = Modifier.testTag("vary_$part"))
                }
            }
        }

        Text(tr("Intensidad", "Intensity"), color = Palette.TextFaint, fontSize = 12.sp)
        val level = when { plan.energy < -0.2f -> 0; plan.energy > 0.2f -> 2; else -> 1 }
        Segmented(listOf(tr("Más tranquila", "Calmer"), "Normal", tr("Más enérgica", "More energy")), level, { i ->
            vary("all", listOf(-0.6f, 0f, 0.6f)[i])
        }, Modifier.fillMaxWidth())

        Text(tr("Comparar", "Compare"), color = Palette.TextFaint, fontSize = 12.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CauceButton(tr("Versión anterior", "Previous"), actions::undo, Modifier.weight(1f), enabled = state.canUndo, icon = Icons.AutoMirrored.Filled.Undo)
            CauceButton(tr("Versión nueva", "Newer"), actions::redo, Modifier.weight(1f), enabled = state.canRedo, icon = Icons.AutoMirrored.Filled.Redo)
        }
        Text(tr("Pulsa Play y alterna: cada variación es un paso de deshacer.", "Press Play and switch: each variation is one undo step."), color = Palette.TextFaint, fontSize = 11.sp)
        if (lastSeed != 0L) Text(tr("Semilla de la última variación: $lastSeed", "Last variation seed: $lastSeed"), color = Palette.TextFaint, fontSize = 11.sp)

        Text(tr("Bloquear pistas (las variaciones no las tocan)", "Lock tracks (variations leave them alone)"), color = Palette.TextFaint, fontSize = 12.sp)
        state.tracks.filter { plan.roles.containsKey(it.id) }.forEach { t ->
            val locked = t.id in plan.locked
            Row(
                Modifier.fillMaxWidth().clip(RoundedCornerShape(12.dp)).background(Palette.SurfaceHigh).padding(start = 12.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                androidx.compose.foundation.layout.Box(Modifier.size(10.dp).clip(RoundedCornerShape(3.dp)).background(Color(t.color.toInt())))
                Column(Modifier.weight(1f).padding(horizontal = 10.dp, vertical = 6.dp)) {
                    Text(t.name, fontWeight = FontWeight.SemiBold, maxLines = 1)
                    Text(partName(plan.roles[t.id] ?: ""), color = Palette.TextDim, fontSize = 11.sp)
                }
                IconButton({ actions.setPartLocked(t.id, !locked) }, Modifier.testTag("lock_${t.id}")) {
                    Icon(if (locked) Icons.Default.Lock else Icons.Default.LockOpen,
                        if (locked) tr("Desbloquear ${t.name}", "Unlock ${t.name}") else tr("Bloquear ${t.name}", "Lock ${t.name}"),
                        tint = if (locked) Palette.Accent else Palette.TextDim)
                }
            }
        }
        if (soloAvailable) SoloAiEntry(actions)
    }
}

/** Acceso a Solo AI desde Variaciones (el generador vive en su propia pantalla). */
@Composable
private fun SoloAiEntry(actions: StudioActions) {
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(Palette.Surface).padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("Solo AI", fontWeight = FontWeight.Bold)
        Text(tr("Cuatro alternativas de melodía o solo sobre la tonalidad de la canción, como notas editables en una pista nueva.",
            "Four melody or solo alternatives in the song's key, as editable notes on a new track."), color = Palette.TextDim, fontSize = 12.sp)
        CauceButton(tr("Abrir Solo AI", "Open Solo AI"), { actions.openPanel(Panel.SOLO) }, Modifier.fillMaxWidth().testTag("open_solo"), icon = Icons.Default.AutoAwesome)
    }
}
