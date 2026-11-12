// Session: instrumentos, patrones de batería, clips MIDI, bucle y plantillas de género (fase 1.2).
#include "cs/Composer.h"
#include "cs/Instruments.h"
#include "cs/Session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

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
    if (std::find (types.begin(), types.end(), type) == types.end()) return OpResult::fail ("efecto desconocido: " + type);
    ProcessorPtr fx = createProcessor (type);
    if (! fx) return OpResult::fail ("efecto no disponible");
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
// Cada plantilla es un arreglo de 16 compases (Intro, A, B, Break, Final) creado por el compositor local
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
        { "trap", 140, 5, -3.0f, 0.06f,
          { { "808", "cauce_808", "bass", -5.0f, 0.0f, 0.0f }, { "Melodía", "dark_bell", "melody", -10.0f, 0.12f, 0.32f }, { "Pad", "air_pad", "pad", -15.0f, 0.0f, 0.35f } },
          0.78f, 0.45f, { 0, 0, 0.05f, 0.22f, 0.28f, -0.3f, 0.3f, -0.12f }, { 0, -1, -2, -4, -6, -6, -5, -8 } },
        { "reggaeton", 95, 4, -3.0f, 0.07f,
          { { "Bajo", "dembow_bass", "bass", -5.0f, 0.0f, 0.0f }, { "Keys", "urban_pluck", "chords", -12.0f, -0.18f, 0.22f },
            { "Melodía", "dark_bell", "melody", -11.0f, 0.15f, 0.3f }, { "Pad", "air_pad", "pad", -17.0f, 0.0f, 0.3f } },
          0.62f, 0.5f, { 0, 0, 0.05f, 0.2f, 0.25f, -0.35f, 0.32f, 0.4f }, { 0, -1, -3, -5, -7, -4, -4, -9 } },
        { "hiphop", 88, 6, -3.0f, 0.08f,
          // Bajo 2 dB por debajo de los otros géneros y charles más presentes: el primer render salía con un 95 % de la
          // energía bajo 250 Hz y casi nada por encima de 8 kHz (sonido apagado).
          { { "Bajo", "cauce_sub", "bass", -7.5f, 0.0f, 0.0f }, { "Keys", "warm_keys", "chords", -8.0f, -0.12f, 0.25f },
            { "Melodía", "urban_pluck", "melody", -11.0f, 0.2f, 0.3f } },
          0.55f, 0.6f, { 0, 0, -0.1f, 0.18f, 0.22f, -0.25f, 0.35f, -0.15f }, { 0, -1, -4, -1, -4, -5, -6, -9 } },
        { "pop", 108, 7, -3.0f, 0.06f,
          { { "Bajo", "synth_bass", "bass", -6.0f, 0.0f, 0.0f }, { "Keys", "bright_pluck", "chords", -12.0f, -0.2f, 0.25f },
            { "Melodía", "bright_lead", "melody", -12.0f, 0.15f, 0.25f }, { "Pad", "air_pad", "pad", -17.0f, 0.0f, 0.3f } },
          0.6f, 0.4f, { 0, 0, 0.0f, 0.2f, 0.25f, -0.3f, 0.35f, -0.15f }, { 0, -1, -3, -4, -6, -5, -7, -8 } },
        { "electronic", 124, 7, -3.0f, 0.05f,
          { { "Bajo", "synth_bass", "bass", -6.0f, 0.0f, 0.0f }, { "Stabs", "bright_pluck", "chords", -13.0f, -0.2f, 0.25f },
            { "Arp", "electro_arp", "melody", -13.0f, 0.2f, 0.28f }, { "Pad", "air_pad", "pad", -16.0f, 0.0f, 0.35f } },
          0.72f, 0.45f, { 0, 0, 0.0f, 0.2f, 0.25f, -0.3f, 0.3f, -0.15f }, { 0, -1, -3, -5, -6, -5, -7, -8 } },
    };
    return d;
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
    const GenreDef* def = findGenre (templateId);
    proj.templateId = def ? def->id : "empty";
    const Tick bar = (Tick) kPpq * proj.beatsPerBar;
    proj.loopStart = 2 * bar; // bucle preparado sobre la sección A (desactivado)
    proj.loopEnd = 6 * bar;
    if (! def)
    {
        proj.loopStart = 0;
        proj.loopEnd = 4 * bar;
        undoStack.clear();
        return;
    }
    proj.bpm = def->bpm;
    MusicPlan plan = composer::defaultPlan (def->id);

    // Bus de reverb compartido (se crea antes para poder enviar a él).
    addTrack ("Reverb", "bus");
    const std::string busId = proj.tracks.back().id;
    {
        TrackModel& b = proj.tracks.back();
        ProcessorPtr rev = configured ("cs.reverb", { { "size", def->reverbSize }, { "damping", def->reverbDamping }, { "mix", 1.0f }, { "width", 1.0f } });
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
        static const char* order[] = { "intro", "main", "var", "varCrash", "fill", "break" };
        for (const char* key : order)
        {
            auto it = pats.find (key);
            if (it == pats.end()) continue;
            DrumPattern p = it->second;
            p.id = newId ("pat");
            static const std::map<std::string, std::string> names = { { "intro", "Intro" }, { "main", "A" }, { "var", "B" }, { "varCrash", "B + crash" }, { "fill", "Remate" }, { "break", "Break" } };
            p.name = names.at (key);
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
OpResult Session::varyPart (const std::string& part, uint32_t seed, float energy, bool setEnergy)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    MusicPlan plan;
    if (proj.music.empty() || ! MusicPlan::fromJson (proj.music, plan))
        return OpResult::fail ("este proyecto no tiene plan musical (se creó antes de la versión 0.4 o vacío)");
    static const std::vector<std::string> parts = { "drums", "bass", "chords", "melody", "pad" };
    if (part != "all" && std::find (parts.begin(), parts.end(), part) == parts.end()) return OpResult::fail ("parte desconocida: " + part);
    if (setEnergy && ! std::isfinite (energy)) return OpResult::fail ("energía no válida");
    pushUndo();
    if (setEnergy) plan.energy = std::clamp (energy, -1.0f, 1.0f);
    int changed = 0;
    for (auto& t : proj.tracks)
    {
        auto it = plan.roles.find (t.id);
        if (it == plan.roles.end()) continue;
        const std::string& role = it->second;
        if (part != "all" && role != part) continue;
        if (std::find (plan.locked.begin(), plan.locked.end(), t.id) != plan.locked.end()) continue;
        if (seed != 0) plan.seeds[role] = part == "all" ? seed ^ fnv32 (role) : seed;
        composeTrackLocked (t, plan, role);
        ++changed;
    }
    if (changed == 0) { undoStack.pop_back(); return OpResult::fail ("no hay pistas que cambiar (¿bloqueadas o borradas?)"); }
    proj.music = plan.toJson();
    return finishEditLocked();
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
