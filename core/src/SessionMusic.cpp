// Session: instrumentos, patrones de batería, clips MIDI, bucle y plantillas de género (fase 1.2).
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

OpResult Session::loadSynthPreset (const std::string& trackId, const std::string& presetId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    OpResult err;
    TrackModel* t = instrumentTrack (trackId, "synth", err);
    if (! t) return err;
    const std::string state = synthPresetState (presetId);
    if (state.empty()) return OpResult::fail ("preset desconocido");
    auto inst = instrumentFor (*t);
    if (! inst || ! inst->loadState (state)) return OpResult::fail ("no se pudo cargar el preset");
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
    proj.findTrack (trackId)->findPattern (patternId)->lanes[(size_t) lane][(size_t) step] = velocity;
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
    proj.findTrack (trackId)->findPattern (patternId)->lanes = lanes;
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

// --- Plantillas --------------------------------------------------------------------------------------------------
namespace
{
// Ritmos de partida (patrones rítmicos genéricos de cada género, escritos aquí; no son material de terceros).
// Cada cadena tiene 16 caracteres: '.' apagado, 'x' 100, 'X' 127, 'o' 70 (fantasma).
std::vector<int> steps (const char* s)
{
    std::vector<int> v (16, 0);
    for (int i = 0; i < 16 && s[i]; ++i) v[(size_t) i] = s[i] == 'X' ? 127 : s[i] == 'x' ? 100 : s[i] == 'o' ? 70 : 0;
    return v;
}

struct TemplateDef
{
    const char* id;
    double bpm;
    int kit;
    float swing;
    const char* lanes[kDrumLanes];
    std::vector<std::pair<const char*, const char*>> synths; // nombre, preset
    int prog[4][2];                                          // acordes por compás: {raíz MIDI, 1 = menor}
};

// Rellena un clip de 8 compases con material básico propio (progresión de 4 acordes repetida), según el papel
// del preset: bajos = raíz; pad/keys = acordes; pluck/lead = arpegio. Son patrones genéricos, no citas de obras.
void fillTemplateClip (MidiClipModel& c, const std::string& preset, const int (&prog)[4][2], int beatsPerBar)
{
    const Tick beat = kPpq, bar = beat * beatsPerBar;
    c.notes.clear();
    auto norm = [] (int k, int lo) { k %= 12; while (k < lo) k += 12; return k; };
    for (int b = 0; b < 8; ++b)
    {
        const int root = prog[b % 4][0];
        const bool minor = prog[b % 4][1] != 0;
        const Tick t0 = b * bar;
        const int bass = norm (root, 36);
        const int r4 = norm (root, 60);
        const int chord[3] = { r4, r4 + (minor ? 3 : 4), r4 + 7 };
        if (preset == "bass_808")
            c.notes.push_back ({ bass, 110, 0, t0, bar - beat / 2 });
        else if (preset == "sub_bass")
        {
            c.notes.push_back ({ bass, 105, 0, t0, beat * 2 - kStepTicks });
            c.notes.push_back ({ bass, 95, 0, t0 + beat * 2 + beat / 2, beat - kStepTicks });
        }
        else if (preset == "synth_bass")
            for (int i = 0; i < beatsPerBar * 2; ++i) c.notes.push_back ({ bass, i % 2 == 0 ? 108 : 84, 0, t0 + i * beat / 2, beat / 2 - kStepTicks / 2 });
        else if (preset == "pad")
            for (int k : chord) c.notes.push_back ({ k, 80, 0, t0, bar - kStepTicks });
        else if (preset == "soft_keys")
            for (int h = 0; h < 2; ++h)
                for (int k : chord) c.notes.push_back ({ k, h == 0 ? 92 : 76, 0, t0 + h * bar / 2 + (h ? beat / 2 : 0), bar / 2 - beat / 2 - kStepTicks });
        else // pluck, lead: arpegio en corcheas, una octava arriba en la segunda mitad del compás
            for (int i = 0; i < beatsPerBar * 2; ++i)
            {
                const int k = chord[(size_t) (i % 3)] + (i >= beatsPerBar ? 12 : 0);
                c.notes.push_back ({ k, 96 - (i % 2) * 14, 0, t0 + i * beat / 2, beat / 2 - kStepTicks / 4 });
            }
    }
    sortNotes (c.notes);
}

const std::vector<TemplateDef>& templates()
{
    static const std::vector<TemplateDef> t = {
        { "trap", 140, 0, 0.0f,
          { "X......x..x.....", "........X.......", "........x.......", "x.x.x.x.x.xxx.x.", "................", "...............o", "................", "................" },
          { { "808", "bass_808" }, { "Melodía", "pluck" } }, { { 57, 1 }, { 53, 0 }, { 48, 0 }, { 55, 0 } } },
        { "reggaeton", 95, 1, 0.0f,
          { "X...x...X...x...", "...x..x....x..x.", "................", "x.x.x.x.x.x.x.x.", "................", "......o.......o.", "..o.......o.....", "o.o.o.o.o.o.o.o." },
          { { "Bajo", "synth_bass" }, { "Keys", "soft_keys" } }, { { 50, 1 }, { 46, 0 }, { 53, 0 }, { 48, 0 } } },
        { "hiphop", 90, 2, 0.3f,
          { "X......x.xX.....", "....X.......X...", "................", "x.x.x.x.x.x.x.x.", "................", "..............o.", "................", "................" },
          { { "Bajo", "sub_bass" }, { "Keys", "soft_keys" } }, { { 50, 1 }, { 57, 1 }, { 46, 0 }, { 45, 0 } } },
        { "pop", 105, 3, 0.0f,
          { "X.......X.x.....", "....X.......X...", "....x.......x...", "x.x.x.x.x.x.x.x.", "................", "................", "................", "................" },
          { { "Bajo", "synth_bass" }, { "Pad", "pad" }, { "Keys", "soft_keys" } }, { { 48, 0 }, { 55, 0 }, { 57, 1 }, { 53, 0 } } },
        { "electronic", 124, 3, 0.0f,
          { "X...X...X...X...", "................", "....x.......x...", "oooooooooooooooo", "..x...x...x...x.", "................", "................", "................" },
          { { "Bajo", "synth_bass" }, { "Lead", "lead" }, { "Pad", "pad" } }, { { 53, 1 }, { 49, 0 }, { 56, 0 }, { 51, 0 } } },
    };
    return t;
}
} // namespace

std::vector<TemplateInfo> projectTemplates()
{
    std::vector<TemplateInfo> out;
    for (const auto& t : templates())
    {
        TemplateInfo i;
        i.id = t.id;
        i.bpm = t.bpm;
        i.kitId = drumKits()[(size_t) t.kit].id;
        i.tracks.push_back ("Drums");
        for (const auto& s : t.synths) i.tracks.push_back (s.first);
        i.tracks.push_back ("Voz");
        out.push_back (i);
    }
    return out;
}

void Session::applyTemplateLocked (const std::string& templateId)
{
    // Proyecto vacío (o plantilla desconocida): una pista de voz, como antes.
    const TemplateDef* def = nullptr;
    for (const auto& t : templates()) if (templateId == t.id) def = &t;
    proj.templateId = def ? def->id : "empty";
    const Tick bar = (Tick) kPpq * proj.beatsPerBar;
    proj.loopStart = 0;
    proj.loopEnd = 4 * bar;
    if (! def)
    {
        undoStack.clear();
        return;
    }
    proj.bpm = def->bpm;
    addTrack ("Drums", "drums");
    TrackModel& d = proj.tracks.back();
    nlohmann::json st = { { "kit", def->kit } };
    d.instrument.state = st.dump();
    DrumPattern& p = d.patterns.front();
    for (int l = 0; l < kDrumLanes; ++l) p.lanes[(size_t) l] = steps (def->lanes[l]);
    p.swing = def->swing;
    d.midiClips.front().length = 8 * bar;
    d.volumeDb = -4.0f;
    for (const auto& [name, preset] : def->synths)
    {
        addTrack (name, "synth");
        TrackModel& s = proj.tracks.back();
        s.instrument.state = synthPresetState (preset);
        s.instrument.preset = preset;
        s.midiClips.front().length = 8 * bar;
        fillTemplateClip (s.midiClips.front(), preset, def->prog, proj.beatsPerBar);
        s.volumeDb = -9.0f; // margen: batería + 2-3 synths a 0 dB saturaban el máster (detectado por la prueba de plantillas)
    }
    addTrack ("Voz", "audio");
    proj.tracks.back().armed = true;
    undoStack.clear(); // crear el proyecto no se deshace
    redoStack.clear();
    instrumentInstances.clear();
}
} // namespace cs
