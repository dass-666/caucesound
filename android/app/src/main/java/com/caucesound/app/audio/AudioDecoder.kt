package com.caucesound.app.audio

import android.content.Context
import android.media.AudioFormat
import android.media.MediaCodec
import android.media.MediaExtractor
import android.media.MediaFormat
import android.net.Uri
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Convierte un archivo de audio del usuario (MP3, AAC/M4A, FLAC, OGG, WAV...) en un WAV temporal que el
 * motor C++ puede importar. Usa los decodificadores del sistema (MediaCodec): no añade licencias ni costes,
 * pero los formatos disponibles dependen del dispositivo.
 * Los WAV se copian sin decodificar (el motor los lee directamente, incluidos 24 bits y float).
 */
object AudioDecoder {
    class DecodeException(msg: String) : Exception(msg)

    fun toWav(context: Context, uri: Uri, out: File): File {
        val resolver = context.contentResolver
        // ¿Es ya un WAV? (cabecera RIFF/WAVE)
        resolver.openInputStream(uri)?.use { input ->
            val head = ByteArray(12)
            val n = input.read(head)
            if (n == 12 && String(head, 0, 4, Charsets.US_ASCII) == "RIFF" && String(head, 8, 4, Charsets.US_ASCII) == "WAVE") {
                resolver.openInputStream(uri)!!.use { src -> out.outputStream().use { dst -> src.copyTo(dst) } }
                return out
            }
        } ?: throw DecodeException("no se pudo abrir el archivo")
        return decodeWithMediaCodec(context, uri, out)
    }

    private fun decodeWithMediaCodec(context: Context, uri: Uri, out: File): File {
        val extractor = MediaExtractor()
        extractor.setDataSource(context, uri, null)
        val track = (0 until extractor.trackCount).firstOrNull {
            extractor.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
        } ?: run { extractor.release(); throw DecodeException("el archivo no contiene una pista de audio") }
        extractor.selectTrack(track)
        val inFormat = extractor.getTrackFormat(track)
        val mime = inFormat.getString(MediaFormat.KEY_MIME)!!
        val codec = try { MediaCodec.createDecoderByType(mime) } catch (e: Exception) {
            extractor.release(); throw DecodeException("este dispositivo no puede decodificar $mime")
        }
        codec.configure(inFormat, null, null, 0)
        codec.start()

        var sampleRate = inFormat.getInteger(MediaFormat.KEY_SAMPLE_RATE)
        var channels = inFormat.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
        var pcmEncoding = AudioFormat.ENCODING_PCM_16BIT
        val writer = WavWriter16(out)
        val info = MediaCodec.BufferInfo()
        var inputDone = false
        var outputDone = false
        try {
            while (!outputDone) {
                if (!inputDone) {
                    val idx = codec.dequeueInputBuffer(10_000)
                    if (idx >= 0) {
                        val buf = codec.getInputBuffer(idx)!!
                        val size = extractor.readSampleData(buf, 0)
                        if (size < 0) {
                            codec.queueInputBuffer(idx, 0, 0, 0, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                            inputDone = true
                        } else {
                            codec.queueInputBuffer(idx, 0, size, extractor.sampleTime, 0)
                            extractor.advance()
                        }
                    }
                }
                val o = codec.dequeueOutputBuffer(info, 10_000)
                when {
                    o == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
                        val f = codec.outputFormat
                        sampleRate = f.getInteger(MediaFormat.KEY_SAMPLE_RATE)
                        channels = f.getInteger(MediaFormat.KEY_CHANNEL_COUNT)
                        if (f.containsKey(MediaFormat.KEY_PCM_ENCODING)) pcmEncoding = f.getInteger(MediaFormat.KEY_PCM_ENCODING)
                    }
                    o >= 0 -> {
                        val buf = codec.getOutputBuffer(o)!!
                        buf.position(info.offset)
                        buf.limit(info.offset + info.size)
                        writer.ensureFormat(sampleRate, channels)
                        writer.write(buf, pcmEncoding)
                        codec.releaseOutputBuffer(o, false)
                        if (info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0) outputDone = true
                    }
                }
            }
        } finally {
            runCatching { codec.stop() }
            codec.release()
            extractor.release()
            writer.close()
        }
        if (writer.frames == 0L) throw DecodeException("no se obtuvo audio del archivo")
        return out
    }
}

/** Escritor WAV PCM 16 bits mínimo (la cabecera se completa al cerrar). */
class WavWriter16(file: File) {
    private val raf = RandomAccessFile(file, "rw").apply { setLength(0); write(ByteArray(44)) }
    private var rate = 0
    private var ch = 0
    var frames = 0L
        private set

    fun ensureFormat(sampleRate: Int, channels: Int) {
        if (rate == 0) { rate = sampleRate; ch = channels }
        else if (rate != sampleRate || ch != channels) throw AudioDecoder.DecodeException("el formato cambió a mitad del archivo")
    }

    fun write(buf: ByteBuffer, encoding: Int) {
        val bytes: ByteArray = when (encoding) {
            AudioFormat.ENCODING_PCM_FLOAT -> {
                val fb = buf.order(ByteOrder.LITTLE_ENDIAN).asFloatBuffer()
                val out = ByteBuffer.allocate(fb.remaining() * 2).order(ByteOrder.LITTLE_ENDIAN)
                while (fb.hasRemaining()) out.putShort((fb.get().coerceIn(-1f, 1f) * 32767f).toInt().toShort())
                out.array()
            }
            else -> ByteArray(buf.remaining()).also { buf.get(it) }
        }
        raf.write(bytes)
        if (ch > 0) frames += bytes.size / (2L * ch)
    }

    fun close() {
        val dataBytes = frames * 2 * ch.coerceAtLeast(1)
        val h = ByteBuffer.allocate(44).order(ByteOrder.LITTLE_ENDIAN)
        h.put("RIFF".toByteArray()).putInt((36 + dataBytes).toInt()).put("WAVE".toByteArray())
        h.put("fmt ".toByteArray()).putInt(16).putShort(1).putShort(ch.toShort()).putInt(rate)
            .putInt(rate * ch * 2).putShort((ch * 2).toShort()).putShort(16)
        h.put("data".toByteArray()).putInt(dataBytes.toInt())
        raf.seek(0)
        raf.write(h.array())
        raf.close()
    }
}
