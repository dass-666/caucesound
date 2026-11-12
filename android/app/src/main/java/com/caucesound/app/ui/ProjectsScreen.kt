package com.caucesound.app.ui

import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Add
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.GraphicEq
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.ExperimentalMaterial3Api
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
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.caucesound.app.DawViewModel
import com.caucesound.app.R
import com.caucesound.app.data.ProjectSummary
import java.text.DateFormat
import java.util.Date

@OptIn(ExperimentalMaterial3Api::class, ExperimentalFoundationApi::class)
@Composable
fun ProjectsScreen(vm: DawViewModel) {
    val projects by vm.projects.collectAsState()
    val message by vm.messages.collectAsState()
    val snackbar = remember { SnackbarHostState() }
    var showNew by remember { mutableStateOf(false) }
    var toDelete by remember { mutableStateOf<ProjectSummary?>(null) }
    val trackName = stringResource(R.string.default_track_name)

    LaunchedEffect(message) { message?.let { snackbar.showSnackbar(it); vm.consumeMessage() } }

    Scaffold(
        containerColor = Palette.Background,
        snackbarHost = { SnackbarHost(snackbar) },
        topBar = {
            TopAppBar(
                title = { Text("CAUCESOUND", fontWeight = FontWeight.Bold) },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = Palette.Background),
            )
        },
        floatingActionButton = {
            ExtendedFloatingActionButton(onClick = { showNew = true }, icon = { Icon(Icons.Default.Add, null) }, text = { Text(stringResource(R.string.new_project)) })
        },
    ) { pad ->
        if (projects.isEmpty()) {
            Box(Modifier.fillMaxSize().padding(pad), contentAlignment = Alignment.Center) {
                Column(horizontalAlignment = Alignment.CenterHorizontally) {
                    Icon(Icons.Default.GraphicEq, null, Modifier.size(64.dp), tint = Palette.TextDim)
                    Spacer(Modifier.padding(8.dp))
                    Text(stringResource(R.string.no_projects), color = Palette.TextDim)
                }
            }
        } else {
            LazyColumn(Modifier.fillMaxSize().padding(pad), contentPadding = PaddingValues(16.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                items(projects, key = { it.folder }) { p ->
                    Row(
                        Modifier.fillMaxWidth()
                            .background(Palette.Surface, RoundedCornerShape(12.dp))
                            .combinedClickable(onClick = { vm.openProject(p.folder) }, onLongClick = { toDelete = p })
                            .padding(16.dp),
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(p.name, style = MaterialTheme.typography.titleMedium)
                            val date = if (p.modifiedAt > 0) DateFormat.getDateTimeInstance(DateFormat.MEDIUM, DateFormat.SHORT).format(Date(p.modifiedAt)) else ""
                            Text(
                                if (p.error != null) stringResource(R.string.project_damaged) else stringResource(R.string.project_meta, p.tracks, date),
                                color = if (p.error != null) Palette.Record else Palette.TextDim,
                                style = MaterialTheme.typography.bodySmall,
                            )
                        }
                        IconButton(onClick = { toDelete = p }) { Icon(Icons.Default.Delete, stringResource(R.string.delete), tint = Palette.TextDim) }
                    }
                }
            }
        }
    }

    if (showNew) {
        var name by remember { mutableStateOf("") }
        AlertDialog(
            onDismissRequest = { showNew = false },
            title = { Text(stringResource(R.string.new_project)) },
            text = { OutlinedTextField(name, { name = it.take(60) }, label = { Text(stringResource(R.string.project_name)) }, singleLine = true) },
            confirmButton = {
                TextButton(onClick = { showNew = false; vm.createProject(name) { i -> "$trackName $i" } }) { Text(stringResource(R.string.create)) }
            },
            dismissButton = { TextButton(onClick = { showNew = false }) { Text(stringResource(R.string.cancel)) } },
        )
    }
    toDelete?.let { p ->
        AlertDialog(
            onDismissRequest = { toDelete = null },
            title = { Text(stringResource(R.string.delete_project_title)) },
            text = { Text(stringResource(R.string.delete_project_text, p.name)) },
            confirmButton = { TextButton(onClick = { vm.deleteProject(p.folder); toDelete = null }) { Text(stringResource(R.string.delete), color = Palette.Record) } },
            dismissButton = { TextButton(onClick = { toDelete = null }) { Text(stringResource(R.string.cancel)) } },
        )
    }
    Spacer(Modifier.width(0.dp))
}
