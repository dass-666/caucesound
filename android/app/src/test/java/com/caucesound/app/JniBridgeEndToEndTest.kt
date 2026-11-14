package com.caucesound.app

import com.caucesound.app.audio.NativeEngine
import com.caucesound.app.data.Cmd
import com.caucesound.app.data.StateParser
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.annotation.Config
import java.io.File

/**
 * Auditoría de la 0.10 — el puente JNI real (JniBridge.cpp compilado para el ordenador) con textos que no son ASCII.
 * Antes las cadenas cruzaban en «UTF-8 modificado»: un emoji en el nombre del proyecto o de una pista hacía que el
 * motor rechazara la petición entera («petición no válida»). Ahora cruzan en UTF-16 y el motor recibe UTF-8 real.
 * Se salta si no se compiló la biblioteca (CS_HOST_JNI_DIR), igual que las demás pruebas de extremo a extremo.
 */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35], qualifiers = "w411dp-h891dp-xxhdpi")
class JniBridgeEndToEndTest {
    @Before fun needsHostJni() {
        val dir = System.getProperty("cs.hostJni")
        assumeTrue("Sin libcaucesound para el ordenador (CS_HOST_JNI_DIR)", dir != null && File(dir, "libcaucesound.so").exists())
    }

    private fun tempProjects(): File = File(System.getProperty("java.io.tmpdir"), "cs_jni_e2e_" + System.nanoTime()).apply { mkdirs() }

    @Test fun emojiAndAccentsSurviveTheBridgeInBothDirections() {
        val root = tempProjects()
        try {
            val name = "Beat 🔥 canción 日本"
            val folder = File(root, "p1").absolutePath
            val made = JSONObject(NativeEngine.command(Cmd.createProject(folder, name, "empty")))
            assertTrue("crear con emoji: $made", made.optBoolean("ok"))

            val trackName = "Voz 🎤 ñ"
            val track = JSONObject(NativeEngine.command(Cmd.addTrack(trackName)))
            assertTrue("pista con emoji: $track", track.optBoolean("ok"))

            val state = StateParser.state(NativeEngine.command(Cmd.state()))!!
            assertEquals(name, state.name)
            assertEquals(trackName, state.tracks.first { it.id == track.getString("id") }.name)

            // Guardado y lista de proyectos: el nombre vuelve igual desde el disco.
            assertTrue(JSONObject(NativeEngine.command(Cmd.save())).optBoolean("ok"))
            NativeEngine.command(Cmd.closeProject())
            val listed = StateParser.projects(NativeEngine.command(Cmd.listProjects(root.absolutePath)))
            assertEquals(listOf(name), listed.map { it.name })
        } finally {
            NativeEngine.command(Cmd.closeProject())
            root.deleteRecursively()
        }
    }

    @Test fun unpairedSurrogateDoesNotBreakTheCommand() {
        val root = tempProjects()
        try {
            // Medio emoji (lo que deja un teclado o un recorte de texto mal hecho): se sustituye, no rompe el comando.
            val made = JSONObject(NativeEngine.command(Cmd.createProject(File(root, "p2").absolutePath, "Roto \uD83D fin", "empty")))
            assertTrue("crear con subrogado suelto: $made", made.optBoolean("ok"))
            val state = StateParser.state(NativeEngine.command(Cmd.state()))!!
            assertEquals("Roto � fin", state.name)
        } finally {
            NativeEngine.command(Cmd.closeProject())
            root.deleteRecursively()
        }
    }

    @Test fun metersReturnTheHeldPeakAndResetOnRead() {
        val root = tempProjects()
        try {
            assertTrue(JSONObject(NativeEngine.command(Cmd.createProject(File(root, "p3").absolutePath, "Medidores", "trap"))).optBoolean("ok"))
            val tracks = StateParser.state(NativeEngine.command(Cmd.state()))!!.tracks.size
            NativeEngine.audioStart()
            NativeEngine.command(Cmd.seek(4L * 48000))
            NativeEngine.command(Cmd.play())
            var peak = 0f
            val deadline = System.currentTimeMillis() + 5000
            while (peak <= 0f && System.currentTimeMillis() < deadline) { Thread.sleep(50); peak = NativeEngine.meters(tracks)[0] }
            assertTrue("el máster debe marcar señal mientras suena", peak > 0f)
            NativeEngine.command(Cmd.stop())
            NativeEngine.audioStop()
            NativeEngine.meters(tracks) // vacía lo retenido
            assertEquals(tracks + 1, NativeEngine.meters(tracks).size)
            assertEquals("sin audio nuevo el medidor queda a cero", 0f, NativeEngine.meters(tracks)[0], 0f)
        } finally {
            NativeEngine.command(Cmd.closeProject())
            NativeEngine.audioStop()
            root.deleteRecursively()
        }
    }
}
