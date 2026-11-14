package com.caucesound.app.data

import org.json.JSONArray
import org.json.JSONObject

/**
 * Datos de los plugins integrados (misión 0.5). Todo lo que aparece aquí sale del motor: presets, rangos, resultados de
 * análisis y propuestas. La interfaz no inventa controles ni valores.
 */

/** Parámetros de VocalShift (los «macros» reales de su motor). Los rangos coinciden con cs::vocal::Params. */
data class VocalParams(
    val preset: String = "urban_spatial",
    val semitones: Float = 7f,          // -24..24
    val character: Float = 65f,         // 0..100 %
    val magic: Float = 70f,             // 0..100 %
    val secondVoice: Boolean = true,
    val secondGainDb: Float = -12f,     // -60..+6 dB
    val radical: Boolean = false,
    val spatial: Boolean = true,
    val formantOffset: Float = 0f,      // -12..12 st
    val brightnessOffset: Float = 0f,   // -12..12 dB
    val mixPercent: Float = 100f,       // 0..100 (solo sin segunda voz)
    val pan: Float = 0f,                // -100..100 (solo segunda voz)
    val offsetMs: Float = 0f,           // 0..100 ms (solo segunda voz)
) {
    fun toJson(): JSONObject = JSONObject()
        .put("preset", preset).put("semitones", semitones.toDouble()).put("character", character.toDouble()).put("magic", magic.toDouble())
        .put("secondVoice", secondVoice).put("secondGainDb", secondGainDb.toDouble()).put("radical", radical).put("spatial", spatial)
        .put("formantOffset", formantOffset.toDouble()).put("brightnessOffset", brightnessOffset.toDouble()).put("mixPercent", mixPercent.toDouble())
        .put("pan", pan.toDouble()).put("offsetMs", offsetMs.toDouble())

    companion object {
        fun fromJson(o: JSONObject?): VocalParams {
            val d = VocalParams()
            if (o == null) return d
            fun f(k: String, def: Float) = o.optDouble(k, def.toDouble()).toFloat()
            return VocalParams(
                o.optString("preset", d.preset), f("semitones", d.semitones), f("character", d.character), f("magic", d.magic),
                o.optBoolean("secondVoice", d.secondVoice), f("secondGainDb", d.secondGainDb), o.optBoolean("radical", d.radical),
                o.optBoolean("spatial", d.spatial), f("formantOffset", d.formantOffset), f("brightnessOffset", d.brightnessOffset),
                f("mixPercent", d.mixPercent), f("pan", d.pan), f("offsetMs", d.offsetMs),
            )
        }
    }
}

data class VocalPreset(val id: String, val name: String, val description: String, val params: VocalParams)

/** Procesado no destructivo de un clip: existe la versión original y la procesada; `playingOriginal` dice cuál suena. */
data class ClipProcess(val plugin: String, val version: String, val params: VocalParams, val playingOriginal: Boolean, val origLength: Long, val procLength: Long,
    /** Nivel RMS de la versión procesada respecto a la original (dB), medido por el motor; match = B se está escuchando
     *  compensada para comparar. Es un estado de escucha: no se guarda ni afecta a la exportación. */
    val levelDb: Float = 0f, val match: Boolean = false)

/** Trabajo offline en curso o terminado (cola del motor). state: queued, running, done, failed, cancelled. */
data class JobUi(val id: String, val type: String, val targetId: String, val state: String, val progress: Float, val error: String? = null) {
    val active: Boolean get() = state == "queued" || state == "running"
}

/** Medidas reales del último procesado de VocalShift (del motor, no estimadas por la interfaz). */
data class VocalResult(val clipId: String, val latencySamples: Int, val tailSeconds: Double, val inputPeak: Float, val outputPeak: Float, val seconds: Double, val clipAltered: Long)

data class KeyCandidate(val root: Int, val minor: Boolean, val score: Double)
data class ChordGuess(val root: Int, val quality: Int, val confidence: Double)

/** Resultado del análisis de un audio con Solo AI: ESTIMACIONES con confianza 0..1. Nada se aplica sin confirmar. */
data class BeatAnalysis(
    val clipId: String, val durationSec: Double, val bpm: Double, val bpmConfidence: Double, val bpmCandidates: List<Double>,
    val tonal: Boolean, val percussiveOnly: Boolean, val silent: Boolean, val keyRoot: Int, val minor: Boolean, val keyConfidence: Double,
    val keyCandidates: List<KeyCandidate>, val bars: Int, val chords: List<ChordGuess>, val chordConfidence: Double, val warnings: List<String>,
) {
    companion object {
        fun parse(r: JSONObject): BeatAnalysis {
            fun <T> arr(key: String, f: (JSONObject) -> T): List<T> = r.optJSONArray(key)?.let { a -> (0 until a.length()).map { f(a.getJSONObject(it)) } } ?: emptyList()
            return BeatAnalysis(
                r.optString("clipId"), r.optDouble("durationSec", 0.0), r.optDouble("bpm", 0.0), r.optDouble("bpmConfidence", 0.0),
                arr("bpmCandidates") { it.optDouble("bpm") }.filter { it > 0 }.distinctBy { Math.round(it * 10) }.take(4),
                r.optBoolean("tonal"), r.optBoolean("percussiveOnly"), r.optBoolean("silent"), r.optInt("keyRoot", -1), r.optBoolean("minor"),
                r.optDouble("keyConfidence", 0.0), arr("keyCandidates") { KeyCandidate(it.optInt("root"), it.optBoolean("minor"), it.optDouble("score")) },
                r.optInt("bars"), arr("chords") { ChordGuess(it.optInt("root"), it.optInt("quality"), it.optDouble("confidence")) },
                r.optDouble("chordConfidence", 0.0),
                r.optJSONArray("warnings")?.let { a -> (0 until a.length()).map { a.optString(it) } } ?: emptyList(),
            )
        }
    }
}

/** Confianza en palabras, con los mismos umbrales en toda la app. */
enum class Confidence { LOW, MEDIUM, HIGH;
    companion object { fun of(v: Double) = when { v >= 0.7 -> HIGH; v >= 0.45 -> MEDIUM; else -> LOW } }
}

data class SoloProposal(val name: String, val description: String, val notes: Int, val inScale: Double, val strongChordTones: Double, val similarity: Double)

/** Petición de Solo AI. Con la misma semilla, el motor devuelve exactamente las mismas notas. */
data class SoloRequest(
    val startBar: Int = 0, val bars: Int = 8, val type: String = "chorus", val character: String = "easy", val freedom: Int = 35,
    val seed: Long = 1, val keyRoot: Int? = null, val minor: Boolean = true, val preset: String = "bright_lead",
)

/** Familia de cuatro alternativas A-D. trackId = pista donde suena la elegida; variation != 0 = variación de la elegida. */
data class SoloFamily(val request: SoloRequest, val proposals: List<SoloProposal>, val diversity: Double, val selected: Int = -1, val trackId: String? = null, val variation: Long = 0) {
    /**
     * Comando que pone a sonar la alternativa [index] (0 = A … 3 = D): misma petición y semilla que la familia, el
     * número de la alternativa y la pista de Solo AI que se sustituye (si sigue existiendo). Lógica pura, probada en la
     * JVM (misión 0.7): si la interfaz enviara siempre la misma propuesta, la prueba lo detecta.
     */
    fun pickCommand(index: Int, variation: Long, existingTrackIds: Set<String>): String =
        Cmd.soloPick(request, index, variation, trackId?.takeIf { it in existingTrackIds })

    companion object {
        fun parse(req: SoloRequest, r: JSONObject): SoloFamily {
            val a = r.optJSONArray("proposals") ?: JSONArray()
            return SoloFamily(req, (0 until a.length()).map { i ->
                a.getJSONObject(i).let { SoloProposal(it.optString("name"), it.optString("description"), it.optInt("notes"), it.optDouble("inScale"), it.optDouble("strongChordTones"), it.optDouble("similarity")) }
            }, r.optDouble("diversity", 0.0))
        }
    }
}

/** Tonalidad confirmada por el usuario y guardada con el proyecto (a mano o tras analizar un audio). */
data class HarmonyState(val keyRoot: Int, val minor: Boolean, val source: String, val chords: Int, val chordsBpm: Double)

/** Escucha previa de VocalShift: una región corta procesada en memoria por el motor. No forma parte del proyecto. */
data class VocalPreviewState(val clipId: String, val from: Long, val length: Long, val listening: Boolean, val params: VocalParams, val seconds: Double = 0.0)

/**
 * Exportación para DAW en curso o recién terminada (estado de interfaz).
 * phase: running (el motor renderiza; progress es el real), saving (copiando a la carpeta elegida), done, failed, cancelled.
 */
data class DawExportUi(
    val phase: String, val jobId: String = "", val progress: Float = 0f, val wet: Boolean = true, val stems: Int = 0, val midiNotes: Int = 0,
    val files: Int = 0, val bytes: Long = 0, val clipped: Long = 0, val error: String? = null,
) {
    val active get() = phase == "running" || phase == "saving"
}

/** Estado de interfaz de los plugins: no forma parte del proyecto. */
data class PluginUi(
    val vocalPresets: List<VocalPreset> = emptyList(),
    val job: JobUi? = null,
    val vocalResult: VocalResult? = null,
    val analysis: BeatAnalysis? = null,
    val family: SoloFamily? = null,
    val export: DawExportUi? = null,
    /** 0.9.2: última variación aceptada (Variar/Regenerar/Rehacer) y si hay una en curso. */
    val variation: VariationReport? = null,
    val variationWorking: String? = null,
)

/** Una parte que cambió en una variación: cuánto (0..1, proporción de notas o golpes distintos). */
data class VariationPart(val trackId: String, val name: String, val role: String, val change: Float)

/**
 * Informe del motor tras Variar/Regenerar (0.9.2). revision = número de versión de la composición; parts = qué
 * pistas cambiaron; skippedLocked = pistas que se respetaron por tener candado.
 */
data class VariationReport(
    val op: String, val part: String, val revision: Int, val change: Float, val parts: List<VariationPart>,
    val skippedLocked: List<String>, val attempts: Int,
) {
    /** Partes que cambiaron de verdad (más del 2 % de sus notas o golpes). */
    val changed: List<VariationPart> get() = parts.filter { it.change > 0.02f }
}

object PluginParser {
    fun variation(raw: JSONObject): VariationReport? {
        val v = raw.optJSONObject("variation") ?: return null
        val parts = v.optJSONArray("parts")?.let { a -> (0 until a.length()).map { i -> a.getJSONObject(i).let {
            VariationPart(it.optString("trackId"), it.optString("name"), it.optString("role"), it.optDouble("change", 0.0).toFloat()) } } } ?: emptyList()
        val skipped = v.optJSONArray("skippedLocked")?.let { a -> (0 until a.length()).map { a.optString(it) } } ?: emptyList()
        return VariationReport(v.optString("op"), v.optString("part"), v.optInt("revision", 1), v.optDouble("change", 0.0).toFloat(), parts, skipped, v.optInt("attempts", 1))
    }

    fun vocalPresets(json: String): List<VocalPreset> {
        val a = JSONObject(json).optJSONArray("presets") ?: return emptyList()
        return (0 until a.length()).map { i -> a.getJSONObject(i).let { VocalPreset(it.getString("id"), it.optString("name"), it.optString("description"), VocalParams.fromJson(it.optJSONObject("params"))) } }
    }

    fun job(o: JSONObject): JobUi = JobUi(o.optString("jobId"), o.optString("type"), o.optString("targetId"), o.optString("state"), o.optDouble("progress", 0.0).toFloat(),
        o.optString("error").takeIf { it.isNotEmpty() })

    fun vocalResult(r: JSONObject): VocalResult = VocalResult(r.optString("clipId"), r.optInt("latencySamples"), r.optLong("tailFrames") / SAMPLE_RATE.toDouble(),
        r.optDouble("inputPeak", 0.0).toFloat(), r.optDouble("outputPeak", 0.0).toFloat(), r.optDouble("seconds", 0.0), r.optLong("clipAltered"))
}

object KeyNames {
    private val es = listOf("Do", "Do#", "Re", "Mib", "Mi", "Fa", "Fa#", "Sol", "Lab", "La", "Sib", "Si")
    fun name(root: Int): String = es[((root % 12) + 12) % 12]
    val all: List<String> get() = es
}
