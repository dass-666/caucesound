package com.caucesound.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.FiberManualRecord
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material3.Icon
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.data.TrackState

/**
 * Grabación con un toque (hotfix 0.9.2). Aparece al pulsar el botón rojo cuando no hay ninguna pista de audio armada:
 * en vez de pedir «arma una pista», ofrece crear la pista de voz y grabar ya (como «Idea rápida»), o grabar en una pista
 * de audio que ya exista. «Sin acompañamiento» calla el beat y el metrónomo solo mientras dura la toma; la mezcla y el
 * proyecto no cambian. Con una pista ya armada el botón rojo graba directamente (modo multipista de siempre).
 */
@Composable
fun QuickRecordDialog(audioTracks: List<TrackState>, solo: Boolean, onDismiss: () -> Unit, onRecord: (trackId: String?, solo: Boolean) -> Unit) {
    var withoutBacking by remember { mutableStateOf(solo) }
    CauceDialog(tr("Grabar voz", "Record voice"), onDismiss, confirm = null, dismiss = tr("Cancelar", "Cancel")) {
        Column(Modifier.testTag("quick_record"), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            CauceButton(tr("Crear pista de voz y grabar", "Create a voice track and record"), { onRecord(null, withoutBacking) },
                Modifier.fillMaxWidth().testTag("quick_record_new"), kind = ButtonKind.Primary, icon = Icons.Default.FiberManualRecord)
            if (audioTracks.isNotEmpty()) {
                Text(tr("O grabar en una pista que ya tienes:", "Or record on a track you already have:"), fontSize = 12.sp, color = Palette.TextDim)
                audioTracks.forEach { t ->
                    Row(
                        Modifier.fillMaxWidth().heightIn(min = 48.dp).clip(RoundedCornerShape(Dims.RadiusMedium)).background(Palette.Surface)
                            .clickable { onRecord(t.id, withoutBacking) }.padding(horizontal = 12.dp).testTag("quick_record_${t.id}"),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Icon(Icons.Default.Mic, null, tint = Palette.Record)
                        Spacer(Modifier.width(8.dp))
                        Text(t.name, fontWeight = FontWeight.SemiBold, modifier = Modifier.weight(1f))
                        Text(if (t.clips.isEmpty()) tr("vacía", "empty") else tr("${t.clips.size} toma(s)", "${t.clips.size} take(s)"), fontSize = 11.sp, color = Palette.TextFaint)
                    }
                }
            }
            Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(tr("Sin acompañamiento", "Without backing"), fontWeight = FontWeight.SemiBold)
                    Text(tr("Silencia el beat y el metrónomo solo durante la toma (la cuenta previa sí suena). No cambia tu mezcla.",
                        "Mutes the beat and click only during the take (the count-in still plays). Your mix is unchanged."), fontSize = 11.sp, color = Palette.TextDim)
                }
                Switch(withoutBacking, { withoutBacking = it }, modifier = Modifier.testTag("quick_record_solo"))
            }
            Text(tr("Para grabar varias pistas: arma (REC) la que quieras y el botón rojo grabará en ella directamente.",
                "To record multiple tracks: arm (REC) the one you want and the red button records on it directly."), fontSize = 11.sp, color = Palette.TextFaint)
        }
    }
}
