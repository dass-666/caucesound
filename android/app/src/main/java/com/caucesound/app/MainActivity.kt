package com.caucesound.app

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.core.content.ContextCompat
import com.caucesound.app.ui.CauceTheme
import com.caucesound.app.ui.EditorScreen
import com.caucesound.app.ui.ProjectsScreen

class MainActivity : ComponentActivity() {
    private val vm: DawViewModel by viewModels()
    private var afterPermission: (() -> Unit)? = null

    private val micPermission = registerForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) afterPermission?.invoke() else vm.toast(getString(R.string.msg_mic_denied))
        afterPermission = null
    }

    /** Ejecuta [action] solo con el permiso de micrófono concedido (lo pide si hace falta). */
    fun withMicPermission(action: () -> Unit) {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) action()
        else { afterPermission = action; micPermission.launch(Manifest.permission.RECORD_AUDIO) }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            CauceTheme {
                val state by vm.state.collectAsState()
                if (state == null) ProjectsScreen(vm)
                else EditorScreen(vm, state!!, requestMic = ::withMicPermission)
            }
        }
    }

    override fun onStart() {
        super.onStart()
        vm.startAudio()
    }

    override fun onStop() {
        super.onStop()
        // Fase 1: sin servicio en primer plano. Al salir de la app se detiene (y guarda) la grabación
        // para no perderla si el sistema cierra el proceso.
        if (vm.state.value?.recording == true) vm.stop()
        vm.saveIfDirty()
        vm.stopAudioIfIdle()
    }
}
