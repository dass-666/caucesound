#pragma once
// Puente con el núcleo de Solo AI (submódulo third_party/solo-ai-studio, commit fijado). Solo usa su parte offline
// de composición de melodías: nada de esto corre en el hilo de audio. Si el submódulo no está, available() = false
// y la interfaz no ofrece la función (no hay pantalla falsa).
#include "cs/AudioData.h"
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
    std::string type = "chorus";     // chorus (estribillo cantable), solo, interlude, intro, outro, background
    std::string character = "easy";  // solo para estribillos: easy (fácil de cantar), emotional, energetic
    double freedom = 35;             // 0..100
    int proposal = 0;                // 0 Sencilla, 1 Emotiva, 2 Rítmica, 3 Atrevida
    uint64_t seed = 1;
    // Misión 0.6 («cambiar la frase final»): si endingSeed != 0, los últimos endingBars compases se vuelven a componer
    // con esa semilla (MelodyExplorer::regenerateRegion) y el resto de la melodía se conserva tal cual.
    uint64_t endingSeed = 0;
    int endingBars = 2;
    uint64_t variation = 0;          // 0 = la propuesta tal cual; otro valor = variación relacionada (conserva el motivo
                                     // según 'freedom': 0-25 muy cerca, 26-50 misma identidad, 51-75 frases nuevas, 76+ libre)
};

struct MelodyResult
{
    bool ok = false;
    std::string error;
    std::string name;                // nombre de la propuesta, del propio Solo AI
    std::string description;         // qué la distingue (texto de Solo AI)
    double similarity = 0;           // parecido con el motivo de identidad (0..1); no mide calidad
    std::vector<MidiNote> notes;     // ticks relativos al inicio (mismo PPQ = 960)
    double inScale = 0;              // fracción de notas dentro de la escala (métrica de Solo AI)
    double strongChordTones = 0;     // fracción de notas fuertes que son notas del acorde
    double millis = 0;               // tiempo de cálculo
};

// Bloqueante: llamar desde un hilo de trabajo, nunca desde el de audio.
MelodyResult generateMelody (const MelodyRequest& req);

// Las cuatro propuestas A-D de una misma familia (mismo motivo, estrategias distintas). req.proposal se ignora.
struct Family { bool ok = false; std::string error; std::vector<MelodyResult> proposals; double diversity = 0; };
Family proposeFamily (const MelodyRequest& req);

// Análisis de un audio importado (BeatAnalyzer de Solo AI): tempo, tonalidad y acordes ESTIMADOS, cada uno con su
// confianza 0..1. No cambia nada del proyecto: la interfaz muestra el resultado y el usuario confirma o corrige.
struct Analysis
{
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;       // ambigüedades, en texto para el usuario
    double durationSec = 0, seconds = 0;     // duración analizada y tiempo de cálculo
    bool silent = false, percussiveOnly = false, tonal = false;
    double bpm = 0, bpmConfidence = 0;
    std::vector<std::pair<double, double>> bpmCandidates; // (bpm, puntuación), mejor primero; incluye mitad y doble
    double firstBeatSec = 0;
    int beats = 0, bars = 0;
    int keyRoot = -1;
    bool minor = false;
    double keyConfidence = 0;
    struct KeyCand { int root; bool minor; double score; };
    std::vector<KeyCand> keyCandidates;
    std::vector<ChordSpec> chords;           // uno por compás (vacío si no es fiable)
    std::vector<double> chordConfidence;
    double chordConfidenceMean = 0;
    std::string summary;
};
// Mezcla a mono [from, from + length) y analiza. Bloqueante (hilo de trabajo). Máximo 10 minutos.
Analysis analyze (const AudioData& audio, Frame from, Frame length);
} // namespace cs::solo
