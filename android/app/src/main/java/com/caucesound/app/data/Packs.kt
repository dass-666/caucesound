package com.caucesound.app.data

import org.json.JSONArray
import org.json.JSONObject

/**
 * Cauce Packs (misión 0.9): paquetes de sonidos instalados, tal como los publica el motor (comando "packs").
 * La interfaz no inventa instrumentos: todo sale del manifiesto de cada paquete (docs/CAUCE_PACK_FORMAT.md).
 */
data class PackInstrumentInfo(
    /** Identificador estable "<paquete>:<instrumento>" que se guarda en el proyecto. */
    val key: String,
    val name: String,
    /** drums | bass | keys | strings | melodic | percussion */
    val family: String,
    /** "drumkit" (pista de batería) o "melodic" (pista de instrumento). */
    val kind: String,
    val description: String = "",
    /** "original" (síntesis CAUCESOUND) o la licencia del origen ("cc0"...). */
    val origin: String = "original",
    val tags: List<String> = emptyList(),
    val genres: List<String> = emptyList(),
    val lanes: List<String> = emptyList(),
    val previewKey: Int = 60,
    val lowKey: Int = 0,
    val highKey: Int = 127,
    val mono: Boolean = false,
    val packId: String = "",
    val packName: String = "",
) {
    val isDrumKit: Boolean get() = kind == "drumkit"
}

data class PackSourceInfo(val name: String, val author: String, val url: String, val license: String)

data class PackInfo(
    val id: String,
    val version: String,
    val name: String,
    val author: String,
    val description: String,
    val license: String,
    val bytes: Long,
    /** Incluido en la app: no se puede borrar. */
    val factory: Boolean,
    /** Todos los archivos del manifiesto están presentes. */
    val complete: Boolean,
    val problems: List<String>,
    val sources: List<PackSourceInfo>,
    val instruments: List<PackInstrumentInfo>,
)

/** Resultado de importar un ZIP. */
data class PackImport(val ok: Boolean, val packId: String, val version: String, val bytes: Long, val replaced: Boolean, val error: String?)

object PackParser {
    private fun strings(a: JSONArray?) = a?.let { arr -> (0 until arr.length()).map { arr.optString(it) } } ?: emptyList()

    fun packs(json: String): List<PackInfo> {
        val o = runCatching { JSONObject(json) }.getOrNull() ?: return emptyList()
        val arr = o.optJSONArray("packs") ?: return emptyList()
        return (0 until arr.length()).map { i ->
            val p = arr.getJSONObject(i)
            val id = p.optString("id")
            val name = p.optString("name")
            val ins = p.optJSONArray("instruments") ?: JSONArray()
            PackInfo(
                id, p.optString("version"), name, p.optString("author"), p.optString("description"), p.optString("license"),
                p.optLong("bytes"), p.optBoolean("factory"), p.optBoolean("complete", true), strings(p.optJSONArray("problems")),
                p.optJSONArray("sources")?.let { s -> (0 until s.length()).map { s.getJSONObject(it).let { x -> PackSourceInfo(x.optString("name"), x.optString("author"), x.optString("url"), x.optString("license")) } } } ?: emptyList(),
                (0 until ins.length()).map { k ->
                    val x = ins.getJSONObject(k)
                    PackInstrumentInfo(
                        x.optString("key"), x.optString("name"), x.optString("family"), x.optString("kind"), x.optString("description"),
                        x.optString("origin", "original"), strings(x.optJSONArray("tags")), strings(x.optJSONArray("genres")), strings(x.optJSONArray("lanes")),
                        x.optInt("previewKey", 60), x.optInt("lowKey", 0), x.optInt("highKey", 127), x.optBoolean("mono"), id, name,
                    )
                },
            )
        }
    }

    fun import(json: String): PackImport {
        val o = runCatching { JSONObject(json) }.getOrDefault(JSONObject())
        return PackImport(o.optBoolean("ok"), o.optString("packId"), o.optString("version"), o.optLong("bytes"), o.optBoolean("replaced"), o.optString("error").takeIf { it.isNotEmpty() })
    }

    /** Proyectos que usan un paquete: nombres (para avisar antes de borrar). */
    fun usage(json: String): List<String> {
        val a = runCatching { JSONObject(json).optJSONArray("projects") }.getOrNull() ?: return emptyList()
        return (0 until a.length()).map { a.getJSONObject(it).optString("name") }
    }
}

/** Filtros del explorador de sonidos. Lógica pura: se prueba en la JVM sin el motor. */
object SoundBrowserFilter {
    /** Categorías visibles: solo las que tienen al menos un instrumento instalado (no se rellenan artificialmente). */
    val CATEGORY_ORDER = listOf("drums", "bass", "keys", "strings", "guitar", "percussion", "melodic")

    fun categories(all: List<PackInstrumentInfo>): List<String> = CATEGORY_ORDER.filter { c -> all.any { it.family == c } } +
        all.map { it.family }.distinct().filter { it !in CATEGORY_ORDER }

    fun genres(all: List<PackInstrumentInfo>): List<String> = all.flatMap { it.genres }.distinct().sorted()

    /**
     * category: "all", "fav" o una familia. kindFilter: null, "drumkit" o "melodic" (lo que admite la pista actual).
     * query busca en nombre, paquete, etiquetas, géneros y descripción, sin distinguir mayúsculas ni acentos.
     */
    fun filter(
        all: List<PackInstrumentInfo>, query: String, category: String, genre: String?, favorites: Set<String>, kindFilter: String? = null,
    ): List<PackInstrumentInfo> {
        val q = normalize(query.trim())
        return all.filter { i ->
            (kindFilter == null || i.kind == kindFilter) &&
                when (category) { "all" -> true; "fav" -> i.key in favorites; else -> i.family == category } &&
                (genre == null || genre in i.genres) &&
                (q.isEmpty() || q.split(' ').filter { it.isNotEmpty() }.all { w ->
                    (listOf(i.name, i.packName, i.description, i.family) + i.tags + i.genres).any { normalize(it).contains(w) }
                })
        }.sortedWith(compareBy({ it.key !in favorites }, { it.name }))
    }

    fun normalize(s: String): String = java.text.Normalizer.normalize(s.lowercase(), java.text.Normalizer.Form.NFD).replace(Regex("\\p{M}+"), "")

    fun sizeLabel(bytes: Long): String = when {
        bytes >= 1024L * 1024 -> "%.1f MB".format(bytes / 1048576.0)
        bytes >= 1024 -> "${bytes / 1024} KB"
        else -> "$bytes B"
    }
}

/** Estado de interfaz del explorador de sonidos (no forma parte del proyecto). */
data class SoundUi(
    val packs: List<PackInfo> = emptyList(),
    val favorites: Set<String> = emptySet(),
    /** Instrumento que está sonando en la escucha previa (null = ninguno). */
    val previewing: String? = null,
    /** Paquete que se pide borrar y proyectos que lo usan (confirmación). */
    val removeAsk: Pair<PackInfo, List<String>>? = null,
    /** Importación en curso. */
    val importing: Boolean = false,
) {
    val instruments: List<PackInstrumentInfo> get() = packs.flatMap { it.instruments }
    fun instrument(key: String): PackInstrumentInfo? = instruments.firstOrNull { it.key == key }
    fun packOf(key: String): PackInfo? = packs.firstOrNull { key.startsWith(it.id + ":") }
}

/** Sonidos instalados, accesibles desde cualquier panel (nombres de kits de muestras en el Rack, etc.). */
val LocalSounds = androidx.compose.runtime.staticCompositionLocalOf { SoundUi() }
