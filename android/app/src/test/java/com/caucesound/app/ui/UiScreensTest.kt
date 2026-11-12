package com.caucesound.app.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.ui.unit.dp
import androidx.compose.foundation.rememberScrollState
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.runtime.getValue
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performScrollToNode
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeUp
import androidx.compose.ui.test.doubleClick
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onAllNodesWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import com.caucesound.app.data.Catalog
import com.caucesound.app.data.PPQ
import com.caucesound.app.data.NoteState
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.StateParser
import com.github.takahirom.roborazzi.captureRoboImage
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

/**
 * Pruebas de interfaz en la JVM (Robolectric). Los datos son JSON reales generados por el motor C++
 * (prueba ui_fixtures_from_engine). Con -Pscreenshots se guardan capturas PNG renderizadas de verdad
 * (no maquetas) en app/build/outputs/roborazzi/. No sustituyen la prueba en un teléfono físico.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], qualifiers = "w411dp-h891dp-xxhdpi")
class UiScreensTest {
    @get:Rule val compose = createComposeRule()

    private fun res(name: String) = javaClass.classLoader!!.getResource("fixtures/$name")!!.readText()
    private val state: ProjectState by lazy { StateParser.state(res("state_trap.json"))!! }
    private val catalog: Catalog by lazy { Catalog.parse(res("catalog.json")) }

    /** Registra las acciones que la interfaz envía al motor. */
    private class Recorder : StudioActions {
        val calls = mutableListOf<String>()
        var lanes: List<List<Int>>? = null
        var notes: List<NoteState>? = null
        val live = mutableListOf<Triple<Int, Int, Boolean>>()
        override fun setPatternLanes(trackId: String, patternId: String, lanes: List<List<Int>>) { calls += "lanes"; this.lanes = lanes }
        override fun setClipNotes(clipId: String, notes: List<NoteState>) { calls += "notes"; this.notes = notes }
        override fun loadPreset(trackId: String, presetId: String) { calls += "preset:$presetId" }
        override fun setMute(id: String, on: Boolean) { calls += "mute:$id:$on" }
        override fun liveNote(slot: Int, key: Int, velocity: Int, on: Boolean) { live += Triple(key, velocity, on) }
        override fun setInsertBypass(insertId: String, bypass: Boolean) { calls += "bypass:$insertId:$bypass" }
        override fun play() { calls += "play" }
        override fun addPattern(trackId: String, copyFrom: String?) { calls += "addPattern:$copyFrom" }
        override fun addReverbBus() { calls += "addReverbBus" }
        override fun setSendLive(trackId: String, busId: String, level: Float) { calls += "send:$trackId:$busId" }
        override fun setPanLive(id: String, pan: Float) { calls += "pan:$id" }
        override fun varyPart(part: String, seed: Long, energy: Float?) { calls += "vary:$part:${seed != 0L}:$energy" }
        override fun setPartLocked(trackId: String, locked: Boolean) { calls += "lock:$trackId:$locked" }
        var vocal: com.caucesound.app.data.VocalParams? = null
        var request: com.caucesound.app.data.SoloRequest? = null
        override fun vocalProcess(clipId: String, params: com.caucesound.app.data.VocalParams) { calls += "vocal:$clipId"; vocal = params }
        override fun cancelJob() { calls += "cancelJob" }
        override fun setClipVersion(clipId: String, original: Boolean, match: Boolean?) { calls += "version:$original" + (match?.let { ":match=$it" } ?: "") }
        override fun discardProcess(clipId: String) { calls += "discard" }
        override fun vocalPreview(clipId: String, params: com.caucesound.app.data.VocalParams, from: Long, length: Long) { calls += "preview:$from:$length"; vocal = params }
        override fun previewListen(clipId: String, on: Boolean) { calls += "previewListen:$on" }
        override fun auditionClip(clipId: String) { calls += "audition" }
        override fun soloAnalyze(clipId: String) { calls += "analyze:$clipId" }
        override fun confirmAnalysis(keyRoot: Int, minor: Boolean, bpm: Double?, useChords: Boolean) { calls += "confirm:$keyRoot:$minor:${bpm?.let { Math.round(it) }}:$useChords" }
        override fun soloProposals(request: com.caucesound.app.data.SoloRequest) { calls += "proposals"; this.request = request }
        override fun soloPick(index: Int) { calls += "pick:$index" }
        override fun soloVary() { calls += "vary" }
        override fun soloTransform(op: String) { calls += "transform:$op" }
        override fun soloNewEnding() { calls += "ending" }
        override fun openPanel(panel: Panel) { calls += "panel:$panel" }
        override fun selectClip(id: String?) { calls += "selectClip:$id" }
        override fun setRoll(trackId: String, patternId: String, lane: Int, step: Int, roll: Int) { calls += "roll:$lane:$step:$roll" }
        override fun setStep(trackId: String, patternId: String, lane: Int, step: Int, velocity: Int) { calls += "step:$lane:$step:$velocity" }
        override fun commitMixer() { calls += "commit" }
            }

    /** Igual que en la app: fondo y color de texto del Scaffold del estudio. */
    @androidx.compose.runtime.Composable
    private fun Themed(content: @androidx.compose.runtime.Composable () -> Unit) = CauceTheme {
        androidx.compose.material3.Surface(color = Palette.Background, contentColor = Palette.Text) { content() }
    }

    private fun shot(name: String) {
        compose.waitForIdle()
        compose.onRoot().captureRoboImage("build/outputs/roborazzi/$name.png")
    }

    @Test fun home_screen() {
        val projects = StateParser.projects(res("projects.json"))
        assertEquals(2, projects.size)
        compose.setContent { Themed { HomeScreen(projects, catalog.templates, null, {}, object : HomeActions {}) } }
        compose.onNodeWithText("Beat trap 140", substring = true).assertExists()
        shot("01_home")
    }

    // Misión 0.4 (D): el botón «Nueva canción» no debe tapar el último proyecto (ni su menú) con la lista al final.
    @Test fun new_song_button_does_not_cover_the_last_project() {
        val projects = (1..9).map { com.caucesound.app.data.ProjectSummary("/p/$it", "Proyecto $it", 1_700_000_000_000L - it * 1000L, 4, null, 120.0, "trap") }
        compose.setContent { Themed { HomeScreen(projects, catalog.templates, null, {}, object : HomeActions {}) } }
        compose.onNodeWithTag("home_list").performScrollToNode(androidx.compose.ui.test.hasText("Proyecto 9"))
        compose.waitForIdle()
        val menus = compose.onAllNodesWithContentDescription(tr("Opciones", "Options")).fetchSemanticsNodes()
        val lastMenu = menus.maxBy { it.boundsInRoot.bottom }.boundsInRoot
        val lastRow = compose.onNodeWithText("Proyecto 9").fetchSemanticsNode().boundsInRoot
        val fab = compose.onNodeWithTag("fab_new_song").fetchSemanticsNode().boundsInRoot
        assertTrue("el botón tapa el menú del último proyecto: menú $lastMenu, botón $fab", lastMenu.bottom <= fab.top)
        assertTrue("el botón tapa el nombre del último proyecto: $lastRow vs $fab", lastRow.bottom <= fab.top)
        shot("01b_home_scrolled_end")
    }

    // Misión 0.4 (B): variaciones locales. El estado sale del motor real (cs_render --state trap).
    private val state04: ProjectState by lazy { StateParser.state(res("state_trap04.json"))!! }

    @Test fun variations_panel_sends_part_energy_and_lock_commands() {
        val plan = state04.music!!
        assertEquals("trap", plan.genre)
        assertTrue(plan.hasPart("drums") && plan.hasPart("bass") && plan.hasPart("melody"))
        val rec = Recorder()
        compose.setContent { Themed { VariationsPanel(state04, rec) } }
        shot("12_variations")
        compose.onNodeWithTag("vary_all").performClick()
        compose.onNodeWithTag("vary_melody").performClick()
        compose.onNodeWithText(tr("Más enérgica", "More energy")).performClick()
        val bass = plan.roles.entries.first { it.value == "bass" }.key
        compose.onNodeWithTag("lock_$bass").performScrollTo().performClick()
        assertEquals(listOf("vary:all:true:null", "vary:melody:true:null", "vary:all:false:0.6", "lock:$bass:true"), rec.calls)
        // El trap no tiene parte de acordes: no se ofrece un botón que no haría nada.
        assertTrue(compose.onAllNodes(androidx.compose.ui.test.hasTestTag("vary_chords")).fetchSemanticsNodes().isEmpty())
    }

    // --- Misión 0.5: Solo AI 2.0 y VocalShift. Todos los datos son JSON reales del motor (cs_render --fixtures05). ---
    private val stateVocal: ProjectState by lazy { StateParser.state(res("state_vocal.json"))!! }
    private val stateVocalPreview: ProjectState by lazy { StateParser.state(res("state_vocal_preview.json"))!! }
    private val stateBeat: ProjectState by lazy { StateParser.state(res("state_beat.json"))!! }
    private val presets by lazy { com.caucesound.app.data.PluginParser.vocalPresets(res("vocal_presets.json")) }
    private val analysis by lazy { com.caucesound.app.data.BeatAnalysis.parse(org.json.JSONObject(res("analysis.json")).getJSONObject("result")) }
    private val family by lazy { com.caucesound.app.data.SoloFamily.parse(com.caucesound.app.data.SoloRequest(bars = 8, seed = 2026), org.json.JSONObject(res("family.json"))) }
    private fun ui(job: com.caucesound.app.data.JobUi? = null, a: com.caucesound.app.data.BeatAnalysis? = null, f: com.caucesound.app.data.SoloFamily? = null) =
        com.caucesound.app.data.PluginUi(presets, job, com.caucesound.app.data.PluginParser.vocalResult(org.json.JSONObject(res("vocal_job.json")).getJSONObject("result")), a, f)

    @Test fun variations_panel_links_to_solo_ai() {
        assertTrue(catalog.hasPlugin("solo.ai") && catalog.hasPlugin("vocalshift"))
        val rec = Recorder()
        compose.setContent { Themed { VariationsPanel(state04, rec, soloAvailable = true) } }
        compose.onNodeWithTag("open_solo").performScrollTo().performClick()
        assertEquals(listOf("panel:SOLO"), rec.calls)
    }

    @Test fun solo_ai_uses_song_key_when_there_is_a_plan() {
        val rec = Recorder()
        compose.setContent { Themed { SoloAiPanel(state04, null, ui(), rec) } }
        compose.onNodeWithTag("solo_key_plan").assertExists()
        compose.onNodeWithTag("solo_type_solo").performClick()
        compose.onNodeWithTag("solo_generate").performScrollTo().performClick()
        assertEquals(listOf("proposals"), rec.calls)
        val r = rec.request!!
        assertEquals("solo", r.type); assertEquals(8, r.bars); assertEquals(2, r.startBar); assertEquals(null, r.keyRoot)
        assertTrue(r.seed != 0L)
    }

    @Test fun solo_ai_asks_for_the_key_without_a_plan_and_never_guesses() {
        val rec = Recorder()
        compose.setContent { Themed { SoloAiPanel(state, null, ui(), rec) } } // proyecto 0.3: sin plan, sin audio
        shot("15_solo_ai_key")
        compose.onNodeWithTag("solo_generate").performScrollTo().performClick()
        assertTrue("sin tonalidad no debe generar", rec.calls.isEmpty())
        compose.onNodeWithTag("solo_key_7").performScrollTo().performClick() // Sol
        compose.onNodeWithText(tr("Mayor", "Major")).performScrollTo().performClick()
        compose.onNodeWithTag("solo_key_save").performScrollTo().performClick()
        assertEquals(listOf("confirm:7:false:null:false"), rec.calls) // guarda la tonalidad y no toca el tempo
    }

    @Test fun solo_ai_offers_to_analyze_an_imported_beat() {
        val rec = Recorder()
        val beat = stateBeat.tracks.first { it.name == "Beat" }.clips.first()
        compose.setContent { Themed { SoloAiPanel(stateBeat, beat.id, ui(), rec) } }
        compose.onNodeWithTag("solo_analyze").performScrollTo().performClick()
        assertEquals(listOf("analyze:${beat.id}"), rec.calls)
    }

    @Test fun solo_ai_analysis_shows_confidence_and_keeps_the_tempo_unless_the_user_picks_one() {
        // Datos del motor: beat sintético de 92 BPM en La menor.
        assertEquals(92.0, analysis.bpm, 1.0)
        assertEquals(9, analysis.keyRoot); assertTrue(analysis.minor && analysis.tonal)
        assertTrue(analysis.bpmConfidence in 0.0..1.0 && analysis.keyConfidence in 0.0..1.0)
        assertTrue(analysis.chords.isNotEmpty())
        val rec = Recorder()
        val beat = stateBeat.tracks.first { it.name == "Beat" }.clips.first()
        compose.setContent { Themed { SoloAiPanel(stateBeat, beat.id, ui(a = analysis), rec) } }
        compose.onNodeWithTag("solo_analysis").assertExists()
        shot("23_solo_ai_analysis")
        // Por defecto: confirmar guarda la tonalidad y NO cambia el tempo.
        compose.onNodeWithTag("solo_confirm").performScrollTo().performClick()
        assertEquals("confirm:9:true:null:false", rec.calls.first())
        rec.calls.clear()
        // El usuario corrige la tonalidad a Do, adopta el tempo detectado y los acordes.
        compose.onNodeWithTag("solo_fix_key_0").performScrollTo().performClick()
        compose.onNodeWithTag("solo_bpm_0").performScrollTo().performClick()
        compose.onNodeWithTag("solo_use_chords").performScrollTo().performClick()
        compose.onNodeWithTag("solo_confirm").performScrollTo().performClick()
        assertEquals("confirm:0:true:92:true", rec.calls.first())
    }

    @Test fun solo_ai_alternatives_can_be_picked_varied_and_opened_in_the_piano_roll() {
        assertEquals(4, family.proposals.size)
        assertTrue(family.proposals.all { it.notes > 0 && it.name.isNotBlank() })
        val pick = org.json.JSONObject(res("family_pick.json"))
        assertEquals(family.proposals[0].notes, pick.getInt("notes")) // lo que se anuncia es lo que entra en la pista
        val rec = Recorder()
        val track = stateVocal.tracks.first { it.name.startsWith("Solo AI") }
        compose.setContent { Themed { SoloAiPanel(stateVocal, null, ui(f = family.copy(selected = 0, trackId = track.id)), rec) } }
        compose.onNodeWithTag("solo_key_confirmed").assertExists()
        compose.onNodeWithTag("solo_alt_2").performScrollTo().performClick()
        compose.onNodeWithTag("solo_vary").performScrollTo().performClick()
        compose.onNodeWithTag("solo_listen").performScrollTo().performClick()
        compose.onNodeWithTag("solo_open_piano").performScrollTo().performClick()
        assertEquals(listOf("pick:2", "vary", "play", "selectClip:${track.midiClips.first().id}", "panel:PIANO"), rec.calls)
        // Controles creativos 0.6: cada botón envía su transformación real.
        rec.calls.clear()
        for (tag in listOf("solo_simpler", "solo_intense", "solo_soft", "solo_octave_up", "solo_octave_down", "solo_new_ending")) compose.onNodeWithTag(tag).performScrollTo().performClick()
        assertEquals(listOf("transform:simpler", "transform:intense", "transform:soft", "transform:octaveUp", "transform:octaveDown", "ending"), rec.calls)
        shot("29_solo_ai_adjust")
        compose.onNodeWithTag("solo_alt_0").performScrollTo()
        shot("24_solo_ai_alternatives")
    }

    @Test fun vocalshift_reference_preset_and_processed_clip_come_from_the_engine() {
        val urban = presets.first { it.id == "urban_spatial" }
        assertEquals(7f, urban.params.semitones); assertEquals(65f, urban.params.character); assertEquals(70f, urban.params.magic)
        assertEquals(-12f, urban.params.secondGainDb); assertTrue(urban.params.secondVoice)
        val clip = stateVocal.tracks.first { it.name == "Voz" }.clips.first()
        val pr = clip.process!!
        assertEquals("vocalshift", pr.plugin)
        assertTrue(!pr.playingOriginal && pr.procLength >= pr.origLength && clip.length == pr.procLength)
        assertEquals(urban.params.copy(preset = ""), pr.params.copy(preset = ""))
        assertTrue(stateVocal.missingPlugins.isEmpty())
    }

    @Test fun vocalshift_panel_processes_with_the_chosen_settings() {
        val rec = Recorder()
        val clip = stateBeat.tracks.first { it.name == "Voz" }.clips.first() // toma sin procesar
        compose.setContent { Themed { VocalShiftPanel(stateBeat, clip.id, ui(), emptyMap(), rec) } }
        shot("20_vocalshift_easy")
        // Sin versión procesada no hay A/B ni descartar.
        assertTrue(compose.onAllNodes(hasTestTag("vocal_ab")).fetchSemanticsNodes().isEmpty())
        compose.onNodeWithTag("vocal_process").performScrollTo().performClick()
        assertEquals(listOf("vocal:${clip.id}"), rec.calls)
        assertEquals(presets.first { it.id == "urban_spatial" }.params, rec.vocal) // por defecto: Urbano Espacial
        // Otro preset y segunda voz desactivada.
        val other = presets.first { it.id != "urban_spatial" }
        compose.onNodeWithTag("vocal_preset_${other.id}").performScrollTo().performClick()
        compose.onNodeWithTag("vocal_process").performScrollTo().performClick()
        assertEquals(other.params, rec.vocal)
    }

    @Test fun vocalshift_preview_renders_a_region_and_switches_between_original_and_test() {
        val rec = Recorder()
        val clip = stateVocalPreview.tracks.first { it.name == "Voz" }.clips.first()
        val pv = stateVocalPreview.vocalPreview!!
        assertTrue(pv.clipId == clip.id && pv.listening && pv.length == 96000L)
        compose.setContent { Themed { VocalShiftPanel(stateVocalPreview, clip.id, ui(), emptyMap(), rec) } }
        // Pide una región corta del original (aquí la toma entera cabe en los 6 s de prueba).
        compose.onNodeWithTag("vocal_preview").performScrollTo().performClick()
        assertEquals(listOf("preview:0:${clip.process!!.origLength}"), rec.calls)
        rec.calls.clear()
        compose.onNodeWithTag("vocal_preview_ab").performScrollTo()
        compose.onNodeWithText(tr("Original", "Original")).performClick()
        assertEquals(listOf("previewListen:false"), rec.calls)
        shot("25_vocalshift_preview")
    }

    @Test fun vocalshift_preview_shows_real_progress_and_can_be_cancelled() {
        val rec = Recorder()
        val clip = stateVocal.tracks.first { it.name == "Voz" }.clips.first()
        val job = com.caucesound.app.data.JobUi("job_1", "vocalPreview", clip.id, "running", 0.37f)
        compose.setContent { Themed { VocalShiftPanel(stateVocal, clip.id, ui().copy(job = job), emptyMap(), rec) } }
        compose.onNodeWithTag("vocal_preview_progress").performScrollTo().assertIsDisplayed()
        compose.onNodeWithText("37 %", substring = true).assertIsDisplayed()
        assertTrue(compose.onAllNodes(hasTestTag("vocal_preview")).fetchSemanticsNodes().isEmpty()) // no se lanza otra mientras tanto
        compose.onNodeWithTag("vocal_preview_cancel").performScrollTo().performClick()
        assertEquals(listOf("cancelJob"), rec.calls)
    }

    /** Defecto de la 0.5: salir de VocalShift sin «Aceptar» dejaba activo el volumen igualado. Ahora salir lo apaga siempre. */
    @Test fun leaving_vocalshift_turns_off_matched_volume_and_test_listening() {
        val rec = Recorder()
        val clip = stateVocalPreview.tracks.first { it.name == "Voz" }.clips.first()
        assertTrue(clip.process!!.match)
        var visible by mutableStateOf(true)
        compose.setContent { Themed { if (visible) VocalShiftPanel(stateVocalPreview, clip.id, ui(), emptyMap(), rec) } }
        compose.waitForIdle()
        assertTrue(rec.calls.isEmpty()) // abrir la pantalla no cambia nada
        visible = false
        compose.waitForIdle()
        assertEquals(setOf("version:false:match=false", "previewListen:false"), rec.calls.toSet())
    }

    @Test fun vocalshift_panel_compares_accepts_and_discards() {
        val rec = Recorder()
        val clip = stateVocal.tracks.first { it.name == "Voz" }.clips.first()
        compose.setContent { Themed { VocalShiftPanel(stateVocal, clip.id, ui(), emptyMap(), rec) } }
        compose.onNodeWithText(tr("A · Original", "A · Original")).performScrollTo().performClick()
        compose.onNodeWithTag("vocal_listen").performScrollTo().performClick()
        compose.onNodeWithTag("vocal_discard").performScrollTo().performClick()
        compose.onNodeWithTag("vocal_accept").performScrollTo().performClick()
        // Aceptar quita el volumen igualado: la versión procesada queda a su nivel real.
        assertTrue(clip.process!!.match && kotlin.math.abs(clip.process!!.levelDb) >= 0.1f)
        assertEquals(listOf("version:true", "audition", "discard", "version:false:match=false", "panel:ARRANGE"), rec.calls)
        rec.calls.clear()
        compose.onNodeWithTag("vocal_match").performScrollTo().performClick()
        assertEquals(listOf("version:false:match=false"), rec.calls)
        compose.onNodeWithTag("vocal_ab").performScrollTo()
        shot("21_vocalshift_ab")
    }

    @Test fun vocalshift_panel_shows_real_progress_and_cancel() {
        val rec = Recorder()
        val clip = stateBeat.tracks.first { it.name == "Voz" }.clips.first()
        val job = com.caucesound.app.data.JobUi("job_1", "vocalshift", clip.id, "running", 0.42f)
        compose.setContent { Themed { VocalShiftPanel(stateBeat, clip.id, ui(job = job), emptyMap(), rec) } }
        compose.onNodeWithTag("vocal_progress").performScrollTo().assertExists()
        compose.onNodeWithText("42 %", substring = true).assertExists()
        assertTrue(compose.onAllNodes(hasTestTag("vocal_process")).fetchSemanticsNodes().isEmpty()) // no se puede lanzar otro encima
        shot("22_vocalshift_processing")
        compose.onNodeWithTag("vocal_cancel").performClick()
        assertEquals(listOf("cancelJob"), rec.calls)
    }

    @Test fun vocalshift_advanced_mode_only_shows_engine_controls() {
        val clip = stateBeat.tracks.first { it.name == "Voz" }.clips.first()
        compose.setContent { Themed { VocalShiftPanel(stateBeat, clip.id, ui(), emptyMap(), Recorder()) } }
        compose.onNodeWithText(tr("Avanzado", "Advanced")).performClick()
        for (tag in listOf("vocal_formant", "vocal_brightness", "vocal_pan", "vocal_offset")) compose.onNodeWithTag(tag).performScrollTo().assertExists()
        // La mezcla solo existe sin segunda voz (así funciona el motor).
        assertTrue(compose.onAllNodes(hasTestTag("vocal_mix")).fetchSemanticsNodes().isEmpty())
        compose.onNodeWithTag("vocal_formant").performScrollTo()
        shot("21b_vocalshift_advanced")
        compose.onNodeWithTag("vocal_second").performScrollTo().performClick()
        compose.onNodeWithTag("vocal_mix").performScrollTo().assertExists()
        assertTrue(compose.onAllNodes(hasTestTag("vocal_pan")).fetchSemanticsNodes().isEmpty())
    }

    @Test fun vocalshift_asks_for_a_clip_when_none_is_selected() {
        val rec = Recorder()
        val clip = stateVocal.tracks.first { it.name == "Voz" }.clips.first()
        compose.setContent { Themed { VocalShiftPanel(stateVocal, null, ui(), emptyMap(), rec) } }
        compose.onNodeWithTag("vocal_clip_${clip.id}").performClick()
        assertEquals("selectClip:${clip.id}", rec.calls.last())
    }

    @Test fun vocalshift_explains_what_it_needs_when_the_project_has_no_audio() {
        val rec = Recorder()
        compose.setContent { Themed { VocalShiftPanel(state04, null, ui(), emptyMap(), rec) } }
        compose.onNodeWithText(tr("VocalShift necesita una voz", "VocalShift needs a voice")).assertExists()
        assertTrue(compose.onAllNodes(hasTestTag("vocal_process")).fetchSemanticsNodes().isEmpty())
        compose.onNodeWithText(tr("Ir al arreglo", "Go to the arrangement")).performClick()
        assertEquals(listOf("panel:ARRANGE"), rec.calls)
    }

    @Test fun selected_audio_clip_offers_vocalshift_and_analysis_in_the_arrangement() {
        val rec = Recorder()
        val clip = stateVocal.tracks.first { it.name == "Voz" }.clips.first()
        val meters = mutableStateOf(FloatArray(stateVocal.tracks.size + 1))
        compose.setContent { Themed { StudioLayout(stateVocal, Panel.ARRANGE, clip.id, stateVocal.tracks[0].id, emptyMap(), catalog, false, remember { mutableLongStateOf(0L) }, false,
            meters, rec, androidx.compose.material3.SnackbarHostState(), {}, {}, {}, {}, plugins = ui()) } }
        shot("19_arrange_audio_clip_tools")
        compose.onNodeWithTag("clip_vocalshift").performScrollTo().performClick()
        assertTrue(rec.calls.contains("panel:VOCAL"))
    }

    @Test fun plugin_tools_are_hidden_when_the_engine_does_not_include_them() {
        val clip = stateVocal.tracks.first { it.name == "Voz" }.clips.first()
        val meters = mutableStateOf(FloatArray(stateVocal.tracks.size + 1))
        compose.setContent { Themed { StudioLayout(stateVocal, Panel.ARRANGE, clip.id, stateVocal.tracks[0].id, emptyMap(), catalog.copy(plugins = emptyList()), false,
            remember { mutableLongStateOf(0L) }, false, meters, Recorder(), androidx.compose.material3.SnackbarHostState(), {}, {}, {}, {}) } }
        assertTrue(compose.onAllNodes(hasTestTag("clip_vocalshift")).fetchSemanticsNodes().isEmpty())
        assertTrue(compose.onAllNodes(hasTestTag("clip_analyze")).fetchSemanticsNodes().isEmpty())
        assertTrue(compose.onAllNodes(hasTestTag("open_variations")).fetchSemanticsNodes().isEmpty()) // sin plan ni Solo AI no hay ✨
    }

    @Test fun variations_panel_explains_when_the_project_has_no_plan() {
        assertEquals(null, state.music) // fixture de la 0.3: sin plan musical
        compose.setContent { Themed { VariationsPanel(state, Recorder()) } }
        compose.onNodeWithText(tr("Este proyecto no tiene variaciones", "This project has no variations")).assertExists()
    }

    @Test fun drum_rolls_are_parsed_and_roll_mode_cycles_hits() {
        val drums = state04.tracks.first { it.isDrums }
        val withRoll = drums.patterns.first { p -> p.rolls.any { lane -> lane.any { it > 1 } } }
        assertTrue(withRoll.rollAt(0, 0) >= 1)
        val rec = Recorder()
        compose.setContent { Themed { DrumsPanel(state04, drums, catalog, remember { mutableLongStateOf(0L) }, false, rec, null) {} } }
        compose.onNodeWithText(tr("Redoble", "Roll")).performClick()
        shot("13_drums_roll_mode")
        compose.onNodeWithText(tr("Toca un paso", "Tap a step"), substring = true).assertExists()
    }

    @Test fun home_import_and_export_actions_are_wired() {
        val projects = StateParser.projects(res("projects.json"))
        var imported = 0
        var exported: String? = null
        val actions = object : HomeActions {
            override fun importArchive() { imported++ }
            override fun exportArchive(folder: String, name: String) { exported = folder }
        }
        compose.setContent { Themed { HomeScreen(projects, catalog.templates, null, {}, actions) } }
        compose.onNodeWithText(tr("Importar", "Import")).performClick()
        assertEquals(1, imported)
        compose.onAllNodesWithContentDescription(tr("Opciones", "Options"))[0].performClick()
        compose.onNodeWithText(tr("Exportar (.caucesound)", "Export (.caucesound)")).performClick()
        assertEquals(projects.first { it.error == null }.folder, exported)
    }

    @Test fun drums_panel_paints_steps() {
        val rec = Recorder()
        val drums = state.tracks.first { it.isDrums }
        compose.setContent { Themed { DrumsPanel(state, drums, catalog, remember { mutableLongStateOf(0L) }, false, rec, null) {} } }
        shot("02_drums")
        assertTrue(drums.patterns.isNotEmpty() && drums.patterns[0].lanes.size == 8)
    }

    @Test fun piano_roll_shows_engine_notes() {
        val rec = Recorder()
        // Pista «Melodía» de la plantilla trap: arpegio generado por el motor.
        val synth = state.tracks.last { it.isSynth }
        val clip = synth.midiClips.first()
        assertTrue(clip.notes.size >= 8 && clip.notes.all { it.start + it.length <= clip.length })
        compose.setContent { Themed { PianoRollPanel(state, synth, clip, remember { mutableLongStateOf(0L) }, false, rec) } }
        shot("03_piano_roll")
    }

    @Test fun synth_panel_loads_preset() {
        val rec = Recorder()
        val synth = state.tracks.first { it.isSynth }
        compose.setContent { Themed { SynthPanel(state, synth, catalog, rec) } }
        shot("04_synth")
        compose.onNodeWithText("Pad").performScrollTo().performClick()
        assertTrue(rec.calls.contains("preset:pad"))
    }

    @Test fun knob_follows_the_engine_value_after_a_drag_and_an_undo() {
        // Error real: tras arrastrar, la perilla seguía mostrando su valor local aunque el motor lo cambiara
        // (deshacer o cargar un preset). Ahora el valor externo manda en cuanto cambia.
        val engine = mutableStateOf(0.2f)
        val sent = mutableListOf<Float>()
        compose.setContent { Themed { Knob("Test", engine.value, 0f, 1f, 0.5f, { sent += it }, format = { "${(it * 100).toInt()}%" }) } }
        compose.onNodeWithContentDescription("Test 20%").performTouchInput { swipeUp() }
        assertTrue(sent.isNotEmpty() && sent.last() > 0.2f)
        // El motor aún no ha contestado: la perilla conserva lo que el dedo dejó (sin saltar atrás).
        compose.waitForIdle()
        assertTrue(compose.onAllNodes(androidx.compose.ui.test.hasContentDescription("Test 20%")).fetchSemanticsNodes().isEmpty())
        // El motor confirma el valor nuevo y después se deshace: la perilla debe mostrar el valor del motor.
        engine.value = sent.last(); compose.waitForIdle()
        engine.value = 0.2f; compose.waitForIdle()
        compose.onNodeWithContentDescription("Test 20%").assertExists()
        // Un segundo gesto parte del valor del motor, no del antiguo valor local.
        sent.clear()
        compose.onNodeWithContentDescription("Test 20%").performTouchInput { down(center); moveBy(androidx.compose.ui.geometry.Offset(0f, -4f)); up() }
        assertTrue(sent.all { it < 0.3f })
    }

    @Test fun synth_panel_filters_by_category_and_marks_favorites() {
        val rec = object : StudioActions { var fav: String? = null; override fun toggleFavoritePreset(presetId: String) { fav = presetId } }
        val synth = state.tracks.first { it.isSynth }
        compose.setContent { Themed { SynthPanel(state, synth, catalog, rec, favorites = setOf("pad")) } }
        compose.onNodeWithText("★ Pad").assertExists()
        compose.onNodeWithText("Pads").performClick()
        compose.onNodeWithText("★ Pad").assertExists()
        assertTrue(compose.onAllNodes(androidx.compose.ui.test.hasText("Lead")).fetchSemanticsNodes().isEmpty())
        compose.onNodeWithText("★").performClick()
        assertTrue(compose.onAllNodes(androidx.compose.ui.test.hasText("Pluck")).fetchSemanticsNodes().isEmpty())
        compose.onNode(androidx.compose.ui.test.hasContentDescription("Añadir a favoritos").or(androidx.compose.ui.test.hasContentDescription("Add to favorites"))).performClick()
        assertEquals(synth.preset, rec.fav)
    }

    @Test fun mixer_panel_toggles_mute() {
        val rec = Recorder()
        val meters = mutableStateOf(FloatArray(state.tracks.size + 1) { 0.3f + 0.1f * it }) // fuera de la composición
        compose.setContent { Themed { MixerPanel(state, meters, state.tracks[0].id, rec) } }
        shot("05_mixer")
        compose.onAllNodes(androidx.compose.ui.test.hasContentDescription("M"))[0].performClick()
        assertEquals("mute:${state.tracks[0].id}:true", rec.calls.last())
        // Perilla de panorámica: arrastrar cambia el valor en vivo y al soltar se cierra el gesto; doble toque = centro.
        val pan = compose.onAllNodes(androidx.compose.ui.test.hasContentDescription("Pan", substring = true), useUnmergedTree = true)[0]
        pan.performTouchInput { swipeUp() }
        assertTrue(rec.calls.contains("pan:${state.tracks[0].id}"))
        assertEquals("commit", rec.calls.last())
        rec.calls.clear()
        pan.performTouchInput { doubleClick() }
        assertTrue(rec.calls.contains("pan:${state.tracks[0].id}"))
    }

    @Test fun mixer_offers_reverb_bus_when_there_is_none() {
        val rec = Recorder()
        val meters = mutableStateOf(FloatArray(state.tracks.size + 1) { 0.3f })
        compose.setContent { Themed { MixerPanel(state, meters, null, rec) } }
        compose.onNode(androidx.compose.ui.test.hasText("Bus de reverb").or(androidx.compose.ui.test.hasText("Reverb bus")))
            .performScrollTo().performClick()
        assertEquals("addReverbBus", rec.calls.last())
    }

    @Test fun mixer_shows_engine_sends_to_the_bus() {
        // Estado real del motor con un bus «Reverb» y un envío del 25 % desde el primer synth.
        val st = StateParser.state(res("state_bus.json"))!!
        val bus = st.tracks.single { it.isBus }
        val synth = st.tracks.first { it.isSynth }
        assertEquals(0.25f, synth.sends[bus.id]!!, 1e-6f)
        val rec = Recorder()
        val meters = mutableStateOf(FloatArray(st.tracks.size + 1) { 0.3f })
        compose.setContent { Themed { MixerPanel(st, meters, null, rec) } }
        shot("05b_mixer_bus")
        compose.onNodeWithContentDescription("Send 25%").assertExists()
        // Una perilla visible por pista que no es bus (la del bus es un hueco invisible sin semántica).
        assertEquals(st.tracks.count { !it.isBus }, compose.onAllNodes(androidx.compose.ui.test.hasContentDescription("Send", substring = true), useUnmergedTree = true).fetchSemanticsNodes().size)
        // La tira es clicable y fusiona la semántica de sus hijos: el gesto se dirige a la perilla en el árbol sin fusionar
        // (en el fusionado, el «nodo» es la tira entera y el toque caería en su centro, sobre el fader).
        compose.onNodeWithContentDescription("Send 25%", useUnmergedTree = true).performScrollTo().performTouchInput { swipeUp() }
        assertTrue(rec.calls.contains("send:${synth.id}:${bus.id}"))
        assertEquals("commit", rec.calls.last())
        assertTrue(compose.onAllNodes(androidx.compose.ui.test.hasText("Bus de reverb")).fetchSemanticsNodes().isEmpty())
    }

    @Test fun effects_panel_shows_engine_chain() {
        val rec = Recorder()
        // Pista con reverb y EQ añadidas por el motor (fixture generado con inserciones reales).
        val t = state.tracks.first { it.inserts.isNotEmpty() }
        assertEquals(listOf("cs.eq", "cs.reverb"), t.inserts.map { it.type })
        assertEquals(7, catalog.effects.size)
        compose.setContent { Themed { EffectsPanel(state, t, catalog, rec) } }
        shot("07_effects")
        compose.onAllNodes(androidx.compose.ui.test.hasContentDescription("Bypass").or(androidx.compose.ui.test.hasContentDescription("On")))[0].performClick()
        assertTrue(rec.calls.any { it.startsWith("bypass:") })
    }

    @androidx.compose.runtime.Composable
    private fun Studio(panel: Panel, rec: StudioActions, selected: String? = null) {
        val meters = remember { mutableStateOf(FloatArray(state.tracks.size + 1) { 0.2f }) }
        Themed {
            StudioLayout(
                state, panel, selected, state.tracks[0].id, emptyMap(), catalog, false, remember { mutableLongStateOf(state.ticksToFrames(PPQ * 6L)) }, false,
                meters, rec, androidx.compose.material3.SnackbarHostState(), {}, {}, {}, {},
            )
        }
    }

    @Test fun studio_arrangement_portrait() {
        val rec = Recorder()
        compose.setContent { Studio(Panel.ARRANGE, rec) }
        shot("08_studio_arrange")
        compose.onNodeWithContentDescription("Play").performClick()
        assertTrue(rec.calls.contains("play"))
    }

    @Config(qualifiers = "w891dp-h411dp-land-xxhdpi")
    @Test fun studio_arrangement_landscape() {
        val clip = state.tracks.first { it.isSynth }.midiClips.first().id
        compose.setContent { Studio(Panel.ARRANGE, Recorder(), clip) }
        shot("09_studio_arrange_land")
    }

    @Test fun studio_drums_panel_in_context() {
        compose.setContent { Studio(Panel.DRUMS, Recorder()) }
        shot("10_studio_drums")
    }

    @Test fun tempo_form_has_tap_tempo_and_count_in() {
        // Se prueba el contenido del diálogo sin la ventana: en Robolectric, un campo de texto dentro de un diálogo
        // no llega a reposo (limitación del entorno JVM, no verificada en teléfono).
        val form = TempoForm(state, 1)
        var clock = 0L
        compose.setContent { Themed { Box(Modifier.fillMaxSize()) { Box(Modifier.align(androidx.compose.ui.Alignment.Center).padding(24.dp)) { TempoFormContent(form) { clock } } } } }
        val tap = compose.onNode(androidx.compose.ui.test.hasText("Tocar", substring = true).or(androidx.compose.ui.test.hasText("Tap", substring = true)))
        for (t in listOf(0L, 500L, 1000L, 1500L)) { clock = t; tap.performClick() }
        assertEquals(120.0, form.bpmVal!!, 1e-9)
        compose.onNode(androidx.compose.ui.test.hasText("2 compases").or(androidx.compose.ui.test.hasText("2 bars"))).performClick()
        assertEquals(2, form.bars)
        assertTrue(form.valid)
        shot("11_tempo_form")
        form.bpm = "999"; assertTrue(!form.valid)
    }

    @Config(qualifiers = "w360dp-h640dp-xhdpi")
    @Test fun studio_fits_a_small_phone() {
        val rec = Recorder()
        compose.setContent { Studio(Panel.ARRANGE, rec) }
        shot("12_small_arrange")
        // Los controles esenciales siguen a la vista y se pueden tocar en 360 dp de ancho.
        compose.onNodeWithContentDescription("Play").performClick()
        assertTrue(rec.calls.contains("play"))
        compose.onNodeWithContentDescription("Tempo").assertExists()
    }

    @Config(qualifiers = "w360dp-h640dp-xhdpi")
    @Test fun drums_fit_a_small_phone() {
        compose.setContent { Studio(Panel.DRUMS, Recorder()) }
        shot("13_small_drums")
    }

    @Config(qualifiers = "w360dp-h640dp-xhdpi")
    @Test fun mixer_fits_a_small_phone() {
        compose.setContent { Studio(Panel.MIXER, Recorder()) }
        shot("14_small_mixer")
    }

    @Config(qualifiers = "w480dp-h1040dp-xxhdpi")
    @Test fun studio_on_a_large_phone() {
        compose.setContent { Studio(Panel.PIANO, Recorder()) }
        shot("15_large_piano")
    }

    @Config(qualifiers = "w891dp-h411dp-land-xxhdpi")
    @Test fun studio_mixer_landscape() {
        compose.setContent { Studio(Panel.MIXER, Recorder()) }
        shot("16_mixer_land")
    }

    @Config(qualifiers = "w891dp-h411dp-land-xxhdpi")
    @Test fun studio_piano_landscape() {
        compose.setContent { Studio(Panel.PIANO, Recorder()) }
        shot("17_piano_land")
    }

    @Test fun studio_synth_in_context() {
        compose.setContent { Studio(Panel.SYNTH, Recorder()) }
        shot("18_studio_synth")
    }

    @Test fun arrangement_timeline_draws_midi_and_audio_lanes() {
        compose.setContent {
            Themed {
                Box(Modifier.fillMaxSize()) {
                    Timeline(state, emptyMap(), TimelineGeometry(60f), rememberScrollState(), null, true, { 0L }, {}, {}, { _, _, _ -> }, { _, _, _ -> })
                }
            }
        }
        shot("06_timeline")
    }

    // --- Exportación para DAW (misión 0.6) ----------------------------------------------------------------------
    @Test fun export_options_offer_mix_daw_package_and_midi_with_the_chosen_mode() {
        val calls = mutableListOf<String>()
        compose.setContent { Themed { Box(Modifier.padding(16.dp)) { ExportOptions(hasMidi = true, onMix = { calls += "mix:$it" }, onDaw = { calls += "daw:$it" }, onMidi = { calls += "midi" }) } } }
        shot("26_export_options")
        compose.onNodeWithTag("export_mix").performScrollTo().performClick()
        compose.onNodeWithTag("export_daw").performScrollTo().performClick()            // por defecto: con efectos
        compose.onNodeWithText(tr("Sin efectos", "No effects")).performScrollTo().performClick()
        compose.onNodeWithTag("export_daw").performScrollTo().performClick()
        compose.onNodeWithTag("export_midi").performScrollTo().performClick()
        assertEquals(listOf("mix:24", "daw:true", "daw:false", "midi"), calls)
    }

    @Test fun export_options_disable_midi_when_the_song_has_no_notes() {
        val calls = mutableListOf<String>()
        compose.setContent { Themed { ExportOptions(hasMidi = false, onMix = {}, onDaw = {}, onMidi = { calls += "midi" }) } }
        compose.onNodeWithTag("export_midi").performScrollTo().performClick()
        assertTrue(calls.isEmpty())
    }

    @Test fun daw_export_status_shows_real_progress_with_cancel_and_never_done_while_working() {
        val calls = mutableListOf<String>()
        var e by mutableStateOf(com.caucesound.app.data.DawExportUi("running", "job1", 0.42f, true))
        compose.setContent { Themed { Box(Modifier.padding(16.dp)) { DawExportStatus(e, onCancel = { calls += "cancel" }, onClose = { calls += "close" }) } } }
        compose.onNodeWithText("42 %", substring = true).assertIsDisplayed()
        assertTrue(compose.onAllNodes(hasTestTag("export_done")).fetchSemanticsNodes().isEmpty())
        shot("27_export_progress")
        compose.onNodeWithTag("export_cancel").performClick()
        assertEquals(listOf("cancel"), calls)
        // Copiando a la carpeta elegida: sigue sin decir «guardado» y ya no se puede cancelar a medias.
        e = e.copy(phase = "saving", progress = 1f)
        compose.onNodeWithTag("export_saving").assertIsDisplayed()
        assertTrue(compose.onAllNodes(hasTestTag("export_done")).fetchSemanticsNodes().isEmpty())
        assertTrue(compose.onAllNodes(hasTestTag("export_cancel")).fetchSemanticsNodes().isEmpty())
        // Terminado: resumen y aviso de recorte.
        e = com.caucesound.app.data.DawExportUi("done", "job1", 1f, true, stems = 6, midiNotes = 499, files = 11, bytes = 63_400_000, clipped = 12)
        compose.onNodeWithTag("export_done").assertIsDisplayed()
        compose.onNodeWithTag("export_clipped").assertIsDisplayed()
        compose.onNodeWithText("499", substring = true).assertIsDisplayed()
        shot("28_export_done")
        compose.onNodeWithTag("export_close").performClick()
        assertEquals(listOf("cancel", "close"), calls)
        // Cancelado: lo dice y no afirma que haya archivo.
        e = com.caucesound.app.data.DawExportUi("cancelled")
        compose.onNodeWithTag("export_failed").assertIsDisplayed()
        assertTrue(compose.onAllNodes(hasTestTag("export_done")).fetchSemanticsNodes().isEmpty())
    }
}
