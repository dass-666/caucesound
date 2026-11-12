#pragma once
// Archivo MIDI estándar (SMF) para llevar la música a otro programa (misión 0.6, exportación para DAW).
//
// Formato 1 (varias pistas), división = kPpq ticks por negra. Pista 0: nombre de la canción, tempo y compás. Después,
// una pista por cada pista de instrumento que tenga notas, con su nombre. Los clips se exportan expandidos (los
// patrones de batería repetidos y los redobles salen como notas), exactamente como suenan en CAUCESOUND.
// Batería: canal 10 (índice 9), carril n → nota 36 + n (el bombo cae en la nota 36 de General MIDI; los demás
// carriles dependen del kit: sus nombres van en el manifiesto del paquete). Instrumentos melódicos: un canal por
// pista (1-9, 11-16, repitiendo si hay más de 15).
// Límite conocido: el proyecto tiene un solo tempo y un solo compás, y no hay controladores ni pitch bend.
#include "cs/Project.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cs
{
struct SmfTrackInfo
{
    std::string trackId, name;
    int index = 0;      // número de pista dentro del archivo (la 0 es la de tempo)
    int channel = 0;    // 0..15
    int notes = 0;
    bool drums = false;
};

struct SmfExport
{
    std::vector<uint8_t> bytes;
    std::vector<SmfTrackInfo> tracks;
    int totalNotes = 0;
};

// Texto solo ASCII: «Melodía» -> «Melodia», «Solo AI · A» -> «Solo AI - A». El estándar MIDI no define la codificación
// de los nombres y cada programa interpreta los acentos a su manera (comprobado con una herramienta externa), así que
// los nombres del archivo MIDI y los nombres de archivo van sin acentos. El nombre real queda en el manifiesto.
std::string asciiText (const std::string& text);

// onlyTrackId vacío = todas las pistas con notas.
SmfExport writeSmf (const ProjectModel& p, const std::string& onlyTrackId = {});

// Lector mínimo, para comprobar lo exportado (no es un importador general).
struct SmfNote { int64_t tick = 0, length = 0; int key = 0, velocity = 0, channel = 0; };
struct SmfParsedTrack { std::string name; std::vector<SmfNote> notes; };
struct SmfParsed
{
    int format = -1, division = 0;
    double bpm = 0;
    int numerator = 0, denominator = 0;
    std::vector<SmfParsedTrack> tracks;
};
bool parseSmf (const std::vector<uint8_t>& bytes, SmfParsed& out, std::string& error);
} // namespace cs
