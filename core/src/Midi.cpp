#include "cs/Midi.h"

#include <algorithm>
#include <cmath>

namespace cs
{
Frame ticksToFrames (Tick t, double bpm, int sampleRate)
{
    return (Frame) std::llround ((double) t * framesPerTick (bpm, sampleRate));
}

Tick framesToTicks (Frame f, double bpm, int sampleRate)
{
    return (Tick) std::llround ((double) f / framesPerTick (bpm, sampleRate));
}

void sortNotes (std::vector<MidiNote>& notes)
{
    std::stable_sort (notes.begin(), notes.end(), [] (const MidiNote& a, const MidiNote& b)
    {
        if (a.start != b.start) return a.start < b.start;
        return a.key < b.key;
    });
}

void quantizeNotes (std::vector<MidiNote>& notes, Tick grid, float strength, bool lengthsToo)
{
    if (grid <= 0) return;
    strength = std::clamp (strength, 0.0f, 1.0f);
    for (auto& n : notes)
    {
        const Tick target = (Tick) std::llround ((double) n.start / (double) grid) * grid;
        n.start = std::max<Tick> (0, n.start + (Tick) std::llround ((double) (target - n.start) * strength));
        if (lengthsToo)
        {
            const Tick lt = std::max<Tick> (grid, (Tick) std::llround ((double) n.length / (double) grid) * grid);
            n.length = std::max<Tick> (1, n.length + (Tick) std::llround ((double) (lt - n.length) * strength));
        }
    }
    sortNotes (notes);
}

Tick stepTick (int step, float swing)
{
    Tick t = (Tick) step * kStepTicks;
    if (step % 2 == 1) t += (Tick) std::llround (std::clamp (swing, 0.0f, 1.0f) * kStepTicks * 0.5);
    return t;
}

void sortEvents (std::vector<SchedEvent>& ev)
{
    std::stable_sort (ev.begin(), ev.end(), [] (const SchedEvent& a, const SchedEvent& b)
    {
        if (a.frame != b.frame) return a.frame < b.frame;
        const bool aOff = (a.status & 0xF0) == 0x80, bOff = (b.status & 0xF0) == 0x80;
        return aOff && ! bOff; // Note Off primero
    });
}

namespace
{
void addNote (std::vector<SchedEvent>& out, Tick absStart, Tick absEnd, int key, int vel, int ch, double bpm, int sr)
{
    const Frame f0 = ticksToFrames (absStart, bpm, sr);
    Frame f1 = ticksToFrames (absEnd, bpm, sr);
    if (f1 <= f0) f1 = f0 + 1;
    const uint8_t c = (uint8_t) std::clamp (ch, 0, 15);
    out.push_back ({ f0, (uint8_t) (0x90 | c), (uint8_t) std::clamp (key, 0, 127), (uint8_t) std::clamp (vel, 1, 127) });
    out.push_back ({ f1, (uint8_t) (0x80 | c), (uint8_t) std::clamp (key, 0, 127), 0 });
}
} // namespace

void clipNotesAbsolute (const MidiClipModel& clip, const DrumPattern* pattern, std::vector<AbsNote>& out)
{
    const Tick clipEnd = clip.start + clip.length;
    if (clip.length <= 0) return;
    if (pattern)
    {
        const Tick patLen = (Tick) pattern->steps * kStepTicks;
        if (patLen <= 0) return;
        for (Tick rep = 0; rep < clip.length; rep += patLen)
            for (int lane = 0; lane < (int) pattern->lanes.size() && lane < kDrumLanes; ++lane)
                for (int s = 0; s < pattern->steps && s < (int) pattern->lanes[(size_t) lane].size(); ++s)
                {
                    const int vel = pattern->lanes[(size_t) lane][(size_t) s];
                    if (vel <= 0) continue;
                    const Tick rel = rep + (Tick) s * kStepTicks;      // la rejilla decide si el paso cabe
                    if (rel >= clip.length) continue;
                    const Tick at = clip.start + rep + stepTick (s, pattern->swing);
                    if (at >= clipEnd) continue;
                    const int roll = std::min (4, pattern->rollAt (lane, s));
                    if (roll <= 1) { out.push_back ({ at, std::min (at + kStepTicks, clipEnd), kDrumBaseNote + lane, vel, 9 }); continue; }
                    // Redoble: 'roll' golpes repartidos en el paso, con una pequeña subida de intensidad hacia el último.
                    const Tick sub = kStepTicks / roll;
                    for (int r = 0; r < roll; ++r)
                    {
                        const Tick ra = at + r * sub;
                        if (ra >= clipEnd) break;
                        const int rv = std::clamp ((int) std::lround (vel * (0.72 + 0.28 * r / (roll - 1.0))), 1, 127);
                        out.push_back ({ ra, std::min (ra + sub, clipEnd), kDrumBaseNote + lane, rv, 9 });
                    }
                }
        return;
    }
    for (const auto& n : clip.notes)
    {
        if (n.start < 0 || n.start >= clip.length || n.length <= 0) continue;
        const Tick s0 = clip.start + n.start;
        out.push_back ({ s0, std::min (s0 + n.length, clipEnd), n.key, n.velocity, n.channel });
    }
}

void appendClipEvents (const MidiClipModel& clip, const DrumPattern* pattern, double bpm, int sampleRate, std::vector<SchedEvent>& out)
{
    // Una sola fuente de verdad para lo que suena y lo que se exporta como MIDI (Smf.cpp).
    std::vector<AbsNote> notes;
    clipNotesAbsolute (clip, pattern, notes);
    for (const auto& n : notes) addNote (out, n.start, n.end, n.key, n.velocity, n.channel, bpm, sampleRate);
}

std::string validateNote (const MidiNote& n)
{
    if (n.key < 0 || n.key > 127) return "nota fuera de rango";
    if (n.velocity < 1 || n.velocity > 127) return "velocidad fuera de rango";
    if (n.channel < 0 || n.channel > 15) return "canal MIDI fuera de rango";
    if (n.start < 0 || n.length <= 0) return "posición o duración de nota no válida";
    return {};
}

std::string validatePattern (const DrumPattern& p)
{
    if (p.id.empty()) return "patrón sin id";
    if (p.steps != 16 && p.steps != 32) return "el patrón debe tener 16 o 32 pasos";
    if (! std::isfinite (p.swing) || p.swing < 0 || p.swing > 1) return "swing fuera de rango";
    if (p.lanes.size() != (size_t) kDrumLanes) return "el patrón debe tener 8 carriles";
    for (const auto& l : p.lanes)
    {
        if (l.size() != (size_t) p.steps) return "carril con número de pasos incorrecto";
        for (int v : l) if (v < 0 || v > 127) return "velocidad de paso fuera de rango";
    }
    if (! p.rolls.empty())
    {
        if (p.rolls.size() != (size_t) kDrumLanes) return "redobles: deben ser 8 carriles";
        for (const auto& l : p.rolls)
        {
            if (l.size() != (size_t) p.steps) return "redobles: número de pasos incorrecto";
            for (int v : l) if (v < 0 || v > 4) return "redoble fuera de rango (0-4)";
        }
    }
    return {};
}
} // namespace cs
