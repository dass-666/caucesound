package com.caucesound.app.ui

import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.data.Catalog
import com.caucesound.app.data.InsertState
import com.caucesound.app.data.ParamInfo
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.TrackState
import kotlin.math.roundToInt

fun effectName(type: String): String = when (type) {
    "cs.gain" -> tr("Ganancia", "Gain")
    "cs.filter" -> tr("Filtro", "Filter")
    "cs.eq" -> "EQ"
    "cs.saturation" -> tr("Saturación", "Saturation")
    "cs.compressor" -> tr("Compresor", "Compressor")
    "cs.delay" -> "Delay"
    "cs.reverb" -> "Reverb"
    else -> type
}

internal fun effectColor(type: String): Color = when (type) {
    "cs.filter", "cs.eq" -> Palette.Blue
    "cs.saturation", "cs.compressor" -> Palette.Solo
    "cs.delay", "cs.reverb" -> Palette.Violet
    else -> Palette.Cyan
}

/** Formato legible según el identificador del parámetro (sufijos Db, Hz, Ms; casos especiales). */
fun formatParam(id: String, v: Float, p: ParamInfo): String = when {
    id == "mode" -> if (v >= 0.5f) "HP" else "LP"
    id == "pingPong" -> if (v >= 0.5f) "On" else "Off"
    id == "ratio" -> "%.1f:1".format(v)
    id.endsWith("Db") -> "%+.1f dB".format(v)
    id.endsWith("Hz") -> if (v >= 1000) "%.1f k".format(v / 1000) else "${v.roundToInt()} Hz"
    id.endsWith("Ms") -> if (v >= 1000) "%.2f s".format(v / 1000) else if (v < 10) "%.1f ms".format(v) else "${v.roundToInt()} ms"
    p.min == 0f && p.max <= 1f -> "${(v * 100).roundToInt()}%"
    else -> "%.2f".format(v)
}

/** Cadena de efectos de una pista: añadir, ajustar, bypass, reordenar y quitar. Los cambios suenan al instante. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun EffectsPanel(state: ProjectState, track: TrackState, catalog: Catalog, actions: StudioActions) {
    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text("Cauce FX", fontWeight = FontWeight.Bold, fontSize = 18.sp)
        Text(track.name + " · " + tr("${track.inserts.size} de 8 efectos, en serie", "${track.inserts.size} of 8 effects, in series"), color = Palette.TextDim, fontSize = 12.sp)
        // Selector de pista (las mismas que en el mezclador).
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            state.tracks.forEach { t -> PillToggle(t.name, t.id == track.id, { actions.selectTrack(t.id) }, color = Color(t.color.toInt()), height = 40.dp) }
        }
        if (track.inserts.isEmpty())
            Text(tr("Sin efectos. Añade uno abajo: se procesa después del instrumento o del audio de la pista.",
                "No effects. Add one below: it runs after the track's instrument or audio."), color = Palette.TextDim, fontSize = 13.sp)
        track.inserts.forEachIndexed { i, ins -> InsertCard(ins, i, track.inserts.size, catalog, actions) }
        if (track.inserts.size < 8) {
            Text(tr("Añadir efecto", "Add effect"), color = Palette.TextFaint, fontSize = 12.sp)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                catalog.effects.forEach { e ->
                    PillToggle(effectName(e.type), false, { actions.addInsert(track.id, e.type) }, color = effectColor(e.type), height = 40.dp, icon = Icons.Default.Add)
                }
            }
        }
        Text(tr("Efectos propios de CAUCESOUND (sin código ni muestras de terceros). Latencia 0.",
            "CAUCESOUND's own effects (no third-party code or samples). Zero latency."), fontSize = 11.sp, color = Palette.TextFaint)
    }
}

@Composable
private fun InsertCard(ins: InsertState, index: Int, count: Int, catalog: Catalog, actions: StudioActions) {
    SectionCard("${index + 1}. ${effectName(ins.type)}") {
        Row(verticalAlignment = Alignment.CenterVertically) {
            PillToggle(if (ins.bypass) "Bypass" else "On", !ins.bypass, { actions.setInsertBypass(ins.id, !ins.bypass) }, color = effectColor(ins.type), height = 36.dp)
            Spacer(Modifier.weight(1f))
            IconButton(onClick = { actions.moveInsert(ins.id, index - 1) }, enabled = index > 0) { Icon(Icons.Default.KeyboardArrowUp, tr("Subir", "Move up")) }
            IconButton(onClick = { actions.moveInsert(ins.id, index + 1) }, enabled = index < count - 1) { Icon(Icons.Default.KeyboardArrowDown, tr("Bajar", "Move down")) }
            IconButton(onClick = { actions.removeInsert(ins.id) }) { Icon(Icons.Default.Delete, tr("Quitar", "Remove"), tint = Palette.Record) }
        }
        InsertControls(ins, catalog, actions, effectColor(ins.type))
    }
}

/** Perillas reales de una inserción (las que publica el motor). Se usa en Cauce FX y en las ranuras del Mixer Pro. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun InsertControls(ins: InsertState, catalog: Catalog, actions: StudioActions, color: Color = effectColor(ins.type)) {
    val info = catalog.effect(ins.type)
    // Valores locales mientras se gira la perilla (el estado del motor se refresca al soltar).
    val local = remember(ins) { mutableStateMapOf<String, Float>() }
    Column {
        if (info == null) { Text(tr("Efecto no disponible en esta versión: se conserva en el proyecto sin sonar.", "Effect not available in this version: kept in the project, silent."), color = Palette.TextDim); return@Column }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            info.params.forEach { p ->
                val v = local[p.id] ?: ins.params[p.id] ?: p.default
                val binary = p.id == "mode" || p.id == "pingPong"
                if (binary) {
                    Column(Modifier.width(76.dp).padding(top = 12.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                        PillToggle(formatParam(p.id, v, p), v >= 0.5f, {
                            val nv = if (v >= 0.5f) 0f else 1f
                            local[p.id] = nv; actions.setInsertParam(ins.id, p.id, nv); actions.commitMixer()
                        }, color = color, height = 40.dp)
                        Text(p.name.substringBefore(" ("), fontSize = 11.sp, color = Palette.TextDim)
                    }
                } else {
                    val log = p.id.endsWith("Hz") || (p.id.endsWith("Ms") && p.max / p.min.coerceAtLeast(0.01f) > 50)
                    Knob(p.name, v, p.min, p.max, p.default, { local[p.id] = it; actions.setInsertParam(ins.id, p.id, it) }, onEnd = actions::commitMixer,
                        log = log, color = color, format = { formatParam(p.id, it, p) })
                }
            }
        }
        if (ins.bypass) Text(tr("En bypass: la pista suena sin este efecto.", "Bypassed: the track plays without this effect."), fontSize = 11.sp, color = Palette.TextFaint)
    }
}
