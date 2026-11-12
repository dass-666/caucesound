package com.caucesound.app.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.rememberScrollState
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeUp
import androidx.compose.ui.test.doubleClick
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onAllNodesWithContentDescription
import androidx.compose.ui.test.onNodeWithText
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
}
