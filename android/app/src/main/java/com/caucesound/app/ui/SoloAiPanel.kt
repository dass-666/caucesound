package com.caucesound.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.AutoAwesome
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Piano
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Shuffle
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.data.BeatAnalysis
import com.caucesound.app.data.Confidence
import com.caucesound.app.data.KeyNames
import com.caucesound.app.data.PPQ
import com.caucesound.app.data.PluginUi
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.SAMPLE_RATE
import com.caucesound.app.data.SoloFamily
import com.caucesound.app.data.SoloRequest
import kotlin.math.roundToInt
import kotlin.random.Random

private fun keyLabel(root: Int, minor: Boolean) = KeyNames.name(root) + if (minor) tr(" menor", " minor") else tr(" mayor", " major")
private fun confLabel(v: Double) = when (Confidence.of(v)) {
    Confidence.HIGH -> tr("confianza alta", "high confidence"); Confidence.MEDIUM -> tr("confianza media", "medium confidence"); else -> tr("confianza baja", "low confidence")
}
private fun confColor(v: Double) = when (Confidence.of(v)) { Confidence.HIGH -> Palette.Ok; Confidence.MEDIUM -> Palette.Warn; else -> Palette.Record }

/**
 * Solo AI (plugin integrado como submódulo). Tres pasos, todos en el teléfono y sin red:
 *  1. Tonalidad: la del estilo de la canción, la que confirma el usuario o la estimada al analizar un beat importado
 *     (con su confianza; nada se aplica sin confirmar y el tempo nunca cambia solo).
 *  2. Pedir cuatro alternativas A-D de melodía o solo.
 *  3. Escucharlas, elegir, variar y abrir la elegida en el Piano roll: siempre son notas MIDI editables.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SoloAiPanel(state: ProjectState, selectedClip: String?, plugins: PluginUi, actions: StudioActions) {
    val plan = state.music?.takeIf { it.keyRoot in 0..11 }
    val harmony = state.harmony
    val hasKey = plan != null || harmony != null
    var type by remember { mutableIntStateOf(0) }
    var character by remember { mutableIntStateOf(0) }
    var bars by remember { mutableIntStateOf(1) }
    var freedom by remember { mutableStateOf(35f) }
    var sound by remember { mutableIntStateOf(0) }
    var manualKey by remember { mutableStateOf(false) }
    val types = listOf("chorus" to tr("Estribillo", "Hook"), "solo" to "Solo", "interlude" to tr("Interludio", "Interlude"), "intro" to "Intro",
        "outro" to tr("Final", "Outro"), "background" to tr("Fondo", "Background"))
    val characters = listOf("easy" to tr("Fácil de cantar", "Easy to sing"), "emotional" to tr("Emotiva", "Emotional"), "energetic" to tr("Enérgica", "Energetic"))
    val sounds = listOf("bright_lead" to tr("Lead brillante", "Bright lead"), "dark_bell" to tr("Campana", "Bell"), "urban_pluck" to "Pluck", "warm_keys" to tr("Teclas", "Keys"))
    val audioClips = state.tracks.filter { it.isAudio }.flatMap { t -> t.clips.map { t to it } }
    val analyzing = plugins.job?.takeIf { it.type == "soloAnalyze" && it.active }

    Column(Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
        PanelHeader("Solo AI", tr("Melodías y solos como notas editables", "Melodies and solos as editable notes"))

        // --- 1. Tonalidad ---
        Card {
            Text(tr("Tonalidad", "Key"), fontWeight = FontWeight.Bold)
            when {
                plan != null -> Text(tr("${keyLabel(plan.keyRoot, plan.minor)}, con los acordes del estilo de esta canción.", "${keyLabel(plan.keyRoot, plan.minor)}, with this song's style chords."),
                    color = Palette.TextDim, fontSize = 13.sp, modifier = Modifier.testTag("solo_key_plan"))
                harmony != null -> Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(tr("${keyLabel(harmony.keyRoot, harmony.minor)}, confirmada por ti", "${keyLabel(harmony.keyRoot, harmony.minor)}, confirmed by you") +
                        if (harmony.chords > 0) tr(" · ${harmony.chords} compases de acordes detectados", " · ${harmony.chords} bars of detected chords") else "",
                        Modifier.weight(1f).testTag("solo_key_confirmed"), color = Palette.TextDim, fontSize = 13.sp)
                    CauceButton(tr("Cambiar", "Change"), { manualKey = true }, kind = ButtonKind.Ghost, height = 40.dp)
                }
                else -> Text(tr("Esta canción no tiene tonalidad guardada. Analiza el beat importado o elígela tú: Solo AI no la adivina.",
                    "This song has no saved key. Analyze the imported beat or pick it yourself: Solo AI does not guess it."), color = Palette.TextDim, fontSize = 13.sp)
            }
            if (plan == null) {
                if (audioClips.isNotEmpty() && plugins.analysis == null) {
                    val target = audioClips.firstOrNull { it.second.id == selectedClip } ?: audioClips.maxBy { it.second.length }
                    if (analyzing != null) {
                        Text(tr("Analizando «${target.second.name}»…", "Analyzing “${target.second.name}”…"), fontSize = 13.sp, modifier = Modifier.testTag("solo_analyzing"))
                        LinearProgressIndicator(Modifier.fillMaxWidth(), color = Palette.Violet, trackColor = Palette.SurfaceTop)
                    } else CauceButton(tr("Analizar «${target.second.name.take(22)}»", "Analyze “${target.second.name.take(22)}”"), { actions.soloAnalyze(target.second.id) },
                        Modifier.fillMaxWidth().testTag("solo_analyze"), icon = Icons.Default.Search)
                    if (audioClips.size > 1 && analyzing == null) Text(tr("Para analizar otro audio, selecciónalo antes en el arreglo.", "To analyze a different audio, select it in the arrangement first."),
                        color = Palette.TextFaint, fontSize = 11.sp)
                }
                plugins.analysis?.let { AnalysisCard(state, it, actions) }
                if (plugins.analysis == null && (manualKey || (!hasKey && analyzing == null))) ManualKey(harmony?.keyRoot ?: -1, harmony?.minor ?: true) { k, m -> manualKey = false; actions.confirmAnalysis(k, m, null, false) }
            }
        }

        // --- 2. Qué generar ---
        Card {
            Text(tr("Qué quieres", "What you want"), fontWeight = FontWeight.Bold)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                types.forEachIndexed { i, t -> PillToggle(t.second, type == i, { type = i }, color = Palette.Violet, height = 40.dp, modifier = Modifier.testTag("solo_type_${t.first}")) }
            }
            if (type == 0) Segmented(characters.map { it.second }, character, { character = it }, Modifier.fillMaxWidth())
            Segmented(listOf(tr("4 compases", "4 bars"), tr("8 compases", "8 bars"), tr("16 compases", "16 bars")), bars, { bars = it }, Modifier.fillMaxWidth())
            Text(tr("Sonido de la pista", "Track sound"), color = Palette.TextFaint, fontSize = 12.sp)
            FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                sounds.forEachIndexed { i, s -> PillToggle(s.second, sound == i, { sound = i }, color = Palette.Blue, height = 36.dp) }
            }
            LabeledSlider(tr("Libertad de las variaciones: ", "Variation freedom: ") + when {
                freedom <= 25 -> tr("muy parecida", "very close"); freedom <= 50 -> tr("misma idea", "same idea"); freedom <= 75 -> tr("frases nuevas", "new phrases"); else -> tr("experimental", "experimental")
            }, freedom, 0f, 100f, "%.0f", "solo_freedom") { freedom = it.roundToInt().toFloat() }
            CauceButton(tr("Generar 4 alternativas", "Generate 4 alternatives"), {
                actions.soloProposals(SoloRequest(
                    startBar = if (plan != null) 2 else 0, bars = listOf(4, 8, 16)[bars], type = types[type].first, character = characters[character].first,
                    freedom = freedom.roundToInt(), seed = Random.nextLong(1, 0x7FFFFFFFL), keyRoot = null, minor = plan?.minor ?: harmony?.minor ?: true, preset = sounds[sound].first))
            }, Modifier.fillMaxWidth().testTag("solo_generate"), kind = ButtonKind.Primary, enabled = hasKey && !state.recording, icon = Icons.Default.AutoAwesome)
            if (!hasKey) Text(tr("Falta la tonalidad (paso anterior).", "The key is missing (previous step)."), color = Palette.Warn, fontSize = 12.sp)
        }

        // --- 3. Alternativas ---
        plugins.family?.let { fam -> Alternatives(state, fam, actions) }
        Spacer(Modifier.height(4.dp))
    }
}

@Composable
private fun Card(content: @Composable androidx.compose.foundation.layout.ColumnScope.() -> Unit) =
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.Radius)).background(Palette.Surface).padding(12.dp), verticalArrangement = Arrangement.spacedBy(8.dp), content = content)

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ManualKey(initialRoot: Int, initialMinor: Boolean, onSave: (Int, Boolean) -> Unit) {
    var root by remember { mutableIntStateOf(initialRoot) }
    var minor by remember { mutableStateOf(initialMinor) }
    FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
        KeyNames.all.forEachIndexed { i, n -> PillToggle(n, root == i, { root = i }, color = Palette.Cyan, height = 40.dp, minWidth = 46.dp, modifier = Modifier.testTag("solo_key_$i")) }
    }
    Segmented(listOf(tr("Mayor", "Major"), tr("Menor", "Minor")), if (minor) 1 else 0, { minor = it == 1 }, Modifier.fillMaxWidth())
    CauceButton(tr("Guardar tonalidad", "Save key"), { onSave(root, minor) }, Modifier.fillMaxWidth().testTag("solo_key_save"), enabled = root >= 0, icon = Icons.Default.Check)
}

/** Resultado del análisis: estimaciones con su confianza. El usuario confirma o corrige; por defecto el tempo NO cambia. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun AnalysisCard(state: ProjectState, a: BeatAnalysis, actions: StudioActions) {
    var bpmChoice by remember(a) { mutableIntStateOf(-1) }      // -1 = no cambiar el tempo del proyecto
    var root by remember(a) { mutableIntStateOf(if (a.tonal) a.keyRoot else -1) }
    var minor by remember(a) { mutableStateOf(a.minor) }
    var useChords by remember(a) { mutableStateOf(false) }
    Column(Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.RadiusMedium)).background(Palette.SurfaceHigh).padding(10.dp).testTag("solo_analysis"), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(tr("Resultado del análisis (estimación)", "Analysis result (estimate)"), fontWeight = FontWeight.Bold)
        if (a.silent) Text(tr("El audio está en silencio: no hay nada que analizar.", "The audio is silent: nothing to analyze."), color = Palette.Warn, fontSize = 13.sp)

        // Tempo
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(tr("Tempo probable: %.1f BPM", "Likely tempo: %.1f BPM").format(a.bpm), Modifier.weight(1f))
            ConfidenceChip(a.bpmConfidence)
        }
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            PillToggle(tr("No cambiar (%.0f BPM)", "Keep (%.0f BPM)").format(state.bpm), bpmChoice == -1, { bpmChoice = -1; useChords = false }, color = Palette.Blue, height = 40.dp, modifier = Modifier.testTag("solo_bpm_keep"))
            a.bpmCandidates.forEachIndexed { i, b ->
                PillToggle(tr("Usar %.1f", "Use %.1f").format(b), bpmChoice == i, { bpmChoice = i }, color = Palette.Blue, height = 40.dp, modifier = Modifier.testTag("solo_bpm_$i"))
            }
        }
        Text(tr("El tempo de la canción solo cambia si eliges uno aquí. El análisis puede dar la mitad o el doble del tempo real.",
            "The song tempo only changes if you pick one here. The analysis may return half or double the real tempo."), color = Palette.TextFaint, fontSize = 11.sp)

        // Tonalidad
        if (a.tonal) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(tr("Tonalidad probable: ", "Likely key: ") + keyLabel(a.keyRoot, a.minor), Modifier.weight(1f))
                ConfidenceChip(a.keyConfidence)
            }
            if (a.keyCandidates.size > 1) FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
                a.keyCandidates.take(4).forEach { k ->
                    PillToggle(keyLabel(k.root, k.minor), root == k.root && minor == k.minor, { root = k.root; minor = k.minor }, color = Palette.Cyan, height = 40.dp)
                }
            }
        } else Text(if (a.percussiveOnly) tr("Solo se detecta percusión: no se propone tonalidad. Elígela tú.", "Only percussion detected: no key is proposed. Pick it yourself.")
            else tr("No se detecta una tonalidad fiable. Elígela tú.", "No reliable key detected. Pick it yourself."), color = Palette.Warn, fontSize = 13.sp)
        Text(tr("Corregir a mano", "Correct by hand"), color = Palette.TextFaint, fontSize = 12.sp)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            KeyNames.all.forEachIndexed { i, n -> PillToggle(n, root == i, { root = i }, color = Palette.Cyan, height = 36.dp, minWidth = 44.dp, modifier = Modifier.testTag("solo_fix_key_$i")) }
        }
        Segmented(listOf(tr("Mayor", "Major"), tr("Menor", "Minor")), if (minor) 1 else 0, { minor = it == 1 }, Modifier.fillMaxWidth())

        if (a.chords.isNotEmpty()) Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(tr("Usar los acordes detectados (${a.chords.size} compases)", "Use the detected chords (${a.chords.size} bars)"), fontSize = 13.sp)
                Text(confLabel(a.chordConfidence) + tr(" · solo si adoptas el tempo detectado", " · only if you adopt the detected tempo"), color = Palette.TextFaint, fontSize = 11.sp)
            }
            PillToggle(if (useChords) tr("Sí", "Yes") else "No", useChords, { if (bpmChoice >= 0) useChords = !useChords }, color = Palette.Cyan, modifier = Modifier.testTag("solo_use_chords"))
        }
        a.warnings.forEach { Text("• $it", color = Palette.Warn, fontSize = 12.sp) }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CauceButton(tr("Descartar", "Discard"), actions::dismissAnalysis, Modifier.weight(1f))
            CauceButton(tr("Confirmar", "Confirm"), { actions.confirmAnalysis(root, minor, a.bpmCandidates.getOrNull(bpmChoice), useChords && bpmChoice >= 0); actions.dismissAnalysis() },
                Modifier.weight(1f).testTag("solo_confirm"), kind = ButtonKind.Primary, enabled = root >= 0, icon = Icons.Default.Check)
        }
    }
}

@Composable
private fun ConfidenceChip(v: Double) {
    Row(Modifier.clip(RoundedCornerShape(10.dp)).background(confColor(v).copy(alpha = 0.16f)).padding(horizontal = 8.dp, vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(6.dp).clip(CircleShape).background(confColor(v)))
        Spacer(Modifier.size(5.dp))
        Text(confLabel(v) + " (%.0f %%)".format(v * 100), fontSize = 11.sp, color = confColor(v))
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun Alternatives(state: ProjectState, fam: SoloFamily, actions: StudioActions) {
    val track = state.tracks.firstOrNull { it.id == fam.trackId }
    Card {
        Text(tr("Alternativas", "Alternatives"), fontWeight = FontWeight.Bold)
        Text(tr("Toca una para ponerla en la pista y pulsa Escuchar. Las cuatro salen del mismo motivo; lo que cambia es la forma de desarrollarlo.",
            "Tap one to put it on the track, then press Listen. All four come from the same motif; what changes is how it is developed."), color = Palette.TextDim, fontSize = 12.sp)
        fam.proposals.forEachIndexed { i, p ->
            val on = i == fam.selected
            Row(
                Modifier.fillMaxWidth().clip(RoundedCornerShape(Dims.RadiusMedium)).background(if (on) Palette.Violet.copy(alpha = 0.18f) else Palette.SurfaceHigh)
                    .border(1.dp, if (on) Palette.Violet else Color.Transparent, RoundedCornerShape(Dims.RadiusMedium)).clickable { actions.soloPick(i) }
                    .padding(10.dp).testTag("solo_alt_$i").semantics { contentDescription = p.name },
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.size(36.dp).clip(CircleShape).background(if (on) Palette.Violet else Palette.SurfaceTop), contentAlignment = Alignment.Center) {
                    Text("ABCD".getOrElse(i) { '?' }.toString(), fontWeight = FontWeight.Bold)
                }
                Column(Modifier.weight(1f).padding(start = 10.dp)) {
                    Text(p.name.substringAfter("· ").ifBlank { p.name }, fontWeight = FontWeight.SemiBold, maxLines = 1)
                    if (p.description.isNotBlank()) Text(p.description, color = Palette.TextDim, fontSize = 12.sp, maxLines = 2)
                    Text(tr("${p.notes} notas · ${(p.inScale * 100).roundToInt()} % en la escala · ${(p.similarity * 100).roundToInt()} % parecida al motivo",
                        "${p.notes} notes · ${(p.inScale * 100).roundToInt()} % in scale · ${(p.similarity * 100).roundToInt()} % like the motif"), color = Palette.TextFaint, fontSize = 11.sp)
                }
                if (on) androidx.compose.material3.Icon(Icons.Default.Check, tr("Elegida", "Selected"), tint = Palette.VioletSoft)
            }
        }
        if (fam.variation != 0L) Text(tr("Suena una variación de la alternativa ${"ABCD".getOrElse(fam.selected) { '?' }}. Tócala de nuevo para volver a la original.",
            "A variation of alternative ${"ABCD".getOrElse(fam.selected) { '?' }} is playing. Tap it again to return to the original."), color = Palette.TextDim, fontSize = 12.sp)
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            CauceButton(tr("Escuchar", "Listen"), {
                actions.seek(state.ticksToFrames(fam.request.startBar.toLong() * PPQ * state.beatsPerBar)); actions.play()
            }, Modifier.weight(1f).testTag("solo_listen"), icon = Icons.Default.PlayArrow, enabled = track != null)
            CauceButton(tr("Variación", "Variation"), actions::soloVary, Modifier.weight(1f).testTag("solo_vary"), icon = Icons.Default.Shuffle, enabled = track != null && fam.selected >= 0)
        }
        // Controles creativos (0.6): cada uno cambia de verdad las notas de la pista y se puede deshacer.
        Text(tr("Ajustar esta melodía", "Adjust this melody"), color = Palette.TextDim, fontSize = 12.sp)
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            val ok = track != null && !state.recording
            CauceButton(tr("Menos notas", "Fewer notes"), { actions.soloTransform("simpler") }, Modifier.testTag("solo_simpler"), enabled = ok, height = 44.dp)
            CauceButton(tr("Más intensa", "More intense"), { actions.soloTransform("intense") }, Modifier.testTag("solo_intense"), enabled = ok, height = 44.dp)
            CauceButton(tr("Más suave", "Softer"), { actions.soloTransform("soft") }, Modifier.testTag("solo_soft"), enabled = ok, height = 44.dp)
            CauceButton(tr("Octava arriba", "Octave up"), { actions.soloTransform("octaveUp") }, Modifier.testTag("solo_octave_up"), enabled = ok, height = 44.dp)
            CauceButton(tr("Octava abajo", "Octave down"), { actions.soloTransform("octaveDown") }, Modifier.testTag("solo_octave_down"), enabled = ok, height = 44.dp)
            CauceButton(tr("Cambiar el final", "New ending"), actions::soloNewEnding, Modifier.testTag("solo_new_ending"), enabled = ok && fam.selected >= 0, height = 44.dp)
        }
        CauceButton(tr("Abrir en el Piano roll", "Open in Piano roll"), { track?.let { t -> actions.selectTrack(t.id); t.midiClips.firstOrNull()?.let { actions.selectClip(it.id) }; actions.openPanel(Panel.PIANO) } },
            Modifier.fillMaxWidth().testTag("solo_open_piano"), kind = ButtonKind.Primary, icon = Icons.Default.Piano, enabled = track != null)
        Text(if (track != null) tr("Está en la pista «${track.name}». Deshacer vuelve a la alternativa anterior.", "It is on track “${track.name}”. Undo returns to the previous alternative.")
            else tr("La pista de Solo AI ya no existe: genera de nuevo o elige una alternativa para crearla.", "The Solo AI track no longer exists: generate again or pick an alternative to create it."),
            color = Palette.TextFaint, fontSize = 11.sp)
    }
}
