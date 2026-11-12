package com.caucesound.app.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.TrackState

/** Mezclador: una tira por pista (medidor, fader, panorámica, mute, solo) y el máster. */
@Composable
fun MixerPanel(state: ProjectState, meters: State<FloatArray>, selectedTrack: String?, actions: StudioActions) {
    Row(Modifier.fillMaxSize().horizontalScroll(rememberScrollState()).padding(horizontal = 8.dp, vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        state.tracks.forEachIndexed { i, t ->
            Strip(t, { meters.value.getOrElse(i + 1) { 0f } }, t.id == selectedTrack, actions)
        }
        MasterStrip(state.masterDb, { meters.value.getOrElse(0) { 0f } }, actions)
    }
}

private fun kindLabel(t: TrackState) = when (t.kind) { "drums" -> "DRUMS"; "synth" -> "SYNTH"; else -> "AUDIO" }

@Composable
private fun Strip(t: TrackState, meter: () -> Float, selected: Boolean, actions: StudioActions) {
    var vol by remember(t.id, t.volumeDb) { mutableFloatStateOf(t.volumeDb.clampDb()) }
    var pan by remember(t.id, t.pan) { mutableFloatStateOf(t.pan) }
    val color = Color(t.color.toInt())
    Column(
        Modifier.width(92.dp).fillMaxHeight().clip(RoundedCornerShape(16.dp)).background(Palette.Surface)
            .border(if (selected) 2.dp else 1.dp, if (selected) color else Palette.Grid, RoundedCornerShape(16.dp))
            .clickable { actions.selectTrack(t.id) }.padding(8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.fillMaxWidth().height(4.dp).background(color, RoundedCornerShape(2.dp)))
        Spacer(Modifier.height(6.dp))
        Text(t.name, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(kindLabel(t), fontSize = 9.sp, color = Palette.TextFaint, letterSpacing = 1.sp)
        Spacer(Modifier.height(4.dp))
        Knob("Pan", pan, -1f, 1f, 0f, { pan = it; actions.setPanLive(t.id, it) }, onEnd = actions::commitMixer, size = 40.dp, color = Palette.Solo,
            format = { if (kotlin.math.abs(it) < 0.02f) "C" else if (it < 0) "L${(-it * 100).toInt()}" else "R${(it * 100).toInt()}" })
        Row(Modifier.weight(1f).padding(vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            VerticalFader(vol, { vol = it; actions.setVolumeLive(t.id, it) }, actions::commitMixer, Modifier.width(40.dp).fillMaxHeight(), color = color)
            LevelMeter(meter, Modifier.width(10.dp).fillMaxHeight())
        }
        Text(dbLabel(vol), fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = Palette.TextDim)
        Spacer(Modifier.height(6.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            PillToggle("M", t.mute, { actions.setMute(t.id, !t.mute) }, color = Palette.Mute, height = 36.dp, minWidth = 36.dp)
            PillToggle("S", t.solo, { actions.setSolo(t.id, !t.solo) }, color = Palette.Solo, height = 36.dp, minWidth = 36.dp)
        }
        if (t.isAudio) {
            Spacer(Modifier.height(4.dp))
            PillToggle("REC", t.armed, { actions.setArmed(t.id, !t.armed) }, color = Palette.Record, height = 32.dp, minWidth = 76.dp)
        } else Spacer(Modifier.height(36.dp)) // misma altura de fader en todas las tiras
    }
}

@Composable
private fun MasterStrip(masterDb: Float, meter: () -> Float, actions: StudioActions) {
    var vol by remember(masterDb) { mutableFloatStateOf(masterDb.clampDb()) }
    Column(
        Modifier.width(100.dp).fillMaxHeight().clip(RoundedCornerShape(16.dp)).background(Palette.SurfaceHigh)
            .border(1.dp, Palette.Violet.copy(alpha = 0.5f), RoundedCornerShape(16.dp)).padding(8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text("MASTER", fontSize = 12.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
        Spacer(Modifier.height(46.dp))
        Row(Modifier.weight(1f).padding(vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            VerticalFader(vol, { vol = it; actions.setMasterLive(it) }, actions::commitMixer, Modifier.width(40.dp).fillMaxHeight(), color = Palette.Violet)
            LevelMeter(meter, Modifier.width(14.dp).fillMaxHeight())
        }
        Text(dbLabel(vol), fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = Palette.TextDim)
        Spacer(Modifier.height(46.dp))
    }
}
