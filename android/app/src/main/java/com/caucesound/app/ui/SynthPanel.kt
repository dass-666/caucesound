package com.caucesound.app.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.KeyboardArrowLeft
import androidx.compose.material.icons.filled.KeyboardArrowRight
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.PointerId
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.data.Catalog
import com.caucesound.app.data.MusicTime
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.TrackState
import kotlin.math.roundToInt

private val WAVES = listOf("Sine", "Tri", "Saw", "Square")

@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SynthPanel(state: ProjectState, track: TrackState, catalog: Catalog, actions: StudioActions) {
    val slot = state.trackIndex(track.id)
    fun v(id: String) = track.params[id] ?: catalog.synthParam(id)?.default ?: 0f
    @Composable
    fun K(id: String, label: String, log: Boolean = false, color: Color = Palette.Cyan, format: (Float) -> String) {
        val info = catalog.synthParam(id) ?: return
        Knob(label, v(id), info.min, info.max, info.default, { actions.setParam(track.id, id, it) }, onEnd = actions::commitMixer, log = log, color = color, format = format)
    }
    val ms: (Float) -> String = { if (it >= 1000) "%.1f s".format(it / 1000) else "${it.roundToInt()} ms" }
    Column(Modifier.fillMaxSize()) {
        Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text("Cauce Synth", fontWeight = FontWeight.Bold, fontSize = 18.sp)
                    Text(track.name + " · " + (catalog.synthPresets.firstOrNull { it.id == track.preset }?.name ?: tr("Ajustes propios", "Custom")), color = Palette.TextDim, fontSize = 12.sp)
                }
            }
            Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                catalog.synthPresets.forEach { p -> PillToggle(p.name, p.id == track.preset, { actions.loadPreset(track.id, p.id) }, height = 40.dp) }
            }
            SectionCard(tr("Osciladores", "Oscillators")) {
                Text("Osc 1", fontSize = 12.sp, color = Palette.TextDim)
                Segmented(WAVES, v("osc1Wave").roundToInt(), { actions.setParam(track.id, "osc1Wave", it.toFloat()); actions.commitMixer() })
                Spacer(Modifier.height(8.dp))
                Text("Osc 2", fontSize = 12.sp, color = Palette.TextDim)
                Segmented(WAVES, v("osc2Wave").roundToInt(), { actions.setParam(track.id, "osc2Wave", it.toFloat()); actions.commitMixer() })
                Spacer(Modifier.height(8.dp))
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    K("osc2Mix", "Mix") { "${(it * 100).roundToInt()}%" }
                    K("osc2Detune", "Detune") { "%+.0f c".format(it) }
                    K("osc2Octave", "Oct 2") { "%+d".format(it.roundToInt()) }
                    K("octave", tr("Octava", "Octave")) { "%+d".format(it.roundToInt()) }
                }
            }
            SectionCard(tr("Filtro", "Filter")) {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    K("cutoffHz", tr("Corte", "Cutoff"), log = true, color = Palette.Violet) { if (it >= 1000) "%.1f k".format(it / 1000) else "${it.roundToInt()} Hz" }
                    K("resonance", "Res", color = Palette.Violet) { "${(it * 100).roundToInt()}%" }
                    K("filterEnv", "Env", color = Palette.Violet) { "${(it * 100).roundToInt()}%" }
                    K("filterDecayMs", "Env decay", log = true, color = Palette.Violet, format = ms)
                }
            }
            SectionCard(tr("Envolvente", "Envelope")) {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    K("attackMs", "Attack", log = true, color = Palette.Blue, format = ms)
                    K("decayMs", "Decay", log = true, color = Palette.Blue, format = ms)
                    K("sustain", "Sustain", color = Palette.Blue) { "${(it * 100).roundToInt()}%" }
                    K("releaseMs", "Release", log = true, color = Palette.Blue, format = ms)
                }
            }
            SectionCard(tr("Salida", "Output")) {
                FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    K("volumeDb", tr("Volumen", "Volume")) { dbLabel(it) + " dB" }
                    K("drive", "Drive") { "${(it * 100).roundToInt()}%" }
                    K("glideMs", "Glide", format = ms)
                    K("polyphony", tr("Voces", "Voices")) { "${it.roundToInt()}" }
                    Box(Modifier.padding(top = 12.dp)) {
                        PillToggle("Mono", v("mono") >= 0.5f, { actions.setParam(track.id, "mono", if (v("mono") >= 0.5f) 0f else 1f); actions.commitMixer() }, height = 40.dp)
                    }
                }
            }
            Text(tr("Sonidos de síntesis sustractiva: los nombres describen el carácter, no imitan instrumentos acústicos.",
                "Subtractive synthesis sounds: names describe the character, not acoustic instruments."), fontSize = 11.sp, color = Palette.TextFaint)
        }
        PianoKeyboard(slot, actions, Modifier.fillMaxWidth().height(132.dp))
    }
}

/** Teclado de dos octavas con multitáctil: cada dedo es una nota (Note On al tocar, Note Off al soltar). */
@Composable
fun PianoKeyboard(slot: Int, actions: StudioActions, modifier: Modifier = Modifier) {
    var baseOctave by remember { mutableIntStateOf(4) }
    val held = remember { mutableStateMapOf<PointerId, Int>() }
    val act by rememberUpdatedState(actions)
    Column(modifier) {
        Row(verticalAlignment = Alignment.CenterVertically, modifier = Modifier.padding(horizontal = 4.dp)) {
            IconButton(onClick = { baseOctave = (baseOctave - 1).coerceAtLeast(0) }) { Icon(Icons.Default.KeyboardArrowLeft, tr("Octava abajo", "Octave down")) }
            Text("C$baseOctave – C${baseOctave + 2}", fontSize = 12.sp, color = Palette.TextDim)
            IconButton(onClick = { baseOctave = (baseOctave + 1).coerceAtMost(8) }) { Icon(Icons.Default.KeyboardArrowRight, tr("Octava arriba", "Octave up")) }
        }
        val first = (baseOctave + 1) * 12 // C de la octava base
        Canvas(Modifier.fillMaxWidth().weight(1f).pointerInput(baseOctave, slot) {
            val whites = 15
            fun keyAt(o: Offset): Int {
                val ww = size.width / whites.toFloat()
                val wi = (o.x / ww).toInt().coerceIn(0, whites - 1)
                val whiteKey = first + wi / 7 * 12 + intArrayOf(0, 2, 4, 5, 7, 9, 11)[wi % 7]
                if (o.y < size.height * 0.6f) {
                    // ¿negra encima?
                    val frac = o.x / ww - wi
                    if (frac < 0.3f && wi % 7 !in intArrayOf(0, 3) && wi > 0) return whiteKey - 1
                    if (frac > 0.7f && wi % 7 !in intArrayOf(2, 6)) return whiteKey + 1
                }
                return whiteKey
            }
            awaitEachGesture {
                awaitFirstDown(requireUnconsumed = false)
                do {
                    val ev = awaitPointerEvent()
                    for (c in ev.changes) {
                        val k = if (c.pressed) keyAt(c.position).coerceIn(0, 127) else -1
                        val prev = held[c.id]
                        if (prev != null && prev != k) { act.liveNote(slot, prev, 0, false); held.remove(c.id) }
                        if (k >= 0 && prev != k) {
                            val vel = (60 + (c.position.y / size.height) * 67).toInt().coerceIn(1, 127) // más abajo = más fuerte
                            act.liveNote(slot, k, vel, true); held[c.id] = k
                        }
                        c.consume()
                    }
                } while (ev.changes.any { it.pressed })
                held.values.forEach { act.liveNote(slot, it, 0, false) }
                held.clear()
            }
        }) {
            val whites = 15
            val ww = size.width / whites
            val down = held.values.toSet()
            for (i in 0 until whites) {
                val key = first + i / 7 * 12 + intArrayOf(0, 2, 4, 5, 7, 9, 11)[i % 7]
                drawRoundRect(if (key in down) Palette.Cyan else Color(0xFFE4E7EE), Offset(i * ww + 1f, 0f), Size(ww - 2f, size.height), CornerRadius(8f))
            }
            for (i in 0 until whites - 1) {
                if (i % 7 in intArrayOf(2, 6)) continue
                val key = first + i / 7 * 12 + intArrayOf(0, 2, 4, 5, 7, 9, 11)[i % 7] + 1
                if (MusicTime.isBlack(key)) drawRoundRect(if (key in down) Palette.Violet else Color(0xFF16181E), Offset((i + 0.7f) * ww, 0f), Size(ww * 0.6f, size.height * 0.6f), CornerRadius(6f))
            }
        }
    }
}
