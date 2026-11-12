package com.caucesound.app.audio

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.media.AudioAttributes
import android.media.AudioDeviceCallback
import android.media.AudioDeviceInfo
import android.media.AudioFocusRequest
import android.media.AudioManager
import androidx.core.content.ContextCompat

/**
 * Interrupciones del sistema:
 *  - Pérdida del foco de audio (llamada, otra app reproduciendo): se detiene el transporte y la grabación.
 *  - ACTION_AUDIO_BECOMING_NOISY (se desconectan auriculares con cable o Bluetooth): se detiene para no
 *    sacar el audio de golpe por el altavoz.
 *  - Cambios de dispositivos: se notifican para mostrar el aviso y el estado (cable / Bluetooth / USB).
 * La reapertura del stream la hace el código nativo (AudioIO::onErrorAfterClose).
 */
class AudioGuard(private val context: Context, private val onInterrupt: (reason: String) -> Unit, private val onDevicesChanged: () -> Unit) {
    private val am = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private var focusRequest: AudioFocusRequest? = null

    private val noisy = object : BroadcastReceiver() {
        override fun onReceive(c: Context?, i: Intent?) {
            if (i?.action == AudioManager.ACTION_AUDIO_BECOMING_NOISY) onInterrupt("noisy")
        }
    }
    private val devices = object : AudioDeviceCallback() {
        override fun onAudioDevicesAdded(added: Array<out AudioDeviceInfo>?) = onDevicesChanged()
        override fun onAudioDevicesRemoved(removed: Array<out AudioDeviceInfo>?) = onDevicesChanged()
    }

    fun register() {
        ContextCompat.registerReceiver(context, noisy, IntentFilter(AudioManager.ACTION_AUDIO_BECOMING_NOISY), ContextCompat.RECEIVER_NOT_EXPORTED)
        am.registerAudioDeviceCallback(devices, null)
    }

    fun unregister() {
        runCatching { context.unregisterReceiver(noisy) }
        am.unregisterAudioDeviceCallback(devices)
        abandonFocus()
    }

    fun requestFocus(): Boolean {
        val req = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_MEDIA).setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setOnAudioFocusChangeListener { change ->
                if (change == AudioManager.AUDIOFOCUS_LOSS || change == AudioManager.AUDIOFOCUS_LOSS_TRANSIENT) onInterrupt("focus")
            }
            .build()
        focusRequest = req
        return am.requestAudioFocus(req) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
    }

    fun abandonFocus() {
        focusRequest?.let { am.abandonAudioFocusRequest(it) }
        focusRequest = null
    }

    /** Descripción de la salida actual: importa para la latencia (Bluetooth añade típicamente 100-300 ms). */
    fun outputRoute(): Route {
        val outs = am.getDevices(AudioManager.GET_DEVICES_OUTPUTS)
        fun has(vararg t: Int) = outs.any { it.type in t }
        return when {
            has(AudioDeviceInfo.TYPE_USB_DEVICE, AudioDeviceInfo.TYPE_USB_HEADSET) -> Route.USB
            has(AudioDeviceInfo.TYPE_WIRED_HEADPHONES, AudioDeviceInfo.TYPE_WIRED_HEADSET) -> Route.WIRED
            has(AudioDeviceInfo.TYPE_BLUETOOTH_A2DP, AudioDeviceInfo.TYPE_BLUETOOTH_SCO) -> Route.BLUETOOTH
            else -> Route.SPEAKER
        }
    }

    enum class Route { SPEAKER, WIRED, USB, BLUETOOTH }
}
