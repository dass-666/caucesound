#pragma once
// Compositor local de CAUCESOUND (misión 0.4, «Cauce Signature Sound»): genera arreglos editables para las
// plantillas y variaciones de cada parte. Sin red ni modelos: reglas musicales explícitas + semillas, así que el
// mismo plan y la misma semilla dan siempre las mismas notas (reproducible y comprobable en pruebas).
//
// Todo lo que produce son datos editables del proyecto: patrones de batería (con redobles) y notas MIDI en clips.
// La tonalidad y la progresión NO se infieren del audio: vienen del plan guardado en el proyecto.
#include "cs/Midi.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cs
{
struct SongSection
{
    std::string name;   // "Intro", "A", "B", "Break", "Final" (es también el nombre de los clips)
    int startBar = 0, bars = 4;
    float energy = 0;   // -1 (muy tranquila) .. +1 (muy enérgica), relativa a la energía global
};

struct MusicPlan
{
    std::string genre;            // "trap", "reggaeton", "hiphop", "boombap", "jazzrap", "darkhiphop", "pop", "electronic"
    int keyRoot = 9;              // clase de altura de la tónica (0 = Do ... 9 = La)
    bool minor = true;
    std::vector<int> progression; // grados de la escala (0..6), un acorde por compás, en ciclo
    float energy = 0;             // energía global -1..+1 (variación «más tranquila / más enérgica»)
    std::map<std::string, uint32_t> seeds;      // por parte: "drums", "bass", "chords", "melody", "pad"
    std::map<std::string, std::string> roles;   // id de pista -> parte
    std::map<std::string, std::string> patternIds; // clave de patrón ("main", "fill"...) -> id del patrón en la pista de batería
    std::vector<std::string> locked;            // pistas bloqueadas: las variaciones no las tocan
    // 0.8 — «Variar»: por parte, semilla de la mutación aplicada sobre la parte de su semilla base (0 = sin variar).
    // La identidad (ritmo base, acordes, motivo) sale de seeds; variants solo cambia detalles.
    std::map<std::string, uint32_t> variants;
    std::vector<SongSection> sections;
    // 0.9.2 — número de versión de la composición: sube con cada Variar/Regenerar aceptado (la interfaz lo muestra
    // como «Versión N»). Deshacer devuelve el plan anterior y, con él, el número anterior. Opcional en el JSON.
    int revision = 1;
    // 0.10 — «Signature 2»: 2 activa la composición mejorada (estructura Intro/Verso/Coro, acentos y notas fantasma
    // por género, remates por sección, bajo anclado al bombo). 1 = composición de siempre: los proyectos anteriores no
    // llevan este campo y se regeneran exactamente igual que antes.
    int style = 1;
    // 0.10 — Humanización 0..1 (0 = exacto, como hasta ahora): pequeñas diferencias deterministas de fuerza en todo y
    // de tiempo en las partes MIDI. Mismo plan = mismas notas; cambiar el valor no cambia qué notas suenan.
    float humanize = 0.0f;

    std::string toJson() const;
    static bool fromJson (const std::string& text, MusicPlan& out);
};

// Parte de batería de una sección: qué patrón (por clave) suena durante cuántos compases.
struct DrumSlot { std::string patternKey; int bars = 1; std::string clipName; };

namespace composer
{
// Escala de la tonalidad del plan (7 clases de altura desde la tónica).
std::vector<int> scale (const MusicPlan& p);
// Notas MIDI del acorde del compás 'bar' (tríada o cuatriada según el género), en el registro dado.
std::vector<int> chordAt (const MusicPlan& p, int bar, bool sevenths);

// Patrones de batería del plan, por clave ("intro", "main", "var", "varCrash", "fill", "break").
std::map<std::string, DrumPattern> drumPatterns (const MusicPlan& p, uint32_t seed);
// Disposición de patrones dentro de una sección.
std::vector<DrumSlot> drumLayout (const MusicPlan& p, const SongSection& s);

// Notas de una parte para una sección (start relativo al inicio de la sección). part: "bass", "chords",
// "melody" o "pad". Respetan la escala y el acorde de cada compás; la energía cambia densidad e intensidad.
std::vector<MidiNote> partNotes (const MusicPlan& p, const std::string& part, const SongSection& s, uint32_t seed, int beatsPerBar);

// Plan por defecto de cada plantilla ("" si el género no existe). style 2 = «Signature 2» (0.10).
MusicPlan defaultPlan (const std::string& genre, int style = 1);
// Tipo interno de una sección: las de «Signature 2» (Verso, Coro...) se componen con las reglas de A, B, Final y Break.
std::string sectionKind (const SongSection& s);

// 0.8 — Progresiones típicas del género (grados de su modo). «Regenerar» elige otra distinta de la actual.
// Familia rítmica (0-3) que usa la batería con esta semilla; 0 = el groove original del estilo.
int drumGroove (uint32_t seed);
std::vector<std::vector<int>> progressionBank (const std::string& genre);

// 0.8 — «Variar»: mutaciones que conservan la identidad. Sobre las notas de una parte (no tocan el primer golpe de
// cada compás ni la armonía) y sobre un patrón de batería (no tocan el bombo del 1 ni la caja de los tiempos 2 y 4).
void mutateNotes (const MusicPlan& p, const std::string& part, std::vector<MidiNote>& notes, uint32_t variant, int beatsPerBar);
void mutatePattern (const MusicPlan& p, DrumPattern& d, uint32_t variant, const std::string& key);

// Familias de género (para compartir reglas).
bool hipHopFamily (const std::string& genre); // hiphop, boombap, jazzrap, darkhiphop
} // namespace composer
} // namespace cs
