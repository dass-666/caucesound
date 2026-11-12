#pragma once
// Modelo del proyecto (hilo de control). No contiene audio, solo referencias a archivos dentro de la carpeta
// del proyecto. Se serializa a JSON versionado; la carga valida todo antes de aceptar el archivo.
#include "cs/AudioData.h"
#include "cs/Midi.h"

#include <map>
#include <string>
#include <vector>

namespace cs
{
// v1: fase 1 (solo audio). v2: pistas de instrumento, patrones, clips MIDI, bucle y plantilla.
// Un proyecto v1 se lee sin cambios (los campos nuevos toman su valor por defecto) y, antes del primer guardado
// en v2, Session copia el original a project.v1.json (ver Session::openProject).
constexpr int kProjectFormatVersion = 2;

struct SourceRef
{
    std::string id;        // único
    std::string file;      // relativo a la carpeta del proyecto, p. ej. "audio/src_ab12.wav"
    std::string origin;    // nombre original (importación) o "recording"
    int channels = 1;
    Frame frames = 0;
};

struct InsertModel
{
    std::string id, type, state; // state: JSON opaco del procesador
    bool bypass = false;
};

struct ClipModel
{
    std::string id, sourceId, name;
    Frame start = 0;        // posición en el timeline
    Frame sourceOffset = 0; // inicio dentro del audio fuente
    Frame length = 0;
    Frame fadeIn = 0, fadeOut = 0;
    float gainDb = 0.0f;
};

struct InstrumentModel
{
    std::string type;   // "" (pista de audio), "cs.synth" o "cs.drums"
    std::string state;  // JSON del instrumento (parámetros)
    std::string preset; // último preset cargado (informativo)
};

struct TrackModel
{
    std::string id, name;
    std::string kind = "audio"; // "audio" | "synth" | "drums" | "bus" (retorno de efectos: entrada = envíos)
    float volumeDb = 0.0f; // -inf se guarda como -144
    float pan = 0.0f;
    bool mute = false, solo = false, armed = false;
    uint32_t color = 0xFF4FC3F7;
    std::vector<ClipModel> clips;
    std::vector<InsertModel> inserts;
    std::map<std::string, float> sends; // id de pista bus -> nivel lineal 0..1 (post-fader). Solo pistas no bus
    InstrumentModel instrument;
    std::vector<DrumPattern> patterns;     // solo pistas de batería
    std::vector<MidiClipModel> midiClips;  // pistas de instrumento
    const DrumPattern* findPattern (const std::string& id) const;
    DrumPattern* findPattern (const std::string& id);
};

struct ProjectModel
{
    int version = kProjectFormatVersion;
    std::string id, name;
    int sampleRate = 48000;
    double bpm = 120.0;
    int beatsPerBar = 4;
    bool metronome = false;
    float masterDb = 0.0f;
    Frame recordLatencyOffset = 0; // compensación aplicada a grabaciones nuevas (frames)
    int64_t createdAt = 0, modifiedAt = 0; // epoch ms
    std::string templateId;                // plantilla con la que se creó ("trap", "reggaeton"...), informativo
    std::string music;                     // plan musical del compositor (JSON: género, tonalidad, semillas, papeles); ver Composer.h
    bool loopEnabled = false;
    Tick loopStart = 0, loopEnd = 16 * kPpq; // 4 compases de 4/4
    std::vector<TrackModel> tracks;
    std::vector<SourceRef> sources;

    const SourceRef* findSource (const std::string& id) const;
    TrackModel* findTrack (const std::string& id);
    const TrackModel* findTrack (const std::string& id) const;
    ClipModel* findClip (const std::string& id, TrackModel** owner = nullptr);
    MidiClipModel* findMidiClip (const std::string& id, TrackModel** owner = nullptr);
    Frame contentEnd() const; // final del último clip (audio o MIDI), en frames
    Frame ticksToFrames (Tick t) const { return cs::ticksToFrames (t, bpm, sampleRate); }
};

std::string toJson (const ProjectModel& p, bool pretty = true);
// Devuelve false (y no toca 'out') si el JSON no es válido, la versión es futura o hay referencias rotas.
bool fromJson (const std::string& json, ProjectModel& out, std::string& error);

// Comprobaciones de coherencia (ids únicos, rangos, clips dentro de su fuente). Vacío = válido.
std::string validate (const ProjectModel& p);

float dbToGain (float db);
std::string newId (const char* prefix); // id aleatorio corto (no criptográfico)

// --- Archivos -----------------------------------------------------------------------------------------------------
// Guardado seguro: escribe project.json.tmp + fsync, mueve el actual a project.json.bak y renombra.
bool atomicWriteFile (const std::string& path, const std::string& content, std::string& error, bool keepBackup);
bool readFile (const std::string& path, std::string& out);
bool fileExists (const std::string& path);
bool makeDirs (const std::string& path);
bool removeFile (const std::string& path);
std::vector<std::string> listDir (const std::string& dir);
} // namespace cs
