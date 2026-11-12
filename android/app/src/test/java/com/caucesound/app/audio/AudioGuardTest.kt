package com.caucesound.app.audio

import android.content.Context
import android.media.AudioManager
import androidx.test.core.app.ApplicationProvider
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf

/**
 * Regresión del fallo de Play de la 0.3.0: cada Play creaba una petición de foco con un oyente nuevo y Android le
 * quitaba el foco (AUDIOFOCUS_LOSS) al oyente anterior de la misma app, que paraba el transporte. Robolectric no
 * reproduce la pila de foco de Android, así que se comprueba la condición que lo evita: todas las peticiones usan
 * el mismo objeto (mismo oyente = mismo dueño del foco, sin pérdida) y la pérdida real sí detiene.
 */
@RunWith(RobolectricTestRunner::class)
class AudioGuardTest {
    private val ctx: Context = ApplicationProvider.getApplicationContext()
    private val am = ctx.getSystemService(Context.AUDIO_SERVICE) as AudioManager

    @Test fun every_play_reuses_the_same_focus_request() {
        val interrupts = mutableListOf<String>()
        val g = AudioGuard(ctx, onInterrupt = { interrupts += it }, onDevicesChanged = {})
        val shadow = shadowOf(am)
        repeat(100) {
            assertTrue(g.requestFocus())
            assertSame("petición distinta en el Play ${it + 1}", g.focusRequestForTest(), shadow.lastAudioFocusRequest.audioFocusRequest)
        }
        assertTrue(g.held)
        assertTrue("pedir el foco no debe interrumpir: $interrupts", interrupts.isEmpty())
    }

    @Test fun real_focus_loss_stops_and_close_abandons() {
        val interrupts = mutableListOf<String>()
        val g = AudioGuard(ctx, onInterrupt = { interrupts += it }, onDevicesChanged = {})
        g.requestFocus()
        // Otra app toma el foco (llamada, reproductor): el oyente registrado recibe la pérdida.
        shadowOf(am).lastAudioFocusRequest.listener.onAudioFocusChange(AudioManager.AUDIOFOCUS_LOSS)
        assertEquals(listOf("focus"), interrupts)
        assertFalse(g.held)
        assertTrue(g.requestFocus())
        g.abandonFocus()
        assertFalse(g.held)
        assertSame(g.focusRequestForTest(), shadowOf(am).lastAbandonedAudioFocusRequest)
    }
}
