// Procesado offline no destructivo con VocalShift (misión 0.5, etapa C).
//
// Flujo: vocalProcess() valida y encola un trabajo → el trabajo procesa SIN el cerrojo de la sesión y fuera del hilo
// de audio, escribiendo a un archivo temporal (audio/<id>.vs.tmp) → al terminar, applyVocalLocked() comprueba que el
// proyecto y el clip siguen siendo los mismos, renombra el temporal a su nombre definitivo y cambia el clip a la
// versión procesada en un solo paso de deshacer.
//
// Garantías:
//  - el WAV original nunca se abre para escritura ni se borra;
//  - un temporal incompleto nunca sustituye a nada: solo se renombra tras un procesado completo y validado;
//  - si se cancela, falla o el proyecto cambia, el temporal se borra y el proyecto queda como estaba;
//  - el trabajo nunca espera el cerrojo de la sesión (lo intenta y vuelve a mirar la cancelación), así que cerrar el
//    proyecto con un trabajo en curso no puede producir un bloqueo mutuo.
#include "cs/Processor.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace cs
{
using nlohmann::json;

namespace
{
constexpr const char* kTmpSuffix = ".vs.tmp";
bool endsWith (const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size() && s.compare (s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
} // namespace

void Session::cleanStaleTempLocked()
{
    // Restos de un procesado interrumpido por un cierre inesperado: nunca llegaron a formar parte del proyecto.
    for (const auto& name : listDir (dir + "/audio"))
        if (endsWith (name, kTmpSuffix)) removeFile (dir + "/audio/" + name);
}

std::vector<std::string> Session::missingPlugins() const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    std::vector<std::string> out;
    auto add = [&] (const std::string& id) { if (std::find (out.begin(), out.end(), id) == out.end()) out.push_back (id); };
    std::map<std::string, bool> known; // una sola creación de prueba por tipo
    auto has = [&] (const std::string& type)
    {
        auto it = known.find (type);
        if (it == known.end()) it = known.emplace (type, createProcessor (type) != nullptr).first;
        return it->second;
    };
    for (const auto& t : proj.tracks)
    {
        if (! t.instrument.type.empty() && ! has (t.instrument.type)) add (t.instrument.type);
        for (const auto& i : t.inserts) if (! has (i.type)) add (i.type);
        for (const auto& c : t.clips)
            if (c.processed() && ! (c.procPlugin == "vocalshift" && vocal::available())) add (c.procPlugin);
    }
    return out;
}

OpResult Session::vocalProcess (const std::string& clipId, const vocal::Params& params)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (! vocal::available()) return OpResult::fail ("VocalShift no está incluido en esta compilación");
    if (recorder) return OpResult::fail ("detén la grabación antes de procesar");
    const ClipModel* c = proj.findClip (clipId);
    if (! c) return OpResult::fail ("clip no encontrado");
    for (const auto& j : jobs.list())
        if (j.targetId == clipId && (j.state == "queued" || j.state == "running")) return OpResult::fail ("este clip ya se está procesando");

    // Siempre se parte del original: reprocesar no acumula generaciones.
    const std::string srcId = c->processed() ? c->origSourceId : c->sourceId;
    const Frame from = c->processed() ? c->origOffset : c->sourceOffset;
    const Frame length = c->processed() ? c->origLength : c->length;
    auto it = pool.find (srcId);
    if (it == pool.end() && c->processed())
    {
        // El original de un clip procesado no se carga al abrir (no suena): se carga ahora.
        const SourceRef* ref = proj.findSource (srcId);
        bool ok = false;
        if (ref) loadSourceLocked (*ref, &ok);
        it = pool.find (srcId);
    }
    if (it == pool.end() || ! it->second->audio) return OpResult::fail ("falta el audio original de este clip");
    std::shared_ptr<const AudioData> in = it->second->audio;
    if (from < 0 || length < 1 || from + length > in->numFrames()) return OpResult::fail ("el clip no coincide con su audio");
    if ((double) length / in->sampleRate > vocal::kMaxClipSeconds) return OpResult::fail ("la toma es demasiado larga para procesarla (máximo 10 minutos)");

    const std::string projectId = proj.id, sid = newId ("src");
    const std::string tmpPath = absPath ("audio/" + sid + ".wav") + kTmpSuffix;
    const double bpm = proj.bpm;

    const std::string jobId = jobs.submit ("vocalshift", projectId, clipId,
        [this, in, from, length, params, projectId, clipId, sid, tmpPath, bpm] (JobContext& ctx, std::string& result, std::string& error)
        {
            AudioData out;
            out.sampleRate = in->sampleRate;
            out.channels.assign (2, {});
            // +1 s de margen para la cola; si la reserva falla (memoria), el trabajo falla limpio en el catch del runner.
            out.channels[0].reserve ((size_t) length + (size_t) in->sampleRate);
            out.channels[1].reserve ((size_t) length + (size_t) in->sampleRate);
            WavStreamWriter writer;
            std::string err;
            if (! writer.open (tmpPath, in->sampleRate, 2, err)) { error = "no se pudo crear el archivo temporal: " + err; return false; }
            vocal::Stats st;
            const std::string perr = vocal::process (*in, from, length, params,
                [&] (const float* inter, int frames)
                {
                    for (int i = 0; i < frames; ++i) { out.channels[0].push_back (inter[i * 2]); out.channels[1].push_back (inter[i * 2 + 1]); }
                    return writer.appendInterleaved (inter, frames);
                }, &ctx.progress, &ctx.cancel, &st, bpm);
            const bool closed = writer.close();
            if (! perr.empty() || ! closed || ctx.cancel.load())
            {
                removeFile (tmpPath);
                error = ! perr.empty() ? perr : (closed ? "cancelado" : "no se pudo terminar de escribir el resultado (¿almacenamiento lleno?)");
                return false;
            }
            // Aplicar: nunca se espera el cerrojo de la sesión; si cierran el proyecto, la cancelación llega aquí.
            OpResult r;
            for (;;)
            {
                if (ctx.cancel.load()) { removeFile (tmpPath); error = "cancelado"; return false; }
                std::unique_lock<std::recursive_mutex> lock (mtx, std::try_to_lock);
                if (lock.owns_lock()) { r = applyVocalLocked (projectId, clipId, sid, tmpPath, std::move (out), params, st.levelDb); break; }
                std::this_thread::sleep_for (std::chrono::milliseconds (2));
            }
            if (! r.ok) { removeFile (tmpPath); error = r.error; return false; }
            ctx.progress = 1.0f;
            result = json { { "clipId", clipId }, { "sourceId", sid }, { "latencySamples", st.latencySamples }, { "inputFrames", st.inputFrames },
                            { "outputFrames", st.outputFrames }, { "tailFrames", st.tailFrames }, { "inputPeak", st.inputPeak },
                            { "outputPeak", st.outputPeak }, { "levelDb", st.levelDb }, { "nonFiniteEvents", st.nonFiniteEvents }, { "clipAltered", st.clipAltered },
                            { "seconds", st.seconds } }.dump();
            return true;
        });
    if (jobId.empty()) return OpResult::fail ("hay demasiados procesos en cola: espera a que termine alguno");
    OpResult r;
    r.id = jobId;
    return r;
}

void Session::clearPreviewLocked()
{
    preview = VocalPreview();
    listenPreview = false;
}

void Session::applyPreviewLocked (const std::string& projectId, VocalPreview&& p)
{
    if (! open || proj.id != projectId || ! proj.findClip (p.clipId)) return; // el destino cambió: la prueba se descarta
    const bool wasListening = listenPreview && preview.clipId == p.clipId;
    preview = std::move (p);
    listenPreview = wasListening;
    if (wasListening) rebuildGraph();
}

OpResult Session::vocalPreview (const std::string& clipId, const vocal::Params& params, Frame from, Frame length)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (! vocal::available()) return OpResult::fail ("VocalShift no está incluido en esta compilación");
    if (recorder) return OpResult::fail ("detén la grabación antes de procesar");
    const ClipModel* c = proj.findClip (clipId);
    if (! c) return OpResult::fail ("clip no encontrado");
    // 0.8 (escucha continua): una prueba nueva sustituye a la que aún se está calculando para el mismo clip, en lugar
    // de rechazarse. Así, al mover una perilla varias veces seguidas solo se termina el cálculo de los últimos ajustes.
    // Un procesado completo en curso sigue bloqueando (no se cancela nunca el trabajo del usuario).
    std::vector<std::string> stalePreviews;
    for (const auto& j : jobs.list())
        if (j.targetId == clipId && (j.state == "queued" || j.state == "running"))
        {
            if (j.type != "vocalPreview") return OpResult::fail ("este clip ya se está procesando");
            stalePreviews.push_back (j.id);
        }

    const std::string srcId = c->processed() ? c->origSourceId : c->sourceId;
    const Frame baseOffset = c->processed() ? c->origOffset : c->sourceOffset;
    const Frame baseLength = c->processed() ? c->origLength : c->length;
    auto it = pool.find (srcId);
    if (it == pool.end() && c->processed())
    {
        const SourceRef* ref = proj.findSource (srcId);
        bool ok = false;
        if (ref) loadSourceLocked (*ref, &ok);
        it = pool.find (srcId);
    }
    if (it == pool.end() || ! it->second->audio) return OpResult::fail ("falta el audio original de este clip");
    std::shared_ptr<const AudioData> in = it->second->audio;
    if (baseOffset < 0 || baseLength < 1 || baseOffset + baseLength > in->numFrames()) return OpResult::fail ("el clip no coincide con su audio");

    // Región: dentro del clip, entre 0,25 s y kPreviewMaxSeconds.
    const Frame rate = in->sampleRate;
    const Frame maxLen = (Frame) (kPreviewMaxSeconds * (double) rate);
    from = std::clamp<Frame> (from, 0, std::max<Frame> (0, baseLength - 1));
    length = std::clamp<Frame> (length <= 0 ? maxLen : length, 1, std::min (maxLen, baseLength - from));
    if (length < std::min<Frame> (baseLength, rate / 4)) return OpResult::fail ("la región de prueba es demasiado corta");

    const std::string paramsJson = params.toJson();
    const std::string key = srcId + "|" + std::to_string (baseOffset) + "|" + std::to_string (from) + "|" + std::to_string (length) + "|" + paramsJson;
    OpResult r;
    if (preview.valid() && preview.clipId == clipId && preview.key == key && stalePreviews.empty()) { r.id = "cached"; return r; } // nada cambió: no se repite
    for (const auto& id : stalePreviews) jobs.cancel (id);

    // Medio segundo de pre-roll: el motor llega a la región con su estado ya asentado, como en el procesado completo.
    const Frame pre = std::min<Frame> (from, rate / 2);
    VocalPreview pv;
    pv.clipId = clipId; pv.key = key; pv.srcId = srcId; pv.baseOffset = baseOffset; pv.baseLength = baseLength;
    pv.from = from; pv.length = length; pv.skip = pre; pv.paramsJson = paramsJson;
    const std::string projectId = proj.id;
    const double bpm = proj.bpm;
    const Frame srcFrom = baseOffset + from - pre, srcLen = pre + length;

    const std::string jobId = jobs.submit ("vocalPreview", projectId, clipId,
        [this, in, srcFrom, srcLen, params, projectId, pv, bpm] (JobContext& ctx, std::string& result, std::string& error) mutable
        {
            auto out = std::make_shared<AudioData>();
            out->sampleRate = in->sampleRate;
            out->channels.assign (2, {});
            out->channels[0].reserve ((size_t) srcLen);
            out->channels[1].reserve ((size_t) srcLen);
            vocal::Stats st;
            const std::string perr = vocal::process (*in, srcFrom, srcLen, params,
                [&] (const float* inter, int frames)
                {
                    // La cola más allá de la región no se guarda: la prueba ocupa exactamente su hueco en el clip.
                    for (int i = 0; i < frames && (Frame) out->channels[0].size() < srcLen; ++i)
                    { out->channels[0].push_back (inter[i * 2]); out->channels[1].push_back (inter[i * 2 + 1]); }
                    return true;
                }, &ctx.progress, &ctx.cancel, &st, bpm);
            if (! perr.empty() || ctx.cancel.load()) { error = ! perr.empty() ? perr : "cancelado"; return false; }
            if ((Frame) out->channels[0].size() < srcLen) { error = "la prueba quedó incompleta"; return false; }
            pv.audio = out;
            pv.seconds = st.seconds;
            const Frame from = pv.from, length = pv.length;
            const std::string target = pv.clipId;
            for (;;)
            {
                if (ctx.cancel.load()) { error = "cancelado"; return false; }
                std::unique_lock<std::recursive_mutex> lock (mtx, std::try_to_lock);
                if (lock.owns_lock()) { applyPreviewLocked (projectId, std::move (pv)); break; }
                std::this_thread::sleep_for (std::chrono::milliseconds (2));
            }
            ctx.progress = 1.0f;
            result = json { { "clipId", target }, { "from", from }, { "length", length }, { "seconds", st.seconds },
                            { "latencySamples", st.latencySamples }, { "preview", true } }.dump();
            return true;
        });
    if (jobId.empty()) return OpResult::fail ("hay demasiados procesos en cola: espera a que termine alguno");
    r.id = jobId;
    return r;
}

OpResult Session::setPreviewListen (const std::string& clipId, bool on)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (on && ! (preview.valid() && preview.clipId == clipId)) return OpResult::fail ("no hay ninguna prueba de este clip: pulsa «Escuchar prueba»");
    const bool want = on;
    if (want == listenPreview) return {};
    if (! on && preview.clipId != clipId && ! clipId.empty()) return {};
    listenPreview = want;
    rebuildGraph();
    return {};
}

OpResult Session::applyVocalLocked (const std::string& projectId, const std::string& clipId, const std::string& sid, const std::string& tmpPath,
                                    AudioData&& audio, const vocal::Params& params, float levelDb)
{
    // Destino validado: mismo proyecto abierto y el clip sigue existiendo. Si no, el resultado se descarta.
    if (! open || proj.id != projectId) return OpResult::fail ("el proyecto cambió mientras se procesaba: el resultado se descartó");
    if (recorder) return OpResult::fail ("hay una grabación en curso: el resultado se descartó");
    const ClipModel* c0 = proj.findClip (clipId);
    if (! c0) return OpResult::fail ("el clip ya no existe: el resultado se descartó");
    if (audio.numFrames() < 1) return OpResult::fail ("el procesado no produjo audio");
    const std::string rel = "audio/" + sid + ".wav";
    if (fileExists (absPath (rel))) return OpResult::fail ("ya existe un archivo con ese nombre");
    if (std::rename (tmpPath.c_str(), absPath (rel).c_str()) != 0) return OpResult::fail ("no se pudo guardar el audio procesado");

    pushUndo();
    ClipModel* c = proj.findClip (clipId);
    const Frame frames = audio.numFrames();
    proj.sources.push_back (SourceRef { sid, rel, "vocalshift", 2, frames });
    if (! c->processed())
    {
        c->origSourceId = c->sourceId;
        c->origOffset = c->sourceOffset;
        c->origLength = c->length;
    }
    c->procPlugin = "vocalshift";
    c->procVersion = vocal::pinnedVersion();
    c->procParams = params.toJson();
    c->procSourceId = sid;
    c->procLength = frames;
    c->procLevelDb = std::isfinite (levelDb) ? std::clamp (levelDb, -24.0f, 24.0f) : 0.0f;
    c->sourceId = sid;
    c->sourceOffset = 0;
    c->length = frames;
    c->fadeIn = std::min (c->fadeIn, c->length);
    c->fadeOut = std::min (c->fadeOut, c->length - c->fadeIn);
    pool[sid] = makeSource (std::make_shared<const AudioData> (std::move (audio)));
    if (preview.clipId == clipId) clearPreviewLocked(); // ya existe el resultado completo: la prueba sobra
    return finishEditLocked();
}

OpResult Session::setClipVersion (const std::string& clipId, bool original, int match)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    ClipModel* c = proj.findClip (clipId);
    if (! c) return OpResult::fail ("clip no encontrado");
    if (! c->processed()) return OpResult::fail ("este clip no tiene versión procesada");
    // El igualado es de escucha: cambia el grafo en vivo, no el proyecto (ni «modificado», ni deshacer, ni guardado).
    const bool wasMatch = listenMatchClip == clipId;
    const bool wantMatch = match < 0 ? wasMatch : match != 0;
    if (wantMatch) listenMatchClip = clipId;
    else if (wasMatch) listenMatchClip.clear();
    if (c->playingOriginal() == original)
    {
        if (wantMatch != wasMatch) rebuildGraph();
        return {};
    }
    const std::string want = original ? c->origSourceId : c->procSourceId;
    if (pool.find (want) == pool.end())
    {
        const SourceRef* ref = proj.findSource (want);
        bool ok = false;
        if (ref) loadSourceLocked (*ref, &ok);
        if (! ok) return OpResult::fail (original ? "falta el audio original" : "falta el audio procesado");
        c = proj.findClip (clipId);
    }
    // Las dos versiones empiezan en el mismo punto del timeline (la latencia ya está compensada en la procesada), así
    // que cambiar con la canción sonando no desplaza nada: el grafo nuevo se publica de forma atómica.
    c->sourceId = want;
    c->sourceOffset = original ? c->origOffset : 0;
    c->length = original ? c->origLength : c->procLength;
    c->fadeIn = std::min (c->fadeIn, c->length);
    c->fadeOut = std::min (c->fadeOut, c->length - c->fadeIn);
    touchLocked();
    rebuildGraph();
    return {};
}

OpResult Session::discardProcess (const std::string& clipId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    const ClipModel* c0 = proj.findClip (clipId);
    if (! c0) return OpResult::fail ("clip no encontrado");
    if (! c0->processed()) return OpResult::fail ("este clip no tiene versión procesada");
    if (pool.find (c0->origSourceId) == pool.end())
    {
        const SourceRef* ref = proj.findSource (c0->origSourceId);
        bool ok = false;
        if (ref) loadSourceLocked (*ref, &ok);
        if (! ok) return OpResult::fail ("falta el audio original");
    }
    pushUndo();
    ClipModel* c = proj.findClip (clipId);
    c->sourceId = c->origSourceId;
    c->sourceOffset = c->origOffset;
    c->length = c->origLength;
    c->fadeIn = std::min (c->fadeIn, c->length);
    c->fadeOut = std::min (c->fadeOut, c->length - c->fadeIn);
    c->clearProcess(); // el archivo procesado queda en disco para poder deshacer
    return finishEditLocked();
}
} // namespace cs
