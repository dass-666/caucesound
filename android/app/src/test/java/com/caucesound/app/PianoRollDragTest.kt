package com.caucesound.app

import com.caucesound.app.data.MusicTime
import com.caucesound.app.data.NoteState
import com.caucesound.app.data.PPQ
import com.caucesound.app.ui.PianoRollOps
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * Arrastre en el piano roll (0.8). Reproduce el fallo «la nota solo se mueve hacia adelante»: el desplazamiento se
 * redondeaba con MusicTime.snapTicks, que nunca devuelve valores negativos.
 */
class PianoRollDragTest {
    private val g = PPQ / 4L // 1/16
    private val n = NoteState(key = 60, velocity = 93, channel = 2, start = 4L * PPQ, length = PPQ.toLong())

    @Test fun oldSnapKilledNegativeDeltas() {
        // Documenta la causa: con la función antigua un arrastre de una negra hacia la izquierda valía 0.
        assertEquals(0L, MusicTime.snapTicks(-PPQ.toLong(), g))
        assertEquals(-PPQ.toLong(), PianoRollOps.snapSigned(-PPQ.toLong(), g))
    }

    @Test fun movesInAllEightDirections() {
        val dirs = listOf(-1 to 0, 1 to 0, 0 to 1, 0 to -1, -1 to 1, 1 to 1, -1 to -1, 1 to -1)
        for ((dx, dk) in dirs) {
            val out = PianoRollOps.dragMove(listOf(n), setOf(0), 0, dx * PPQ.toLong(), dk * 3, g)[0]
            assertEquals("dx=$dx dk=$dk", n.start + dx * PPQ, out.start)
            assertEquals("dx=$dx dk=$dk", n.key + dk * 3, out.key)
            assertEquals(n.length, out.length)     // la duración no cambia al mover
            assertEquals(n.velocity, out.velocity) // ni la intensidad
            assertEquals(n.channel, out.channel)
        }
    }

    @Test fun movesBeforeItsOriginalPositionRepeatedly() {
        var notes = listOf(n)
        repeat(3) { notes = PianoRollOps.dragMove(notes, setOf(0), 0, -PPQ.toLong(), 0, g) }
        assertEquals(PPQ.toLong(), notes[0].start)
        repeat(2) { notes = PianoRollOps.dragMove(notes, setOf(0), 0, 2L * PPQ, -1, g) }
        assertEquals(5L * PPQ, notes[0].start)
        assertEquals(58, notes[0].key)
    }

    @Test fun neverBeforeZeroAndGroupKeepsShape() {
        val b = n.copy(start = 6L * PPQ, key = 64)
        val out = PianoRollOps.dragMove(listOf(n, b), setOf(0, 1), 0, -100L * PPQ, 0, g)
        assertEquals(0L, out[0].start)
        assertEquals(2L * PPQ, out[1].start) // la distancia entre las dos se conserva
        val up = PianoRollOps.dragMove(listOf(n.copy(key = 120), b), setOf(0, 1), 0, 0, 20, g)
        assertEquals(127, up[0].key)
        assertEquals(71, up[1].key)          // el acorde sube entero 7 semitonos, sin deformarse
    }

    @Test fun snapsTheNotePositionNotTheDelta() {
        val off = n.copy(start = 4L * PPQ + 37) // nota fuera de la rejilla (por ejemplo, grabada en vivo)
        val out = PianoRollOps.dragMove(listOf(off), setOf(0), 0, -PPQ.toLong() + 5, 0, g)[0]
        assertEquals(0L, out.start % g)
        assertEquals(3L * PPQ, out.start)
        // Sin imán: movimiento exacto, al tick.
        val free = PianoRollOps.dragMove(listOf(off), setOf(0), 0, -111, 0, 0)[0]
        assertEquals(off.start - 111, free.start)
    }

    @Test fun resizeGrowsAndShrinks() {
        val longer = PianoRollOps.dragResize(listOf(n), setOf(0), 0, PPQ.toLong(), g)[0]
        assertEquals(2L * PPQ, longer.length)
        assertEquals(n.start, longer.start) // cambiar la duración no mueve la nota
        val shorter = PianoRollOps.dragResize(listOf(n), setOf(0), 0, -PPQ.toLong() / 2, g)[0]
        assertEquals(PPQ / 2L, shorter.length)
        val tiny = PianoRollOps.dragResize(listOf(n), setOf(0), 0, -10L * PPQ, g)[0]
        assertEquals(g / 2, tiny.length) // mínimo: media celda
        assertTrue(PianoRollOps.dragResize(listOf(n), setOf(0), 0, -10L * PPQ, 0)[0].length > 0)
    }

    @Test fun untouchedNotesStayUntouched() {
        val other = n.copy(start = 0, key = 40)
        val out = PianoRollOps.dragMove(listOf(n, other), setOf(0), 0, -PPQ.toLong(), 2, g)
        assertEquals(other, out[1])
    }
}
