#include "cs/SoloBridge.h"

#include <algorithm>
#include <chrono>

#ifdef CS_WITH_SOLO
#include "solo/BeatAnalyzer.h"
#include "solo/MelodyExplorer.h"
#include "solo/MusicTheory.h"
#endif

namespace cs::solo
{
const char* pinnedVersion() { return "solo-ai-studio@fe8445b"; }

#ifndef CS_WITH_SOLO
bool available() { return false; }
MelodyResult generateMelody (const MelodyRequest&) { MelodyResult r; r.error = "Solo AI no está incluido en esta compilación"; return r; }
Family proposeFamily (const MelodyRequest&) { Family f; f.error = "Solo AI no está incluido en esta compilación"; return f; }
Analysis analyze (const AudioData&, Frame, Frame) { Analysis a; a.error = "Solo AI no está incluido en esta compilación"; return a; }
#else
bool available() { return true; }

namespace
{
std::string validate (const MelodyRequest& req)
{
    if (req.chords.empty() || req.chords.size() > 64) return "hacen falta entre 1 y 64 compases de acordes";
    if (req.keyRoot < 0 || req.keyRoot > 11) return "tonalidad no válida";
    if (! (req.bpm >= 30 && req.bpm <= 300)) return "tempo no válido";
    if (req.proposal < 0 || req.proposal > 3) return "propuesta no válida (0-3)";
    return {};
}

struct Prepared { ::solo::Key key; ::solo::Progression prog; ::solo::MelodyContext ctx; ::solo::MelodyBrief brief; };

Prepared prepare (const MelodyRequest& req)
{
    Prepared p;
    p.key.tonic = req.keyRoot;
    p.key.mode = req.minor ? ::solo::Mode::Minor : ::solo::Mode::Major;
    for (const auto& c : req.chords)
    {
        ::solo::Chord ch;
        ch.root = ((c.root % 12) + 12) % 12;
        ch.quality = c.quality == 1 ? ::solo::ChordQuality::Minor : c.quality == 2 ? ::solo::ChordQuality::Diminished
                   : c.quality == 3 ? ::solo::ChordQuality::Augmented : ::solo::ChordQuality::Major;
        p.prog.bars.push_back (ch);
    }
    p.ctx = ::solo::MelodyContext::manual (p.key, p.prog, req.bpm);
    p.brief.type = req.type == "solo" ? ::solo::MelodyType::Solo : req.type == "interlude" ? ::solo::MelodyType::Interlude
                 : req.type == "intro" ? ::solo::MelodyType::Intro : req.type == "outro" ? ::solo::MelodyType::Outro
                 : req.type == "background" ? ::solo::MelodyType::Background : ::solo::MelodyType::Chorus;
    p.brief.character = req.character == "emotional" ? ::solo::ChorusCharacter::Emotional
                      : req.character == "energetic" ? ::solo::ChorusCharacter::Energetic : ::solo::ChorusCharacter::EasyToSing;
    p.brief.freedom = std::clamp (req.freedom, 0.0, 100.0);
    p.brief.instrument = ::solo::InstrumentId::Synth; // la guitarra modelada de Solo AI no se usa (diferencia ARM/x86 sin resolver)
    p.brief.seed = req.seed == 0 ? 1 : req.seed;
    return p;
}

MelodyResult toResult (const ::solo::MelodyProposal& p, const Prepared& pr, const MelodyRequest& req, int profile, bool variation)
{
    MelodyResult r;
    r.name = profileName (profile);
    if (variation) r.name += " · variación";
    r.description = profileDescription (profile);
    r.similarity = p.similarityToIdentity;
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
    r.notes = applyProfile (r.notes, profile, req);
    const auto fit = measureProfile (r.notes, req);
    r.inScale = fit.inScale;
    r.strongChordTones = fit.strongChordTones;
    r.ok = ! r.notes.empty();
    if (! r.ok) r.error = "Solo AI no devolvió notas";
    return r;
}
} // namespace

MelodyResult generateMelody (const MelodyRequest& req)
{
    MelodyResult r;
    r.error = validate (req);
    if (! r.error.empty()) return r;
    const auto t0 = std::chrono::steady_clock::now();
    try
    {
        const Prepared pr = prepare (req);
        const auto id = ::solo::MelodyExplorer::createIdentity (pr.ctx, pr.brief);
        auto p = ::solo::MelodyExplorer::proposeOne (pr.ctx, pr.brief, id, (::solo::ProposalStrategy) req.proposal);
        if (req.variation != 0)
        {
            // Variación relacionada de la propuesta elegida (la lógica es de Solo AI: MelodyExplorer::vary).
            const auto v = ::solo::MelodyExplorer::vary (pr.ctx, pr.brief, id, p, 1, req.variation, {});
            if (v.empty() || v.front().sequence.notes.empty()) { r.error = "Solo AI no pudo variar esta melodía"; return r; }
            p = v.front();
        }
        if (req.endingSeed != 0)
        {
            const int bars = (int) req.chords.size();
            const int from = std::max (0, bars - std::clamp (req.endingBars, 1, bars));
            p.sequence = ::solo::MelodyExplorer::regenerateRegion (pr.ctx, p, from, bars - 1, req.endingSeed, {});
            if (p.sequence.notes.empty()) { r.error = "Solo AI no pudo cambiar la frase final"; return r; }
        }
        r = toResult (p, pr, req, req.proposal, req.variation != 0);
    }
    catch (const std::exception& e) { r.ok = false; r.error = std::string ("Solo AI: ") + e.what(); }
    r.millis = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
    return r;
}

Family proposeFamily (const MelodyRequest& req)
{
    Family f;
    f.error = validate (req);
    if (! f.error.empty()) return f;
    try
    {
        const Prepared pr = prepare (req);
        const auto fam = ::solo::MelodyExplorer::propose (pr.ctx, pr.brief);
        for (size_t i = 0; i < fam.proposals.size(); ++i) f.proposals.push_back (toResult (fam.proposals[i], pr, req, (int) i, false));
        // Diversidad medida sobre lo que de verdad entra en la pista: 1 - solapamiento medio de notas entre pares.
        double ov = 0; int pairs = 0;
        for (size_t i = 0; i < f.proposals.size(); ++i)
            for (size_t j = i + 1; j < f.proposals.size(); ++j) { ov += onsetOverlap (f.proposals[i].notes, f.proposals[j].notes); ++pairs; }
        f.diversity = pairs > 0 ? 1.0 - ov / pairs : 0.0;
        f.ok = f.proposals.size() == 4;
        for (const auto& p : f.proposals) f.ok = f.ok && p.ok;
        if (! f.ok) f.error = "Solo AI no devolvió las cuatro propuestas";
    }
    catch (const std::exception& e) { f.ok = false; f.error = std::string ("Solo AI: ") + e.what(); }
    return f;
}

Analysis analyze (const AudioData& audio, Frame from, Frame length)
{
    Analysis a;
    if (audio.numChannels() < 1 || from < 0 || length < 1 || from + length > audio.numFrames()) { a.error = "rango fuera del audio"; return a; }
    const auto t0 = std::chrono::steady_clock::now();
    try
    {
        ::solo::MonoAudio mono;
        mono.sampleRate = audio.sampleRate;
        mono.sourceChannels = audio.numChannels();
        mono.samples.resize ((size_t) length);
        const float g = 1.0f / (float) audio.numChannels();
        for (const auto& ch : audio.channels)
            for (Frame i = 0; i < length; ++i) mono.samples[(size_t) i] += ch[(size_t) (from + i)] * g;
        const auto r = ::solo::analyseBeat (mono);
        a.ok = r.ok;
        a.error = r.error;
        a.warnings = r.warnings;
        a.durationSec = r.durationSec;
        a.silent = r.silent;
        a.percussiveOnly = r.percussiveOnly;
        a.tonal = r.tonal;
        a.bpm = r.bpm;
        a.bpmConfidence = r.bpmConfidence;
        for (const auto& c : r.bpmCandidates) a.bpmCandidates.push_back ({ c.bpm, c.score });
        a.firstBeatSec = r.firstBeatSec;
        a.beats = (int) r.beatsSec.size();
        a.bars = r.bars;
        if (r.tonal)
        {
            a.keyRoot = r.key.tonic;
            a.minor = r.key.mode == ::solo::Mode::Minor;
            a.keyConfidence = r.keyConfidence;
            for (const auto& k : r.keyCandidates) a.keyCandidates.push_back ({ k.key.tonic, k.key.mode == ::solo::Mode::Minor, k.score });
            if (! r.harmony.empty())
                for (int b = 0; b < r.harmony.bars && b < 256; ++b)
                {
                    const auto ch = r.harmony.chordAtBar (b);
                    // El motor de CAUCESOUND trabaja con tríadas: las séptimas y suspendidas se reducen a su tríada base.
                    using Q = ::solo::ChordQuality;
                    const int q = (ch.quality == Q::Minor || ch.quality == Q::Minor7) ? 1
                                : (ch.quality == Q::Diminished || ch.quality == Q::HalfDiminished7) ? 2 : ch.quality == Q::Augmented ? 3 : 0;
                    a.chords.push_back ({ ch.root, q });
                    a.chordConfidence.push_back (r.harmony.chordConfidenceAtBar (b));
                }
            a.chordConfidenceMean = r.chordConfidence;
        }
        a.summary = r.summary();
    }
    catch (const std::exception& e) { a.ok = false; a.error = std::string ("Solo AI: ") + e.what(); }
    a.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    return a;
}
#endif
} // namespace cs::solo
