package com.caucesound.app.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.FolderZip
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.caucesound.app.data.DawExportUi
import kotlin.math.roundToInt

/**
 * Opciones de exportación (misión 0.6). Tres salidas, todas con archivos estándar:
 *  - la mezcla en un WAV;
 *  - un paquete para DAW (ZIP con un WAV por pista, el MIDI, la mezcla, el proyecto y un manifiesto);
 *  - solo el MIDI.
 * No es un formato nativo de ningún DAW, y la pantalla lo dice.
 */
@Composable
fun ExportOptions(hasMidi: Boolean, onMix: (bits: Int) -> Unit, onDaw: (wet: Boolean) -> Unit, onMidi: () -> Unit, modifier: Modifier = Modifier) {
    var bits by remember { mutableIntStateOf(1) }
    var mode by remember { mutableIntStateOf(0) }
    Column(modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(Space.md)) {
        SectionCard(tr("Mezcla (un archivo WAV)", "Mix (one WAV file)")) { Column(verticalArrangement = Arrangement.spacedBy(Space.sm)) {
            Text(tr("La canción tal como suena, para compartirla o subirla.", "The song as it sounds, to share or upload."),
                style = MaterialTheme.typography.bodySmall, color = Palette.TextDim)
            Segmented(listOf("16 bit", "24 bit", "32 float"), bits, { bits = it }, Modifier.fillMaxWidth().testTag("export_bits"))
            CauceButton(tr("Exportar mezcla", "Export mix"), { onMix(listOf(16, 24, 32)[bits]) }, Modifier.fillMaxWidth().testTag("export_mix"),
                icon = Icons.Default.FileDownload)
        } }
        SectionCard(tr("Paquete para DAW (ZIP)", "DAW package (ZIP)")) { Column(verticalArrangement = Arrangement.spacedBy(Space.sm)) {
            Text(tr("Para seguir en FL Studio, Reaper, Cubase u otro programa: un WAV por pista (todos empiezan en el compás 1 y duran lo mismo), el MIDI, la mezcla y el proyecto original.",
                "To continue in FL Studio, Reaper, Cubase or another program: one WAV per track (all start at bar 1 and have the same length), the MIDI, the mix and the original project."),
                style = MaterialTheme.typography.bodySmall, color = Palette.TextDim)
            Segmented(listOf(tr("Con efectos", "With effects"), tr("Sin efectos", "No effects")), mode, { mode = it }, Modifier.fillMaxWidth().testTag("export_daw_mode"))
            Text(if (mode == 0) tr("Cada pista sale con sus efectos, volumen y panorama. La reverb compartida sale en un archivo aparte («retorno»). Juntas suenan igual que la mezcla.",
                    "Each track is exported with its effects, volume and pan. The shared reverb is a separate file (\"return\"). Together they sound like the mix.")
                else tr("Cada pista sale limpia, sin efectos, a 0 dB y centrada, para mezclar desde cero en el otro programa.",
                    "Each track is exported clean, with no effects, at 0 dB and centred, to mix from scratch in the other program."),
                style = MaterialTheme.typography.bodySmall, modifier = Modifier.testTag("export_daw_hint"))
            CauceButton(tr("Crear paquete", "Create package"), { onDaw(mode == 0) }, Modifier.fillMaxWidth().testTag("export_daw"),
                kind = ButtonKind.Primary, icon = Icons.Default.FolderZip)
            Text(tr("WAV de 24 bits. No es un proyecto de FL Studio: los instrumentos y efectos de CAUCESOUND no se convierten en plugins.",
                "24-bit WAV. It is not an FL Studio project: CAUCESOUND instruments and effects are not converted to plugins."),
                style = MaterialTheme.typography.labelSmall, color = Palette.TextFaint)
        } }
        SectionCard(tr("Solo MIDI (.mid)", "MIDI only (.mid)")) { Column(verticalArrangement = Arrangement.spacedBy(Space.sm)) {
            Text(if (hasMidi) tr("Las notas de la batería y los instrumentos, una pista por instrumento.", "The notes of the drums and instruments, one track per instrument.")
                else tr("Esta canción no tiene notas que exportar.", "This song has no notes to export."),
                style = MaterialTheme.typography.bodySmall, color = Palette.TextDim)
            CauceButton(tr("Exportar MIDI", "Export MIDI"), onMidi, Modifier.fillMaxWidth().testTag("export_midi"), icon = Icons.Default.MusicNote, enabled = hasMidi)
        } }
    }
}

/**
 * Estado de la exportación para DAW: progreso real del motor, cancelar siempre visible, y un final claro.
 * Nunca muestra «terminado» mientras se sigue trabajando: «saving» es la copia a la carpeta elegida.
 */
@Composable
fun DawExportStatus(export: DawExportUi, onCancel: () -> Unit, onClose: () -> Unit, modifier: Modifier = Modifier) {
    Column(modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(Space.md)) {
        when (export.phase) {
            "running" -> {
                val pct = (export.progress * 100).roundToInt()
                Text(tr("Renderizando pistas… $pct %", "Rendering tracks… $pct %"), fontWeight = FontWeight.SemiBold, modifier = Modifier.testTag("export_progress"))
                LinearProgressIndicator(progress = { export.progress.coerceIn(0f, 1f) }, modifier = Modifier.fillMaxWidth(), color = Palette.Violet, trackColor = Palette.SurfaceTop)
                Text(tr("Puedes seguir escuchando la canción. No cierres la app hasta que termine.", "You can keep listening to the song. Do not close the app until it finishes."),
                    style = MaterialTheme.typography.bodySmall, color = Palette.TextDim)
                CauceButton(tr("Cancelar", "Cancel"), onCancel, Modifier.fillMaxWidth().testTag("export_cancel"), icon = Icons.Default.Close)
            }
            "saving" -> {
                Text(tr("Guardando el paquete en la carpeta elegida…", "Saving the package to the chosen folder…"), fontWeight = FontWeight.SemiBold, modifier = Modifier.testTag("export_saving"))
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth(), color = Palette.Violet, trackColor = Palette.SurfaceTop)
            }
            "done" -> {
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(Space.sm)) {
                    Icon(Icons.Default.Check, null, Modifier.size(24.dp), tint = Palette.Ok)
                    Text(tr("Paquete guardado", "Package saved"), style = MaterialTheme.typography.titleMedium, modifier = Modifier.testTag("export_done"))
                }
                Text(tr("${export.stems} pistas de audio (${if (export.wet) "con efectos" else "sin efectos"}), ${export.midiNotes} notas MIDI, ${export.files} archivos, %.1f MB.".format(export.bytes / 1048576.0),
                    "${export.stems} audio stems (${if (export.wet) "with effects" else "no effects"}), ${export.midiNotes} MIDI notes, ${export.files} files, %.1f MB.".format(export.bytes / 1048576.0)),
                    style = MaterialTheme.typography.bodyMedium)
                if (export.clipped > 0) Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(Space.sm)) {
                    Icon(Icons.Default.Warning, null, Modifier.size(20.dp), tint = Palette.Warn)
                    Text(tr("${export.clipped} muestras superaban el máximo y se recortaron: baja el volumen de la pista más fuerte y exporta otra vez.",
                        "${export.clipped} samples were over the maximum and were clipped: lower the loudest track and export again."),
                        style = MaterialTheme.typography.bodySmall, modifier = Modifier.testTag("export_clipped"))
                }
                Text(tr("Dentro del ZIP, README.txt explica cómo importarlo. El proyecto no ha cambiado.", "Inside the ZIP, README.txt explains how to import it. The project has not changed."),
                    style = MaterialTheme.typography.bodySmall, color = Palette.TextDim)
                CauceButton(tr("Listo", "Done"), onClose, Modifier.fillMaxWidth().testTag("export_close"), kind = ButtonKind.Primary)
            }
            else -> {
                val cancelled = export.phase == "cancelled"
                Text(if (cancelled) tr("Exportación cancelada", "Export cancelled") else tr("No se pudo exportar", "Export failed"),
                    style = MaterialTheme.typography.titleMedium, modifier = Modifier.testTag("export_failed"))
                Text(if (cancelled) tr("No se ha guardado ningún archivo. El proyecto no ha cambiado.", "No file was saved. The project has not changed.")
                    else (export.error ?: tr("Error desconocido", "Unknown error")), style = MaterialTheme.typography.bodySmall, color = Palette.TextDim)
                CauceButton(tr("Cerrar", "Close"), onClose, Modifier.fillMaxWidth().testTag("export_close"))
            }
        }
    }
}
