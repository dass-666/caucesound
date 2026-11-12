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

// Instrumento: genera audio a partir de eventos MIDI (futuro piano roll / Solo AI Studio).
class Instrument : public AudioProcessor
{
public:
    virtual void handleMidi (const MidiEvent* events, int count) = 0; // hilo de audio, antes de process()
};

using ProcessorPtr = std::shared_ptr<AudioProcessor>;

// Registro de tipos. Fase 1: solo procesadores integrados en el binario (no hay carga dinámica).
ProcessorPtr createProcessor (const std::string& typeId);
std::vector<std::string> availableProcessorTypes();
} // namespace cs
