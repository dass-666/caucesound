package com.caucesound.app.data

import org.json.JSONObject

/** AutoMix (0.9, prototipo por reglas, no IA): propuesta de faders por pista. */
data class AutoMixSuggestion(
    val trackId: String, val name: String, val role: String, val reason: String,
    val measuredDb: Float, val targetDb: Float, val volumeDb: Float, val suggestedVolumeDb: Float, val pan: Float, val suggestedPan: Float, val change: Boolean,
)

data class AutoMixUi(val suggestions: List<AutoMixSuggestion> = emptyList(), val canRevert: Boolean = false, val analyzing: Boolean = false, val open: Boolean = false)

object AutoMixParser {
    fun parse(json: String): AutoMixUi {
        val o = runCatching { JSONObject(json) }.getOrNull() ?: return AutoMixUi()
        val a = o.optJSONArray("suggestions") ?: return AutoMixUi(canRevert = o.optBoolean("canRevert"))
        return AutoMixUi((0 until a.length()).map { i ->
            val x = a.getJSONObject(i)
            AutoMixSuggestion(x.optString("trackId"), x.optString("name"), x.optString("role"), x.optString("reason"), x.optDouble("measuredDb").toFloat(),
                x.optDouble("targetDb").toFloat(), x.optDouble("volumeDb").toFloat(), x.optDouble("suggestedVolumeDb").toFloat(), x.optDouble("pan").toFloat(),
                x.optDouble("suggestedPan").toFloat(), x.optBoolean("change"))
        }, o.optBoolean("canRevert"), open = true)
    }
}
