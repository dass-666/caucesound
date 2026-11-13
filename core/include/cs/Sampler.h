#pragma once
// Cauce Sampler (misión 0.9): motor de instrumentos basados en muestras, nativo del motor C++17 (sin dependencias).
//
// Piezas:
//  - SampleBuffer: audio de una muestra en memoria (PCM 16 bits planar: la mitad de RAM que float), inmutable.
//  - SamplerRegion / SamplerProgram: el mapa de un instrumento (qué muestra suena para cada nota y fuerza). Es el
//    modelo de regiones de SFZ (formato abierto e interoperable) reducido a lo que el motor sabe reproducir.
//  - parseSfz(): lee un subconjunto documentado de SFZ (docs/CAUCE_SAMPLER.md). Lo que no entiende NO se ignora en
//    silencio: vuelve en 'warnings'.
//  - loadProgram(): carga las muestras de un mapa (hilo de control: lee disco, convierte la frecuencia y reserva).
//  - SamplerEngine: reproducción en tiempo real (hilo de audio): sin reservas, sin disco, sin bloqueos. Polifonía con
//    robo de voces sin clics, capas por velocity, round robin (secuencial y aleatorio determinista), ADSR, afinación,
//    panorama, ganancia, pedal de sustain (CC64), muestras de release, grupos de corte (off_by), bucle y modo mono
//    con glide.
//
// Contrato de hilos: un SamplerProgram publicado es inmutable y se comparte con shared_ptr; el motor nunca lo
// libera en el hilo de audio (la instancia que lo usa vive en el grafo, que se recoge en el hilo de control).
#include "cs/AudioData.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cs
{
struct SampleBuffer
{
    int sampleRate = 48000;
    int channels = 1;                 // 1 o 2
    std::vector<int16_t> left, right; // right vacío si es mono
    std::string file;                 // ruta de origen (diagnóstico)
    Frame frames() const { return (Frame) left.size(); }
    size_t bytes() const { return (left.size() + right.size()) * sizeof (int16_t); }
};

struct SamplerRegion
{
    std::string samplePath;                     // relativa a la carpeta del mapa (como en SFZ)
    std::shared_ptr<const SampleBuffer> sample; // nulo si falta (la región no suena)
    int loKey = 0, hiKey = 127, keyCenter = 60;
    int loVel = 1, hiVel = 127;
    float loRand = 0.0f, hiRand = 1.0f;          // round robin aleatorio (SFZ lorand/hirand)
    int seqLength = 1, seqPosition = 1;          // round robin secuencial (SFZ seq_length/seq_position)
    float tuneCents = 0.0f;                      // tune + transpose * 100
    float pitchKeytrack = 100.0f;                // cents por semitono (0 = no transportar, típico de batería)
    float gainDb = 0.0f, pan = 0.0f;             // pan -100..100 (SFZ)
    float ampVeltrack = 100.0f;                  // 0..100 %: cuánto baja el volumen con la fuerza dentro de la capa
    float attack = 0.0f, hold = 0.0f, decay = 0.0f, sustain = 100.0f, release = 0.0f; // segundos / % (SFZ ampeg_*)
    enum class Loop { NoLoop, OneShot, Continuous, Sustain };
    Loop loop = Loop::NoLoop;
    Frame loopStart = 0, loopEnd = 0;            // en frames de la muestra; loopEnd inclusive como en SFZ
    Frame offset = 0, end = -1;                  // end inclusive; -1 = hasta el final
    int group = 0, offBy = 0;                    // corte: al sonar el grupo 'offBy' esta región se apaga
    bool releaseTrigger = false;                 // SFZ trigger=release: suena al soltar la nota
};

struct SamplerProgram
{
    std::string id, name;
    std::vector<SamplerRegion> regions;
    int polyphony = 32;
    bool mono = false;
    float glideSeconds = 0.0f;
    float volumeDb = 0.0f;
    std::vector<std::string> missingSamples; // rutas que no se pudieron cargar (la región queda muda)
    std::vector<std::string> warnings;       // opcodes de SFZ no admitidos, valores recortados...
    size_t bytes() const;                    // RAM de las muestras (sin duplicar las compartidas)
};

// SFZ (subconjunto): <control> <global> <master> <group> <region>, default_path, #define/$VAR no. Devuelve false
// solo si el texto no se puede interpretar como SFZ; los opcodes desconocidos van a out.warnings.
bool parseSfz (const std::string& text, SamplerProgram& out, std::string& error);

// Caché de muestras: misma ruta y frecuencia = misma memoria (dos pistas con el mismo kit no duplican RAM).
class SampleCache
{
public:
    // Lee y convierte a 'sampleRate'. nullptr si falta o está dañada (error con el motivo). Hilo de control.
    std::shared_ptr<const SampleBuffer> get (const std::string& absPath, int sampleRate, std::string& error);
    size_t residentBytes();         // bytes de las muestras que siguen vivas en algún programa
    void purge();                   // olvida las entradas que ya nadie usa
private:
    struct Entry { std::string path; int rate; std::weak_ptr<const SampleBuffer> buf; };
    std::vector<Entry> entries;
};
SampleCache& sampleCache();

// Carga las muestras de 'program' (rutas relativas a baseDir) a la frecuencia del motor. maxBytes: si el total
// superaría el límite, no carga nada y devuelve false (política predecible: un instrumento entra entero o no entra).
bool loadProgramSamples (SamplerProgram& program, const std::string& baseDir, int sampleRate, size_t maxBytes, std::string& error);

// --- Reproducción en tiempo real ------------------------------------------------------------------------------------
constexpr int kSamplerMaxVoices = 64;

class SamplerEngine
{
public:
    // Hilo de control. Fija el programa (puede ser nulo = silencio) y la frecuencia. Sin reservas después.
    void prepare (std::shared_ptr<const SamplerProgram> program, double sampleRate);
    void reset();

    // Hilo de audio. rateMul: transposición extra (carril de batería con «pitch»); tag: etiqueta de la voz para
    // procesar por grupos (carriles de batería). decayScale < 1 acorta el sonido con un fundido (carril «decay»).
    void noteOn (int key, int velocity, int tag = 0, double rateMul = 1.0, float decayScale = 1.0f);
    void noteOff (int key);
    void controlChange (int cc, int value);
    void allNotesOff (bool hard);
    void releaseTag (int tag);  // fundido rápido de las voces con esa etiqueta (choke del charles)

    // Suma la salida estéreo de las voces con 'tag' (o todas si tag < 0) a L/R con las ganancias dadas.
    void process (float* L, float* R, int n, int tag = -1, float gainL = 1.0f, float gainR = 1.0f);
    int activeVoices() const;
    int stolenVoices() const { return stolen; } // diagnóstico: voces robadas desde prepare()
    int tailSamples() const;
    // Ajustes globales del instrumento (hilo de audio, antes de noteOn): multiplicador del release y sensibilidad a la
    // fuerza (0 = todas las notas igual de fuertes dentro de su capa, 1 = la del mapa).
    void setModulation (float releaseMul, float velSens) { relMul = releaseMul; velSensitivity = velSens; }

private:
    struct Voice
    {
        bool active = false, releasing = false, sustained = false, killing = false;
        int key = 0, vel = 0, tag = 0;
        const SamplerRegion* region = nullptr;
        const SampleBuffer* buf = nullptr;
        double pos = 0, rate = 1, targetRate = 1, glideK = 1;
        Frame endFrame = 0;
        double env = 0, relK = 0, decK = 0;
        int stage = 0;       // 0 attack, 1 hold, 2 decay, 3 sustain, 4 release
        double attackStep = 1, holdLeft = 0, sustainLevel = 1;
        float gainL = 1, gainR = 1;
        double fadeFrom = 1e18, fadeK = 0;  // decayScale
        float kill = 1, killStep = 0;
        uint64_t order = 0;
    };
    void startVoice (const SamplerRegion& r, int key, int vel, int tag, double rateMul, float decayScale);
    void startRelease (Voice& v);
    void kill (Voice& v);
    Voice* freeVoice();
    int logicalVoices() const;
    float nextRandom();

    std::shared_ptr<const SamplerProgram> prog;
    double sr = 48000;
    std::array<Voice, kSamplerMaxVoices> voices {};
    std::array<uint32_t, 128> seqCounter {};
    std::array<uint8_t, 128> noteVel {};
    bool pedal = false;
    uint64_t counter = 0;
    uint32_t rng = 0x2545F491u;
    int stolen = 0;
    float programGain = 1.0f;
    float relMul = 1.0f, velSensitivity = 1.0f;
};
} // namespace cs
