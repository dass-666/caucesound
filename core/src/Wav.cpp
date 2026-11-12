#include "cs/Wav.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#if defined(_WIN32)
 #include <io.h>
#else
 #include <unistd.h>
#endif

namespace cs
{
namespace
{
uint16_t rd16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
uint32_t rd32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
void wr16 (uint8_t* p, uint16_t v) { p[0] = (uint8_t) v; p[1] = (uint8_t) (v >> 8); }
void wr32 (uint8_t* p, uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = (uint8_t) (v >> (8 * i)); }

constexpr uint16_t kFmtPcm = 1, kFmtFloat = 3, kFmtExtensible = 0xFFFE;

void syncFile (std::FILE* f)
{
    std::fflush (f);
#if defined(_WIN32)
    _commit (_fileno (f));
#else
    fsync (fileno (f));
#endif
}

// Encabezado canónico de 44 bytes (PCM) o 46+ con fact para float; usamos siempre 44 bytes con formato 1 o 3.
void makeHeader (uint8_t* h, int rate, int ch, int bits, bool isFloat, uint64_t dataBytes)
{
    const uint32_t data32 = dataBytes > 0xFFFFFFFFull - 36 ? 0xFFFFFFFFu - 36 : (uint32_t) dataBytes;
    std::memcpy (h, "RIFF", 4); wr32 (h + 4, 36 + data32); std::memcpy (h + 8, "WAVE", 4);
    std::memcpy (h + 12, "fmt ", 4); wr32 (h + 16, 16);
    wr16 (h + 20, isFloat ? kFmtFloat : kFmtPcm); wr16 (h + 22, (uint16_t) ch);
    wr32 (h + 24, (uint32_t) rate); wr32 (h + 28, (uint32_t) (rate * ch * bits / 8));
    wr16 (h + 32, (uint16_t) (ch * bits / 8)); wr16 (h + 34, (uint16_t) bits);
    std::memcpy (h + 36, "data", 4); wr32 (h + 40, data32);
}

struct FileCloser { void operator() (std::FILE* f) const { if (f) std::fclose (f); } };
using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

bool readWholeFile (const std::string& path, std::vector<uint8_t>& out)
{
    FilePtr f (std::fopen (path.c_str(), "rb"));
    if (! f) return false;
    if (std::fseek (f.get(), 0, SEEK_END) != 0) return false;
    const long size = std::ftell (f.get());
    if (size < 0) return false;
    std::fseek (f.get(), 0, SEEK_SET);
    out.resize ((size_t) size);
    return size == 0 || std::fread (out.data(), 1, (size_t) size, f.get()) == (size_t) size;
}

// Un generador determinista para el dither (misma salida en todas las plataformas).
struct XorShift
{
    uint32_t s = 0x12345678u;
    float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s >> 8) * (1.0f / 16777216.0f); }
};
} // namespace

bool readWavFromMemory (const uint8_t* d, size_t size, AudioData& out, std::string& error, WavInfo* infoOut)
{
    if (size < 12 || std::memcmp (d, "RIFF", 4) != 0 || std::memcmp (d + 8, "WAVE", 4) != 0)
    { error = "no es un archivo WAV (RIFF/WAVE)"; return false; }

    WavInfo info;
    uint16_t fmtTag = 0;
    int blockAlign = 0;
    bool haveFmt = false;
    size_t dataPos = 0, dataLen = 0;
    bool haveData = false;

    size_t p = 12;
    while (p + 8 <= size)
    {
        const uint8_t* c = d + p;
        const uint32_t len = rd32 (c + 4);
        const size_t body = p + 8;
        if (std::memcmp (c, "fmt ", 4) == 0)
        {
            if (len < 16 || body + 16 > size) { error = "chunk fmt incompleto"; return false; }
            fmtTag = rd16 (d + body);
            info.channels = rd16 (d + body + 2);
            info.sampleRate = (int) rd32 (d + body + 4);
            blockAlign = rd16 (d + body + 12);
            info.bitsPerSample = rd16 (d + body + 14);
            if (fmtTag == kFmtExtensible)
            {
                if (len < 40 || body + 40 > size) { error = "WAVE_FORMAT_EXTENSIBLE incompleto"; return false; }
                fmtTag = rd16 (d + body + 24); // primeros 2 bytes del GUID del subformato
            }
            haveFmt = true;
        }
        else if (std::memcmp (c, "data", 4) == 0)
        {
            dataPos = body;
            const size_t remaining = size - body;
            // Un tamaño mayor que el archivo (o 0 / 0xFFFFFFFF de una grabación cortada) se corrige al real.
            if (len == 0 || len == 0xFFFFFFFFu || (size_t) len > remaining)
            {
                dataLen = remaining;
                info.headerRepaired = true;
            }
            else
                dataLen = len;
            haveData = true;
            break; // ignoramos lo que venga después de data
        }
        p = body + (size_t) len + (len & 1u); // relleno a par
    }

    if (! haveFmt) { error = "falta el chunk fmt"; return false; }
    if (! haveData) { error = "falta el chunk data"; return false; }
    if (info.channels < 1 || info.channels > 8) { error = "número de canales no soportado"; return false; }
    if (info.sampleRate < 8000 || info.sampleRate > 384000) { error = "frecuencia de muestreo no soportada"; return false; }

    const int bits = info.bitsPerSample;
    const bool isFloat = fmtTag == kFmtFloat;
    if (fmtTag != kFmtPcm && ! isFloat) { error = "codificación WAV no soportada (solo PCM y float)"; return false; }
    if (isFloat && bits != 32 && bits != 64) { error = "float de tamaño no soportado"; return false; }
    if (! isFloat && bits != 8 && bits != 16 && bits != 24 && bits != 32) { error = "profundidad de bits no soportada"; return false; }
    const int bytesPer = bits / 8;
    if (blockAlign != bytesPer * info.channels) { error = "blockAlign incoherente"; return false; }
    info.isFloat = isFloat;

    const Frame frames = (Frame) (dataLen / (size_t) blockAlign);
    if (dataLen % (size_t) blockAlign != 0) info.headerRepaired = true; // muestra final incompleta descartada
    info.frames = frames;

    AudioData a;
    a.sampleRate = info.sampleRate;
    a.allocate (info.channels, frames);
    const uint8_t* s = d + dataPos;
    for (Frame i = 0; i < frames; ++i)
        for (int ch = 0; ch < info.channels; ++ch, s += bytesPer)
        {
            float v = 0;
            if (isFloat)
            {
                if (bits == 32) { uint32_t u = rd32 (s); float f; std::memcpy (&f, &u, 4); v = f; }
                else { uint64_t u = (uint64_t) rd32 (s) | ((uint64_t) rd32 (s + 4) << 32); double x; std::memcpy (&x, &u, 8); v = (float) x; }
                if (! std::isfinite (v)) v = 0.0f; // nunca dejamos pasar NaN/inf al motor
            }
            else if (bits == 8) v = ((int) s[0] - 128) / 128.0f;
            else if (bits == 16) v = (int16_t) rd16 (s) / 32768.0f;
            else if (bits == 24) { int32_t x = (int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24) >> 8; v = (float) x / 8388608.0f; }
            else v = (float) ((double) (int32_t) rd32 (s) / 2147483648.0);
            a.channels[(size_t) ch][(size_t) i] = v;
        }

    out = std::move (a);
    if (infoOut) *infoOut = info;
    return true;
}

bool readWav (const std::string& path, AudioData& out, std::string& error, WavInfo* info)
{
    std::vector<uint8_t> bytes;
    if (! readWholeFile (path, bytes)) { error = "no se pudo leer el archivo"; return false; }
    return readWavFromMemory (bytes.data(), bytes.size(), out, error, info);
}

bool writeWav (const std::string& path, const AudioData& data, WavFormat fmt, std::string& error, WriteStats* stats)
{
    const int ch = data.numChannels();
    if (ch < 1) { error = "audio sin canales"; return false; }
    const int bits = fmt == WavFormat::Pcm16 ? 16 : fmt == WavFormat::Pcm24 ? 24 : 32;
    const int bytesPer = bits / 8;
    const Frame frames = data.numFrames();
    const uint64_t dataBytes = (uint64_t) frames * (uint64_t) ch * (uint64_t) bytesPer;
    if (dataBytes > 0xFFFFFFFFull - 64) { error = "archivo demasiado grande para WAV (4 GB)"; return false; }

    const std::string tmp = path + ".tmp";
    FilePtr f (std::fopen (tmp.c_str(), "wb"));
    if (! f) { error = "no se pudo crear " + tmp; return false; }
    uint8_t h[44];
    makeHeader (h, data.sampleRate, ch, bits, fmt == WavFormat::Float32, dataBytes);
    if (std::fwrite (h, 1, 44, f.get()) != 44) { error = "error de escritura"; return false; }

    XorShift rng;
    long clipped = 0;
    std::vector<uint8_t> buf;
    const Frame chunk = 4096;
    buf.resize ((size_t) (chunk * ch * bytesPer));
    for (Frame start = 0; start < frames; start += chunk)
    {
        const Frame n = std::min (chunk, frames - start);
        uint8_t* o = buf.data();
        for (Frame i = 0; i < n; ++i)
            for (int c = 0; c < ch; ++c, o += bytesPer)
            {
                float v = data.channels[(size_t) c][(size_t) (start + i)];
                if (! std::isfinite (v)) v = 0.0f;
                if (fmt == WavFormat::Float32)
                {
                    uint32_t u; std::memcpy (&u, &v, 4); wr32 (o, u);
                    continue;
                }
                if (fmt == WavFormat::Pcm16)
                {
                    const float dither = (rng.next() - rng.next()) / 32768.0f; // TPDF, 1 LSB pico
                    v += dither;
                }
                if (v > 1.0f || v < -1.0f) { ++clipped; v = v > 1.0f ? 1.0f : -1.0f; }
                if (fmt == WavFormat::Pcm16)
                {
                    // Misma escala que la lectura (/32768) para que ida y vuelta no cambie el nivel.
                    const long x = std::clamp (std::lround ((double) v * 32768.0), -32768L, 32767L);
                    wr16 (o, (uint16_t) (int16_t) x);
                }
                else
                {
                    const long x = std::clamp (std::lround ((double) v * 8388608.0), -8388608L, 8388607L);
                    o[0] = (uint8_t) x; o[1] = (uint8_t) (x >> 8); o[2] = (uint8_t) (x >> 16);
                }
            }
        const size_t bytes = (size_t) (n * ch * bytesPer);
        if (std::fwrite (buf.data(), 1, bytes, f.get()) != bytes) { error = "error de escritura (¿disco lleno?)"; std::remove (tmp.c_str()); return false; }
    }
    syncFile (f.get());
    if (std::fclose (f.release()) != 0) { error = "error al cerrar"; std::remove (tmp.c_str()); return false; }
    std::remove (path.c_str());
    if (std::rename (tmp.c_str(), path.c_str()) != 0) { error = "no se pudo renombrar el archivo final"; return false; }
    if (stats) stats->clippedSamples = clipped;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
WavStreamWriter::~WavStreamWriter() { close(); }

bool WavStreamWriter::open (const std::string& path, int sampleRate, int numChannels, std::string& error)
{
    close();
    file = std::fopen (path.c_str(), "wb");
    if (! file) { error = "no se pudo crear " + path; return false; }
    channels = numChannels; rate = sampleRate; written = 0;
    uint8_t h[44];
    makeHeader (h, rate, channels, 32, true, 0);
    if (std::fwrite (h, 1, 44, file) != 44) { error = "error de escritura"; std::fclose (file); file = nullptr; return false; }
    return true;
}

bool WavStreamWriter::appendInterleaved (const float* samples, Frame frames)
{
    if (! file) return false;
    const size_t n = (size_t) (frames * channels);
    // Little-endian en todas las plataformas objetivo (ARM64, x86-64); se comprueba en las pruebas.
    if (std::fwrite (samples, sizeof (float), n, file) != n) return false;
    written += frames;
    return true;
}

bool WavStreamWriter::updateHeader()
{
    if (! file) return false;
    const long here = std::ftell (file);
    uint8_t h[44];
    makeHeader (h, rate, channels, 32, true, (uint64_t) written * (uint64_t) channels * 4u);
    std::fseek (file, 0, SEEK_SET);
    const bool ok = std::fwrite (h, 1, 44, file) == 44;
    std::fseek (file, here, SEEK_SET);
    std::fflush (file);
    return ok;
}

bool WavStreamWriter::close()
{
    if (! file) return true;
    bool ok = updateHeader();
    syncFile (file);
    ok = std::fclose (file) == 0 && ok;
    file = nullptr;
    return ok;
}

bool repairWavHeader (const std::string& path, std::string& error, bool* changed)
{
    std::vector<uint8_t> bytes;
    if (! readWholeFile (path, bytes)) { error = "no se pudo leer"; return false; }
    AudioData tmp;
    WavInfo info;
    if (! readWavFromMemory (bytes.data(), bytes.size(), tmp, error, &info)) return false;
    if (changed) *changed = info.headerRepaired;
    if (! info.headerRepaired) return true;
    // Reescribe el archivo de forma atómica con el formato original.
    const WavFormat fmt = info.isFloat ? WavFormat::Float32 : info.bitsPerSample == 16 ? WavFormat::Pcm16 : WavFormat::Pcm24;
    if (! info.isFloat && info.bitsPerSample == 16)
    {
        // Evita re-dither: escribimos a 24 bits para no degradar (el valor de 16 bits cabe exacto).
        return writeWav (path, tmp, WavFormat::Pcm24, error);
    }
    return writeWav (path, tmp, fmt, error);
}
} // namespace cs
