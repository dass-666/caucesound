#pragma once
// Modelo del proyecto (hilo de control). No contiene audio, solo referencias a archivos dentro de la carpeta
// del proyecto. Se serializa a JSON versionado; la carga valida todo antes de aceptar el archivo.
#include "cs/AudioData.h"

#include <string>
#include <vector>

namespace cs
{
constexpr int kProjectFormatVersion = 1;

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

struct TrackModel
{
    std::string id, name;
    float volumeDb = 0.0f; // -inf se guarda como -144
    float pan = 0.0f;
    bool mute = false, solo = false, armed = false;
    uint32_t color = 0xFF4FC3F7;
    std::vector<ClipModel> clips;
    std::vector<InsertModel> inserts;
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
    std::vector<TrackModel> tracks;
    std::vector<SourceRef> sources;

    const SourceRef* findSource (const std::string& id) const;
    TrackModel* findTrack (const std::string& id);
    const TrackModel* findTrack (const std::string& id) const;
    ClipModel* findClip (const std::string& id, TrackModel** owner = nullptr);
    Frame contentEnd() const; // final del último clip
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
