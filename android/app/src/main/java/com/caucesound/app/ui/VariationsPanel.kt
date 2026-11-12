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
            SoloAiSection(state, actions)
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
    fun vary(part: String, energy: Float? = null) {
        val seed = if (energy != null) 0L else Random.nextLong(1, 0xFFFFFFFFL)
        if (seed != 0L) lastSeed = seed
        actions.varyPart(part, seed, energy)
    }
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(tr("Variaciones", "Variations"), fontWeight = FontWeight.Bold, fontSize = 18.sp)
        Text(tr("Genera otra versión con la misma tonalidad y acordes. Todo queda como notas y patrones que puedes editar. " +
            "Sustituye las notas de las pistas sin candado: bloquea las que hayas retocado a mano.",
            "Generate another version in the same key and chords. Everything stays as notes and patterns you can edit. " +
            "It replaces the notes of unlocked tracks: lock the ones you edited by hand."), color = Palette.TextDim, fontSize = 12.sp)

        CauceButton(tr("Otra versión completa", "New full version"), { vary("all") }, Modifier.fillMaxWidth().testTag("vary_all"), kind = ButtonKind.Primary, icon = Icons.Default.AutoAwesome)
        Text(tr("Cambiar solo una parte", "Change one part only"), color = Palette.TextFaint, fontSize = 12.sp)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf("drums", "bass", "chords", "melody", "pad").filter { plan.hasPart(it) }.forEach { part ->
                PillToggle(partName(part), false, { vary(part) }, color = Palette.Violet, height = 40.dp, modifier = Modifier.testTag("vary_$part"))
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
        if (soloAvailable) SoloAiSection(state, actions)
    }
}

/**
 * Solo AI (plugin integrado como submódulo). Compone en el teléfono, sin red, y el resultado entra como pista nueva
 * con un clip MIDI editable; deshacer la quita. Si la canción no tiene plan musical, la tonalidad no es fiable y se
 * pide aquí (no se adivina).
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SoloAiSection(state: ProjectState, actions: StudioActions) {
    val needsKey = state.music == null
    var type by remember { androidx.compose.runtime.mutableIntStateOf(0) }
    var proposal by remember { androidx.compose.runtime.mutableIntStateOf(0) }
    var bars by remember { androidx.compose.runtime.mutableIntStateOf(1) }
    var keyRoot by remember { androidx.compose.runtime.mutableIntStateOf(-1) }
    var minor by remember { androidx.compose.runtime.mutableStateOf(true) }
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(Palette.Surface).padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(tr("Solo AI · Generar melodía o solo", "Solo AI · Generate melody or solo"), fontWeight = FontWeight.Bold)
        Text(if (needsKey) tr("Esta canción no tiene tonalidad guardada: elígela para que la melodía encaje.", "This song has no saved key: pick it so the melody fits.")
            else tr("Usa la tonalidad y los acordes de la canción. Se crea una pista nueva desde la sección A.", "Uses the song's key and chords. A new track is created from section A."),
            color = Palette.TextDim, fontSize = 12.sp)
        Segmented(listOf(tr("Estribillo", "Hook"), "Solo"), type, { type = it }, Modifier.fillMaxWidth())
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            listOf(tr("Sencilla", "Simple"), tr("Emotiva", "Emotional"), tr("Rítmica", "Rhythmic"), tr("Atrevida", "Bold")).forEachIndexed { i, label ->
                PillToggle(label, proposal == i, { proposal = i }, color = Palette.Violet, height = 36.dp)
            }
        }
        Segmented(listOf(tr("4 compases", "4 bars"), tr("8 compases", "8 bars"), tr("16 compases", "16 bars")), bars, { bars = it }, Modifier.fillMaxWidth())
        if (needsKey) {
            val names = listOf("Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si")
            FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                names.forEachIndexed { i, n -> PillToggle(n, keyRoot == i, { keyRoot = i }, color = Palette.Cyan, height = 36.dp, minWidth = 44.dp, modifier = Modifier.testTag("solo_key_$i")) }
            }
            Segmented(listOf(tr("Mayor", "Major"), tr("Menor", "Minor")), if (minor) 1 else 0, { minor = it == 1 }, Modifier.fillMaxWidth())
        }
        CauceButton(tr("Generar con Solo AI", "Generate with Solo AI"), {
            val n = listOf(4, 8, 16)[bars]
            actions.soloMelody(if (needsKey) 0 else 2, n, if (type == 1) "solo" else "chorus", proposal, Random.nextLong(1, 0xFFFFFFFFL),
                if (needsKey) keyRoot else null, minor)
        }, Modifier.fillMaxWidth().testTag("solo_generate"), kind = ButtonKind.Primary, enabled = !needsKey || keyRoot >= 0, icon = Icons.Default.AutoAwesome)
    }
}
