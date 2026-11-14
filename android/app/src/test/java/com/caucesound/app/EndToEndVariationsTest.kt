package com.caucesound.app

import android.os.Looper
import androidx.compose.foundation.layout.Box
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.test.SemanticsNodeInteraction
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import com.caucesound.app.audio.NativeEngine
import com.caucesound.app.ui.CauceTheme
import com.caucesound.app.ui.Panel
import com.caucesound.app.ui.StudioLayout
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import java.io.File

/**
 * Hotfix 0.9.2 — pruebas de EXTREMO A EXTREMO de Variar/Regenerar: se pulsan los botones reales de la interfaz,
 * que llaman al DawViewModel real, que envía el JSON por el puente JNI real (JniBridge.cpp compilado para el
 * ordenador con -DCS_BUILD_HOST_JNI=ON) al motor C++ real; después se lee el estado del motor y se comprueba lo que
 * muestra la interfaz. Lo único simulado es la salida de audio (un hilo que llama a Engine::process a ritmo real en
 * vez de Oboe). Se saltan si no se compiló la biblioteca (variable CS_HOST_JNI_DIR; ver docs/HOTFIX_0.9.2.md).
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w411dp-h891dp-xxhdpi")
class EndToEndVariationsTest {
    @get:Rule val compose = createComposeRule()

    @Before fun needsHostJni() {
        val dir = System.getProperty("cs.hostJni")
        assumeTrue("Sin libcaucesound para el ordenador (CS_HOST_JNI_DIR)", dir != null && File(dir, "libcaucesound.so").exists())
    }

    private fun engineState(): JSONObject = JSONObject(NativeEngine.command("""{"cmd":"state"}"""))

    /** Notas y golpes de cada parte generada, tal como los guarda el motor (sin velocidades). */
    private fun music(st: JSONObject = engineState()): Map<String, Set<String>> {
        val p = st.getJSONObject("project")
        val roles = p.optJSONObject("music")?.optJSONObject("roles") ?: return emptyMap()
        val out = mutableMapOf<String, MutableSet<String>>()
        val tracks = p.getJSONArray("tracks")
        for (i in 0 until tracks.length()) {
            val t = tracks.getJSONObject(i)
            val role = roles.optString(t.getString("id"))
            if (role.isEmpty()) continue
            val e = out.getOrPut(role) { mutableSetOf() }
            val clips = t.optJSONArray("midiClips")
            if (clips != null) for (c in 0 until clips.length()) {
                val clip = clips.getJSONObject(c)
                val ns = clip.getJSONArray("notes")
                for (n in 0 until ns.length()) {
                    val note = ns.getJSONArray(n)
                    e.add(clip.getLong("start").toString() + "|" + note.getLong(3) + "|" + note.getInt(0))
                }
            }
            val pats = t.optJSONArray("patterns")
            if (pats != null) for (k in 0 until pats.length()) {
                val pat = pats.getJSONObject(k)
                val lanes = pat.getJSONArray("lanes")
                for (l in 0 until lanes.length()) {
                    val lane = lanes.getJSONArray(l)
                    for (st2 in 0 until lane.length()) if (lane.getInt(st2) > 0) e.add(pat.getString("id") + "|" + l + "|" + st2)
                }
            }
        }
        return out
    }

    private fun change(a: Set<String>, b: Set<String>): Double {
        if (a.isEmpty() && b.isEmpty()) return 0.0
        val inter = a.count { it in b }
        return 1.0 - inter.toDouble() / (a.size + b.size - inter)
    }

    private fun revision(vm: DawViewModel) = vm.state.value?.music?.revision ?: -1

    private fun waitFor(what: String, timeoutMs: Long = 20_000, cond: () -> Boolean) {
        val end = System.currentTimeMillis() + timeoutMs
        while (!cond()) {
            shadowOf(Looper.getMainLooper()).idle()
            if (System.currentTimeMillis() > end) throw AssertionError("tiempo agotado esperando: $what")
            Thread.sleep(10)
        }
    }

    private fun node(tag: String): SemanticsNodeInteraction = compose.onNodeWithTag(tag, useUnmergedTree = true)
    private fun tap(tag: String) {
        runCatching { node(tag).performScrollTo() } // los botones del panel están en una columna desplazable; los de la barra no
        node(tag).performClick()
        compose.waitForIdle()
    }

    /** El estudio real (StudioLayout) con el DawViewModel real como acciones. */
    private fun showStudio(vm: DawViewModel) {
        compose.setContent {
            CauceTheme {
                val s by vm.state.collectAsState()
                val panel by vm.panel.collectAsState()
                val catalog by vm.catalog.collectAsState()
                val plugins by vm.plugins.collectAsState()
                val selected by vm.selectedClip.collectAsState()
                val selectedTrack by vm.selectedTrack.collectAsState()
                val position = remember { mutableLongStateOf(0L) }
                val meters = remember { mutableStateOf(FloatArray(0)) }
                val snackbar = remember { SnackbarHostState() }
                s?.let { st ->
                    StudioLayout(st, panel, selected, selectedTrack, emptyMap(), catalog, false, position, NativeEngine.isPlaying(), meters, vm, snackbar,
                        onDialog = {}, onSettings = {}, onImport = {}, onRecord = {}, plugins = plugins)
                } ?: Box {}
            }
        }
        compose.waitForIdle()
    }

    private fun newSong(vm: DawViewModel, style: String) {
        val before = vm.state.value?.folder
        vm.createProject("E2E $style", style) { "Pista $it" }
        waitFor("proyecto $style abierto") { vm.state.value?.let { it.folder != before && it.music?.genre == style } == true }
    }

    @Test fun electronic_vary_regenerate_lock_undo_and_persist_from_the_buttons() {
        val vm = DawViewModel(RuntimeEnvironment.getApplication())
        newSong(vm, "electronic")
        showStudio(vm)
        tap("open_variations")
        waitFor("panel de variaciones") { vm.panel.value == Panel.VARY }
        node("variation_revision").assertIsDisplayed()
        val v1 = music()
        assertEquals(1, revision(vm))

        // 1. Variar todo: llega al motor, cambia las notas, la versión sube y la interfaz lo dice.
        tap("mutate_all")
        waitFor("versión 2") { revision(vm) == 2 && vm.plugins.value.variationWorking == null }
        val v2 = music()
        val varied = v1.keys.map { change(v1[it]!!, v2[it]!!) }
        assertTrue("Variar todo no cambió lo suficiente: $varied", varied.average() >= 0.15)
        assertTrue("mensaje: ${vm.messages.value}", vm.messages.value?.let { it.contains("Versión 2 lista") || it.contains("Version 2 ready") } == true)
        compose.waitForIdle()
        node("variation_changes").assertIsDisplayed()

        // 2. Regenerar beat: otra composición.
        tap("regenerate_beat")
        waitFor("versión 3") { revision(vm) == 3 && vm.plugins.value.variationWorking == null }
        val v3 = music()
        val regen = v2.keys.map { change(v2[it]!!, v3[it]!!) }
        assertTrue("Regenerar cambió poco: $regen", regen.average() >= 0.40)

        // 3. Cada botón individual de Variar y de Rehacer cambia SU parte y nada más.
        val parts = listOf("drums", "bass", "chords", "melody", "pad").filter { it in v3.keys }
        var rev = 3
        for (prefix in listOf("mutate_", "vary_")) for (part in parts) {
            val before = music()
            tap(prefix + part)
            rev += 1
            waitFor("versión $rev ($prefix$part)") { revision(vm) == rev && vm.plugins.value.variationWorking == null }
            val after = music()
            assertTrue("$prefix$part no cambió su parte", change(before[part]!!, after[part]!!) >= 0.15)
            for (other in parts - part) assertEquals("$prefix$part tocó $other", before[other], after[other])
        }

        // 4. Candado: la batería bloqueada no cambia al variar todo.
        val drumsId = vm.state.value!!.music!!.roles.entries.first { it.value == "drums" }.key
        tap("lock_$drumsId")
        waitFor("batería bloqueada") { drumsId in (vm.state.value?.music?.locked ?: emptySet()) }
        val beforeLock = music()
        tap("mutate_all")
        rev += 1
        waitFor("versión $rev con candado") { revision(vm) == rev && vm.plugins.value.variationWorking == null }
        assertEquals(beforeLock["drums"], music()["drums"])
        assertTrue(vm.plugins.value.variation!!.skippedLocked.isNotEmpty())
        // Solo la batería (bloqueada): mensaje comprensible, sin versión nueva.
        tap("mutate_drums")
        waitFor("mensaje de candado") { vm.messages.value?.contains("bloqueadas") == true }
        assertEquals(rev, revision(vm))

        // 5. Comparar: «Versión anterior» vuelve exactamente a la anterior y «Versión nueva» a la nueva.
        val newest = music()
        tap("variation_previous")
        waitFor("deshacer") { revision(vm) == rev - 1 }
        assertEquals(beforeLock, music())
        tap("variation_next")
        waitFor("rehacer") { revision(vm) == rev }
        assertEquals(newest, music())

        // 6. Persistencia: guardar, cerrar y volver a abrir conserva la versión y las notas.
        val folder = vm.state.value!!.folder
        vm.saveNow()
        waitFor("guardado") { vm.state.value?.dirty == false }
        vm.closeProject()
        waitFor("cerrado") { vm.state.value == null }
        vm.openProject(folder)
        waitFor("reabierto") { vm.state.value?.folder == folder }
        assertEquals(rev, revision(vm))
        assertEquals(newest, music())

        // 7. Durante la reproducción el motor usa la versión nueva (el grafo se publica al momento).
        tap("open_variations")
        vm.play()
        waitFor("sonando") { NativeEngine.isPlaying() }
        val playingBefore = music()
        tap("regenerate_beat")
        waitFor("regenerar sonando") { revision(vm) == rev + 1 && vm.plugins.value.variationWorking == null }
        assertNotEquals(playingBefore, music())
        assertTrue("mensaje: ${vm.messages.value}", vm.messages.value?.let { it.contains("Ya la estás escuchando") || it.contains("hearing it now") } == true)
        vm.stop()
        waitFor("parado") { !NativeEngine.isPlaying() }
        vm.closeProject()
        waitFor("cerrado al final") { vm.state.value == null }
    }

    @Test fun every_style_vary_and_regenerate_buttons_produce_a_new_version() {
        val vm = DawViewModel(RuntimeEnvironment.getApplication())
        val results = mutableListOf<String>()
        showStudio(vm) // la misma pantalla sigue al proyecto abierto
        for (style in listOf("trap", "reggaeton", "hiphop", "boombap", "jazzrap", "darkhiphop", "pop", "electronic")) {
            newSong(vm, style)
            compose.waitForIdle()
            tap("open_variations")
            val m0 = music()
            tap("mutate_all")
            waitFor("$style variar") { revision(vm) == 2 && vm.plugins.value.variationWorking == null }
            val m1 = music()
            tap("regenerate_beat")
            waitFor("$style regenerar") { revision(vm) == 3 && vm.plugins.value.variationWorking == null }
            val m2 = music()
            val vary = m0.keys.map { change(m0[it]!!, m1[it]!!) }.average()
            val regen = m1.keys.map { change(m1[it]!!, m2[it]!!) }.average()
            results += "$style variar ${(vary * 100).toInt()} % regenerar ${(regen * 100).toInt()} %"
            assertTrue("$style: variar $vary", vary >= 0.15)
            assertTrue("$style: regenerar $regen", regen >= 0.40)
            vm.closeProject()
            waitFor("$style cerrado") { vm.state.value == null }
        }
        println("measure: " + results.joinToString("; "))
    }

    /** Grabación con un toque (0.9.2): sin pista armada, el botón rojo crea la pista de voz y graba; sin acompañamiento
     *  el máster queda en silencio durante la toma y, al parar, la toma existe y la mezcla no cambió. */
    @Test fun quick_record_creates_a_voice_track_and_records_without_backing() {
        val vm = DawViewModel(RuntimeEnvironment.getApplication())
        newSong(vm, "electronic")
        val voz = vm.state.value!!.tracks.first { it.isAudio }
        vm.setArmed(voz.id, false)
        waitFor("pista desarmada") { vm.state.value?.tracks?.none { it.armed } == true }
        assertTrue(!vm.hasArmedTrack())
        val mixBefore = vm.state.value!!.tracks.map { it.id to it.volumeDb }
        vm.recordQuick(null, solo = true, trackName = "Voz")
        waitFor("grabando") { vm.state.value?.recording == true }
        val st = vm.state.value!!
        val newTrack = st.tracks.last { it.isAudio }
        assertTrue("pista nueva armada", newTrack.id != voz.id && newTrack.armed)
        waitFor("fin de la cuenta previa", 10_000) { NativeEngine.countInRemaining() == 0L && NativeEngine.position() > 48_000 }
        Thread.sleep(400)
        val peaks = (0 until 10).map { Thread.sleep(30); NativeEngine.meters(st.tracks.size)[0] }
        assertTrue("el máster debe callar durante la toma: $peaks", peaks.all { it < 1e-4f })
        vm.stop()
        waitFor("toma guardada") { vm.state.value?.recording == false && vm.state.value!!.tracks.first { it.id == newTrack.id }.clips.isNotEmpty() }
        assertTrue(vm.recordSolo.value)
        assertEquals(mixBefore, vm.state.value!!.tracks.filter { t -> mixBefore.any { it.first == t.id } }.map { it.id to it.volumeDb })
        // Con el beat: la siguiente reproducción suena (el silencio solo dura la toma).
        vm.seek(4 * 48_000L)
        vm.play()
        waitFor("sonando otra vez", 10_000) { NativeEngine.isPlaying() && NativeEngine.meters(st.tracks.size)[0] > 1e-3f }
        vm.stop()
        vm.setRecordSolo(false)
        vm.closeProject()
        waitFor("cerrado") { vm.state.value == null }
    }
}
