// Integración de Solo AI (misión 0.4, etapa C): «Generar melodía o solo». El cálculo va fuera del cerrojo de la
// sesión (soloRequestLocked → generateMelody sin cerrojo → addSoloTrack), así que nunca toca el hilo de audio ni
// bloquea otros comandos. El resultado entra como una pista nueva con un clip MIDI editable (no destructivo):
// deshacer la quita entera.
#include "cs/Composer.h"
#include "cs/Instruments.h"
#include "cs/Session.h"

#include <algorithm>
#include <set>

namespace cs
{
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
    // Sin plan (proyecto anterior a la 0.4 o vacío) la tonalidad no es fiable: la interfaz debe pedirla.
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

OpResult Session::addSoloTrack (const std::string& projectId, Tick start, const std::string& name, const std::vector<MidiNote>& notes, const std::string& preset)
{
    std::lock_guard<std::recursive_mutex> l (mtx);
    if (! open || proj.id != projectId) return OpResult::fail ("el proyecto cambió mientras se generaba la melodía");
    if (notes.empty()) return OpResult::fail ("no hay notas");
    const std::string state = synthPresetState (preset);
    if (state.empty()) return OpResult::fail ("preset desconocido");
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
} // namespace cs
