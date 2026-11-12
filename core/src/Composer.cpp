// Compositor local (ver Composer.h). Material 100 % generado por estas reglas: patrones rítmicos genéricos de cada
// género (dembow, half-time de trap, boom bap, four-on-the-floor) y frases construidas nota a nota sobre la escala.
// No contiene ni cita obras de terceros.
#include "cs/Composer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace cs
{
using nlohmann::json;

// --- Plan ---------------------------------------------------------------------------------------------------------
std::string MusicPlan::toJson() const
{
    json j;
    j["genre"] = genre;
    j["keyRoot"] = keyRoot;
    j["minor"] = minor;
    j["progression"] = progression;
    j["energy"] = energy;
    j["seeds"] = seeds;
    j["roles"] = roles;
    j["patternIds"] = patternIds;
    j["locked"] = locked;
    json s = json::array();
    for (const auto& x : sections) s.push_back ({ { "name", x.name }, { "startBar", x.startBar }, { "bars", x.bars }, { "energy", x.energy } });
    j["sections"] = s;
    return j.dump();
}

bool MusicPlan::fromJson (const std::string& text, MusicPlan& out)
{
    const json j = json::parse (text, nullptr, false);
    if (j.is_discarded() || ! j.is_object()) return false;
    try
    {
        MusicPlan p;
        p.genre = j.value ("genre", std::string());
        p.keyRoot = std::clamp (j.value ("keyRoot", 9), 0, 11);
        p.minor = j.value ("minor", true);
        p.progression = j.value ("progression", std::vector<int> { 0 });
        for (int& d : p.progression) d = ((d % 7) + 7) % 7;
        if (p.progression.empty()) p.progression = { 0 };
        p.energy = std::clamp (j.value ("energy", 0.0f), -1.0f, 1.0f);
        if (j.contains ("seeds")) p.seeds = j.at ("seeds").get<std::map<std::string, uint32_t>>();
        if (j.contains ("roles")) p.roles = j.at ("roles").get<std::map<std::string, std::string>>();
        if (j.contains ("patternIds")) p.patternIds = j.at ("patternIds").get<std::map<std::string, std::string>>();
        if (j.contains ("locked")) p.locked = j.at ("locked").get<std::vector<std::string>>();
        if (j.contains ("sections"))
            for (const auto& s : j.at ("sections"))
                p.sections.push_back ({ s.value ("name", std::string ("A")), std::max (0, s.value ("startBar", 0)), std::clamp (s.value ("bars", 4), 1, 64),
                                        std::clamp (s.value ("energy", 0.0f), -1.0f, 1.0f) });
        out = std::move (p);
        return true;
    }
    catch (const std::exception&) { return false; }
}

namespace composer
{
namespace
{
// Generador determinista (splitmix64): mismo plan + misma semilla = mismas notas en cualquier plataforma.
struct Rand
{
    uint64_t s;
    explicit Rand (uint64_t seed) : s (seed * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull) {}
    uint64_t next() { uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
    double uni() { return (double) (next() >> 11) * (1.0 / 9007199254740992.0); }
    bool chance (double p) { return uni() < p; }
    int range (int lo, int hi) { return lo + (int) (next() % (uint64_t) (hi - lo + 1)); } // [lo, hi]
};

uint32_t hashStr (const std::string& s)
{
    uint32_t h = 2166136261u;
    for (char c : s) { h ^= (uint8_t) c; h *= 16777619u; }
    return h;
}

int clampVel (double v) { return std::clamp ((int) std::lround (v), 1, 127); }

// Patrón vacío de 16 pasos.
DrumPattern emptyPattern (const std::string& name, float swing)
{
    DrumPattern p;
    p.name = name;
    p.steps = 16;
    p.swing = swing;
    p.rolls.assign ((size_t) kDrumLanes, std::vector<int> (16, 0));
    return p;
}

struct PatternBuilder
{
    DrumPattern& p;
    Rand& r;
    int human; // amplitud de la variación de intensidad
    void hit (int lane, int step, double vel) { if (step >= 0 && step < p.steps) p.lanes[(size_t) lane][(size_t) step] = clampVel (vel + r.range (-human, human)); }
    void roll (int lane, int step, int n) { if (step >= 0 && step < p.steps) p.rolls[(size_t) lane][(size_t) step] = n; }
    void clear (int lane, int from = 0, int to = 16) { for (int s = from; s < to && s < p.steps; ++s) { p.lanes[(size_t) lane][(size_t) s] = 0; p.rolls[(size_t) lane][(size_t) s] = 0; } }
};

enum Lane { kKick = 0, kSnare = 1, kClap = 2, kHat = 3, kOpen = 4, kP1 = 5, kP2 = 6, kP3 = 7 };
} // namespace

std::vector<int> scale (const MusicPlan& p)
{
    static const int minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };
    static const int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
    std::vector<int> v;
    for (int i = 0; i < 7; ++i) v.push_back ((p.keyRoot + (p.minor ? minorSteps[i] : majorSteps[i])) % 12);
    return v;
}

namespace
{
// Clases de altura del acorde del compás (grado de la escala apilado en terceras). En menor, el V se hace mayor
// (sensible de la escala menor armónica) cuando el género lo pide (trap): es lo que da la tensión «oscura».
std::vector<int> chordPcs (const MusicPlan& p, int bar, bool sevenths)
{
    const auto sc = scale (p);
    const int deg = p.progression[(size_t) (((bar % (int) p.progression.size()) + (int) p.progression.size()) % (int) p.progression.size())];
    std::vector<int> pcs;
    for (int k = 0; k < (sevenths ? 4 : 3); ++k) pcs.push_back (sc[(size_t) ((deg + 2 * k) % 7)]);
    if (p.minor && deg == 4 && p.genre == "trap") pcs[1] = (pcs[0] + 4) % 12; // V mayor
    return pcs;
}

int nearestPc (int pc, int near) // nota MIDI con clase de altura 'pc' más cercana a 'near'
{
    int best = near, bd = 99;
    for (int k = near - 7; k <= near + 7; ++k) if (((k % 12) + 12) % 12 == pc && std::abs (k - near) < bd) { bd = std::abs (k - near); best = k; }
    return best;
}

// Inversión con la menor distancia total respecto al acorde anterior (conducción de voces), dentro de [lo, hi].
std::vector<int> voiceLead (const std::vector<int>& pcs, const std::vector<int>& prev, int center, int lo, int hi)
{
    std::vector<int> best;
    double bestCost = 1e9;
    const int n = (int) pcs.size();
    for (int inv = 0; inv < n; ++inv)
        for (int base = lo; base <= hi; ++base)
        {
            if (((base % 12) + 12) % 12 != pcs[(size_t) inv]) continue;
            std::vector<int> v { base };
            for (int k = 1; k < n; ++k)
            {
                int x = v.back() + 1;
                while (((x % 12) + 12) % 12 != pcs[(size_t) ((inv + k) % n)]) ++x;
                v.push_back (x);
            }
            if (v.back() > hi + 3) continue;
            double cost = 0;
            if (prev.size() == v.size()) for (size_t i = 0; i < v.size(); ++i) cost += std::abs (v[i] - prev[i]);
            else for (int x : v) cost += 0.6 * std::abs (x - center);
            if (cost < bestCost) { bestCost = cost; best = v; }
        }
    if (best.empty()) for (int pc : pcs) best.push_back (nearestPc (pc, center));
    return best;
}

double sectionEnergy (const MusicPlan& p, const SongSection& s) { return std::clamp ((double) p.energy + s.energy, -1.0, 1.0); }
} // namespace

std::vector<int> chordAt (const MusicPlan& p, int bar, bool sevenths)
{
    std::vector<int> out;
    for (int pc : chordPcs (p, bar, sevenths)) out.push_back (nearestPc (pc, 64));
    std::sort (out.begin(), out.end());
    return out;
}

// --- Batería ------------------------------------------------------------------------------------------------------
std::map<std::string, DrumPattern> drumPatterns (const MusicPlan& p, uint32_t seed)
{
    std::map<std::string, DrumPattern> out;
    const double e = p.energy;
    const std::string& g = p.genre;
    const float swing = g == "hiphop" ? 0.28f : 0.0f;
    Rand r (seed ^ 0xD1u);

    auto make = [&] (const std::string& key, const std::string& name, auto fn)
    {
        DrumPattern d = emptyPattern (name, swing);
        PatternBuilder b { d, r, 5 };
        fn (b);
        out[key] = d;
    };

    if (g == "reggaeton")
    {
        // Dembow: bombo en cada negra; caja en el «3-3-2» (pasos 3, 6, 11, 14); güira continua; conga y timbal.
        const bool kick10 = r.chance (0.4), conga7 = r.chance (0.5), timbal15 = r.chance (0.6);
        auto main = [&] (PatternBuilder& b)
        {
            for (int s : { 0, 4, 8, 12 }) b.hit (kKick, s, s % 8 == 0 ? 118 : 104);
            for (int s : { 3, 6, 11, 14 }) b.hit (kSnare, s, s == 6 || s == 14 ? 112 : 100);
            for (int s = 0; s < 16; s += 2) b.hit (kHat, s, s % 4 == 0 ? 74 : 56);
            for (int s = 0; s < 16; ++s) if (e > -0.4 || s % 2 == 0) b.hit (kP3, s, s % 4 == 0 ? 70 : s % 2 == 0 ? 52 : 38);
            b.hit (kP2, 2, 78); b.hit (kP2, 10, 74);
            if (conga7) b.hit (kP2, 7, 66);
            if (timbal15) b.hit (kP1, 15, 82);
        };
        make ("main", "A", main);
        make ("var", "B", [&] (PatternBuilder& b) {
            main (b);
            b.hit (kClap, 6, 96); b.hit (kClap, 14, 100);
            b.hit (kOpen, 14, 70); b.clear (kHat, 14, 15);
            if (kick10 || e > 0.3) b.hit (kKick, 10, 92);
            b.hit (kP1, 7, 76);
        });
        out["varCrash"] = out["var"]; out["varCrash"].name = "B";
        make ("fill", "Remate", [&] (PatternBuilder& b) {
            main (b);
            b.clear (kSnare, 8); b.clear (kP2, 8); b.clear (kP1, 8);
            b.hit (kP1, 8, 92); b.hit (kP2, 9, 84); b.hit (kP1, 10, 96); b.hit (kP2, 11, 88);
            for (int s : { 12, 13, 14, 15 }) b.hit (kSnare, s, 78 + (s - 12) * 12);
            b.roll (kSnare, 14, 2); b.roll (kSnare, 15, 3);
        });
        make ("intro", "Intro", [&] (PatternBuilder& b) {
            for (int s = 0; s < 16; s += 2) b.hit (kHat, s, 58);
            for (int s = 0; s < 16; s += 2) b.hit (kP3, s, 48);
            b.hit (kSnare, 6, 62); b.hit (kSnare, 14, 66); b.hit (kP2, 10, 60);
        });
        make ("break", "Break", [&] (PatternBuilder& b) {
            for (int s : { 3, 6, 11, 14 }) b.hit (kSnare, s, 84);
            for (int s = 0; s < 16; ++s) b.hit (kP3, s, s % 4 == 0 ? 62 : 40);
            b.hit (kP1, 15, 90); b.roll (kP1, 15, 3);
        });
    }
    else if (g == "trap")
    {
        // Half-time: caja y palmas en el tiempo 3; bombo sincopado; charles en corcheas con redobles (2, 3, 4).
        std::vector<int> kicks { 0 };
        for (int s : { 3, 7, 10, 11, 14 }) if (r.chance (s == 10 ? 0.7 : 0.35)) kicks.push_back (s);
        std::vector<std::pair<int, int>> rolls;
        for (int s : { 6, 7, 11, 14, 15 }) if (r.chance (0.35 + 0.15 * e)) rolls.push_back ({ s, r.range (2, 4) });
        if (rolls.empty()) rolls.push_back ({ 14, 3 });
        const bool openHat = r.chance (0.5);
        auto main = [&] (PatternBuilder& b)
        {
            for (int s : kicks) b.hit (kKick, s, s == 0 ? 120 : 100);
            b.hit (kSnare, 8, 118); b.hit (kClap, 8, 104);
            for (int s = 0; s < 16; s += 2) b.hit (kHat, s, s % 4 == 0 ? 86 : 64);
            if (e > 0.2) for (int s = 1; s < 16; s += 4) b.hit (kHat, s, 46);
            for (auto [s, n] : rolls) { b.hit (kHat, s, 72); b.roll (kHat, s, n); }
            if (openHat) { b.hit (kOpen, 13, 66); b.clear (kHat, 13, 14); }
        };
        make ("main", "A", main);
        make ("var", "B", [&] (PatternBuilder& b) {
            main (b);
            b.hit (kP2, 12, 70);
            b.hit (kP1, 15, 70);
            for (int s : { 3, 9 }) { b.hit (kHat, s, 70); b.roll (kHat, s, r.chance (0.5) ? 3 : 2); }
            if (r.chance (0.5)) b.hit (kKick, 6, 96);
        });
        make ("varCrash", "B", [&] (PatternBuilder& b) {
            b.p = out["var"]; b.hit (kP3, 0, 96);
        });
        make ("fill", "Remate", [&] (PatternBuilder& b) {
            main (b);
            for (int s : { 12, 13, 14, 15 }) { b.hit (kSnare, s, 70 + (s - 12) * 15); b.roll (kSnare, s, s < 14 ? 2 : 4); }
            b.clear (kHat, 12);
        });
        make ("intro", "Intro", [&] (PatternBuilder& b) {
            for (int s = 0; s < 16; s += 2) b.hit (kHat, s, 60);
            b.hit (kHat, 14, 64); b.roll (kHat, 14, 3);
            b.hit (kP2, 8, 72);
        });
        make ("break", "Break", [&] (PatternBuilder& b) {
            b.hit (kClap, 8, 96);
            for (int s = 0; s < 16; s += 4) b.hit (kHat, s, 60);
            b.hit (kHat, 12, 70); b.roll (kHat, 12, 4); b.hit (kHat, 14, 76); b.roll (kHat, 14, 4);
        });
    }
    else if (g == "hiphop")
    {
        // Boom bap: caja en 2 y 4 con notas fantasma; bombo con anticipaciones; charles con swing e intensidad viva.
        std::vector<int> kicks { 0, 10 };
        if (r.chance (0.6)) kicks.push_back (7);
        if (r.chance (0.4)) kicks.push_back (r.chance (0.5) ? 3 : 13);
        std::vector<int> ghosts;
        for (int s : { 7, 9, 15 }) if (r.chance (0.45)) ghosts.push_back (s);
        const bool openHat = r.chance (0.5);
        auto main = [&] (PatternBuilder& b)
        {
            for (int s : kicks) b.hit (kKick, s, s == 0 ? 118 : 100);
            b.hit (kSnare, 4, 116); b.hit (kSnare, 12, 120);
            for (int s : ghosts) b.hit (kSnare, s, 42);
            for (int s = 0; s < 16; s += 2) b.hit (kHat, s, s % 4 == 0 ? 88 : 54);
            b.human = 9;
            if (openHat) { b.hit (kOpen, 14, 62); b.clear (kHat, 14, 15); }
        };
        make ("main", "A", main);
        make ("var", "B", [&] (PatternBuilder& b) {
            main (b);
            for (int s = 1; s < 16; s += 2) b.hit (kP2, s, 46);
            b.hit (kP1, 3, 68); b.hit (kP1, 11, 60);
            if (r.chance (0.5)) b.hit (kKick, 13, 92);
        });
        make ("varCrash", "B", [&] (PatternBuilder& b) { b.p = out["var"]; b.hit (kP3, 0, 84); });
        make ("fill", "Remate", [&] (PatternBuilder& b) {
            main (b);
            b.hit (kKick, 11, 96);
            for (int s : { 13, 14, 15 }) b.hit (kSnare, s, 66 + (s - 13) * 18);
            b.roll (kSnare, 15, 2);
        });
        make ("intro", "Intro", [&] (PatternBuilder& b) {
            for (int s = 0; s < 16; s += 2) b.hit (kHat, s, s % 4 == 0 ? 70 : 46);
            b.hit (kP1, 4, 64); b.hit (kP1, 12, 70);
            for (int s = 1; s < 16; s += 2) b.hit (kP2, s, 36);
        });
        make ("break", "Break", [&] (PatternBuilder& b) {
            b.hit (kKick, 0, 108); b.hit (kSnare, 12, 104);
            for (int s = 0; s < 16; s += 2) b.hit (kHat, s, 52);
        });
    }
    else // pop y electrónica
    {
        const bool electro = g == "electronic";
        std::vector<int> kicks = electro ? std::vector<int> { 0, 4, 8, 12 } : std::vector<int> { 0, 8 };
        if (! electro && r.chance (0.6)) kicks.push_back (r.chance (0.5) ? 6 : 10);
        const bool perc = r.chance (0.6);
        auto main = [&] (PatternBuilder& b)
        {
            for (int s : kicks) b.hit (kKick, s, s == 0 ? 118 : 108);
            b.hit (kSnare, 4, 112); b.hit (kSnare, 12, 116);
            if (electro)
            {
                for (int s : { 2, 6, 10, 14 }) b.hit (kOpen, s, 74);
                if (e > -0.3) for (int s = 0; s < 16; ++s) if (s % 4 != 2) b.hit (kHat, s, s % 2 ? 40 : 58);
                if (perc) { b.hit (kP1, 3, 64); b.hit (kP1, 11, 58); }
            }
            else
            {
                for (int s = 0; s < 16; s += 2) b.hit (kHat, s, s % 4 == 0 ? 80 : 60);
                b.hit (kP2, 4, 70); b.hit (kP2, 12, 74);
            }
        };
        make ("main", "A", main);
        make ("var", "B", [&] (PatternBuilder& b) {
            main (b);
            b.hit (kClap, 4, 96); b.hit (kClap, 12, 100);
            for (int s = 0; s < 16; ++s) b.hit (kP2, s, s % 4 == 0 ? 64 : 40);
            if (! electro) { b.hit (kKick, 10, 96); b.hit (kOpen, 14, 68); b.clear (kHat, 14, 15); }
        });
        make ("varCrash", "B", [&] (PatternBuilder& b) { b.p = out["var"]; b.hit (kP3, 0, 92); });
        make ("fill", "Remate", [&] (PatternBuilder& b) {
            main (b);
            for (int s = 8; s < 16; ++s) b.hit (kSnare, s, 60 + (s - 8) * 8);
            b.roll (kSnare, 14, 2); b.roll (kSnare, 15, 2);
        });
        make ("intro", "Intro", [&] (PatternBuilder& b) {
            if (electro) for (int s : { 0, 4, 8, 12 }) b.hit (kKick, s, 96);
            for (int s : { 2, 6, 10, 14 }) b.hit (electro ? kOpen : kHat, s, 60);
            if (! electro) for (int s = 0; s < 16; s += 4) b.hit (kP2, s, 56);
        });
        make ("break", "Break", [&] (PatternBuilder& b) {
            // Subida: caja en corcheas y luego semicorcheas, cada vez más fuerte (la segunda barra repite: se oye como 2).
            for (int s = 0; s < 8; s += 2) b.hit (kSnare, s, 56 + s * 3);
            for (int s = 8; s < 16; ++s) b.hit (kSnare, s, 72 + (s - 8) * 6);
            b.roll (kSnare, 15, 4);
            for (int s : { 4, 12 }) b.hit (kClap, s, 84);
        });
    }
    return out;
}

std::vector<DrumSlot> drumLayout (const MusicPlan& p, const SongSection& s)
{
    (void) p;
    std::vector<DrumSlot> v;
    auto add = [&] (const char* key, int bars, const std::string& name) { if (bars > 0) v.push_back ({ key, bars, name }); };
    const int n = s.bars;
    if (s.name == "Intro") add ("intro", n, s.name);
    else if (s.name == "Break") add ("break", n, s.name);
    else if (s.name == "A") { add ("main", n - 1, s.name); add ("fill", n > 1 ? 1 : 0, "Remate"); if (n == 1) add ("main", 1, s.name); }
    else if (s.name == "B" || s.name == "Final")
    {
        if (n == 1) add ("varCrash", 1, s.name);
        else { add ("varCrash", 1, s.name); add ("var", n - 2, s.name); add ("fill", 1, "Remate"); }
    }
    else add ("main", n, s.name);
    return v;
}

// --- Partes melódicas ---------------------------------------------------------------------------------------------
namespace
{
constexpr Tick S = kStepTicks; // una semicorchea

void note (std::vector<MidiNote>& v, int key, double vel, Tick start, Tick len)
{
    if (len <= 0 || key < 0 || key > 127) return;
    MidiNote n;
    n.key = key; n.velocity = clampVel (vel); n.start = start; n.length = len;
    v.push_back (n);
}

std::vector<MidiNote> bassPart (const MusicPlan& p, const SongSection& s, Rand& r, int bpb)
{
    std::vector<MidiNote> v;
    const double e = sectionEnergy (p, s);
    const Tick bar = (Tick) bpb * kPpq;
    const std::string& g = p.genre;
    if (s.name == "Intro" && g != "hiphop") return v; // el bajo entra con la sección principal
    for (int b = 0; b < s.bars; ++b)
    {
        const int absBar = s.startBar + b;
        const auto ch = chordPcs (p, absBar, false);
        const auto next = chordPcs (p, absBar + 1, false);
        const int root = nearestPc (ch[0], 36), fifth = nearestPc (ch[2], root + 7), nextRoot = nearestPc (next[0], root);
        const Tick t0 = b * bar;
        const bool sparse = s.name == "Break" || e < -0.5;
        if (g == "trap")
        {
            // 808: nota larga en el primer bombo; a veces una segunda nota ligada (octava o quinta) que hace glide.
            if (sparse) { note (v, root, 104, t0, bar - S); continue; }
            const int mid = r.chance (0.5) ? 10 : 7;
            note (v, root, 112, t0, mid * S + S); // solapa: la siguiente entra en legato (glide)
            const int second = r.chance (0.45) ? root + 12 : r.chance (0.5) ? fifth : root;
            note (v, second, 100, t0 + mid * S, (14 - mid) * S + (e > 0.2 ? 0 : S));
            if (e > 0.2 || r.chance (0.35)) note (v, r.chance (0.5) ? nextRoot : root + 12, 92, t0 + 14 * S, 2 * S - S / 4);
        }
        else if (g == "reggaeton")
        {
            // Bajo pegado al dembow: tresillo 3-3-2 en cada mitad del compás y aproximación al acorde siguiente.
            if (sparse) { note (v, root, 100, t0, 3 * S); note (v, root, 92, t0 + 8 * S, 3 * S); continue; }
            note (v, root, 112, t0, 3 * S - S / 4);
            note (v, root, 96, t0 + 3 * S, 3 * S - S / 4);
            note (v, r.chance (0.5) ? root + 12 : fifth, 100, t0 + 6 * S, 2 * S - S / 4);
            note (v, root, 108, t0 + 8 * S, 3 * S - S / 4);
            note (v, root, 94, t0 + 11 * S, 3 * S - S / 4);
            const int app = nextRoot != root ? (r.chance (0.5) ? nextRoot : fifth) : (r.chance (0.5) ? root + 12 : fifth);
            note (v, app, 100, t0 + 14 * S, 2 * S - S / 4);
        }
        else if (g == "hiphop")
        {
            // Bajo cálido en los bombos, con nota de paso cromática/diatónica hacia el siguiente acorde.
            if (s.name == "Intro") { note (v, root, 84, t0, bar - 2 * S); continue; }
            note (v, root, 108, t0, 6 * S);
            if (! sparse) note (v, r.chance (0.5) ? root : fifth, 96, t0 + 7 * S, 3 * S);
            note (v, root, 100, t0 + 10 * S, 3 * S);
            if (! sparse && nextRoot != root) note (v, nextRoot + (nextRoot > root ? -1 : 1), 88, t0 + 14 * S, 2 * S - S / 4);
            else if (! sparse && r.chance (0.5)) note (v, root + 12, 86, t0 + 14 * S, S);
        }
        else if (g == "electronic")
        {
            // Bajo «rodante» a contratiempo (deja el golpe del bombo libre).
            if (sparse) { note (v, root, 96, t0, bar - S); continue; }
            for (int st = 0; st < 16; ++st)
            {
                const bool off = st % 4 == 2;
                if (! off && ! (e > 0.3 && st % 4 == 3)) continue;
                note (v, st % 8 == 6 && r.chance (0.3) ? root + 12 : root, off ? 106 : 84, t0 + st * S, S + S / 2);
            }
        }
        else // pop
        {
            if (sparse) { note (v, root, 96, t0, bar / 2); note (v, fifth, 88, t0 + bar / 2, bar / 2 - S); continue; }
            for (int st = 0; st < 16; st += 2)
            {
                int k = root;
                if (st == 6 && e > -0.2) k = root + 12;
                if (st == 14 && nextRoot != root) k = fifth;
                note (v, k, st % 4 == 0 ? 106 : 90, t0 + st * S, 2 * S - S / 3);
            }
        }
    }
    return v;
}

std::vector<MidiNote> chordPart (const MusicPlan& p, const SongSection& s, Rand& r, int bpb, bool pad)
{
    std::vector<MidiNote> v;
    const double e = sectionEnergy (p, s);
    const Tick bar = (Tick) bpb * kPpq;
    const std::string& g = p.genre;
    const bool sevenths = g == "hiphop" || (g == "pop" && ! pad && r.chance (0.3));
    std::vector<int> prev;
    for (int b = 0; b < s.bars; ++b)
    {
        const auto pcs = chordPcs (p, s.startBar + b, sevenths);
        const auto vc = pad ? voiceLead (pcs, prev, 60, 52, 70) : voiceLead (pcs, prev, 64, 57, 74);
        prev = vc;
        const Tick t0 = b * bar;
        if (pad)
        {
            for (int k : vc) note (v, k, s.name == "Break" || s.name == "Intro" ? 78 : 66, t0, bar - S / 2);
            continue;
        }
        auto hitAll = [&] (int step, int len, double vel) { for (int k : vc) note (v, k, vel + r.range (-4, 4), t0 + step * S, len * S - S / 4); };
        if (s.name == "Intro" || s.name == "Break" || e < -0.5) { hitAll (0, 15, 74); continue; }
        if (g == "reggaeton") for (int st : { 0, 3, 6, 10, 14 }) hitAll (st, 2, st == 0 ? 96 : 84);
        else if (g == "hiphop") { hitAll (0, r.chance (0.5) ? 7 : 9, 92); hitAll (10, 5, 80); }
        else if (g == "electronic") for (int st : { 2, 6, 10, 14 }) hitAll (st, 1, 90);
        else if (g == "pop") for (int st = 0; st < 16; st += 2) hitAll (st, 1, st % 4 == 0 ? 92 : 76);
        else hitAll (0, 15, 80);
    }
    return v;
}

// Melodía por motivos: un motivo de 2 compases (ritmo + contorno) y su respuesta, todo sobre la escala, con notas
// del acorde en los tiempos fuertes y cadencia al final. Densidad e intensidad según la energía de la sección.
std::vector<MidiNote> melodyPart (const MusicPlan& p, const SongSection& s, uint32_t seed, int bpb)
{
    std::vector<MidiNote> v;
    const double e = sectionEnergy (p, s);
    const Tick bar = (Tick) bpb * kPpq;
    const std::string& g = p.genre;
    if (s.name == "Intro" && g != "electronic") return v;
    // A y Final comparten motivo (reconocible); B tiene el suyo.
    Rand r (seed ^ (s.name == "B" ? 0xB0B0u : 0xA0A0u));
    const auto sc = scale (p);
    const int center = nearestPc (p.keyRoot, g == "trap" ? 72 : g == "hiphop" ? 69 : 74);
    const int lo = center - 7, hi = center + 12;
    std::vector<int> all; // notas de la escala dentro del registro
    for (int k = lo; k <= hi; ++k) if (std::find (sc.begin(), sc.end(), ((k % 12) + 12) % 12) != sc.end()) all.push_back (k);
    auto idxOf = [&] (int key) { int best = 0; for (int i = 0; i < (int) all.size(); ++i) if (std::abs (all[(size_t) i] - key) < std::abs (all[(size_t) best] - key)) best = i; return best; };

    // Ritmo del motivo: 32 semicorcheas (2 compases).
    const double density = std::clamp ((g == "trap" ? 0.32 : g == "reggaeton" ? 0.42 : g == "hiphop" ? 0.3 : 0.45) + 0.18 * e, 0.12, 0.8);
    const double synco = g == "reggaeton" || g == "trap" ? 0.45 : 0.25;
    std::vector<int> onsets;
    for (int st = 0; st < 32; ++st)
    {
        const bool beat = st % 4 == 0, eighth = st % 2 == 0;
        double prob = beat ? density + 0.15 : eighth ? density : density * synco;
        if (st == 0 && r.chance (0.5)) prob = 1.0;
        if (st >= 28) prob *= 0.5; // respiración al final del motivo
        if (r.chance (prob)) onsets.push_back (st);
    }
    if (onsets.size() < 3) onsets = { 0, 6, 10, 16, 22 };
    // Contorno: pasos en la escala (-2..+2) desde una nota del acorde.
    std::vector<int> moves;
    for (size_t i = 0; i < onsets.size(); ++i)
    {
        const double u = r.uni();
        moves.push_back (u < 0.36 ? 1 : u < 0.72 ? -1 : u < 0.84 ? 2 : u < 0.94 ? -2 : 0);
    }
    const int phrases = std::max (1, s.bars / 2);
    const bool thin = s.name == "Break";
    for (int ph = 0; ph < phrases; ++ph)
    {
        const int barBase = ph * 2;
        if (barBase >= s.bars) break;
        const auto firstChord = chordPcs (p, s.startBar + barBase, false);
        int cur = idxOf (nearestPc (firstChord[(size_t) (r.range (0, 2))], center));
        const bool answer = ph % 2 == 1;
        for (size_t i = 0; i < onsets.size(); ++i)
        {
            if (thin && i % 2 == 1) continue;
            const int st = onsets[i];
            const int barIn = barBase + st / 16;
            if (barIn >= s.bars) break;
            int mv = moves[i];
            if (answer && i + 2 >= onsets.size()) mv = -std::abs (mv); // la respuesta baja hacia la cadencia
            cur = std::clamp (cur + mv, 0, (int) all.size() - 1);
            int key = all[(size_t) cur];
            const auto ch = chordPcs (p, s.startBar + barIn, false);
            if (st % 4 == 0)
            {
                // Tiempo fuerte: la nota del acorde más cercana.
                int best = key, bd = 99;
                for (int pc : ch) { const int k = nearestPc (pc, key); if (std::abs (k - key) < bd && k >= lo && k <= hi) { bd = std::abs (k - key); best = k; } }
                key = best; cur = idxOf (key);
            }
            const bool last = i + 1 == onsets.size();
            if (answer && last) { key = nearestPc (ch[0], key); cur = idxOf (key); } // cadencia en la raíz
            const int nextSt = last ? 32 : onsets[i + 1];
            int lenSteps = std::max (1, nextSt - st);
            const int cap = g == "trap" || g == "reggaeton" ? 3 : g == "hiphop" ? 4 : 6;
            lenSteps = std::min (lenSteps, last ? cap + 2 : cap);
            const double vel = (st % 4 == 0 ? 100 : 86) + 10 * e + r.range (-5, 5) - (thin ? 14 : 0);
            note (v, key, vel, barBase * bar + (Tick) st * S, lenSteps * S - S / 4);
        }
    }
    return v;
}
} // namespace

std::vector<MidiNote> partNotes (const MusicPlan& p, const std::string& part, const SongSection& s, uint32_t seed, int bpb)
{
    Rand r (seed ^ hashStr (part) ^ (hashStr (s.name) * 31u) ^ (uint32_t) s.startBar);
    std::vector<MidiNote> v;
    if (part == "bass") v = bassPart (p, s, r, bpb);
    else if (part == "chords") v = chordPart (p, s, r, bpb, false);
    else if (part == "pad") v = chordPart (p, s, r, bpb, true);
    else if (part == "melody") v = melodyPart (p, s, seed, bpb);
    sortNotes (v);
    return v;
}

MusicPlan defaultPlan (const std::string& genre)
{
    MusicPlan p;
    p.genre = genre;
    p.sections = { { "Intro", 0, 2, -0.6f }, { "A", 2, 4, 0.0f }, { "B", 6, 4, 0.3f }, { "Break", 10, 2, -0.5f }, { "Final", 12, 4, 0.5f } };
    p.seeds = { { "drums", 11 }, { "bass", 23 }, { "chords", 37 }, { "melody", 41 }, { "pad", 53 } };
    if (genre == "reggaeton") { p.keyRoot = 9; p.minor = true; p.progression = { 0, 5, 2, 6 }; }       // Lam - Fa - Do - Sol
    else if (genre == "trap") { p.keyRoot = 4; p.minor = true; p.progression = { 0, 5, 3, 4 }; }       // Mim - Do - Lam - Si (V mayor)
    else if (genre == "hiphop") { p.keyRoot = 2; p.minor = true; p.progression = { 0, 3, 6, 2 }; }     // Rem7 - Solm7 - Do7 - Fa7
    else if (genre == "pop") { p.keyRoot = 7; p.minor = false; p.progression = { 0, 4, 5, 3 }; }       // Sol - Re - Mim - Do
    else if (genre == "electronic") { p.keyRoot = 5; p.minor = true; p.progression = { 0, 5, 2, 6 }; } // Fam - Reb - Lab - Mib
    else return MusicPlan();
    return p;
}
} // namespace composer
} // namespace cs
