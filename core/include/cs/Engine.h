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

#include <cmath>
#include <atomic>
#include <memory>
#include <vector>

namespace cs
{
constexpr int kMaxTracks = 32;
constexpr int kMaxBuses = 8; // buses de efectos (envíos) y canales propios de piezas de batería (0.7: antes 4)
constexpr int kMaxAuxLanes = 8; // carriles de un instrumento que pueden ir a su propio canal (Cauce Drums: 8)

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
    // >= 0: pista bus (retorno de efectos). Su entrada es la suma de los envíos de las demás pistas; se procesa
    // después de todas ellas. Los envíos son post-fader (TrackParams::send[busIndex]).
    int busIndex = -1;
    // true: la pista no suena directamente, solo alimenta sus envíos (post-fader). Lo usa la exportación de retornos
    // de bus por separado; en la reproducción normal siempre es false.
    bool sendsOnly = false;
    // Channel Rack (0.7): carril del instrumento -> índice de bus que lo recibe como canal propio (-1 = sale por la
    // pista). El carril entra en ese bus antes del fader y las inserciones de la pista de batería (como un canal
    // del mezclador). Ignorado si la pista tiene latencia (PDC): el canal propio no está compensado.
    int laneBus[kMaxAuxLanes] = { -1, -1, -1, -1, -1, -1, -1, -1 };
    bool hasLaneRoutes() const { for (int b : laneBus) if (b >= 0) return true; return false; }
    // true: bus que es el canal propio de un carril; participa en el solo como una pista normal.
    bool laneChannel = false;
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
    std::atomic<float> send[kMaxBuses] = {}; // nivel de envío lineal (0..1) hacia cada bus, post-fader
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
    // Espera (acotada) a que el hilo de audio adopte el último grafo publicado y libera los anteriores. Devuelve false
    // si el audio no llegó a procesar en ese tiempo (flujo parado o hilo sin CPU): entonces el grafo anterior sigue
    // vivo y se liberará en el siguiente publish()/settle(). Sin esto, cerrar un proyecto dejaba sus instrumentos y
    // muestras en memoria hasta abrir el siguiente (0.10; ver TestSampler.cpp).
    bool settle (int timeoutMs);
    int sampleRate() const { return rate; }

    TrackParams& trackParams (int slot) { return params[(size_t) slot]; }

    void setMasterGain (float linear) { masterGain.store (linear); }
    void setMetronome (bool on) { metronomeOn.store (on); }
    void setMetronomeGain (float linear) { metronomeGain.store (linear); }
    void setMonitoring (bool on, float gain = 1.0f) { monitorGain.store (on ? gain : 0.0f); }
    // Modo bocinas (0.8): mientras se graba, la salida EN VIVO (beat + metrónomo) baja 'db' decibelios para que el
    // micrófono capte menos música. No toca faders, master, proyecto ni exportación; al parar vuelve sola con una
    // rampa (sin chasquidos). 0 = desactivado. Rango -30..0.
    // 0.9.2: db <= -100 = «grabar sin acompañamiento»: silencio total del beat y del metrónomo SOLO mientras se graba
    // (la cuenta previa sigue sonando; la mezcla, los faders y el proyecto no cambian). Entre -30 y 0: modo bocinas.
    void setRecordDuck (float db) { recordDuckGain.store (db >= 0.0f ? 1.0f : db <= -100.0f ? 0.0f : std::pow (10.0f, std::max (-30.0f, db) / 20.0f)); }
    float recordDuck() const { return recordDuckGain.load(); }
    // Región de bucle [start, end) en frames. Se ignora mientras se graba (la toma es lineal).
    void setLoop (bool on, Frame start, Frame end);
    bool loopEnabled() const { return loopOn.load(); }

    // Eventos MIDI en vivo (pads, teclado, vista previa del piano roll) hacia la pista 'slot'. Cualquier hilo
    // de interfaz (los productores se serializan con un spinlock que el hilo de audio nunca toma). Se aplican al
    // principio del siguiente bloque. Devuelve false si la cola está llena.
    bool sendLive (int slot, uint8_t status, uint8_t data1, uint8_t data2);

    // --- Transporte (cualquier hilo) ---------------------------------------------------------------------------
    void play() { playing.store (true); }
    void stop() { countInRequest.store (-1); playing.store (false); }
    // Arranca con cuenta previa: suenan clics durante 'frames' sin que el transporte avance ni se grabe nada; al
    // terminar, el transporte y la grabación empiezan exactamente en la posición actual. stop() la cancela.
    void playWithCountIn (Frame frames) { countInRequest.store (frames > 0 ? frames : -1); playing.store (true); }
    // Frames de cuenta previa que faltan (0 = no hay cuenta en curso). Para la interfaz.
    Frame countInRemaining() const { return countInOut.load (std::memory_order_relaxed); }
    bool isPlaying() const { return playing.load(); }
    void seek (Frame pos) { seekRequest.store (pos < 0 ? 0 : pos); }
    // Cambio de proyecto (abrir/crear/cerrar): transporte parado, cuenta previa cancelada, bucle desactivado,
    // posición al inicio (visible al instante para la interfaz aunque el stream esté parado) y MIDI en vivo
    // pendiente descartado (era para las pistas del proyecto anterior). Hilo de control.
    void resetTransport();
    Frame position() const { return positionOut.load(); }
    float masterPeak() const { return masterPeakOut.load(); }
    long xrunsReportedByHost() const { return hostXruns.load(); }
    // Carga DSP: tiempo de process() / duración del bloque (1,0 = sin margen). Media móvil (~1 s) y pico desde la
    // última lectura con takeDspLoadPeak(). blocksOverBudget: bloques que tardaron más que su duración.
    float dspLoadAverage() const { return dspLoadAvg.load (std::memory_order_relaxed); }
    float takeDspLoadPeak() { return dspLoadPeak.exchange (0.0f, std::memory_order_relaxed); }
    long blocksOverBudget() const { return dspOver.load (std::memory_order_relaxed); }
    void reportXruns (long n) { hostXruns.store (n); }
    // Diagnóstico: frames procesados por process() desde el arranque (si no avanza, el callback no se ejecuta) y
    // pico del máster del último bloque con el transporte en marcha.
    long long processedFrames() const { return processedOut.load (std::memory_order_relaxed); }

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
    // cancel: si se pone a true a mitad, deja de renderizar y devuelve false (el resto del búfer queda sin tocar).
    bool renderOffline (Frame from, float* left, float* right, Frame frames, const std::atomic<bool>* cancel = nullptr);

private:
    void renderBlock (const RenderGraph* g, Frame pos, bool rolling, float* L, float* R, int n);
    // busOffset: posición de tl dentro del bloque (para dirigir los carriles con canal propio a su bus).
    void renderInstrument (const TrackRT& t, Frame position, bool rolling, float* tl, float* tr, int n, int busOffset = 0);
    void notesOff (const RenderGraph* g, bool hard);
    void addSends (const TrackRT& t, const float* l, const float* r, int n, bool smooth);
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
    std::atomic<uint32_t> blocksSeen { 0 }; // latido del hilo de audio: settle() no espera si el audio está parado
    RenderGraph* active = nullptr;
    SpscRing<RenderGraph*> trash { 256 };

    // Estado del hilo de audio
    std::vector<float> trackL, trackR, mixL, mixR, inScratch, recScratch;
    std::vector<float> smGainL, smGainR; // suavizado por pista (ganancia efectiva incl. mute/solo/pan)
    std::vector<float> busL, busR;       // entrada de cada bus (kMaxBuses x maxBlock)
    std::vector<float> smSend;           // envío suavizado por pista y bus (kMaxTracks x kMaxBuses)
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
    std::atomic<float> dspLoadAvg { 0.0f }, dspLoadPeak { 0.0f };
    std::atomic<long> dspOver { 0 };
    std::atomic<long long> processedOut { 0 };
    std::atomic<uint32_t> liveEpoch { 0 }; // resetTransport() lo incrementa: se descartan eventos en vivo anteriores
    void noteDspLoad (double seconds, int frames);

    std::atomic<bool> loopOn { false };
    std::atomic<Frame> loopStart { 0 }, loopEnd { 0 };
    bool prevRolling = false;
    float duckNow = 1.0f;                                  // ganancia aplicada (rampa hacia recordDuckGain)
    std::atomic<float> recordDuckGain { 1.0f };
    std::atomic<Frame> countInRequest { -1 }, countInOut { 0 };
    Frame countInLeft = 0, countInTotal = 0; // solo hilo de audio
    struct LiveMidi { int slot; uint8_t status, d1, d2; uint32_t epoch; };
    SpscRing<LiveMidi> live { 1024 };
    std::atomic_flag liveLock = ATOMIC_FLAG_INIT;

    std::atomic<SpscRing<float>*> recRing { nullptr };
    std::atomic<int> recChannels { 1 };
    std::atomic<bool> recording { false };
    std::atomic<Frame> recStart { -1 };
    std::atomic<long> recDropped { 0 };
};
} // namespace cs
