#pragma once
// Puente con el núcleo de VocalShift (submódulo third_party/vocalshift-ai-studio fijado en 10e679c, rama
// feat/urban-spatial; su único tercero es Signalsmith Stretch/Linear, MIT). Solo procesamiento OFFLINE: la cadena
// tiene ~150 ms de latencia y no se ofrece como efecto en vivo. Nunca se llama desde el hilo de audio.
#include "cs/AudioData.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace cs::vocal
{
bool available();
const char* pinnedVersion();

// Parámetros que ve el usuario. Son los «macros» reales de VocalShift (VoiceParams): no hay controles inventados.
struct Params
{
    std::string preset;          // id del preset de fábrica del que se partió (informativo)
    float semitones = 7;         // -24..24
    float character = 65;        // 0..100 %
    float magic = 70;            // 0..100 %
    bool secondVoice = true;     // true: la voz original queda delante y se añade la procesada detrás
    float secondGainDb = -12;    // -60..+6 dB respecto a la original
    bool radical = false;        // modo «Transformación radical» (los formantes siguen al tono)
    bool spatial = true;         // motor espacial (solo actúa con segunda voz)
    // Avanzado (ajustes finos reales del motor)
    float formantOffset = 0;     // -12..12 st
    float brightnessOffset = 0;  // -12..12 dB
    float mixPercent = 100;      // solo sin segunda voz
    float pan = 0;               // -100..100, solo segunda voz
    float offsetMs = 0;          // 0..100, solo segunda voz

    std::string toJson() const;
    static bool fromJson (const std::string& text, Params& out); // valida rangos; false si algo no es finito
};

struct PresetInfo { std::string id, name, description; Params params; };
std::vector<PresetInfo> presets();

struct Stats
{
    int latencySamples = 0;      // latencia real de la cadena, descartada del principio (resultado alineado)
    Frame inputFrames = 0, outputFrames = 0, tailFrames = 0;
    float inputPeak = 0, outputPeak = 0;
    // Diferencia de nivel RMS (procesada − original, en dB) medida sobre la duración de la entrada, sin la cola.
    // Sirve para comparar A/B a volumen igualado: lo más fuerte tiende a parecer mejor. 0 si no se puede medir.
    float levelDb = 0;
    int nonFiniteEvents = 0;
    long clipAltered = 0;
    double seconds = 0;          // tiempo de proceso
};

// Recibe audio estéreo entrelazado ya alineado con la entrada. Devuelve false para abortar (p. ej. disco lleno).
using Sink = std::function<bool (const float* interleavedStereo, int frames)>;

// Procesa [from, from + length) de 'in' (mono o estéreo). La salida es siempre estéreo, empieza alineada con la
// entrada (latencia compensada) y añade la cola de reverb/eco hasta que decae (máx. kMaxTailSeconds).
// Bloqueante. progress 0..1; cancel se consulta en cada bloque. Devuelve "" si todo fue bien, o el error.
std::string process (const AudioData& in, Frame from, Frame length, const Params& p, const Sink& sink,
                     std::atomic<float>* progress, const std::atomic<bool>* cancel, Stats* stats, double tempoBpm = 0);
// tempoBpm: tempo del proyecto para el eco sincronizado del motor espacial (0 = desconocido).

constexpr double kMaxClipSeconds = 600.0; // límite de memoria/tiempo por toma (10 min)
constexpr double kMaxTailSeconds = 4.0;
} // namespace cs::vocal
