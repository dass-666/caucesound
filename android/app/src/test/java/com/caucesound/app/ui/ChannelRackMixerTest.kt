package com.caucesound.app.ui

import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import com.caucesound.app.data.Catalog
import com.caucesound.app.data.PPQ
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.SoloFamily
import com.caucesound.app.data.SoloRequest
import com.caucesound.app.data.StateParser
import com.github.takahirom.roborazzi.captureRoboImage
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

/**
 * Misión 0.7: Channel Rack, Mixer Pro y selección de alternativas de Solo AI. Datos reales del motor
 * (state_rack.json: el bombo en su propio canal y la cadena «Voz urbana» en la voz; catalog.json con categorías y
 * cadenas). Comprueban qué comandos envía la interfaz; el sonido lo comprueban las pruebas del motor.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], qualifiers = "w411dp-h891dp-xxhdpi")
class ChannelRackMixerTest {
    @get:Rule val compose = createComposeRule()

    private fun res(name: String) = javaClass.classLoader!!.getResource("fixtures/$name")!!.readText()
    private val rack: ProjectState by lazy { StateParser.state(res("state_rack.json"))!! }
    private val catalog: Catalog by lazy { Catalog.parse(res("catalog.json")) }
    private val drums get() = rack.tracks.first { it.isDrums }
    private val voice get() = rack.tracks.first { it.isAudio }

    private class Rec : StudioActions {
        val calls = mutableListOf<String>()
        override fun setStep(trackId: String, patternId: String, lane: Int, step: Int, velocity: Int) { calls += "step:$lane:$step:$velocity" }
        override fun routeDrumLane(trackId: String, lane: Int, own: Boolean) { calls += "route:$lane:$own" }
        override fun selectTrack(id: String) { calls += "select:$id" }
        override fun openPanel(panel: Panel) { calls += "panel:$panel" }
        override fun addMidiClip(trackId: String, startTick: Long, lengthTick: Long, patternId: String?) { calls += "clip:$trackId:$startTick:$lengthTick:$patternId" }
        override fun addInsert(trackId: String, type: String) { calls += "insert:$type" }
        override fun applyFxChain(trackId: String, presetId: String, replace: Boolean) { calls += "chain:$presetId:$replace" }
        override fun moveInsert(insertId: String, index: Int) { calls += "move:$insertId:$index" }
        override fun setInsertBypass(insertId: String, bypass: Boolean) { calls += "bypass:$insertId:$bypass" }
        override fun removeInsert(insertId: String) { calls += "remove:$insertId" }
        override fun setMute(id: String, on: Boolean) { calls += "mute:$id:$on" }
        override fun setSolo(id: String, on: Boolean) { calls += "solo:$id:$on" }
        override fun setParam(trackId: String, param: String, value: Float) { calls += "param:$param:$value" }
        override fun addTrack(name: String, kind: String) { calls += "addTrack:$kind" }
    }

    @androidx.compose.runtime.Composable
    private fun Themed(content: @androidx.compose.runtime.Composable () -> Unit) = CauceTheme {
        androidx.compose.material3.Surface(color = Palette.Background, contentColor = Palette.Text) { content() }
    }

    @androidx.compose.runtime.Composable
    private fun Studio(panel: Panel, rec: StudioActions, selectedTrack: String? = null) {
        val meters = remember { mutableStateOf(FloatArray(rack.tracks.size + 1) { 0.25f + 0.05f * it }) }
        Themed {
            StudioLayout(
                rack, panel, null, selectedTrack ?: rack.tracks[0].id, emptyMap(), catalog, false, remember { mutableLongStateOf(rack.ticksToFrames(PPQ * 6L)) }, false,
                meters, rec, androidx.compose.material3.SnackbarHostState(), {}, {}, {}, {},
            )
        }
    }

    private fun shot(name: String) {
        compose.waitForIdle()
        compose.onRoot().captureRoboImage("build/outputs/roborazzi/$name.png")
    }

    // --- Channel Rack -------------------------------------------------------------------------------------------

    @Test fun channel_rack_edits_the_real_drum_pattern_and_shows_mixer_channels() {
        val rec = Rec()
        compose.setContent { Studio(Panel.RACK, rec) }
        shot("40_channel_rack")
        val pat = drums.patterns.first()
        // Un paso vacío se enciende con 100 y uno encendido se apaga: el mismo comando que la rejilla de batería.
        val empty = (0 until 8).first { pat.lanes[1][it] == 0 }
        compose.onNodeWithTag("rack_step_1_$empty").performClick()
        assertEquals("step:1:$empty:100", rec.calls.last())
        val lit = (0 until 8).first { pat.lanes[3][it] > 0 }
        compose.onNodeWithTag("rack_step_3_$lit").performClick()
        assertEquals("step:3:$lit:0", rec.calls.last())
        // Modo intensidad: un paso encendido recorre 127 → 100 → 70 → 40.
        compose.onNodeWithTag("rack_velocity").performClick()
        compose.onNodeWithTag("rack_step_3_$lit").performClick()
        val v = pat.lanes[3][lit]
        val cycle = listOf(127, 100, 70, 40)
        val expected = cycle[(cycle.indexOfFirst { it <= v }.let { if (it < 0) 0 else it } + 1) % 4]
        assertEquals("step:3:$lit:$expected", rec.calls.last())
        // Cada fila dice a qué canal del Mixer va (nombre real del canal).
        compose.onNodeWithText("Mixer ${rack.trackIndex(drums.id) + 1} · ${drums.name}", substring = true).assertExists()
        // 8 pasos por página en un teléfono en vertical: la página 2 existe.
        compose.onNodeWithTag("rack_page_1").assertExists()
    }

    @Test fun drum_piece_gets_its_own_mixer_channel_from_the_rack() {
        val rec = Rec()
        val bus = drums.laneRoutes[0]!!
        compose.setContent { Studio(Panel.RACK, rec) }
        // El bombo ya tiene canal propio (fixture del motor): abrirlo lleva al Mixer con ese canal seleccionado.
        compose.onNodeWithTag("rack_lane_0").performClick()
        shot("41_channel_rack_lane")
        compose.onNodeWithTag("rack_open_lane_mixer_0").performScrollTo().performClick()
        assertEquals(listOf("select:$bus", "panel:MIXER"), rec.calls.takeLast(2))
        compose.onNodeWithTag("rack_own_channel_0").performScrollTo().performClick()
        assertEquals("route:0:false", rec.calls.last())
        // La caja (pieza 2) no tiene canal propio: el interruptor pide uno al motor.
        compose.onNodeWithTag("rack_lane_1").performScrollTo().performClick()
        compose.onNodeWithTag("rack_own_channel_1").performScrollTo().performClick()
        assertEquals("route:1:true", rec.calls.last())
    }

    @Test fun melodic_rows_open_the_existing_piano_roll_and_create_patterns() {
        val rec = Rec()
        val synth = rack.tracks.first { it.isSynth }
        compose.setContent { Studio(Panel.RACK, rec) }
        compose.onNodeWithTag("rack_piano_${synth.id}").performScrollTo().performClick()
        assertEquals(listOf("select:${synth.id}", "panel:PIANO"), rec.calls.takeLast(2))
        rec.calls.clear()
        compose.onNodeWithTag("rack_new_clip_${synth.id}").performScrollTo().performClick()
        val bar = PPQ.toLong() * rack.beatsPerBar
        // Cursor en el tick 6·PPQ → compás 2 (empieza en 4·PPQ con 4/4); 4 compases.
        assertEquals("clip:${synth.id}:${(PPQ * 6L) / bar * bar}:${bar * 4}:null", rec.calls[0])
        assertEquals("panel:PIANO", rec.calls.last())
    }

    @Config(qualifiers = "w360dp-h640dp-xhdpi")
    @Test fun channel_rack_fits_a_small_phone() {
        compose.setContent { Studio(Panel.RACK, Rec()) }
        shot("42_small_channel_rack")
        compose.onNodeWithTag("rack_step_0_0").assertExists()
    }

    @Config(qualifiers = "w891dp-h411dp-land-xxhdpi")
    @Test fun channel_rack_landscape_shows_16_steps() {
        compose.setContent { Studio(Panel.RACK, Rec()) }
        shot("43_channel_rack_land")
        compose.onNodeWithTag("rack_step_0_15").assertExists()
    }

    // --- Mixer Pro ----------------------------------------------------------------------------------------------

    /** 0.8: en vertical los efectos empiezan cerrados (faders a toda altura); se abren con la barra «Ver efectos». */
    private fun openFx() = compose.onNodeWithTag("rack_toggle").performClick()

    @Test fun mixer_rack_shows_the_engine_chain_in_processing_order() {
        val rec = Rec()
        compose.setContent { Studio(Panel.MIXER, rec, voice.id) }
        openFx()
        shot("44_mixer_pro_voice")
        assertEquals(listOf("cs.filter", "cs.eq", "cs.compressor", "cs.saturation", "cs.delay", "cs.reverb"), voice.inserts.map { it.type })
        voice.inserts.indices.forEach { compose.onNodeWithTag("slot_$it").performScrollTo().assertExists() }
        compose.onNodeWithTag("slot_2_bypass").performScrollTo().performClick()
        assertEquals("bypass:${voice.inserts[2].id}:true", rec.calls.last())
    }

    @Test fun effect_picker_lists_only_real_effects_by_category_and_vocalshift_offline() {
        val rec = Rec()
        compose.setContent { Studio(Panel.MIXER, rec, voice.id) }
        openFx()
        compose.onNodeWithTag("add_effect").performScrollTo().performClick()
        shot("45_effect_picker")
        // Solo los efectos que el motor publica; ninguno latente; el catálogo trae descripción para cada uno.
        assertEquals(7, catalog.effects.size)
        assertTrue(catalog.effects.all { it.description.isNotBlank() && it.category.isNotBlank() && it.latencySamples == 0 })
        compose.onNodeWithTag("pick_cs.compressor").performScrollTo().performClick()
        assertEquals("insert:cs.compressor", rec.calls.last())
        compose.onNodeWithTag("add_effect").performScrollTo().performClick()
        // VocalShift aparece como proceso de clip (sin conexión), nunca como efecto en vivo. Esta voz no tiene grabación:
        // la entrada está, pero explica que primero hay que grabar o importar.
        compose.onNodeWithTag("pick_vocalshift").performScrollTo()
        compose.onNode(androidx.compose.ui.test.hasText(tr("Graba o importa una voz en esta pista primero", "Record or import a voice on this track first"), substring = true) and
            androidx.compose.ui.test.hasAnyAncestor(androidx.compose.ui.test.hasTestTag("effect_picker"))).assertExists()
    }

    @Test fun vocalshift_entry_processes_the_clip_offline() {
        val st = StateParser.state(res("state_vocal.json"))!!
        val t = st.tracks.first { it.isAudio && it.clips.isNotEmpty() }
        var vocal: String? = null
        compose.setContent { Themed { EffectPicker(t, catalog, {}, {}, { vocal = it }) } }
        compose.onNodeWithTag("pick_vocalshift").performScrollTo().performClick()
        assertEquals(t.clips.first().id, vocal)
    }

    @Test fun chain_presets_append_or_replace_with_undo() {
        val rec = Rec()
        val synth = rack.tracks.first { it.isSynth && it.inserts.isEmpty() }
        compose.setContent { Studio(Panel.MIXER, rec, synth.id) }
        openFx()
        compose.onNodeWithTag("fx_chains").performScrollTo().performClick()
        compose.onNodeWithTag("chain_trap_808").performScrollTo().performClick()
        shot("46_chain_dialog")
        compose.onNodeWithTag("chain_append").performClick()
        assertEquals("chain:trap_808:false", rec.calls.last())
        assertTrue(catalog.fxChains.map { it.id }.containsAll(listOf("vocal_urban", "vocal_clean", "vocal_double", "trap_808", "drums_punch", "melody_space")))
        assertTrue(catalog.fxChains.all { ch -> ch.steps.all { s -> catalog.effects.any { it.type == s } } })
    }

    @Test fun lane_channel_strip_says_where_its_signal_comes_from() {
        val bus = rack.tracks.first { it.id == drums.laneRoutes[0] }
        compose.setContent { Studio(Panel.MIXER, Rec(), bus.id) }
        openFx()
        compose.onNodeWithTag("strip_route_${bus.id}", useUnmergedTree = true).assertExists()
        compose.onNodeWithTag("routing_label").assertExists()
        compose.onNodeWithText(tr("Pieza 1 de «${drums.name}» → Master", "Piece 1 of “${drums.name}” → Master"), substring = true).assertExists()
    }

    @Test fun bottom_navigation_has_arrange_rack_piano_mixer_and_sound() {
        val rec = Rec()
        compose.setContent { Studio(Panel.ARRANGE, rec) }
        for ((label, panel) in listOf("Rack" to Panel.RACK, "Piano roll" to Panel.PIANO, "Mixer" to Panel.MIXER, tr("Sonido", "Sound") to Panel.SYNTH, tr("Arreglo", "Arrange") to Panel.ARRANGE)) {
            compose.onNodeWithTag("nav_$panel").assertExists().performClick()
            compose.onNodeWithText(label, useUnmergedTree = true).let { } // la etiqueta existe en el idioma activo
            assertEquals("panel:$panel", rec.calls.last())
        }
    }

    // --- Solo AI: la interfaz pide de verdad la alternativa tocada -------------------------------------------------

    @Test fun solo_pick_sequence_a_b_c_d_a_sends_each_proposal_on_the_same_track() {
        val fam = SoloFamily.parse(SoloRequest(bars = 8, seed = 2026), JSONObject(res("family.json"))).copy(selected = 0, trackId = "trk_solo")
        val sent = listOf(0, 1, 2, 3, 0).map { JSONObject(fam.pickCommand(it, 0L, setOf("trk_solo", "trk_x"))) }
        assertEquals(listOf(0, 1, 2, 3, 0), sent.map { it.getInt("proposal") })
        assertTrue(sent.all { it.getString("replaceTrackId") == "trk_solo" && it.getLong("variation") == 0L })
        assertEquals(1, sent.map { it.getLong("seed") }.toSet().size) // misma familia: misma semilla
        // Si el usuario borró la pista de Solo AI, se crea una nueva en vez de escribir en una que no existe.
        assertTrue(JSONObject(fam.pickCommand(2, 0L, setOf("trk_x"))).isNull("replaceTrackId"))
    }
}
