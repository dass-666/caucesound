package com.caucesound.app.ui

import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.test.hasAnyAncestor
import androidx.compose.ui.test.hasContentDescription
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onAllNodesWithTag
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performTextInput
import com.caucesound.app.data.AutoMixParser
import com.caucesound.app.data.Catalog
import com.caucesound.app.data.PPQ
import com.caucesound.app.data.PackParser
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.SoundBrowserFilter
import com.caucesound.app.data.SoundUi
import com.caucesound.app.data.StateParser
import com.github.takahirom.roborazzi.captureRoboImage
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

/**
 * Misión 0.9: explorador de Sonidos Cauce, Cauce Sampler en la pista y avisos de sonidos que faltan. Datos reales del
 * motor: packs.json (paquetes generados por cauce_packs), state_sampler.json (Boom Bap con el kit Raw Boom Bap y el bajo
 * eléctrico) y state_missing_sound.json (el mismo proyecto abierto sin el paquete del bajo). Comprueban qué comandos
 * envía la interfaz; el sonido lo comprueban las pruebas del motor.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], qualifiers = "w411dp-h891dp-xxhdpi")
class SoundBrowserTest {
    @get:Rule val compose = createComposeRule()

    private fun res(name: String) = javaClass.classLoader!!.getResource("fixtures/$name")!!.readText()
    private val packs by lazy { PackParser.packs(res("packs.json")) }
    private val sampler: ProjectState by lazy { StateParser.state(res("state_sampler.json"))!! }
    private val missing: ProjectState by lazy { StateParser.state(res("state_missing_sound.json"))!! }
    private val catalog: Catalog by lazy { Catalog.parse(res("catalog.json")) }

    private class Rec : StudioActions {
        val calls = mutableListOf<String>()
        override fun previewSound(key: String) { calls += "preview:$key" }
        override fun stopSoundPreview() { calls += "stop" }
        override fun loadSound(trackId: String, key: String) { calls += "load:$trackId:$key" }
        override fun toggleFavoriteSound(key: String) { calls += "fav:$key" }
        override fun askRemovePack(packId: String) { calls += "remove:$packId" }
        override fun selectTrack(id: String) { calls += "select:$id" }
        override fun openPanel(panel: Panel) { calls += "panel:$panel" }
        override fun setParam(trackId: String, param: String, value: Float) { calls += "param:$param" }
        override fun autoMixSuggest() { calls += "automix" }
    }

    @androidx.compose.runtime.Composable
    private fun Studio(state: ProjectState, panel: Panel, rec: StudioActions, selectedTrack: String, sounds: SoundUi = SoundUi(packs)) {
        val meters = remember { mutableStateOf(FloatArray(state.tracks.size + 1) { 0.2f }) }
        CauceTheme {
            androidx.compose.material3.Surface(color = Palette.Background, contentColor = Palette.Text) {
                StudioLayout(
                    state, panel, null, selectedTrack, emptyMap(), catalog, false, remember { mutableLongStateOf(state.ticksToFrames(PPQ * 2L)) }, false,
                    meters, rec, androidx.compose.material3.SnackbarHostState(), {}, {}, {}, {}, sounds = sounds,
                )
            }
        }
    }

    private fun shot(name: String) {
        compose.waitForIdle()
        compose.onRoot().captureRoboImage("build/outputs/roborazzi/$name.png")
    }

    private val drums get() = sampler.tracks.first { it.isDrums }
    private val bass get() = sampler.tracks.first { it.isSampler }

    // --- Datos ---------------------------------------------------------------------------------------------------

    @Test fun packs_and_sampler_state_parse_from_the_real_engine() {
        assertEquals(setOf("cauce.signature.drums", "cauce.bass.lab", "cauce.bass.electric"), packs.map { it.id }.toSet())
        assertTrue(packs.all { it.factory && it.complete && it.bytes > 0 })
        val all = packs.flatMap { it.instruments }
        assertEquals(3, all.count { it.isDrumKit })
        assertTrue(all.first { it.key == "cauce.bass.electric:electric-bass" }.origin == "cc0")
        assertEquals("cauce.signature.drums:raw-boombap", drums.soundKey)
        assertEquals("cauce.bass.electric:electric-bass", bass.soundKey)
        assertTrue(sampler.samplers.getValue(bass.id).loaded)
        assertTrue(sampler.missingInstruments.isEmpty())
        assertEquals(listOf("cauce.bass.electric:electric-bass"), missing.missingInstruments)
        assertFalse(missing.samplers.getValue(bass.id).loaded)
        // El sonido que falta no se sustituye: la pista conserva su clave.
        assertEquals(bass.soundKey, missing.tracks.first { it.id == bass.id }.soundKey)
        assertEquals(6, catalog.samplerParams.size)
    }

    @Test fun filters_search_categories_genres_and_kind() {
        val all = packs.flatMap { it.instruments }
        assertEquals(listOf("drums", "bass"), SoundBrowserFilter.categories(all)) // solo familias con contenido instalado
        assertTrue(SoundBrowserFilter.filter(all, "", "all", null, emptySet(), "drumkit").all { it.isDrumKit })
        assertTrue(SoundBrowserFilter.filter(all, "", "all", null, emptySet(), "melodic").none { it.isDrumKit })
        assertEquals(listOf("cauce.bass.lab:808-lab"), SoundBrowserFilter.filter(all, "808", "all", null, emptySet(), "melodic").map { it.key })
        assertTrue(SoundBrowserFilter.filter(all, "808", "all", null, emptySet()).any { it.key == "cauce.signature.drums:dark-trap" }) // la etiqueta 808 del kit también cuenta
        // Sin acentos ni mayúsculas: «eléctrico» encuentra el bajo eléctrico por su descripción.
        assertTrue(SoundBrowserFilter.filter(all, "ELECTRICO", "all", null, emptySet()).any { it.key == "cauce.bass.electric:electric-bass" })
        assertTrue(SoundBrowserFilter.filter(all, "", "all", "trap", emptySet()).all { "trap" in it.genres })
        val fav = setOf("cauce.bass.lab:deep-sub")
        assertEquals(listOf("cauce.bass.lab:deep-sub"), SoundBrowserFilter.filter(all, "", "fav", null, fav).map { it.key })
        assertEquals("cauce.bass.lab:deep-sub", SoundBrowserFilter.filter(all, "", "all", null, fav).first().key) // favoritos primero
        assertEquals("1.5 MB", SoundBrowserFilter.sizeLabel(1572864))
    }

    @Test fun automix_suggestions_parse() {
        val ui = AutoMixParser.parse("""{"ok":true,"canRevert":false,"suggestions":[{"trackId":"t1","name":"Keys","role":"chords","reason":"tapa a las demás para su papel","measuredDb":-27.2,"targetDb":-36.3,"volumeDb":7,"suggestedVolumeDb":1,"pan":0,"suggestedPan":0,"change":true}]}""")
        assertTrue(ui.open)
        assertEquals(1f, ui.suggestions.single().suggestedVolumeDb)
    }

    // --- Pantallas -----------------------------------------------------------------------------------------------

    @Test fun browser_for_a_drum_track_lists_kits_previews_and_loads() {
        val rec = Rec()
        compose.setContent { Studio(sampler, Panel.SOUNDS, rec, drums.id) }
        shot("90_sonidos_kits")
        // Una batería solo ve kits.
        compose.onNodeWithTag("sound_cauce.signature.drums:dark-trap").assertExists()
        assertEquals(0, compose.onAllNodesWithTag("sound_cauce.bass.lab:808-lab").fetchSemanticsNodes().size)
        compose.onNode(hasContentDescription("Preview") and hasAnyAncestor(hasTestTag("sound_cauce.signature.drums:dark-trap")), useUnmergedTree = true).performClick()
        assertEquals("preview:cauce.signature.drums:dark-trap", rec.calls.last())
        // El kit cargado no ofrece «Usar»; los otros dos sí.
        compose.onNode(hasText("Use on “${drums.name}”") and hasAnyAncestor(hasTestTag("sound_cauce.signature.drums:dark-trap")), useUnmergedTree = true).performClick()
        assertEquals("load:${drums.id}:cauce.signature.drums:dark-trap", rec.calls.last())
        compose.onNode(hasText("Loaded on track") and hasAnyAncestor(hasTestTag("sound_cauce.signature.drums:raw-boombap")), useUnmergedTree = true).assertExists()
    }

    @Test fun browser_search_for_an_instrument_track() {
        val rec = Rec()
        compose.setContent { Studio(sampler, Panel.SOUNDS, rec, bass.id) }
        compose.onNodeWithTag("sounds_search").performTextInput("808")
        compose.waitForIdle()
        shot("91_sonidos_buscar_808")
        compose.onNodeWithTag("sound_cauce.bass.lab:808-lab").assertExists()
        assertEquals(0, compose.onAllNodesWithTag("sound_cauce.bass.electric:electric-bass").fetchSemanticsNodes().size)
        compose.onNodeWithText("Use on “${bass.name}”").performClick()
        assertEquals("load:${bass.id}:cauce.bass.lab:808-lab", rec.calls.last())
    }

    @Test fun packs_tab_shows_sizes_licenses_and_no_delete_for_built_in() {
        val rec = Rec()
        compose.setContent { Studio(sampler, Panel.SOUNDS, rec, drums.id) }
        compose.onNodeWithText("Packs").performClick()
        shot("92_sonidos_paquetes")
        compose.onNodeWithTag("pack_cauce.bass.electric").assertExists()
        compose.onNodeWithTag("import_pack").assertExists()
        assertEquals(0, compose.onAllNodesWithText("Delete pack", useUnmergedTree = true).fetchSemanticsNodes().size) // de fábrica: no se borran
    }

    @Test fun sampler_panel_shows_controls_and_opens_the_browser() {
        val rec = Rec()
        compose.setContent { Studio(sampler, Panel.SYNTH, rec, bass.id) }
        shot("93_cauce_sampler")
        compose.onNodeWithText("Cauce Electric Bass").assertExists()
        compose.onNodeWithTag("open_sounds").performClick()
        assertEquals(listOf("select:${bass.id}", "panel:SOUNDS"), rec.calls.takeLast(2))
    }

    @Test fun missing_sound_is_announced_and_track_keeps_its_data() {
        val rec = Rec()
        compose.setContent { Studio(missing, Panel.ARRANGE, rec, drums.id) }
        shot("94_sonido_que_falta")
        compose.onNodeWithTag("missing_sounds").assertExists()
    }

    @Test fun rack_shows_the_sample_kit_name_and_mixer_offers_automix() {
        val rec = Rec()
        compose.setContent { Studio(sampler, Panel.RACK, rec, drums.id) }
        compose.onNodeWithText("Cauce Drums · Cauce Raw Boom Bap", substring = true).assertExists()
        shot("95_rack_kit_muestras")
    }

    @Test fun mixer_has_automix_button() {
        val rec = Rec()
        compose.setContent { Studio(sampler, Panel.MIXER, rec, drums.id) }
        compose.onNodeWithTag("automix").performClick()
        assertEquals("automix", rec.calls.last())
    }
}
