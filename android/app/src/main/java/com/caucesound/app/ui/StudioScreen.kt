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
import androidx.compose.material.icons.filled.AutoAwesome
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
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
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
    val countIn = remember { mutableLongStateOf(0L) }
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
            countIn.longValue = NativeEngine.countInRemaining()
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
        favorites = vm.favoritePresets.collectAsState().value, countIn = countIn,
    )

    // --- Diálogos ---
    when (val d = dialog) {
        "tempo" -> TempoDialog(state, vm.countInBars.collectAsState().value, onDismiss = { dialog = null }) { bpm, bpb, bars -> vm.setCountInBars(bars); if (bpm != state.bpm || bpb != state.beatsPerBar) vm.setTempo(bpm, bpb); dialog = null }
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
    favorites: Set<String> = emptySet(), countIn: LongState? = null,
) {
    var drumClipboard by remember { mutableStateOf<List<List<Int>>?>(null) }
    // Horizontal (poca altura): barra superior y transporte en una sola fila y navegación compacta, para dejar
    // el máximo espacio vertical a las pistas.
    val compact = androidx.compose.ui.platform.LocalConfiguration.current.screenHeightDp < 500
    val transport: @Composable (Modifier) -> Unit = { m ->
        Transport(
            state, playing, position, countIn, onRewind = { actions.seek(0) }, onPlay = actions::play, onStop = actions::stop,
            onRecord = onRecord, onTempo = { onDialog("tempo") }, onMetronome = { actions.setMetronome(!state.metronome) },
            onLoop = { actions.setLoop(!state.loopEnabled, state.loopStart, state.loopEnd) }, onLoopEdit = { onDialog("loop") }, modifier = m,
        )
    }
    Scaffold(containerColor = Palette.Background, snackbarHost = { SnackbarHost(snackbar) }, bottomBar = { StudioNav(panel, actions::openPanel, compact) }) { pad ->
        Column(Modifier.fillMaxSize().padding(pad)) {
            // --- Barra superior (+ transporte en horizontal) ---
            Row(Modifier.fillMaxWidth().background(Palette.Surface).padding(horizontal = 4.dp, vertical = 2.dp), verticalAlignment = Alignment.CenterVertically) {
                ToolButton(Icons.AutoMirrored.Filled.ArrowBack, stringResource(R.string.back), { actions.closeProject() }, tint = Palette.Text)
                Column(Modifier.weight(1f).padding(horizontal = 4.dp)) {
                    Text(state.name, maxLines = 1, overflow = TextOverflow.Ellipsis, style = androidx.compose.material3.MaterialTheme.typography.titleSmall)
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Box(Modifier.size(6.dp).clip(androidx.compose.foundation.shape.CircleShape).background(if (state.dirty) Palette.Warn else Palette.Ok))
                        Spacer(Modifier.width(5.dp))
                        Text(if (state.dirty) stringResource(R.string.unsaved) else stringResource(R.string.saved), color = Palette.TextDim, fontSize = 11.sp, maxLines = 1)
                    }
                }
                if (compact) transport(Modifier.width(440.dp))
                if (state.music != null) ToolButton(Icons.Default.AutoAwesome, tr("Variaciones", "Variations"), { actions.openPanel(if (panel == Panel.VARY) Panel.ARRANGE else Panel.VARY) },
                    active = panel == Panel.VARY, tint = Palette.Text, modifier = Modifier.testTag("open_variations"))
                ToolButton(Icons.AutoMirrored.Filled.Undo, stringResource(R.string.undo), actions::undo, enabled = state.canUndo && !state.recording, tint = Palette.Text)
                ToolButton(Icons.AutoMirrored.Filled.Redo, stringResource(R.string.redo), actions::redo, enabled = state.canRedo && !state.recording, tint = Palette.Text)
                ToolButton(Icons.Default.Save, stringResource(R.string.save), actions::saveNow, tint = if (state.dirty) Palette.Text else Palette.TextDim)
                ToolButton(Icons.Default.FileDownload, stringResource(R.string.export), { onDialog("export") }, enabled = state.contentEnd > 0 && !state.recording, tint = Palette.Text)
                ToolButton(Icons.Default.Settings, stringResource(R.string.settings), onSettings)
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
                        } else SynthPanel(state, t, catalog, actions, favorites)
                    }
                    Panel.MIXER -> MixerPanel(state, meters, selectedTrack, actions)
                    Panel.VARY -> VariationsPanel(state, actions)
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

/** Navegación inferior propia: misma apariencia en vertical y horizontal; la pestaña activa se marca en violeta. */
@Composable
private fun StudioNav(panel: Panel, onSelect: (Panel) -> Unit, compact: Boolean = false) {
    val items = listOf(
        Triple(Panel.ARRANGE, Icons.Default.ViewTimeline, tr("Arreglo", "Arrange")),
        Triple(Panel.DRUMS, Icons.Default.GridOn, tr("Batería", "Drums")),
        Triple(Panel.PIANO, Icons.Default.Piano, "Piano roll"),
        Triple(Panel.SYNTH, Icons.Default.Tune, "Synth"),
        Triple(Panel.MIXER, Icons.Default.GraphicEq, tr("Mezcla", "Mixer")),
    )
    Column(Modifier.fillMaxWidth().background(Palette.Surface)) {
        Box(Modifier.fillMaxWidth().height(1.dp).background(Palette.Stroke))
        Row(Modifier.fillMaxWidth().navigationBarsPadding().height(if (compact) 48.dp else 60.dp), verticalAlignment = Alignment.CenterVertically) {
            items.forEach { (p, icon, label) ->
                val on = p == panel || (p == Panel.MIXER && panel == Panel.FX)
                val tint = if (on) Palette.VioletSoft else Palette.TextDim
                val content: @Composable () -> Unit = {
                    Box(
                        Modifier.clip(RoundedCornerShape(50)).background(if (on) Palette.Violet.copy(alpha = 0.2f) else Color.Transparent).padding(horizontal = if (compact) 10.dp else 16.dp, vertical = 4.dp),
                    ) { Icon(icon, null, Modifier.size(22.dp), tint = tint) }
                    Text(label, fontSize = 11.sp, maxLines = 1, color = if (on) Palette.Text else Palette.TextDim, fontWeight = if (on) FontWeight.SemiBold else FontWeight.Medium,
                        modifier = if (compact) Modifier.padding(start = 6.dp) else Modifier.padding(top = 2.dp))
                }
                val m = Modifier.weight(1f).fillMaxHeight().clickable(role = androidx.compose.ui.semantics.Role.Tab) { onSelect(p) }
                if (compact) Row(m, verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.Center) { content() }
                else Column(m, verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) { content() }
            }
        }
    }
}

@Composable
private fun EmptyPanel(text: String, button: String, onClick: () -> Unit) {
    EmptyState(Icons.Default.Add, text, null, Modifier.fillMaxSize(), action = button, onAction = onClick)
}

/** Lectura de posición aislada: es lo único que se recompone a cada fotograma. */
@Composable
private fun PositionReadout(state: ProjectState, position: LongState, countIn: LongState?) {
    val pos = position.longValue
    val left = countIn?.longValue ?: 0L
    Column(Modifier.padding(horizontal = 4.dp)) {
        // Cuenta previa: tiempos que faltan para que empiece la toma (dato del motor, no un temporizador de la interfaz).
        if (left > 0) Text(tr("CUENTA", "COUNT-IN") + " " + MusicTime.countInBeatsLeft(left, state.bpm), color = Palette.Solo, fontWeight = FontWeight.Bold, fontSize = 11.sp)
        else if (state.recording) Text("● REC", color = Palette.Record, fontWeight = FontWeight.Bold, fontSize = 11.sp)
        Text(MusicTime.barsBeats(pos, state.bpm, state.beatsPerBar), fontFamily = FontFamily.Monospace, fontSize = 17.sp, fontWeight = FontWeight.Bold, maxLines = 1, softWrap = false)
        Text(MusicTime.clock(pos), fontFamily = FontFamily.Monospace, fontSize = 11.sp, letterSpacing = (-0.3).sp, color = Palette.TextDim, maxLines = 1, softWrap = false)
    }
}

@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun Transport(
    s: ProjectState, playing: Boolean, position: LongState, countIn: LongState?, onRewind: () -> Unit, onPlay: () -> Unit, onStop: () -> Unit,
    onRecord: () -> Unit, onTempo: () -> Unit, onMetronome: () -> Unit, onLoop: () -> Unit, onLoopEdit: () -> Unit,
    modifier: Modifier = Modifier.fillMaxWidth(),
) {
    // Cabe en 360 dp de ancho (teléfonos compactos) sin desplazamiento: lo esencial siempre visible. En pantallas
    // estrechas los botones se encogen un poco (sin bajar de 40 dp) para que la posición no se parta en varias líneas.
    val narrow = androidx.compose.ui.platform.LocalConfiguration.current.screenWidthDp < 400
    val big = if (narrow) 46.dp else 50.dp
    val small = if (narrow) 40.dp else Dims.TouchMin
    Row(
        modifier.padding(horizontal = 6.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(if (narrow) 2.dp else 4.dp),
    ) {
        RoundButton(Icons.Default.SkipPrevious, stringResource(R.string.rewind), onRewind, size = if (narrow) 36.dp else 40.dp, background = Palette.SurfaceHigh)
        if (playing || s.recording) RoundButton(Icons.Default.Stop, stringResource(R.string.stop), onStop, size = big, background = Palette.SurfaceTop)
        else RoundButton(Icons.Default.PlayArrow, stringResource(R.string.play), onPlay, size = big, background = Palette.Violet, tint = Color.White, glow = true)
        RoundButton(
            Icons.Default.FiberManualRecord, stringResource(R.string.record), onRecord, size = big, enabled = !s.recording,
            background = if (s.recording) Palette.Record else Palette.SurfaceTop, tint = if (s.recording) Color.White else Palette.Record,
        )
        Box(Modifier.weight(1f)) { PositionReadout(s, position, countIn) }
        Column(
            Modifier.clip(RoundedCornerShape(Dims.RadiusMedium)).background(Palette.SurfaceHigh)
                .clickable(enabled = !s.recording, onClick = onTempo).semantics { contentDescription = "Tempo" }.padding(horizontal = if (narrow) 7.dp else 10.dp, vertical = 5.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(if (s.bpm % 1.0 == 0.0) "${s.bpm.toInt()}" else "%.1f".format(s.bpm), fontFamily = FontFamily.Monospace, fontSize = 16.sp, fontWeight = FontWeight.Bold)
            Text("BPM · ${s.beatsPerBar}/4", fontSize = 10.sp, color = Palette.TextDim, maxLines = 1)
        }
        ToolButton(Icons.Default.Timer, stringResource(R.string.metronome), onMetronome, active = s.metronome, activeColor = Palette.Cyan, size = small)
        // Bucle: toque = activar/desactivar; pulsación larga = editar rango.
        Box(
            Modifier.size(small).clip(RoundedCornerShape(Dims.RadiusMedium)).background(if (s.loopEnabled) Palette.Cyan.copy(alpha = 0.18f) else Color.Transparent)
                .combinedClickable(onClick = onLoop, onLongClick = onLoopEdit), contentAlignment = Alignment.Center,
        ) { Icon(Icons.Default.Loop, tr("Bucle", "Loop"), Modifier.size(22.dp), tint = if (s.loopEnabled) Palette.Cyan else Palette.TextDim) }
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
                    CauceButton(stringResource(R.string.add_track), { addMenu = true }, Modifier.padding(8.dp), kind = ButtonKind.Ghost, icon = Icons.Default.Add)
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
            ToolButton(Icons.Default.ZoomOut, stringResource(R.string.zoom_out), { zoom(1 / 1.5f, 0f) })
            ToolButton(Icons.Default.ZoomIn, stringResource(R.string.zoom_in), { zoom(1.5f, 0f) })
            ToolButton(Icons.Default.GridOn, stringResource(R.string.snap), { snap = !snap }, active = snap)
            Box(Modifier.padding(horizontal = 6.dp).width(1.dp).height(24.dp).background(Palette.Stroke))
            val audioSel = selected?.let { state.clip(it) }
            val midiSel = selected?.let { state.midiClip(it) }
            if (audioSel != null && !state.recording) {
                val c = audioSel.second
                val pos = position.longValue
                ToolButton(Icons.Default.ContentCut, stringResource(R.string.split), { vm.splitSelectedAt(position.longValue) }, enabled = pos > c.start && pos < c.end, tint = Palette.Text)
                ToolButton(Icons.Default.ContentCopy, stringResource(R.string.duplicate), vm::duplicateSelected, tint = Palette.Text)
                ToolButton(Icons.Default.Gradient, stringResource(R.string.fades), onFades, tint = Palette.Text)
                ToolButton(Icons.Default.VolumeUp, stringResource(R.string.clip_gain), onGain, tint = Palette.Text)
                ToolButton(Icons.Default.Delete, stringResource(R.string.delete), vm::deleteSelected, tint = Palette.Record)
            } else if (midiSel != null && !state.recording) {
                val (t, c) = midiSel
                CauceButton(tr("Editar", "Edit"), { vm.selectTrack(t.id); vm.openPanel(if (t.isDrums) Panel.DRUMS else Panel.PIANO) }, icon = Icons.Default.Edit, height = 40.dp)
                Spacer(Modifier.width(6.dp))
                // Duplicar coloca la copia justo después: es la forma rápida de repetir compases.
                CauceButton(stringResource(R.string.duplicate), { vm.duplicateMidiClip(c.id) }, icon = Icons.Default.ContentCopy, height = 40.dp)
                ToolButton(Icons.Default.Delete, stringResource(R.string.delete), { vm.deleteMidiClip(c.id) }, tint = Palette.Record)
            } else {
                Text(tr("Toca un clip · doble toque: editor", "Tap a clip · double tap: editor"),
                    color = Palette.TextDim, fontSize = 12.sp, modifier = Modifier.padding(horizontal = 8.dp))
            }
        }
    }
}

/**
 * Cabecera de pista. Un toque en el botón de la derecha abre directamente lo que más se usa en ese tipo de pista
 * (batería → rejilla, synth → piano roll, bus → efectos, audio → importar); la pulsación larga abre el menú completo.
 */
@OptIn(ExperimentalFoundationApi::class)
@Composable
private fun TrackHeader(t: TrackState, selected: Boolean, meter: () -> Float, vm: StudioActions, onImport: () -> Unit, onMenu: () -> Unit) {
    val color = Color(t.color.toInt())
    var menu by remember { mutableStateOf(false) }
    fun open(p: Panel) { vm.selectTrack(t.id); vm.openPanel(p) }
    Row(
        Modifier.width(HEADER_W).height(TRACK_HEIGHT).background(if (selected) Palette.SurfaceHigh else Palette.Surface)
            .combinedClickable(onClick = { vm.selectTrack(t.id) }, onLongClick = { menu = true }),
    ) {
        Box(Modifier.width(if (selected) 4.dp else 3.dp).fillMaxHeight().background(if (selected) color else color.copy(alpha = 0.7f)))
        Column(Modifier.weight(1f).padding(start = 8.dp, end = 2.dp, top = 4.dp, bottom = 6.dp), verticalArrangement = Arrangement.SpaceBetween) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(kindIcon(t), null, Modifier.size(14.dp), tint = color)
                Spacer(Modifier.width(5.dp))
                Text(t.name, Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis, fontSize = 13.sp, fontWeight = FontWeight.SemiBold)
                Box {
                    val (icon, desc) = when {
                        t.isAudio -> Icons.Default.FileUpload to stringResource(R.string.import_audio)
                        t.isDrums -> Icons.Default.GridOn to tr("Editar batería", "Edit drums")
                        t.isSynth -> Icons.Default.Piano to tr("Editar notas", "Edit notes")
                        else -> Icons.Default.GraphicEq to tr("Efectos del bus", "Bus effects")
                    }
                    Icon(icon, desc, Modifier.size(32.dp).clip(RoundedCornerShape(8.dp)).combinedClickable(
                        onClick = { when { t.isAudio -> onImport(); t.isDrums -> open(Panel.DRUMS); t.isSynth -> open(Panel.PIANO); else -> open(Panel.FX) } },
                        onLongClick = { menu = true }).padding(6.dp), tint = Palette.TextDim)
                    DropdownMenu(menu, { menu = false }) {
                        if (t.isSynth) {
                            DropdownMenuItem({ Text("Piano roll") }, leadingIcon = { Icon(Icons.Default.Piano, null) }, onClick = { menu = false; open(Panel.PIANO) })
                            DropdownMenuItem({ Text(tr("Sonido (Synth)", "Sound (Synth)")) }, leadingIcon = { Icon(Icons.Default.Tune, null) }, onClick = { menu = false; open(Panel.SYNTH) })
                        }
                        if (t.isDrums) DropdownMenuItem({ Text(tr("Rejilla de batería", "Drum grid")) }, leadingIcon = { Icon(Icons.Default.GridOn, null) }, onClick = { menu = false; open(Panel.DRUMS) })
                        if (t.isAudio) DropdownMenuItem({ Text(stringResource(R.string.import_audio)) }, leadingIcon = { Icon(Icons.Default.FileUpload, null) }, onClick = { menu = false; onImport() })
                        DropdownMenuItem({ Text(tr("Efectos", "Effects")) }, leadingIcon = { Icon(Icons.Default.GraphicEq, null) }, onClick = { menu = false; open(Panel.FX) })
                        DropdownMenuItem({ Text(tr("Renombrar o eliminar…", "Rename or delete…")) }, leadingIcon = { Icon(Icons.Default.Edit, null) }, onClick = { menu = false; onMenu() })
                    }
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(4.dp), verticalAlignment = Alignment.CenterVertically) {
                PillToggle("M", t.mute, { vm.setMute(t.id, !t.mute) }, color = Palette.Mute, minWidth = 32.dp, height = 30.dp)
                PillToggle("S", t.solo, { vm.setSolo(t.id, !t.solo) }, color = Palette.Solo, minWidth = 32.dp, height = 30.dp)
                if (t.isAudio) PillToggle("R", t.armed, { vm.setArmed(t.id, !t.armed) }, color = Palette.Record, minWidth = 32.dp, height = 30.dp)
                else Text(kindLabel(t), fontSize = 9.sp, color = Palette.TextFaint, letterSpacing = 1.sp, modifier = Modifier.padding(start = 4.dp))
            }
            LevelMeter(meter, Modifier.fillMaxWidth().padding(end = 6.dp).height(4.dp), vertical = false)
        }
        Box(Modifier.width(1.dp).fillMaxHeight().background(Palette.Stroke))
    }
}

internal fun kindIcon(t: TrackState): ImageVector = when {
    t.isDrums -> Icons.Default.GridOn; t.isSynth -> Icons.Default.MusicNote; t.isBus -> Icons.Default.CallSplit; else -> Icons.Default.Mic
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
