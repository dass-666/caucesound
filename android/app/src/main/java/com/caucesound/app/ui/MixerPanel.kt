package com.caucesound.app.ui

import androidx.compose.material.icons.filled.KeyboardArrowDown
import androidx.compose.material.icons.filled.KeyboardArrowUp
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
import androidx.compose.foundation.layout.size
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
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.platform.testTag
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.TrackState

/**
 * Mixer Pro (0.7): arriba las tiras (una por canal: medidor real, fader, pan, mute, solo, enrutado) y debajo, o a la
 * derecha en horizontal, el rack de inserciones del canal seleccionado. Seleccionar una tira abre su procesamiento
 * sin cambiar de pantalla.
 */
@Composable
fun MixerPanel(state: ProjectState, meters: State<FloatArray>, selectedTrack: String?, actions: StudioActions, catalog: com.caucesound.app.data.Catalog = com.caucesound.app.data.Catalog.EMPTY) {
    val short = androidx.compose.ui.platform.LocalConfiguration.current.screenHeightDp < 500
    val sel = state.tracks.firstOrNull { it.id == selectedTrack } ?: state.tracks.firstOrNull()
    val selIndex = state.tracks.indexOfFirst { it.id == sel?.id }
    // 0.8: en vertical los efectos empiezan CERRADOS y los faders ocupan toda la altura. Se abren con el botón «FX» de
    // cualquier tira o con la barra inferior, que siempre está a la vista y dice qué canal se está editando.
    // 0.9.2: tocar una tira (no solo su botón FX) selecciona el canal Y abre su cadena de efectos, como en los DAW de
    // escritorio: elegir canal → ver sus plugins → «+ Agregar plugin».
    var fxOpen by androidx.compose.runtime.saveable.rememberSaveable { androidx.compose.runtime.mutableStateOf(false) }
    val openFx: (String) -> Unit = { id -> actions.selectTrack(id); fxOpen = true }
    if (short) Row(Modifier.fillMaxSize()) {
        // En horizontal hay anchura de sobra: faders a la izquierda y efectos a la derecha, siempre a la vez.
        Box(Modifier.weight(0.58f).fillMaxHeight()) { MixerStrips(state, meters, sel?.id, selIndex, actions, true, openFx) }
        Box(Modifier.width(1.dp).fillMaxHeight().background(Palette.Grid))
        if (sel != null) InsertRack(state, sel, catalog, actions, Modifier.weight(0.42f).fillMaxHeight())
    } else Column(Modifier.fillMaxSize()) {
        Box(Modifier.weight(if (fxOpen && sel != null) 0.5f else 1f).fillMaxWidth()) { MixerStrips(state, meters, sel?.id, selIndex, actions, false, openFx) }
        if (sel != null) {
            FxBar(sel, fxOpen) { fxOpen = !fxOpen }
            if (fxOpen) Column(Modifier.weight(0.5f).fillMaxWidth()) {
                // El volumen del canal sigue a mano mientras se editan sus efectos (no hay que cerrar el rack para bajarlo).
                SelectedChannelVolume(sel, { meters.value.getOrElse(selIndex + 1) { 0f } }, actions)
                InsertRack(state, sel, catalog, actions, Modifier.weight(1f).fillMaxWidth())
            }
        }
    }
}

/** Barra fija entre los faders y los efectos: canal seleccionado, número de efectos y un botón grande para abrir/cerrar. */
@Composable
private fun FxBar(sel: TrackState, open: Boolean, toggle: () -> Unit) {
    val active = sel.inserts.count { !it.bypass }
    Row(Modifier.fillMaxWidth().height(52.dp).background(Palette.SurfaceHigh).clickable(onClick = toggle).padding(horizontal = 12.dp).testTag("rack_toggle"),
        verticalAlignment = Alignment.CenterVertically) {
        Box(Modifier.size(10.dp).clip(androidx.compose.foundation.shape.CircleShape).background(Color(sel.color.toInt())))
        Spacer(Modifier.width(8.dp))
        Column(Modifier.weight(1f)) {
            Text(sel.name, fontSize = 13.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(if (sel.inserts.isEmpty()) tr("Sin efectos · toca para agregar un plugin", "No effects · tap to add a plugin")
                else tr("$active de ${sel.inserts.size} efectos activos", "$active of ${sel.inserts.size} effects on"),
                fontSize = 11.sp, color = Palette.TextDim)
        }
        Row(Modifier.clip(RoundedCornerShape(12.dp)).background(if (open) Palette.Surface else Palette.Violet).padding(horizontal = 14.dp, vertical = 8.dp),
            verticalAlignment = Alignment.CenterVertically) {
            Text(if (open) tr("Ocultar efectos", "Hide effects") else tr("Efectos / plugins", "Effects / plugins"), fontSize = 13.sp, fontWeight = FontWeight.SemiBold,
                color = if (open) Palette.Text else Color.White)
            androidx.compose.material3.Icon(if (open) androidx.compose.material.icons.Icons.Default.KeyboardArrowDown else androidx.compose.material.icons.Icons.Default.KeyboardArrowUp, null,
                tint = if (open) Palette.Text else Color.White)
        }
    }
}

/** Volumen, medidor, M y S del canal seleccionado, en horizontal, encima de sus efectos. */
@Composable
private fun SelectedChannelVolume(t: TrackState, meter: () -> Float, actions: StudioActions) {
    var vol by remember(t.id, t.volumeDb) { mutableFloatStateOf(t.volumeDb.clampDb()) }
    Column(Modifier.fillMaxWidth().background(Palette.Surface).padding(horizontal = 12.dp, vertical = 4.dp).testTag("fx_volume")) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(tr("Volumen", "Volume"), fontSize = 12.sp, color = Palette.TextDim, modifier = Modifier.width(64.dp))
            androidx.compose.material3.Slider(vol, { vol = it; actions.setVolumeLive(t.id, it) }, Modifier.weight(1f).testTag("fx_volume_slider"),
                valueRange = -60f..6f, onValueChangeFinished = actions::commitMixer,
                colors = androidx.compose.material3.SliderDefaults.colors(thumbColor = Palette.Text, activeTrackColor = Color(t.color.toInt()), inactiveTrackColor = Palette.SurfaceTop))
            Text(dbLabel(vol), fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = Palette.TextDim, modifier = Modifier.width(52.dp))
            PillToggle("M", t.mute, { actions.setMute(t.id, !t.mute) }, color = Palette.Mute, height = 36.dp, minWidth = 36.dp)
            Spacer(Modifier.width(4.dp))
            PillToggle("S", t.solo, { actions.setSolo(t.id, !t.solo) }, color = Palette.Solo, height = 36.dp, minWidth = 36.dp)
        }
        LevelMeter(meter, Modifier.fillMaxWidth().height(4.dp), vertical = false)
    }
}

@Composable
private fun MixerStrips(state: ProjectState, meters: State<FloatArray>, selectedTrack: String?, selIndex: Int, actions: StudioActions, short: Boolean, openFx: (String) -> Unit) {
    val scroll = rememberScrollState()
    val density = androidx.compose.ui.platform.LocalDensity.current
    // 0.8: al elegir un canal desde otra pantalla, el Mixer se desplaza hasta su tira (antes había que buscarla).
    androidx.compose.runtime.LaunchedEffect(selIndex, short) {
        if (selIndex < 0) return@LaunchedEffect
        val stripPx = with(density) { ((if (short) 176 else 92) + 8).dp.toPx() }
        val target = (selIndex * stripPx - with(density) { 24.dp.toPx() }).toInt().coerceIn(0, scroll.maxValue.coerceAtLeast(0))
        if (target < scroll.value || target + stripPx > scroll.value + scroll.viewportSize) scroll.animateScrollTo(target)
    }
    Row(Modifier.fillMaxSize().horizontalScroll(scroll).padding(horizontal = 8.dp, vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
        // Primer bus (reverb compartida): las pistas normales le envían con la perilla «Send».
        val bus = state.tracks.firstOrNull { it.isBus && !isLaneChannel(state, it) }
        state.tracks.forEachIndexed { i, t ->
            Strip(t, { meters.value.getOrElse(i + 1) { 0f } }, t.id == selectedTrack, state.tracks.firstOrNull { b -> b.isBus && !isLaneChannel(state, b) }, actions, short, stripRoute(state, t), openFx)
        }
        if (bus == null) {
            Column(
                Modifier.width(92.dp).fillMaxHeight().clip(RoundedCornerShape(16.dp)).border(1.dp, Palette.Grid, RoundedCornerShape(16.dp))
                    .clickable(onClick = actions::addReverbBus).padding(8.dp),
                horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.Center,
            ) {
                Text("+", fontSize = 28.sp, color = Palette.Violet)
                Text(tr("Bus de reverb", "Reverb bus"), fontSize = 12.sp, fontWeight = FontWeight.SemiBold, color = Palette.TextDim)
                Spacer(Modifier.height(4.dp))
                Text(tr("Una reverb compartida: menos CPU que una por pista", "One shared reverb: less CPU than one per track"),
                    fontSize = 10.sp, color = Palette.TextFaint)
            }
        }
        MasterStrip(state.masterDb, { meters.value.getOrElse(0) { 0f } }, actions, short)
    }
}

internal fun kindLabel(t: TrackState) = when (t.kind) { "drums" -> "DRUMS"; "synth" -> "SYNTH"; "bus" -> "BUS"; else -> "AUDIO" }

/** Bus que es el canal propio de una pieza de batería (Channel Rack). */
fun isLaneChannel(state: ProjectState, t: TrackState) = t.isBus && state.tracks.any { it.laneRoutes.values.contains(t.id) }

/** Enrutado en una línea para la tira: de dónde viene (canal de pieza) o a dónde va. */
private fun stripRoute(state: ProjectState, t: TrackState): String {
    state.tracks.firstOrNull { it.laneRoutes.values.contains(t.id) }?.let { return "← ${it.name}" }
    val busSend = t.sends.entries.firstOrNull { it.value > 0f }?.let { e -> state.tracks.firstOrNull { it.id == e.key }?.name }
    return if (busSend != null) "→ M · $busSend" else "→ Master"
}

@Composable
private fun Strip(t: TrackState, meter: () -> Float, selected: Boolean, bus: TrackState?, actions: StudioActions, short: Boolean = false, route: String = "", openFx: (String) -> Unit = { actions.selectTrack(it) }) {
    var vol by remember(t.id, t.volumeDb) { mutableFloatStateOf(t.volumeDb.clampDb()) }
    var pan by remember(t.id, t.pan) { mutableFloatStateOf(t.pan) }
    val sendLevel = bus?.let { t.sends[it.id] } ?: 0f
    var send by remember(t.id, bus?.id, sendLevel) { mutableFloatStateOf(sendLevel) }
    val color = Color(t.color.toInt())
    val panKnob: @Composable () -> Unit = {
        Knob("Pan", pan, -1f, 1f, 0f, { pan = it; actions.setPanLive(t.id, it) }, onEnd = actions::commitMixer, size = 40.dp, color = Palette.Solo,
            format = { if (kotlin.math.abs(it) < 0.02f) "C" else if (it < 0) "L${(-it * 100).toInt()}" else "R${(it * 100).toInt()}" })
    }
    val sendKnob: @Composable () -> Unit = {
        if (bus != null && !t.isBus) Knob("Send", send, 0f, 1f, 0f, { send = it; actions.setSendLive(t.id, bus.id, it) }, onEnd = actions::commitMixer, size = 40.dp,
            color = Palette.Violet, format = { "${(it * 100).toInt()}%" })
    }
    val fxPill: @Composable () -> Unit = {
        PillToggle(if (t.inserts.isEmpty()) "FX" else "FX ${t.inserts.count { !it.bypass }}", t.inserts.any { !it.bypass },
            { openFx(t.id) }, color = Palette.Violet, height = 36.dp, minWidth = 60.dp)
    }
    if (short) {
        Column(
            Modifier.width(176.dp).fillMaxHeight().clip(RoundedCornerShape(16.dp)).background(Palette.Surface)
                .border(if (selected) 2.dp else 1.dp, if (selected) color else Palette.Grid, RoundedCornerShape(16.dp))
                .clickable { openFx(t.id) }.padding(8.dp).testTag("strip_${t.id}"),
        ) {
            Box(Modifier.fillMaxWidth().height(4.dp).background(color, RoundedCornerShape(2.dp)))
            Row(Modifier.fillMaxWidth().padding(top = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(t.name, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.weight(1f))
                Text(dbLabel(vol), fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = Palette.TextDim)
            }
            Row(Modifier.weight(1f).padding(vertical = 4.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Column(Modifier.weight(1f).fillMaxHeight(), horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.SpaceEvenly) {
                    fxPill(); panKnob(); sendKnob()
                }
                VerticalFader(vol, { vol = it; actions.setVolumeLive(t.id, it) }, actions::commitMixer, Modifier.width(40.dp).fillMaxHeight(), color = color)
                LevelMeter(meter, Modifier.width(10.dp).fillMaxHeight())
            }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                PillToggle("M", t.mute, { actions.setMute(t.id, !t.mute) }, color = Palette.Mute, height = 36.dp, minWidth = 36.dp)
                PillToggle("S", t.solo, { actions.setSolo(t.id, !t.solo) }, color = Palette.Solo, height = 36.dp, minWidth = 36.dp)
                if (t.isAudio) PillToggle("REC", t.armed, { actions.setArmed(t.id, !t.armed) }, color = Palette.Record, height = 36.dp, minWidth = 56.dp)
            }
        }
        return
    }
    Column(
        Modifier.width(92.dp).fillMaxHeight().clip(RoundedCornerShape(16.dp)).background(Palette.Surface)
            .border(if (selected) 2.dp else 1.dp, if (selected) color else Palette.Grid, RoundedCornerShape(16.dp))
            .clickable { openFx(t.id) }.padding(8.dp).testTag("strip_${t.id}"),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Box(Modifier.fillMaxWidth().height(4.dp).background(color, RoundedCornerShape(2.dp)))
        Spacer(Modifier.height(6.dp))
        Text(t.name, fontSize = 12.sp, fontWeight = FontWeight.SemiBold, maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(kindLabel(t), fontSize = 9.sp, color = Palette.TextFaint, letterSpacing = 1.sp)
        Text(route, fontSize = 9.sp, color = Palette.TextDim, maxLines = 1, overflow = TextOverflow.Ellipsis, modifier = Modifier.testTag("strip_route_${t.id}"))
        Spacer(Modifier.height(4.dp))
        Box(Modifier.testTag("strip_fx_${t.id}")) { fxPill() }
        Spacer(Modifier.height(4.dp))
        // 0.7: el envío al bus pasó al rack del canal (debajo), para dejar más recorrido al fader.
        Knob("Pan", pan, -1f, 1f, 0f, { pan = it; actions.setPanLive(t.id, it) }, onEnd = actions::commitMixer, size = 34.dp, color = Palette.Solo,
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
            PillToggle("REC", t.armed, { actions.setArmed(t.id, !t.armed) }, color = Palette.Record, height = 28.dp, minWidth = 76.dp)
        } else Spacer(Modifier.height(32.dp)) // misma altura de fader en todas las tiras
    }
}

@Composable
private fun MasterStrip(masterDb: Float, meter: () -> Float, actions: StudioActions, short: Boolean = false) {
    var vol by remember(masterDb) { mutableFloatStateOf(masterDb.clampDb()) }
    Column(
        Modifier.width(100.dp).fillMaxHeight().clip(RoundedCornerShape(16.dp)).background(Palette.SurfaceHigh)
            .border(1.dp, Palette.Violet.copy(alpha = 0.5f), RoundedCornerShape(16.dp)).padding(8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Text("MASTER", fontSize = 12.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.sp)
        Spacer(Modifier.height(if (short) 4.dp else 60.dp))
        Row(Modifier.weight(1f).padding(vertical = 6.dp), horizontalArrangement = Arrangement.spacedBy(6.dp)) {
            VerticalFader(vol, { vol = it; actions.setMasterLive(it) }, actions::commitMixer, Modifier.width(40.dp).fillMaxHeight(), color = Palette.Violet)
            LevelMeter(meter, Modifier.width(14.dp).fillMaxHeight())
        }
        Text(dbLabel(vol), fontSize = 11.sp, fontFamily = FontFamily.Monospace, color = Palette.TextDim)
        Spacer(Modifier.height(if (short) 4.dp else 70.dp))
    }
}
