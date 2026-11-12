package com.caucesound.app.data

import org.json.JSONArray
import org.json.JSONObject

const val SAMPLE_RATE = 48000

data class ClipState(
    val id: String,
    val sourceId: String,
    val name: String,
    val start: Long,
    val sourceOffset: Long,
    val length: Long,
    val fadeIn: Long,
    val fadeOut: Long,
    val gainDb: Float,
    val missing: Boolean,
) {
    val end: Long get() = start + length
}

const val PPQ = 960
const val STEP_TICKS = PPQ / 4
const val DRUM_BASE_NOTE = 36

/** Nota MIDI de un clip (ticks relativos al inicio del clip). */
data class NoteState(val key: Int, val velocity: Int, val channel: Int, val start: Long, val length: Long) {
    val end: Long get() = start + length
}

data class PatternState(val id: String, val name: String, val steps: Int, val swing: Float, val lanes: List<List<Int>>)

data class MidiClipState(val id: String, val name: String, val patternId: String, val start: Long, val length: Long, val notes: List<NoteState>) {
    val end: Long get() = start + length
    val isPattern: Boolean get() = patternId.isNotEmpty()
}

data class TrackState(
    val id: String,
    val name: String,
    val volumeDb: Float,
    val pan: Float,
    val mute: Boolean,
    val solo: Boolean,
    val armed: Boolean,
    val color: Long,
    val clips: List<ClipState>,
    val kind: String = "audio",
    val instrumentType: String = "",
    val params: Map<String, Float> = emptyMap(),
    val preset: String = "",
    val patterns: List<PatternState> = emptyList(),
    val midiClips: List<MidiClipState> = emptyList(),
) {
    val isAudio: Boolean get() = kind == "audio"
    val isDrums: Boolean get() = kind == "drums"
    val isSynth: Boolean get() = kind == "synth"
    fun pattern(id: String): PatternState? = patterns.firstOrNull { it.id == id }
}

data class SourceState(val id: String, val frames: Long, val channels: Int, val origin: String)

data class ProjectState(
    val id: String,
    val name: String,
    val folder: String,
    val bpm: Double,
    val beatsPerBar: Int,
    val metronome: Boolean,
    val masterDb: Float,
    val recordLatencyOffset: Long,
    val tracks: List<TrackState>,
    val sources: Map<String, SourceState>,
    val dirty: Boolean,
    val canUndo: Boolean,
    val canRedo: Boolean,
    val recording: Boolean,
    val contentEnd: Long,
    val loopEnabled: Boolean = false,
    val loopStart: Long = 0,
    val loopEnd: Long = 16L * PPQ,
    val templateId: String = "",
) {
    fun clip(id: String): Pair<TrackState, ClipState>? {
        for (t in tracks) for (c in t.clips) if (c.id == id) return t to c
        return null
    }
    fun midiClip(id: String): Pair<TrackState, MidiClipState>? {
        for (t in tracks) for (c in t.midiClips) if (c.id == id) return t to c
        return null
    }
    fun trackIndex(id: String): Int = tracks.indexOfFirst { it.id == id }
    fun ticksToFrames(t: Long): Long = MusicTime.ticksToFrames(t, bpm)
    fun framesToTicks(f: Long): Long = MusicTime.framesToTicks(f, bpm)
}

data class ThumbTrack(val color: Long, val kind: String, val spans: List<Pair<Float, Float>>)

data class ProjectSummary(
    val folder: String, val name: String, val modifiedAt: Long, val tracks: Int, val error: String?,
    val bpm: Double = 120.0, val templateId: String = "", val thumb: List<ThumbTrack> = emptyList(),
)

data class CommandResult(val ok: Boolean, val id: String?, val error: String?, val raw: JSONObject)

object StateParser {
    fun result(json: String): CommandResult {
        val o = JSONObject(json)
        return CommandResult(
            o.optBoolean("ok", false),
            if (o.has("id")) o.getString("id") else null,
            if (o.has("error")) o.getString("error") else null,
            o,
        )
    }

    fun projects(json: String): List<ProjectSummary> {
        val arr = JSONObject(json).optJSONArray("projects") ?: JSONArray()
        return (0 until arr.length()).map {
            val p = arr.getJSONObject(it)
            val thumb = mutableListOf<ThumbTrack>()
            p.optJSONArray("thumb")?.let { ta ->
                for (i in 0 until ta.length()) {
                    val t = ta.getJSONObject(i)
                    val sp = t.optJSONArray("spans") ?: JSONArray()
                    thumb += ThumbTrack(t.optLong("color"), t.optString("kind", "audio"),
                        (0 until sp.length()).map { k -> sp.getJSONArray(k).let { it.getDouble(0).toFloat() to it.getDouble(1).toFloat() } })
                }
            }
            ProjectSummary(
                p.getString("folder"), p.optString("name"), p.optLong("modifiedAt"), p.optInt("tracks"),
                if (p.has("error")) p.getString("error") else null,
                p.optDouble("bpm", 120.0), p.optString("template"), thumb,
            )
        }.sortedByDescending { it.modifiedAt }
    }

    /** Devuelve null si no hay proyecto abierto. */
    fun state(json: String): ProjectState? {
        val o = JSONObject(json)
        if (!o.optBoolean("open", false)) return null
        val p = o.getJSONObject("project")
        val missing = mutableSetOf<String>()
        o.optJSONArray("missingClips")?.let { for (i in 0 until it.length()) missing += it.getString(i) }
        val sources = mutableMapOf<String, SourceState>()
        val sArr = p.getJSONArray("sources")
        for (i in 0 until sArr.length()) {
            val s = sArr.getJSONObject(i)
            sources[s.getString("id")] = SourceState(s.getString("id"), s.getLong("frames"), s.getInt("channels"), s.optString("origin"))
        }
        val tracks = mutableListOf<TrackState>()
        val tArr = p.getJSONArray("tracks")
        for (i in 0 until tArr.length()) {
            val t = tArr.getJSONObject(i)
            val cArr = t.getJSONArray("clips")
            val clips = (0 until cArr.length()).map { k ->
                val c = cArr.getJSONObject(k)
                ClipState(
                    c.getString("id"), c.getString("sourceId"), c.optString("name"), c.getLong("start"),
                    c.getLong("sourceOffset"), c.getLong("length"), c.optLong("fadeIn"), c.optLong("fadeOut"),
                    c.optDouble("gainDb", 0.0).toFloat(), c.getString("id") in missing,
                )
            }.sortedBy { it.start }
            val inst = t.optJSONObject("instrument")
            val params = mutableMapOf<String, Float>()
            inst?.optString("state")?.takeIf { it.isNotEmpty() }?.let { st ->
                runCatching { JSONObject(st) }.getOrNull()?.let { o -> for (k in o.keys()) o.optDouble(k).takeIf { !it.isNaN() }?.let { params[k] = it.toFloat() } }
            }
            val patterns = mutableListOf<PatternState>()
            t.optJSONArray("patterns")?.let { pa ->
                for (k in 0 until pa.length()) {
                    val po = pa.getJSONObject(k)
                    val la = po.getJSONArray("lanes")
                    patterns += PatternState(po.getString("id"), po.optString("name"), po.optInt("steps", 16), po.optDouble("swing", 0.0).toFloat(),
                        (0 until la.length()).map { l -> la.getJSONArray(l).let { a -> (0 until a.length()).map { a.getInt(it) } } })
                }
            }
            val midi = mutableListOf<MidiClipState>()
            t.optJSONArray("midiClips")?.let { ma ->
                for (k in 0 until ma.length()) {
                    val mo = ma.getJSONObject(k)
                    val na = mo.optJSONArray("notes") ?: JSONArray()
                    val notes = (0 until na.length()).map { n -> na.getJSONArray(n).let { NoteState(it.getInt(0), it.getInt(1), it.getInt(2), it.getLong(3), it.getLong(4)) } }
                    midi += MidiClipState(mo.getString("id"), mo.optString("name"), mo.optString("patternId"), mo.getLong("start"), mo.getLong("length"), notes)
                }
            }
            tracks += TrackState(
                t.getString("id"), t.optString("name"), t.optDouble("volumeDb", 0.0).toFloat(),
                t.optDouble("pan", 0.0).toFloat(), t.optBoolean("mute"), t.optBoolean("solo"), t.optBoolean("armed"),
                t.optLong("color", 0xFF4FC3F7), clips, t.optString("kind", "audio"), inst?.optString("type") ?: "", params,
                inst?.optString("preset") ?: "", patterns, midi.sortedBy { it.start },
            )
        }
        return ProjectState(
            p.getString("id"), p.optString("name"), o.optString("folder"), p.optDouble("bpm", 120.0),
            p.optInt("beatsPerBar", 4), p.optBoolean("metronome"), p.optDouble("masterDb", 0.0).toFloat(),
            p.optLong("recordLatencyOffset"), tracks, sources, o.optBoolean("dirty"), o.optBoolean("canUndo"),
            o.optBoolean("canRedo"), o.optBoolean("recording"), o.optLong("contentEnd"),
            p.optJSONObject("loop")?.optBoolean("enabled") ?: false,
            p.optJSONObject("loop")?.optLong("start") ?: 0L,
            p.optJSONObject("loop")?.optLong("end", 16L * PPQ) ?: 16L * PPQ,
            p.optString("template"),
        )
    }
}

/** Utilidades de tiempo musical (puras, probadas en test/). */
object MusicTime {
    fun framesPerBeat(bpm: Double): Double = 60.0 * SAMPLE_RATE / bpm

    /** "compás.tiempo" empezando en 1.1 */
    fun barsBeats(frame: Long, bpm: Double, beatsPerBar: Int): String {
        val beat = (frame / framesPerBeat(bpm)).toLong()
        return "${beat / beatsPerBar + 1}.${beat % beatsPerBar + 1}"
    }

    fun clock(frame: Long): String {
        val totalMs = frame * 1000 / SAMPLE_RATE
        val m = totalMs / 60000
        val s = (totalMs / 1000) % 60
        val cs = (totalMs / 10) % 100
        return "%d:%02d.%02d".format(m, s, cs)
    }

    /** Ajusta a la rejilla de tiempos (beat) más cercana. */
    fun snapToBeat(frame: Long, bpm: Double): Long {
        val fpb = framesPerBeat(bpm)
        return (Math.round(frame / fpb) * fpb).toLong().coerceAtLeast(0)
    }

    fun msToFrames(ms: Double): Long = Math.round(ms * SAMPLE_RATE / 1000.0)

    // Mismo redondeo que cs::ticksToFrames (núcleo C++): la interfaz y el motor coinciden en la muestra.
    fun ticksToFrames(t: Long, bpm: Double): Long = Math.round(t * 60.0 * SAMPLE_RATE / (bpm * PPQ))
    fun framesToTicks(f: Long, bpm: Double): Long = Math.round(f * bpm * PPQ / (60.0 * SAMPLE_RATE))
    fun snapTicks(t: Long, grid: Long): Long = if (grid <= 0) t else (Math.round(t.toDouble() / grid) * grid).coerceAtLeast(0)
    fun floorTicks(t: Long, grid: Long): Long = if (grid <= 0) t else Math.floorDiv(t, grid) * grid

    /** Paso del patrón que suena en 'frame' para un clip de patrón (o -1 si el cursor no está en el clip). */
    fun activeStep(frame: Long, bpm: Double, clipStart: Long, clipLength: Long, steps: Int): Int {
        val tick = framesToTicks(frame, bpm)
        if (tick < clipStart || tick >= clipStart + clipLength) return -1
        return (((tick - clipStart) / STEP_TICKS) % steps).toInt()
    }

    private val NAMES = arrayOf("C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B")
    fun noteName(key: Int): String = NAMES[key % 12] + (key / 12 - 1)
    fun isBlack(key: Int): Boolean = (key % 12) in intArrayOf(1, 3, 6, 8, 10)
    fun framesToMs(f: Long): Double = f * 1000.0 / SAMPLE_RATE
}
