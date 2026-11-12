package com.caucesound.app.data

import org.json.JSONObject

data class ParamInfo(val id: String, val name: String, val min: Float, val max: Float, val default: Float)
data class PresetInfo(val id: String, val name: String)
data class KitInfo(val id: String, val name: String, val lanes: List<String>)
data class EffectInfo(val type: String, val params: List<ParamInfo>)
data class TemplateInfo(val id: String, val bpm: Double, val kit: String, val tracks: List<String>)

/** Catálogo que publica el motor (presets, kits, parámetros): Android e iOS muestran exactamente lo mismo. */
data class Catalog(
    val synthPresets: List<PresetInfo>,
    val drumKits: List<KitInfo>,
    val synthParams: List<ParamInfo>,
    val drumParams: List<ParamInfo>,
    val effects: List<EffectInfo>,
    val templates: List<TemplateInfo> = emptyList(),
) {
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
            val presets = o.optJSONArray("synthPresets")?.let { a -> (0 until a.length()).map { a.getJSONObject(it).let { p -> PresetInfo(p.getString("id"), p.getString("name")) } } } ?: emptyList()
            val kits = o.optJSONArray("drumKits")?.let { a ->
                (0 until a.length()).map { a.getJSONObject(it).let { k -> KitInfo(k.getString("id"), k.getString("name"), k.getJSONArray("lanes").let { l -> (0 until l.length()).map { l.getString(it) } }) } }
            } ?: emptyList()
            val fx = o.optJSONArray("effects")?.let { a -> (0 until a.length()).map { a.getJSONObject(it).let { e -> EffectInfo(e.getString("type"), paramList(e.optJSONArray("params"))) } } } ?: emptyList()
            val tpl = o.optJSONArray("templates")?.let { a ->
                (0 until a.length()).map { a.getJSONObject(it).let { t -> TemplateInfo(t.getString("id"), t.getDouble("bpm"), t.getString("kit"), t.getJSONArray("tracks").let { l -> (0 until l.length()).map { l.getString(it) } }) } }
            } ?: emptyList()
            return Catalog(presets, kits, params("synthParams"), params("drumParams"), fx, tpl)
        }
    }
}
