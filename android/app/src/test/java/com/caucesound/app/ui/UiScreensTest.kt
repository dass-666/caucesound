package com.caucesound.app.ui

import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.rememberScrollState
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.Modifier
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithContentDescription
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import com.caucesound.app.data.Catalog
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
        override fun addPattern(trackId: String, copyFrom: String?) { calls += "addPattern:$copyFrom" }
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

    @Test fun drums_panel_paints_steps() {
        val rec = Recorder()
        val drums = state.tracks.first { it.isDrums }
        compose.setContent { Themed { DrumsPanel(state, drums, catalog, mutableLongStateOf(0L), false, rec, null) {} } }
        shot("02_drums")
        assertTrue(drums.patterns.isNotEmpty() && drums.patterns[0].lanes.size == 8)
    }

    @Test fun piano_roll_shows_engine_notes() {
        val rec = Recorder()
        // Pista «Melodía» de la plantilla trap: arpegio generado por el motor.
        val synth = state.tracks.last { it.isSynth }
        val clip = synth.midiClips.first()
        assertTrue(clip.notes.size >= 8 && clip.notes.all { it.start + it.length <= clip.length })
        compose.setContent { Themed { PianoRollPanel(state, synth, clip, mutableLongStateOf(0L), false, rec) } }
        shot("03_piano_roll")
    }

    @Test fun synth_panel_loads_preset() {
        val rec = Recorder()
        val synth = state.tracks.first { it.isSynth }
        compose.setContent { Themed { SynthPanel(state, synth, catalog, rec) } }
        shot("04_synth")
        compose.onNodeWithText("Pad").performClick()
        assertTrue(rec.calls.contains("preset:pad"))
    }

    @Test fun mixer_panel_toggles_mute() {
        val rec = Recorder()
        val meters = mutableStateOf(FloatArray(state.tracks.size + 1) { 0.3f + 0.1f * it })
        compose.setContent { Themed { MixerPanel(state, meters, state.tracks[0].id, rec) } }
        shot("05_mixer")
        compose.onAllNodes(androidx.compose.ui.test.hasContentDescription("M"))[0].performClick()
        assertEquals("mute:${state.tracks[0].id}:true", rec.calls.last())
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
