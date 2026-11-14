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
    if (! variants.empty()) j["variants"] = variants;
    if (revision > 1) j["revision"] = revision;
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
        if (j.contains ("variants")) p.variants = j.at ("variants").get<std::map<std::string, uint32_t>>();
        p.revision = std::max (1, j.value ("revision", 1));
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

// «Familia» rítmica elegida por la semilla (0.8, para que Regenerar cambie de verdad el groove). La semilla por defecto
// de cada parte cae siempre en la familia 0, que es exactamente el patrón de la 0.7: las plantillas no cambian.
int grooveIndex (uint32_t seed, uint32_t defaultSeed, int n)
{
    auto h = [n] (uint32_t x) { Rand q (x ^ 0x6A09E667u); return (int) (q.next() % (uint64_t) n); };
    return (h (seed) - h (defaultSeed) + n) % n;
}

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
    if (p.minor && deg == 4 && (p.genre == "trap" || p.genre == "darkhiphop")) pcs[1] = (pcs[0] + 4) % 12; // V mayor
    return pcs;
}

int nearestPc (int pc, int near) // nota MIDI con clase de altura 'pc' más cercana a 'near'
{
    int best = near, bd = 99;
    for (int k = near - 7; k <= near + 7; ++k) if (((k % 12) + 12) % 12 == pc && std::abs (k - near) < bd) { bd = std::abs (k - near); best = k; }
    return best;
}

int melodyCenter (const std::string& g)
{
    return g == "trap" ? 72 : g == "hiphop" ? 69 : g == "boombap" ? 70 : g == "jazzrap" ? 74 : g == "darkhiphop" ? 67 : 74;
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
    const float swing = g == "hiphop" ? 0.28f : g == "boombap" ? 0.36f : g == "jazzrap" ? 0.33f : g == "darkhiphop" ? 0.16f : 0.0f;
    Rand r (seed ^ 0xD1u);
    const int groove = grooveIndex (seed, 11u, 4); // 0 = groove de siempre; 1-3 = otras familias del mismo estilo

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
            for (int s = 0; s < 16; s += (groove == 3 ? 1 : 2)) b.hit (kHat, s, s % 4 == 0 ? 74 : s % 2 == 0 ? 56 : 40);
            for (int s = 0; s < 16; ++s) if (e > -0.4 || s % 2 == 0) b.hit (kP3, s, s % 4 == 0 ? 70 : s % 2 == 0 ? 52 : 38);
            if (groove == 1) { b.hit (kP2, 5, 76); b.hit (kP2, 13, 72); b.hit (kP1, 7, 70); } // congas desplazadas
            else { b.hit (kP2, 2, 78); b.hit (kP2, 10, 74); }
            if (groove == 2) { b.hit (kSnare, 7, 84); b.hit (kSnare, 15, 88); b.hit (kKick, 6, 90); } // «perreo» más cargado
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
        if (groove == 1) kicks = { 0, 7, 10 };
        else if (groove == 2) kicks = { 0, 2, 11 };
        else if (groove == 3) kicks = { 0, 6, 9, 14 };
        std::vector<std::pair<int, int>> rolls;
        for (int s : { 6, 7, 11, 14, 15 }) if (r.chance (0.35 + 0.15 * e)) rolls.push_back ({ s, r.range (2, 4) });
        if (rolls.empty()) rolls.push_back ({ 14, 3 });
        const bool openHat = r.chance (0.5);
        auto main = [&] (PatternBuilder& b)
        {
            for (int s : kicks) b.hit (kKick, s, s == 0 ? 120 : 100);
            b.hit (kSnare, 8, 118); b.hit (kClap, 8, 104);
            if (groove == 1) for (int s = 0; s < 16; ++s) b.hit (kHat, s, s % 4 == 0 ? 84 : s % 2 == 0 ? 62 : 44); // semicorcheas
            else if (groove == 3) for (int s = 0; s < 16; s += 4) b.hit (kHat, s, 86);                               // negras: más aire
            else for (int s = 0; s < 16; s += 2) b.hit (kHat, s, s % 4 == 0 ? 86 : 64);
            if (e > 0.2) for (int s = 1; s < 16; s += 4) b.hit (kHat, s, 46);
            for (auto [s, n] : rolls) { b.hit (kHat, s, 72); b.roll (kHat, s, groove == 2 ? std::min (4, n + 1) : n); }
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
        if (groove == 1) kicks = { 0, 3, 10 };
        else if (groove == 2) kicks = { 0, 7, 8, 13 };
        else if (groove == 3) kicks = { 0, 10, 11 };
        std::vector<int> ghosts;
        for (int s : { 7, 9, 15 }) if (r.chance (0.45)) ghosts.push_back (s);
        const bool openHat = r.chance (0.5);
        auto main = [&] (PatternBuilder& b)
        {
            for (int s : kicks) b.hit (kKick, s, s == 0 ? 118 : 100);
            b.hit (kSnare, 4, 116); b.hit (kSnare, 12, 120);
            for (int s : ghosts) b.hit (kSnare, s, 42);
            for (int s = 0; s < 16; s += (groove == 2 ? 1 : 2)) b.hit (kHat, s, s % 4 == 0 ? 88 : s % 2 == 0 ? 54 : 38);
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
    else if (g == "boombap")
    {
        // Boom bap crudo (0.8): pocos elementos y golpes duros. Bombo seco con anticipación, caja fuerte en 2 y 4 sin
        // adornos, charles en corcheas con acentos muy marcados y algún hueco (menos mecánico). Mucho swing.
        std::vector<int> kicks { 0, 10 };
        if (r.chance (0.55)) kicks.push_back (7);
        if (r.chance (0.35)) kicks.push_back (15); // empuja hacia el siguiente compás
        if (groove == 1) kicks = { 0, 6, 10 };
        else if (groove == 2) kicks = { 0, 3, 7, 10 };
        else if (groove == 3) kicks = { 0, 9, 11 };
        std::vector<int> drops; // charles que faltan (respira)
        for (int s : { 6, 14 }) if (r.chance (0.3)) drops.push_back (s);
        const bool late = r.chance (0.5);
        auto main = [&] (PatternBuilder& b)
        {
            b.human = 14;
            for (int s : kicks) b.hit (kKick, s, s == 0 ? 124 : s == 15 ? 92 : 108);
            b.hit (kSnare, 4, 124); b.hit (kSnare, 12, 126);
            for (int s = 0; s < 16; s += 2)
                if (std::find (drops.begin(), drops.end(), s) == drops.end()) b.hit (kHat, s, s % 4 == 0 ? 96 : 46);
            if (late) b.hit (kSnare, 13, 38); // un eco de la caja, casi inaudible: carácter, no relleno
        };
        make ("main", "A", main);
        make ("var", "B", [&] (PatternBuilder& b) {
            main (b);
            b.hit (kKick, 3, 96);
            b.hit (kP1, 6, 70); b.hit (kP1, 14, 64);
            b.hit (kOpen, 10, 58); b.clear (kHat, 10, 11);
        });
        make ("varCrash", "B", [&] (PatternBuilder& b) { b.p = out["var"]; b.hit (kP3, 0, 80); });
        make ("fill", "Remate", [&] (PatternBuilder& b) {
            // Corte «de sampler»: el compás se queda casi vacío y la caja tartamudea al final.
            b.human = 10;
            b.hit (kKick, 0, 124); b.hit (kSnare, 4, 124); b.hit (kKick, 10, 110); b.hit (kKick, 11, 100);
            for (int s : { 12, 14, 15 }) b.hit (kSnare, s, 96 + (s - 12) * 9);
            for (int s = 0; s < 10; s += 2) b.hit (kHat, s, s % 4 == 0 ? 90 : 44);
        });
        make ("intro", "Intro", [&] (PatternBuilder& b) {
            for (int s = 0; s < 16; s += 2) b.hit (kHat, s, s % 4 == 0 ? 78 : 40);
            b.hit (kKick, 0, 90);
        });
        make ("break", "Break", [&] (PatternBuilder& b) { b.hit (kKick, 0, 118); b.hit (kSnare, 12, 118); });
    }
    else if (g == "jazzrap")
    {
        // Jazz rap / soul (0.8): bombo suave, caja de escobilla con muchas notas fantasma, ride con swing relajado y
        // acento en 2 y 4; aro de baqueta en lugar de caja en la sección B.
        std::vector<int> kicks { 0, 10 };
        if (r.chance (0.6)) kicks.push_back (7);
        if (groove == 1) kicks = { 0, 5, 10 };
        else if (groove == 2) kicks = { 0, 8 };
        else if (groove == 3) kicks = { 0, 7, 11 };
        std::vector<int> ghosts;
        for (int s : { 3, 7, 9, 11, 15 }) if (r.chance (0.55)) ghosts.push_back (s);
        if (groove == 2) ghosts = { 2, 6, 10, 14 };
        auto main = [&] (PatternBuilder& b)
        {
            b.human = 10;
            for (int s : kicks) b.hit (kKick, s, s == 0 ? 104 : 86);
            b.hit (kSnare, 4, 100); b.hit (kSnare, 12, 104);
            for (int s : ghosts) b.hit (kSnare, s, 34);
            for (int s = 0; s < 16; s += 2) b.hit (kHat, s, s == 4 || s == 12 ? 84 : s % 4 == 0 ? 66 : 50);
        };
        make ("main", "A", main);
        make ("var", "B", [&] (PatternBuilder& b) {
            main (b);
            b.clear (kSnare, 4, 5); b.clear (kSnare, 12, 13);
            b.hit (kP1, 4, 92); b.hit (kP1, 12, 96);         // aro de baqueta
            for (int s = 0; s < 16; ++s) b.hit (kP2, s, s % 4 == 0 ? 44 : 30); // escobilla continua
        });
        make ("varCrash", "B", [&] (PatternBuilder& b) { b.p = out["var"]; b.hit (kP3, 0, 70); });
        make ("fill", "Remate", [&] (PatternBuilder& b) {
            main (b);
            for (int s : { 13, 14, 15 }) b.hit (kSnare, s, 56 + (s - 13) * 14);
            b.hit (kOpen, 14, 60); b.clear (kHat, 14, 15);
        });
        make ("intro", "Intro", [&] (PatternBuilder& b) {
            for (int s = 0; s < 16; s += 2) b.hit (kHat, s, s == 4 || s == 12 ? 72 : 44);
            b.hit (kP1, 12, 60);
        });
        make ("break", "Break", [&] (PatternBuilder& b) {
            for (int s = 0; s < 16; s += 2) b.hit (kHat, s, 50);
            for (int s = 0; s < 16; ++s) b.hit (kP2, s, 30);
        });
    }
    else if (g == "darkhiphop")
    {
        // Dark hip-hop (0.8): golpes contundentes con espacio. Charles en negras (deja silencio), bombo profundo con un
        // doble golpe, caja con palmas en el 4 y huecos deliberados; el Break casi se vacía.
        std::vector<int> kicks { 0, 10 };
        if (r.chance (0.6)) kicks.push_back (11);
        if (r.chance (0.4)) kicks.push_back (3);
        if (groove == 1) kicks = { 0, 8, 11 };
        else if (groove == 2) kicks = { 0, 3, 6, 10 };
        else if (groove == 3) kicks = { 0, 11, 14 };
        auto main = [&] (PatternBuilder& b)
        {
            b.human = 6;
            for (int s : kicks) b.hit (kKick, s, s == 0 ? 124 : 110);
            b.hit (kSnare, 4, 118); b.hit (kSnare, 12, 122); b.hit (kClap, 12, 92);
            for (int s : { 0, 4, 8, 12 }) b.hit (kHat, s, 64);
            if (e > 0.2) for (int s : { 2, 6, 10, 14 }) b.hit (kHat, s, 40);
        };
        make ("main", "A", main);
        make ("var", "B", [&] (PatternBuilder& b) {
            main (b);
            for (int s : { 2, 6, 10, 14 }) b.hit (kHat, s, 46);
            b.hit (kP2, 15, 84);                       // tom grave que anuncia el compás siguiente
            if (r.chance (0.5)) { b.hit (kHat, 14, 60); b.roll (kHat, 14, 3); }
        });
        make ("varCrash", "B", [&] (PatternBuilder& b) { b.p = out["var"]; b.hit (kP3, 0, 96); });
        make ("fill", "Remate", [&] (PatternBuilder& b) {
            main (b);
            b.clear (kHat, 8);
            b.hit (kP2, 12, 96); b.hit (kP2, 14, 100); b.hit (kSnare, 15, 110);
        });
        make ("intro", "Intro", [&] (PatternBuilder& b) { b.hit (kKick, 0, 110); b.hit (kP3, 0, 70); b.hit (kHat, 8, 50); });
        make ("break", "Break", [&] (PatternBuilder& b) { b.hit (kKick, 0, 120); b.hit (kP2, 15, 70); });
    }
    else // pop y electrónica
    {
        const bool electro = g == "electronic";
        std::vector<int> kicks = electro ? std::vector<int> { 0, 4, 8, 12 } : std::vector<int> { 0, 8 };
        if (! electro && r.chance (0.6)) kicks.push_back (r.chance (0.5) ? 6 : 10);
        if (! electro && groove == 1) kicks = { 0, 6, 8 };
        else if (! electro && groove == 2) kicks = { 0, 8, 11 };
        else if (! electro && groove == 3) kicks = { 0, 3, 8, 10 };
        else if (electro && groove == 2) kicks = { 0, 4, 8, 12, 14 };
        // 0.9: la electrónica tenía una sola familia distinta; ahora Regenerar cambia de verdad el pie del beat:
        // 1 = breakbeat (bombo sincopado, caja fantasma), 3 = medio tiempo (caja solo en el 3). La familia 0 no cambia.
        else if (electro && groove == 1) kicks = { 0, 6, 10 };
        else if (electro && groove == 3) kicks = { 0, 10 };
        const bool halfTime = electro && groove == 3;
        const bool perc = r.chance (0.6);
        auto main = [&] (PatternBuilder& b)
        {
            for (int s : kicks) b.hit (kKick, s, s == 0 ? 118 : 108);
            if (halfTime) b.hit (kSnare, 8, 118);
            else { b.hit (kSnare, 4, 112); b.hit (kSnare, 12, 116); }
            if (electro && groove == 1) b.hit (kSnare, 7, 58);
            if (electro)
            {
                for (int s : { 2, 6, 10, 14 }) b.hit (kOpen, s, 74);
                if (e > -0.3) for (int s = 0; s < 16; ++s) if (s % 4 != 2) b.hit (kHat, s, s % 2 ? 40 : 58);
                if (perc) { b.hit (kP1, 3, 64); b.hit (kP1, 11, 58); }
                if (groove == 1) { b.hit (kClap, 7, 70); b.hit (kClap, 15, 76); }
                if (groove == 3) for (int s : { 1, 5, 9, 13 }) b.hit (kP1, s, 56);
            }
            else
            {
                for (int s = 0; s < 16; s += (groove == 2 ? 1 : 2)) b.hit (kHat, s, s % 4 == 0 ? 80 : s % 2 == 0 ? 60 : 42);
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
    // 0.8: una familia rítmica distinta de la 0 no cambia solo unos bombos del A. Cambia la «mano» del charles en todo
    // el beat (semicorcheas, contratiempo o figura 3-3-2) y añade una figura de percusión propia, conservando caja,
    // swing y kit del estilo. El trap ya cambia sus charles (redobles) en la rama propia, así que solo recibe la figura.
    if (groove != 0)
    {
        const int hatStyle = (groove + (int) (hashStr (g) % 3u)) % 3;
        static const std::vector<int> percFig[4] = { {}, { 3, 11 }, { 6, 14, 15 }, { 2, 7, 10 } };
        for (auto& [key, d] : out)
        {
            auto& hat = d.lanes[(size_t) kHat];
            const auto& open = d.lanes[(size_t) kOpen];
            int count = 0, peak = 0;
            for (int st = 0; st < d.steps; ++st) { count += hat[(size_t) st] > 0; peak = std::max (peak, hat[(size_t) st]); }
            if (g != "trap" && count >= 3)
            {
                PatternBuilder b { d, r, 4 };
                b.clear (kHat, 0, d.steps);
                for (int st = 0; st < d.steps; ++st)
                {
                    const int q = st % 16;
                    double v = 0;
                    if (hatStyle == 0) v = q % 4 == 0 ? peak : q % 2 == 0 ? peak * 0.72 : peak * 0.5;
                    else if (hatStyle == 1) v = q % 4 == 2 ? peak : q % 4 == 0 ? peak * 0.45 : q % 4 == 3 ? peak * 0.35 : 0;
                    else v = (q == 0 || q == 8) ? peak : (q == 3 || q == 6 || q == 11 || q == 14) ? peak * 0.8 : 0;
                    if (v > 0 && open[(size_t) st] == 0) b.hit (kHat, st, v);
                }
            }
            if ((key == "main" || key == "var" || key == "varCrash" || key == "fill"))
            {
                PatternBuilder b { d, r, 6 };
                for (int st : percFig[groove]) if (d.lanes[(size_t) kP1][(size_t) st] == 0) b.hit (kP1, st, 60);
            }
        }
    }
    // Cierre (misión 0.6): un solo golpe con platillo (o charles abierto en el kit de dembow, que no tiene platillo)
    // sobre el que se apaga el acorde final.
    make ("end", "Cierre", [&] (PatternBuilder& b) {
        b.hit (kKick, 0, 118);
        if (g == "reggaeton") { b.hit (kSnare, 0, 100); b.hit (kOpen, 0, 92); }
        else b.hit (kP3, 0, 104);
    });
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
    else if (s.name == "Cierre") add ("end", n, s.name);
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

std::vector<MidiNote> bassPart (const MusicPlan& p, const SongSection& s, Rand& r, int bpb, uint32_t seed)
{
    std::vector<MidiNote> v;
    // 0.9.2: en Pop, Dark Hip-hop y Electrónica el bajo apenas dependía de la semilla (Rehacer «Bajo» no hacía nada en
    // Pop ni en Dark). La semilla elige ahora un patrón; la semilla por defecto (23) da el de siempre.
    const int bassGroove = grooveIndex (seed, 23u, 3);
    const double e = sectionEnergy (p, s);
    const Tick bar = (Tick) bpb * kPpq;
    const std::string& g = p.genre;
    if (s.name == "Intro" && ! hipHopFamily (g)) return v; // el bajo entra con la sección principal
    if (s.name == "Cierre") { note (v, nearestPc (p.keyRoot, 36), 108, 0, bar - S); return v; } // tónica que se apaga
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
        else if (g == "boombap")
        {
            // Bajo seco sobre los bombos (0, 7, 10): notas cortas, huecos, a veces la octava baja. Nada de legato.
            if (s.name == "Intro") continue;
            note (v, root, 112, t0, 3 * S);
            if (! sparse && r.chance (0.6)) note (v, r.chance (0.4) ? root - 12 >= 24 ? root - 12 : root : root, 98, t0 + 7 * S, 2 * S);
            note (v, r.chance (0.7) ? root : fifth, 104, t0 + 10 * S, 3 * S);
            if (! sparse && nextRoot != root && r.chance (0.5)) note (v, nextRoot, 90, t0 + 15 * S, S);
        }
        else if (g == "jazzrap")
        {
            // Bajo «caminando» en negras: raíz, nota del acorde, quinta u octava y aproximación cromática a la raíz
            // siguiente. Intensidad alternada (el 1 y el 3 un poco más fuertes).
            if (s.name == "Intro") { note (v, root, 80, t0, 8 * S); continue; }
            const auto ch4 = chordPcs (p, absBar, true);
            const int third = nearestPc (ch4[1], root + 4), sev = nearestPc (ch4[3], root + 10);
            const int walk[4] = { root, r.chance (0.5) ? third : sev - 12 >= 28 ? sev - 12 : third, r.chance (0.6) ? fifth : root + 12,
                                  nextRoot + (nextRoot > root ? -1 : 1) };
            const int beats = sparse ? 2 : 4;
            for (int q = 0; q < beats; ++q) note (v, walk[q * (4 / beats)], q % 2 == 0 ? 100 : 86, t0 + q * (16 / beats) * S, (16 / beats) * S - S / 2);
        }
        else if (g == "darkhiphop")
        {
            // Sub largo en la raíz durante medio compás, silencio y una caída de octava o aproximación al final.
            if (s.name == "Intro") { note (v, nearestPc (p.keyRoot, 36), 96, t0, bar - S); continue; }
            if (bassGroove == 1 && ! sparse) { note (v, root, 114, t0, 11 * S); note (v, root + 12, 96, t0 + 12 * S, 3 * S); continue; }
            if (bassGroove == 2 && ! sparse) { note (v, root, 114, t0, 6 * S); note (v, fifth, 100, t0 + 7 * S, 3 * S); note (v, root, 104, t0 + 12 * S, 3 * S); continue; }
            note (v, root, 114, t0, 9 * S);
            if (! sparse) note (v, root, 104, t0 + 10 * S, 3 * S);
            if (! sparse && (r.chance (0.5) || nextRoot != root)) note (v, nextRoot != root ? nextRoot : root + 12, 96, t0 + 14 * S, 2 * S - S / 4);
        }
        else if (g == "electronic")
        {
            // Bajo «rodante» a contratiempo (deja el golpe del bombo libre).
            if (sparse) { note (v, root, 96, t0, bar - S); continue; }
            if (bassGroove == 1) // corcheas con octava en el contratiempo
            {
                for (int st = 0; st < 16; st += 2) note (v, st % 4 == 2 ? root + 12 : root, st % 4 == 2 ? 100 : 88, t0 + st * S, S + S / 2);
                continue;
            }
            if (bassGroove == 2) // contratiempo con anticipación de semicorchea y quinta en la segunda mitad
            {
                for (int st : { 2, 3, 6, 7, 10, 11, 14 }) note (v, st >= 10 && st <= 11 ? fifth : root, st % 4 == 2 ? 106 : 82, t0 + st * S, S);
                continue;
            }
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
            if (bassGroove == 1) // sincopado: 1, «y» del 2, 3 y anticipación
            {
                for (int st : { 0, 3, 6, 8, 11, 14 }) note (v, st == 11 ? fifth : st == 6 ? root + 12 : root, st % 8 == 0 ? 106 : 92, t0 + st * S, 2 * S - S / 3);
                continue;
            }
            if (bassGroove == 2) // negras con paso de quinta
            {
                for (int st : { 0, 4, 8, 12 }) note (v, st == 8 ? fifth : root, st == 0 ? 108 : 94, t0 + st * S, 3 * S);
                if (nextRoot != root) note (v, nextRoot, 86, t0 + 15 * S, S - S / 4);
                continue;
            }
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

std::vector<MidiNote> chordPart (const MusicPlan& p, const SongSection& s, Rand& r, int bpb, bool pad, uint32_t seed)
{
    const int comp = grooveIndex (seed, 37u, 3); // ritmo de acompañamiento: 0 = el de siempre
    std::vector<MidiNote> v;
    const double e = sectionEnergy (p, s);
    const Tick bar = (Tick) bpb * kPpq;
    const std::string& g = p.genre;
    const bool sevenths = g == "hiphop" || g == "jazzrap" || (g == "pop" && ! pad && r.chance (0.3));
    std::vector<int> prev;
    for (int b = 0; b < s.bars; ++b)
    {
        const auto pcs = chordPcs (p, s.startBar + b, sevenths);
        // 0.9.2: el Pad no dependía de la semilla (Variar/Rehacer «Pad» no hacían nada). Ahora la semilla elige una de
        // tres texturas; la semilla por defecto (53) da exactamente el Pad de siempre.
        const int padStyle = pad ? grooveIndex (seed, 53u, 3) : 0;
        const auto vc = pad ? (padStyle == 2 ? voiceLead (pcs, prev, 67, 59, 76) : voiceLead (pcs, prev, 60, 52, 70)) : voiceLead (pcs, prev, 64, 57, 74);
        prev = vc;
        const Tick t0 = b * bar;
        if (pad)
        {
            const int vel = s.name == "Break" || s.name == "Intro" || s.name == "Cierre" ? 78 : 66;
            if (padStyle == 1 && s.name != "Cierre") // dos ataques por compás: pulso lento que respira
            {
                for (int k : vc) note (v, k, vel, t0, bar / 2 - S / 2);
                for (int k : vc) note (v, k, vel - 8, t0 + bar / 2, bar / 2 - S / 2);
            }
            else for (int k : vc) note (v, k, vel, t0, bar - S / 2); // 0: colchón de siempre; 2: registro más agudo
            continue;
        }
        auto hitAll = [&] (int step, int len, double vel) { for (int k : vc) note (v, k, vel + r.range (-4, 4), t0 + step * S, len * S - S / 4); };
        if (g == "jazzrap" && s.name != "Cierre" && s.name != "Intro")
        {
            // Teclas soul: acorde con novena, cuerdas escalonadas (ligero rasgueo) y anticipación del acorde siguiente.
            std::vector<int> nine = vc;
            const auto sc = scale (p);
            const int deg = p.progression[(size_t) (((s.startBar + b) % (int) p.progression.size()) + (int) p.progression.size()) % (int) p.progression.size()];
            nine.push_back (nearestPc (sc[(size_t) ((deg + 1) % 7)], vc.back() + 3));
            if (comp == 1) { for (int st : { 0, 8 }) for (size_t i = 0; i < nine.size(); ++i) note (v, nine[i], 74 + r.range (-5, 5), t0 + st * S + (Tick) i * (S / 8), 6 * S); continue; }
            if (comp == 2) { for (int st : { 0, 6 }) for (size_t i = 0; i < nine.size(); ++i) note (v, nine[i], (st ? 70 : 80) + r.range (-5, 5), t0 + st * S + (Tick) i * (S / 8), (st ? 8 : 5) * S); continue; } // «Charleston»
            for (size_t i = 0; i < nine.size(); ++i) note (v, nine[i], 78 + r.range (-5, 5), t0 + (Tick) i * (S / 8), 9 * S);
            if (e > -0.5 && s.name != "Break")
            {
                const auto nx = voiceLead (chordPcs (p, s.startBar + b + 1, true), vc, 64, 57, 74);
                for (int k : nx) note (v, k, 70 + r.range (-4, 4), t0 + 14 * S + S / 3, 2 * S - S / 3);
            }
            continue;
        }
        if (s.name == "Cierre") { hitAll (0, 15, 92); continue; }
        if (s.name == "Intro" || s.name == "Break" || e < -0.5) { hitAll (0, 15, 74); continue; }
        if (g == "reggaeton")
        {
            static const std::vector<int> c0 { 0, 3, 6, 10, 14 }, c1 { 0, 3, 6, 8, 11, 14 }, c2 { 2, 6, 10, 14 };
            for (int st : comp == 1 ? c1 : comp == 2 ? c2 : c0) hitAll (st, 2, st == 0 ? 96 : 84);
        }
        else if (g == "hiphop")
        {
            if (comp == 1) hitAll (0, 15, 84);
            else if (comp == 2) { hitAll (0, 3, 92); hitAll (4, 3, 80); hitAll (10, 5, 84); }
            else { hitAll (0, r.chance (0.5) ? 7 : 9, 92); hitAll (10, 5, 80); }
        }
        else if (g == "boombap") // golpes cortos «recortados»
        {
            if (comp == 1) { hitAll (0, 2, 96); hitAll (3, 2, 86); hitAll (8, 3, 90); }
            else if (comp == 2) hitAll (0, 7, 92);
            else { hitAll (0, 3, 96); if (r.chance (0.7)) hitAll (r.chance (0.5) ? 6 : 10, 2, 84); }
        }
        else if (g == "darkhiphop") // acordes largos con espacio
        {
            // 0.9.2: las variantes 1 y 2 solo cambiaban la duración (Rehacer «Acordes» casi no se notaba): ahora tienen
            // otro ritmo. La 0 (semilla por defecto) es la de siempre.
            if (comp == 1) { hitAll (0, 6, 78); hitAll (8, 6, 70); }
            else if (comp == 2) { hitAll (0, 5, 70); hitAll (6, 9, 62); }
            else if (b % 2 == 0 || e > 0.2) hitAll (0, 15, 70);
        }
        else if (g == "electronic")
        {
            static const std::vector<int> c0 { 2, 6, 10, 14 }, c1 { 0, 3, 6, 10, 13 }, c2 { 2, 6, 10, 14, 15 };
            for (int st : comp == 1 ? c1 : comp == 2 ? c2 : c0) hitAll (st, 1, 90);
        }
        else if (g == "pop")
        {
            if (comp == 1) for (int st : { 0, 3, 6, 8, 11, 14 }) hitAll (st, 2, st % 8 == 0 ? 92 : 78);
            else if (comp == 2) for (int st : { 0, 4, 8, 12 }) hitAll (st, 3, st == 0 ? 92 : 80);
            else for (int st = 0; st < 16; st += 2) hitAll (st, 1, st % 4 == 0 ? 92 : 76);
        }
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
    if (s.name == "Cierre")
    {
        // La melodía se posa en la tónica y la deja sonar.
        note (v, nearestPc (p.keyRoot, melodyCenter (g)), 96, 0, bar - S);
        return v;
    }
    // A y Final comparten motivo (reconocible); B tiene el suyo. En el Final el motivo vuelve reforzado: las notas de
    // los tiempos fuertes se doblan una octava arriba (misión 0.6: que el final se sienta como la llegada).
    const bool lift = s.name == "Final";
    Rand r (seed ^ (s.name == "B" ? 0xB0B0u : 0xA0A0u));
    const auto sc = scale (p);
    const int center = nearestPc (p.keyRoot, melodyCenter (g));
    const int lo = center - 7, hi = center + 12;
    std::vector<int> all; // notas de la escala dentro del registro
    for (int k = lo; k <= hi; ++k) if (std::find (sc.begin(), sc.end(), ((k % 12) + 12) % 12) != sc.end()) all.push_back (k);
    auto idxOf = [&] (int key) { int best = 0; for (int i = 0; i < (int) all.size(); ++i) if (std::abs (all[(size_t) i] - key) < std::abs (all[(size_t) best] - key)) best = i; return best; };

    // Ritmo del motivo: 32 semicorcheas (2 compases). El motivo se genera con la energía GLOBAL de la canción, no con
    // la de la sección: así A y Final tienen exactamente el mismo motivo (mismo ritmo y mismas alturas) y se reconoce.
    // La energía de la sección no cambia el motivo: añade notas de paso entre sus notas (adornos) y fuerza.
    const double eBase = std::clamp ((double) p.energy, -1.0, 1.0);
    const double baseDensity = g == "trap" ? 0.32 : g == "reggaeton" ? 0.42 : g == "hiphop" ? 0.3 : g == "boombap" ? 0.22 : g == "jazzrap" ? 0.34 : g == "darkhiphop" ? 0.18 : 0.45;
    const double density = std::clamp (baseDensity + 0.18 * eBase, 0.12, 0.8);
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
    // Adornos: en secciones con más energía que la base, notas de paso en corcheas libres (otra secuencia aleatoria,
    // para no alterar el motivo).
    Rand orn (seed ^ 0x0E0E0E0Eu ^ (hashStr (s.name) * 131u));
    const double ornProb = std::max (0.0, e - eBase) * 0.4;
    struct Ev { int st; int motif; }; // motif = índice en onsets, o -1 si es un adorno
    std::vector<Ev> evs;
    for (int st = 0, i = 0; st < 32; ++st)
    {
        if (i < (int) onsets.size() && onsets[(size_t) i] == st) { evs.push_back ({ st, i }); ++i; continue; }
        const bool roll = st % 2 == 0 && st > 0 && st < 28;
        if (roll && orn.chance (ornProb)) evs.push_back ({ st, -1 });
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
        for (size_t n = 0; n < evs.size(); ++n)
        {
            const int st = evs[n].st;
            const int barIn = barBase + st / 16;
            if (barIn >= s.bars) break;
            const int nextSt = n + 1 == evs.size() ? 32 : evs[n + 1].st;
            const int cap = g == "trap" || g == "reggaeton" || g == "boombap" ? 3 : g == "hiphop" || g == "jazzrap" ? 4 : g == "darkhiphop" ? 8 : 6;
            const Tick at = barBase * bar + (Tick) st * S;
            if (evs[n].motif < 0)
            {
                // Nota de paso: un grado hacia la siguiente nota del motivo; no mueve el motivo.
                if (thin) continue;
                size_t nm = n + 1;
                while (nm < evs.size() && evs[nm].motif < 0) ++nm;
                const int dir = nm < evs.size() && moves[(size_t) evs[nm].motif] < 0 ? -1 : 1;
                const int k = all[(size_t) std::clamp (cur + dir, 0, (int) all.size() - 1)];
                note (v, k, 74 + 10 * e + orn.range (-4, 4), at, std::min (std::max (1, nextSt - st), 2) * S - S / 4);
                continue;
            }
            const size_t i = (size_t) evs[n].motif;
            if (thin && i % 2 == 1) continue;
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
            int lenSteps = std::max (1, nextSt - st);
            lenSteps = std::min (lenSteps, last ? cap + 2 : cap);
            const double vel = (st % 4 == 0 ? 100 : 86) + 10 * e + r.range (-5, 5) - (thin ? 14 : 0);
            note (v, key, vel, at, lenSteps * S - S / 4);
            if (lift && st % 4 == 0 && key + 12 <= 96) note (v, key + 12, vel - 20, at, lenSteps * S - S / 4);
        }
    }
    return v;
}
} // namespace

std::vector<MidiNote> partNotes (const MusicPlan& p, const std::string& part, const SongSection& s, uint32_t seed, int bpb)
{
    Rand r (seed ^ hashStr (part) ^ (hashStr (s.name) * 31u) ^ (uint32_t) s.startBar);
    std::vector<MidiNote> v;
    if (part == "bass") v = bassPart (p, s, r, bpb, seed);
    else if (part == "chords") v = chordPart (p, s, r, bpb, false, seed);
    else if (part == "pad") v = chordPart (p, s, r, bpb, true, seed);
    else if (part == "melody") v = melodyPart (p, s, seed, bpb);
    const auto var = p.variants.find (part);
    if (var != p.variants.end() && var->second != 0) mutateNotes (p, part, v, var->second ^ (hashStr (s.name) * 97u) ^ (uint32_t) s.startBar, bpb);
    sortNotes (v);
    return v;
}

bool hipHopFamily (const std::string& g) { return g == "hiphop" || g == "boombap" || g == "jazzrap" || g == "darkhiphop"; }

void mutateNotes (const MusicPlan& p, const std::string& part, std::vector<MidiNote>& notes, uint32_t variant, int bpb)
{
    if (notes.empty() || variant == 0) return;
    Rand r (variant ^ hashStr (part) ^ 0x5EEDu);
    const Tick bar = (Tick) bpb * kPpq;
    const auto sc = scale (p);
    auto inScale = [&] (int k) { return std::find (sc.begin(), sc.end(), ((k % 12) + 12) % 12) != sc.end(); };
    auto stepScale = [&] (int k, int dir) { int x = k; do x += dir; while (! inScale (x) && std::abs (x - k) < 4); return inScale (x) ? x : k; };
    std::vector<MidiNote> out;
    for (size_t i = 0; i < notes.size(); ++i)
    {
        MidiNote n = notes[i];
        const bool downbeat = n.start % bar == 0;
        if (part == "melody")
        {
            // Mismo ritmo y mismo contorno general: algunas notas débiles se mueven un grado; alguna larga se parte.
            if (! downbeat && r.chance (0.35)) n.key = std::clamp (stepScale (n.key, r.chance (0.5) ? 1 : -1), 0, 127);
            // 0.9.2: en Electrónica la melodía es un arpegio; saltar de octava algunas notas débiles es lo que más se
            // oye en el altavoz del teléfono (Variar casi solo tocaba bombo y bajo).
            else if (p.genre == "electronic" && ! downbeat && r.chance (0.3)) n.key = n.key + 12 <= 96 ? n.key + 12 : n.key - 12;
            if (n.length >= 4 * S && r.chance (0.18))
            {
                MidiNote a = n, b = n;
                a.length = n.length / 2; b.start = n.start + a.length; b.length = n.length - a.length;
                b.key = std::clamp (stepScale (n.key, r.chance (0.5) ? 1 : -1), 0, 127); b.velocity = (uint8_t) std::max (1, n.velocity - 10);
                out.push_back (a); out.push_back (b);
                continue;
            }
            n.velocity = (uint8_t) clampVel (n.velocity + r.range (-6, 6));
        }
        else if (part == "bass")
        {
            // Mismo bajo en el 1 de cada compás; en el resto, saltos de octava, alguna nota fuera (más aire) o un
            // golpe que se adelanta una semicorchea.
            if (! downbeat)
            {
                const double u = r.uni();
                if (u < 0.25) n.key = n.key + 12 <= 60 ? n.key + 12 : n.key - 12 >= 24 ? n.key - 12 : n.key;
                else if (u < 0.38) continue; // silencio
                else if (u < 0.5 && n.start >= S) n.start -= S, n.length += S / 2;
            }
        }
        else // chords, pad: misma armonía; en algunos acordes enteros, la nota más aguda baja (otra inversión)
        {
            const Tick slot = n.start / S;
            Rand cr (variant ^ (uint32_t) slot * 2654435761u);
            const bool reVoice = cr.chance (part == "pad" ? 0.4 : 0.22), push = part == "chords" && ! downbeat && cr.chance (0.18);
            if (reVoice && n.key >= 67) n.key -= 12;
            else if (reVoice && part == "pad" && n.key < 60) n.key += 12; // 0.9.2: el Pad (52..70) casi nunca llegaba a 67
            // 0.9.2: en el Pad, alguna nota del acorde entra una corchea tarde (se nota sin cambiar la armonía).
            if (part == "pad" && downbeat && cr.chance (0.3) && n.length > 2 * S) { n.start += 2 * S; n.length -= 2 * S; }
            if (push) n.start = std::max<Tick> (0, n.start - S);
            n.velocity = (uint8_t) clampVel (n.velocity + r.range (-6, 6));
        }
        out.push_back (n);
    }
    notes = std::move (out);
}

void mutatePattern (const MusicPlan& p, DrumPattern& d, uint32_t variant, const std::string& key)
{
    if (variant == 0 || key == "end") return;
    Rand r (variant ^ hashStr (key) ^ 0xD5u);
    const int n = d.steps;
    auto lane = [&] (int l) -> std::vector<int>& { return d.lanes[(size_t) l]; };
    auto roll = [&] (int l) -> std::vector<int>& { return d.rolls[(size_t) l]; };
    // Charles: añade o quita algunas semicorcheas débiles (nunca las negras).
    for (int s = 1; s < n; s += 2)
        if (r.chance (0.22)) { auto& h = lane (kHat); h[(size_t) s] = h[(size_t) s] ? 0 : clampVel (38 + r.range (0, 18)); }
    // Caja: alguna nota fantasma nueva o quitada (2 y 4 intactos).
    for (int s : { 3, 7, 9, 11, 15 })
        if (s < n && r.chance (0.2)) { auto& c = lane (kSnare); if (c[(size_t) s] < 60) c[(size_t) s] = c[(size_t) s] ? 0 : 36 + r.range (0, 10); }
    // Bombo: un golpe que no sea el 1 se mueve una semicorchea, o aparece uno nuevo en la segunda mitad.
    auto& k = lane (kKick);
    std::vector<int> ks;
    for (int s = 1; s < n; ++s) if (k[(size_t) s]) ks.push_back (s);
    if (! ks.empty() && r.chance (0.6))
    {
        const int from = ks[(size_t) r.range (0, (int) ks.size() - 1)];
        const int to = std::clamp (from + (r.chance (0.5) ? 1 : -1), 1, n - 1);
        if (! k[(size_t) to] && to % 4 != 0) { k[(size_t) to] = k[(size_t) from]; k[(size_t) from] = 0; }
    }
    else { const int s = std::min (n - 1, r.chance (0.5) ? 13 : 14); if (! k[(size_t) s]) k[(size_t) s] = 92; }
    // Un redoble de charles en trap o dark; un charles abierto en el resto.
    if ((p.genre == "trap" || p.genre == "darkhiphop") && r.chance (0.6)) { const int s = std::min (n - 1, r.chance (0.5) ? 11 : 15); lane (kHat)[(size_t) s] = 70; roll (kHat)[(size_t) s] = r.range (2, 4); }
    else if (r.chance (0.35) && n > 14) { lane (kOpen)[(size_t) 14] = 64; lane (kHat)[(size_t) 14] = 0; }
}

int drumGroove (uint32_t seed) { return grooveIndex (seed, 11u, 4); }

std::vector<std::vector<int>> progressionBank (const std::string& g)
{
    if (g == "trap") return { { 0, 5, 3, 4 }, { 0, 3, 5, 4 }, { 0, 6, 5, 6 }, { 0, 5, 0, 4 }, { 0, 2, 5, 4 } };
    if (g == "reggaeton") return { { 0, 5, 2, 6 }, { 0, 3, 6, 2 }, { 0, 6, 5, 6 }, { 0, 5, 3, 4 } };
    if (g == "hiphop") return { { 0, 3, 6, 2 }, { 0, 5, 3, 4 }, { 0, 3, 0, 4 }, { 0, 6, 5, 4 } };
    if (g == "boombap") return { { 0, 0, 5, 4 }, { 0, 3, 0, 3 }, { 0, 0, 3, 4 }, { 0, 5, 6, 0 } };
    if (g == "jazzrap") return { { 1, 4, 0, 5 }, { 0, 5, 1, 4 }, { 3, 2, 1, 0 }, { 1, 4, 2, 5 } };
    if (g == "darkhiphop") return { { 0, 0, 5, 1 }, { 0, 5, 0, 4 }, { 0, 1, 0, 5 }, { 0, 0, 3, 4 } };
    if (g == "pop") return { { 0, 4, 5, 3 }, { 0, 5, 3, 4 }, { 5, 3, 0, 4 }, { 0, 3, 4, 3 } };
    if (g == "electronic") return { { 0, 5, 2, 6 }, { 0, 3, 5, 4 }, { 0, 6, 5, 6 }, { 0, 2, 5, 6 } };
    return {};
}

MusicPlan defaultPlan (const std::string& genre)
{
    MusicPlan p;
    p.genre = genre;
    p.sections = { { "Intro", 0, 2, -0.6f }, { "A", 2, 4, 0.0f }, { "B", 6, 4, 0.3f }, { "Break", 10, 2, -0.5f }, { "Final", 12, 4, 0.5f },
                   { "Cierre", 16, 1, -0.2f } }; // misión 0.6: la canción termina (acorde de tónica que se apaga), no se corta
    p.seeds = { { "drums", 11 }, { "bass", 23 }, { "chords", 37 }, { "melody", 41 }, { "pad", 53 } };
    if (genre == "reggaeton") { p.keyRoot = 9; p.minor = true; p.progression = { 0, 5, 2, 6 }; }       // Lam - Fa - Do - Sol
    else if (genre == "trap") { p.keyRoot = 4; p.minor = true; p.progression = { 0, 5, 3, 4 }; }       // Mim - Do - Lam - Si (V mayor)
    else if (genre == "hiphop") { p.keyRoot = 2; p.minor = true; p.progression = { 0, 3, 6, 2 }; }     // Rem7 - Solm7 - Do7 - Fa7
    else if (genre == "pop") { p.keyRoot = 7; p.minor = false; p.progression = { 0, 4, 5, 3 }; }       // Sol - Re - Mim - Do
    else if (genre == "electronic") { p.keyRoot = 5; p.minor = true; p.progression = { 0, 5, 2, 6 }; } // Fam - Reb - Lab - Mib
    else if (genre == "boombap") { p.keyRoot = 1; p.minor = true; p.progression = { 0, 0, 5, 4 }; }    // Do#m - Do#m - La - Sol#m
    else if (genre == "jazzrap") { p.keyRoot = 5; p.minor = false; p.progression = { 1, 4, 0, 5 }; }   // Solm9 - Do9 - Fa(maj7)9 - Rem9 (ii-V-I-vi)
    else if (genre == "darkhiphop") { p.keyRoot = 0; p.minor = true; p.progression = { 0, 0, 5, 1 }; } // Dom - Dom - Lab - Re°
    else return MusicPlan();
    return p;
}
} // namespace composer
} // namespace cs
