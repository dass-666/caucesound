#include "cs/Smf.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace cs
{
namespace
{
// Letras con tilde y eñe (UTF-8 de dos bytes) a su letra base.
char baseLetter (unsigned char lead, unsigned char next)
{
    if (lead != 0xC3) return 0;
    switch (next)
    {
        case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: return 'a';
        case 0x80: case 0x81: case 0x82: case 0x83: case 0x84: return 'A';
        case 0xA8: case 0xA9: case 0xAA: case 0xAB: return 'e';
        case 0x88: case 0x89: case 0x8A: case 0x8B: return 'E';
        case 0xAC: case 0xAD: case 0xAE: case 0xAF: return 'i';
        case 0x8C: case 0x8D: case 0x8E: case 0x8F: return 'I';
        case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: return 'o';
        case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: return 'O';
        case 0xB9: case 0xBA: case 0xBB: case 0xBC: return 'u';
        case 0x99: case 0x9A: case 0x9B: case 0x9C: return 'U';
        case 0xB1: return 'n';
        case 0x91: return 'N';
        case 0xA7: return 'c';
        case 0x87: return 'C';
        default: return 0;
    }
}


void be16 (std::vector<uint8_t>& b, uint32_t v) { b.push_back ((uint8_t) (v >> 8)); b.push_back ((uint8_t) v); }
void be32 (std::vector<uint8_t>& b, uint32_t v) { be16 (b, v >> 16); be16 (b, v & 0xFFFF); }
void varLen (std::vector<uint8_t>& b, uint64_t v)
{
    uint8_t tmp[10];
    int n = 0;
    tmp[n++] = (uint8_t) (v & 0x7F);
    while ((v >>= 7) > 0) tmp[n++] = (uint8_t) ((v & 0x7F) | 0x80);
    while (n > 0) b.push_back (tmp[--n]);
}
void meta (std::vector<uint8_t>& b, uint64_t delta, uint8_t type, const uint8_t* data, size_t size)
{
    varLen (b, delta);
    b.push_back (0xFF);
    b.push_back (type);
    varLen (b, size);
    b.insert (b.end(), data, data + size);
}
void metaText (std::vector<uint8_t>& b, uint8_t type, const std::string& text)
{
    const std::string t = text.substr (0, 120);
    meta (b, 0, type, (const uint8_t*) t.data(), t.size());
}
void chunk (std::vector<uint8_t>& out, const char* id, const std::vector<uint8_t>& body)
{
    out.insert (out.end(), id, id + 4);
    be32 (out, (uint32_t) body.size());
    out.insert (out.end(), body.begin(), body.end());
}
struct Ev { Tick tick; int order; uint8_t status, d1, d2; }; // order 0 = note off (antes que un note on en el mismo tick)
} // namespace

std::string asciiText (const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        const unsigned char c = (unsigned char) text[i];
        if (c < 0x80) { out.push_back (c >= 0x20 && c != 0x7F ? (char) c : ' '); continue; }
        const unsigned char next = i + 1 < text.size() ? (unsigned char) text[i + 1] : 0;
        if (const char b = baseLetter (c, next)) { out.push_back (b); ++i; continue; }
        // Cualquier otro carácter (·, —, emoji…): un guion, y se saltan sus bytes de continuación.
        if (c >= 0xC0) { if (out.empty() || out.back() != '-') out.push_back ('-'); }
        while (i + 1 < text.size() && ((unsigned char) text[i + 1] & 0xC0) == 0x80) ++i;
    }
    return out;
}

SmfExport writeSmf (const ProjectModel& p, const std::string& onlyTrackId)
{
    SmfExport ex;
    std::vector<std::vector<uint8_t>> bodies;

    // Pista 0: nombre, compás y tempo.
    {
        std::vector<uint8_t> b;
        metaText (b, 0x03, asciiText (p.name));
        const int num = std::clamp (p.beatsPerBar, 1, 32);
        const uint8_t ts[4] = { (uint8_t) num, 2, 24, 8 }; // num/4; 24 relojes MIDI por negra; 8 fusas por negra
        meta (b, 0, 0x58, ts, 4);
        const uint32_t us = (uint32_t) std::clamp (std::llround (60000000.0 / std::max (1.0, p.bpm)), 1LL, 0xFFFFFFLL);
        const uint8_t tempo[3] = { (uint8_t) (us >> 16), (uint8_t) (us >> 8), (uint8_t) us };
        meta (b, 0, 0x51, tempo, 3);
        meta (b, 0, 0x2F, nullptr, 0);
        bodies.push_back (std::move (b));
    }

    int melodic = 0;
    for (const auto& t : p.tracks)
    {
        if (t.instrument.type.empty() || t.midiClips.empty()) continue;
        if (! onlyTrackId.empty() && t.id != onlyTrackId) continue;
        std::vector<AbsNote> notes;
        for (const auto& c : t.midiClips) clipNotesAbsolute (c, c.patternId.empty() ? nullptr : t.findPattern (c.patternId), notes);
        if (notes.empty()) continue;
        const bool drums = t.kind == "drums";
        int channel = 9;
        if (! drums) { static const int ch[15] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15 }; channel = ch[melodic++ % 15]; }

        std::vector<Ev> evs;
        evs.reserve (notes.size() * 2);
        for (const auto& n : notes)
        {
            const Tick s = std::max<Tick> (0, n.start), e = std::max (s + 1, n.end);
            const uint8_t key = (uint8_t) std::clamp (n.key, 0, 127);
            evs.push_back ({ s, 1, (uint8_t) (0x90 | channel), key, (uint8_t) std::clamp (n.velocity, 1, 127) });
            evs.push_back ({ e, 0, (uint8_t) (0x80 | channel), key, 0 });
        }
        std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.tick != b.tick ? a.tick < b.tick : a.order < b.order; });

        std::vector<uint8_t> b;
        metaText (b, 0x03, asciiText (t.name));
        Tick last = 0;
        for (const auto& e : evs)
        {
            varLen (b, (uint64_t) (e.tick - last));
            last = e.tick;
            b.push_back (e.status); b.push_back (e.d1); b.push_back (e.d2); // sin «running status»: más compatible
        }
        meta (b, 0, 0x2F, nullptr, 0);
        bodies.push_back (std::move (b));

        SmfTrackInfo info;
        info.trackId = t.id; info.name = asciiText (t.name); info.index = (int) bodies.size() - 1; info.channel = channel;
        info.notes = (int) notes.size(); info.drums = drums;
        ex.totalNotes += info.notes;
        ex.tracks.push_back (info);
    }

    std::vector<uint8_t> head;
    be16 (head, 1);
    be16 (head, (uint32_t) bodies.size());
    be16 (head, (uint32_t) kPpq);
    chunk (ex.bytes, "MThd", head);
    for (const auto& b : bodies) chunk (ex.bytes, "MTrk", b);
    return ex;
}

bool parseSmf (const std::vector<uint8_t>& d, SmfParsed& out, std::string& error)
{
    out = SmfParsed();
    size_t p = 0;
    auto need = [&] (size_t n) { return p + n <= d.size(); };
    auto rd16 = [&] { const uint32_t v = ((uint32_t) d[p] << 8) | d[p + 1]; p += 2; return v; };
    auto rd32 = [&] { const uint32_t a = rd16(); return (a << 16) | rd16(); };
    if (! need (14) || std::string (d.begin(), d.begin() + 4) != "MThd") { error = "sin cabecera MThd"; return false; }
    p = 4;
    if (rd32() != 6) { error = "cabecera de tamaño inesperado"; return false; }
    out.format = (int) rd16();
    const int ntrk = (int) rd16();
    out.division = (int) rd16();
    for (int t = 0; t < ntrk; ++t)
    {
        if (! need (8) || std::string (d.begin() + (long) p, d.begin() + (long) p + 4) != "MTrk") { error = "falta una pista MTrk"; return false; }
        p += 4;
        const size_t len = rd32();
        if (! need (len)) { error = "pista truncada"; return false; }
        const size_t end = p + len;
        SmfParsedTrack tr;
        int64_t tick = 0;
        uint8_t running = 0;
        std::map<int, std::vector<std::pair<int64_t, int>>> open; // (canal<<8 | nota) -> (tick, velocidad)
        bool sawEnd = false;
        while (p < end)
        {
            uint64_t delta = 0;
            for (int i = 0; i < 5 && p < end; ++i) { const uint8_t c = d[p++]; delta = (delta << 7) | (c & 0x7F); if (! (c & 0x80)) break; }
            tick += (int64_t) delta;
            if (p >= end) { error = "evento truncado"; return false; }
            uint8_t st = d[p];
            if (st & 0x80) ++p; else st = running;
            if (st == 0xFF)
            {
                if (p >= end) { error = "meta truncado"; return false; }
                const uint8_t type = d[p++];
                uint64_t n = 0;
                for (int i = 0; i < 5 && p < end; ++i) { const uint8_t c = d[p++]; n = (n << 7) | (c & 0x7F); if (! (c & 0x80)) break; }
                if (p + n > end) { error = "meta truncado"; return false; }
                if (type == 0x03) tr.name.assign (d.begin() + (long) p, d.begin() + (long) (p + n));
                if (type == 0x51 && n == 3) out.bpm = 60000000.0 / (double) (((uint32_t) d[p] << 16) | ((uint32_t) d[p + 1] << 8) | d[p + 2]);
                if (type == 0x58 && n == 4) { out.numerator = d[p]; out.denominator = 1 << d[p + 1]; }
                if (type == 0x2F) sawEnd = true;
                p += n;
                continue;
            }
            if (st == 0xF0 || st == 0xF7) { error = "SysEx no esperado"; return false; }
            running = st;
            const int hi = st & 0xF0, ch = st & 0x0F;
            const int nData = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            if (p + (size_t) nData > end) { error = "evento truncado"; return false; }
            const int d1 = d[p], d2 = nData == 2 ? d[p + 1] : 0;
            p += (size_t) nData;
            const int k = (ch << 8) | d1;
            if (hi == 0x90 && d2 > 0) open[k].push_back ({ tick, d2 });
            else if (hi == 0x80 || (hi == 0x90 && d2 == 0))
            {
                auto& v = open[k];
                if (v.empty()) { error = "note off sin note on"; return false; }
                tr.notes.push_back ({ v.front().first, tick - v.front().first, d1, v.front().second, ch });
                v.erase (v.begin());
            }
        }
        for (const auto& [k, v] : open) if (! v.empty()) { error = "nota sin cerrar"; return false; }
        if (! sawEnd) { error = "pista sin fin de pista"; return false; }
        std::stable_sort (tr.notes.begin(), tr.notes.end(), [] (const SmfNote& a, const SmfNote& b) { return a.tick != b.tick ? a.tick < b.tick : a.key < b.key; });
        out.tracks.push_back (std::move (tr));
        p = end;
    }
    return true;
}
} // namespace cs
