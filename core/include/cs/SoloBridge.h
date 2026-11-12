#pragma once
// Puente con el núcleo de Solo AI (submódulo third_party/solo-ai-studio, commit fijado). Solo usa su parte offline
// de composición de melodías: nada de esto corre en el hilo de audio. Si el submódulo no está, available() = false
// y la interfaz no ofrece la función (no hay pantalla falsa).
#include "cs/Midi.h"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace cs::solo
{
bool available();
const char* pinnedVersion(); // commit del submódulo con el que se compiló

struct ChordSpec { int root = 0; int quality = 0; }; // quality: 0 mayor, 1 menor, 2 disminuido, 3 aumentado

struct MelodyRequest
{
    int keyRoot = 9;                 // clase de altura de la tónica
    bool minor = true;
    std::vector<ChordSpec> chords;   // uno por compás (define la duración: bars = chords.size())
    double bpm = 120;
    std::string type = "chorus";     // "chorus" (estribillo cantable) o "solo"
    double freedom = 35;             // 0..100
    int proposal = 0;                // 0 Sencilla, 1 Emotiva, 2 Rítmica, 3 Atrevida
    uint64_t seed = 1;
};

struct MelodyResult
{
    bool ok = false;
    std::string error;
    std::string name;                // nombre de la propuesta, del propio Solo AI
    std::vector<MidiNote> notes;     // ticks relativos al inicio (mismo PPQ = 960)
    double inScale = 0;              // fracción de notas dentro de la escala (métrica de Solo AI)
    double strongChordTones = 0;     // fracción de notas fuertes que son notas del acorde
    double millis = 0;               // tiempo de cálculo
};

// Bloqueante: llamar desde un hilo de trabajo, nunca desde el de audio.
MelodyResult generateMelody (const MelodyRequest& req);
} // namespace cs::solo
