package com.caucesound.app

import android.os.Looper
import com.caucesound.app.audio.NativeEngine
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import java.io.File

/**
 * Misión 0.10 — «Signature 2» de extremo a extremo: DawViewModel real → JSON → puente JNI real → motor C++ real.
 * Se salta si no se compiló la biblioteca para el ordenador (CS_HOST_JNI_DIR). En esta prueba no hay paquetes de
 * sonidos instalados: comprueba también que la canción se crea igual con los sintetizadores y que se avisa.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w411dp-h891dp-xxhdpi")
class Signature2EndToEndTest {
    @Before fun needsHostJni() {
        val dir = System.getProperty("cs.hostJni")
        assumeTrue("Sin libcaucesound para el ordenador (CS_HOST_JNI_DIR)", dir != null && File(dir, "libcaucesound.so").exists())
    }

    private fun project(): JSONObject = JSONObject(NativeEngine.command("""{"cmd":"state"}""")).getJSONObject("project")
    private fun waitFor(what: String, timeoutMs: Long = 20_000, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!cond()) {
            shadowOf(Looper.getMainLooper()).idle()
            if (System.currentTimeMillis() > end) throw AssertionError("tiempo agotado esperando: $what")
            Thread.sleep(10)
        }
    }
    private fun noteStarts(p: JSONObject): List<Long> {
        val out = mutableListOf<Long>()
        val tracks = p.getJSONArray("tracks")
        for (i in 0 until tracks.length()) {
            val clips = tracks.getJSONObject(i).optJSONArray("midiClips") ?: continue
            for (c in 0 until clips.length()) {
                val ns = clips.getJSONObject(c).optJSONArray("notes") ?: continue
                for (n in 0 until ns.length()) out += ns.getJSONArray(n).getLong(3)
            }
        }
        return out
    }

    @Test fun classic_and_signature2_songs_from_the_view_model() {
        val vm = DawViewModel(RuntimeEnvironment.getApplication())
        // La plantilla de siempre: sin estilo 2 y con las secciones de siempre.
        vm.createProject("Clásica", "hiphop") { "Pista $it" }
        waitFor("proyecto clásico") { vm.state.value?.music?.genre == "hiphop" }
        val classicFolder = vm.state.value!!.folder
        assertEquals(1, vm.state.value!!.music!!.style)
        assertEquals("A", project().getJSONObject("music").getJSONArray("sections").getJSONObject(1).getString("name"))

        // Signature 2: mismo estilo, forma de canción y humanización por defecto.
        vm.createProject("Nueva", "hiphop@2") { "Pista $it" }
        waitFor("proyecto Signature 2") { vm.state.value?.let { it.folder != classicFolder && it.music?.style == 2 } == true }
        val st = vm.state.value!!
        assertEquals("hiphop", st.music!!.genre)
        assertEquals(0.4f, st.music!!.humanize, 0.001f)
        val sections = project().getJSONObject("music").getJSONArray("sections")
        assertEquals(listOf("Intro", "Verso", "Coro", "Verso 2", "Coro 2", "Cierre"), (0 until sections.length()).map { sections.getJSONObject(it).getString("name") })
        assertTrue("sin paquetes debe avisar: ${vm.messages.value}", vm.messages.value?.contains("Signature 2") == true)

        // Humanizar desde la interfaz: 0 deja todo en la rejilla; «Suelto» mueve notas; Deshacer vuelve atrás.
        vm.setHumanize(0f)
        waitFor("cuadrado") { vm.state.value?.music?.humanize == 0f }
        assertTrue(noteStarts(project()).all { it % 60L == 0L })
        val square = noteStarts(project())
        vm.setHumanize(0.8f)
        waitFor("suelto") { vm.state.value?.music?.humanize == 0.8f }
        val loose = noteStarts(project())
        assertEquals(square.size, loose.size)
        assertNotEquals(square, loose)
        assertTrue(square.zip(loose).all { (a, b) -> kotlin.math.abs(a - b) <= 14 })
        vm.undo()
        waitFor("deshacer") { vm.state.value?.music?.humanize == 0f }
        assertEquals(square, noteStarts(project()))

        // Variar sigue funcionando y conserva el modo.
        vm.regenerateBeat(4242)
        waitFor("regenerar") { vm.state.value?.music?.revision == 2 && vm.plugins.value.variationWorking == null }
        assertEquals(2, vm.state.value!!.music!!.style)
    }

    /** El diagnóstico de VocalShift en vivo llega al motor real y devuelve cifras; no activa el efecto ni toca el proyecto. */
    @Test fun live_vocalshift_diagnostic_measures_without_enabling_anything() {
        val vm = DawViewModel(RuntimeEnvironment.getApplication())
        vm.createProject("Diag", "trap") { "Pista $it" }
        waitFor("proyecto") { vm.state.value?.music?.genre == "trap" }
        val before = project().toString()
        vm.runLiveBench()
        waitFor("medición", 60_000) { vm.liveBench.value?.startsWith("CPU") == true }
        assertTrue(vm.liveBench.value!!.contains("x86-64"))
        assertEquals(before, project().toString())
        assertTrue(!NativeEngine.command("""{"cmd":"catalog"}""").contains("cs.vocalshift.live"))
    }
}
