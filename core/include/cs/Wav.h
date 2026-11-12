#pragma once
// Lectura/escritura WAV (RIFF) sin dependencias.
// Lectura: PCM 8/16/24/32 bits, float 32/64, WAVE_FORMAT_EXTENSIBLE, 1..8 canales. Tolera chunks desconocidos,
// tamaños de chunk de datos erróneos (archivo truncado o grabación interrumpida) y relleno impar.
// Escritura: PCM 16 (con dither TPDF determinista), PCM 24 o float 32. Escritura atómica (tmp + rename).
#include "cs/AudioData.h"

#include <cstdio>
#include <string>

namespace cs
{
enum class WavFormat { Pcm16, Pcm24, Float32 };

struct WavInfo
{
    int sampleRate = 0, channels = 0, bitsPerSample = 0;
    bool isFloat = false;
    Frame frames = 0;
    bool headerRepaired = false; // el tamaño declarado no coincidía con el archivo
};

bool readWav (const std::string& path, AudioData& out, std::string& error, WavInfo* info = nullptr);
bool readWavFromMemory (const uint8_t* data, size_t size, AudioData& out, std::string& error, WavInfo* info = nullptr);

struct WriteStats { long clippedSamples = 0; };
// Escribe en path + ".tmp" y lo renombra al final: un fallo nunca deja un WAV a medias con el nombre final.
bool writeWav (const std::string& path, const AudioData& data, WavFormat fmt, std::string& error, WriteStats* stats = nullptr);

// Escritura incremental en float 32 para grabaciones. El encabezado se actualiza periódicamente
// (updateHeader) para que un cierre inesperado deje un archivo recuperable.
class WavStreamWriter
{
public:
    ~WavStreamWriter();
    bool open (const std::string& path, int sampleRate, int numChannels, std::string& error);
    bool appendInterleaved (const float* samples, Frame frames);
    bool updateHeader(); // reescribe tamaños y hace flush
    bool close();        // updateHeader + fsync + cierre
    Frame framesWritten() const { return written; }
    bool isOpen() const { return file != nullptr; }

private:
    std::FILE* file = nullptr;
    int channels = 0, rate = 0;
    Frame written = 0;
};

// Corrige los tamaños RIFF/data de un WAV cuyo encabezado quedó desactualizado (p. ej. grabación cortada).
// Devuelve true si el archivo es válido después de la reparación.
bool repairWavHeader (const std::string& path, std::string& error, bool* changed = nullptr);
} // namespace cs
