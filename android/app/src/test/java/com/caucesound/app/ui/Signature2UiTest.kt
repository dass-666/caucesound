package com.caucesound.app.ui

import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import com.caucesound.app.data.Catalog
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.StateParser
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import org.robolectric.annotation.GraphicsMode

/**
 * Misión 0.10 — «Signature 2» en la interfaz: el interruptor de la pantalla de inicio (apagado crea la plantilla de
 * siempre; encendido pide "<estilo>@2") y el control de humanización del panel de Variaciones. El recorrido con el
 * motor real está en Signature2EndToEndTest.
 */
@RunWith(RobolectricTestRunner::class)
@GraphicsMode(GraphicsMode.Mode.NATIVE)
@Config(sdk = [35], qualifiers = "w411dp-h891dp-xxhdpi")
class Signature2UiTest {
    @get:Rule val compose = createComposeRule()

    private fun res(name: String) = javaClass.classLoader!!.getResource("fixtures/$name")!!.readText()
    private val trap: ProjectState by lazy { StateParser.state(res("state_trap04.json"))!! }
    private val catalog: Catalog by lazy { Catalog.parse(res("catalog.json")) }

    @Test fun home_toggle_is_off_by_default_and_asks_for_signature2_only_when_on() {
        val created = mutableListOf<String>()
        compose.setContent {
            CauceTheme { HomeScreen(emptyList(), catalog.templates, null, {}, object : HomeActions {
                override fun create(name: String, template: String) { created += template }
            }) }
        }
        compose.onNodeWithTag("template_trap").performClick()
        assertEquals(listOf("trap"), created) // sin tocar nada: la plantilla de siempre
        compose.onNodeWithTag("signature2_switch").performClick()
        compose.onNodeWithTag("template_trap").performClick()
        assertEquals(listOf("trap", "trap@2"), created)
        compose.onNodeWithTag("signature2_toggle").performClick() // la fila entera también lo cambia
        compose.onNodeWithTag("template_trap").performClick()
        assertEquals(listOf("trap", "trap@2", "trap"), created)
    }

    @Test fun variations_panel_has_humanize_and_sends_the_amount() {
        val sent = mutableListOf<Float>()
        val actions = object : StudioActions {
            override fun setHumanize(amount: Float) { sent += amount }
        }
        val st = trap.copy(music = trap.music!!.copy(style = 2, humanize = 0.4f))
        compose.setContent { CauceTheme { VariationsPanel(st, actions) } }
        compose.onNodeWithTag("humanize").performScrollTo()
        compose.onNodeWithText(tr("Suelto", "Loose")).performClick()
        compose.onNodeWithText("Natural").performClick() // ya está en Natural: no reenvía
        compose.onNodeWithText(tr("Cuadrado", "Tight")).performClick()
        assertEquals(listOf(0.8f, 0f), sent)
    }

    @Test fun plan_parses_style_and_humanize_and_defaults_for_old_projects() {
        assertEquals(1, trap.music!!.style)
        assertEquals(0f, trap.music!!.humanize)
        val json = res("state_trap04.json").replaceFirst("\"genre\"", "\"style\": 2, \"humanize\": 0.4, \"genre\"")
        val st = StateParser.state(json)!!
        assertEquals(2, st.music!!.style)
        assertEquals(0.4f, st.music!!.humanize)
    }

    @Test fun live_bench_text_is_honest_about_what_fits() {
        fun j(cpu: Double, worst: Double, tripped: Boolean = false) = org.json.JSONObject()
            .put("cpuPercentOfOneCore", cpu).put("worstBlockPercent", worst).put("startBurstMs", 30.0).put("blockMs", 4.0).put("arch", "arm64").put("protectionTripped", tripped)
        val ok = com.caucesound.app.data.LiveBench.describe(j(9.0, 22.0))
        org.junit.Assert.assertTrue(ok.contains("arm64") && ok.contains(tr("Cabe con margen", "Fits with headroom")))
        org.junit.Assert.assertTrue(com.caucesound.app.data.LiveBench.describe(j(30.0, 70.0)).contains(tr("Muy justo", "Very tight")))
        org.junit.Assert.assertTrue(com.caucesound.app.data.LiveBench.describe(j(60.0, 130.0)).contains(tr("No cabe", "Does not fit")))
        org.junit.Assert.assertTrue(com.caucesound.app.data.LiveBench.describe(j(5.0, 10.0, tripped = true)).contains(tr("No cabe", "Does not fit")))
    }
}
