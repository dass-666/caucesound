#include "cs/SoloBridge.h"

#include <algorithm>
#include <chrono>

#ifdef CS_WITH_SOLO
#include "solo/MelodyExplorer.h"
#include "solo/MusicTheory.h"
#endif

namespace cs::solo
{
const char* pinnedVersion() { return "solo-ai-studio@fe8445b"; }

#ifndef CS_WITH_SOLO
bool available() { return false; }
MelodyResult generateMelody (const MelodyRequest&) { MelodyResult r; r.error = "Solo AI no está incluido en esta compilación"; return r; }
#else
bool available() { return true; }

MelodyResult generateMelody (const MelodyRequest& req)
{
    MelodyResult r;
    if (req.chords.empty() || req.chords.size() > 64) { r.error = "hacen falta entre 1 y 64 compases de acordes"; return r; }
    if (req.keyRoot < 0 || req.keyRoot > 11) { r.error = "tonalidad no válida"; return r; }
    if (! (req.bpm >= 30 && req.bpm <= 300)) { r.error = "tempo no válido"; return r; }
    if (req.proposal < 0 || req.proposal > 3) { r.error = "propuesta no válida (0-3)"; return r; }
    const auto t0 = std::chrono::steady_clock::now();
    try
    {
        ::solo::Key key;
        key.tonic = req.keyRoot;
        key.mode = req.minor ? ::solo::Mode::Minor : ::solo::Mode::Major;
        ::solo::Progression prog;
        for (const auto& c : req.chords)
        {
            ::solo::Chord ch;
            ch.root = ((c.root % 12) + 12) % 12;
            ch.quality = c.quality == 1 ? ::solo::ChordQuality::Minor : c.quality == 2 ? ::solo::ChordQuality::Diminished
                       : c.quality == 3 ? ::solo::ChordQuality::Augmented : ::solo::ChordQuality::Major;
            prog.bars.push_back (ch);
        }
        const auto ctx = ::solo::MelodyContext::manual (key, prog, req.bpm);
        ::solo::MelodyBrief brief;
        brief.type = req.type == "solo" ? ::solo::MelodyType::Solo : ::solo::MelodyType::Chorus;
        brief.freedom = std::clamp (req.freedom, 0.0, 100.0);
        brief.instrument = ::solo::InstrumentId::Synth;
        brief.seed = req.seed == 0 ? 1 : req.seed;
        const auto id = ::solo::MelodyExplorer::createIdentity (ctx, brief);
        const auto p = ::solo::MelodyExplorer::proposeOne (ctx, brief, id, (::solo::ProposalStrategy) req.proposal);
        r.name = p.name;
        for (const auto& n : p.sequence.notes)
        {
            MidiNote m;
            m.key = std::clamp (n.targetPitch(), 0, 127);
            m.velocity = std::clamp (n.velocity, 1, 127);
            m.channel = 0;
            m.start = (Tick) n.startTick;
            m.length = (Tick) std::max<int64_t> (1, n.lengthTicks);
            r.notes.push_back (m);
        }
        const auto fit = ::solo::harmonicFit (p.sequence, prog, key);
        r.inScale = fit.inScale;
        r.strongChordTones = fit.strongChordTones;
        r.ok = ! r.notes.empty();
        if (! r.ok) r.error = "Solo AI no devolvió notas";
    }
    catch (const std::exception& e) { r.error = std::string ("Solo AI: ") + e.what(); }
    r.millis = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    return r;
}
#endif
} // namespace cs::solo
