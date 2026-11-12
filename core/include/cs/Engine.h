#pragma once
// Motor de audio en tiempo real.
//
// Hilos:
//  - Hilo de control (UI / Session): construye un RenderGraph inmutable y lo publica con publish().
//  - Hilo de audio: process(). Toma el último grafo publicado con un intercambio atómico. Nunca libera memoria:
//    el grafo anterior va a una cola de basura que el hilo de control vacía en collectGarbage().
//  - Parámetros continuos (volumen, pan, mute, solo, transporte) se leen de atómicos, sin republicar el grafo.
//
// Garantías comprobadas por pruebas: 0 reservas de memoria en process(), salida idéntica bit a bit con cualquier
// tamaño de bloque, y la exportación offline usa exactamente el mismo código que la reproducción.
#include "cs/AudioData.h"
#include "cs/Midi.h"
#include "cs/Processor.h"
#include "cs/SpscRing.h"

#include <atomic>
#include <memory>
#include <vector>

namespace cs
{
constexpr int kMaxTracks = 32;

struct ClipRT
{
    const AudioData* data = nullptr; // nullptr = audio ausente (se renderiza silencio)
    Frame start = 0, length = 0, sourceOffset = 0;
    Frame fadeIn = 0, fadeOut = 0;
    float gain = 1.0f;
};

struct TrackRT
{
    int slot = 0;                       // índice en los parámetros atómicos
    std::vector<ClipRT> clips;          // ordenados por start
    std::vector<ProcessorPtr> inserts;  // ya preparados
    std::shared_ptr<Instrument> instrument;  // pistas de instrumento (Cauce Synth / Cauce Drums); ya preparado
    std::vector<SchedEvent> events;          // eventos MIDI planificados en frames absolutos, ordenados
    uint64_t eventsHash = 0;                 // huella de 'events' (calculada en el hilo de control)
    // Compensación de latencia (PDC): suma de latencySamples() de las inserciones. La calcula publish().
    // La pista lee su contenido (clips y eventos) adelantado esa cantidad, así su salida procesada queda alineada.
    Frame latency = 0;
};
uint64_t hashEvents (const std::vector<SchedEvent>& ev);

struct RenderGraph
{
    int sampleRate = 48000;
    double bpm = 120.0;
    int beatsPerBar = 4;
    std::vector<TrackRT> tracks;
    std::vector<std::shared_ptr<const AudioData>> keepAlive; // mantiene vivo el audio mientras el grafo exista
    Frame contentEnd = 0;
};

struct TrackParams
{
    std::atomic<float> gain { 1.0f };  // lineal
    std::atomic<float> pan { 0.0f };   // -1 .. +1 (ley de balance: centro = 0 dB en ambos lados)
    std::atomic<bool> mute { false }, solo { false };
    std::atomic<float> peak { 0.0f };  // pico del último bloque (medidor), escrito por el hilo de audio
};

class Engine
{
public:
    Engine();
    ~Engine();

    // --- Hilo de control ---------------------------------------------------------------------------------------
    // Reserva todos los buffers. maxBlock: bloque máximo esperado (bloques mayores se trocean internamente).
    void prepare (int sampleRate, int maxBlock);
    void publish (std::unique_ptr<RenderGraph> graph);
    void collectGarbage();
    int sampleRate() const { return rate; }

    TrackParams& trackParams (int slot) { return params[(size_t) slot]; }

    void setMasterGain (float linear) { masterGain.store (linear); }
    void setMetronome (bool on) { metronomeOn.store (on); }
    void setMetronomeGain (float linear) { metronomeGain.store (linear); }
    void setMonitoring (bool on, float gain = 1.0f) { monitorGain.store (on ? gain : 0.0f); }
    // Región de bucle [start, end) en frames. Se ignora mientras se graba (la toma es lineal).
    void setLoop (bool on, Frame start, Frame end);
    bool loopEnabled() const { return loopOn.load(); }

    // Eventos MIDI en vivo (pads, teclado, vista previa del piano roll) hacia la pista 'slot'. Cualquier hilo
    // de interfaz (los productores se serializan con un spinlock que el hilo de audio nunca toma). Se aplican al
    // principio del siguiente bloque. Devuelve false si la cola está llena.
    bool sendLive (int slot, uint8_t status, uint8_t data1, uint8_t data2);

    // --- Transporte (cualquier hilo) ---------------------------------------------------------------------------
    void play() { playing.store (true); }
    void stop() { playing.store (false); }
    bool isPlaying() const { return playing.load(); }
    void seek (Frame pos) { seekRequest.store (pos < 0 ? 0 : pos); }
    Frame position() const { return positionOut.load(); }
    float masterPeak() const { return masterPeakOut.load(); }
    long xrunsReportedByHost() const { return hostXruns.load(); }
    void reportXruns (long n) { hostXruns.store (n); }

    // --- Grabación ---------------------------------------------------------------------------------------------
    // El hilo de audio copia la entrada (mientras reproduce) a 'ring' en 'recChannels' canales intercalados.
    // recordStartPosition() pasa a >= 0 en cuanto se escribe la primera muestra.
    void startRecording (SpscRing<float>* ring, int recChannels);
    void stopRecording();
    bool isRecording() const { return recording.load(); }
    Frame recordStartPosition() const { return recStart.load(); }
    long recordDroppedFrames() const { return recDropped.load(); }

    // --- Hilo de audio -----------------------------------------------------------------------------------------
    // input: intercalado (puede ser nullptr), output: intercalado, outChannels 1 o 2.
    void process (const float* input, int inChannels, float* output, int outChannels, int frames);

    // --- Render offline (exportación): mismo camino de código, sin transporte compartido ----------------------
    // Renderiza [from, from+frames) en L/R planar. Usa el grafo publicado. No usar a la vez que process().
    void renderOffline (Frame from, float* left, float* right, Frame frames);

private:
    void renderBlock (const RenderGraph* g, Frame pos, bool rolling, float* L, float* R, int n);
    void renderInstrument (const TrackRT& t, Frame position, bool rolling, float* tl, float* tr, int n);
    void notesOff (const RenderGraph* g, bool hard);
    void loopNotesOff (const RenderGraph* g);
    // Estado del bucle para el bloque en curso (solo hilo de audio; renderOffline lo deja desactivado).
    bool blockLooping = false;
    Frame blockLoopStart = 0, blockLoopEnd = 0;
    void drainLive (const RenderGraph* g);
    void adoptPendingGraph();
    void snapSmoothersToTargets();

    int rate = 48000, maxBlock = 0;
    std::vector<TrackParams> params;
    std::atomic<RenderGraph*> pending { nullptr };
    RenderGraph* active = nullptr;
    SpscRing<RenderGraph*> trash { 256 };

    // Estado del hilo de audio
    std::vector<float> trackL, trackR, mixL, mixR, inScratch, recScratch;
    std::vector<float> smGainL, smGainR; // suavizado por pista (ganancia efectiva incl. mute/solo/pan)
    float smMaster = 1.0f;
    float smoothStep = 0.0f;
    std::vector<float> clickHi, clickLo;  // clics del metrónomo precalculados (acento / tiempo)
    Frame pos = 0;
    bool wasRolling = false;

    std::atomic<bool> playing { false };
    std::atomic<Frame> seekRequest { -1 }, positionOut { 0 };
    std::atomic<float> masterGain { 1.0f }, metronomeGain { 0.5f }, monitorGain { 0.0f }, masterPeakOut { 0.0f };
    std::atomic<bool> metronomeOn { false };
    std::atomic<long> hostXruns { 0 };

    std::atomic<bool> loopOn { false };
    std::atomic<Frame> loopStart { 0 }, loopEnd { 0 };
    bool prevRolling = false;
    struct LiveMidi { int slot; uint8_t status, d1, d2; };
    SpscRing<LiveMidi> live { 1024 };
    std::atomic_flag liveLock = ATOMIC_FLAG_INIT;

    std::atomic<SpscRing<float>*> recRing { nullptr };
    std::atomic<int> recChannels { 1 };
    std::atomic<bool> recording { false };
    std::atomic<Frame> recStart { -1 };
    std::atomic<long> recDropped { 0 };
};
} // namespace cs
