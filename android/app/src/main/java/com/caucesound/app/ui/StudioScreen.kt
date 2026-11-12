package com.caucesound.app.ui

import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.animation.core.tween
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.horizontalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.Redo
import androidx.compose.material.icons.automirrored.filled.Undo
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.ContentCut
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.FiberManualRecord
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.FileUpload
import androidx.compose.material.icons.filled.Gradient
import androidx.compose.material.icons.filled.CallSplit
import androidx.compose.material.icons.filled.GridOn
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material.icons.filled.Loop
import androidx.compose.material.icons.filled.Mic
import androidx.compose.material.icons.filled.MusicNote
import androidx.compose.material.icons.filled.Piano
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.filled.SkipPrevious
import androidx.compose.material.icons.filled.Stop
import androidx.compose.material.icons.filled.Timer
import androidx.compose.material.icons.filled.Tune
import androidx.compose.material.icons.filled.ViewTimeline
import androidx.compose.material.icons.filled.VolumeUp
import androidx.compose.material.icons.filled.ZoomIn
import androidx.compose.material.icons.filled.ZoomOut
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.NavigationBarItemDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.LongState
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableLongStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.DawViewModel
import com.caucesound.app.R
import com.caucesound.app.audio.NativeEngine
import com.caucesound.app.data.Catalog
import com.caucesound.app.data.MusicTime
import com.caucesound.app.data.PPQ
import com.caucesound.app.data.ProjectState
import com.caucesound.app.data.TrackState
import kotlinx.coroutines.launch
import kotlin.math.max
import kotlin.math.roundToInt

private val HEADER_W = 136.dp

/** Estudio: barra superior, transporte, panel activo (Arreglo / Batería / Piano roll / Synth / Mezclador) y navegación. */
@Composable
fun StudioScreen(vm: DawViewModel, state: ProjectState, requestMic: (() -> Unit) -> Unit) {
    val selected by vm.selectedClip.collectAsState()
    val waveforms by vm.waveforms.collectAsState()
    val message by vm.messages.collectAsState()
    val busy by vm.busy.collectAsState()
    val catalog by vm.catalog.collectAsState()
    val selectedTrack by vm.selectedTrack.collectAsState()
    val panel by vm.panel.collectAsState()
    val snackbar = remember { SnackbarHostState() }
    val position = remember { mutableLongStateOf(0L) }
    val playingState = remember { mutableStateOf(false) }
    val meters = remember { mutableStateOf(FloatArray(0)) }
    var dialog by remember { mutableStateOf<String?>(null) }
    var importTrack by remember { mutableStateOf<String?>(null) }
    var exportBits by remember { mutableStateOf(24) }

    LaunchedEffect(message) { message?.let { snackbar.showSnackbar(it); vm.consumeMessage() } }
    // Bucle de interfaz a la frecuencia de pantalla. Solo se leen atómicos del motor; los valores se consumen en la fase
    // de dibujo (o en composables pequeños) para no recomponer toda la pantalla en cada fotograma.
    LaunchedEffect(state.tracks.size) {
        while (true) {
            withFrameNanos { }
            position.longValue = NativeEngine.position()
            playingState.value = NativeEngine.isPlaying()
            meters.value = NativeEngine.meters(state.tracks.size)
        }
    }
    BackHandler { when (panel) { Panel.ARRANGE -> vm.closeProject(); Panel.FX -> vm.openPanel(Panel.MIXER); else -> vm.openPanel(Panel.ARRANGE) } }

    val importer = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        val t = importTrack
        if (uri != null && t != null) vm.importAudio(uri, t, uri.lastPathSegment?.substringAfterLast('/') ?: "audio")
        importTrack = null
    }
    val exporter = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("audio/wav")) { uri ->
        if (uri != null) vm.exportWav(uri, exportBits)
    }

    StudioLayout(
        state, panel, selected, selectedTrack, waveforms, catalog, busy != null, position, playingState.value, meters, vm, snackbar,
        onDialog = { dialog = it },
        onSettings = { vm.refreshAudioStatus(); dialog = "settings" },
        onImport = { id -> importTrack = id; importer.launch(arrayOf("audio/*")) },
        onRecord = { requestMic { vm.record() } },
    )

    // --- Diálogos ---
    when (val d = dialog) {
        "tempo" -> TempoDialog(state, onDismiss = { dialog = null }) { bpm, bpb -> vm.setTempo(bpm, bpb); dialog = null }
        "loop" -> LoopDialog(state, onDismiss = { dialog = null }) { on, s, e -> vm.setLoop(on, s, e); dialog = null }
        "fades" -> selected?.let { state.clip(it) }?.let { (_, c) ->
            FadesDialog(c.length, c.fadeIn, c.fadeOut, onDismiss = { dialog = null }) { i, o -> vm.setFades(c.id, i, o); dialog = null }
        } ?: run { dialog = null }
        "gain" -> selected?.let { state.clip(it) }?.let { (_, c) ->
            GainDialog(c.gainDb, onDismiss = { dialog = null }) { db -> vm.setClipGain(c.id, db); dialog = null }
        } ?: run { dialog = null }
        "export" -> ExportDialog(onDismiss = { dialog = null }) { bits ->
            exportBits = bits; dialog = null
            exporter.launch("${state.name.ifBlank { "mix" }}.wav")
        }
        "settings" -> SettingsDialog(vm, state, requestMic, onDismiss = { dialog = null })
        null -> {}
        else -> if (d.startsWith("rename:")) {
            val id = d.removePrefix("rename:")
            val t = state.tracks.firstOrNull { it.id == id }
            if (t == null) dialog = null else RenameDialog(t.name, onDismiss = { dialog = null }, onDelete = { vm.removeTrack(id); dialog = null }) { vm.renameTrack(id, it); dialog = null }
        }
    }
}

/** Disposición del estudio sin dependencias del motor (se puede renderizar en pruebas JVM). */
@Composable
fun StudioLayout(
    state: ProjectState, panel: Panel, selected: String?, selectedTrack: String?, waveforms: Map<String, FloatArray>, catalog: Catalog,
    busy: Boolean, position: LongState, playing: Boolean, meters: androidx.compose.runtime.State<FloatArray>, actions: StudioActions,
    snackbar: SnackbarHostState, onDialog: (String) -> Unit, onSettings: () -> Unit, onImport: (String) -> Unit, onRecord: () -> Unit,
) {
    var drumClipboard by remember { mutableStateOf<List<List<Int>>?>(null) }
    // Horizontal (poca altura): barra superior y transporte en una sola fila y navegación compacta, para dejar
    // el máximo espacio vertical a las pistas.
    val compact = androidx.compose.ui.platform.LocalConfiguration.current.screenHeightDp < 500
    val transport: @Composable (Modifier) -> Unit = { m ->
        Transport(
            state, playing, position, onRewind = { actions.seek(0) }, onPlay = actions::play, onStop = actions::stop,
            onRecord = onRecord, onTempo = { onDialog("tempo") }, onMetronome = { actions.setMetronome(!state.metronome) },
            onLoop = { actions.setLoop(!state.loopEnabled, state.loopStart, state.loopEnd) }, onLoopEdit = { onDialog("loop") }, modifier = m,
        )
    }
    Scaffold(containerColor = Palette.Background, snackbarHost = { SnackbarHost(snackbar) }, bottomBar = { StudioNav(panel, actions::openPanel, compact) }) { pad ->
        Column(Modifier.fillMaxSize().padding(pad)) {
            // --- Barra superior (+ transporte en horizontal) ---
            Row(Modifier.fillMaxWidth().background(Palette.Surface).padding(horizontal = 4.dp), verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = { actions.closeProject() }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back)) }
                Column(Modifier.weight(1f)) {
                    Text(state.name, maxLines = 1, overflow = TextOverflow.Ellipsis, fontWeight = FontWeight.SemiBold)
                    Text(if (state.dirty) stringResource(R.string.unsaved) else stringResource(R.string.saved), color = Palette.TextDim, fontSize = 11.sp)
                }
                if (compact) transport(Modifier.width(440.dp))
                IconButton(onClick = actions::undo, enabled = state.canUndo && !state.recording) { Icon(Icons.AutoMirrored.Filled.Undo, stringResource(R.string.undo)) }
                IconButton(onClick = actions::redo, enabled = state.canRedo && !state.recording) { Icon(Icons.AutoMirrored.Filled.Redo, stringResource(R.string.redo)) }
                IconButton(onClick = actions::saveNow) { Icon(Icons.Default.Save, stringResource(R.string.save)) }
                IconButton(onClick = { onDialog("export") }, enabled = state.contentEnd > 0 && !state.recording) { Icon(Icons.Default.FileDownload, stringResource(R.string.export)) }
                IconButton(onClick = onSettings) { Icon(Icons.Default.Settings, stringResource(R.string.settings)) }
            }
            // --- Transporte ---
            if (!compact) transport(Modifier.fillMaxWidth().background(Palette.Background))
            if (busy) LinearProgressIndicator(Modifier.fillMaxWidth())

            // --- Panel activo ---
            val track = state.tracks.firstOrNull { it.id == selectedTrack }
            AnimatedContent(panel, Modifier.weight(1f).fillMaxWidth(), transitionSpec = { fadeIn(tween(140)) togetherWith fadeOut(tween(100)) }, label = "panel") { p ->
                when (p) {
                    Panel.ARRANGE -> ArrangementView(
                        state, waveforms, selected, selectedTrack, position, meters, actions,
                        onImport = onImport,
                        onTrackMenu = { id -> onDialog("rename:$id") },
                        onFades = { onDialog("fades") }, onGain = { onDialog("gain") },
                    )
                    Panel.DRUMS -> {
                        val t = track?.takeIf { it.isDrums } ?: state.tracks.firstOrNull { it.isDrums }
                        if (t == null) EmptyPanel(tr("No hay pistas de batería.", "No drum tracks."), tr("Añadir Cauce Drums", "Add Cauce Drums")) {
                            actions.addTrack(tr("Batería", "Drums"), "drums")
                        } else DrumsPanel(state, t, catalog, position, playing, actions, drumClipboard) { drumClipboard = it }
                    }
                    Panel.PIANO -> {
                        val t = track?.takeIf { it.isSynth } ?: state.tracks.firstOrNull { it.isSynth }
                        val clip = t?.let { tt -> selected?.let { id -> tt.midiClips.firstOrNull { it.id == id } } ?: tt.midiClips.firstOrNull() }
                        when {
                            t == null -> EmptyPanel(tr("No hay pistas de sintetizador.", "No synth tracks."), tr("Añadir Cauce Synth", "Add Cauce Synth")) {
                                actions.addTrack("Synth", "synth")
                            }
                            clip == null -> EmptyPanel(tr("La pista «${t.name}» no tiene clips MIDI.", "Track “${t.name}” has no MIDI clips."), tr("Crear clip de 4 compases", "Create 4-bar clip")) {
                                val bar = PPQ.toLong() * state.beatsPerBar
                                val start = MusicTime.floorTicks(state.framesToTicks(position.longValue), bar)
                                actions.addMidiClip(t.id, start, bar * 4, null)
                            }
                            else -> PianoRollPanel(state, t, clip, position, playing, actions)
                        }
                    }
                    Panel.SYNTH -> {
                        val t = track?.takeIf { it.isSynth } ?: state.tracks.firstOrNull { it.isSynth }
                        if (t == null) EmptyPanel(tr("No hay pistas de sintetizador.", "No synth tracks."), tr("Añadir Cauce Synth", "Add Cauce Synth")) {
                            actions.addTrack("Synth", "synth")
                        } else SynthPanel(state, t, catalog, actions)
                    }
                    Panel.MIXER -> MixerPanel(state, meters, selectedTrack, actions)
                    Panel.FX -> {
                        val t = track ?: state.tracks.firstOrNull()
                        if (t == null) EmptyPanel(tr("No hay pistas.", "No tracks."), tr("Añadir pista de audio", "Add audio track")) { actions.addTrack("Audio 1", "audio") }
                        else EffectsPanel(state, t, catalog, actions)
                    }
                }
            }
        }
    }
}

@Composable
private fun StudioNav(panel: Panel, onSelect: (Panel) -> Unit, compact: Boolean = false) {
    val items = listOf(
        Triple(Panel.ARRANGE, Icons.Default.ViewTimeline, tr("Arreglo", "Arrange")),
        Triple(Panel.DRUMS, Icons.Default.GridOn, tr("Batería", "Drums")),
        Triple(Panel.PIANO, Icons.Default.Piano, "Piano roll"),
        Triple(Panel.SYNTH, Icons.Default.Tune, "Synth"),
        Triple(Panel.MIXER, Icons.Default.GraphicEq, tr("Mezcla", "Mixer")),
    )
    if (compact) {
        // Fila de 52 dp con icono + etiqueta corta en línea (objetivos táctiles de 48 dp).
        Row(Modifier.fillMaxWidth().height(52.dp).background(Palette.Surface).navigationBarsPadding(), horizontalArrangement = Arrangement.SpaceEvenly, verticalAlignment = Alignment.CenterVertically) {
            items.forEach { (p, icon, label) ->
                val on = p == panel || (p == Panel.MIXER && panel == Panel.FX)
                PillToggle(label, on, { onSelect(p) }, color = Palette.Cyan, height = 44.dp, icon = icon)
            }
        }
        return
    }
    NavigationBar(containerColor = Palette.Surface, tonalElevation = 0.dp) {
        items.forEach { (p, icon, label) ->
            NavigationBarItem(
                selected = p == panel || (p == Panel.MIXER && panel == Panel.FX), onClick = { onSelect(p) }, icon = { Icon(icon, null) },
                label = { Text(label, fontSize = 11.sp, maxLines = 1) },
                colors = NavigationBarItemDefaults.colors(
                    selectedIconColor = Color.Black, indicatorColor = Palette.Cyan, selectedTextColor = Palette.Text,
                    unselectedIconColor = Palette.TextDim, unselectedTextColor = Palette.TextDim,
                ),
            )
        }
    }
}

@Composable
private fun EmptyPanel(text: String, button: String, onClick: () -> Unit) {
    Column(Modifier.fillMaxSize().padding(24.dp), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
        Text(text, color = Palette.TextDim)
        Spacer(Modifier.height(12.dp))
        PillToggle(button, true, onClick, color = Palette.Violet, height = 48.dp, icon = Icons.Default.Add)
    }
}

/** Lectura de posición aislada: es lo único que se recompone a cada fotograma. */
@Composable
private fun PositionReadout(state: ProjectState, position: LongState) {
    val pos = position.longValue
    Column(Modifier.padding(horizontal = 4.dp)) {
        if (state.recording) Text("● REC", color = Palette.Record, fontWeight = FontWeight.Bold, fontSize = 11.sp)
        Text(MusicTime.barsBeats(pos, state.bpm, state.beatsPerBar), fontFamily = FontFamily.Monospace, fontSize = 17.sp, fontWeight = FontWeight.Bold, maxLines = 1)
        Text(MusicTime.clock(pos), fontFamily = FontFamily.Monospace, fontSize = 11.sp, color = Palette.TextDim)
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun Transport(
    s: ProjectState, playing: Boolean, position: LongState, onRewind: () -> Unit, onPlay: () -> Unit, onStop: () -> Unit,
    onRecord: () -> Unit, onTempo: () -> Unit, onMetronome: () -> Unit, onLoop: () -> Unit, onLoopEdit: () -> Unit,
    modifier: Modifier = Modifier.fillMaxWidth(),
) {
    // Cabe en 360 dp de ancho (teléfonos compactos) sin desplazamiento: lo esencial siempre visible.
    Row(
        modifier.padding(horizontal = 6.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(4.dp),
    ) {
        RoundButton(Icons.Default.SkipPrevious, stringResource(R.string.rewind), onRewind, size = 40.dp)
        if (playing || s.recording) RoundButton(Icons.Default.Stop, stringResource(R.string.stop), onStop, size = 50.dp, background = Palette.SurfaceTop)
        else RoundButton(Icons.Default.PlayArrow, stringResource(R.string.play), onPlay, size = 50.dp, background = Palette.Violet, tint = Color.White, glow = true)
        RoundButton(
            Icons.Default.FiberManualRecord, stringResource(R.string.record), onRecord, size = 50.dp, enabled = !s.recording,
            background = if (s.recording) Palette.Record else Palette.SurfaceTop, tint = if (s.recording) Color.White else Palette.Record,
        )
        Box(Modifier.weight(1f)) { PositionReadout(s, position) }
        Column(
            Modifier.clip(RoundedCornerShape(12.dp)).border(1.dp, Palette.Grid, RoundedCornerShape(12.dp))
                .clickable(enabled = !s.recording, onClick = onTempo).padding(horizontal = 8.dp, vertical = 4.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(if (s.bpm % 1.0 == 0.0) "${s.bpm.toInt()}" else "%.1f".format(s.bpm), fontFamily = FontFamily.Monospace, fontSize = 15.sp, fontWeight = FontWeight.Bold)
            Text("BPM ${s.beatsPerBar}/4", fontSize = 10.sp, color = Palette.TextDim)
        }
        Box(Modifier.size(44.dp).clickable(onClick = onMetronome), contentAlignment = Alignment.Center) {
            Icon(Icons.Default.Timer, stringResource(R.string.metronome), tint = if (s.metronome) Palette.Accent else Palette.TextDim)
        }
        // Bucle: toque = activar/desactivar; pulsación larga = editar rango.
        Box(Modifier.size(44.dp).combinedClickable(onClick = onLoop, onLongClick = onLoopEdit), contentAlignment = Alignment.Center) {
            Icon(Icons.Default.Loop, tr("Bucle", "Loop"), tint = if (s.loopEnabled) Palette.Cyan else Palette.TextDim)
        }
    }
}

@Composable
private fun ToggleIcon(icon: ImageVector, desc: String, on: Boolean, onClick: () -> Unit) {
    IconButton(onClick = onClick, colors = IconButtonDefaults.iconButtonColors(contentColor = if (on) Palette.Accent else Palette.TextDim)) { Icon(icon, desc) }
}

// ----------------------------------------------------------------------------------------------------------------
@Composable
private fun ArrangementView(
    state: ProjectState, waveforms: Map<String, FloatArray>, selected: String?, selectedTrack: String?, position: LongState,
    meters: androidx.compose.runtime.State<FloatArray>, vm: StudioActions, onImport: (String) -> Unit, onTrackMenu: (String) -> Unit,
    onFades: () -> Unit, onGain: () -> Unit,
) {
    val hScroll = rememberScrollState()
    val vScroll = rememberScrollState()
    val scope = rememberCoroutineScope()
    var pxPerSecond by remember { mutableFloatStateOf(60f) }
    var snap by remember { mutableStateOf(true) }
    var addMenu by remember { mutableStateOf(false) }

    fun zoom(factor: Float, focusX: Float) {
        val old = pxPerSecond
        val next = (old * factor).coerceIn(5f, 2000f)
        if (next == old) return
        val anchor = (hScroll.value + focusX) * (next / old) - focusX
        pxPerSecond = next
        scope.launch { hScroll.scrollTo(max(0f, anchor).roundToInt()) }
    }

    Column(Modifier.fillMaxSize()) {
        Row(Modifier.weight(1f).fillMaxWidth().verticalScroll(vScroll)) {
            Column(Modifier.width(HEADER_W)) {
                Spacer(Modifier.height(RULER_HEIGHT).fillMaxWidth().background(Palette.Surface))
                state.tracks.forEachIndexed { i, t ->
                    TrackHeader(t, t.id == selectedTrack, { meters.value.getOrElse(i + 1) { 0f } }, vm, onImport = { onImport(t.id) }, onMenu = { onTrackMenu(t.id) })
                }
                if (state.tracks.size < 32) Box {
                    TextButton(onClick = { addMenu = true }, modifier = Modifier.padding(4.dp).height(48.dp)) {
                        Icon(Icons.Default.Add, null); Spacer(Modifier.width(4.dp)); Text(stringResource(R.string.add_track))
                    }
                    DropdownMenu(addMenu, { addMenu = false }) {
                        val n = state.tracks.size + 1
                        DropdownMenuItem({ Text(tr("Audio (grabar / importar)", "Audio (record / import)")) }, leadingIcon = { Icon(Icons.Default.Mic, null) },
                            onClick = { addMenu = false; vm.addTrack(tr("Audio", "Audio") + " $n", "audio") })
                        DropdownMenuItem({ Text("Cauce Synth") }, leadingIcon = { Icon(Icons.Default.Piano, null) },
                            onClick = { addMenu = false; vm.addTrack("Synth $n", "synth") })
                        DropdownMenuItem({ Text("Cauce Drums") }, leadingIcon = { Icon(Icons.Default.GridOn, null) },
                            onClick = { addMenu = false; vm.addTrack(tr("Batería", "Drums") + " $n", "drums") })
                    }
                }
            }
            Box(Modifier.weight(1f)) {
                Timeline(
                    state, waveforms, TimelineGeometry(pxPerSecond), hScroll, selected, snap,
                    position = { position.longValue },
                    onSeek = vm::seek,
                    onSelect = { id ->
                        vm.selectClip(id)
                        id?.let { state.midiClip(it)?.first ?: state.clip(it)?.first }?.let { vm.selectTrack(it.id) }
                    },
                    onMove = vm::moveClip, onTrim = vm::trimClip,
                    onMoveMidi = vm::moveMidiClip, onResizeMidi = vm::resizeMidiClip,
                    onOpenClip = { id ->
                        val t = state.midiClip(id)?.first
                        if (t != null) { vm.selectClip(id); vm.selectTrack(t.id); vm.openPanel(if (t.isDrums) Panel.DRUMS else Panel.PIANO) }
                    },
                    onZoom = ::zoom,
                )
            }
        }
        // --- Herramientas del arreglo ---
        Row(
            Modifier.fillMaxWidth().background(Palette.Surface).horizontalScroll(rememberScrollState()).padding(horizontal = 4.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            IconButton(onClick = { zoom(1 / 1.5f, 0f) }) { Icon(Icons.Default.ZoomOut, stringResource(R.string.zoom_out)) }
            IconButton(onClick = { zoom(1.5f, 0f) }) { Icon(Icons.Default.ZoomIn, stringResource(R.string.zoom_in)) }
            ToggleIcon(Icons.Default.GridOn, stringResource(R.string.snap), snap) { snap = !snap }
            val audioSel = selected?.let { state.clip(it) }
            val midiSel = selected?.let { state.midiClip(it) }
            if (audioSel != null && !state.recording) {
                val c = audioSel.second
                val pos = position.longValue
                IconButton(onClick = { vm.splitSelectedAt(position.longValue) }, enabled = pos > c.start && pos < c.end) {
                    Icon(Icons.Default.ContentCut, stringResource(R.string.split))
                }
                IconButton(onClick = vm::duplicateSelected) { Icon(Icons.Default.ContentCopy, stringResource(R.string.duplicate)) }
                IconButton(onClick = onFades) { Icon(Icons.Default.Gradient, stringResource(R.string.fades)) }
                IconButton(onClick = onGain) { Icon(Icons.Default.VolumeUp, stringResource(R.string.clip_gain)) }
                IconButton(onClick = vm::deleteSelected) { Icon(Icons.Default.Delete, stringResource(R.string.delete), tint = Palette.Record) }
            } else if (midiSel != null && !state.recording) {
                val (t, c) = midiSel
                IconButton(onClick = { vm.selectTrack(t.id); vm.openPanel(if (t.isDrums) Panel.DRUMS else Panel.PIANO) }) {
                    Icon(Icons.Default.Edit, tr("Editar clip", "Edit clip"))
                }
                IconButton(onClick = { vm.duplicateMidiClip(c.id) }) { Icon(Icons.Default.ContentCopy, stringResource(R.string.duplicate)) }
                IconButton(onClick = { vm.deleteMidiClip(c.id) }) { Icon(Icons.Default.Delete, stringResource(R.string.delete), tint = Palette.Record) }
                Text(c.name, color = Palette.TextDim, fontSize = 12.sp, maxLines = 1)
            } else {
                Text(tr("Toca un clip · doble toque: editor", "Tap a clip · double tap: editor"),
                    color = Palette.TextDim, fontSize = 12.sp, modifier = Modifier.padding(horizontal = 8.dp))
            }
        }
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun TrackHeader(t: TrackState, selected: Boolean, meter: () -> Float, vm: StudioActions, onImport: () -> Unit, onMenu: () -> Unit) {
    val color = Color(t.color.toInt())
    Row(
        Modifier.width(HEADER_W).height(TRACK_HEIGHT).background(if (selected) Palette.SurfaceHigh else Palette.Surface).border(0.5.dp, Palette.Grid)
            .combinedClickable(onClick = { vm.selectTrack(t.id) }, onLongClick = onMenu),
    ) {
        Box(Modifier.width(4.dp).fillMaxHeight().background(color))
        Column(Modifier.weight(1f).padding(horizontal = 6.dp, vertical = 4.dp), verticalArrangement = Arrangement.SpaceBetween) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    when { t.isDrums -> Icons.Default.GridOn; t.isSynth -> Icons.Default.MusicNote; t.isBus -> Icons.Default.CallSplit; else -> Icons.Default.Mic }, null,
                    Modifier.size(14.dp), tint = color,
                )
                Spacer(Modifier.width(4.dp))
                Text(t.name, Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                if (t.isAudio) Icon(Icons.Default.FileUpload, stringResource(R.string.import_audio), Modifier.size(28.dp).clickable(onClick = onImport).padding(4.dp), tint = Palette.TextDim)
                else Icon(Icons.Default.Settings, tr("Pista", "Track"), Modifier.size(28.dp).clickable(onClick = onMenu).padding(4.dp), tint = Palette.TextFaint)
            }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.CenterVertically) {
                PillToggle("M", t.mute, { vm.setMute(t.id, !t.mute) }, color = Palette.Mute, minWidth = 30.dp, height = 30.dp)
                PillToggle("S", t.solo, { vm.setSolo(t.id, !t.solo) }, color = Palette.Solo, minWidth = 30.dp, height = 30.dp)
                if (t.isAudio) PillToggle("R", t.armed, { vm.setArmed(t.id, !t.armed) }, color = Palette.Record, minWidth = 30.dp, height = 30.dp)
            }
            LevelMeter(meter, Modifier.fillMaxWidth().height(5.dp), vertical = false)
        }
    }
}

@Composable
internal fun LoopDialog(s: ProjectState, onDismiss: () -> Unit, onOk: (Boolean, Long, Long) -> Unit) {
    val bar = PPQ.toLong() * s.beatsPerBar
    var start by remember { mutableStateOf((s.loopStart / bar).toInt() + 1) }
    var end by remember { mutableStateOf(max((s.loopEnd / bar).toInt() + 1, start + 1)) }
    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(tr("Bucle", "Loop")) },
        text = {
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                Stepper(tr("Desde el compás", "From bar"), start, 1, 998) { start = it; if (end <= it) end = it + 1 }
                Stepper(tr("Hasta el compás", "To bar"), end, 2, 999) { end = it; if (start >= it) start = it - 1 }
                Text(tr("El bucle termina al inicio del compás final. Durante la grabación se ignora.",
                    "The loop ends at the start of the end bar. It is ignored while recording."), fontSize = 12.sp, color = Palette.TextDim)
            }
        },
        confirmButton = { TextButton(onClick = { onOk(true, (start - 1) * bar, (end - 1) * bar) }) { Text(tr("Activar", "Enable")) } },
        dismissButton = { TextButton(onClick = { onOk(false, (start - 1) * bar, (end - 1) * bar) }) { Text(tr("Desactivar", "Disable")) } },
    )
}

@Composable
private fun Stepper(label: String, value: Int, min: Int, maxV: Int, onChange: (Int) -> Unit) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, Modifier.weight(1f))
        PillToggle("−", false, { onChange((value - 1).coerceIn(min, maxV)) }, minWidth = 48.dp, height = 44.dp)
        Text("$value", Modifier.width(48.dp), fontFamily = FontFamily.Monospace, fontSize = 16.sp, textAlign = androidx.compose.ui.text.style.TextAlign.Center)
        PillToggle("+", false, { onChange((value + 1).coerceIn(min, maxV)) }, minWidth = 48.dp, height = 44.dp)
    }
}
