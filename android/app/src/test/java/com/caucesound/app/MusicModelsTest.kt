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
        // Catálogo de la 0.4 (regenerado con cs_render --catalog): 17 presets y 8 kits (4 nuevos «Cauce»).
        assertEquals(17, c.synthPresets.size)
        assertEquals(8, c.drumKits.size)
        assertEquals("Crash", c.drumKits[5].lanes[7])
        assertEquals(8, c.drumKits[0].lanes.size)
        assertEquals(5, c.templates.size)
        assertEquals(25, c.synthParams.size)
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

    @Test fun drumVariationsAreDeterministicAndKeepTheGrid() {
        val s = StateParser.state(res("state_trap.json"))!!
        val lanes = s.tracks[0].patterns[0].lanes
        for (v in com.caucesound.app.ui.DrumVariation.values()) {
            val a = v.apply(lanes)
            assertEquals(a, v.apply(lanes))                     // misma entrada, misma salida: nada aleatorio
            assertEquals(lanes.size, a.size)
            assertTrue(a.all { it.size == lanes[0].size && it.all { x -> x in 0..127 } })
        }
        // Dinámica: no añade ni quita golpes y es idempotente.
        val dyn = com.caucesound.app.ui.DrumVariation.DYNAMICS.apply(lanes)
        assertEquals(lanes.map { l -> l.map { it > 0 } }, dyn.map { l -> l.map { it > 0 } })
        assertEquals(dyn, com.caucesound.app.ui.DrumVariation.DYNAMICS.apply(dyn))
        // Remate: solo toca el último tiempo; la caja crece hasta el final.
        val fill = com.caucesound.app.ui.DrumVariation.FILL.apply(lanes)
        val n = lanes[0].size
        assertEquals(lanes.map { it.take(n - 4) }, fill.map { it.take(n - 4) })
        assertEquals(listOf(78, 92, 106, 122), fill[1].takeLast(4))
        // Hats al doble llena todos los pasos; a la mitad deja libres los impares y no toca otros carriles.
        assertTrue(com.caucesound.app.ui.DrumVariation.DOUBLE_HATS.apply(lanes)[3].all { it > 0 })
        val half = com.caucesound.app.ui.DrumVariation.HALF_HATS.apply(lanes)
        assertTrue(half[3].filterIndexed { i, _ -> i % 2 == 1 }.all { it == 0 })
        assertEquals(lanes[0], half[0])
    }

    @Test fun pianoRollRepeatAndVelocityLaneHelpers() {
        val notes = listOf(NoteState(60, 100, 0, 0, 240), NoteState(64, 80, 0, 960, 240), NoteState(67, 120, 0, 960, 480))
        val d = PianoRollOps.doubled(notes, 1920)
        assertEquals(6, d.size)
        assertEquals(notes, d.take(3))
        assertEquals(listOf(1920L, 2880L, 2880L), d.drop(3).map { it.start })
        assertEquals(notes.map { it.key to it.velocity }, d.drop(3).map { it.key to it.velocity })
        assertEquals(0, PianoRollOps.nearestByStart(notes, 100, 240))
        assertEquals(2, PianoRollOps.nearestByStart(notes, 950, 240))   // empate en el inicio: gana la más fuerte
        assertEquals(-1, PianoRollOps.nearestByStart(notes, 500, 120))
        assertEquals(-1, PianoRollOps.nearestByStart(emptyList(), 0, 120))
    }

    @Test fun scalesHighlightTheRightKeys() {
        val sc = com.caucesound.app.ui.Scale.MINOR
        // La menor: A B C D E F G.
        assertEquals(listOf(57, 59, 60, 62, 64, 65, 67), (57..68).filter { sc.contains(it, 9) })
        assertTrue((0..127).all { com.caucesound.app.ui.Scale.NONE.contains(it, 3) })
        assertTrue(com.caucesound.app.ui.Scale.MINOR_PENTA.contains(0, 0) && !com.caucesound.app.ui.Scale.MINOR_PENTA.contains(2, 0))
        assertTrue(com.caucesound.app.ui.Scale.MAJOR.contains(1, 1)) // la tónica siempre pertenece, también en octavas bajas
    }

    @Test fun tapTempoUsesTheMedianAndResetsAfterAPause() {
        val t = com.caucesound.app.data.TapTempo()
        assertEquals(null, t.tap(1000))
        assertEquals(120.0, t.tap(1500)!!, 1e-9)
        t.tap(2000); t.tap(2500)
        // Un toque tardío (700 ms) no mueve la mediana: sigue en 120.
        assertEquals(120.0, t.tap(3200)!!, 1e-9)
        assertEquals(120.0, t.tap(3700)!!, 1e-9)
        // Pausa larga: empieza de cero con el tempo nuevo.
        assertEquals(null, t.tap(10_000))
        assertEquals(1, t.count)
        assertEquals(140.0, t.tap(10_000 + 429)!!, 0.2)
        // Límites del motor (20–400 BPM).
        val fast = com.caucesound.app.data.TapTempo(); fast.tap(0)
        assertEquals(400.0, fast.tap(50)!!, 1e-9)
        // Cuenta previa: 4, 3, 2, 1 a 120 BPM (24000 frames por tiempo).
        assertEquals(listOf(4, 4, 3, 1, 1), listOf(96000L, 72001L, 72000L, 24000L, 1L).map { MusicTime.countInBeatsLeft(it, 120.0) })
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
