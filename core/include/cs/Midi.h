#pragma once
// Núcleo MIDI de CAUCESOUND (compartido Android / iOS / escritorio).
//
// Tiempo musical: ticks a kPpq por negra. Los clips MIDI y los patrones viven en ticks, así que siguen al tempo;
// el audio grabado vive en frames (no se estira al cambiar el BPM, igual que en la fase 1).
// Con tempo constante por proyecto: frame = round(tick * 60 * sampleRate / (bpm * kPpq)).
//
// Pensado para crecer: MidiNote lleva canal; el planificador trabaja con mensajes MIDI genéricos (status, data1,
// data2), así que Pitch Bend, Control Change, Aftertouch y automatizaciones se añadirán como nuevos tipos de evento
// sin cambiar el motor.
#include "cs/AudioData.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs
{
constexpr int kPpq = 960;               // ticks por negra
constexpr int kStepTicks = kPpq / 4;    // un paso del secuenciador = semicorchea
constexpr int kDrumLanes = 8;
constexpr int kDrumBaseNote = 36;       // carril 0 = nota 36 (C1, convención General MIDI para el bombo)

using Tick = int64_t;

struct MidiNote
{
    int key = 60;          // 0..127
    int velocity = 100;    // 1..127
    int channel = 0;       // 0..15
    Tick start = 0;        // relativo al inicio del clip
    Tick length = kStepTicks;
};

struct DrumPattern
{
    std::string id, name;
    int steps = 16;        // 16 o 32 (1 o 2 compases de 4/4)
    float swing = 0.0f;    // 0..1: retrasa los pasos pares hasta medio paso (0,33 ~ tresillo)
    // lanes[carril][paso] = velocidad (0 = apagado, 1..127)
    std::vector<std::vector<int>> lanes = std::vector<std::vector<int>> ((size_t) kDrumLanes, std::vector<int> (16, 0));
};

// Clip MIDI en el arreglo. Si patternId no está vacío, el clip reproduce ese patrón de batería en bucle durante
// toda su duración (encadenar patrones = poner varios clips seguidos). Si está vacío, reproduce 'notes'.
struct MidiClipModel
{
    std::string id, name, patternId;
    Tick start = 0, length = 4 * kPpq;
    std::vector<MidiNote> notes;  // ordenadas por inicio (se normaliza al guardar)
};

// Evento planificado en frames absolutos del timeline (lo consume el hilo de audio).
struct SchedEvent
{
    Frame frame = 0;
    uint8_t status = 0, data1 = 0, data2 = 0;
};

inline double framesPerTick (double bpm, int sampleRate) { return 60.0 * sampleRate / (bpm * kPpq); }
Frame ticksToFrames (Tick t, double bpm, int sampleRate);
Tick framesToTicks (Frame f, double bpm, int sampleRate);

// Cuantiza inicio (y opcionalmente duración) a la rejilla 'grid' (ticks). strength 0..1 (1 = completa).
void quantizeNotes (std::vector<MidiNote>& notes, Tick grid, float strength, bool lengthsToo);
void sortNotes (std::vector<MidiNote>& notes);

// Tick (relativo al patrón) en el que suena el paso 'step', con swing aplicado.
Tick stepTick (int step, float swing);

// Notas del clip como eventos absolutos: Note On / Note Off ordenados (a igual frame, Note Off antes que Note On,
// para que repetir una nota no la corte). Las notas que pasan del final del clip se acortan al final del clip.
// Las notas se recortan a la duración del clip; los patrones se repiten hasta llenarlo.
void appendClipEvents (const MidiClipModel& clip, const DrumPattern* pattern, double bpm, int sampleRate,
                       std::vector<SchedEvent>& out);
void sortEvents (std::vector<SchedEvent>& ev);

// Validación (vacío = válido).
std::string validateNote (const MidiNote& n);
std::string validatePattern (const DrumPattern& p);
} // namespace cs
