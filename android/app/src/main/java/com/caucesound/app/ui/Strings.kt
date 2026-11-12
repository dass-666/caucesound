package com.caucesound.app.ui

import java.util.Locale

/** Textos de las pantallas nuevas: español primero (mercado inicial), inglés para el resto de idiomas. */
fun tr(es: String, en: String): String = if (Locale.getDefault().language == "es") es else en

object Genre {
    fun name(id: String) = when (id) {
        "trap" -> "Trap"; "reggaeton" -> "Reggaetón"; "hiphop" -> "Hip-hop"; "pop" -> "Pop"; "electronic" -> tr("Electrónica", "Electronic")
        else -> tr("Proyecto vacío", "Empty project")
    }
}
