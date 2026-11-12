package com.caucesound.app

import com.caucesound.app.data.Cmd
import com.caucesound.app.data.MusicTime
import com.caucesound.app.data.StateParser
import com.caucesound.app.ui.TimelineGeometry
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class ModelsTest {
    // Respuesta real de cs::Session::stateJson (formato fijado por el motor C++)
    private val stateJson = """
        {"open":true,"folder":"/data/p","dirty":true,"canUndo":true,"canRedo":false,"recording":false,"contentEnd":96000,
         "missingClips":["clp_b"],
         "project":{"format":"caucesound-project","version":1,"id":"prj_1","name":"Demo","sampleRate":48000,"bpm":98.5,
           "beatsPerBar":3,"metronome":true,"masterDb":0.0,"recordLatencyOffset":480,"createdAt":1,"modifiedAt":2,
           "sources":[{"id":"src_a","file":"audio/src_a.wav","origin":"voz.wav","channels":1,"frames":96000}],
           "tracks":[{"id":"trk_1","name":"Voz","volumeDb":-3.5,"pan":0.25,"mute":false,"solo":true,"armed":true,"color":4283417591,
             "clips":[{"id":"clp_b","sourceId":"src_a","name":"b","start":48000,"sourceOffset":48000,"length":48000,"fadeIn":0,"fadeOut":10,"gainDb":0},
                      {"id":"clp_a","sourceId":"src_a","name":"a","start":0,"sourceOffset":0,"length":48000,"fadeIn":5,"fadeOut":0,"gainDb":-1.5}],
             "inserts":[]}]}}
    """.trimIndent()

    @Test fun parsesStateFromEngine() {
        val s = StateParser.state(stateJson)!!
        assertEquals("Demo", s.name)
        assertEquals(98.5, s.bpm, 1e-9)
        assertEquals(3, s.beatsPerBar)
        assertEquals(1, s.tracks.size)
        val t = s.tracks[0]
        assertTrue(t.solo && t.armed && !t.mute)
        assertEquals(listOf("clp_a", "clp_b"), t.clips.map { it.id }) // ordenados por inicio
        assertTrue(t.clips[1].missing)
        assertFalse(t.clips[0].missing)
        assertEquals(96000L, s.sources["src_a"]!!.frames)
        assertNotNull(s.clip("clp_b"))
        assertNull(s.clip("x"))
    }

    @Test fun closedSessionIsNull() {
        assertNull(StateParser.state("""{"open":false}"""))
    }

    @Test fun commandsAreValidJson() {
        val o = JSONObject(Cmd.moveClip("c\"1", 10, null))
        assertEquals("moveClip", o.getString("cmd"))
        assertEquals("c\"1", o.getString("clipId"))
        assertFalse(o.has("trackId"))
        assertEquals(-3.0, JSONObject(Cmd.setVolume("t", -3f)).getDouble("db"), 1e-6)
    }

    @Test fun musicTime() {
        assertEquals("1.1", MusicTime.barsBeats(0, 120.0, 4))
        assertEquals("1.2", MusicTime.barsBeats(24000, 120.0, 4))
        assertEquals("2.1", MusicTime.barsBeats(96000, 120.0, 4))
        assertEquals("0:01.50", MusicTime.clock(72000))
        assertEquals(24000L, MusicTime.snapToBeat(30000, 120.0))
        assertEquals(48000L, MusicTime.snapToBeat(37000, 120.0))
        assertEquals(480L, MusicTime.msToFrames(10.0))
    }

    @Test fun timelineGeometryRoundTrips() {
        val g = TimelineGeometry(100f)
        assertEquals(100f, g.x(48000), 1e-3f)
        assertEquals(48000L, g.frame(100f))
        assertEquals(0L, g.frame(-5f))
    }
}
