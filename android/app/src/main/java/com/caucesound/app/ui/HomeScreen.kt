package com.caucesound.app.ui

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.fadeIn
import androidx.compose.animation.slideInVertically
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.ContentCopy
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Edit
import androidx.compose.material.icons.filled.MoreVert
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.ExtendedFloatingActionButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.caucesound.app.data.ProjectSummary
import com.caucesound.app.data.TemplateInfo
import java.text.DateFormat
import java.util.Date

/** Acciones de la pantalla de inicio (las implementa DawViewModel). */
interface HomeActions {
    fun open(folder: String) {}
    fun create(name: String, template: String) {}
    fun rename(folder: String, name: String) {}
    fun duplicate(folder: String, name: String) {}
    fun delete(folder: String) {}
}

private val TEMPLATE_COLORS = mapOf(
    "trap" to listOf(Color(0xFF8B5CF6), Color(0xFF3B0764)),
    "reggaeton" to listOf(Color(0xFFF59E0B), Color(0xFF7C2D12)),
    "hiphop" to listOf(Color(0xFF3B82F6), Color(0xFF172554)),
    "pop" to listOf(Color(0xFFF472B6), Color(0xFF831843)),
    "electronic" to listOf(Color(0xFF22D3EE), Color(0xFF164E63)),
    "empty" to listOf(Color(0xFF39404E), Color(0xFF14161C)),
)

@Composable
fun HomeScreen(projects: List<ProjectSummary>, templates: List<TemplateInfo>, message: String?, onMessageShown: () -> Unit, actions: HomeActions) {
    val snackbar = remember { SnackbarHostState() }
    var creating by remember { mutableStateOf<String?>(null) }      // id de plantilla
    var chooser by remember { mutableStateOf(false) }
    var renaming by remember { mutableStateOf<ProjectSummary?>(null) }
    var deleting by remember { mutableStateOf<ProjectSummary?>(null) }
    LaunchedEffect(message) { message?.let { snackbar.showSnackbar(it); onMessageShown() } }
    val allTemplates = templates.map { it.id } + "empty"

    Scaffold(
        containerColor = Palette.Background,
        snackbarHost = { SnackbarHost(snackbar) },
        floatingActionButton = {
            ExtendedFloatingActionButton(
                onClick = { chooser = true }, containerColor = Palette.Violet, contentColor = Color.White,
                icon = { Icon(Icons.Default.Add, null) }, text = { Text(tr("Nuevo proyecto", "New project"), fontWeight = FontWeight.Bold) },
                modifier = Modifier.navigationBarsPadding(),
            )
        },
    ) { pad ->
        LazyColumn(Modifier.fillMaxSize().padding(pad), contentPadding = PaddingValues(bottom = 96.dp)) {
            item {
                Column(Modifier.fillMaxWidth().statusBarsPadding().padding(horizontal = 20.dp, vertical = 16.dp)) {
                    CauceLogo(height = 30.dp)
                    Spacer(Modifier.height(4.dp))
                    Text(tr("Estudio de producción musical", "Music production studio"), color = Palette.TextDim, style = MaterialTheme.typography.bodySmall)
                }
            }
            projects.firstOrNull { it.error == null }?.let { last ->
                item {
                    AnimatedVisibility(true, enter = fadeIn() + slideInVertically { it / 4 }) {
                        ContinueCard(last, onOpen = { actions.open(last.folder) })
                    }
                }
            }
            item { SectionTitle(tr("Empieza con una plantilla", "Start from a template")) }
            item {
                LazyRow(contentPadding = PaddingValues(horizontal = 16.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    items(allTemplates) { id ->
                        TemplateCard(id, templates.firstOrNull { it.id == id }) { creating = id }
                    }
                }
            }
            item { SectionTitle(tr("Proyectos recientes", "Recent projects")) }
            if (projects.isEmpty()) item {
                Text(tr("Todavía no hay proyectos. Elige una plantilla para empezar.", "No projects yet. Pick a template to begin."),
                    color = Palette.TextDim, modifier = Modifier.padding(horizontal = 20.dp, vertical = 8.dp))
            }
            items(projects, key = { it.folder }) { p ->
                ProjectRow(p, onOpen = { actions.open(p.folder) }, onRename = { renaming = p },
                    onDuplicate = { actions.duplicate(p.folder, p.name + tr(" (copia)", " (copy)")) }, onDelete = { deleting = p })
            }
        }
    }

    if (chooser) {
        AlertDialog(
            onDismissRequest = { chooser = false },
            title = { Text(tr("Nuevo proyecto", "New project")) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    allTemplates.forEach { id ->
                        val info = templates.firstOrNull { it.id == id }
                        Row(
                            Modifier.fillMaxWidth().height(52.dp).clip(RoundedCornerShape(12.dp)).background(Palette.SurfaceHigh)
                                .clickable { chooser = false; creating = id }.padding(horizontal = 12.dp),
                            verticalAlignment = Alignment.CenterVertically,
                        ) {
                            Box(Modifier.size(28.dp).clip(RoundedCornerShape(8.dp)).background(Brush.linearGradient(TEMPLATE_COLORS[id] ?: TEMPLATE_COLORS["empty"]!!)))
                            Spacer(Modifier.width(12.dp))
                            Text(Genre.name(id), Modifier.weight(1f), fontWeight = FontWeight.SemiBold)
                            if (info != null) Text("${info.bpm.toInt()} BPM", color = Palette.TextDim, fontSize = 12.sp)
                        }
                    }
                }
            },
            confirmButton = { TextButton(onClick = { chooser = false }) { Text(tr("Cancelar", "Cancel")) } },
        )
    }
    creating?.let { tpl ->
        var name by remember(tpl) { mutableStateOf(Genre.name(tpl).takeIf { tpl != "empty" }?.let { "$it ${projects.size + 1}" } ?: tr("Proyecto", "Project") + " ${projects.size + 1}") }
        AlertDialog(
            onDismissRequest = { creating = null },
            title = { Text(Genre.name(tpl)) },
            text = {
                Column {
                    OutlinedTextField(name, { name = it.take(60) }, label = { Text(tr("Nombre", "Name")) }, singleLine = true)
                    templates.firstOrNull { it.id == tpl }?.let { info ->
                        Spacer(Modifier.height(8.dp))
                        Text(tr("Incluye: ", "Includes: ") + info.tracks.joinToString(" · ") + " · ${info.bpm.toInt()} BPM", color = Palette.TextDim, fontSize = 12.sp)
                    }
                }
            },
            confirmButton = { TextButton(onClick = { actions.create(name, tpl); creating = null }) { Text(tr("Crear", "Create"), fontWeight = FontWeight.Bold) } },
            dismissButton = { TextButton(onClick = { creating = null }) { Text(tr("Cancelar", "Cancel")) } },
        )
    }
    renaming?.let { p ->
        var name by remember(p.folder) { mutableStateOf(p.name) }
        AlertDialog(
            onDismissRequest = { renaming = null },
            title = { Text(tr("Renombrar", "Rename")) },
            text = { OutlinedTextField(name, { name = it.take(60) }, singleLine = true) },
            confirmButton = { TextButton(onClick = { actions.rename(p.folder, name); renaming = null }) { Text("OK") } },
            dismissButton = { TextButton(onClick = { renaming = null }) { Text(tr("Cancelar", "Cancel")) } },
        )
    }
    deleting?.let { p ->
        AlertDialog(
            onDismissRequest = { deleting = null },
            title = { Text(tr("¿Eliminar proyecto?", "Delete project?")) },
            text = { Text(tr("«${p.name}» y todo su audio se borrarán de este dispositivo. No se puede deshacer.", "“${p.name}” and all its audio will be deleted from this device. This cannot be undone.")) },
            confirmButton = { TextButton(onClick = { actions.delete(p.folder); deleting = null }) { Text(tr("Eliminar", "Delete"), color = Palette.Record) } },
            dismissButton = { TextButton(onClick = { deleting = null }) { Text(tr("Cancelar", "Cancel")) } },
        )
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(text, style = MaterialTheme.typography.titleSmall, color = Palette.TextDim, modifier = Modifier.padding(start = 20.dp, end = 20.dp, top = 24.dp, bottom = 10.dp))
}

private fun dateOf(ms: Long) = if (ms > 0) DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(ms)) else ""

@Composable
private fun ContinueCard(p: ProjectSummary, onOpen: () -> Unit) {
    Column(
        Modifier.padding(horizontal = 16.dp).fillMaxWidth().clip(RoundedCornerShape(20.dp))
            .background(Brush.linearGradient(listOf(Color(0xFF231A3D), Color(0xFF111827))))
            .border(1.dp, Palette.Violet.copy(alpha = 0.35f), RoundedCornerShape(20.dp))
            .clickable(onClick = onOpen).padding(16.dp),
    ) {
        Text(tr("CONTINUAR", "CONTINUE"), style = MaterialTheme.typography.labelSmall, color = Palette.Cyan)
        Spacer(Modifier.height(6.dp))
        Row(verticalAlignment = Alignment.CenterVertically) {
            Column(Modifier.weight(1f)) {
                Text(p.name, style = MaterialTheme.typography.titleLarge, maxLines = 1, overflow = TextOverflow.Ellipsis)
                Text("${dateOf(p.modifiedAt)} · ${"%.0f".format(p.bpm)} BPM · ${p.tracks} " + tr("pistas", "tracks"), color = Palette.TextDim, fontSize = 12.sp)
            }
            RoundButton(Icons.Default.PlayArrow, tr("Abrir", "Open"), onOpen, size = 52.dp, background = Palette.Violet, tint = Color.White)
        }
        Spacer(Modifier.height(12.dp))
        Thumbnail(p, Modifier.fillMaxWidth().height(56.dp))
    }
}

@Composable
private fun TemplateCard(id: String, info: TemplateInfo?, onClick: () -> Unit) {
    val colors = TEMPLATE_COLORS[id] ?: TEMPLATE_COLORS["empty"]!!
    Column(
        Modifier.width(148.dp).height(132.dp).clip(RoundedCornerShape(18.dp)).background(Brush.linearGradient(colors))
            .clickable(onClick = onClick).padding(14.dp),
        verticalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(if (info != null) "${info.bpm.toInt()} BPM" else tr("Libre", "Blank"), color = Color.White.copy(alpha = 0.8f), fontSize = 12.sp, fontWeight = FontWeight.Medium)
        Column {
            Text(Genre.name(id), color = Color.White, fontWeight = FontWeight.Bold, fontSize = 18.sp, maxLines = 1)
            Text(
                info?.tracks?.joinToString(" · ") ?: tr("4 pistas de audio", "4 audio tracks"),
                color = Color.White.copy(alpha = 0.75f), fontSize = 11.sp, maxLines = 2, overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@Composable
private fun ProjectRow(p: ProjectSummary, onOpen: () -> Unit, onRename: () -> Unit, onDuplicate: () -> Unit, onDelete: () -> Unit) {
    var menu by remember { mutableStateOf(false) }
    Row(
        Modifier.padding(horizontal = 16.dp, vertical = 5.dp).fillMaxWidth().clip(RoundedCornerShape(16.dp)).background(Palette.Surface)
            .clickable(onClick = onOpen).padding(start = 12.dp, top = 10.dp, bottom = 10.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Thumbnail(p, Modifier.size(64.dp, 48.dp).clip(RoundedCornerShape(10.dp)).background(Palette.Background))
        Spacer(Modifier.width(12.dp))
        Column(Modifier.weight(1f)) {
            Text(p.name, style = MaterialTheme.typography.titleMedium, maxLines = 1, overflow = TextOverflow.Ellipsis)
            Text(
                if (p.error != null) tr("Archivo de proyecto dañado", "Damaged project file")
                else listOfNotNull(dateOf(p.modifiedAt), "${"%.0f".format(p.bpm)} BPM", p.templateId.takeIf { it.isNotEmpty() && it != "empty" }?.let { Genre.name(it) }).joinToString(" · "),
                color = if (p.error != null) Palette.Record else Palette.TextDim, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
            )
        }
        Box {
            IconButton(onClick = { menu = true }, modifier = Modifier.size(Dims.Touch)) { Icon(Icons.Default.MoreVert, tr("Opciones", "Options"), tint = Palette.TextDim) }
            DropdownMenu(menu, { menu = false }) {
                DropdownMenuItem(text = { Text(tr("Renombrar", "Rename")) }, leadingIcon = { Icon(Icons.Default.Edit, null) }, onClick = { menu = false; onRename() }, enabled = p.error == null)
                DropdownMenuItem(text = { Text(tr("Duplicar", "Duplicate")) }, leadingIcon = { Icon(Icons.Default.ContentCopy, null) }, onClick = { menu = false; onDuplicate() }, enabled = p.error == null)
                DropdownMenuItem(text = { Text(tr("Eliminar", "Delete"), color = Palette.Record) }, leadingIcon = { Icon(Icons.Default.Delete, null, tint = Palette.Record) }, onClick = { menu = false; onDelete() })
            }
        }
    }
}

/** Miniatura de la sesión: una franja por pista con los tramos que tienen clips (datos reales del proyecto). */
@Composable
fun Thumbnail(p: ProjectSummary, modifier: Modifier) {
    Canvas(modifier.padding(4.dp)) {
        val rows = p.thumb.size.coerceAtLeast(1)
        val rowH = size.height / rows
        p.thumb.forEachIndexed { i, t ->
            val c = Color(t.color.toInt())
            drawRoundRect(c.copy(alpha = 0.12f), Offset(0f, i * rowH + 1f), Size(size.width, rowH - 2f), CornerRadius(3f))
            for ((a, b) in t.spans) {
                val x0 = a.coerceIn(0f, 1f) * size.width
                val x1 = b.coerceIn(0f, 1f) * size.width
                if (x1 > x0) drawRoundRect(c.copy(alpha = 0.85f), Offset(x0, i * rowH + 1f), Size(x1 - x0, rowH - 2f), CornerRadius(3f))
            }
        }
    }
}
