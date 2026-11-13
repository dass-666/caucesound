package com.caucesound.app.data

import org.json.JSONObject

data class ParamInfo(val id: String, val name: String, val min: Float, val max: Float, val default: Float)
data class PresetInfo(val id: String, val name: String, val category: String = "")
data class KitInfo(val id: String, val name: String, val lanes: List<String>)
/**
 * Efecto de inserción con su ficha para el selector del Mixer Pro (0.7). category: eq, dynamics, reverb, delay,
 * modulation, distortion, vocal, tools. Todo lo publica el motor: la interfaz no inventa efectos ni categorías.
 */
data class EffectInfo(
    val type: String, val params: List<ParamInfo>, val category: String = "tools", val name: String = "", val nameEn: String = "",
    val description: String = "", val descriptionEn: String = "", val latencySamples: Int = 0,
)
/** Cadena de efectos completa (preset de cadena). steps: tipos en orden de procesamiento. */
data class FxChainInfo(val id: String, val name: String, val nameEn: String, val description: String, val descriptionEn: String, val suits: String, val steps: List<String>)
data class TemplateInfo(val id: String, val bpm: Double, val kit: String, val tracks: List<String>)

/** Catálogo que publica el motor (presets, kits, parámetros): Android e iOS muestran exactamente lo mismo. */
data class Catalog(
    val synthPresets: List<PresetInfo>,
    val drumKits: List<KitInfo>,
    val synthParams: List<ParamInfo>,
    val drumParams: List<ParamInfo>,
    val effects: List<EffectInfo>,
    val templates: List<TemplateInfo> = emptyList(),
    /** Plugins compilados en el motor (p. ej. "solo.ai"). Lo que no está aquí no se muestra en la interfaz. */
    val plugins: List<String> = emptyList(),
    val fxChains: List<FxChainInfo> = emptyList(),
    /** Parámetros de Cauce Sampler (0.9). */
    val samplerParams: List<ParamInfo> = emptyList(),
) {
    fun samplerParam(id: String) = samplerParams.firstOrNull { it.id == id }
    fun hasPlugin(id: String) = id in plugins
    fun synthParam(id: String) = synthParams.firstOrNull { it.id == id }
    fun effect(type: String) = effects.firstOrNull { it.type == type }

    companion object {
        val EMPTY = Catalog(emptyList(), emptyList(), emptyList(), emptyList(), emptyList())

        fun parse(json: String): Catalog {
            val o = JSONObject(json)
            fun paramList(a: org.json.JSONArray?) = a?.let { arr ->
                (0 until arr.length()).map { arr.getJSONObject(it).let { p -> ParamInfo(p.getString("id"), p.optString("name"), p.getDouble("min").toFloat(), p.getDouble("max").toFloat(), p.getDouble("default").toFloat()) } }
            } ?: emptyList()
            fun params(key: String) = o.optJSONArray(key)?.let { a ->
                (0 until a.length()).map { a.getJSONObject(it).let { p -> ParamInfo(p.getString("id"), p.optString("name"), p.getDouble("min").toFloat(), p.getDouble("max").toFloat(), p.getDouble("default").toFloat()) } }
            } ?: emptyList()
            val presets = o.optJSONArray("synthPresets")?.let { a -> (0 until a.length()).map { a.getJSONObject(it).let { p -> PresetInfo(p.getString("id"), p.getString("name"), p.optString("category")) } } } ?: emptyList()
            val kits = o.optJSONArray("drumKits")?.let { a ->
                (0 until a.length()).map { a.getJSONObject(it).let { k -> KitInfo(k.getString("id"), k.getString("name"), k.getJSONArray("lanes").let { l -> (0 until l.length()).map { l.getString(it) } }) } }
            } ?: emptyList()
            val fx = o.optJSONArray("effects")?.let { a -> (0 until a.length()).map { a.getJSONObject(it).let { e ->
                EffectInfo(e.getString("type"), paramList(e.optJSONArray("params")), e.optString("category", "tools"), e.optString("name"), e.optString("nameEn"),
                    e.optString("description"), e.optString("descriptionEn"), e.optInt("latencySamples", 0))
            } } } ?: emptyList()
            val chains = o.optJSONArray("fxChains")?.let { a -> (0 until a.length()).map { a.getJSONObject(it).let { c ->
                FxChainInfo(c.getString("id"), c.optString("name"), c.optString("nameEn"), c.optString("description"), c.optString("descriptionEn"), c.optString("suits"),
                    c.optJSONArray("steps")?.let { s -> (0 until s.length()).map { s.getString(it) } } ?: emptyList())
            } } } ?: emptyList()
            val tpl = o.optJSONArray("templates")?.let { a ->
                (0 until a.length()).map { a.getJSONObject(it).let { t -> TemplateInfo(t.getString("id"), t.getDouble("bpm"), t.getString("kit"), t.getJSONArray("tracks").let { l -> (0 until l.length()).map { l.getString(it) } }) } }
            } ?: emptyList()
            val plugins = o.optJSONArray("plugins")?.let { a -> (0 until a.length()).map { a.getJSONObject(it).getString("id") } } ?: emptyList()
            return Catalog(presets, kits, params("synthParams"), params("drumParams"), fx, tpl, plugins, chains, params("samplerParams"))
        }
    }
}
