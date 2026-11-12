#pragma once
// Session: controlador de un proyecto abierto. Es la API que usan Android (JNI) y, en el futuro, iOS.
// Todas las funciones son del hilo de control y están serializadas internamente con un mutex.
// Ninguna se llama desde el hilo de audio.
#include "cs/Engine.h"
#include "cs/Project.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace cs
{
struct OpResult
{
    bool ok = true;
    std::string error;
    std::string id; // id creado (pista, clip...) cuando aplica
    static OpResult fail (std::string e) { OpResult r; r.ok = false; r.error = std::move (e); return r; }
};

struct LoadReport
{
    std::vector<std::string> missingSources;     // ids cuyo archivo falta o está dañado
    std::vector<std::string> recoveredRecordings; // grabaciones interrumpidas que se han recuperado
    bool usedBackup = false;                       // project.json estaba dañado o ausente
    std::string backupFileUsed;
};

class Recorder;

// Plantillas de proyecto (definidas en SessionMusic.cpp). La interfaz las muestra tal cual: lo que se anuncia es
// exactamente lo que se crea.
struct TemplateInfo
{
    std::string id;
    double bpm = 120;
    std::string kitId;
    std::vector<std::string> tracks; // nombres de pista en orden
};
std::vector<TemplateInfo> projectTemplates();

class Session
{
public:
    explicit Session (Engine& engine);
    ~Session();

    // --- Proyecto -------------------------------------------------------------------------------------------------
    // templateId: "" o "empty" (vacío), "trap", "reggaeton", "hiphop", "pop", "electronic" (ver Templates.cpp).
    OpResult createProject (const std::string& folder, const std::string& name, int sampleRate = 48000, const std::string& templateId = {});
    OpResult openProject (const std::string& folder, LoadReport* report = nullptr);
    OpResult save();
    void close();
    bool isOpen() const;
    bool isDirty() const;
    std::string folder() const;
    ProjectModel model() const;      // copia
    std::string stateJson() const;   // modelo + datos derivados para la interfaz

    // --- Pistas ---------------------------------------------------------------------------------------------------
    // kind: "audio" (por defecto), "synth" (Cauce Synth + clip MIDI vacío) o "drums" (Cauce Drums + patrón + clip).
    OpResult addTrack (const std::string& name = {}, const std::string& kind = "audio");
    OpResult removeTrack (const std::string& trackId);
    OpResult renameTrack (const std::string& trackId, const std::string& name);
    // Mezcla: no entra en deshacer (se aplica al instante por atómicos), sí marca el proyecto como modificado.
    OpResult setTrackVolumeDb (const std::string& trackId, float db);
    OpResult setTrackPan (const std::string& trackId, float pan);
    OpResult setTrackMute (const std::string& trackId, bool on);
    OpResult setTrackSolo (const std::string& trackId, bool on);
    OpResult setTrackArmed (const std::string& trackId, bool on);
    OpResult setMasterDb (float db);

    // --- Clips (con deshacer) ------------------------------------------------------------------------------------
    // Importa un WAV (lo convierte a la frecuencia del proyecto y lo copia a audio/). atFrame < 0 = posición actual.
    OpResult importWav (const std::string& path, const std::string& trackId, Frame atFrame, const std::string& displayName = {});
    OpResult moveClip (const std::string& clipId, Frame newStart, const std::string& newTrackId = {});
    // Recorta: nuevo inicio en el timeline y nueva duración; el contenido no se desplaza (recorte por la izquierda).
    OpResult trimClip (const std::string& clipId, Frame newStart, Frame newLength);
    OpResult splitClip (const std::string& clipId, Frame atTimelineFrame);
    OpResult deleteClip (const std::string& clipId);
    OpResult duplicateClip (const std::string& clipId);
    OpResult setClipFades (const std::string& clipId, Frame fadeIn, Frame fadeOut);
    OpResult setClipGain (const std::string& clipId, float gainDb);

    bool canUndo() const;
    bool canRedo() const;
    OpResult undo();
    OpResult redo();

    // --- Instrumentos (sin deshacer, como la mezcla: se aplican al instante) ----------------------------------
    OpResult setInstrumentParam (const std::string& trackId, const std::string& paramId, float value);
    OpResult loadSynthPreset (const std::string& trackId, const std::string& presetId);
    int activeVoices (const std::string& trackId) const; // para pruebas e indicadores

    // --- Patrones de batería (con deshacer) -----------------------------------------------------------------
    OpResult addPattern (const std::string& trackId, const std::string& copyFromId = {});
    OpResult setPatternStep (const std::string& trackId, const std::string& patternId, int lane, int step, int velocity);
    OpResult setPatternLength (const std::string& trackId, const std::string& patternId, int steps); // 16 <-> 32
    OpResult setPatternSwing (const std::string& trackId, const std::string& patternId, float swing);
    OpResult setPatternLanes (const std::string& trackId, const std::string& patternId, const std::vector<std::vector<int>>& lanes);
    OpResult renamePattern (const std::string& trackId, const std::string& patternId, const std::string& name);

    // --- Clips MIDI (con deshacer). Posiciones en ticks (kPpq por negra) ------------------------------------
    OpResult addMidiClip (const std::string& trackId, Tick start, Tick length, const std::string& patternId = {});
    OpResult moveMidiClip (const std::string& clipId, Tick start, const std::string& newTrackId = {});
    OpResult resizeMidiClip (const std::string& clipId, Tick start, Tick length);
    OpResult deleteMidiClip (const std::string& clipId);
    OpResult duplicateMidiClip (const std::string& clipId);
    OpResult setMidiClipPattern (const std::string& clipId, const std::string& patternId);
    OpResult setClipNotes (const std::string& clipId, std::vector<MidiNote> notes);
    OpResult quantizeClip (const std::string& clipId, Tick grid, float strength, bool lengthsToo);

    // --- Bucle --------------------------------------------------------------------------------------------------
    OpResult setLoop (bool on, Tick start, Tick end);

    // --- Efectos de inserción (Cauce FX), máx. 8 por pista, en serie tras el instrumento o el audio ------------
    OpResult addInsert (const std::string& trackId, const std::string& type);   // r.id = id de la inserción
    OpResult removeInsert (const std::string& insertId);
    OpResult setInsertParam (const std::string& insertId, const std::string& paramId, float value); // sin deshacer
    OpResult setInsertBypass (const std::string& insertId, bool bypass);
    OpResult moveInsert (const std::string& insertId, int newIndex);
    float insertMeter (const std::string& insertId) const; // reducción de ganancia del compresor (dB), si aplica

    // --- Tempo / metrónomo ---------------------------------------------------------------------------------------
    OpResult setTempo (double bpm, int beatsPerBar);
    OpResult setMetronome (bool on);
    OpResult setRecordLatencyOffset (Frame frames);

    // --- Grabación -----------------------------------------------------------------------------------------------
    // Empieza a grabar en la pista armada (o trackId). El transporte debe ponerse en marcha (play) aparte o con
    // startTransport = true. channels: 1 (mono) o 2.
    OpResult startRecording (const std::string& trackId, int channels, bool startTransport);
    // Detiene la grabación (y el transporte) y crea el clip. Devuelve el id del clip.
    OpResult stopRecording();
    bool isRecording() const;
    Frame recordingFrames() const;

    // --- Exportación ---------------------------------------------------------------------------------------------
    // Renderiza [0, final del contenido + tail) con el mismo motor. Debe llamarse con el transporte parado.
    // bitDepth: 16 (con dither), 24 o 32 (float). stats opcional: muestras recortadas por saturación.
    OpResult exportMix (const std::string& outPath, int bitDepth, Frame tailFrames = 0, long* clippedSamples = nullptr);

    // --- Formas de onda --------------------------------------------------------------------------------------------
    // Devuelve buckets pares (min, max) de la fuente en [from, to) (frames de la fuente).
    std::vector<float> peaks (const std::string& sourceId, Frame from, Frame to, int buckets) const;

    // Para pruebas: simula un cierre inesperado (no guarda, no finaliza grabaciones).
    void abandonForTest();

private:
    struct Source
    {
        std::shared_ptr<const AudioData> audio;
        std::vector<float> summaryMin, summaryMax; // por bloques de kPeakBlock frames
    };
    static constexpr int kPeakBlock = 256;

    void pushUndo();
    void rebuildGraph();
    TrackRT makeTrackRT (const TrackModel& t, int slot, bool offline, RenderGraph& g);
    std::shared_ptr<Instrument> instrumentFor (const TrackModel& t);
    void applyLoopLocked();
    void applyTemplateLocked (const std::string& templateId);
    TrackModel* instrumentTrack (const std::string& trackId, const char* kind, OpResult& err);
    void applyMixerParams();
    OpResult loadSourceLocked (const SourceRef& ref, bool* ok);
    std::shared_ptr<Source> makeSource (std::shared_ptr<const AudioData> audio) const;
    void recoverRecordingsLocked (LoadReport* report);
    std::string absPath (const std::string& rel) const;
    OpResult finishEditLocked();
    void touchLocked();

    Engine& engine;
    mutable std::recursive_mutex mtx;
    bool open = false, dirty = false;
    std::string dir;
    ProjectModel proj;
    std::map<std::string, std::shared_ptr<Source>> pool;
    std::map<std::string, ProcessorPtr> insertInstances;
    std::map<std::string, std::shared_ptr<Instrument>> instrumentInstances; // por id de pista
    std::vector<ProjectModel> undoStack, redoStack;
    std::unique_ptr<Recorder> recorder;
    std::string recordingSourceId, recordingTrackId;
};
} // namespace cs
