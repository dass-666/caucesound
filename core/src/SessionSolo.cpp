// Integración de Solo AI (misión 0.4, etapa C): «Generar melodía o solo». El cálculo va fuera del cerrojo de la
// sesión (soloRequestLocked → generateMelody sin cerrojo → addSoloTrack), así que nunca toca el hilo de audio ni
// bloquea otros comandos. El resultado entra como una pista nueva con un clip MIDI editable (no destructivo):
// deshacer la quita entera.
#include "cs/Composer.h"
#include "cs/Instruments.h"
#include "cs/Session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <set>

namespace cs
{
using nlohmann::json;
OpResult Session::soloRequest (int startBar, int bars, int keyRoot, bool minor, solo::MelodyRequest& out) const
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (! solo::available()) return OpResult::fail ("Solo AI no está incluido en esta compilación");
    if (startBar < 0 || bars < 1 || bars > 64) return OpResult::fail ("compases fuera de rango (1-64)");
    MusicPlan plan;
    const bool hasPlan = ! proj.music.empty() && MusicPlan::fromJson (proj.music, plan) && ! plan.progression.empty();
    out = {};
    out.bpm = proj.bpm;
    if (hasPlan && keyRoot < 0)
    {
        // Tonalidad y acordes confirmados: vienen del plan guardado, no de un análisis del audio.
        out.keyRoot = plan.keyRoot;
        out.minor = plan.minor;
        const auto sc = composer::scale (plan);
        for (int b = 0; b < bars; ++b)
        {
            const int bar = startBar + b;
            const int root = sc[(size_t) plan.progression[(size_t) bar % plan.progression.size()]] % 12;
            std::set<int> iv;
            for (int k : composer::chordAt (plan, bar, false)) iv.insert (((k - root) % 12 + 12) % 12);
            solo::ChordSpec c;
            c.root = root;
            c.quality = iv.count (3) ? (iv.count (6) ? 2 : 1) : (iv.count (8) ? 3 : 0);
            out.chords.push_back (c);
        }
        return {};
    }
    // Sin plan: se usa la tonalidad que el usuario confirmó (a mano o tras analizar un audio). Si tampoco hay, la
    // interfaz debe pedirla: nunca se adivina.
    if (keyRoot < 0 && ! proj.harmony.empty())
    {
        const json h = json::parse (proj.harmony, nullptr, false);
        if (h.is_object() && h.value ("keyRoot", -1) >= 0 && h.value ("keyRoot", -1) <= 11)
        {
            out.keyRoot = h.value ("keyRoot", 0);
            out.minor = h.value ("minor", false);
            // Los acordes detectados solo valen si el proyecto va al tempo con el que se midieron (los compases coinciden).
            const double hb = h.value ("chordsBpm", 0.0);
            const json ch = h.value ("chords", json::array());
            if (! ch.empty() && hb > 0 && std::fabs (hb - proj.bpm) / proj.bpm < 0.015)
            {
                for (int b = 0; b < bars; ++b)
                {
                    const json& c = ch[(size_t) (startBar + b) % ch.size()];
                    out.chords.push_back ({ std::clamp (c.value ("root", 0), 0, 11), std::clamp (c.value ("quality", 0), 0, 3) });
                }
                return {};
            }
            keyRoot = out.keyRoot;
            minor = out.minor;
        }
    }
    if (keyRoot < 0 || keyRoot > 11) return OpResult::fail ("key_required");
    out.keyRoot = keyRoot;
    out.minor = minor;
    // Sin acordes conocidos: I - VI - IV - V (menor: i - VI - iv - v) sobre la tonalidad elegida, uno por compás.
    static const int majDeg[4] = { 0, 9, 5, 7 }, minDeg[4] = { 0, 8, 5, 7 };
    static const int majQ[4] = { 0, 1, 0, 0 }, minQ[4] = { 1, 0, 1, 1 };
    for (int b = 0; b < bars; ++b)
        out.chords.push_back ({ (keyRoot + (minor ? minDeg : majDeg)[b % 4]) % 12, (minor ? minQ : majQ)[b % 4] });
    return {};
}

OpResult Session::addSoloTrack (const std::string& projectId, Tick start, const std::string& name, const std::vector<MidiNote>& notes, const std::string& preset,
                                const std::string& replaceTrackId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open || proj.id != projectId) return OpResult::fail ("el proyecto cambió mientras se generaba la melodía");
    if (notes.empty()) return OpResult::fail ("no hay notas");
    const std::string state = synthPresetState (preset);
    if (state.empty()) return OpResult::fail ("preset desconocido");
    if (! replaceTrackId.empty())
    {
        // Escuchar otra alternativa: se sustituyen las notas de la pista de Solo AI anterior (un paso de deshacer).
        const TrackModel* t0 = proj.findTrack (replaceTrackId);
        if (! t0) return OpResult::fail ("la pista de Solo AI ya no existe");
        if (t0->kind != "synth" || t0->name.rfind ("Solo AI", 0) != 0 || t0->midiClips.size() != 1)
            return OpResult::fail ("esa pista no es una melodía de Solo AI: no se sustituye");
        MusicPlan plan;
        if (! proj.music.empty() && MusicPlan::fromJson (proj.music, plan) && std::find (plan.locked.begin(), plan.locked.end(), replaceTrackId) != plan.locked.end())
            return OpResult::fail ("la pista está bloqueada: desbloquéala para cambiar su melodía");
        pushUndo();
        TrackModel* t = proj.findTrack (replaceTrackId);
        Tick end = 0;
        for (const auto& n : notes) end = std::max (end, n.start + n.length);
        const Tick bar = (Tick) kPpq * proj.beatsPerBar;
        auto& c = t->midiClips.front();
        c.name = name;
        c.start = start;
        c.length = std::max (bar, (end + bar - 1) / bar * bar);
        c.notes = notes;
        t->name = name;
        if (t->instrument.preset != preset)
        {
            t->instrument.state = state;
            t->instrument.preset = preset;
            if (auto inst = instrumentFor (*t)) inst->loadState (state);
        }
        finishEditLocked();
        OpResult r;
        r.id = replaceTrackId;
        return r;
    }
    auto r = addTrack (name, "synth"); // un paso de deshacer: quitarla devuelve el proyecto a como estaba
    if (! r.ok) return r;
    TrackModel* t = proj.findTrack (r.id);
    Tick end = 0;
    for (const auto& n : notes) end = std::max (end, n.start + n.length);
    const Tick bar = (Tick) kPpq * proj.beatsPerBar;
    MidiClipModel c;
    c.id = newId ("mcl");
    c.name = name;
    c.start = start;
    c.length = std::max (bar, (end + bar - 1) / bar * bar);
    c.notes = notes;
    t->midiClips.clear();
    t->midiClips.push_back (c);
    t->instrument.state = state;
    t->instrument.preset = preset;
    t->volumeDb = -10.0f;
    if (auto inst = instrumentFor (*t)) inst->loadState (state);
    finishEditLocked();
    return r;
}

OpResult Session::soloAnalyze (const std::string& clipId)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (! solo::available()) return OpResult::fail ("Solo AI no está incluido en esta compilación");
    const ClipModel* c = proj.findClip (clipId);
    if (! c) return OpResult::fail ("clip no encontrado");
    auto it = pool.find (c->sourceId);
    if (it == pool.end() || ! it->second->audio) return OpResult::fail ("falta el audio de este clip");
    std::shared_ptr<const AudioData> in = it->second->audio;
    const Frame from = c->sourceOffset, length = c->length;
    if (from < 0 || length < 1 || from + length > in->numFrames()) return OpResult::fail ("el clip no coincide con su audio");
    if ((double) length / in->sampleRate > 600.0) return OpResult::fail ("el audio es demasiado largo para analizarlo (máximo 10 minutos)");
    if ((double) length / in->sampleRate < 4.0) return OpResult::fail ("hacen falta al menos 4 segundos de audio para analizar el tempo");
    // El análisis no escribe en el proyecto: su resultado se lee con jobStatus y el usuario decide qué aplicar.
    const std::string id = jobs.submit ("soloAnalyze", proj.id, clipId, [in, from, length, clipId] (JobContext& ctx, std::string& result, std::string& error)
    {
        ctx.progress = 0.05f;
        const solo::Analysis a = solo::analyze (*in, from, length);
        if (! a.ok) { error = a.error.empty() ? "no se pudo analizar el audio" : a.error; return false; }
        json j { { "clipId", clipId }, { "durationSec", a.durationSec }, { "seconds", a.seconds }, { "silent", a.silent },
                 { "percussiveOnly", a.percussiveOnly }, { "tonal", a.tonal }, { "bpm", a.bpm }, { "bpmConfidence", a.bpmConfidence },
                 { "firstBeatSec", a.firstBeatSec }, { "beats", a.beats }, { "bars", a.bars }, { "keyRoot", a.keyRoot }, { "minor", a.minor },
                 { "keyConfidence", a.keyConfidence }, { "chordConfidence", a.chordConfidenceMean }, { "warnings", a.warnings }, { "summary", a.summary } };
        json bc = json::array(), kc = json::array(), ch = json::array();
        for (const auto& b : a.bpmCandidates) bc.push_back ({ { "bpm", b.first }, { "score", b.second } });
        for (const auto& k : a.keyCandidates) kc.push_back ({ { "root", k.root }, { "minor", k.minor }, { "score", k.score } });
        for (size_t i = 0; i < a.chords.size(); ++i)
            ch.push_back ({ { "root", a.chords[i].root }, { "quality", a.chords[i].quality }, { "confidence", i < a.chordConfidence.size() ? a.chordConfidence[i] : 0.0 } });
        j["bpmCandidates"] = bc;
        j["keyCandidates"] = kc;
        j["chords"] = ch;
        result = j.dump();
        return true;
    });
    if (id.empty()) return OpResult::fail ("hay demasiados procesos en cola: espera a que termine alguno");
    OpResult r;
    r.id = id;
    return r;
}

OpResult Session::setHarmony (int keyRoot, bool minor, const std::vector<solo::ChordSpec>& chords, double chordsBpm, const std::string& source)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open) return OpResult::fail ("no hay proyecto abierto");
    if (keyRoot > 11) return OpResult::fail ("tonalidad no válida");
    if (chords.size() > 256) return OpResult::fail ("demasiados acordes");
    pushUndo();
    if (keyRoot < 0) proj.harmony.clear();
    else
    {
        json ch = json::array();
        for (const auto& c : chords) ch.push_back ({ { "root", std::clamp (c.root, 0, 11) }, { "quality", std::clamp (c.quality, 0, 3) } });
        proj.harmony = json { { "keyRoot", keyRoot }, { "minor", minor }, { "confirmed", true }, { "source", source },
                              { "chords", ch }, { "chordsBpm", chords.empty() ? 0.0 : chordsBpm } }.dump();
    }
    return finishEditLocked();
}
} // namespace cs
