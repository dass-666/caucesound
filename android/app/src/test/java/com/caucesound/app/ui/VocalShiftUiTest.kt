package com.caucesound.app.ui

import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Canvas
import androidx.compose.ui.graphics.ImageBitmap
import androidx.compose.ui.graphics.drawscope.CanvasDrawScope
import androidx.compose.ui.test.doubleClick
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.hasTestTag
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipeRight
import androidx.compose.ui.test.swipeUp
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.LayoutDirection
import com.caucesound.app.data.PluginParser
import com.caucesound.app.data.PluginUi
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.StateParser
import com.caucesound.app.data.VocalParams
import com.github.takahirom.roborazzi.captureRoboImage
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode
import java.io.File
import java.util.Locale

/**
 * Rediseño «Dark Rose / Neon Violet» de VocalShift. Datos reales del motor (fixtures JSON). Las capturas son renders
 * JVM (Robolectric, gráficos nativos), en español; NO son capturas de un teléfono.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], qualifiers = "w411dp-h891dp-xxhdpi")
class VocalShiftUiTest {
    @get:Rule val compose = createComposeRule()

    private var savedLocale: Locale = Locale.getDefault()
    @Before fun spanish() { savedLocale = Locale.getDefault(); Locale.setDefault(Locale("es", "ES")) }
    @After fun restore() { Locale.setDefault(savedLocale) }

    private fun res(name: String) = javaClass.classLoader!!.getResource("fixtures/$name")!!.readText()
    private val stateVocal: ProjectState by lazy { StateParser.state(res("state_vocal.json"))!! }
    private val stateBeat: ProjectState by lazy { StateParser.state(res("state_beat.json"))!! }
    private val presets by lazy { PluginParser.vocalPresets(res("vocal_presets.json")) }
    private fun ui() = PluginUi(presets, null, PluginParser.vocalResult(org.json.JSONObject(res("vocal_job.json")).getJSONObject("result")))
    private val urban get() = presets.first { it.id == "urban_spatial" }.params

    private class Rec : StudioActions {
        val calls = mutableListOf<String>()
        var vocal: VocalParams? = null
        override fun vocalProcess(clipId: String, params: VocalParams) { calls += "vocal"; vocal = params }
        override fun setClipVersion(clipId: String, original: Boolean, match: Boolean?) { calls += "version:$original" }
    }

    @androidx.compose.runtime.Composable
    private fun Themed(content: @androidx.compose.runtime.Composable () -> Unit) = CauceTheme {
        androidx.compose.material3.Surface(color = VS.Bg, contentColor = VS.Text) { content() }
    }

    private fun shot(name: String) {
        compose.waitForIdle()
        compose.onRoot().captureRoboImage("build/outputs/roborazzi/$name.png")
    }

    /** Toma sin procesar, pantalla Fácil, y pulsar Procesar: devuelve los parámetros que se mandarían al motor. */
    private fun processed(): VocalParams? {
        rec.calls.clear()
        compose.onNodeWithTag("vocal_process").performScrollTo().performClick()
        return rec.vocal
    }
    private val rec = Rec()
    private fun open(state: ProjectState = stateBeat, peak: Float = 0f, playing: Boolean = false) {
        val clip = state.tracks.first { it.name == "Voz" }.clips.first()
        compose.setContent { Themed { VocalShiftPanel(state, clip.id, ui(), emptyMap(), rec, trackPeak = { peak }, playing = playing) } }
    }

    // --- Cada control escribe el mismo campo de VocalParams que antes (y solo ese) ---------------------------------

    @Test fun pitch_knob_drag_double_tap_and_shortcuts_change_only_semitones() {
        open()
        compose.onNodeWithTag("vocal_pitch").performScrollTo().performTouchInput { swipeUp() }
        val up = processed()!!
        assertTrue("arrastre vertical hacia arriba sube el tono: ${up.semitones}", up.semitones > urban.semitones)
        assertEquals(urban.copy(semitones = up.semitones, preset = ""), up)
        assertEquals(up.semitones, up.semitones.toInt().toFloat()) // PITCH va por semitonos enteros
        compose.onNodeWithTag("vocal_pitch").performScrollTo().performTouchInput { doubleClick() }
        assertEquals(urban.copy(semitones = 7f, preset = ""), processed())
        compose.onNodeWithTag("vocal_pitch_-12").performScrollTo().performClick()
        assertEquals(urban.copy(semitones = -12f, preset = ""), processed())
    }

    @Test fun route_mode_and_fader_change_the_same_fields_as_before() {
        open()
        compose.onNodeWithTag("vocal_mode_1").performScrollTo().performClick()
        assertEquals(urban.copy(radical = true, preset = ""), processed())
        compose.onNodeWithTag("vocal_second_level").performScrollTo().performTouchInput { swipeRight() }
        val f = processed()!!
        assertTrue("el fader sube la 2.ª voz: ${f.secondGainDb}", f.secondGainDb > urban.secondGainDb)
        assertEquals(urban.copy(radical = true, secondGainDb = f.secondGainDb, preset = ""), f)
        // Ruta Transformar: desaparece el fader y Espacio queda deshabilitado.
        compose.onNodeWithTag("vocal_second_0").performScrollTo().performClick()
        assertEquals(urban.copy(radical = true, secondGainDb = f.secondGainDb, secondVoice = false, preset = ""), processed())
        assertTrue(compose.onAllNodes(hasTestTag("vocal_second_level")).fetchSemanticsNodes().isEmpty())
    }

    @Test fun pro_view_fine_tune_is_stored_inside_semitones_like_the_engine_adapter() {
        open()
        compose.onNodeWithTag("vocal_view_1").performClick()
        compose.onNodeWithTag("vocal_fine").performScrollTo().performTouchInput { swipeUp(startY = centerY, endY = centerY - 40f) }
        val p = processed()!!
        val (st, ct) = vocalPitchSplit(p.semitones)
        assertEquals(7, st)
        assertTrue("cents > 0: $ct", ct > 0)
        assertEquals(urban.copy(semitones = p.semitones, preset = ""), p)
        // Pan y desfase solo con Segunda voz; Mezcla solo con Transformar.
        compose.onNodeWithTag("vocal_pan").performScrollTo().performTouchInput { swipeUp() }
        val q = processed()!!
        assertEquals(p.copy(pan = q.pan), q)
        assertTrue(q.pan > 0f)
        compose.onNodeWithTag("vocal_mix").performScrollTo().performTouchInput { swipeUp() }
        assertEquals(q, processed()) // deshabilitada con Segunda voz: no cambia nada
    }

    @Test fun pitch_split_round_trips() {
        for (v in listOf(7f, 7.3f, -7.8f, 0.5f, -24f, 24f, 12.99f)) {
            val (st, ct) = vocalPitchSplit(v)
            assertEquals(v, vocalPitchJoin(st, ct), 0.0051f)
        }
        assertEquals(24f, vocalPitchJoin(24, 80))
    }

    @Test fun preset_sheet_applies_the_engine_preset() {
        open()
        val other = presets.first { it.id != "urban_spatial" }
        compose.onNodeWithTag("vocal_preset_picker").performClick()
        compose.onNodeWithTag("vocal_preset_${other.id}").performScrollTo().performClick()
        assertEquals(other.params, processed())
    }

    @Test fun bypass_switches_to_the_original_version() {
        open(stateVocal)
        compose.onNodeWithTag("vocal_bypass").performClick()
        assertEquals(listOf("version:true"), rec.calls)
    }

    @Test fun bypass_is_disabled_without_a_processed_version() {
        open(stateBeat) // sin versión procesada: no hay nada que puentear
        compose.onNodeWithTag("vocal_bypass").assertIsNotEnabled().performClick()
        assertTrue(rec.calls.isEmpty())
    }

    // --- Capa reactiva: nivel suavizado, sin repintar en silencio, nada corriendo al cerrar ------------------------

    @Test fun level_follows_the_spec_formula() {
        assertEquals(1f, SingerLevel.target(1f), 1e-6f)
        assertEquals(0f, SingerLevel.target(0f), 1e-6f)
        assertEquals(0.5f, SingerLevel.target(Math.pow(10.0, -24.0 / 20).toFloat()), 1e-4f) // −24 dBFS
        assertEquals(0.5f, SingerLevel.next(0f, 1f), 1e-6f)      // sube rápido
        assertEquals(0.45f, SingerLevel.next(0.5f, 0f), 1e-6f)   // cae suave
        assertEquals(0f, SingerLevel.next(0.0021f, 0f), 0f)      // por debajo de 0,002 se apaga del todo
        assertTrue(!SingerLevel.shouldRepaint(0.5f, 0.502f))
        assertTrue(SingerLevel.shouldRepaint(0.5f, 0.504f))
    }

    @Test fun silence_does_not_repaint_and_closing_leaves_nothing_running() {
        compose.mainClock.autoAdvance = false
        var playing by mutableStateOf(true)
        var peak = 0f
        var show by mutableStateOf(true)
        compose.setContent { if (show) SingerIllustration({ peak }, playing, androidx.compose.ui.Modifier.fillMaxSizeForTest()) }
        // Se cuentan las invalidaciones de la capa reactiva (cada una provoca un repintado de esa capa, y solo de ella).
        compose.mainClock.advanceTimeBy(200)
        val quiet = SingerDebug.levelUpdates.get()
        compose.mainClock.advanceTimeBy(3000) // 3 s sonando en silencio
        assertEquals("en silencio no se repinta", quiet, SingerDebug.levelUpdates.get())
        assertEquals(1, SingerDebug.loopsRunning.get())
        peak = 0.5f
        compose.mainClock.advanceTimeBy(1000)
        val lit = SingerDebug.levelUpdates.get() - quiet
        assertTrue("con voz se repinta, como mucho a 30 fps: $lit", lit in 1..31)
        playing = false // se para el transporte: el brillo se apaga y el bucle termina solo
        compose.mainClock.advanceTimeBy(5000)
        assertEquals(0, SingerDebug.loopsRunning.get())
        val off = SingerDebug.levelUpdates.get()
        compose.mainClock.advanceTimeBy(2000)
        assertEquals(off, SingerDebug.levelUpdates.get())
        playing = true; peak = 0.8f
        androidx.compose.runtime.snapshots.Snapshot.sendApplyNotifications()
        compose.mainClock.advanceTimeBy(1000)
        println("loops tras reanudar: ${SingerDebug.loopsRunning.get()}, updates ${SingerDebug.levelUpdates.get() - off}")
        assertTrue(SingerDebug.levelUpdates.get() > off)
        show = false // cerrar la pantalla con la voz sonando
        androidx.compose.runtime.snapshots.Snapshot.sendApplyNotifications()
        compose.mainClock.advanceTimeBy(500)
        assertEquals("al cerrar no queda ningún bucle", 0, SingerDebug.loopsRunning.get())
    }

    @Test fun static_layer_is_cached_while_only_the_glow_changes() {
        val before = SingerDebug.staticRenders.get()
        val level = androidx.compose.runtime.mutableFloatStateOf(0f)
        var view: android.view.View? = null
        compose.setContent {
            view = androidx.compose.ui.platform.LocalView.current
            SingerIllustration({ 0f }, false, androidx.compose.ui.Modifier.fillMaxSizeForTest(), levelState = level)
        }
        // Dibujado real de la vista en un lienzo por software (Robolectric no dibuja la ventana por sí solo).
        fun draw() { compose.waitForIdle(); compose.runOnIdle { val v = view!!; val b = android.graphics.Bitmap.createBitmap(v.width, v.height, android.graphics.Bitmap.Config.ARGB_8888); v.draw(android.graphics.Canvas(b)) } }
        draw()
        val draws = SingerDebug.reactiveDraws.get()
        for (v in listOf(0.3f, 0.6f, 0.9f)) { level.floatValue = v; draw() }
        assertTrue("la capa reactiva se repinta", SingerDebug.reactiveDraws.get() >= draws + 3)
        assertEquals("una sola rasterización de la parte estática", 1, SingerDebug.staticRenders.get() - before)
    }

    /**
     * Medición en la JVM (x86, Robolectric con Skia nativo, sin GPU): NO es una medida de teléfono. Sirve para comparar
     * con la referencia de escritorio (0,46 ms por fotograma con caché, ~15 ms al regenerar).
     */
    @Test fun measure_draw_cost_on_the_jvm() {
        val d = Density(2.625f)
        val w = 1080; val h = 950
        repeat(2) { renderSingerStatic(w, h, d) } // calentamiento
        val n1 = 5
        val t0 = System.nanoTime()
        repeat(n1) { renderSingerStatic(w, h, d) }
        val staticMs = (System.nanoTime() - t0) / 1e6 / n1
        val bmp = ImageBitmap(w, h)
        val scope = CanvasDrawScope()
        fun frame(l: Float) = scope.draw(d, LayoutDirection.Ltr, Canvas(bmp), Size(w.toFloat(), h.toFloat())) { drawSingerReactive(l) }
        repeat(50) { frame(0.7f) }
        val n2 = 300
        val t1 = System.nanoTime()
        repeat(n2) { frame(0.3f + 0.5f * (it % 10) / 10f) }
        val reactiveMs = (System.nanoTime() - t1) / 1e6 / n2
        val report = "VocalShift ilustración, JVM x86 (Robolectric, Skia por software), panel ${w}x$h px:\n" +
            "  capa estática (se rehace solo al cambiar el tamaño): %.2f ms\n".format(staticMs) +
            "  capa reactiva por fotograma (nivel 0,3-0,8): %.3f ms\n".format(reactiveMs)
        println(report)
        File("build/outputs").mkdirs()
        File("build/outputs/vocalshift_draw_cost.txt").writeText(report)
        assertTrue(reactiveMs < 16.0)
    }

    // --- Capturas (renders JVM, no de teléfono) -----------------------------------------------------------------

    private fun captureSet(prefix: String) {
        var playing by mutableStateOf(true)
        val clip = stateVocal.tracks.first { it.name == "Voz" }.clips.first()
        compose.mainClock.autoAdvance = false
        compose.setContent { Themed { VocalShiftPanel(stateVocal, clip.id, ui(), emptyMap(), rec, trackPeak = { 0.55f }, playing = playing) } }
        compose.mainClock.advanceTimeBy(600) // la voz «suena»: el brillo sube
        shot("${prefix}_facil")
        // Para desplazar la pantalla en la prueba hace falta que no haya animación: se para el transporte.
        playing = false
        compose.mainClock.advanceTimeBy(4000)
        compose.mainClock.autoAdvance = true
        compose.onNodeWithTag("vocal_effective").performScrollTo()
        shot("${prefix}_facil_b")
        compose.onNodeWithTag("vocal_view_1").let { n -> runCatching { n.performScrollTo() }; n.performClick() }
        shot("${prefix}_pro")
        compose.onNodeWithTag("vocal_mix").performScrollTo()
        shot("${prefix}_pro_b")
        compose.onNodeWithTag("vocal_effective").performScrollTo()
        shot("${prefix}_pro_c")
    }

    @Config(qualifiers = "w360dp-h780dp-xxhdpi") @Test fun shots_small_phone() = captureSet("60_vs_pequeno")
    @Config(qualifiers = "w412dp-h915dp-xxhdpi") @Test fun shots_large_phone() = captureSet("61_vs_grande")
    @Config(qualifiers = "w800dp-h1280dp-xhdpi") @Test fun shots_tablet() = captureSet("62_vs_tableta")
    @Config(qualifiers = "w780dp-h360dp-land-xxhdpi") @Test fun shots_phone_landscape() = captureSet("63_vs_horizontal")
    @Config(qualifiers = "w1280dp-h800dp-land-xhdpi") @Test fun shots_tablet_landscape() = captureSet("64_vs_tableta_horizontal")

    @Config(qualifiers = "w360dp-h780dp-xxhdpi") @Test fun shots_small_phone_silent_and_sheet() {
        open(stateBeat)
        shot("65_vs_pequeno_silencio")
        compose.onNodeWithTag("vocal_preset_picker").performClick()
        shot("66_vs_presets")
    }
}

private fun androidx.compose.ui.Modifier.fillMaxSizeForTest() = this.fillMaxSize()
