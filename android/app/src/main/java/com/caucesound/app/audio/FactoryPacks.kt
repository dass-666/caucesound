package com.caucesound.app.audio

import android.content.res.AssetManager
import java.io.File

/**
 * Paquetes de fábrica (misión 0.9). Vienen dentro del APK en assets/packs/<id>/ y el motor necesita rutas de archivo,
 * así que se copian una vez a filesDir/packs/factory/<id>/. Solo se vuelve a copiar si cambia el manifiesto
 * (otra versión de la app). La copia se hace en una carpeta temporal y se renombra al final: si la app se cierra a
 * medias, el paquete anterior sigue intacto y en el siguiente arranque se repite.
 */
object FactoryPacks {
    fun install(assets: AssetManager, factoryDir: File): List<String> {
        factoryDir.mkdirs()
        val ids = runCatching { assets.list("packs")?.toList() }.getOrNull().orEmpty()
        val done = mutableListOf<String>()
        for (id in ids) {
            if (!id.matches(Regex("[a-z0-9._-]+"))) continue
            val manifest = runCatching { assets.open("packs/$id/cauce-pack.json").use { it.readBytes() } }.getOrNull() ?: continue
            val dest = File(factoryDir, id)
            val current = File(dest, "cauce-pack.json")
            if (current.isFile && current.readBytes().contentEquals(manifest)) { done += id; continue }
            val tmp = File(factoryDir, ".tmp-$id")
            tmp.deleteRecursively()
            if (!copyDir(assets, "packs/$id", tmp)) { tmp.deleteRecursively(); continue }
            val old = File(factoryDir, ".old-$id")
            old.deleteRecursively()
            if (dest.exists() && !dest.renameTo(old)) { tmp.deleteRecursively(); continue }
            if (tmp.renameTo(dest)) { old.deleteRecursively(); done += id } else old.renameTo(dest)
        }
        // Paquetes de fábrica que ya no trae esta versión: se borran (los importados por el usuario van en otra carpeta).
        factoryDir.listFiles()?.forEach { f -> if (f.isDirectory && f.name !in ids) f.deleteRecursively() }
        return done
    }

    private fun copyDir(assets: AssetManager, path: String, to: File): Boolean = runCatching {
        val children = assets.list(path).orEmpty()
        if (children.isEmpty()) {
            to.parentFile?.mkdirs()
            assets.open(path).use { input -> to.outputStream().use { input.copyTo(it, 1 shl 16) } }
        } else {
            to.mkdirs()
            for (c in children) if (!copyDir(assets, "$path/$c", File(to, c))) return false
        }
        true
    }.getOrDefault(false)
}
