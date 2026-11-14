package com.caucesound.app.ui

import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.click
import com.caucesound.app.data.Catalog
import com.caucesound.app.data.InsertState
import com.caucesound.app.data.PPQ
import com.caucesound.app.data.PluginUi
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.StateParser
import com.caucesound.app.data.VariationPart
import com.caucesound.app.data.VariationReport
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
 * Hotfix 0.9.2 — pruebas de interfaz que pulsan de verdad: el Mixer (canal → cadena → «+ Agregar plugin» → explorador
 * por categorías → controles), VocalShift en pistas de audio, la tarjeta de versión de Variaciones y la grabación con un
 * toque. Datos: JSON real del motor (fixtures). El recorrido completo con el motor está en EndToEndVariationsTest.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], qualifiers = "w411dp-h891dp-xxhdpi")
class Hotfix092UiTest {
    @get:Rule val compose = createComposeRule()

    private fun res(name: String) = javaClass.classLoader!!.getResource("fixtures/$name")!!.readText()
    private val rack: ProjectState by lazy { StateParser.state(res("state_rack.json"))!! }
    private val trap: ProjectState by lazy { StateParser.state(res("state_trap04.json"))!! }
    private val catalog: Catalog by lazy { Catalog.parse(res("catalog.json")) }

    private class Rec : StudioActions {
        val calls = mutableListOf<String>()
        var onAdd: (String, String) -> Unit = { _, _ -> }
        override fun selectTrack(id: String) { calls += "select:$id" }
        override fun selectClip(id: String?) { calls += "clip:$id" }
        override fun openPanel(panel: Panel) { calls += "panel:$panel" }
        override fun addInsert(trackId: String, type: String) { calls += "insert:$type"; onAdd(trackId, type) }
        override fun mutatePart(part: String, seed: Long) { calls += "mutate:$part" }
        override fun regenerateBeat(seed: Long) { calls += "regen" }
        override fun listenVariation() { calls += "listen" }
        override fun undo() { calls += "undo" }
        override fun redo() { calls += "redo" }
    }

    @androidx.compose.runtime.Composable
    private fun Themed(content: @androidx.compose.runtime.Composable () -> Unit) = CauceTheme {
        androidx.compose.material3.Surface(color = Palette.Background, contentColor = Palette.Text) { content() }
    }

    private fun shot(name: String) { compose.waitForIdle(); compose.onRoot().captureRoboImage("build/outputs/roborazzi/$name.png") }
    private fun exists(tag: String) = compose.onAllNodes(hasTestTag(tag), useUnmergedTree = true).fetchSemanticsNodes().isNotEmpty()

    @Test fun mixer_channel_tap_opens_chain_then_add_plugin_browser_by_category() {
        val rec = Rec()
        val st = mutableStateOf(rack)
        val voice = rack.tracks.first { it.isAudio }
        val selected = mutableStateOf<String?>(rack.tracks[0].id)
        val actions = object : StudioActions by rec { override fun selectTrack(id: String) { selected.value = id; rec.selectTrack(id) } }
        compose.setContent {
            Themed { StudioLayout(st.value, Panel.MIXER, null, selected.value, emptyMap(), catalog, false, remember { mutableLongStateOf(0L) }, false,
                remember { mutableStateOf(FloatArray(rack.tracks.size + 1) { 0.3f }) }, actions, androidx.compose.material3.SnackbarHostState(), {}, {}, {}, {}) }
        }
        // 1-3. Mixer abierto: tocar el canal de la voz lo selecciona y muestra su cadena de efectos sin más pasos.
        assertTrue(!exists("insert_rack"))
        // Se toca la cabecera de la tira (nombre), no el fader: es lo que hace un usuario para elegir canal.
        compose.onNodeWithTag("strip_${voice.id}", useUnmergedTree = true).performScrollTo()
            .performTouchInput { click(androidx.compose.ui.geometry.Offset(centerX, 40f)) }
        assertEquals(voice.id, selected.value)
        compose.onNodeWithTag("insert_rack").assertIsDisplayed()
        // VocalShift a la vista en la pista de audio, separado de las ranuras (no es inserción en vivo).
        compose.onNodeWithTag("rack_vocalshift").performScrollTo().assertExists()
        // 4-5. «+ Agregar plugin» abre el explorador por categorías: solo lo implementado.
        compose.onNodeWithTag("add_effect").performScrollTo().performClick()
        shot("50_plugin_browser")
        for (cat in listOf("eq", "dynamics", "reverb", "delay", "distortion", "tools")) assertTrue("falta $cat", exists("plugin_filter_$cat"))
        assertTrue("no hay efectos de modulación: no se anuncia", !exists("plugin_filter_modulation"))
        assertTrue(exists("plugin_filter_vocal_proc") && exists("plugin_filter_chains"))
        // Filtrar por «Reverb y espacios» deja solo la reverb.
        compose.onNodeWithTag("plugin_filter_reverb").performClick()
        assertTrue(exists("pick_cs.reverb") && !exists("pick_cs.eq") && !exists("pick_vocalshift"))
        shot("51_plugin_browser_reverb")
        compose.onNodeWithTag("plugin_filter_all").performClick()
        // 6-7. Elegir un efecto: se inserta y sus controles se abren solos.
        rec.onAdd = { tid, type ->
            st.value = st.value.copy(tracks = st.value.tracks.map { t -> if (t.id == tid) t.copy(inserts = t.inserts + InsertState("fx_new", type, emptyMap(), false)) else t })
        }
        compose.onNodeWithTag("pick_cs.compressor").performScrollTo().performClick()
        assertEquals("insert:cs.compressor", rec.calls.last())
        compose.waitForIdle()
        compose.onNodeWithTag("insert_controls_fx_new").performScrollTo().assertIsDisplayed()
        shot("52_plugin_added_controls")
    }

    @Test fun plugin_browser_separates_vocal_processing_chains_and_generative_tools() {
        val voice = rack.tracks.first { it.isAudio }
        var vocal: String? = null
        var chains = 0
        var tool: Panel? = null
        compose.setContent { Themed { EffectPicker(voice, catalog, {}, {}, { vocal = it }, onChains = { chains++ }, onTool = { tool = it }) } }
        // Esta voz no tiene grabación: VocalShift aparece pero explica que primero hay que grabar.
        compose.onNodeWithTag("pick_vocalshift").performScrollTo().performClick()
        assertEquals(null, vocal)
        compose.onNodeWithTag("pick_chains").performScrollTo().performClick()
        assertEquals(1, chains)
        if (catalog.hasPlugin("solo.ai")) { compose.onNodeWithTag("pick_solo_ai").performScrollTo().performClick(); assertEquals(Panel.SOLO, tool) }
    }

    @Test fun plugin_browser_has_no_vocal_processing_on_a_synth() {
        val synth = rack.tracks.first { it.isSynth }
        compose.setContent { Themed { EffectPicker(synth, catalog, {}, {}, {}, onChains = {}, onTool = {}) } }
        compose.waitForIdle()
        assertTrue(!exists("pick_vocalshift") && !exists("plugin_filter_vocal_proc"))
        assertTrue(exists("pick_cs.eq"))
    }

    @Test fun vocalshift_opens_from_the_rack_with_the_recorded_clip() {
        val st = StateParser.state(res("state_vocal.json"))!!
        val t = st.tracks.first { it.isAudio && it.clips.isNotEmpty() }
        val rec = Rec()
        compose.setContent { Themed { InsertRack(st, t, catalog, rec) } }
        compose.onNodeWithTag("rack_vocalshift").performScrollTo().performClick()
        assertEquals(listOf("select:${t.id}", "clip:${t.clips.last().id}", "panel:VOCAL"), rec.calls.takeLast(3))
    }

    @Test fun variations_card_shows_version_changes_working_and_listen() {
        val plan = trap.music!!
        val st = trap.copy(music = plan.copy(revision = 3), canUndo = true)
        val report = VariationReport("mutate", "all", 3, 0.42f, listOf(VariationPart("t1", "Drums", "drums", 0.24f), VariationPart("t2", "Pad", "pad", 0.0f)), listOf("Bajo"), 1)
        val rec = Rec()
        val ui = mutableStateOf(PluginUi(variation = report))
        compose.setContent { Themed { VariationsPanel(st, rec, plugins = ui.value, playing = false) } }
        shot("53_variations_version")
        compose.onNodeWithTag("variation_revision").assertIsDisplayed()
        compose.onNodeWithText(tr("Versión 3", "Version 3"), substring = true).assertExists()
        compose.onNodeWithText("Drums 24 %", substring = true).assertExists()
        compose.onNodeWithText("Bajo", substring = true).assertExists() // respetada por el candado
        compose.onNodeWithTag("listen_variation").performClick()
        assertEquals("listen", rec.calls.last())
        // Mientras el motor trabaja: indicador visible y botones bloqueados (no se encolan pulsaciones repetidas).
        ui.value = PluginUi(variation = report, variationWorking = "Creando una variación…")
        compose.waitForIdle()
        compose.onNodeWithTag("variation_working").assertIsDisplayed()
        compose.onNodeWithTag("mutate_all").assertIsNotEnabled()
        compose.onNodeWithTag("regenerate_beat").assertIsNotEnabled()
        shot("54_variations_working")
    }

    @Test fun quick_record_dialog_creates_track_or_uses_existing_with_solo_option() {
        val audio = rack.tracks.filter { it.isAudio }
        var got: Pair<String?, Boolean>? = null
        compose.setContent { Themed { QuickRecordDialog(audio, solo = false, onDismiss = {}) { id, solo -> got = id to solo } } }
        shot("55_quick_record")
        compose.onNodeWithTag("quick_record_solo").performClick()
        compose.onNodeWithTag("quick_record_new").performClick()
        assertEquals(null to true, got)
        compose.onNodeWithTag("quick_record_${audio.first().id}").performClick()
        assertEquals(audio.first().id to true, got)
    }

    private fun androidx.compose.ui.test.junit4.ComposeContentTestRule.onNodeWithText(text: String, substring: Boolean = false) =
        onNode(androidx.compose.ui.test.hasText(text, substring = substring), useUnmergedTree = true)
}
