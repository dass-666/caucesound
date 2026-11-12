#include "cs/Session.h"

#include "cs/Instruments.h"
#include "cs/Resampler.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

namespace cs
{
using nlohmann::json;

namespace
{
int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::system_clock::now().time_since_epoch()).count();
}
constexpr size_t kMaxUndo = 100;
constexpr int kProcessorMaxBlock = 4096;
const char* kProjectFile = "project.json";
} // namespace

// =================================================================================================================
// Recorder: vacía la cola del hilo de audio a disco en un hilo propio.
// =================================================================================================================
class Recorder
{
public:
    Recorder (Engine& e, std::string wavPath, std::string sidecar, json sidecarBase, int rate, int ch)
        : engine (e), path (std::move (wavPath)), sidecarPath (std::move (sidecar)), meta (std::move (sidecarBase)),
          sampleRate (rate), channels (ch), ring ((size_t) rate * (size_t) ch * 10) // 10 s de margen
    {
        drainBuf.resize ((size_t) rate * (size_t) ch / 10);
    }
    ~Recorder() { stop(); }

    bool start (std::string& error)
    {
        if (! writer.open (path, sampleRate, channels, error)) return false;
        engine.startRecording (&ring, channels);
        running.store (true);
        worker = std::thread ([this] { run(); });
        return true;
    }

    // Detiene la captura, escribe lo que quede en la cola y cierra el archivo.
    bool stop()
    {
        if (! worker.joinable()) return finished;
        engine.stopRecording();
        // El hilo de audio puede estar a mitad de un bloque: esperamos un poco antes del último vaciado.
        std::this_thread::sleep_for (std::chrono::milliseconds (30));
        running.store (false);
        worker.join();
        drain();
        writeSidecarIfReady();
        finished = writer.close();
        return finished;
    }

    Frame frames() const { return framesWritten.load(); }
    bool ioError() const { return failed.load(); }

    // Para pruebas de cierre inesperado: detiene el hilo sin finalizar el archivo.
    void abandon()
    {
        engine.stopRecording();
        running.store (false);
        if (worker.joinable()) worker.join();
        writer.updateHeader(); // simula lo que habría hecho la última actualización periódica
    }

private:
    void run()
    {
        auto lastHeader = std::chrono::steady_clock::now();
        while (running.load())
        {
            drain();
            writeSidecarIfReady();
            const auto now = std::chrono::steady_clock::now();
            if (now - lastHeader > std::chrono::seconds (1)) { writer.updateHeader(); lastHeader = now; }
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
        }
    }
    void drain()
    {
        size_t n;
        while ((n = ring.pop (drainBuf.data(), drainBuf.size() - drainBuf.size() % (size_t) channels)) > 0)
        {
            if (! writer.appendInterleaved (drainBuf.data(), (Frame) (n / (size_t) channels))) failed.store (true);
            framesWritten.store (writer.framesWritten());
        }
    }
    void writeSidecarIfReady()
    {
        if (sidecarWritten) return;
        const Frame s = engine.recordStartPosition();
        if (s < 0) return;
        meta["startPosition"] = s;
        std::string err;
        sidecarWritten = atomicWriteFile (sidecarPath, meta.dump(), err, false);
    }

    Engine& engine;
    std::string path, sidecarPath;
    json meta;
    int sampleRate, channels;
    SpscRing<float> ring;
    std::vector<float> drainBuf;
    WavStreamWriter writer;
    std::thread worker;
    std::atomic<bool> running { false }, failed { false };
    std::atomic<Frame> framesWritten { 0 };
    bool sidecarWritten = false, finished = false;
};

// =================================================================================================================
Session::Session (Engine& e) : engine (e) {}
Session::~Session() { close(); }

std::string Session::absPath (const std::string& rel) const { return dir + "/" + rel; }

bool Session::isOpen() const { std::lock_guard<std::recursive_mutex> l (mtx); return open; }
bool Session::isDirty() const { std::lock_guard<std::recursive_mutex> l (mtx); return dirty; }
std::string Session::folder() const { std::lock_guard<std::recursive_mutex> l (mtx); return dir; }
ProjectModel Session::model() const { std::lock_guard<std::recursive_mutex> l (mtx); return proj; }
bool Session::canUndo() const { std::lock_guard<std::recursive_mutex> l (mtx); return ! undoStack.empty(); }
bool Session::canRedo() const { std::lock_guard<std::recursive_mutex> l (mtx); return ! redoStack.empty(); }
bool Session::isRecording() const { std::lock_guard<std::recursive_mutex> l (mtx); return recorder != nullptr; }
Frame Session::recordingFrames() const { std::lock_guard<std::recursive_mutex> l (mtx); return recorder ? recorder->frames() : 0; }

void Session::touchLocked() { dirty = true; proj.modifiedAt = nowMs(); }

void Session::pushUndo()
{
    undoStack.push_back (proj);
    if (undoStack.size() > kMaxUndo) undoStack.erase (undoStack.begin());
    redoStack.clear();
    paramGestureKey.clear(); // cualquier otra edición cierra el gesto de parámetros abierto
}

void Session::paramUndoLocked (const std::string& key)
{
    if (key == paramGestureKey) return; // mismo gesto: el estado previo ya está guardado
    pushUndo();
    paramGestureKey = key;
}

void Session::endParamGesture()
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    paramGestureKey.clear();
}

// Deshacer/rehacer cambian el estado guardado de instrumentos y efectos; las instancias que ya suenan deben seguirlo
// (si no, se oiría una cosa y se guardaría otra). El kit de batería es estructural: se recrea la instancia.
void Session::syncInstancesLocked()
{
    paramGestureKey.clear();
    for (const auto& t : proj.tracks)
    {
        auto it = instrumentInstances.find (t.id);
        if (it != instrumentInstances.end() && it->second && ! t.instrument.state.empty() && it->second->saveState() != t.instrument.state)
        {
            if (t.instrument.type == "cs.drums") instrumentInstances.erase (it);
            else it->second->loadState (t.instrument.state);
        }
        for (const auto& ins : t.inserts)
        {
            auto fx = insertInstances.find (ins.id);
            if (fx != insertInstances.end() && fx->second && ! ins.state.empty() && fx->second->saveState() != ins.state)
                fx->second->loadState (ins.state);
        }
    }
}

OpResult Session::finishEditLocked()
{
    touchLocked();
    rebuildGraph();
    return {};
}

std::shared_ptr<Session::Source> Session::makeSource (std::shared_ptr<const AudioData> audio) const
{
    auto s = std::make_shared<Source>();
    s->audio = std::move (audio);
    const AudioData& a = *s->audio;
    const Frame frames = a.numFrames();
    const size_t blocks = (size_t) ((frames + kPeakBlock - 1) / kPeakBlock);
    s->summaryMin.assign (blocks, 0.0f);
    s->summaryMax.assign (blocks, 0.0f);
    for (size_t b = 0; b < blocks; ++b)
    {
        float mn = 0, mx = 0;
        const Frame from = (Frame) b * kPeakBlock, to = std::min (frames, from + kPeakBlock);
        for (const auto& ch : a.channels)
            for (Frame i = from; i < to; ++i) { mn = std::min (mn, ch[(size_t) i]); mx = std::max (mx, ch[(size_t) i]); }
        s->summaryMin[b] = mn;
        s->summaryMax[b] = mx;
    }
    return s;
}

// --- Grafo de render ----------------------------------------------------------------------------------------------
std::shared_ptr<Instrument> Session::instrumentFor (const TrackModel& t)
{
    if (t.instrument.type.empty()) return nullptr;
    auto& inst = instrumentInstances[t.id];
    if (inst && inst->typeId() != t.instrument.type) inst.reset();
    if (! inst)
    {
        inst = std::dynamic_pointer_cast<Instrument> (createProcessor (t.instrument.type));
        if (! inst) return nullptr; // tipo desconocido (proyecto de una versión futura)
        if (! t.instrument.state.empty()) inst->loadState (t.instrument.state);
        inst->prepare (proj.sampleRate, kProcessorMaxBlock);
    }
    return inst;
}

// Construye la pista de render. offline = true: instancias propias (clonadas por estado) para la exportación,
// para no compartir procesadores con el hilo de audio.
int Session::busIndexOf (const std::string& trackId) const
{
    int b = 0;
    for (const auto& x : proj.tracks)
        if (x.kind == "bus") { if (x.id == trackId) return b; ++b; }
    return -1;
}

void Session::applySendsTo (TrackParams& p, const TrackModel& t) const
{
    for (auto& v : p.send) v.store (0.0f);
    for (const auto& [busId, level] : t.sends)
    {
        const int b = busIndexOf (busId);
        if (b >= 0 && b < kMaxBuses) p.send[b].store (std::clamp (level, 0.0f, 1.0f));
    }
}

TrackRT Session::makeTrackRT (const TrackModel& t, int slot, bool offline, RenderGraph& g)
{
    TrackRT tr;
    tr.slot = slot;
    if (t.kind == "bus") tr.busIndex = busIndexOf (t.id);
    for (const auto& c : t.clips)
    {
        ClipRT rc;
        auto it = pool.find (c.sourceId);
        if (it != pool.end())
        {
            rc.data = it->second->audio.get();
            g.keepAlive.push_back (it->second->audio);
        }
        rc.start = c.start; rc.length = c.length; rc.sourceOffset = c.sourceOffset;
        rc.fadeIn = c.fadeIn; rc.fadeOut = c.fadeOut; rc.gain = dbToGain (c.gainDb);
        tr.clips.push_back (rc);
    }
    std::sort (tr.clips.begin(), tr.clips.end(), [] (const ClipRT& a, const ClipRT& b) { return a.start < b.start; });
    for (const auto& ins : t.inserts)
    {
        if (ins.bypass) continue;
        if (offline)
        {
            ProcessorPtr fx = createProcessor (ins.type);
            if (! fx) continue;
            auto live = insertInstances.find (ins.id);
            fx->loadState (live != insertInstances.end() && live->second ? live->second->saveState() : ins.state);
            fx->prepare (proj.sampleRate, 1024);
            tr.inserts.push_back (fx);
            continue;
        }
        auto& inst = insertInstances[ins.id];
        if (! inst)
        {
            inst = createProcessor (ins.type);
            if (! inst) continue; // tipo desconocido (p. ej. proyecto de una versión con más plugins)
            if (! ins.state.empty()) inst->loadState (ins.state);
            inst->prepare (proj.sampleRate, kProcessorMaxBlock);
        }
        tr.inserts.push_back (inst);
    }
    if (! t.instrument.type.empty())
    {
        if (offline)
        {
            auto live = instrumentInstances.find (t.id);
            auto inst = std::dynamic_pointer_cast<Instrument> (createProcessor (t.instrument.type));
            if (inst)
            {
                inst->loadState (live != instrumentInstances.end() && live->second ? live->second->saveState() : t.instrument.state);
                inst->prepare (proj.sampleRate, 1024);
                tr.instrument = inst;
            }
        }
        else tr.instrument = instrumentFor (t);
        if (tr.instrument)
        {
            for (const auto& c : t.midiClips)
                appendClipEvents (c, c.patternId.empty() ? nullptr : t.findPattern (c.patternId), proj.bpm, proj.sampleRate, tr.events);
            sortEvents (tr.events);
            tr.eventsHash = hashEvents (tr.events);
        }
    }
    return tr;
}

void Session::rebuildGraph()
{
    auto g = std::make_unique<RenderGraph>();
    g->sampleRate = proj.sampleRate;
    g->bpm = proj.bpm;
    g->beatsPerBar = proj.beatsPerBar;
    g->contentEnd = proj.contentEnd();
    int slot = 0;
    for (const auto& t : proj.tracks) g->tracks.push_back (makeTrackRT (t, slot++, false, *g));
    // Instancias de pistas borradas: fuera de la caché (el grafo anterior las mantiene vivas mientras haga falta).
    for (auto it = instrumentInstances.begin(); it != instrumentInstances.end();)
        it = proj.findTrack (it->first) ? std::next (it) : instrumentInstances.erase (it);
    applyMixerParams();
    engine.publish (std::move (g));
}

void Session::applyLoopLocked()
{
    engine.setLoop (proj.loopEnabled, proj.ticksToFrames (proj.loopStart), proj.ticksToFrames (proj.loopEnd));
}

void Session::applyMixerParams()
{
    for (size_t i = 0; i < proj.tracks.size() && i < (size_t) kMaxTracks; ++i)
    {
        const auto& t = proj.tracks[i];
        TrackParams& p = engine.trackParams ((int) i);
        p.gain.store (dbToGain (t.volumeDb));
        p.pan.store (t.pan);
        p.mute.store (t.mute);
        p.solo.store (t.solo);
        applySendsTo (p, t);
    }
    engine.setMasterGain (dbToGain (proj.masterDb));
    engine.setMetronome (proj.metronome);
    applyLoopLocked();
}

// --- Proyecto -----------------------------------------------------------------------------------------------------
OpResult Session::createProject (const std::string& folderPath, const std::string& name, int sampleRate, const std::string& templateId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    close();
    if (! makeDirs (folderPath + "/audio")) return OpResult::fail ("no se pudo crear la carpeta del proyecto");
    if (fileExists (folderPath + "/" + kProjectFile)) return OpResult::fail ("ya existe un proyecto en esa carpeta");
    dir = folderPath;
    proj = ProjectModel();
    proj.id = newId ("prj");
    proj.name = name;
    proj.sampleRate = sampleRate;
    proj.createdAt = proj.modifiedAt = nowMs();
    open = true;
    dirty = true;
    applyTemplateLocked (templateId);
    rebuildGraph();
    OpResult r = save();
    r.id = proj.id;
    return r;
}

OpResult Session::loadSourceLocked (const SourceRef& ref, bool* ok)
{
    AudioData a;
    std::string err;
    WavInfo info;
    *ok = false;
    if (! readWav (absPath (ref.file), a, err, &info)) return OpResult::fail (err);
    if (a.sampleRate != proj.sampleRate) a = resample (a, proj.sampleRate);
    pool[ref.id] = makeSource (std::make_shared<const AudioData> (std::move (a)));
    *ok = true;
    return {};
}

OpResult Session::openProject (const std::string& folderPath, LoadReport* report)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    close();
    LoadReport rep;
    ProjectModel p;
    std::string firstError;
    bool loaded = false;
    for (const char* suffix : { "", ".tmp", ".bak" })
    {
        const std::string file = folderPath + "/" + kProjectFile + suffix;
        std::string text, err;
        if (! readFile (file, text)) { if (firstError.empty() && *suffix == 0) firstError = "no se encontró project.json"; continue; }
        if (fromJson (text, p, err)) { loaded = true; if (*suffix) { rep.usedBackup = true; rep.backupFileUsed = file; } break; }
        if (firstError.empty()) firstError = err;
    }
    if (! loaded) return OpResult::fail ("no se pudo abrir el proyecto: " + firstError);

    if (p.version < kProjectFormatVersion)
    {
        // Migración: el primer guardado escribirá el formato nuevo. Antes conservamos el original intacto.
        const std::string legacy = folderPath + "/project.v" + std::to_string (p.version) + ".json";
        std::string text, err;
        if (! fileExists (legacy) && readFile (folderPath + "/" + kProjectFile, text)) atomicWriteFile (legacy, text, err, false);
        p.version = kProjectFormatVersion;
    }

    dir = folderPath;
    proj = std::move (p);
    open = true;
    dirty = rep.usedBackup;
    for (const auto& s : proj.sources)
    {
        // Solo cargamos fuentes que usa algún clip (el resto son restos de deshacer).
        bool used = false;
        for (const auto& t : proj.tracks) for (const auto& c : t.clips) used |= c.sourceId == s.id;
        if (! used) continue;
        bool ok = false;
        loadSourceLocked (s, &ok);
        if (! ok) rep.missingSources.push_back (s.id);
    }
    recoverRecordingsLocked (&rep);
    cleanStaleTempLocked();
    rebuildGraph();
    if (dirty) save();
    if (report) *report = rep;
    OpResult r;
    r.id = proj.id;
    return r;
}

void Session::recoverRecordingsLocked (LoadReport* report)
{
    for (const auto& name : listDir (dir + "/audio"))
    {
        const std::string suffix = ".rec.json";
        if (name.size() <= suffix.size() || name.compare (name.size() - suffix.size(), suffix.size(), suffix) != 0) continue;
        const std::string sidecar = dir + "/audio/" + name;
        std::string text;
        if (! readFile (sidecar, text)) continue;
        const json m = json::parse (text, nullptr, false);
        if (m.is_discarded() || ! m.contains ("sourceId") || ! m.contains ("startPosition")) { removeFile (sidecar); continue; }
        const std::string sid = m["sourceId"].get<std::string>();
        const std::string rel = "audio/" + sid + ".wav";
        const std::string part = absPath (rel) + ".part";

        bool referenced = proj.findSource (sid) != nullptr;
        if (referenced) { removeFile (sidecar); removeFile (part); continue; }

        std::string err;
        if (fileExists (part))
        {
            if (! repairWavHeader (part, err)) { removeFile (sidecar); continue; }
            std::rename (part.c_str(), absPath (rel).c_str());
        }
        AudioData a;
        if (! readWav (absPath (rel), a, err) || a.numFrames() == 0) { removeFile (sidecar); continue; }

        SourceRef ref { sid, rel, "recording", a.numChannels(), a.numFrames() };
        proj.sources.push_back (ref);
        pool[sid] = makeSource (std::make_shared<const AudioData> (std::move (a)));

        TrackModel* t = proj.findTrack (m.value ("trackId", std::string()));
        if (! t)
        {
            if (proj.tracks.size() >= (size_t) kMaxTracks) { removeFile (sidecar); continue; }
            TrackModel nt;
            nt.id = newId ("trk");
            nt.name = "Recovered";
            proj.tracks.push_back (nt);
            t = &proj.tracks.back();
        }
        ClipModel c;
        c.id = newId ("clp");
        c.sourceId = sid;
        c.name = "Recovered";
        Frame start = m["startPosition"].get<Frame>() - m.value ("latencyOffset", (Frame) 0);
        if (start < 0) { c.sourceOffset = -start; start = 0; }
        c.start = start;
        c.length = ref.frames - c.sourceOffset;
        if (c.length > 0) t->clips.push_back (c);
        removeFile (sidecar);
        touchLocked();
        report->recoveredRecordings.push_back (sid);
    }
}

OpResult Session::save()
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    std::string err;
    if (! atomicWriteFile (dir + "/" + kProjectFile, toJson (proj), err, true)) return OpResult::fail (err);
    dirty = false;
    return {};
}

void Session::close()
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    // Trabajos offline del proyecto que se cierra: se cancelan y se espera a que suelten sus archivos. No hay bloqueo
    // mutuo: un trabajo nunca espera el cerrojo de la sesión, lo intenta y vuelve a mirar si lo han cancelado.
    jobs.cancelAll();
    jobs.waitIdle();
    if (recorder) stopRecording();
    if (open && dirty) save();
    // El motor sobrevive entre proyectos: además de parar, se deja el transporte como recién creado. Sin esto, el
    // proyecto siguiente heredaba la posición del anterior (p. ej. compás 40 tras reproducir A un rato) y sonaba
    // silencio más allá de su contenido hasta reiniciar la app (fallo de la 0.3.0; ver TestSwitch.cpp).
    engine.stop();
    engine.resetTransport();
    engine.publish (std::make_unique<RenderGraph>());
    open = false;
    dirty = false;
    pool.clear();
    insertInstances.clear();
    instrumentInstances.clear();
    undoStack.clear();
    redoStack.clear();
    proj = ProjectModel();
    dir.clear();
}

void Session::abandonForTest()
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    jobs.cancelAll();
    jobs.waitIdle();
    if (recorder) { recorder->abandon(); recorder.reset(); }
    // El motor sobrevive entre proyectos: además de parar, se deja el transporte como recién creado. Sin esto, el
    // proyecto siguiente heredaba la posición del anterior (p. ej. compás 40 tras reproducir A un rato) y sonaba
    // silencio más allá de su contenido hasta reiniciar la app (fallo de la 0.3.0; ver TestSwitch.cpp).
    engine.stop();
    engine.resetTransport();
    engine.publish (std::make_unique<RenderGraph>());
    open = false;
    dirty = false;
    pool.clear();
    insertInstances.clear();
    instrumentInstances.clear();
    undoStack.clear();
    redoStack.clear();
}

std::string Session::stateJson() const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    json j;
    j["open"] = open;
    if (! open) return j.dump();
    j["project"] = json::parse (toJson (proj, false));
    j["folder"] = dir;
    j["dirty"] = dirty;
    j["canUndo"] = ! undoStack.empty();
    j["canRedo"] = ! redoStack.empty();
    j["recording"] = recorder != nullptr;
    j["contentEnd"] = proj.contentEnd();
    json missing = json::array();
    for (const auto& t : proj.tracks)
        for (const auto& c : t.clips)
            if (pool.find (c.sourceId) == pool.end()) missing.push_back (c.id);
    j["missingClips"] = missing;
    j["missingPlugins"] = missingPlugins();
    return j.dump();
}

// --- Pistas -------------------------------------------------------------------------------------------------------
OpResult Session::addTrack (const std::string& name, const std::string& kind)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (proj.tracks.size() >= (size_t) kMaxTracks) return OpResult::fail ("límite de pistas alcanzado");
    if (kind != "audio" && kind != "synth" && kind != "drums" && kind != "bus") return OpResult::fail ("tipo de pista desconocido");
    if (kind == "bus")
    {
        int buses = 0;
        for (const auto& x : proj.tracks) buses += x.kind == "bus";
        if (buses >= kMaxBuses) return OpResult::fail ("límite de buses alcanzado");
    }
    pushUndo();
    TrackModel t;
    t.id = newId ("trk");
    t.kind = kind;
    t.name = name.empty() ? (kind == "drums" ? "Cauce Drums" : kind == "synth" ? "Cauce Synth" : kind == "bus" ? "Bus FX" : "Track " + std::to_string (proj.tracks.size() + 1)) : name;
    // Paleta de pistas de la identidad visual: violeta, azul eléctrico, cian, ámbar, verde, rosa, lima, naranja.
    static const uint32_t palette[] = { 0xFF8B5CF6, 0xFF3B82F6, 0xFF22D3EE, 0xFFF59E0B, 0xFF34D399, 0xFFF472B6, 0xFFA3E635, 0xFFFB923C };
    t.color = palette[proj.tracks.size() % 8];
    const Tick bar = (Tick) kPpq * proj.beatsPerBar;
    if (kind == "drums")
    {
        t.instrument.type = "cs.drums";
        t.instrument.state = "{\"kit\":0}";
        DrumPattern pt;
        pt.id = newId ("pat");
        pt.name = "A";
        t.patterns.push_back (pt);
        MidiClipModel c;
        c.id = newId ("mcl");
        c.name = "A";
        c.patternId = pt.id;
        c.start = 0;
        c.length = 4 * bar;
        t.midiClips.push_back (c);
    }
    else if (kind == "synth")
    {
        t.instrument.type = "cs.synth";
        t.instrument.state = synthPresetState ("soft_keys");
        t.instrument.preset = "soft_keys";
        MidiClipModel c;
        c.id = newId ("mcl");
        c.name = t.name;
        c.start = 0;
        c.length = 4 * bar;
        t.midiClips.push_back (c);
    }
    proj.tracks.push_back (t);
    finishEditLocked();
    OpResult r;
    r.id = t.id;
    return r;
}

OpResult Session::removeTrack (const std::string& id)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    auto it = std::find_if (proj.tracks.begin(), proj.tracks.end(), [&] (const TrackModel& t) { return t.id == id; });
    if (it == proj.tracks.end()) return OpResult::fail ("pista no encontrada");
    if (recorder) return OpResult::fail ("no se puede borrar una pista durante la grabación");
    pushUndo();
    proj.tracks.erase (it);
    for (auto& t : proj.tracks) t.sends.erase (id); // envíos hacia un bus borrado
    return finishEditLocked();
}

OpResult Session::renameTrack (const std::string& id, const std::string& name)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* t = proj.findTrack (id);
    if (! t) return OpResult::fail ("pista no encontrada");
    pushUndo();
    t->name = name;
    touchLocked();
    return {};
}

#define CS_MIXER_SETTER(fn, type, field, expr)                                    \
    OpResult Session::fn (const std::string& id, type v)                          \
    {                                                                             \
        std::lock_guard<std::recursive_mutex> l (mtx);                            \
        TrackModel* t = proj.findTrack (id);                                      \
        if (! t) return OpResult::fail ("pista no encontrada");                   \
        t->field = expr;                                                          \
        touchLocked();                                                            \
        applyMixerParams();                                                       \
        return {};                                                                \
    }
CS_MIXER_SETTER (setTrackVolumeDb, float, volumeDb, std::isfinite (v) ? std::clamp (v, -144.0f, 12.0f) : 0.0f)
CS_MIXER_SETTER (setTrackPan, float, pan, std::isfinite (v) ? std::clamp (v, -1.0f, 1.0f) : 0.0f)
CS_MIXER_SETTER (setTrackMute, bool, mute, v)
CS_MIXER_SETTER (setTrackSolo, bool, solo, v)
#undef CS_MIXER_SETTER

OpResult Session::setTrackSend (const std::string& id, const std::string& busId, float level)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* t = proj.findTrack (id);
    const TrackModel* b = proj.findTrack (busId);
    if (! t || ! b) return OpResult::fail ("pista no encontrada");
    if (b->kind != "bus" || t->kind == "bus") return OpResult::fail ("los envíos van de una pista a un bus");
    if (! std::isfinite (level)) return OpResult::fail ("valor no válido");
    level = std::clamp (level, 0.0f, 1.0f);
    if (level <= 0.0f) t->sends.erase (busId); else t->sends[busId] = level;
    touchLocked();
    applyMixerParams();
    return {};
}

OpResult Session::setTrackArmed (const std::string& id, bool on)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* t = proj.findTrack (id);
    if (! t) return OpResult::fail ("pista no encontrada");
    // Fase 1: una sola pista armada a la vez.
    for (auto& o : proj.tracks) o.armed = false;
    t->armed = on;
    touchLocked();
    return {};
}

OpResult Session::setMasterDb (float db)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    proj.masterDb = std::isfinite (db) ? std::clamp (db, -144.0f, 12.0f) : 0.0f;
    touchLocked();
    applyMixerParams();
    return {};
}

// --- Clips --------------------------------------------------------------------------------------------------------
OpResult Session::importWav (const std::string& path, const std::string& trackId, Frame atFrame, const std::string& displayName)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    TrackModel* t = trackId.empty() ? (proj.tracks.empty() ? nullptr : &proj.tracks.front()) : proj.findTrack (trackId);
    if (! t) return OpResult::fail ("pista no encontrada");
    if (t->kind == "bus") return OpResult::fail ("un bus no admite clips: importa en una pista de audio");
    const std::string tid = t->id;

    AudioData a;
    std::string err;
    if (! readWav (path, a, err)) return OpResult::fail ("no se pudo importar: " + err);
    if (a.numFrames() == 0) return OpResult::fail ("el archivo no contiene audio");
    if (a.numChannels() > 2) a.channels.resize (2); // fase 1: se conservan los dos primeros canales
    if (a.sampleRate != proj.sampleRate) a = resample (a, proj.sampleRate);

    const std::string sid = newId ("src");
    const std::string rel = "audio/" + sid + ".wav";
    if (! writeWav (absPath (rel), a, WavFormat::Float32, err)) return OpResult::fail ("no se pudo copiar el audio al proyecto: " + err);

    pushUndo();
    t = proj.findTrack (tid);
    SourceRef ref { sid, rel, displayName.empty() ? path.substr (path.find_last_of ("/\\") + 1) : displayName, a.numChannels(), a.numFrames() };
    proj.sources.push_back (ref);
    ClipModel c;
    c.id = newId ("clp");
    c.sourceId = sid;
    c.name = ref.origin;
    c.start = atFrame >= 0 ? atFrame : engine.position();
    c.length = a.numFrames();
    t->clips.push_back (c);
    pool[sid] = makeSource (std::make_shared<const AudioData> (std::move (a)));
    finishEditLocked();
    OpResult r;
    r.id = c.id;
    return r;
}

OpResult Session::moveClip (const std::string& clipId, Frame newStart, const std::string& newTrackId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* owner = nullptr;
    ClipModel* c = proj.findClip (clipId, &owner);
    if (! c) return OpResult::fail ("clip no encontrado");
    TrackModel* dest = newTrackId.empty() ? owner : proj.findTrack (newTrackId);
    if (! dest) return OpResult::fail ("pista destino no encontrada");
    if (dest->kind == "bus") return OpResult::fail ("un bus no admite clips");
    pushUndo();
    c = proj.findClip (clipId, &owner);
    ClipModel moved = *c;
    moved.start = std::max<Frame> (0, newStart);
    if (dest == owner) *c = moved;
    else
    {
        owner->clips.erase (std::find_if (owner->clips.begin(), owner->clips.end(), [&] (const ClipModel& x) { return x.id == clipId; }));
        proj.findTrack (dest->id)->clips.push_back (moved);
    }
    return finishEditLocked();
}

OpResult Session::trimClip (const std::string& clipId, Frame newStart, Frame newLength)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    ClipModel* c = proj.findClip (clipId);
    if (! c) return OpResult::fail ("clip no encontrado");
    const SourceRef* s = proj.findSource (c->sourceId);
    if (! s) return OpResult::fail ("fuente no encontrada");
    const Frame newOffset = c->sourceOffset + (newStart - c->start);
    if (newLength < 1 || newStart < 0 || newOffset < 0 || newOffset + newLength > s->frames)
        return OpResult::fail ("recorte fuera de los límites del audio");
    pushUndo();
    c = proj.findClip (clipId);
    c->clearProcess(); // recortar «acepta» la versión que suena (el original sigue en disco)
    c->sourceOffset = newOffset;
    c->start = newStart;
    c->length = newLength;
    c->fadeIn = std::min (c->fadeIn, newLength);
    c->fadeOut = std::min (c->fadeOut, newLength - c->fadeIn);
    return finishEditLocked();
}

OpResult Session::splitClip (const std::string& clipId, Frame at)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* owner = nullptr;
    ClipModel* c = proj.findClip (clipId, &owner);
    if (! c) return OpResult::fail ("clip no encontrado");
    if (at <= c->start || at >= c->start + c->length) return OpResult::fail ("el punto de corte no está dentro del clip");
    pushUndo();
    c = proj.findClip (clipId, &owner);
    c->clearProcess(); // dividir «acepta» la versión que suena
    ClipModel right = *c;
    const Frame leftLen = at - c->start;
    right.id = newId ("clp");
    right.start = at;
    right.sourceOffset = c->sourceOffset + leftLen;
    right.length = c->length - leftLen;
    right.fadeIn = 0;
    right.fadeOut = std::min (c->fadeOut, right.length);
    c->length = leftLen;
    c->fadeOut = 0;
    c->fadeIn = std::min (c->fadeIn, leftLen);
    owner->clips.push_back (right);
    finishEditLocked();
    OpResult r;
    r.id = right.id;
    return r;
}

OpResult Session::deleteClip (const std::string& clipId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* owner = nullptr;
    if (! proj.findClip (clipId, &owner)) return OpResult::fail ("clip no encontrado");
    pushUndo();
    proj.findClip (clipId, &owner);
    owner->clips.erase (std::find_if (owner->clips.begin(), owner->clips.end(), [&] (const ClipModel& x) { return x.id == clipId; }));
    return finishEditLocked();
}

OpResult Session::duplicateClip (const std::string& clipId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* owner = nullptr;
    ClipModel* c = proj.findClip (clipId, &owner);
    if (! c) return OpResult::fail ("clip no encontrado");
    pushUndo();
    c = proj.findClip (clipId, &owner);
    ClipModel d = *c;
    d.id = newId ("clp");
    d.start = c->start + c->length;
    owner->clips.push_back (d);
    finishEditLocked();
    OpResult r;
    r.id = d.id;
    return r;
}

OpResult Session::setClipFades (const std::string& clipId, Frame fadeIn, Frame fadeOut)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    ClipModel* c = proj.findClip (clipId);
    if (! c) return OpResult::fail ("clip no encontrado");
    if (fadeIn < 0 || fadeOut < 0 || fadeIn + fadeOut > c->length) return OpResult::fail ("fundidos más largos que el clip");
    pushUndo();
    c = proj.findClip (clipId);
    c->fadeIn = fadeIn;
    c->fadeOut = fadeOut;
    return finishEditLocked();
}

OpResult Session::setClipGain (const std::string& clipId, float gainDb)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    ClipModel* c = proj.findClip (clipId);
    if (! c) return OpResult::fail ("clip no encontrado");
    if (! std::isfinite (gainDb)) return OpResult::fail ("ganancia no válida");
    pushUndo();
    c = proj.findClip (clipId);
    c->gainDb = std::clamp (gainDb, -144.0f, 12.0f);
    return finishEditLocked();
}

OpResult Session::undo()
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (undoStack.empty()) return OpResult::fail ("nada que deshacer");
    if (recorder) return OpResult::fail ("no disponible durante la grabación");
    // La mezcla (volumen, pan, mute, solo) no forma parte del deshacer: se conserva la actual.
    std::map<std::string, TrackModel> mixer;
    for (const auto& t : proj.tracks) mixer[t.id] = t;
    redoStack.push_back (proj);
    proj = undoStack.back();
    undoStack.pop_back();
    for (auto& t : proj.tracks)
        if (auto it = mixer.find (t.id); it != mixer.end())
        { t.volumeDb = it->second.volumeDb; t.pan = it->second.pan; t.mute = it->second.mute; t.solo = it->second.solo; t.armed = it->second.armed; }
    syncInstancesLocked();
    return finishEditLocked();
}

OpResult Session::redo()
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (redoStack.empty()) return OpResult::fail ("nada que rehacer");
    if (recorder) return OpResult::fail ("no disponible durante la grabación");
    std::map<std::string, TrackModel> mixer;
    for (const auto& t : proj.tracks) mixer[t.id] = t;
    undoStack.push_back (proj);
    proj = redoStack.back();
    redoStack.pop_back();
    for (auto& t : proj.tracks)
        if (auto it = mixer.find (t.id); it != mixer.end())
        { t.volumeDb = it->second.volumeDb; t.pan = it->second.pan; t.mute = it->second.mute; t.solo = it->second.solo; t.armed = it->second.armed; }
    syncInstancesLocked();
    return finishEditLocked();
}

// --- Tempo --------------------------------------------------------------------------------------------------------
OpResult Session::setTempo (double bpm, int beatsPerBar)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! std::isfinite (bpm) || bpm < 20 || bpm > 400) return OpResult::fail ("tempo fuera de rango (20-400 BPM)");
    if (beatsPerBar < 1 || beatsPerBar > 16) return OpResult::fail ("compás no válido");
    pushUndo();
    proj.bpm = bpm;
    proj.beatsPerBar = beatsPerBar;
    return finishEditLocked();
}

OpResult Session::setMetronome (bool on)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    proj.metronome = on;
    engine.setMetronome (on);
    touchLocked();
    return {};
}

OpResult Session::setRecordLatencyOffset (Frame frames)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (frames < 0 || frames > proj.sampleRate) return OpResult::fail ("compensación fuera de rango (0-1 s)");
    proj.recordLatencyOffset = frames;
    touchLocked();
    return {};
}

// --- Grabación ----------------------------------------------------------------------------------------------------
OpResult Session::startRecording (const std::string& trackId, int channels, bool startTransport, int countInBars)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (recorder) return OpResult::fail ("ya se está grabando");
    const TrackModel* t = nullptr;
    if (! trackId.empty()) t = proj.findTrack (trackId);
    else for (const auto& x : proj.tracks) if (x.armed) t = &x;
    if (! t) return OpResult::fail ("no hay pista armada para grabar");
    if (t->kind == "bus") return OpResult::fail ("no se puede grabar en un bus");

    const std::string sid = newId ("rec");
    json meta = { { "sourceId", sid }, { "trackId", t->id }, { "channels", channels }, { "latencyOffset", proj.recordLatencyOffset },
                  { "sampleRate", proj.sampleRate } };
    recorder = std::make_unique<Recorder> (engine, absPath ("audio/" + sid + ".wav.part"), absPath ("audio/" + sid + ".rec.json"),
                                           meta, proj.sampleRate, std::clamp (channels, 1, 2));
    std::string err;
    if (! recorder->start (err)) { recorder.reset(); return OpResult::fail ("no se pudo iniciar la grabación: " + err); }
    recordingSourceId = sid;
    recordingTrackId = t->id;
    recordingCountIn = false;
    if (startTransport)
    {
        const int bars = std::clamp (countInBars, 0, 4);
        const Frame countIn = proj.bpm > 0 ? (Frame) std::llround ((double) bars * proj.beatsPerBar * 60.0 * proj.sampleRate / proj.bpm) : 0;
        recordingCountIn = countIn > 0;
        if (countIn > 0) engine.playWithCountIn (countIn); else engine.play();
    }
    OpResult r;
    r.id = sid;
    return r;
}

OpResult Session::stopRecording()
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! recorder) return OpResult::fail ("no se está grabando");
    engine.stop();
    const bool closed = recorder->stop();
    const bool ioErr = recorder->ioError();
    const Frame startPos = engine.recordStartPosition();
    const long dropped = engine.recordDroppedFrames();
    recorder.reset();

    const std::string sid = recordingSourceId;
    const std::string rel = "audio/" + sid + ".wav";
    const std::string part = absPath (rel) + ".part";
    const std::string sidecar = absPath ("audio/" + sid + ".rec.json");
    if (startPos < 0)
    {
        removeFile (part); removeFile (sidecar);
        // Parar durante la cuenta previa no es un error: simplemente no hay toma (resultado correcto, sin id).
        if (recordingCountIn) { recordingCountIn = false; return OpResult(); }
        return OpResult::fail ("no se recibió audio del micrófono");
    }
    if (std::rename (part.c_str(), absPath (rel).c_str()) != 0) return OpResult::fail ("no se pudo finalizar el archivo de grabación");
    AudioData a;
    std::string err;
    if (! readWav (absPath (rel), a, err) || a.numFrames() == 0)
    {
        removeFile (sidecar);
        return OpResult::fail ("grabación vacía o ilegible: " + err);
    }

    pushUndo();
    TrackModel* t = proj.findTrack (recordingTrackId);
    if (! t)
    {
        TrackModel nt; nt.id = newId ("trk"); nt.name = "Recording";
        proj.tracks.push_back (nt);
        t = &proj.tracks.back();
    }
    SourceRef ref { sid, rel, "recording", a.numChannels(), a.numFrames() };
    proj.sources.push_back (ref);
    ClipModel c;
    c.id = newId ("clp");
    c.sourceId = sid;
    c.name = t->name;
    Frame start = startPos - proj.recordLatencyOffset;
    if (start < 0) { c.sourceOffset = -start; start = 0; }
    c.start = start;
    c.length = a.numFrames() - c.sourceOffset;
    if (c.length > 0) t->clips.push_back (c);
    pool[sid] = makeSource (std::make_shared<const AudioData> (std::move (a)));
    finishEditLocked();
    OpResult saved = save();
    removeFile (sidecar);

    OpResult r;
    r.id = c.id;
    if (! closed || ioErr) { r.ok = false; r.error = "la grabación se guardó, pero hubo errores de escritura en disco"; }
    else if (dropped > 0) { r.ok = false; r.error = "se perdieron " + std::to_string (dropped) + " muestras (el disco no siguió el ritmo)"; }
    else if (! saved.ok) { r.ok = false; r.error = saved.error; }
    return r;
}

// --- Exportación --------------------------------------------------------------------------------------------------
OpResult Session::exportMix (const std::string& outPath, int bitDepth, Frame tailFrames, long* clippedSamples)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (recorder) return OpResult::fail ("no se puede exportar durante la grabación");
    if (proj.contentEnd() <= 0) return OpResult::fail ("el proyecto está vacío");

    // Motor independiente: no interfiere con la reproducción en curso y parte de un estado limpio.
    Engine offline;
    offline.prepare (proj.sampleRate, 1024);
    auto g = std::make_unique<RenderGraph>();
    g->sampleRate = proj.sampleRate; g->bpm = proj.bpm; g->beatsPerBar = proj.beatsPerBar;
    int slot = 0;
    Frame instrumentTail = 0, busTail = 0;
    for (const auto& t : proj.tracks)
    {
        TrackParams& p = offline.trackParams (slot);
        p.gain.store (dbToGain (t.volumeDb)); p.pan.store (t.pan); p.mute.store (t.mute); p.solo.store (t.solo);
        applySendsTo (p, t);
        TrackRT tr = makeTrackRT (t, slot++, true, *g);
        Frame trackTail = tr.instrument && ! tr.events.empty() ? tr.instrument->tailSamples() : 0;
        for (const auto& fx : tr.inserts) trackTail += fx->tailSamples(); // delay y reverb en serie suman su cola
        if (tr.busIndex >= 0) busTail = std::max<Frame> (busTail, trackTail);
        else instrumentTail = std::max<Frame> (instrumentTail, trackTail);
        g->tracks.push_back (std::move (tr));
    }
    // Las colas de los instrumentos (release, platillos) se incluyen siempre: el archivo no corta notas.
    // Un bus recibe la cola de las pistas y añade la suya (p. ej. la reverb compartida).
    const Frame total = proj.contentEnd() + std::max<Frame> (std::max<Frame> (0, tailFrames), instrumentTail + busTail);
    offline.setMasterGain (dbToGain (proj.masterDb));
    offline.publish (std::move (g));

    AudioData out;
    out.sampleRate = proj.sampleRate;
    out.allocate (2, total);
    offline.renderOffline (0, out.channels[0].data(), out.channels[1].data(), total);

    const WavFormat fmt = bitDepth == 16 ? WavFormat::Pcm16 : bitDepth == 32 ? WavFormat::Float32 : WavFormat::Pcm24;
    std::string err;
    WriteStats st;
    if (! writeWav (outPath, out, fmt, err, &st)) return OpResult::fail (err);
    if (clippedSamples) *clippedSamples = st.clippedSamples;
    return {};
}

// --- Formas de onda -----------------------------------------------------------------------------------------------
std::vector<float> Session::peaks (const std::string& sourceId, Frame from, Frame to, int buckets) const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    std::vector<float> out ((size_t) std::max (0, buckets) * 2, 0.0f);
    auto it = pool.find (sourceId);
    if (it == pool.end() || buckets <= 0 || to <= from) return out;
    const Source& s = *it->second;
    const AudioData& a = *s.audio;
    from = std::clamp<Frame> (from, 0, a.numFrames());
    to = std::clamp<Frame> (to, 0, a.numFrames());
    const double per = (double) (to - from) / buckets;
    for (int b = 0; b < buckets; ++b)
    {
        const Frame f0 = from + (Frame) (b * per), f1 = std::max (f0 + 1, from + (Frame) ((b + 1) * per));
        float mn = 0, mx = 0;
        if (per >= kPeakBlock * 2)
        {
            for (Frame k = f0 / kPeakBlock; k < std::min<Frame> ((Frame) s.summaryMin.size(), (f1 + kPeakBlock - 1) / kPeakBlock); ++k)
            { mn = std::min (mn, s.summaryMin[(size_t) k]); mx = std::max (mx, s.summaryMax[(size_t) k]); }
        }
        else
        {
            for (const auto& ch : a.channels)
                for (Frame i = f0; i < std::min (f1, a.numFrames()); ++i) { mn = std::min (mn, ch[(size_t) i]); mx = std::max (mx, ch[(size_t) i]); }
        }
        out[(size_t) b * 2] = mn;
        out[(size_t) b * 2 + 1] = mx;
    }
    return out;
}
} // namespace cs
