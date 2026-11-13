// Perfiles musicales de las alternativas A-D de Solo AI (misión 0.7, prioridad 1). Código propio de CAUCESOUND en el
// adaptador: el núcleo de Solo AI (submódulo fijado) no se modifica.
//
// Diagnóstico que motivó esto (docs/SOLO_AI_ALTERNATIVES.md): el núcleo devuelve cuatro secuencias MIDI distintas,
// pero todas salen del mismo motivo, empiezan con las mismas notas, viven en el mismo registro y tienen densidad y
// duraciones parecidas. Con la canción sonando, la melodía queda ~15 dB por debajo de la mezcla y las cuatro se
// confunden. Cada perfil aplica aquí un algoritmo real y determinista sobre la propuesta del núcleo, y un último paso
// armónico común garantiza que todas las notas sigan en la escala y que los tiempos fuertes caigan en notas del acorde.
#include "cs/SoloBridge.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace cs::solo
{
namespace
{
constexpr Tick kBeat = kPpq, kBar = 4 * kPpq, k16 = kPpq / 4, k8 = kPpq / 2;

std::vector<int> scaleOf (int root, bool minor)
{
    static const int maj[7] = { 0, 2, 4, 5, 7, 9, 11 }, mnr[7] = { 0, 2, 3, 5, 7, 8, 10 };
    std::vector<int> s;
    for (int i = 0; i < 7; ++i) s.push_back ((root + (minor ? mnr : maj)[i]) % 12);
    return s;
}

std::vector<int> chordTones (const ChordSpec& c)
{
    const int third = c.quality == 1 || c.quality == 2 ? 3 : 4;
    const int fifth = c.quality == 2 ? 6 : c.quality == 3 ? 8 : 7;
    return { c.root % 12, (c.root + third) % 12, (c.root + fifth) % 12 };
}

bool has (const std::vector<int>& v, int pc) { return std::find (v.begin(), v.end(), ((pc % 12) + 12) % 12) != v.end(); }

// Nota más cercana a 'key' cuya clase de altura está en 'pcs' (empate: hacia abajo, más estable).
int nearestIn (int key, const std::vector<int>& pcs)
{
    for (int d = 0; d <= 6; ++d)
    {
        if (has (pcs, key - d)) return key - d;
        if (has (pcs, key + d)) return key + d;
    }
    return key;
}

// Paso diatónico: desplaza 'steps' grados de la escala (positivo = arriba).
int scaleStep (int key, int steps, const std::vector<int>& sc)
{
    int k = nearestIn (key, sc);
    const int dir = steps > 0 ? 1 : -1;
    for (int s = 0; s != steps; s += dir)
    {
        k += dir;
        while (! has (sc, k)) k += dir;
    }
    return k;
}

const ChordSpec& chordAt (const MelodyRequest& r, Tick t) { return r.chords[(size_t) std::clamp<Tick> (t / kBar, 0, (Tick) r.chords.size() - 1)]; }

bool strongBeat (Tick t) { const Tick inBar = t % kBar; return inBar % kBeat == 0 && (inBar / kBeat) % 2 == 0; }

void sortAndClean (std::vector<MidiNote>& v, Tick end)
{
    std::sort (v.begin(), v.end(), [] (const MidiNote& a, const MidiNote& b) { return a.start != b.start ? a.start < b.start : a.key < b.key; });
    std::vector<MidiNote> out;
    for (auto n : v)
    {
        if (n.start < 0 || n.start >= end) continue;
        if (! out.empty() && out.back().start == n.start) continue; // monofónica: una nota por ataque
        out.push_back (n);
    }
    for (size_t i = 0; i < out.size(); ++i)
    {
        const Tick next = i + 1 < out.size() ? out[i + 1].start : end;
        out[i].length = std::clamp<Tick> (out[i].length, 1, std::max<Tick> (1, next - out[i].start));
        out[i].key = std::clamp (out[i].key, 36, 96);
        out[i].velocity = std::clamp (out[i].velocity, 1, 127);
    }
    v = out;
}

// Paso armónico común: notas fuera de la escala → la más cercana de la escala; notas largas en tiempo fuerte → nota del
// acorde más cercana (como hace Solo AI con sus propias propuestas).
void harmonize (std::vector<MidiNote>& v, const MelodyRequest& r)
{
    const auto sc = scaleOf (r.keyRoot, r.minor);
    for (auto& n : v)
    {
        const auto ct = chordTones (chordAt (r, n.start));
        if (strongBeat (n.start) && n.length >= kBeat / 2) { if (! has (ct, n.key)) n.key = nearestIn (n.key, ct); }
        else if (! has (sc, n.key) && ! has (ct, n.key)) n.key = nearestIn (n.key, sc);
    }
}

// Registro: transporta por octavas (de -24 a +24) para dejar la altura media lo más cerca posible de 'center'; si
// alguna nota se sale de [lo, hi] se mueve por octavas hasta entrar (sigue siendo la misma clase de altura).
void placeRegister (std::vector<MidiNote>& v, double center, int lo, int hi)
{
    if (v.empty()) return;
    double mean = 0;
    for (const auto& n : v) mean += n.key;
    mean /= (double) v.size();
    int best = 0;
    for (int s = -24; s <= 24; s += 12) if (std::fabs (mean + s - center) < std::fabs (mean + best - center)) best = s;
    for (auto& n : v)
    {
        n.key += best;
        while (n.key < lo) n.key += 12;
        while (n.key > hi) n.key -= 12;
    }
}

// --- A · Sencilla y pegadiza: motivo de 2 compases simplificado y repetido, pocas notas, registro medio ----------
std::vector<MidiNote> simple (const std::vector<MidiNote>& in, const MelodyRequest& r, Tick end)
{
    const int bars = (int) (end / kBar);
    // Celda: notas de los dos primeros compases que caen en corchea; las de adorno se absorben en la anterior.
    std::vector<MidiNote> cell;
    for (const auto& n : in)
    {
        if (n.start >= std::min<Tick> (2 * kBar, end)) break;
        if (n.start % k8 != 0 && ! cell.empty()) continue;
        cell.push_back (n);
    }
    // Máximo 6 notas en la celda (un gancho, no una frase): se quitan las más cortas que no estén en tiempo.
    while (cell.size() > 6)
    {
        size_t worst = 1;
        for (size_t i = 1; i < cell.size(); ++i)
            if ((cell[i].start % kBeat != 0 && cell[worst].start % kBeat == 0) || (((cell[i].start % kBeat != 0) == (cell[worst].start % kBeat != 0)) && cell[i].length < cell[worst].length)) worst = i;
        cell.erase (cell.begin() + (long) worst);
    }
    std::vector<MidiNote> out;
    if (cell.empty()) return in;
    const int cellBars = bars >= 2 ? 2 : 1;
    // Repetición reconocible en todos los bloques menos el último, que cierra con la frase final del núcleo simplificada.
    const int lastBlockFrom = bars >= 4 ? bars - cellBars : bars;
    for (int b = 0; b < lastBlockFrom; b += cellBars)
        for (auto n : cell)
        {
            if (n.start >= (Tick) cellBars * kBar) continue;
            n.start += (Tick) b * kBar;
            n.length = std::max<Tick> (n.length, k8);
            out.push_back (n);
        }
    for (const auto& n : in)
        if (n.start >= (Tick) lastBlockFrom * kBar && n.start % k8 == 0) out.push_back (n);
    sortAndClean (out, end);
    // Notas sostenidas hasta la siguiente (sin huecos de semicorchea): suena más cantable.
    // Notas de una negra como máximo: el gancho se marca, no se arrastra (eso es la B).
    for (auto& n : out) { n.length = std::min<Tick> (n.length, kBeat); n.velocity = strongBeat (n.start) ? 100 : 86; }
    placeRegister (out, 67, 57, 79);
    return out;
}

// --- B · Emotiva: frases largas (legato), arco hacia un clímax a los 2/3, resolución larga en la tónica ----------
std::vector<MidiNote> emotional (const std::vector<MidiNote>& in, const MelodyRequest& r, Tick end)
{
    const auto sc = scaleOf (r.keyRoot, r.minor);
    std::vector<MidiNote> out;
    // Menos ataques: solo los que caen en negra o los largos.
    for (const auto& n : in) if (n.start % kBeat == 0 || n.length >= kBeat) out.push_back (n);
    if (out.size() < 4) out = in;
    sortAndClean (out, end);
    const int bars = (int) (end / kBar);
    // Arco: la segunda mitad sube una tercera diatónica y el compás del clímax (2/3) una quinta.
    const Tick climax = (Tick) std::max (1, (bars * 2) / 3) * kBar;
    for (auto& n : out)
    {
        if (n.start >= end / 2 && n.start < climax) n.key = scaleStep (n.key, 2, sc);
        else if (n.start >= climax && n.start < climax + kBar) n.key = scaleStep (n.key, 4, sc);
    }
    // Legato con respiración solo al final de cada frase de 4 compases.
    for (size_t i = 0; i + 1 < out.size(); ++i)
    {
        const bool phraseEnd = out[i + 1].start / (4 * kBar) != out[i].start / (4 * kBar);
        out[i].length = std::max<Tick> (1, out[i + 1].start - out[i].start - (phraseEnd ? kBeat : 0));
    }
    // Resolución: el último compás es una nota larga de la tónica (o del acorde final) en el tiempo 1.
    while (! out.empty() && out.back().start >= end - kBar) out.pop_back();
    MidiNote last;
    const auto ct = chordTones (chordAt (r, end - 1));
    const int ref = out.empty() ? 64 : out.back().key;
    last.key = has (ct, r.keyRoot) ? nearestIn (ref, { r.keyRoot % 12 }) : nearestIn (ref, ct);
    last.start = end - kBar;
    last.length = kBar - k8;
    last.velocity = 84;
    out.push_back (last);
    // Dinámica: crece hasta el clímax y baja al resolver.
    for (auto& n : out)
    {
        const double x = (double) n.start / (double) std::max<Tick> (1, climax);
        n.velocity = (int) std::lround (n.start <= climax ? 70 + 40 * x : 110 - 30 * std::min (1.0, (double) (n.start - climax) / (double) std::max<Tick> (1, end - climax)));
    }
    placeRegister (out, 71, 60, 84);
    return out;
}

// --- C · Rítmica: frases cortas, anticipaciones (síncopas), notas cortas repetidas, registro grave --------------
std::vector<MidiNote> rhythmic (const std::vector<MidiNote>& in, const MelodyRequest& r, Tick end)
{
    std::vector<MidiNote> out;
    int k = 0;
    for (auto n : in)
    {
        // Silencio en el último tiempo de cada compás: frases cortas que dejan hueco a la batería.
        if (n.start % kBar >= 3 * kBeat) continue;
        // Notas largas → figura 3+3+2 semicorcheas (el ritmo típico de trap/reguetón), mismas alturas.
        if (n.length >= kBeat)
        {
            const Tick cuts[3] = { 0, 3 * k16, 6 * k16 };
            for (int i = 0; i < 3; ++i)
            {
                MidiNote m = n;
                m.start = n.start + cuts[i];
                m.length = (i < 2 ? 3 : 2) * k16 * 2 / 3;
                if (m.start % kBar >= 3 * kBeat) break;
                out.push_back (m);
            }
            continue;
        }
        // Anticipación: una de cada dos notas en tiempo (salvo el 1 del compás) entra una semicorchea antes.
        if (n.start % kBeat == 0 && n.start % kBar != 0 && (k++ % 2 == 0)) n.start -= k16;
        n.length = std::min<Tick> (n.length, k8) * 2 / 3; // staccato
        out.push_back (n);
    }
    sortAndClean (out, end);
    for (auto& n : out) n.velocity = (n.start % kBeat == 0) ? 96 : 112; // acento en los contratiempos
    placeRegister (out, 55, 45, 70);
    return out;
}

// --- D · Experimental: contorno invertido en la segunda mitad, saltos de octava controlados, ritmo de tresillo ---
std::vector<MidiNote> experimental (const std::vector<MidiNote>& in, const MelodyRequest& r, Tick end)
{
    const auto sc = scaleOf (r.keyRoot, r.minor);
    // Como mucho una nota por corchea: los solos rápidos del núcleo se aclaran antes de añadir saltos.
    std::vector<MidiNote> out;
    for (const auto& n : in) if (out.empty() || n.start - out.back().start >= k8) out.push_back (n);
    if (out.empty()) return out;
    const int axis = out.front().key;
    const int bars = (int) (end / kBar);
    for (size_t i = 0; i < out.size(); ++i)
    {
        auto& n = out[i];
        const int bar = (int) (n.start / kBar);
        // Segunda mitad: inversión del contorno alrededor de la primera nota (en grados de la escala).
        if (bar >= bars / 2) n.key = nearestIn (2 * axis - n.key, sc);
        // Saltos controlados: una de cada tres notas que no está en tiempo fuerte sube una octava.
        if (i % 3 == 2 && ! strongBeat (n.start)) n.key += 12;
        // Compases impares (2.º, 4.º...): rejilla de tresillos de corchea en vez de semicorcheas.
        if (bar % 2 == 1)
        {
            const Tick t3 = kBeat / 3, inBar = n.start - (Tick) bar * kBar;
            n.start = (Tick) bar * kBar + (Tick) std::llround ((double) inBar / (double) t3) * t3;
        }
    }
    sortAndClean (out, end);
    // Sin saltos mayores de una décima: lo «experimental» sigue siendo cantable/tocable.
    for (size_t i = 1; i < out.size(); ++i)
    {
        while (out[i].key - out[i - 1].key > 15) out[i].key -= 12;
        while (out[i - 1].key - out[i].key > 15) out[i].key += 12;
    }
    for (size_t i = 0; i < out.size(); ++i) out[i].velocity = 80 + (int) ((i * 37) % 41); // dinámica irregular
    placeRegister (out, 72, 57, 88);
    return out;
}
} // namespace

const char* profileName (int p)
{
    static const char* n[4] = { "A · Sencilla y pegadiza", "B · Emotiva", "C · Rítmica", "D · Experimental" };
    return n[std::clamp (p, 0, 3)];
}

const char* profileDescription (int p)
{
    static const char* d[4] = {
        "Pocas notas, un gancho de 2 compases que se repite y registro medio. La más fácil de recordar.",
        "Frases largas y ligadas que suben hacia un clímax y resuelven en una nota larga.",
        "Frases cortas, síncopas y notas repetidas en registro grave; deja hueco a la batería.",
        "Saltos de octava controlados, contorno invertido en la segunda mitad y compases en tresillo.",
    };
    return d[std::clamp (p, 0, 3)];
}

std::vector<MidiNote> applyProfile (const std::vector<MidiNote>& notes, int profile, const MelodyRequest& req)
{
    if (notes.empty() || req.chords.empty()) return notes;
    const Tick end = (Tick) req.chords.size() * kBar;
    std::vector<MidiNote> in = notes;
    sortAndClean (in, end);
    std::vector<MidiNote> out;
    switch (std::clamp (profile, 0, 3))
    {
        case 0: out = simple (in, req, end); break;
        case 1: out = emotional (in, req, end); break;
        case 2: out = rhythmic (in, req, end); break;
        default: out = experimental (in, req, end); break;
    }
    harmonize (out, req);
    sortAndClean (out, end);
    return out.empty() ? in : out;
}

ProfileMetrics measureProfile (const std::vector<MidiNote>& v, const MelodyRequest& req)
{
    ProfileMetrics m;
    if (v.empty() || req.chords.empty()) return m;
    const auto sc = scaleOf (req.keyRoot, req.minor);
    const double bars = (double) req.chords.size();
    m.notes = (int) v.size();
    m.notesPerBar = v.size() / bars;
    int lo = 127, hi = 0, scaleOk = 0, strong = 0, strongOk = 0, sync = 0;
    double len = 0, leap = 0, pitch = 0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const auto& n = v[i];
        lo = std::min (lo, n.key); hi = std::max (hi, n.key);
        len += (double) n.length / kBeat;
        pitch += n.key;
        if (i > 0) leap += std::abs (n.key - v[i - 1].key);
        if (n.start % k8 != 0 || (n.start % kBeat != 0 && n.length > k8)) ++sync; // fuera de la corchea, o anticipación sostenida
        const auto ct = chordTones (chordAt (req, n.start));
        if (has (sc, n.key) || has (ct, n.key)) ++scaleOk;
        if (strongBeat (n.start) && n.length >= kBeat / 2) { ++strong; if (has (ct, n.key)) ++strongOk; }
    }
    m.meanLengthBeats = len / v.size();
    m.meanLeap = v.size() > 1 ? leap / (v.size() - 1) : 0;
    m.meanPitch = pitch / v.size();
    m.lowKey = lo; m.highKey = hi;
    m.syncopation = (double) sync / v.size();
    m.inScale = (double) scaleOk / v.size();
    m.strongChordTones = strong > 0 ? (double) strongOk / strong : 1.0;
    return m;
}

double onsetOverlap (const std::vector<MidiNote>& a, const std::vector<MidiNote>& b)
{
    // Jaccard de pares (ataque en semicorcheas, altura): 1 = mismas notas en los mismos sitios.
    std::map<std::pair<long, int>, int> s;
    for (const auto& n : a) s[{ (long) (n.start / k16), n.key }] |= 1;
    for (const auto& n : b) s[{ (long) (n.start / k16), n.key }] |= 2;
    int both = 0;
    for (const auto& kv : s) both += kv.second == 3;
    return s.empty() ? 1.0 : (double) both / (double) s.size();
}
} // namespace cs::solo
