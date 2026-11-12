#pragma once
// Interfaz modular para efectos e instrumentos internos (plugins propios del DAW).
//
// Contrato de tiempo real:
//  - prepare() y loadState() se llaman fuera del hilo de audio y pueden reservar memoria.
//  - process() se llama en el hilo de audio: sin reservas de memoria, bloqueos, disco ni red.
//  - setParameter() puede llamarse desde cualquier hilo (implementarlo con atómicos).
//  - latencySamples() es fijo tras prepare() (el motor compensará la latencia en fases futuras).
// Los motores de VocalShift / Solo AI / Spatial Echo se integrarán como implementaciones de estas interfaces.
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace cs
{
struct ParameterInfo
{
    std::string id, name;
    float minValue = 0, maxValue = 1, defaultValue = 0;
};

class AudioProcessor
{
public:
    virtual ~AudioProcessor() = default;
    virtual std::string typeId() const = 0; // identificador estable, p. ej. "cs.gain"
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;
    virtual void reset() = 0;
    virtual int latencySamples() const { return 0; }
    virtual void process (float* const* io, int numChannels, int numFrames) = 0; // en el sitio, tiempo real

    virtual std::vector<ParameterInfo> parameters() const { return {}; }
    virtual void setParameter (int index, float value) { (void) index; (void) value; }
    virtual float getParameter (int index) const { (void) index; return 0; }

    virtual std::string saveState() const;          // por defecto: parámetros en JSON
    virtual bool loadState (const std::string& s);  // debe validar y no modificar nada si falla
};

struct MidiEvent
{
    int frameOffset = 0; // dentro del bloque
    uint8_t status = 0, data1 = 0, data2 = 0;
};

// Contexto de transporte que el motor ofrece a los procesadores (para efectos sincronizados, Solo AI Studio, etc.).
struct TransportInfo
{
    double sampleRate = 48000, bpm = 120;
    int beatsPerBar = 4;
    bool playing = false;
    int64_t positionFrames = 0;    // primera muestra del bloque actual
    double positionQuarters = 0;   // misma posición en negras
};

// Instrumento: genera audio a partir de eventos MIDI (Cauce Synth, Cauce Drums, futuro Solo AI Studio).
// El motor divide el bloque en cada evento, así que handleMidi() recibe eventos con frameOffset = 0 y
// process() SUMA su salida estéreo a io[0], io[1]. Resultado idéntico con cualquier tamaño de bloque.
class Instrument : public AudioProcessor
{
public:
    virtual void handleMidi (const MidiEvent* events, int count) = 0; // hilo de audio, antes de process()
    // hard = false: pasa todas las voces a release (stop, loop, seek). hard = true: silencio inmediato.
    virtual void allNotesOff (bool hard) = 0;
    virtual int activeVoices() const { return 0; }
    // Cola máxima tras el último Note Off (release, decaimiento), para la exportación.
    virtual int tailSamples() const { return 0; }
};

using ProcessorPtr = std::shared_ptr<AudioProcessor>;

// Registro de tipos. Fase 1: solo procesadores integrados en el binario (no hay carga dinámica).
ProcessorPtr createProcessor (const std::string& typeId);
std::vector<std::string> availableProcessorTypes();
} // namespace cs
