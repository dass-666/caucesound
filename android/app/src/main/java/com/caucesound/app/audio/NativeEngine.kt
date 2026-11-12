package com.caucesound.app.audio

/**
 * Puente con el motor C++ (core/ + Oboe). Los comandos usan el contrato JSON de cs::handleCommand,
 * idéntico al que usará la app de iOS. Todas las funciones son seguras desde cualquier hilo excepto
 * [command], que debe llamarse desde un único hilo de control (ver EditorViewModel).
 */
object NativeEngine {
    init {
        System.loadLibrary("caucesound")
    }

    external fun audioStart(): Boolean
    external fun audioStop()
    external fun setInputEnabled(on: Boolean): Boolean
    external fun audioStatus(): String
    external fun command(json: String): String
    external fun position(): Long
    external fun isPlaying(): Boolean
    external fun recordedFrames(): Long
    external fun meters(numTracks: Int): FloatArray
    external fun peaks(sourceId: String, from: Long, to: Long, buckets: Int): FloatArray
    /** MIDI en vivo hacia la pista 'slot' (índice): pads, teclado y vista previa. Sin bloqueo, cualquier hilo. */
    external fun liveMidi(slot: Int, status: Int, data1: Int, data2: Int): Boolean
}
