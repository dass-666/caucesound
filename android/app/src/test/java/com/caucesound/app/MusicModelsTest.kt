package com.caucesound.app

import com.caucesound.app.data.Catalog
import com.caucesound.app.data.MusicTime
import com.caucesound.app.data.NoteState
import com.caucesound.app.data.PPQ
import com.caucesound.app.data.StateParser
import com.caucesound.app.ui.PianoRollOps
import com.caucesound.app.ui.fitLanes
import com.caucesound.app.ui.formatParam
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Modelo musical de la interfaz contra JSON real del motor (fixtures generados por ui_fixtures_from_engine). */
class MusicModelsTest {
    private fun res(name: String) = javaClass.classLoader!!.getResource("fixtures/$name")!!.readText()

    @Test fun parsesV2StateFromEngine() {
        val s = StateParser.state(res("state_trap.json"))!!
        assertEquals(140.0, s.bpm, 1e-9)
        assertEquals("trap", s.templateId)
        assertEquals(listOf("drums", "synth", "synth", "audio"), s.tracks.map { it.kind })
        val drums = s.tracks[0]
        assertEquals(8, drums.patterns[0].lanes.size)
        assertEquals(16, drums.patterns[0].steps)
        assertTrue(drums.patterns[0].lanes[0][0] > 0) // bombo en el primer paso
        val lead = s.tracks[2]
        assertTrue(lead.midiClips[0].notes.isNotEmpty())
        assertEquals("pluck", lead.preset)
        assertEquals(listOf("cs.eq", "cs.reverb"), lead.inserts.map { it.type })
        assertEquals(0.35f, lead.inserts[1].params["mix"]!!, 1e-6f)
        assertTrue(s.tracks[3].armed)
    }

    @Test fun parsesCatalogFromEngine() {
        val c = Catalog.parse(res("catalog.json"))
        assertEquals(7, c.synthPresets.size)
        assertEquals(4, c.drumKits.size)
        assertEquals(8, c.drumKits[0].lanes.size)
        assertEquals(5, c.templates.size)
        assertEquals(19, c.synthParams.size)
        assertEquals(listOf("cs.gain", "cs.filter", "cs.eq", "cs.saturation", "cs.compressor", "cs.delay", "cs.reverb"), c.effects.map { it.type })
        assertTrue(c.effects.all { it.params.isNotEmpty() })
    }

    @Test fun ticksAndFramesRoundTripLikeTheEngine() {
        // 140 BPM: una negra = 60/140 s = 20571.43 frames; el motor redondea al más cercano (llround).
        assertEquals(20571L, MusicTime.ticksToFrames(PPQ.toLong(), 140.0))
        for (t in listOf(0L, 1L, 239L, 240L, 960L, 3841L, 960L * 64)) {
            val f = MusicTime.ticksToFrames(t, 140.0)
            assertTrue(kotlin.math.abs(MusicTime.framesToTicks(f, 140.0) - t) <= 1)
        }
        assertEquals(480L, MusicTime.snapTicks(470, 240))
        assertEquals(240L, MusicTime.floorTicks(470, 240))
        assertEquals("C4", MusicTime.noteName(60))
        assertTrue(MusicTime.isBlack(61) && !MusicTime.isBlack(64))
    }

    @Test fun pianoRollEditOperations() {
        val a = NoteState(60, 100, 0, 0, 240)
        val b = NoteState(64, 90, 0, 480, 240)
        assertEquals(NoteState(62, 100, 0, 0, 240), PianoRollOps.moved(a, -500, 2)) // no antes de 0
        assertEquals(120L, PianoRollOps.resized(a, 10, 240).length)          // longitud mínima: media celda
        val (dup, sel) = PianoRollOps.duplicate(listOf(a, b), setOf(0, 1))
        assertEquals(4, dup.size)
        assertEquals(setOf(2, 3), sel)
        assertEquals(720L, dup[2].start) // detrás del grupo (span = 720)
        assertEquals(listOf(127, 64), PianoRollOps.transpose(listOf(a.copy(key = 120), b), setOf(0), 12).map { it.key })
        assertEquals(1, PianoRollOps.hit(listOf(a, b), 500, 64))
        assertEquals(-1, PianoRollOps.hit(listOf(a, b), 240, 60)) // el final es exclusivo
    }

    @Test fun drumLanesResizeFrom16To32ByRepeating() {
        val src = List(8) { l -> List(16) { s -> if (s == l) 100 else 0 } }
        val r = fitLanes(src, 32)
        assertEquals(8, r.size)
        assertTrue(r.all { it.size == 32 })
        assertEquals(100, r[3][3 + 16])
        assertEquals(List(16) { 0 }, fitLanes(listOf(), 16)[7])
    }

    @Test fun effectParameterLabels() {
        val p = com.caucesound.app.data.ParamInfo("x", "x", 0f, 1f, 0f)
        assertEquals("+3.0 dB", formatParam("lowDb", 3f, p).replace(',', '.'))
        assertEquals("1.5 k", formatParam("cutoffHz", 1500f, p).replace(',', '.'))
        assertEquals("HP", formatParam("mode", 1f, p))
        assertEquals("35%", formatParam("mix", 0.35f, p))
    }
}
