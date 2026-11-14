package com.caucesound.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.FileUpload
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Search
import androidx.compose.material.icons.filled.Star
import androidx.compose.material.icons.filled.StarBorder
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Icon
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.data.PackInfo
import com.caucesound.app.data.PackInstrumentInfo
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.SoundBrowserFilter
import com.caucesound.app.data.SoundUi
import com.caucesound.app.data.TrackState

/** Nombre visible de cada familia (las que no tengan instrumentos instalados no aparecen). */
fun familyLabel(f: String): String = when (f) {
    "drums" -> tr("Baterías", "Drum kits")
    "bass" -> tr("Bajos", "Bass")
    "keys" -> tr("Teclados", "Keys")
    "strings" -> tr("Cuerdas", "Strings")
    "guitar" -> tr("Guitarras", "Guitars")
    "percussion" -> tr("Percusión", "Percussion")
    else -> tr("Melódicos", "Melodic")
}

private fun genreLabel(g: String): String = when (g) {
    "boombap" -> "Boom Bap"; "trap" -> "Trap"; "darkhiphop" -> "Dark Hip Hop"; "jazzrap" -> "Jazz Rap"; "rnb" -> "R&B"
    "reggaeton" -> "Reggaetón"; "latin" -> tr("Latino", "Latin"); "drill" -> "Drill"; "pop" -> "Pop"; "electronic" -> tr("Electrónica", "Electronic")
    "funk" -> "Funk"; "ballad" -> tr("Balada", "Ballad"); "lofi" -> "Lo-fi"; "afro" -> "Afro"
    else -> g.replaceFirstChar { it.uppercase() }
}

private fun originLabel(o: String): String {
    val l = o.lowercase()
    val own = "original" in l
    val cc0 = "cc0" in l
    return when {
        own && cc0 -> tr("Cauce + CC0", "Cauce + CC0")
        own -> tr("Original Cauce", "Cauce original")
        cc0 -> tr("Grabación real CC0", "Real recording, CC0")
        else -> o
    }
}

/**
 * Explorador de sonidos (misión 0.9): instrumentos de los paquetes instalados con búsqueda, categorías, géneros,
 * favoritos, escucha previa, carga en la pista seleccionada y gestión de paquetes (importar ZIP, borrar importados).
 * Solo muestra lo que publica el motor.
 */
@OptIn(ExperimentalLayoutApi::class)
@Composable
fun SoundBrowserPanel(state: ProjectState, track: TrackState?, sounds: SoundUi, actions: StudioActions, onImportPack: () -> Unit) {
    var query by rememberSaveable { mutableStateOf("") }
    var category by rememberSaveable { mutableStateOf("all") }
    var genre by rememberSaveable { mutableStateOf<String?>(null) }
    var tab by rememberSaveable { mutableStateOf(0) }
    // Lo que admite la pista: una batería acepta kits; una pista de instrumento, instrumentos melódicos.
    val kindFilter = when { track?.isDrums == true -> "drumkit"; track?.isSynth == true -> "melodic"; else -> null }
    val all = sounds.instruments
    Column(Modifier.fillMaxSize().padding(horizontal = 12.dp, vertical = 8.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        PanelHeader(tr("Sonidos Cauce", "Cauce Sounds"),
            when { track == null -> tr("Elige una pista para cargar un sonido", "Pick a track to load a sound")
                track.isDrums -> tr("Kits para «${track.name}»", "Kits for “${track.name}”")
                track.isSynth -> tr("Instrumentos para «${track.name}»", "Instruments for “${track.name}”")
                else -> tr("Las pistas de audio no usan instrumentos", "Audio tracks do not use instruments") }) {
            if (sounds.previewing != null) ToolButton(Icons.Default.Stop, tr("Parar la escucha", "Stop preview"), { actions.stopSoundPreview() }, active = true, activeColor = Palette.Cyan)
        }
        if (state.missingInstruments.isNotEmpty()) MissingSoundsBanner(state.missingInstruments)
        if (sounds.missingFactory.isNotEmpty())
            Text(tr("Esta versión debería traer sonidos de fábrica que no se encuentran: ${sounds.missingFactory.joinToString()}. Reinstala la app; si sigue igual, avisa: el APK está incompleto.",
                "This version should include factory sounds that were not found: ${sounds.missingFactory.joinToString()}. Reinstall the app; if it persists, report it: the APK is incomplete."),
                color = Palette.Warn, fontSize = 13.sp, modifier = Modifier.testTag("factory_missing"))
        Segmented(listOf(tr("Instrumentos", "Instruments"), tr("Paquetes", "Packs")), tab, { tab = it }, Modifier.testTag("sounds_tabs"), height = 38.dp)
        if (tab == 1) { PackList(sounds, actions, onImportPack); return@Column }

        OutlinedTextField(query, { query = it }, Modifier.fillMaxWidth().testTag("sounds_search"), singleLine = true,
            leadingIcon = { Icon(Icons.Default.Search, null) }, placeholder = { Text(tr("Buscar: 808, cálido, boom bap…", "Search: 808, warm, boom bap…")) },
            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
            colors = OutlinedTextFieldDefaults.colors(focusedBorderColor = Palette.Violet, unfocusedBorderColor = Palette.GridStrong))
        val cats = listOf("all" to tr("Todos", "All"), "fav" to "★") + SoundBrowserFilter.categories(all).map { it to familyLabel(it) }
        Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            cats.forEach { (id, label) -> PillToggle(label, category == id, { category = id }, height = 36.dp) }
        }
        val genres = SoundBrowserFilter.genres(all)
        if (genres.isNotEmpty()) Row(Modifier.horizontalScroll(rememberScrollState()), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            genres.forEach { g -> PillToggle(genreLabel(g), genre == g, { genre = if (genre == g) null else g }, color = Palette.Blue, height = 32.dp) }
        }
        val shown = SoundBrowserFilter.filter(all, query, category, genre, sounds.favorites, kindFilter)
        if (all.isEmpty()) {
            Text(tr("No hay paquetes de sonidos instalados. Puedes importar un paquete Cauce (.zip) en la pestaña Paquetes.",
                "No sound packs installed. You can import a Cauce pack (.zip) in the Packs tab."), color = Palette.TextDim, fontSize = 13.sp)
        } else if (shown.isEmpty()) {
            Text(when {
                category == "fav" -> tr("Aún no tienes favoritos: toca la estrella de un sonido.", "No favorites yet: tap a sound's star.")
                kindFilter != null && all.none { it.kind == kindFilter } -> tr("Ningún paquete instalado tiene sonidos para este tipo de pista.", "No installed pack has sounds for this track type.")
                else -> tr("Nada coincide con la búsqueda.", "Nothing matches your search.")
            }, color = Palette.TextDim, fontSize = 13.sp)
        }
        LazyColumn(Modifier.weight(1f).testTag("sounds_list"), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            items(shown, key = { it.key }) { i -> SoundRow(i, track, sounds, actions) }
            if (track?.isDrums == true && track.soundKey.isNotEmpty()) item {
                CauceButton(tr("Volver al kit sintetizado", "Back to the synth kit"), { actions.loadSound(track.id, "") }, Modifier.fillMaxWidth(), kind = ButtonKind.Ghost)
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun SoundRow(i: PackInstrumentInfo, track: TrackState?, sounds: SoundUi, actions: StudioActions) {
    val loaded = track?.soundKey == i.key
    val playing = sounds.previewing == i.key
    val canLoad = track != null && ((i.isDrumKit && track.isDrums) || (!i.isDrumKit && track.isSynth))
    Column(Modifier.fillMaxWidth().clip12().background(if (loaded) Palette.Violet.copy(alpha = 0.16f) else Palette.SurfaceHigh)
        .border(1.dp, if (loaded) Palette.Violet else Palette.Grid, RoundedCornerShape(12.dp)).padding(10.dp).testTag("sound_${i.key}")) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(i.name, fontWeight = FontWeight.Bold, fontSize = 15.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text("${familyLabel(i.family)} · ${i.packName} · ${originLabel(i.origin)}", color = Palette.TextDim, fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            val fav = i.key in sounds.favorites
            ToolButton(if (fav) Icons.Default.Star else Icons.Default.StarBorder, if (fav) tr("Quitar de favoritos", "Remove from favorites") else tr("Añadir a favoritos", "Add to favorites"),
                { actions.toggleFavoriteSound(i.key) }, active = fav, activeColor = Palette.Solo)
            ToolButton(if (playing) Icons.Default.Stop else Icons.Default.PlayArrow, if (playing) tr("Parar", "Stop") else tr("Escuchar", "Preview"),
                { if (playing) actions.stopSoundPreview() else actions.previewSound(i.key) }, active = playing, activeColor = Palette.Cyan)
        }
        if (i.description.isNotEmpty()) Text(i.description, color = Palette.TextDim, fontSize = 12.sp, maxLines = 2, overflow = TextOverflow.Ellipsis)
        if (i.tags.isNotEmpty()) FlowRow(Modifier.padding(top = 4.dp), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            i.tags.take(5).forEach { t -> Text(t, fontSize = 10.sp, color = Palette.TextDim, modifier = Modifier.clip12().background(Palette.SurfaceTop).padding(horizontal = 6.dp, vertical = 2.dp)) }
        }
        if (canLoad) {
            Spacer(Modifier.width(4.dp))
            CauceButton(if (loaded) tr("Cargado en la pista", "Loaded on track") else tr("Usar en «${track!!.name}»", "Use on “${track!!.name}”"),
                { if (!loaded) actions.loadSound(track.id, i.key) }, Modifier.fillMaxWidth().padding(top = 6.dp), kind = if (loaded) ButtonKind.Ghost else ButtonKind.Primary,
                enabled = !loaded, height = 40.dp)
        }
    }
}

@Composable
private fun PackList(sounds: SoundUi, actions: StudioActions, onImportPack: () -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        CauceButton(if (sounds.importing) tr("Importando…", "Importing…") else tr("Importar paquete (.zip)", "Import pack (.zip)"), onImportPack,
            Modifier.fillMaxWidth().testTag("import_pack"), icon = Icons.Default.FileUpload, enabled = !sounds.importing)
        Text(tr("Solo paquetes Cauce con manifiesto, licencia y huellas SHA-256. Se comprueba todo antes de instalar; nada se descarga de Internet.",
            "Only Cauce packs with manifest, license and SHA-256 hashes. Everything is checked before installing; nothing is downloaded."), fontSize = 11.sp, color = Palette.TextFaint)
        LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp)) {
            items(sounds.packs, key = { it.id }) { p -> PackRow(p, actions) }
        }
    }
}

@Composable
private fun PackRow(p: PackInfo, actions: StudioActions) {
    Column(Modifier.fillMaxWidth().clip12().background(Palette.SurfaceHigh).padding(10.dp).testTag("pack_${p.id}")) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text("${p.name} ${p.version}", fontWeight = FontWeight.Bold, fontSize = 14.sp)
                Text("${SoundBrowserFilter.sizeLabel(p.bytes)} · ${p.instruments.size} ${tr("sonidos", "sounds")} · ${p.license} · " +
                    if (p.factory) tr("incluido", "built-in") else tr("importado", "imported"), color = Palette.TextDim, fontSize = 11.sp)
            }
            if (!p.factory) ToolButton(Icons.Default.Delete, tr("Borrar paquete", "Delete pack"), { actions.askRemovePack(p.id) }, tint = Palette.Record)
        }
        if (!p.complete) Row(verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Default.Warning, null, tint = Palette.Warn, modifier = Modifier.padding(end = 4.dp))
            Text(tr("Faltan ${p.problems.size} archivos: algunos sonidos no sonarán.", "${p.problems.size} files missing: some sounds will be silent."), color = Palette.Warn, fontSize = 12.sp)
        }
        p.sources.forEach { s -> Text("${s.name} — ${s.author} (${s.license})", color = Palette.TextFaint, fontSize = 10.sp, maxLines = 2, overflow = TextOverflow.Ellipsis) }
    }
}

/** Aviso de sonidos que faltan: el proyecto se conserva y esas pistas quedan mudas, nunca con otro sonido. */
@Composable
fun MissingSoundsBanner(missing: List<String>) {
    Row(Modifier.fillMaxWidth().clip12().background(Palette.Warn.copy(alpha = 0.14f)).padding(10.dp).testTag("missing_sounds"), verticalAlignment = Alignment.CenterVertically) {
        Icon(Icons.Default.Warning, null, tint = Palette.Warn)
        Spacer(Modifier.width(8.dp))
        Text(tr("Faltan sonidos de este proyecto: ${missing.joinToString()}. Esas pistas están en silencio; sus notas y ajustes se conservan. Importa el paquete para recuperarlos.",
            "This project's sounds are missing: ${missing.joinToString()}. Those tracks are silent; notes and settings are kept. Import the pack to restore them."),
            fontSize = 12.sp, color = Palette.Text)
    }
}

/** Confirmación de borrado: avisa de los proyectos guardados que usan el paquete. */
@Composable
fun RemovePackDialog(ask: Pair<PackInfo, List<String>>, onDismiss: () -> Unit, onConfirm: () -> Unit) {
    val (p, projects) = ask
    CauceDialog(tr("¿Borrar ${p.name}?", "Delete ${p.name}?"), onDismiss, confirm = tr("Borrar", "Delete"), onConfirm = onConfirm, dismiss = tr("Cancelar", "Cancel"), danger = true) {
        Text(if (projects.isEmpty()) tr("Ningún proyecto guardado lo usa. Libera ${SoundBrowserFilter.sizeLabel(p.bytes)}.", "No saved project uses it. Frees ${SoundBrowserFilter.sizeLabel(p.bytes)}.")
            else tr("Lo usan ${projects.size} proyectos: ${projects.joinToString()}. Esas pistas quedarán en silencio (sin perder notas ni ajustes) hasta que lo vuelvas a importar.",
                "Used by ${projects.size} projects: ${projects.joinToString()}. Those tracks will be silent (notes and settings kept) until you import it again."))
    }
}

private fun Modifier.clip12() = this.clip(RoundedCornerShape(12.dp))

/** AutoMix (prototipo por reglas): lista lo que cambiaría y por qué; no toca nada hasta Aplicar. */
@Composable
fun AutoMixDialog(ui: com.caucesound.app.data.AutoMixUi, onApply: () -> Unit, onDismiss: () -> Unit) {
    val changes = ui.suggestions.filter { it.change }
    CauceDialog("AutoMix", onDismiss, confirm = if (changes.isEmpty()) null else tr("Aplicar ${changes.size} cambios", "Apply ${changes.size} changes"), onConfirm = onApply,
        dismiss = tr("Cerrar", "Close")) {
        if (ui.analyzing) { Text(tr("Escuchando cada pista por separado…", "Listening to each track…")); return@CauceDialog }
        Text(tr("Reglas fijas por papel y estilo (no es IA). Mueve faders, el volumen de la caja y el máster si la mezcla recortaría. Después puedes comparar con la mezcla anterior y revertir de una vez.",
            "Fixed rules per role and style (not AI). Moves faders, the snare level and the master if the mix would clip. Afterwards you can compare with the previous mix and revert at once."),
            fontSize = 12.sp, color = Palette.TextDim)
        Spacer(Modifier.width(4.dp))
        if (changes.isEmpty()) Text(tr("La mezcla ya está equilibrada según las reglas: no hay nada que cambiar.", "The mix is already balanced by the rules: nothing to change."), modifier = Modifier.testTag("automix_none"))
        ui.suggestions.forEach { s ->
            Row(Modifier.fillMaxWidth().padding(vertical = 3.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(s.name, fontWeight = FontWeight.Bold, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text(s.reason, fontSize = 11.sp, color = Palette.TextDim, maxLines = 4)
                }
                Text(if (s.change) "%+.1f → %+.1f dB".format(s.volumeDb, s.suggestedVolumeDb) else "—", fontSize = 12.sp, color = if (s.change) Palette.Cyan else Palette.TextFaint)
            }
        }
    }
}
