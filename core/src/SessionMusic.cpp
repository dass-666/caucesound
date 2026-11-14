// Session: instrumentos, patrones de batería, clips MIDI, bucle y plantillas de género (fase 1.2).
#include "cs/Composer.h"
#include "cs/Instruments.h"
#include "cs/Packs.h"
#include "cs/VocalBridge.h"
#include "cs/FxChains.h"
#include "cs/Session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace cs
{
using nlohmann::json;

TrackModel* Session::instrumentTrack (const std::string& trackId, const char* kind, OpResult& err)
{
    TrackModel* t = proj.findTrack (trackId);
    if (! t) { err = OpResult::fail ("pista no encontrada"); return nullptr; }
    if (kind && t->kind != kind) { err = OpResult::fail (std::string ("la pista no es de tipo ") + kind); return nullptr; }
    return t;
}

// --- Instrumentos ----------------------------------------------------------------------------------------------
OpResult Session::setInstrumentParam (const std::string& trackId, const std::string& paramId, float value)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    OpResult err;
    TrackModel* t = instrumentTrack (trackId, nullptr, err);
    if (! t) return err;
    if (t->instrument.type.empty()) return OpResult::fail ("la pista no tiene instrumento");
    if (! std::isfinite (value)) return OpResult::fail ("valor no válido");
    auto inst = instrumentFor (*t);
    if (! inst) return OpResult::fail ("instrumento no disponible");
    const int idx = parameterIndex (*inst, paramId);
    if (idx < 0) return OpResult::fail ("parámetro desconocido: " + paramId);
    const bool structural = t->instrument.type == "cs.drums" && paramId == "kit";
    paramUndoLocked ("i/" + trackId + "/" + paramId);
    inst->setParameter (idx, value);
    t->instrument.state = inst->saveState();
    if (structural)
    {
        // Elegir un kit sintetizado deja el kit de muestras (0.9): el estado vuelve a ser el de la 0.8.
        json st = json::parse (t->instrument.state, nullptr, false);
        if (st.is_object() && st.contains ("sampleKit")) { st.erase ("sampleKit"); st.erase ("sampleKitVersion"); t->instrument.state = st.dump(); }
        // Cambiar de kit regenera los sonidos: nueva instancia preparada aquí (nunca en el hilo de audio).
        instrumentInstances.erase (t->id);
        rebuildGraph();
    }
    touchLocked();
    return {};
}

// --- Efectos de inserción ----------------------------------------------------------------------------------------
namespace
{
InsertModel* findInsert (ProjectModel& m, const std::string& id, TrackModel** owner)
{
    for (auto& t : m.tracks)
        for (auto& i : t.inserts)
            if (i.id == id) { if (owner) *owner = &t; return &i; }
    return nullptr;
}
} // namespace

OpResult Session::addInsert (const std::string& trackId, const std::string& type)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    TrackModel* t = proj.findTrack (trackId);
    if (! t) return OpResult::fail ("pista no encontrada");
    if (t->inserts.size() >= 8) return OpResult::fail ("máximo 8 efectos por pista");
    const auto types = effectTypes();
    if (type == "cs.vocalshift.live" && expLiveVocal)
    {
        // 0.10, experimental (apagado por defecto y sin botón en la app): solo en pistas de audio y con un máximo de
        // instancias, porque su coste en ARM no está medido. Sin el interruptor sigue siendo «desconocido».
        if (t->kind != "audio") return OpResult::fail ("VocalShift en vivo (experimental) solo se puede usar en pistas de audio");
        int count = 0;
        for (const auto& tr : proj.tracks) for (const auto& i : tr.inserts) count += i.type == type;
        if (count >= vocal::kMaxLiveInserts) return OpResult::fail ("máximo " + std::to_string (vocal::kMaxLiveInserts) + " VocalShift en vivo por proyecto (experimental)");
    }
    else if (std::find (types.begin(), types.end(), type) == types.end()) return OpResult::fail ("efecto desconocido: " + type);
    ProcessorPtr fx = createProcessor (type);
    if (! fx) return OpResult::fail ("efecto no disponible");
    // PDC: el motor compensa la latencia de las inserciones en pistas, no en buses. Hasta que eso exista con pruebas,
    // un efecto con latencia no se admite en un bus (sonaría desalineado con el resto de la mezcla).
    fx->prepare (proj.sampleRate, 4096);
    if (t->kind == "bus" && fx->latencySamples() > 0) return OpResult::fail ("este efecto tiene latencia y todavía no se puede usar en un bus: ponlo en la pista");
    pushUndo();
    t = proj.findTrack (trackId);
    InsertModel ins;
    ins.id = newId ("fx");
    ins.type = type;
    ins.state = fx->saveState();
    t->inserts.push_back (ins);
    finishEditLocked();
    OpResult r;
    r.id = ins.id;
    return r;
}

OpResult Session::applyFxChain (const std::string& trackId, const std::string& presetId, bool replace)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    const TrackModel* t0 = proj.findTrack (trackId);
    if (! t0) return OpResult::fail ("pista no encontrada");
    const ChainPreset* preset = findChainPreset (presetId);
    if (! preset) return OpResult::fail ("cadena desconocida: " + presetId);
    const size_t total = (replace ? 0 : t0->inserts.size()) + preset->steps.size();
    if (total > 8) return OpResult::fail ("no caben: la pista tendría " + std::to_string (total) + " efectos (máximo 8). Usa «Reemplazar» o quita alguno");
    // Todo se prepara antes de tocar el proyecto: si un efecto falla, no queda una cadena a medias.
    std::vector<InsertModel> added;
    for (const auto& st : preset->steps)
    {
        ProcessorPtr fx = createProcessor (st.type);
        if (! fx) return OpResult::fail ("efecto no disponible: " + st.type);
        fx->prepare (proj.sampleRate, 4096);
        if (t0->kind == "bus" && fx->latencySamples() > 0) return OpResult::fail ("la cadena tiene un efecto con latencia y no se puede usar en un bus");
        const auto ps = fx->parameters();
        for (const auto& kv : st.params)
        {
            std::string id = kv.first;
            float v = kv.second;
            if (id == "timeBeats") { id = "timeMs"; v = (float) (60000.0 / proj.bpm * kv.second); }
            const int idx = parameterIndex (*fx, id);
            if (idx < 0) return OpResult::fail ("parámetro desconocido en la cadena: " + id);
            fx->setParameter (idx, std::clamp (v, ps[(size_t) idx].minValue, ps[(size_t) idx].maxValue));
        }
        InsertModel ins;
        ins.id = newId ("fx");
        ins.type = st.type;
        ins.state = fx->saveState();
        added.push_back (ins);
    }
    pushUndo();
    TrackModel* t = proj.findTrack (trackId);
    if (replace)
    {
        for (const auto& i : t->inserts) insertInstances.erase (i.id);
        t->inserts.clear();
    }
    for (const auto& i : added) t->inserts.push_back (i);
    finishEditLocked();
    OpResult r;
    r.id = std::to_string (t->inserts.size());
    return r;
}

OpResult Session::routeDrumLane (const std::string& trackId, int lane, bool own)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (recorder) return OpResult::fail ("no se puede cambiar el enrutado durante la grabación");
    const TrackModel* t0 = proj.findTrack (trackId);
    if (! t0 || t0->kind != "drums") return OpResult::fail ("no es una pista de batería");
    if (lane < 0 || lane >= kDrumLanes) return OpResult::fail ("pieza no válida");
    auto existing = t0->laneRoutes.find (lane);
    if (! own)
    {
        if (existing == t0->laneRoutes.end()) return {};
        const std::string busId = existing->second;
        pushUndo();
        proj.findTrack (trackId)->laneRoutes.erase (lane);
        const TrackModel* b = proj.findTrack (busId);
        if (b && b->kind == "bus")
        {
            for (const auto& i : b->inserts) insertInstances.erase (i.id);
            proj.tracks.erase (std::remove_if (proj.tracks.begin(), proj.tracks.end(), [&] (const TrackModel& x) { return x.id == busId; }), proj.tracks.end());
            for (auto& x : proj.tracks) x.sends.erase (busId);
        }
        return finishEditLocked();
    }
    if (existing != t0->laneRoutes.end()) { OpResult r; r.id = existing->second; return r; }
    int buses = 0;
    for (const auto& x : proj.tracks) buses += x.kind == "bus";
    if (buses >= kMaxBuses) return OpResult::fail ("no quedan canales libres (máximo " + std::to_string (kMaxBuses) + " entre buses y piezas con canal propio)");
    if (proj.tracks.size() >= (size_t) kMaxTracks) return OpResult::fail ("límite de pistas alcanzado");
    // Nombre de la pieza según el kit de la pista.
    std::string name = "Pieza " + std::to_string (lane + 1);
    {
        const json st = json::parse (t0->instrument.state, nullptr, false);
        const int kit = st.is_object() ? (int) std::lround (st.value ("kit", 0.0)) : 0;
        const auto& kits = drumKits();
        if (kit >= 0 && kit < (int) kits.size() && lane < (int) kits[(size_t) kit].laneNames.size()) name = kits[(size_t) kit].laneNames[(size_t) lane];
    }
    pushUndo();
    TrackModel b;
    b.id = newId ("trk");
    b.kind = "bus";
    b.name = name;
    b.color = proj.findTrack (trackId)->color;
    // Justo después de la batería y de los canales de sus otras piezas: el mezclador queda agrupado.
    auto pos = std::find_if (proj.tracks.begin(), proj.tracks.end(), [&] (const TrackModel& x) { return x.id == trackId; });
    auto after = std::next (pos);
    while (after != proj.tracks.end() && after->kind == "bus")
    {
        bool mine = false;
        for (const auto& r : pos->laneRoutes) mine = mine || r.second == after->id;
        if (! mine) break;
        ++after;
    }
    const std::string busId = b.id;
    proj.tracks.insert (after, b);
    proj.findTrack (trackId)->laneRoutes[lane] = busId;
    finishEditLocked();
    OpResult r;
    r.id = busId;
    return r;
}

OpResult Session::removeInsert (const std::string& insertId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* t = nullptr;
    if (! findInsert (proj, insertId, &t)) return OpResult::fail ("efecto no encontrado");
    pushUndo();
    findInsert (proj, insertId, &t);
    t->inserts.erase (std::remove_if (t->inserts.begin(), t->inserts.end(), [&] (const InsertModel& i) { return i.id == insertId; }), t->inserts.end());
    insertInstances.erase (insertId);
    finishEditLocked();
    return {};
}

OpResult Session::setInsertParam (const std::string& insertId, const std::string& paramId, float value)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    InsertModel* ins = findInsert (proj, insertId, nullptr);
    if (! ins) return OpResult::fail ("efecto no encontrado");
    if (! std::isfinite (value)) return OpResult::fail ("valor no válido");
    // Instancia viva (la que suena) o una temporal si el efecto está en bypass.
    ProcessorPtr fx;
    auto it = insertInstances.find (insertId);
    if (it != insertInstances.end() && it->second) fx = it->second;
    else { fx = createProcessor (ins->type); if (! fx) return OpResult::fail ("efecto no disponible"); fx->loadState (ins->state); }
    const int idx = parameterIndex (*fx, paramId);
    if (idx < 0) return OpResult::fail ("parámetro desconocido: " + paramId);
    paramUndoLocked ("f/" + insertId + "/" + paramId);
    fx->setParameter (idx, value);
    ins->state = fx->saveState();
    touchLocked();
    return {};
}

OpResult Session::setInsertBypass (const std::string& insertId, bool bypass)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    InsertModel* ins = findInsert (proj, insertId, nullptr);
    if (! ins) return OpResult::fail ("efecto no encontrado");
    if (ins->bypass == bypass) return {};
    pushUndo();
    findInsert (proj, insertId, nullptr)->bypass = bypass;
    finishEditLocked();
    return {};
}

OpResult Session::moveInsert (const std::string& insertId, int newIndex)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* t = nullptr;
    if (! findInsert (proj, insertId, &t)) return OpResult::fail ("efecto no encontrado");
    if (newIndex < 0 || newIndex >= (int) t->inserts.size()) return OpResult::fail ("posición no válida");
    pushUndo();
    findInsert (proj, insertId, &t);
    auto it = std::find_if (t->inserts.begin(), t->inserts.end(), [&] (const InsertModel& i) { return i.id == insertId; });
    InsertModel m = *it;
    t->inserts.erase (it);
    t->inserts.insert (t->inserts.begin() + newIndex, m);
    finishEditLocked();
    return {};
}

float Session::insertMeter (const std::string& insertId) const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    auto it = insertInstances.find (insertId);
    if (it == insertInstances.end() || ! it->second || it->second->typeId() != "cs.compressor") return 0.0f;
    // El compresor publica su reducción como parámetro de solo lectura del estado; se expone vía getParameter(-1).
    return it->second->getParameter (-1);
}

OpResult Session::loadSynthPreset (const std::string& trackId, const std::string& presetId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    OpResult err;
    TrackModel* t = instrumentTrack (trackId, "synth", err);
    if (! t) return err;
    const std::string state = synthPresetState (presetId);
    if (state.empty()) return OpResult::fail ("preset desconocido");
    if (t->instrument.type != "cs.synth")
    {
        // Volver de un instrumento de muestras (0.9) a Cauce Synth: instancia nueva, un paso de deshacer.
        pushUndo();
        t->instrument.type = "cs.synth";
        t->instrument.state = state;
        t->instrument.preset = presetId;
        instrumentInstances.erase (t->id);
        return finishEditLocked();
    }
    auto inst = instrumentFor (*t);
    if (! inst) return OpResult::fail ("no se pudo cargar el preset");
    pushUndo();
    if (! inst->loadState (state)) { undoStack.pop_back(); return OpResult::fail ("no se pudo cargar el preset"); }
    t->instrument.state = inst->saveState();
    t->instrument.preset = presetId;
    touchLocked();
    return {};
}

int Session::activeVoices (const std::string& trackId) const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    auto it = instrumentInstances.find (trackId);
    return it != instrumentInstances.end() && it->second ? it->second->activeVoices() : 0;
}

// --- Patrones --------------------------------------------------------------------------------------------------
#define CS_PATTERN(trackId, patternId)                                              \
    OpResult err;                                                                   \
    TrackModel* t = instrumentTrack (trackId, "drums", err);                        \
    if (! t) return err;                                                            \
    if (! t->findPattern (patternId)) return OpResult::fail ("patrón no encontrado");

OpResult Session::addPattern (const std::string& trackId, const std::string& copyFromId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    OpResult err;
    TrackModel* t = instrumentTrack (trackId, "drums", err);
    if (! t) return err;
    if (t->patterns.size() >= 64) return OpResult::fail ("límite de patrones alcanzado");
    const DrumPattern* src = copyFromId.empty() ? nullptr : t->findPattern (copyFromId);
    if (! copyFromId.empty() && ! src) return OpResult::fail ("patrón de origen no encontrado");
    pushUndo();
    t = proj.findTrack (trackId);
    DrumPattern p = src ? *t->findPattern (copyFromId) : DrumPattern();
    p.id = newId ("pat");
    // Nombres A, B, C... (A2, B2... a partir del 27)
    const size_t n = t->patterns.size();
    p.name = std::string (1, (char) ('A' + n % 26)) + (n >= 26 ? std::to_string (n / 26 + 1) : "");
    t->patterns.push_back (p);
    finishEditLocked();
    OpResult r;
    r.id = p.id;
    return r;
}

OpResult Session::setPatternStep (const std::string& trackId, const std::string& patternId, int lane, int step, int velocity)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    CS_PATTERN (trackId, patternId)
    const DrumPattern* p = t->findPattern (patternId);
    if (lane < 0 || lane >= kDrumLanes || step < 0 || step >= p->steps) return OpResult::fail ("paso fuera del patrón");
    if (velocity < 0 || velocity > 127) return OpResult::fail ("velocidad fuera de rango");
    pushUndo();
    DrumPattern* w = proj.findTrack (trackId)->findPattern (patternId);
    w->lanes[(size_t) lane][(size_t) step] = velocity;
    // Apagar un paso borra su redoble: si no, al volver a encenderlo reaparecería un redoble invisible.
    if (velocity == 0 && (size_t) lane < w->rolls.size() && (size_t) step < w->rolls[(size_t) lane].size()) w->rolls[(size_t) lane][(size_t) step] = 0;
    return finishEditLocked();
}

OpResult Session::setPatternRoll (const std::string& trackId, const std::string& patternId, int lane, int step, int roll)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    CS_PATTERN (trackId, patternId)
    const DrumPattern* p = t->findPattern (patternId);
    if (lane < 0 || lane >= kDrumLanes || step < 0 || step >= p->steps) return OpResult::fail ("paso fuera del patrón");
    if (roll < 1 || roll > 4) return OpResult::fail ("redoble fuera de rango (1-4)");
    pushUndo();
    DrumPattern* w = proj.findTrack (trackId)->findPattern (patternId);
    if (w->rolls.empty()) w->rolls.assign ((size_t) kDrumLanes, std::vector<int> ((size_t) w->steps, 0));
    w->rolls[(size_t) lane][(size_t) step] = roll == 1 ? 0 : roll;
    return finishEditLocked();
}

OpResult Session::setPatternLength (const std::string& trackId, const std::string& patternId, int steps)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    CS_PATTERN (trackId, patternId)
    if (steps != 16 && steps != 32) return OpResult::fail ("el patrón debe tener 16 o 32 pasos");
    pushUndo();
    DrumPattern* p = proj.findTrack (trackId)->findPattern (patternId);
    for (auto& lane : p->lanes)
    {
        if (steps == 32 && p->steps == 16) { lane.resize (32); std::copy (lane.begin(), lane.begin() + 16, lane.begin() + 16); } // el 2.º compás empieza como copia del 1.º
        else lane.resize ((size_t) steps, 0);
    }
    for (auto& lane : p->rolls)
    {
        if (steps == 32 && lane.size() == 16) { lane.resize (32); std::copy (lane.begin(), lane.begin() + 16, lane.begin() + 16); }
        else lane.resize ((size_t) steps, 0);
    }
    p->steps = steps;
    return finishEditLocked();
}

OpResult Session::setPatternSwing (const std::string& trackId, const std::string& patternId, float swing)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    CS_PATTERN (trackId, patternId)
    if (! std::isfinite (swing) || swing < 0 || swing > 1) return OpResult::fail ("swing fuera de rango (0-1)");
    pushUndo();
    proj.findTrack (trackId)->findPattern (patternId)->swing = swing;
    return finishEditLocked();
}

OpResult Session::setPatternLanes (const std::string& trackId, const std::string& patternId, const std::vector<std::vector<int>>& lanes)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    CS_PATTERN (trackId, patternId)
    DrumPattern test = *t->findPattern (patternId);
    test.lanes = lanes;
    const std::string e = validatePattern (test);
    if (! e.empty()) return OpResult::fail (e);
    pushUndo();
    DrumPattern* w = proj.findTrack (trackId)->findPattern (patternId);
    w->lanes = lanes;
    for (size_t l = 0; l < w->rolls.size() && l < lanes.size(); ++l) // pasos apagados pierden su redoble
        for (size_t st = 0; st < w->rolls[l].size() && st < lanes[l].size(); ++st)
            if (lanes[l][st] == 0) w->rolls[l][st] = 0;
    return finishEditLocked();
}

OpResult Session::renamePattern (const std::string& trackId, const std::string& patternId, const std::string& name)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    CS_PATTERN (trackId, patternId)
    pushUndo();
    proj.findTrack (trackId)->findPattern (patternId)->name = name.substr (0, 24);
    touchLocked();
    return {};
}
#undef CS_PATTERN

// --- Clips MIDI -------------------------------------------------------------------------------------------------
OpResult Session::addMidiClip (const std::string& trackId, Tick start, Tick length, const std::string& patternId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    OpResult err;
    TrackModel* t = instrumentTrack (trackId, nullptr, err);
    if (! t) return err;
    if (t->instrument.type.empty()) return OpResult::fail ("las pistas de audio no admiten clips MIDI");
    if (start < 0 || length <= 0) return OpResult::fail ("posición o duración no válida");
    std::string pid = patternId;
    if (t->kind == "drums")
    {
        if (pid.empty() && ! t->patterns.empty()) pid = t->patterns.front().id;
        if (! t->findPattern (pid)) return OpResult::fail ("patrón no encontrado");
    }
    else pid.clear();
    pushUndo();
    t = proj.findTrack (trackId);
    MidiClipModel c;
    c.id = newId ("mcl");
    c.patternId = pid;
    c.name = pid.empty() ? t->name : t->findPattern (pid)->name;
    c.start = start;
    c.length = length;
    t->midiClips.push_back (c);
    finishEditLocked();
    OpResult r;
    r.id = c.id;
    return r;
}

OpResult Session::moveMidiClip (const std::string& clipId, Tick start, const std::string& newTrackId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* owner = nullptr;
    MidiClipModel* c = proj.findMidiClip (clipId, &owner);
    if (! c) return OpResult::fail ("clip no encontrado");
    TrackModel* dest = newTrackId.empty() ? owner : proj.findTrack (newTrackId);
    if (! dest) return OpResult::fail ("pista destino no encontrada");
    if (dest->kind != owner->kind) return OpResult::fail ("solo se puede mover a una pista del mismo tipo");
    if (dest != owner && ! c->patternId.empty()) return OpResult::fail ("los patrones pertenecen a su pista de batería");
    pushUndo();
    c = proj.findMidiClip (clipId, &owner);
    MidiClipModel moved = *c;
    moved.start = std::max<Tick> (0, start);
    if (dest->id == owner->id) *c = moved;
    else
    {
        const std::string destId = dest->id;
        owner->midiClips.erase (std::find_if (owner->midiClips.begin(), owner->midiClips.end(), [&] (const MidiClipModel& x) { return x.id == clipId; }));
        proj.findTrack (destId)->midiClips.push_back (moved);
    }
    return finishEditLocked();
}

OpResult Session::resizeMidiClip (const std::string& clipId, Tick start, Tick length)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    MidiClipModel* c = proj.findMidiClip (clipId);
    if (! c) return OpResult::fail ("clip no encontrado");
    if (start < 0 || length < kStepTicks) return OpResult::fail ("duración mínima: una semicorchea");
    pushUndo();
    c = proj.findMidiClip (clipId);
    // Recorte por la izquierda: las notas conservan su posición absoluta.
    const Tick shift = start - c->start;
    if (shift != 0 && c->patternId.empty())
    {
        std::vector<MidiNote> kept;
        for (auto n : c->notes) { n.start -= shift; if (n.start >= 0) kept.push_back (n); }
        c->notes = std::move (kept);
    }
    c->start = start;
    c->length = length;
    return finishEditLocked();
}

OpResult Session::deleteMidiClip (const std::string& clipId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* owner = nullptr;
    if (! proj.findMidiClip (clipId, &owner)) return OpResult::fail ("clip no encontrado");
    pushUndo();
    proj.findMidiClip (clipId, &owner);
    owner->midiClips.erase (std::find_if (owner->midiClips.begin(), owner->midiClips.end(), [&] (const MidiClipModel& x) { return x.id == clipId; }));
    return finishEditLocked();
}

OpResult Session::duplicateMidiClip (const std::string& clipId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* owner = nullptr;
    if (! proj.findMidiClip (clipId, &owner)) return OpResult::fail ("clip no encontrado");
    pushUndo();
    MidiClipModel d = *proj.findMidiClip (clipId, &owner);
    d.id = newId ("mcl");
    d.start += d.length;
    owner->midiClips.push_back (d);
    finishEditLocked();
    OpResult r;
    r.id = d.id;
    return r;
}

OpResult Session::setMidiClipPattern (const std::string& clipId, const std::string& patternId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    TrackModel* owner = nullptr;
    MidiClipModel* c = proj.findMidiClip (clipId, &owner);
    if (! c) return OpResult::fail ("clip no encontrado");
    const DrumPattern* p = owner->findPattern (patternId);
    if (! p) return OpResult::fail ("patrón no encontrado");
    pushUndo();
    c = proj.findMidiClip (clipId, &owner);
    c->patternId = patternId;
    c->name = owner->findPattern (patternId)->name;
    return finishEditLocked();
}

OpResult Session::setClipNotes (const std::string& clipId, std::vector<MidiNote> notes)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    MidiClipModel* c = proj.findMidiClip (clipId);
    if (! c) return OpResult::fail ("clip no encontrado");
    if (! c->patternId.empty()) return OpResult::fail ("este clip reproduce un patrón de batería");
    if (notes.size() > 20000) return OpResult::fail ("demasiadas notas");
    for (const auto& n : notes) { const std::string e = validateNote (n); if (! e.empty()) return OpResult::fail (e); }
    sortNotes (notes);
    pushUndo();
    proj.findMidiClip (clipId)->notes = std::move (notes);
    return finishEditLocked();
}

OpResult Session::quantizeClip (const std::string& clipId, Tick grid, float strength, bool lengthsToo)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    MidiClipModel* c = proj.findMidiClip (clipId);
    if (! c) return OpResult::fail ("clip no encontrado");
    if (grid <= 0 || grid > 4 * kPpq) return OpResult::fail ("rejilla no válida");
    pushUndo();
    quantizeNotes (proj.findMidiClip (clipId)->notes, grid, strength, lengthsToo);
    return finishEditLocked();
}

// --- Bucle ---------------------------------------------------------------------------------------------------
OpResult Session::setLoop (bool on, Tick start, Tick end)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (start < 0 || end <= start) return OpResult::fail ("bucle no válido");
    proj.loopEnabled = on;
    proj.loopStart = start;
    proj.loopEnd = end;
    applyLoopLocked();
    touchLocked();
    return {};
}

// --- Plantillas (misión 0.4: «Cauce Signature Sound») -----------------------------------------------------------
// Cada plantilla es un arreglo de 17 compases (Intro, A, B, Break, Final y un compás de Cierre) creado por el compositor local
// (Composer.cpp) con un plan guardado en el proyecto, así que cada parte se puede regenerar después con otra
// semilla o energía (varyPart). Los proyectos creados con plantillas anteriores no cambian.
namespace
{
struct PartDef { const char* name; const char* preset; const char* role; float volDb, pan, send; };
struct GenreDef
{
    const char* id;
    double bpm;
    int kit;
    float drumsDb, drumsSend;
    std::vector<PartDef> parts;
    float reverbSize, reverbDamping;
    // Panorama y volumen por carril del kit (charles a un lado, percusiones repartidas).
    float lanePan[kDrumLanes], laneDb[kDrumLanes];
};

const std::vector<GenreDef>& genreDefs()
{
    static const std::vector<GenreDef> d = {
        // Mezcla 3.0 (misión 0.6). Medido por pistas con la exportación para DAW: en la 0.5 la batería sonaba ~25 dB por
        // encima de la melodía y entre el 88 y el 94 % de la energía quedaba por debajo de 250 Hz (el bombo lo tapaba
        // todo y en un altavoz pequeño no se oía la música). Ahora: bombo 3 dB más bajo dentro del kit, caja y charles
        // más presentes, y melodías y acordes entre 8 y 10 dB más altos. (Se probó añadir saturación al bajo: medido, no
        // aportaba armónicos por encima de 150 Hz —el 808 ya los tiene— y solo subía su nivel, así que se retiró.) Solo afecta a canciones nuevas: los valores se guardan
        // en cada proyecto, así que los proyectos existentes suenan igual que antes.
        { "trap", 140, 5, -3.0f, 0.05f,
          { { "808", "cauce_808", "bass", -5.0f, 0.0f, 0.0f }, { "Melodía", "dark_bell", "melody", -1.0f, 0.12f, 0.30f }, { "Pad", "air_pad", "pad", -8.0f, 0.0f, 0.32f } },
          0.78f, 0.45f, { 0, 0, 0.05f, 0.22f, 0.28f, -0.3f, 0.3f, -0.12f }, { -5, 0, -1, -2, -4, -5, -4, -7 } },
        { "reggaeton", 95, 4, -3.0f, 0.06f,
          { { "Bajo", "dembow_bass", "bass", -5.0f, 0.0f, 0.0f }, { "Keys", "urban_pluck", "chords", -3.0f, -0.18f, 0.22f },
            { "Melodía", "dark_bell", "melody", -2.0f, 0.15f, 0.28f }, { "Pad", "air_pad", "pad", -10.0f, 0.0f, 0.3f } },
          0.62f, 0.5f, { 0, 0, 0.05f, 0.2f, 0.25f, -0.35f, 0.32f, 0.4f }, { -5, 0, -2, -3, -5, -2, -2, -6 } },
        { "hiphop", 88, 6, -3.0f, 0.07f,
          { { "Bajo", "cauce_sub", "bass", -9.5f, 0.0f, 0.0f }, { "Keys", "warm_keys", "chords", -1.0f, -0.12f, 0.25f },
            { "Melodía", "urban_pluck", "melody", 3.0f, 0.2f, 0.28f } },
          0.55f, 0.6f, { 0, 0, -0.1f, 0.18f, 0.22f, -0.25f, 0.35f, -0.15f }, { -7, 0, -3, 0, -3, -4, -4, -8 } },
        // --- Misión 0.8: tres caracteres de hip-hop (la plantilla «hiphop» de siempre no cambia). Kits 8-10 nuevos. ---
        // Boom bap crudo: batería seca y al frente (casi sin reverb), bajo redondo y corto, teclas polvorientas,
        // melodía escasa. Sin pad: menos capas.
        { "boombap", 90, 8, -2.0f, 0.03f,
          { { "Bajo", "upright_bass", "bass", -6.0f, 0.0f, 0.0f }, { "Keys", "dusty_keys", "chords", -2.0f, -0.15f, 0.1f },
            { "Melodía", "urban_pluck", "melody", -2.0f, 0.2f, 0.15f } },
          0.32f, 0.75f, { 0, 0, 0.05f, 0.22f, 0.25f, -0.3f, 0.35f, 0.0f }, { -6, 0, -4, -1, -4, -3, -6, -10 } },
        // Jazz rap / soul: batería suave, bajo que camina, teclas soul con novenas, melodía cálida y algo de sala.
        { "jazzrap", 86, 9, -4.0f, 0.08f,
          { { "Bajo", "upright_bass", "bass", -5.0f, 0.0f, 0.0f }, { "Rhodes", "soul_keys", "chords", 0.0f, -0.12f, 0.24f },
            { "Melodía", "soft_keys", "melody", -2.0f, 0.18f, 0.26f } },
          0.5f, 0.7f, { 0, 0, 0.05f, 0.25f, 0.28f, -0.25f, 0.3f, 0.0f }, { -6, -1, -5, -2, -5, -2, -8, -12 } },
        // Dark hip-hop: bombo y sub profundos, teclas oscuras largas, campana con mucha sala y un pad tenue.
        { "darkhiphop", 80, 10, -3.0f, 0.06f,
          { { "Sub", "dark_sub", "bass", -10.0f, 0.0f, 0.0f }, { "Keys", "dark_piano", "chords", -3.0f, -0.15f, 0.3f },
            { "Melodía", "dark_bell", "melody", -2.0f, 0.15f, 0.36f }, { "Pad", "air_pad", "pad", -13.0f, 0.0f, 0.35f } },
          0.88f, 0.55f, { 0, 0, 0.05f, 0.2f, 0.25f, -0.3f, 0.2f, 0.0f }, { -10, 0, -4, -4, -6, -4, -3, -8 } },
        { "pop", 108, 7, -4.0f, 0.06f,
          { { "Bajo", "synth_bass", "bass", -6.0f, 0.0f, 0.0f }, { "Keys", "bright_pluck", "chords", -5.0f, -0.2f, 0.25f },
            { "Melodía", "bright_lead", "melody", -5.0f, 0.15f, 0.25f }, { "Pad", "air_pad", "pad", -11.0f, 0.0f, 0.3f } },
          0.6f, 0.4f, { 0, 0, 0.0f, 0.2f, 0.25f, -0.3f, 0.35f, -0.15f }, { -3, 0, -2, -3, -5, -4, -6, -7 } },
        { "electronic", 124, 7, -4.0f, 0.05f,
          { { "Bajo", "synth_bass", "bass", -6.0f, 0.0f, 0.0f }, { "Stabs", "bright_pluck", "chords", -6.0f, -0.2f, 0.25f },
            { "Arp", "electro_arp", "melody", -6.0f, 0.2f, 0.28f }, { "Pad", "air_pad", "pad", -10.0f, 0.0f, 0.35f } },
          0.72f, 0.45f, { 0, 0, 0.0f, 0.2f, 0.25f, -0.3f, 0.3f, -0.15f }, { -3, 0, -2, -4, -5, -4, -6, -7 } },
    };
    return d;
}

// --- 0.10 «Signature 2»: los sonidos de los paquetes 2.0 para cada estilo -------------------------------------------
// Cada entrada cambia el sonido de un papel si su paquete está instalado; si no lo está, la pista conserva el
// sintetizador de la plantilla de siempre (la canción se crea igual, y el resultado lo dice). Los ajustes de volumen se
// calibraron midiendo cada papel respecto a la batería para conservar el balance de la 0.6 (cs_render --compare10).
struct Sig2Sound { const char* role; const char* key; float trimDb; };
struct Sig2Def
{
    const char* id;
    const char* kit;      // kit de muestras ("" = se queda el kit sintetizado)
    float drumsTrimDb;
    float laneDb[kDrumLanes];
    std::vector<Sig2Sound> sounds;
};
const std::vector<Sig2Def>& sig2Defs()
{
    static const std::vector<Sig2Def> d = {
        // Ajustes medidos el 2026-11-14 con cs_render --compare10 (RMS de cada papel a solas, canción entera, misma
        // composición con sintetizador y con muestras). Los teclados y el colchón de muestras salen entre 14 y 21 dB más
        // fuertes que los sintetizadores a igual fader: sin este ajuste tres estilos recortaban la mezcla.
        { "trap", "cauce.drums.two:midnight-trap", -0.5f, { 0, 0, 0, 0, 0, 0, 0, 0 },
          { { "bass", "cauce.bass.two:808-punch", -6.0f }, { "pad", "cauce.keys.two:night-pad", -15.5f } } },
        { "reggaeton", "cauce.drums.two:calle-reggaeton", 0.0f, { 0, 0, 0, 0, 0, 0, 0, 0 },
          { { "bass", "cauce.bass.two:808-short", -5.5f }, { "pad", "cauce.keys.two:night-pad", -15.0f } } },
        { "hiphop", "cauce.drums.two:organic-hiphop", 0.5f, { 0, 0, 0, 0, 0, 0, 0, 0 },
          { { "bass", "cauce.bass.two:warm-sub", -4.0f }, { "chords", "cauce.keys.two:warm-ep", -21.0f } } },
        { "boombap", "cauce.drums.two:crate-boombap", 1.0f, { 0, 0, 0, 0, 0, 0, 0, 0 },
          { { "bass", "cauce.bass.electric:electric-bass", -1.5f }, { "chords", "cauce.keys.two:warm-ep", -18.0f } } },
        { "jazzrap", "cauce.drums.two:soul-pocket", -3.0f, { 0, 0, 0, 0, 0, 0, 0, 0 },
          { { "bass", "cauce.bass.electric:electric-bass", -1.0f }, { "chords", "cauce.keys.two:warm-ep", -14.0f } } },
        { "darkhiphop", "cauce.drums.two:midnight-trap", -1.0f, { 0, 0, 0, 0, 0, 0, 0, 0 },
          { { "bass", "cauce.bass.two:808-punch", -0.5f }, { "pad", "cauce.keys.two:night-pad", -14.5f } } },
        { "pop", "cauce.drums.two:organic-hiphop", -0.5f, { 0, 0, 0, 0, 0, 0, 0, 0 },
          { { "bass", "cauce.bass.electric:electric-bass", -1.0f }, { "chords", "cauce.keys.two:glass-pluck", -19.0f }, { "pad", "cauce.keys.two:night-pad", -15.5f } } },
        { "electronic", "", 0.0f, { 0, 0, 0, 0, 0, 0, 0, 0 },
          { { "chords", "cauce.keys.two:glass-pluck", -20.0f }, { "pad", "cauce.keys.two:night-pad", -16.0f } } },
    };
    return d;
}
const Sig2Def* findSig2 (const std::string& id)
{
    for (const auto& g : sig2Defs()) if (id == g.id) return &g;
    return nullptr;
}

const GenreDef* findGenre (const std::string& id)
{
    for (const auto& g : genreDefs()) if (id == g.id) return &g;
    return nullptr;
}

uint32_t fnv32 (const std::string& s)
{
    uint32_t h = 2166136261u;
    for (char c : s) { h ^= (uint8_t) c; h *= 16777619u; }
    return h;
}

ProcessorPtr configured (const std::string& type, std::initializer_list<std::pair<const char*, float>> values)
{
    ProcessorPtr fx = createProcessor (type);
    if (! fx) return nullptr;
    for (const auto& [k, v] : values) { const int i = parameterIndex (*fx, k); if (i >= 0) fx->setParameter (i, v); }
    return fx;
}
} // namespace

std::vector<TemplateInfo> projectTemplates()
{
    std::vector<TemplateInfo> out;
    for (const auto& g : genreDefs())
    {
        TemplateInfo i;
        i.id = g.id;
        i.bpm = g.bpm;
        i.kitId = drumKits()[(size_t) g.kit].id;
        i.tracks.push_back ("Drums");
        for (const auto& p : g.parts) i.tracks.push_back (p.name);
        i.tracks.push_back ("Voz");
        i.tracks.push_back ("Reverb");
        out.push_back (i);
    }
    return out;
}

// Rellena (o vuelve a rellenar) el contenido de una pista con papel en el plan. Mantiene ids, posiciones y
// duraciones de los clips: si el usuario movió o alargó un clip, la parte nueva se adapta a su duración.
void Session::composeTrackLocked (TrackModel& t, const MusicPlan& plan, const std::string& role)
{
    const Tick bar = (Tick) kPpq * proj.beatsPerBar;
    auto sectionFor = [&] (const MidiClipModel& c)
    {
        SongSection sec;
        const int startBar = (int) (c.start / bar);
        sec.name = "A";
        for (const auto& x : plan.sections)
            if (c.name == x.name || (c.name == "Remate" && startBar >= x.startBar && startBar < x.startBar + x.bars)) { sec = x; break; }
        sec.startBar = startBar;
        sec.bars = std::max<int> (1, (int) ((c.length + bar - 1) / bar));
        return sec;
    };
    const uint32_t seed = plan.seeds.count (role) ? plan.seeds.at (role) : 1u;
    if (role == "drums")
    {
        auto pats = composer::drumPatterns (plan, seed);
        if (plan.variants.count ("drums")) for (auto& [key, pat] : pats) composer::mutatePattern (plan, pat, plan.variants.at ("drums"), key);
        for (const auto& [key, id] : plan.patternIds)
        {
            DrumPattern* p = t.findPattern (id);
            auto it = pats.find (key);
            if (! p || it == pats.end()) continue;
            const std::string keepId = p->id, keepName = p->name;
            const int steps = p->steps;
            *p = it->second;
            p->id = keepId;
            p->name = keepName;
            if (steps == 32) // el usuario lo alargó a 2 compases: el segundo repite el primero
                for (auto* lanes : { &p->lanes, &p->rolls })
                    for (auto& l : *lanes) { l.resize (32); std::copy (l.begin(), l.begin() + 16, l.begin() + 16); }
            p->steps = steps;
        }
        return;
    }
    for (auto& c : t.midiClips)
    {
        if (! c.patternId.empty()) continue;
        c.notes = composer::partNotes (plan, role, sectionFor (c), seed, proj.beatsPerBar);
        c.notes.erase (std::remove_if (c.notes.begin(), c.notes.end(), [&] (const MidiNote& n) { return n.start >= c.length; }), c.notes.end());
        for (auto& n : c.notes) n.length = std::min<Tick> (n.length, c.length - n.start);
    }
}

void Session::applyTemplateLocked (const std::string& templateId)
{
    // "trap@2" = el estilo trap en modo «Signature 2» (0.10). Sin sufijo, la plantilla de siempre, sin ningún cambio.
    const size_t at = templateId.find ('@');
    const int style = at != std::string::npos && templateId.substr (at + 1) == "2" ? 2 : 1;
    const GenreDef* def = findGenre (templateId.substr (0, at));
    const Sig2Def* sig = def && style == 2 ? findSig2 (def->id) : nullptr;
    lastTemplateNotes.clear();
    proj.templateId = def ? def->id : "empty";
    const Tick bar = (Tick) kPpq * proj.beatsPerBar;
    proj.loopStart = (sig ? 4 : 2) * bar; // bucle preparado sobre la sección A o el primer verso (desactivado)
    proj.loopEnd = (sig ? 12 : 6) * bar;
    if (! def)
    {
        proj.loopStart = 0;
        proj.loopEnd = 4 * bar;
        undoStack.clear();
        return;
    }
    proj.bpm = def->bpm;
    MusicPlan plan = composer::defaultPlan (def->id, sig ? 2 : 1);

    // Bus de reverb compartido (se crea antes para poder enviar a él).
    addTrack ("Reverb", "bus");
    const std::string busId = proj.tracks.back().id;
    {
        TrackModel& b = proj.tracks.back();
        ProcessorPtr rev = configured ("cs.reverb", { { "size", def->reverbSize }, { "damping", def->reverbDamping }, { "mix", 1.0f }, { "width", 1.0f } });
        // Paso alto antes de la reverb: el bombo y el bajo no entran en ella (una reverb con graves emborrona la mezcla).
        ProcessorPtr hp = configured ("cs.filter", { { "mode", 1.0f }, { "cutoffHz", 240.0f }, { "resonance", 0.0f } });
        InsertModel cut; cut.id = newId ("fx"); cut.type = "cs.filter"; cut.state = hp->saveState();
        b.inserts.push_back (cut);
        InsertModel ins; ins.id = newId ("fx"); ins.type = "cs.reverb"; ins.state = rev->saveState();
        b.inserts.push_back (ins);
        b.volumeDb = -4.0f;
    }

    // Batería: patrones por clave y un clip por tramo de cada sección.
    addTrack ("Drums", "drums");
    {
        TrackModel& d = proj.tracks.back();
        nlohmann::json st = { { "kit", def->kit } };
        for (int l = 0; l < kDrumLanes; ++l)
        {
            st["pan" + std::to_string (l)] = def->lanePan[l];
            st["vol" + std::to_string (l)] = def->laneDb[l];
        }
        d.instrument.state = st.dump();
        d.patterns.clear();
        d.midiClips.clear();
        const auto pats = composer::drumPatterns (plan, plan.seeds["drums"]);
        static const char* order[] = { "intro", "main", "var", "varCrash", "fill", "break", "end" };
        for (const char* key : order)
        {
            auto it = pats.find (key);
            if (it == pats.end()) continue;
            DrumPattern p = it->second;
            p.id = newId ("pat");
            static const std::map<std::string, std::string> names = { { "intro", "Intro" }, { "main", "A" }, { "var", "B" }, { "varCrash", "B + crash" }, { "fill", "Remate" }, { "break", "Break" }, { "end", "Cierre" } };
            static const std::map<std::string, std::string> names2 = { { "intro", "Intro" }, { "main", "Verso" }, { "var", "Coro" }, { "varCrash", "Coro + platillo" }, { "fill", "Remate" }, { "break", "Puente" }, { "end", "Cierre" } };
            p.name = (sig ? names2 : names).at (key);
            plan.patternIds[key] = p.id;
            d.patterns.push_back (p);
        }
        for (const auto& sec : plan.sections)
        {
            Tick at = (Tick) sec.startBar * bar;
            for (const auto& slot : composer::drumLayout (plan, sec))
            {
                MidiClipModel c;
                c.id = newId ("mcl");
                c.name = slot.clipName;
                c.patternId = plan.patternIds[slot.patternKey];
                c.start = at;
                c.length = slot.bars * bar;
                d.midiClips.push_back (c);
                at += c.length;
            }
        }
        d.volumeDb = def->drumsDb;
        if (sig && sig->kit[0])
        {
            InstalledPack pack;
            PackInstrument info;
            nlohmann::json st2 = st;
            st2["sampleKit"] = sig->kit;
            auto inst = std::dynamic_pointer_cast<Instrument> (createProcessor ("cs.drums"));
            bool ok = packLibrary().findInstrument (sig->kit, pack, info) && info.kind == "drumkit" && inst;
            if (ok)
            {
                st2["sampleKitVersion"] = pack.manifest.version;
                for (int l = 0; l < kDrumLanes; ++l) st2["vol" + std::to_string (l)] = sig->laneDb[l];
                ok = inst->loadState (st2.dump());
                if (ok) { inst->prepare (proj.sampleRate, 1024); ok = samplerStatus (inst.get()).loaded; }
            }
            if (ok) { d.instrument.state = inst->saveState(); d.volumeDb = def->drumsDb + sig->drumsTrimDb; }
            else lastTemplateNotes.push_back (std::string ("Batería: falta el kit ") + sig->kit + "; se usa el kit sintetizado");
        }
        d.sends[busId] = def->drumsSend;
        // Compresión suave de «pegamento» en la batería (más pegada sin aplastar los transitorios).
        ProcessorPtr comp = configured ("cs.compressor", { { "thresholdDb", -14 }, { "ratio", 2.5f }, { "attackMs", 18 }, { "releaseMs", 120 }, { "makeupDb", 1.5f } });
        InsertModel ins; ins.id = newId ("fx"); ins.type = "cs.compressor"; ins.state = comp->saveState();
        d.inserts.push_back (ins);
        plan.roles[d.id] = "drums";
    }

    // Partes melódicas: un clip por sección (vacío si la parte calla en esa sección: sigue siendo editable).
    for (const auto& part : def->parts)
    {
        addTrack (part.name, "synth");
        TrackModel& s = proj.tracks.back();
        s.instrument.state = synthPresetState (part.preset);
        s.instrument.preset = part.preset;
        s.midiClips.clear();
        for (const auto& sec : plan.sections)
        {
            MidiClipModel c;
            c.id = newId ("mcl");
            c.name = sec.name;
            c.start = (Tick) sec.startBar * bar;
            c.length = (Tick) sec.bars * bar;
            s.midiClips.push_back (c);
        }
        s.volumeDb = part.volDb;
        if (sig)
            for (const auto& snd : sig->sounds)
            {
                if (std::string (snd.role) != part.role) continue;
                InstalledPack pack;
                PackInstrument info;
                auto inst = std::dynamic_pointer_cast<Instrument> (createProcessor ("cs.sampler"));
                bool ok = packLibrary().findInstrument (snd.key, pack, info) && info.kind == "melodic" && inst;
                if (ok) ok = inst->loadState (nlohmann::json { { "instrument", snd.key }, { "packVersion", pack.manifest.version } }.dump());
                if (ok) { inst->prepare (proj.sampleRate, 1024); ok = samplerStatus (inst.get()).loaded; }
                if (ok)
                {
                    s.instrument.type = "cs.sampler";
                    s.instrument.state = inst->saveState();
                    s.instrument.preset = snd.key;
                    s.volumeDb = part.volDb + snd.trimDb;
                }
                else lastTemplateNotes.push_back (std::string (part.name) + ": falta " + snd.key + "; se usa el sintetizador");
            }
        s.pan = part.pan;
        if (part.send > 0) s.sends[busId] = part.send;
        if (std::string (part.role) == "melody")
        {
            // Eco en corchea con puntillo, ping-pong, oscuro y discreto: profundidad sin ensuciar.
            const float dotted8 = (float) (0.75 * 60000.0 / def->bpm);
            ProcessorPtr dly = configured ("cs.delay", { { "timeMs", dotted8 }, { "feedback", 0.28f }, { "mix", 0.16f }, { "toneHz", 4200 }, { "pingPong", 1 } });
            InsertModel ins; ins.id = newId ("fx"); ins.type = "cs.delay"; ins.state = dly->saveState();
            s.inserts.push_back (ins);
        }
        const std::string role = part.role;
        if (role == "pad" || role == "chords")
        {
            // Menos graves en acordes y colchón: dejan sitio al bajo y al bombo.
            ProcessorPtr eq = configured ("cs.eq", { { "lowDb", role == "pad" ? -7.0f : -4.0f } });
            InsertModel ins; ins.id = newId ("fx"); ins.type = "cs.eq"; ins.state = eq->saveState();
            s.inserts.push_back (ins);
        }
        plan.roles[s.id] = part.role;
        composeTrackLocked (s, plan, part.role);
    }
    addTrack ("Voz", "audio");
    proj.tracks.back().armed = true;
    proj.tracks.back().sends[busId] = 0.15f;
    // El bus va al final de la lista (orden visual: instrumentos, voz, retorno).
    std::rotate (proj.tracks.begin(), proj.tracks.begin() + 1, proj.tracks.end());
    proj.masterDb = -1.0f;
    proj.music = plan.toJson();
    undoStack.clear(); // crear el proyecto no se deshace
    redoStack.clear();
    instrumentInstances.clear();
}

// --- Variaciones ----------------------------------------------------------------------------------------------
// Hotfix 0.9.2: en el teléfono «Variar» y «Regenerar» parecían no hacer nada. El motor sí cambiaba las notas, pero
// (1) la interfaz no confirmaba nada, (2) algunos cambios eran mínimos (Variar en Electrónica tocaba sobre todo el
// bombo y el bajo, que el altavoz del teléfono casi no reproduce, y el Pad no cambiaba nunca) y (3) nada impedía que
// una semilla diera un resultado casi idéntico. Ahora cada operación mide cuánto cambió cada parte (proporción de notas
// o golpes distintos), reintenta con otra semilla derivada si el cambio no llega al mínimo y devuelve un informe que
// la interfaz enseña («Versión 3: cambiaron Batería 24 %, Arp 61 %…»). Ver docs/HOTFIX_0.9.2.md.
namespace
{
std::set<std::string> musicalEvents (const TrackModel& t)
{
    std::set<std::string> e;
    for (const auto& c : t.midiClips)
        // El inicio se redondea a 1/16 de negra: la humanización (0.10, hasta 14 ticks) no cuenta como otra nota.
        for (const auto& n : c.notes) e.insert (std::to_string (c.start) + "|" + std::to_string ((n.start + 30) / 60) + "|" + std::to_string (n.key));
    for (const auto& p : t.patterns)
        for (size_t l = 0; l < p.lanes.size(); ++l)
            for (size_t s = 0; s < p.lanes[l].size(); ++s)
                if (p.lanes[l][s] > 0) e.insert (p.id + "|" + std::to_string (l) + "|" + std::to_string (s) + "|" + std::to_string (l < p.rolls.size() && s < p.rolls[l].size() ? p.rolls[l][s] : 0));
    return e;
}
// 0 = idénticas, 1 = nada en común (1 - índice de Jaccard).
double eventChange (const TrackModel& a, const TrackModel& b)
{
    const auto x = musicalEvents (a), y = musicalEvents (b);
    if (x.empty() && y.empty()) return 0.0;
    size_t inter = 0;
    for (const auto& k : x) inter += y.count (k);
    return 1.0 - (double) inter / (double) (x.size() + y.size() - inter);
}
uint32_t attemptSeed (uint32_t seed, int attempt)
{
    if (attempt == 0) return seed;
    uint32_t s = seed ^ (0x9E3779B9u * (uint32_t) attempt);
    s ^= s >> 16; s *= 0x7FEB352Du; s ^= s >> 15; // mezcla para que los intentos no se parezcan entre sí
    return s ? s : 1u;
}
} // namespace

std::string Session::lastVariationReport() const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    return lastVariation;
}

OpResult Session::variationLocked (const char* op, const std::string& part, uint32_t seed, double minChange,
                                   const std::function<std::vector<std::string> (MusicPlan&, uint32_t)>& apply)
{
    lastVariation.clear();
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    MusicPlan plan0;
    if (proj.music.empty() || ! MusicPlan::fromJson (proj.music, plan0))
        return OpResult::fail ("este proyecto no tiene plan musical (se creó antes de la versión 0.4 o vacío)");
    std::vector<std::string> skipped; // pistas que tocaría pero están bloqueadas
    for (const auto& t : proj.tracks)
    {
        auto it = plan0.roles.find (t.id);
        if (it == plan0.roles.end() || (part != "all" && it->second != part)) continue;
        if (std::find (plan0.locked.begin(), plan0.locked.end(), t.id) != plan0.locked.end()) skipped.push_back (t.name);
    }
    const ProjectModel before = proj;
    constexpr int kAttempts = 8;
    double bestScore = -1;
    ProjectModel best;
    MusicPlan bestPlan;
    std::vector<std::pair<std::string, double>> bestParts; // id de pista -> cambio
    int bestAttempt = 0;
    bool anyTarget = false;
    for (int a = 0; a < kAttempts; ++a)
    {
        proj = before;
        MusicPlan plan = plan0;
        const std::vector<std::string> touched = apply (plan, attemptSeed (seed, a));
        if (touched.empty()) break; // nada que cambiar (todo bloqueado o borrado): no tiene sentido reintentar
        anyTarget = true;
        double sum = 0;
        std::vector<std::pair<std::string, double>> parts;
        for (const auto& id : touched)
        {
            const TrackModel* x = before.findTrack (id);
            const TrackModel* y = proj.findTrack (id);
            const double c = x && y ? eventChange (*x, *y) : 0.0;
            parts.emplace_back (id, c);
            sum += c;
        }
        const double score = sum / (double) touched.size();
        if (score > bestScore) { bestScore = score; best = proj; bestPlan = plan; bestParts = parts; bestAttempt = a; }
        if (score >= minChange) break;
    }
    proj = before;
    if (! anyTarget) return OpResult::fail (skipped.empty() ? "no hay pistas generadas que cambiar (¿se borraron?)" : "todas las pistas de esa parte están bloqueadas: quita el candado para variarlas");
    if (bestScore <= 0.0) return OpResult::fail ("no se encontró una versión distinta de la actual; prueba otra parte o «Regenerar»");
    pushUndo();
    proj = std::move (best);
    bestPlan.revision = plan0.revision + 1;
    proj.music = bestPlan.toJson();
    json rep = { { "op", op }, { "part", part }, { "revision", bestPlan.revision }, { "attempts", bestAttempt + 1 },
                 { "change", std::round (bestScore * 1000.0) / 1000.0 }, { "significant", bestScore >= minChange } };
    json ps = json::array();
    for (const auto& [id, c] : bestParts)
    {
        const TrackModel* t = proj.findTrack (id);
        ps.push_back ({ { "trackId", id }, { "name", t ? t->name : id }, { "role", bestPlan.roles.count (id) ? bestPlan.roles.at (id) : "" },
                        { "change", std::round (c * 1000.0) / 1000.0 } });
    }
    rep["parts"] = ps;
    rep["skippedLocked"] = skipped;
    lastVariation = rep.dump();
    return finishEditLocked();
}

OpResult Session::varyPart (const std::string& part, uint32_t seed, float energy, bool setEnergy)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    static const std::vector<std::string> parts = { "drums", "bass", "chords", "melody", "pad" };
    if (part != "all" && std::find (parts.begin(), parts.end(), part) == parts.end()) return OpResult::fail ("parte desconocida: " + part);
    if (setEnergy && ! std::isfinite (energy)) return OpResult::fail ("energía no válida");
    // Con semilla nueva («Rehacer una parte») se exige un cambio claro; solo energía: basta con que algo cambie.
    const bool newSeed = seed != 0;
    return variationLocked ("vary", part, newSeed ? seed : 1u, newSeed ? 0.25 : 0.0, [&] (MusicPlan& plan, uint32_t s)
    {
        std::vector<std::string> touched;
        if (setEnergy) plan.energy = std::clamp (energy, -1.0f, 1.0f);
        for (auto& t : proj.tracks)
        {
            auto it = plan.roles.find (t.id);
            if (it == plan.roles.end()) continue;
            const std::string role = it->second;
            if (part != "all" && role != part) continue;
            if (std::find (plan.locked.begin(), plan.locked.end(), t.id) != plan.locked.end()) continue;
            if (newSeed)
            {
                uint32_t ns = part == "all" ? s ^ fnv32 (role) : s;
                if (role == "drums") // como en Regenerar: otra familia rítmica, no solo otros adornos
                {
                    const auto cur = plan.seeds.find (role);
                    const int was = composer::drumGroove (cur != plan.seeds.end() ? cur->second : 11u);
                    for (int k = 0; k < 64 && composer::drumGroove (ns) == was; ++k) ns = ns * 1664525u + 1013904223u;
                }
                plan.seeds[role] = ns;
                plan.variants.erase (role);
            }
            composeTrackLocked (t, plan, role);
            touched.push_back (t.id);
        }
        return touched;
    });
}

OpResult Session::mutatePart (const std::string& part, uint32_t seed)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    static const std::vector<std::string> parts = { "drums", "bass", "chords", "melody", "pad" };
    if (part != "all" && std::find (parts.begin(), parts.end(), part) == parts.end()) return OpResult::fail ("parte desconocida: " + part);
    if (seed == 0) seed = 1;
    // Variar conserva la identidad: se pide un cambio perceptible pero moderado (ver TestVariations.cpp).
    return variationLocked ("mutate", part, seed, 0.15, [&] (MusicPlan& plan, uint32_t s)
    {
        std::vector<std::string> touched;
        for (auto& t : proj.tracks)
        {
            auto it = plan.roles.find (t.id);
            if (it == plan.roles.end()) continue;
            const std::string role = it->second;
            if (part != "all" && role != part) continue;
            if (std::find (plan.locked.begin(), plan.locked.end(), t.id) != plan.locked.end()) continue;
            plan.variants[role] = (s ^ fnv32 (role)) | 1u;
            composeTrackLocked (t, plan, role);
            touched.push_back (t.id);
        }
        return touched;
    });
}

OpResult Session::regenerateBeat (uint32_t seed)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (seed == 0) seed = 1;
    return variationLocked ("regenerate", "all", seed, 0.40, [&] (MusicPlan& plan, uint32_t s)
    {
        std::vector<std::string> touched;
        // ¿Hay alguna parte armónica bloqueada? Entonces la progresión no cambia (lo bloqueado debe seguir encajando).
        bool harmonyLocked = false;
        for (const auto& [tid, role] : plan.roles)
            if (role != "drums" && std::find (plan.locked.begin(), plan.locked.end(), tid) != plan.locked.end()) harmonyLocked = true;
        if (! harmonyLocked)
        {
            auto bank = composer::progressionBank (plan.genre);
            bank.erase (std::remove (bank.begin(), bank.end(), plan.progression), bank.end());
            if (! bank.empty()) plan.progression = bank[(size_t) (s % (uint32_t) bank.size())];
        }
        for (auto& t : proj.tracks)
        {
            auto it = plan.roles.find (t.id);
            if (it == plan.roles.end()) continue;
            const std::string role = it->second;
            if (std::find (plan.locked.begin(), plan.locked.end(), t.id) != plan.locked.end()) continue;
            uint32_t ns = (s * 2654435761u) ^ fnv32 (role) ^ 0xBEA7u;
            if (role == "drums") // la batería regenerada cambia siempre de familia rítmica (no solo de adornos)
            {
                const auto cur = plan.seeds.find (role);
                const int was = composer::drumGroove (cur != plan.seeds.end() ? cur->second : 11u);
                for (int k = 0; k < 64 && composer::drumGroove (ns) == was; ++k) ns = ns * 1664525u + 1013904223u;
            }
            plan.seeds[role] = ns;
            plan.variants.erase (role);
            composeTrackLocked (t, plan, role);
            touched.push_back (t.id);
        }
        return touched;
    });
}

// 0.10: cuánto se «toca a mano» (0 = cuadrado, 1 = máximo). Vuelve a escribir las partes generadas sin candado con las
// mismas semillas: las notas son las mismas, cambian su colocación fina y su fuerza. Se puede deshacer.
OpResult Session::setHumanize (float amount)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (! std::isfinite (amount)) return OpResult::fail ("valor no válido");
    MusicPlan plan;
    if (proj.music.empty() || ! MusicPlan::fromJson (proj.music, plan)) return OpResult::fail ("este proyecto no tiene plan musical");
    plan.humanize = std::clamp (amount, 0.0f, 1.0f);
    pushUndo();
    for (auto& t : proj.tracks)
    {
        auto it = plan.roles.find (t.id);
        if (it == plan.roles.end()) continue;
        if (std::find (plan.locked.begin(), plan.locked.end(), t.id) != plan.locked.end()) continue;
        composeTrackLocked (t, plan, it->second);
    }
    proj.music = plan.toJson();
    return finishEditLocked();
}

std::vector<std::string> Session::templateNotes() const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    return lastTemplateNotes;
}

OpResult Session::setPartLocked (const std::string& trackId, bool locked)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    MusicPlan plan;
    if (proj.music.empty() || ! MusicPlan::fromJson (proj.music, plan)) return OpResult::fail ("este proyecto no tiene plan musical");
    if (! plan.roles.count (trackId)) return OpResult::fail ("la pista no tiene una parte generada");
    auto& v = plan.locked;
    v.erase (std::remove (v.begin(), v.end(), trackId), v.end());
    if (locked) v.push_back (trackId);
    proj.music = plan.toJson();
    touchLocked();
    return {};
}
} // namespace cs
