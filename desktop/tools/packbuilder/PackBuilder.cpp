// cauce_packs: construye los paquetes Cauce Signature Sounds de forma REPRODUCIBLE (misión 0.9).
//
//   cauce_packs <carpeta VCSL> <carpeta salida> [solo: drums|bass|piano|ebass] [carpeta Black And Blue Basses]
//
// - Sonidos originales: síntesis propia con parámetros y semillas fijas (mismo resultado en cada ejecución).
// - Sonidos de terceros: solo de bibliotecas CC0 verificadas (VCSL de Versilian Studios; Black And Blue Basses de
//   Karoryfer). Cada archivo de salida queda anotado en PROVENANCE.md con su archivo de origen y el procesado.
// - Se escribe cauce-pack.json con el SHA-256 de cada archivo. Formato: docs/CAUCE_PACK_FORMAT.md.
// Las muestras de terceros NO están en el repositorio: se regeneran con esta herramienta desde los originales.
#include "cs/Packs.h"
#include "cs/Project.h"
#include "cs/Resampler.h"
#include "cs/Wav.h"

#include "../../../core/src/Dsp.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace cs;
using cs::dsp::Biquad;
using cs::dsp::kPi;
using cs::dsp::Rng;
using Buf = std::vector<float>;

namespace
{
constexpr int kRate = 48000;
const double sr = kRate;
std::string gVcsl, gBbb;
const char* kRetrieved = "2026-11-13";
const char* kVcslCommit = "c1ea7bcc3c7309650ab0da9d15c9cd1fbc4a4c7e";

[[noreturn]] void die (const std::string& m) { std::fprintf (stderr, "error: %s\n", m.c_str()); std::exit (1); }

// --- Utilidades de audio ------------------------------------------------------------------------------------------
Buf len (double s) { return Buf ((size_t) std::max (1.0, s * sr), 0.0f); }
void fadeOut (Buf& b, double sec) { const size_t f = std::min (b.size(), (size_t) (sec * sr)); for (size_t i = 0; i < f; ++i) { const double x = (double) i / f; b[b.size() - 1 - i] *= (float) (0.5 - 0.5 * std::cos (kPi * x)); } }
float peakOf (const Buf& b) { float m = 0; for (float v : b) m = std::max (m, std::fabs (v)); return m; }
void gain (Buf& b, float g) { for (float& v : b) v *= g; }
Buf lowpass (Buf b, double hz, double q = 0.7) { Biquad f; f.set (Biquad::LowPass, sr, hz, q); for (float& v : b) v = f.process (v); return b; }
Buf highpass (Buf b, double hz) { Biquad f; f.set (Biquad::HighPass, sr, hz, 0.7); for (float& v : b) v = f.process (v); return b; }
Buf shelf (Buf b, bool high, double hz, double db) { Biquad f; f.set (high ? Biquad::HighShelf : Biquad::LowShelf, sr, hz, 0.7, db); for (float& v : b) v = f.process (v); return b; }
Buf peakEq (Buf b, double hz, double q, double db) { Biquad f; f.set (Biquad::Peak, sr, hz, q, db); for (float& v : b) v = f.process (v); return b; }
Buf saturate (Buf b, double drive) { const double n = std::tanh (drive); for (float& v : b) v = (float) (std::tanh (v * drive) / n); return b; }
// Grano de sampler antiguo: reducción de bits con retención x2 y paso bajo para quitar el zumbido.
Buf grit (Buf b, int bits)
{
    const double q = std::pow (2.0, bits - 1);
    float hold = 0;
    for (size_t i = 0; i < b.size(); ++i) { if (i % 2 == 0) hold = (float) (std::round (b[i] * q) / q); b[i] = hold; }
    return lowpass (b, 12000);
}
void mixInto (Buf& d, const Buf& s, float g, size_t off = 0) { if (d.size() < s.size() + off) d.resize (s.size() + off, 0.0f); for (size_t i = 0; i < s.size(); ++i) d[i + off] += s[i] * g; }
void addRoom (Buf& b, double amount, double decay, uint32_t seed)
{
    const size_t dry = b.size();
    Buf out (dry + (size_t) (decay * sr * 3), 0.0f);
    std::copy (b.begin(), b.end(), out.begin());
    static const double taps[6][2] = { { 0.0071, 0.42 }, { 0.0113, 0.35 }, { 0.0167, 0.3 }, { 0.0229, 0.22 }, { 0.0311, 0.17 }, { 0.0397, 0.12 } };
    for (const auto& t : taps) { const size_t d = (size_t) (t[0] * sr); for (size_t i = 0; i < dry && i + d < out.size(); ++i) out[i + d] += b[i] * (float) (t[1] * amount); }
    Rng rng (seed);
    Biquad lp; lp.set (Biquad::LowPass, sr, 5500, 0.7);
    double env = 0;
    for (size_t i = 0; i < out.size(); ++i) { const double x = i < dry ? std::fabs (b[i]) : 0.0; env = std::max (x, env * std::exp (-1.0 / (decay * sr))); out[i] += (float) (lp.process (rng.next()) * env * 0.35 * amount); }
    b.swap (out);
    fadeOut (b, 0.02);
}
// Recorta silencio inicial (con 1 ms de margen) y la cola por debajo de -60 dB, con un máximo y fundido final.
Buf trim (Buf b, double maxSec, double fadeSec)
{
    const float p = peakOf (b);
    if (p <= 0) return b;
    size_t start = 0;
    while (start < b.size() && std::fabs (b[start]) < p * 0.003f) ++start;
    start = start > 48 ? start - 48 : 0;
    size_t end = b.size();
    while (end > start + 1 && std::fabs (b[end - 1]) < p * 0.001f) --end;
    end = std::min (end, start + (size_t) (maxSec * sr));
    Buf out (b.begin() + (long) start, b.begin() + (long) end);
    for (size_t i = 0; i < std::min<size_t> (48, out.size()); ++i) if (start > 0) out[i] *= (float) i / 48.0f;
    fadeOut (out, std::min (fadeSec, (double) out.size() / sr * 0.5));
    return out;
}

// --- Síntesis propia (mismas familias que Cauce Drums, con parámetros por capa y por toma) ---------------------------
Buf kick (double f0, double f1, double pitchTau, double subTau, double knockAmt, double click, double drive, double length, uint32_t seed)
{
    Buf b = len (length);
    Rng rng (seed);
    Biquad hp; hp.set (Biquad::HighPass, sr, 2500, 0.7);
    Biquad bp; bp.set (Biquad::BandPass, sr, 4500, 1.2);
    double ph = 0, ph2 = 0;
    const double dn = std::tanh (drive);
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double f = f1 + (f0 - f1) * std::exp (-t / pitchTau);
        ph += f / sr; ph2 += 2.0 * f / sr;
        const double sub = std::sin (2 * kPi * ph) * std::exp (-t / subTau);
        const double knock = std::sin (2 * kPi * ph2) * std::exp (-t / 0.028) * knockAmt;
        double x = (sub + knock) * std::min (1.0, t / 0.0006);
        x = std::tanh (x * drive) / dn;
        const double c = t < 0.004 ? (hp.process (rng.next()) * 0.6 + bp.process (rng.next()) * 0.4) * click * (1.0 - t / 0.004) : 0.0;
        b[i] = (float) (x + c);
    }
    fadeOut (b, 0.01);
    return b;
}
Buf snare (double tone, double toneTau, double noiseTau, double hpHz, double lpHz, double toneMix, double length, uint32_t seed)
{
    Buf b = len (length);
    Rng rng (seed);
    Biquad hp, lp; hp.set (Biquad::HighPass, sr, hpHz, 0.7); lp.set (Biquad::LowPass, sr, lpHz, 0.7);
    double ph1 = 0, ph2 = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double bend = 1.0 + 0.5 * std::exp (-t / 0.01);
        ph1 += tone * bend / sr; ph2 += tone * 1.6 * bend / sr;
        const double body = (std::sin (2 * kPi * ph1) + 0.5 * std::sin (2 * kPi * ph2)) * std::exp (-t / toneTau);
        const double n = lp.process (hp.process (rng.next())) * std::exp (-t / noiseTau);
        b[i] = (float) ((body * toneMix + n * (1.0 - toneMix) * 2.2) * std::min (1.0, t / 0.0005));
    }
    fadeOut (b, 0.01);
    return b;
}
Buf clapSynth (double center, double tail, double length, uint32_t seed, double spacing = 0.0095)
{
    Buf b = len (length);
    Rng rng (seed);
    Biquad bp; bp.set (Biquad::BandPass, sr, center, 1.4);
    Biquad hp; hp.set (Biquad::HighPass, sr, 600, 0.7);
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        double env = 0;
        for (int k = 0; k < 3; ++k) { const double tk = t - k * spacing; if (tk >= 0 && tk < spacing) env = std::max (env, std::exp (-tk / 0.0025)); }
        const double tt = t - 3 * spacing;
        if (tt >= 0) env = std::max (env, std::exp (-tt / tail));
        b[i] = (float) (hp.process (bp.process (rng.next())) * env);
    }
    fadeOut (b, 0.01);
    return b;
}
Buf hat (double tau, double hpHz, double length, uint32_t seed, double metal = 0.7, double detune = 1.7)
{
    static const double fr[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    Buf b = len (length);
    Rng rng (seed);
    Biquad bp; bp.set (Biquad::BandPass, sr, 10000, 0.9);
    Biquad hp1, hp2; hp1.set (Biquad::HighPass, sr, hpHz, 0.7); hp2.set (Biquad::HighPass, sr, hpHz, 0.7);
    double ph[6] = { 0, 0.1, 0.2, 0.3, 0.4, 0.5 };
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        double m = 0;
        for (int k = 0; k < 6; ++k) { ph[k] += fr[k] * detune / sr; ph[k] -= std::floor (ph[k]); m += ph[k] < 0.5 ? 1.0 : -1.0; }
        const double x = m / 6.0 * metal + rng.next() * (1.0 - metal);
        const double env = std::exp (-t / tau) * std::min (1.0, t / 0.0004);
        b[i] = (float) (hp2.process (hp1.process (bp.process ((float) x))) * env);
    }
    fadeOut (b, 0.005);
    return b;
}
Buf rimSynth (double hz, double tau, uint32_t seed)
{
    Buf b = len (0.12);
    Biquad bp; bp.set (Biquad::BandPass, sr, hz, 3.0);
    Rng rng (seed);
    double ph = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        ph += hz / sr;
        const double x = (ph - std::floor (ph) < 0.5 ? 1.0 : -1.0) * 0.6 + rng.next() * 0.4;
        b[i] = (float) (bp.process ((float) x) * std::exp (-t / tau));
    }
    fadeOut (b, 0.005);
    return b;
}
Buf crashSynth (double tau, double length, uint32_t seed)
{
    static const double fr[6] = { 263.0, 400.0, 421.0, 474.0, 587.0, 845.0 };
    Buf b = len (length);
    Rng rng (seed);
    Biquad hp; hp.set (Biquad::HighPass, sr, 4200, 0.7);
    Biquad hp2; hp2.set (Biquad::HighPass, sr, 4200, 0.7);
    double ph[6] = {};
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        double m = 0;
        for (int k = 0; k < 6; ++k) { ph[k] += fr[k] * 2.9 / sr; ph[k] -= std::floor (ph[k]); m += ph[k] < 0.5 ? 1.0 : -1.0; }
        const double x = m / 6.0 * 0.5 + rng.next() * 0.5;
        const double env = (0.7 * std::exp (-t / tau) + 0.3 * std::exp (-t / 0.05)) * std::min (1.0, t / 0.001);
        b[i] = (float) (hp2.process (hp.process ((float) x)) * env);
    }
    fadeOut (b, length * 0.35);
    return b;
}
Buf membrane (double hz, double tau, double slap, double ring, double length, uint32_t seed)
{
    Buf b = len (length);
    Rng rng (seed);
    Biquad bp; bp.set (Biquad::BandPass, sr, hz * 4.2, 1.6);
    double p1 = 0, p2 = 0;
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double bend = 1.0 + 0.18 * std::exp (-t / 0.012);
        p1 += hz * bend / sr; p2 += hz * 1.59 * bend / sr;
        const double body = std::sin (2 * kPi * p1) * std::exp (-t / tau) + std::sin (2 * kPi * p2) * ring * std::exp (-t / (tau * 0.6));
        const double n = t < 0.02 ? bp.process (rng.next()) * slap * std::exp (-t / 0.004) : 0.0;
        b[i] = (float) ((body + n) * std::min (1.0, t / 0.0004));
    }
    fadeOut (b, 0.01);
    return b;
}
Buf snapSynth (uint32_t seed, double hz = 2300)
{
    Buf b = len (0.18);
    Rng rng (seed);
    Biquad bp; bp.set (Biquad::BandPass, sr, hz, 2.2);
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double env = std::exp (-t / 0.012) + (t > 0.004 ? 0.6 * std::exp (-(t - 0.004) / 0.03) : 0.0);
        b[i] = (float) (bp.process (rng.next()) * env);
    }
    fadeOut (b, 0.005);
    return b;
}

// --- Muestras de VCSL ----------------------------------------------------------------------------------------------
Buf loadMono (const std::string& rel)
{
    AudioData a;
    std::string err;
    const std::string path = gVcsl + "/" + rel;
    if (! readWav (path, a, err)) die ("no se pudo leer " + path + ": " + err);
    if (a.sampleRate != kRate) a = resample (a, kRate);
    Buf m (a.channels[0].size(), 0.0f);
    for (const auto& ch : a.channels) for (size_t i = 0; i < m.size(); ++i) m[i] += ch[i] / (float) a.numChannels();
    return m;
}

// --- Escritura del paquete ------------------------------------------------------------------------------------------
struct Layer { int lo, hi; };
struct PackWriter
{
    std::string dir;
    std::ostringstream provenance;
    explicit PackWriter (const std::string& d) : dir (d)
    {
        std::error_code ec;
        fs::remove_all (d, ec);
        fs::create_directories (d + "/instruments", ec);
        fs::create_directories (d + "/samples", ec);
    }
    void writeBuf (const std::string& rel, const Buf& b, const std::string& origin)
    {
        AudioData a;
        a.sampleRate = kRate;
        a.channels = { b };
        std::string err;
        fs::create_directories (fs::path (dir + "/" + rel).parent_path());
        WriteStats st;
        if (! writeWav (dir + "/" + rel, a, WavFormat::Pcm16, err, &st)) die (err);
        if (st.clippedSamples) die ("muestra recortada al escribir: " + rel);
        provenance << "| `" << rel << "` | " << origin << " |\n";
    }
    void writeStereo (const std::string& rel, const AudioData& a, const std::string& origin)
    {
        std::string err;
        fs::create_directories (fs::path (dir + "/" + rel).parent_path());
        WriteStats st;
        if (! writeWav (dir + "/" + rel, a, WavFormat::Pcm16, err, &st)) die (err);
        if (st.clippedSamples) die ("muestra recortada al escribir: " + rel);
        provenance << "| `" << rel << "` | " << origin << " |\n";
    }
    void text (const std::string& rel, const std::string& t)
    {
        std::string err;
        fs::create_directories (fs::path (dir + "/" + rel).parent_path());
        if (! atomicWriteFile (dir + "/" + rel, t, err, false)) die (err);
    }
    void finish (PackManifest& m, const std::string& licenseText, const std::string& provHeader)
    {
        text ("LICENSE.txt", licenseText);
        text ("PROVENANCE.md", provHeader + "\n| Archivo | Origen y procesado |\n|---|---|\n" + provenance.str());
        std::string err;
        if (! fillPackFiles (dir, m, err)) die (err);
        text ("cauce-pack.json", packManifestToJson (m));
        std::printf ("paquete %s %s: %zu archivos, %.2f MB\n", m.id.c_str(), m.version.c_str(), m.files.size(), m.totalBytes() / 1048576.0);
    }
};

// Una pieza de batería: varias capas (cada una con varias tomas). Todas las tomas comparten normalización (se
// conserva la diferencia de nivel natural entre capas) y la pieza queda con el pico 'peak'.
struct Piece
{
    std::string name;
    int key;
    float peak;
    std::vector<Layer> layers;
    std::vector<std::vector<Buf>> takes;      // [capa][toma]
    std::vector<std::vector<std::string>> origins;
    float ampVeltrack = 35.0f;
    int group = 0, offBy = 0;
};

std::string writePiece (PackWriter& w, const std::string& kit, Piece& p)
{
    float m = 0;
    for (auto& l : p.takes) for (auto& t : l) m = std::max (m, peakOf (t));
    if (m <= 0) die ("pieza en silencio: " + p.name);
    std::ostringstream sfz;
    sfz << "\n// " << p.name << "\n<group> key=" << p.key << " loop_mode=one_shot pitch_keytrack=0 amp_veltrack=" << p.ampVeltrack;
    if (p.group) sfz << " group=" << p.group;
    if (p.offBy) sfz << " off_by=" << p.offBy;
    sfz << "\n";
    for (size_t li = 0; li < p.takes.size(); ++li)
        for (size_t ti = 0; ti < p.takes[li].size(); ++ti)
        {
            Buf b = p.takes[li][ti];
            gain (b, p.peak / m);
            const std::string rel = "samples/" + kit + "/" + p.name + "_v" + std::to_string (li + 1) + "_rr" + std::to_string (ti + 1) + ".wav";
            w.writeBuf (rel, b, p.origins[li][ti]);
            sfz << "<region> lovel=" << p.layers[li].lo << " hivel=" << p.layers[li].hi;
            if (p.takes[li].size() > 1) sfz << " seq_length=" << p.takes[li].size() << " seq_position=" << ti + 1;
            sfz << " sample=../" << rel << "\n";
        }
    return sfz.str();
}

const std::vector<Layer> kL3 = { { 1, 60 }, { 61, 100 }, { 101, 127 } };
const std::vector<Layer> kL2 = { { 1, 80 }, { 81, 127 } };
const std::vector<Layer> kL1 = { { 1, 127 } };

// Variación por toma: pequeñas diferencias de afinación y caída (como dos golpes reales nunca idénticos).
double rrPitch (int take) { static const double v[] = { 1.0, 1.012, 0.991, 1.006 }; return v[take % 4]; }
double rrDecay (int take) { static const double v[] = { 1.0, 0.94, 1.05, 0.97 }; return v[take % 4]; }

std::string orig (const char* what) { return std::string ("Original CAUCESOUND (síntesis, ") + what + ")"; }
std::string vcsl (const std::string& rel, const std::string& proc) { return "VCSL (CC0) `" + rel + "` → " + proc; }

const char* kSnare1 = "Membranophones/Struck Membranophones/Snare Drum, Modern 1/";
const char* kSnare3 = "Membranophones/Struck Membranophones/Snare Drum, Modern 3/";
const char* kHat = "Idiophones/Struck Idiophones/Hi-Hat Cymbal/";
const char* kClaps = "Idiophones/Struck Idiophones/Claps/";
const char* kShaker = "Idiophones/Struck Idiophones/Shaker, Small/";
const char* kConga = "Membranophones/Struck Membranophones/Conga/";
const char* kBongo = "Membranophones/Struck Membranophones/Bongos/";
const char* kGuiro = "Idiophones/Struck Idiophones/Guiro/";
const char* kCymbal = "Idiophones/Struck Idiophones/Suspended Cymbal 1/";

// Añade a la pieza una capa con tomas de VCSL procesadas por 'proc'.
void addVcslLayer (Piece& p, const std::string& folder, const std::vector<std::string>& files, const std::function<Buf (Buf)>& proc, const char* procText)
{
    std::vector<Buf> takes;
    std::vector<std::string> origins;
    for (const auto& f : files) { takes.push_back (proc (loadMono (folder + f))); origins.push_back (vcsl (folder + f, procText)); }
    p.takes.push_back (takes);
    p.origins.push_back (origins);
}

void addSynthLayer (Piece& p, int takes, const std::function<Buf (int)>& make, const char* what)
{
    std::vector<Buf> t;
    std::vector<std::string> o;
    for (int i = 0; i < takes; ++i) { t.push_back (make (i)); o.push_back (orig (what)); }
    p.takes.push_back (t);
    p.origins.push_back (o);
}

// ==================================================================================================================
// Cauce Signature Drums
// ==================================================================================================================
std::string rawBoomBap (PackWriter& w)
{
    std::string sfz = "// Cauce Raw Boom Bap — CAUCESOUND. Muestras: síntesis propia + VCSL (CC0) procesado. Ver PROVENANCE.md\n";
    { // Bombo: corto, con cuerpo, «polvoriento» (paso bajo + saturación + grano de 12 bits)
        Piece p { "kick", 36, 0.95f, kL3 };
        const double drive[3] = { 2.0, 2.8, 3.6 }, click[3] = { 0.2, 0.35, 0.5 };
        for (int l = 0; l < 3; ++l)
            addSynthLayer (p, 3, [&] (int t) { return grit (lowpass (kick (135 * rrPitch (t), 49, 0.03, 0.27 * rrDecay (t), 0.6, click[l], drive[l], 0.75, 300 + 10 * l + t), 3600), 12); },
                           "bombo en capas, paso bajo 3,6 kHz, grano 12 bits");
        sfz += writePiece (w, "boombap", p);
    }
    { // Caja: caja real de VCSL, áspera: saturación, medios marcados, oscura y con grano
        Piece p { "snare", 37, 0.78f, kL3 };
        auto proc = [] (Buf b) { b = highpass (b, 110); b = peakEq (b, 1800, 1.0, 3); b = saturate (b, 2.6); b = lowpass (b, 8500); b = grit (b, 12); return trim (b, 0.42, 0.12); };
        addVcslLayer (p, kSnare1, { "Snare2_HitSN_v3_rr1_Mid.wav", "Snare2_HitSN_v3_rr2_Mid.wav" }, proc, "paso alto 110 Hz, +3 dB a 1,8 kHz, saturación, paso bajo 8,5 kHz, grano 12 bits, recorte 0,42 s");
        addVcslLayer (p, kSnare1, { "Snare2_HitSN_v6_rr1_Mid.wav", "Snare2_HitSN_v6_rr2_Mid.wav" }, proc, "ídem");
        addVcslLayer (p, kSnare1, { "Snare2_HitSN_v9_rr1_Mid.wav", "Snare2_HitSN_v9_rr2_Mid.wav" }, proc, "ídem");
        sfz += writePiece (w, "boombap", p);
    }
    { // Palmas reales, secas y con grano
        Piece p { "clap", 38, 0.68f, kL1 };
        auto proc = [] (Buf b) { b = highpass (b, 300); b = saturate (b, 1.8); b = lowpass (b, 9000); b = grit (b, 12); return trim (b, 0.35, 0.1); };
        addVcslLayer (p, kClaps, { "Clap_rr1.wav", "Clap_rr2.wav", "Clap_rr3.wav", "Clap_rr4.wav" }, proc, "paso alto 300 Hz, saturación, paso bajo 9 kHz, grano 12 bits");
        p.ampVeltrack = 70;
        sfz += writePiece (w, "boombap", p);
    }
    { // Charles cerrado orgánico (real), oscuro
        Piece p { "hat_closed", 39, 0.42f, kL3 };
        p.group = 1;
        auto proc = [] (Buf b) { b = highpass (b, 400); b = lowpass (b, 10500); b = saturate (b, 1.4); return trim (b, 0.22, 0.08); };
        addVcslLayer (p, kHat, { "HiHat_HitC_v1_rr1_Mid.wav", "HiHat_HitC_v1_rr2_Mid.wav" }, proc, "paso alto 400 Hz, paso bajo 10,5 kHz, saturación suave, recorte 0,22 s");
        addVcslLayer (p, kHat, { "HiHat_HitC_v2_rr1_Mid.wav", "HiHat_HitC_v2_rr2_Mid.wav", "HiHat_HitC_v3_rr1_Mid.wav" }, proc, "ídem");
        addVcslLayer (p, kHat, { "HiHat_HitC_v4_rr1_Mid.wav", "HiHat_HitC_v4_rr2_Mid.wav" }, proc, "ídem");
        sfz += writePiece (w, "boombap", p);
    }
    { // Charles abierto (lo corta el cerrado: off_by)
        Piece p { "hat_open", 40, 0.38f, kL1 };
        p.offBy = 1;
        auto proc = [] (Buf b) { b = highpass (b, 400); b = lowpass (b, 10000); return trim (b, 0.9, 0.35); };
        addVcslLayer (p, kHat, { "HiHat_HitO_rr1_Mid.wav", "HiHat_HitO_rr2_Mid.wav" }, proc, "paso alto 400 Hz, paso bajo 10 kHz, recorte 0,9 s");
        p.ampVeltrack = 60;
        sfz += writePiece (w, "boombap", p);
    }
    { // Golpe de aro (cross-stick real)
        Piece p { "rim", 41, 0.55f, kL1 };
        auto proc = [] (Buf b) { b = highpass (b, 250); b = saturate (b, 1.6); b = lowpass (b, 9000); return trim (b, 0.2, 0.06); };
        addVcslLayer (p, kSnare3, { "Snare4_Xstick_v2_rr1_Mid.wav", "Snare4_Xstick_v2_rr2_Mid.wav" }, proc, "paso alto 250 Hz, saturación, paso bajo 9 kHz");
        p.ampVeltrack = 70;
        sfz += writePiece (w, "boombap", p);
    }
    { // Shaker discreto
        Piece p { "shaker", 42, 0.45f, kL1 };
        auto proc = [] (Buf b) { b = highpass (b, 1500); b = lowpass (b, 11000); return trim (b, 0.2, 0.06); };
        addVcslLayer (p, kShaker, { "Mid_ShakerDouble_Down_rr1.wav", "Mid_ShakerDouble_Down_rr2.wav", "Mid_ShakerDouble_Up_rr1.wav" }, proc, "paso alto 1,5 kHz, paso bajo 11 kHz");
        p.ampVeltrack = 70;
        sfz += writePiece (w, "boombap", p);
    }
    { // Platillo oscuro (real, recortado)
        Piece p { "crash", 43, 0.6f, kL2 };
        auto proc = [] (Buf b) { b = highpass (b, 300); b = lowpass (b, 8000); return trim (b, 2.2, 0.9); };
        addVcslLayer (p, kCymbal, { "susCymb1_hit_mp1.wav" }, proc, "paso alto 300 Hz, paso bajo 8 kHz, recorte 2,2 s");
        addVcslLayer (p, kCymbal, { "susCymb1_hit_f1.wav" }, proc, "ídem");
        sfz += writePiece (w, "boombap", p);
    }
    return sfz;
}

std::string darkTrap (PackWriter& w)
{
    std::string sfz = "// Cauce Dark Trap — CAUCESOUND. Muestras: síntesis propia + palmas VCSL (CC0). Ver PROVENANCE.md\n";
    { // Bombo con pegada que deja sitio al 808
        Piece p { "kick", 36, 0.95f, kL3 };
        const double drive[3] = { 2.0, 2.6, 3.2 }, click[3] = { 0.45, 0.65, 0.85 };
        for (int l = 0; l < 3; ++l)
            addSynthLayer (p, 3, [&] (int t) { return kick (180 * rrPitch (t), 50, 0.035, 0.16 * rrDecay (t), 0.45, click[l], drive[l], 0.5, 400 + 10 * l + t); }, "bombo en capas, caída de tono 180→50 Hz");
        sfz += writePiece (w, "trap", p);
    }
    { // Caja seca: «crack» sintetizado + palma real debajo
        Piece p { "snare", 37, 0.78f, kL3 };
        const double tone[3] = { 0.42, 0.32, 0.26 };
        for (int l = 0; l < 3; ++l)
            addSynthLayer (p, 3, [&] (int t)
            {
                Buf b = snare (205 * rrPitch (t), 0.05, 0.1 * rrDecay (t), 1800, 12000, tone[l], 0.32, 500 + 10 * l + t);
                Buf c = highpass (loadMono (std::string (kClaps) + "Clap_rr" + std::to_string (t + 1) + ".wav"), 500);
                mixInto (b, trim (c, 0.3, 0.1), 0.45f);
                return saturate (b, 1.5 + 0.3 * l);
            }, "caja sintetizada + palma VCSL `Idiophones/Struck Idiophones/Claps/Clap_rr1-3.wav` (CC0) a -7 dB, saturación");
        sfz += writePiece (w, "trap", p);
    }
    { // Palma con carácter: palmas reales con sala corta
        Piece p { "clap", 38, 0.72f, kL1 };
        auto proc = [] (Buf b) { b = highpass (b, 450); b = saturate (b, 1.7); b = trim (b, 0.35, 0.1); addRoom (b, 0.7, 0.11, 600); return b; };
        addVcslLayer (p, kClaps, { "Clap_rr1.wav", "Clap_rr2.wav", "Clap_rr3.wav", "Clap_rr4.wav", "Clap_rr5.wav", "Clap_rr6.wav" }, proc, "paso alto 450 Hz, saturación, sala corta sintetizada");
        p.ampVeltrack = 65;
        sfz += writePiece (w, "trap", p);
    }
    { // Charles definidos: cortos y brillantes, 4 tomas (los redobles no suenan a ametralladora)
        Piece p { "hat_closed", 39, 0.4f, kL3 };
        p.group = 1;
        const double tau[3] = { 0.014, 0.018, 0.022 };
        for (int l = 0; l < 3; ++l)
            addSynthLayer (p, 4, [&] (int t) { return hat (tau[l] * rrDecay (t), 9200 + 150 * t, 0.12, 700 + 10 * l + t, 0.72, 1.7 * rrPitch (t)); }, "seis osciladores inarmónicos + ruido");
        sfz += writePiece (w, "trap", p);
    }
    { // Charles abierto
        Piece p { "hat_open", 40, 0.38f, kL1 };
        p.offBy = 1;
        addSynthLayer (p, 2, [] (int t) { return hat (0.28 * rrDecay (t), 8200, 1.1, 800 + t); }, "charles abierto sintetizado");
        p.ampVeltrack = 60;
        sfz += writePiece (w, "trap", p);
    }
    { // Aro
        Piece p { "rim", 41, 0.55f, kL1 };
        addSynthLayer (p, 3, [] (int t) { return rimSynth (1800 * rrPitch (t), 0.02, 900 + t); }, "aro sintetizado");
        p.ampVeltrack = 60;
        sfz += writePiece (w, "trap", p);
    }
    { // Percusión oscura: membrana grave con chasquido
        Piece p { "perc_dark", 42, 0.55f, kL1 };
        addSynthLayer (p, 3, [] (int t) { Buf b = membrane (180 * rrPitch (t), 0.12, 0.9, 0.3, 0.45, 1000 + t); addRoom (b, 0.6, 0.14, 1010 + t); return b; }, "membrana afinada con sala");
        p.ampVeltrack = 60;
        sfz += writePiece (w, "trap", p);
    }
    { // Platillo oscuro
        Piece p { "crash", 43, 0.6f, kL1 };
        addSynthLayer (p, 2, [] (int t) { return lowpass (crashSynth (1.0 * rrDecay (t), 2.3, 1100 + t), 6500); }, "platillo sintetizado, paso bajo 6,5 kHz");
        sfz += writePiece (w, "trap", p);
    }
    return sfz;
}

std::string latinPulse (PackWriter& w)
{
    std::string sfz = "// Cauce Latin Pulse — CAUCESOUND. Muestras: síntesis propia + percusión VCSL (CC0). Ver PROVENANCE.md\n";
    { // Bombo de dembow: corto, redondo
        Piece p { "kick", 36, 0.95f, kL3 };
        const double drive[3] = { 1.8, 2.2, 2.7 }, click[3] = { 0.3, 0.5, 0.7 };
        for (int l = 0; l < 3; ++l)
            addSynthLayer (p, 3, [&] (int t) { return kick (210 * rrPitch (t), 56, 0.022, 0.2 * rrDecay (t), 0.35, click[l], drive[l], 0.6, 1200 + 10 * l + t); }, "bombo corto 210→56 Hz");
        sfz += writePiece (w, "latin", p);
    }
    { // Caja seca con aro (la del dembow)
        Piece p { "snare", 37, 0.75f, kL3 };
        const double mix[3] = { 0.55, 0.45, 0.38 };
        for (int l = 0; l < 3; ++l)
            addSynthLayer (p, 3, [&] (int t) { Buf b = snare (260 * rrPitch (t), 0.04, 0.07 * rrDecay (t), 2200, 11000, mix[l], 0.3, 1300 + 10 * l + t); mixInto (b, rimSynth (1900, 0.018, 1350 + t), 0.5f); addRoom (b, 0.45, 0.06, 1360 + t); return b; },
                           "caja + aro sintetizados, sala corta");
        sfz += writePiece (w, "latin", p);
    }
    { // Palmas reales con sala
        Piece p { "clap", 38, 0.7f, kL1 };
        auto proc = [] (Buf b) { b = highpass (b, 500); b = shelf (b, true, 5000, 2); b = trim (b, 0.35, 0.1); addRoom (b, 0.6, 0.08, 1400); return b; };
        addVcslLayer (p, kClaps, { "Clap_rr1.wav", "Clap_rr2.wav", "Clap_rr3.wav", "Clap_rr4.wav" }, proc, "paso alto 500 Hz, +2 dB agudos, sala corta");
        p.ampVeltrack = 65;
        sfz += writePiece (w, "latin", p);
    }
    { // Charles reales, brillantes y cortos
        Piece p { "hat_closed", 39, 0.4f, kL3 };
        p.group = 1;
        auto proc = [] (Buf b) { b = highpass (b, 600); b = shelf (b, true, 8000, 2); return trim (b, 0.16, 0.06); };
        addVcslLayer (p, kHat, { "HiHat_HitC_v1_rr1_Mid.wav", "HiHat_HitC_v1_rr2_Mid.wav" }, proc, "paso alto 600 Hz, +2 dB a 8 kHz, recorte 0,16 s");
        addVcslLayer (p, kHat, { "HiHat_HitC_v2_rr2_Mid.wav", "HiHat_HitC_v3_rr2_Mid.wav" }, proc, "ídem");
        addVcslLayer (p, kHat, { "HiHat_HitC_v4_rr1_Mid.wav", "HiHat_HitC_v4_rr2_Mid.wav" }, proc, "ídem");
        sfz += writePiece (w, "latin", p);
    }
    { // Charles abierto real
        Piece p { "hat_open", 40, 0.36f, kL1 };
        p.offBy = 1;
        auto proc = [] (Buf b) { b = highpass (b, 600); return trim (b, 0.7, 0.3); };
        addVcslLayer (p, kHat, { "HiHat_HitO_rr1_Mid.wav", "HiHat_HitO_rr2_Mid.wav" }, proc, "paso alto 600 Hz, recorte 0,7 s");
        p.ampVeltrack = 60;
        sfz += writePiece (w, "latin", p);
    }
    { // Bongó (real): tres fuerzas, dos tomas
        Piece p { "bongo", 41, 0.6f, kL3 };
        auto proc = [] (Buf b) { b = highpass (b, 150); return trim (b, 0.4, 0.12); };
        addVcslLayer (p, kBongo, { "BongoH_Hit1_v1_rr1_Mid.wav", "BongoH_Hit1_v1_rr2_Mid.wav" }, proc, "paso alto 150 Hz, recorte 0,4 s");
        addVcslLayer (p, kBongo, { "BongoH_Hit1_v2_rr1_Mid.wav", "BongoH_Hit1_v2_rr2_Mid.wav" }, proc, "ídem");
        addVcslLayer (p, kBongo, { "BongoH_Hit1_v3_rr1_Mid.wav", "BongoH_Hit1_v3_rr2_Mid.wav" }, proc, "ídem");
        sfz += writePiece (w, "latin", p);
    }
    { // Conga (real)
        Piece p { "conga", 42, 0.62f, kL3 };
        auto proc = [] (Buf b) { b = highpass (b, 80); return trim (b, 0.55, 0.15); };
        addVcslLayer (p, kConga, { "Conga_HitN_v1_rr1_Sum.wav", "Conga_HitN_v1_rr2_Sum.wav" }, proc, "paso alto 80 Hz, recorte 0,55 s");
        addVcslLayer (p, kConga, { "Conga_HitN_v2_rr1_Sum.wav", "Conga_HitN_v2_rr2_Sum.wav" }, proc, "ídem");
        addVcslLayer (p, kConga, { "Conga_HitN_v3_rr1_Sum.wav", "Conga_HitN_v3_rr2_Sum.wav" }, proc, "ídem");
        sfz += writePiece (w, "latin", p);
    }
    { // Güiro (real), golpe corto
        Piece p { "guiro", 43, 0.5f, kL1 };
        auto proc = [] (Buf b) { b = highpass (b, 900); return trim (b, 0.3, 0.08); };
        addVcslLayer (p, kGuiro, { "Guiro_Hit_rr1_Mid.wav", "Guiro_Hit_rr2_Mid.wav" }, proc, "paso alto 900 Hz, recorte 0,3 s");
        p.ampVeltrack = 60;
        sfz += writePiece (w, "latin", p);
    }
    return sfz;
}

void buildDrums (const std::string& out)
{
    PackWriter w (out + "/cauce.signature.drums");
    w.text ("instruments/raw-boombap.sfz", rawBoomBap (w));
    w.text ("instruments/dark-trap.sfz", darkTrap (w));
    w.text ("instruments/latin-pulse.sfz", latinPulse (w));
    PackManifest m;
    m.id = "cauce.signature.drums";
    m.version = "1.0.0";
    m.name = "Cauce Signature Drums";
    m.author = "CAUCESOUND";
    m.description = "Tres kits con capas por fuerza y varias tomas por golpe: Raw Boom Bap, Dark Trap y Latin Pulse.";
    m.license = "LicenseRef-CauceSound-Content AND CC0-1.0";
    m.licenseFile = "LICENSE.txt";
    m.minEngine = "0.9.0";
    m.sources.push_back ({ "Versilian Community Sample Library (VCSL)", "Versilian Studios LLC", "https://github.com/sgossner/VCSL", kVcslCommit, "CC0-1.0", kRetrieved,
                           "Licencia CC0 1.0 (archivo LICENSE del repositorio). Uso comercial y redistribución permitidos sin atribución. Muestras procesadas aquí (ver PROVENANCE.md)." });
    m.sources.push_back ({ "Síntesis CAUCESOUND", "CAUCESOUND", "desktop/tools/packbuilder/PackBuilder.cpp", "0.9", "LicenseRef-CauceSound-Content", kRetrieved,
                           "Sonidos originales generados por síntesis con parámetros y semillas fijas." });
    const std::vector<std::string> lanesBB = { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Rim", "Shaker", "Crash" };
    PackInstrument bb { "raw-boombap", "Cauce Raw Boom Bap", "drums", "drumkit", "instruments/raw-boombap.sfz",
                        "Bombo corto y con cuerpo, caja real áspera, charles orgánicos, grano de sampler antiguo.", "original + cc0",
                        { "Boom Bap", "Vintage", "Raw", "Dusty", "Organic" }, { "boombap", "jazzrap", "hiphop" }, lanesBB, 24 };
    PackInstrument tr { "dark-trap", "Cauce Dark Trap", "drums", "drumkit", "instruments/dark-trap.sfz",
                        "Bombo con pegada para el 808, caja seca con palma real, charles definidos con 4 tomas, percusión oscura.", "original + cc0",
                        { "Trap", "Dark", "Modern", "Punchy" }, { "trap", "darkhiphop" }, { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Rim", "Dark Perc", "Crash" }, 24 };
    PackInstrument la { "latin-pulse", "Cauce Latin Pulse", "drums", "drumkit", "instruments/latin-pulse.sfz",
                        "Bombo y caja de dembow, palmas, bongó, conga y güiro reales con varias fuerzas y tomas.", "original + cc0",
                        { "Reggaetón", "Latin", "Dembow", "Organic" }, { "reggaeton", "latin", "dembow" }, { "Kick", "Snare + Rim", "Clap", "Closed Hat", "Open Hat", "Bongó", "Conga", "Güiro" }, 24 };
    m.instruments = { bb, tr, la };
    w.finish (m, "Cauce Signature Drums 1.0.0\n\n"
                 "1) Sonidos sintetizados por CAUCESOUND (ver PROVENANCE.md): contenido propio de CAUCESOUND. Se distribuye dentro de la app\n"
                 "   para crear música; las canciones creadas con ellos son del usuario sin regalías. No se pueden redistribuir las\n"
                 "   muestras sueltas como biblioteca de sonidos sin permiso. (Texto provisional: revisión jurídica pendiente antes de\n"
                 "   publicar.)\n"
                 "2) Muestras derivadas de VCSL (Versilian Studios LLC): CC0 1.0 Universal (dominio público).\n"
                 "   https://creativecommons.org/publicdomain/zero/1.0/  —  https://github.com/sgossner/VCSL\n",
             "# Procedencia — Cauce Signature Drums 1.0.0\n\nGenerado por `cauce_packs` (desktop/tools/packbuilder). VCSL en el commit " + std::string (kVcslCommit) + " (consultado " + kRetrieved + ").\n");
}

// ==================================================================================================================
// Cauce Bass Lab: 808 Lab y Deep Sub (síntesis propia, muestreada a varias notas)
// ==================================================================================================================
Buf render808 (double hz, double drive, double click, uint32_t seed)
{
    Buf b = len (2.6);
    Rng rng (seed);
    Biquad hp; hp.set (Biquad::HighPass, sr, 3000, 0.7);
    double ph = 0;
    const double dn = std::tanh (1.0 + drive * 3.0);
    for (size_t i = 0; i < b.size(); ++i)
    {
        const double t = (double) i / sr;
        const double f = hz * std::pow (2.0, 7.0 / 12.0 * std::exp (-t / 0.035)); // golpe de tono: +7 semitonos en ~35 ms
        ph += f / sr;
        const double amp = std::exp (-t / 1.4) * std::min (1.0, t / 0.001);
        double x = std::sin (2 * kPi * ph) * amp;
        x = std::tanh (x * (1.0 + drive * 3.0)) / dn;  // armónicos: se oye en altavoces pequeños
        const double c = t < 0.004 ? hp.process (rng.next()) * click * (1.0 - t / 0.004) : 0.0;
        b[i] = (float) (x * 0.9 + c);
    }
    fadeOut (b, 0.4);
    return b;
}
// Sub sostenido con bucle exacto: número entero de ciclos, sin clic en el empalme.
Buf renderSub (double hz, int cycles, double h2, double h3)
{
    const size_t n = (size_t) std::lround (cycles * sr / hz);
    const double f = cycles * sr / (double) n; // frecuencia exacta para el bucle
    Buf b (n);
    for (size_t i = 0; i < n; ++i)
    {
        const double ph = f * (double) i / sr;
        b[i] = (float) (0.8 * (std::sin (2 * kPi * ph) + h2 * std::sin (4 * kPi * ph) + h3 * std::sin (6 * kPi * ph)) / (1.0 + h2 + h3));
    }
    return b;
}

void buildBass (const std::string& out)
{
    PackWriter w (out + "/cauce.bass.lab");
    std::ostringstream s808, ssub;
    s808 << "// Cauce 808 Lab — síntesis CAUCESOUND muestreada cada quinta. Mono con glide (manifiesto).\n<group> ampeg_release=0.12 amp_veltrack=40\n";
    const int keys[3] = { 24, 31, 38 };
    const int lo[3] = { 0, 28, 35 }, hi[3] = { 27, 34, 127 };
    for (int k = 0; k < 3; ++k)
    {
        const double hz = 440.0 * std::pow (2.0, (keys[k] - 69) / 12.0);
        for (int v = 0; v < 2; ++v)
        {
            Buf b = render808 (hz, v ? 0.75 : 0.35, v ? 0.12 : 0.04, 1500 + 2 * k + v);
            gain (b, 0.92f / peakOf (b));
            const std::string rel = "samples/808/808_k" + std::to_string (keys[k]) + "_v" + std::to_string (v + 1) + ".wav";
            w.writeBuf (rel, b, orig (v ? "808: seno con golpe de tono +7 st, saturación fuerte" : "808: seno con golpe de tono +7 st, saturación suave"));
            s808 << "<region> lokey=" << lo[k] << " hikey=" << hi[k] << " pitch_keycenter=" << keys[k] << " lovel=" << (v ? 91 : 1) << " hivel=" << (v ? 127 : 90)
                 << " sample=../" << rel << "\n";
        }
    }
    w.text ("instruments/808-lab.sfz", s808.str());

    ssub << "// Cauce Deep Sub — seno con 2.º y 3.er armónico opcionales (audibilidad en móviles), bucle exacto.\n"
            "<group> loop_mode=loop_continuous ampeg_attack=0.004 ampeg_release=0.15 amp_veltrack=50\n";
    const int skeys[3] = { 24, 36, 48 };
    const int slo[3] = { 0, 31, 43 }, shi[3] = { 30, 42, 127 };
    for (int k = 0; k < 3; ++k)
    {
        const double hz = 440.0 * std::pow (2.0, (skeys[k] - 69) / 12.0);
        Buf b = renderSub (hz, (int) std::ceil (hz * 0.5), 0.18, 0.08);
        const std::string rel = "samples/sub/sub_k" + std::to_string (skeys[k]) + ".wav";
        w.writeBuf (rel, b, orig ("sub: seno + 18 % 2.º armónico + 8 % 3.º, bucle de ciclos enteros"));
        ssub << "<region> lokey=" << slo[k] << " hikey=" << shi[k] << " pitch_keycenter=" << skeys[k] << " loop_start=0 loop_end=" << (b.size() - 1) << " sample=../" << rel << "\n";
    }
    w.text ("instruments/deep-sub.sfz", ssub.str());

    PackManifest m;
    m.id = "cauce.bass.lab";
    m.version = "1.0.0";
    m.name = "Cauce Bass Lab";
    m.author = "CAUCESOUND";
    m.description = "808 Lab (golpe de tono, glide, dos capas de saturación) y Deep Sub (bucle estable con armónicos para móviles).";
    m.license = "LicenseRef-CauceSound-Content";
    m.licenseFile = "LICENSE.txt";
    m.minEngine = "0.9.0";
    m.sources.push_back ({ "Síntesis CAUCESOUND", "CAUCESOUND", "desktop/tools/packbuilder/PackBuilder.cpp", "0.9", "LicenseRef-CauceSound-Content", kRetrieved, "Sonidos originales." });
    PackInstrument b808 { "808-lab", "Cauce 808 Lab", "bass", "melodic", "instruments/808-lab.sfz", "808 afinado con golpe de tono y glide entre notas ligadas. Tocar fuerte satura más.",
                          "original", { "808", "Trap", "Dark", "Sub" }, { "trap", "darkhiphop", "reggaeton", "drill" }, {}, 4, true, 0.07f, -3.0f, 36, 12, 60 };
    PackInstrument sub { "deep-sub", "Cauce Deep Sub", "bass", "melodic", "instruments/deep-sub.sfz", "Subgrave sostenido y estable, con armónicos suaves para oírse en el móvil.",
                         "original", { "Sub", "Deep", "Clean" }, { "trap", "rnb", "electronic", "boombap" }, {}, 4, true, 0.03f, -4.0f, 36, 12, 60 };
    m.instruments = { b808, sub };
    w.finish (m, "Cauce Bass Lab 1.0.0\n\nSonidos sintetizados por CAUCESOUND (ver PROVENANCE.md). Contenido propio; texto de licencia provisional,\n"
                 "revisión jurídica pendiente antes de publicar. Las canciones creadas con ellos son del usuario.\n",
             "# Procedencia — Cauce Bass Lab 1.0.0\n\nGenerado por `cauce_packs`. Todo es síntesis original.\n");
}

// ==================================================================================================================
// Cauce Grand Piano (VCSL «Grand Piano, Kawai», CC0): paquete OPCIONAL (no va dentro del APK)
// ==================================================================================================================
int vcslPianoKey (const std::string& note) // «C3» de VCSL = MIDI 60 (octava desplazada: A#-1 = 22). Comprobado midiendo.
{
    static const std::map<std::string, int> base = { { "C", 0 }, { "C#", 1 }, { "D", 2 }, { "D#", 3 }, { "E", 4 }, { "F", 5 }, { "F#", 6 }, { "G", 7 }, { "G#", 8 }, { "A", 9 }, { "A#", 10 }, { "B", 11 } };
    size_t i = 1;
    if (i < note.size() && note[i] == '#') ++i;
    const int oct = std::stoi (note.substr (i));
    return (oct + 2) * 12 + base.at (note.substr (0, i));
}

void buildPiano (const std::string& out)
{
    const std::string folder = "Chordophones/Zithers/Grand Piano, Kawai/Sustains/";
    if (! fs::exists (gVcsl + "/" + folder)) { std::printf ("(sin muestras del piano: se omite Cauce Grand Piano)\n"); return; }
    struct S { int key, vel; std::string file; };
    std::vector<S> found;
    for (const auto& e : fs::directory_iterator (gVcsl + "/" + folder))
    {
        const std::string n = e.path().filename().string(); // GPiano_sus_C3_v3_rr1_Player.wav
        if (n.rfind ("GPiano_sus_", 0) != 0) continue;
        const size_t a = 11, b = n.find ("_v", a);
        const int vel = n[b + 2] - '0';
        if (vel != 1 && vel != 3 && vel != 4) continue;
        found.push_back ({ vcslPianoKey (n.substr (a, b - a)), vel, n });
    }
    if (found.empty()) { std::printf ("(sin muestras del piano)\n"); return; }
    PackWriter w (out + "/cauce.keys.grand");
    std::ostringstream sfz;
    sfz << "// Cauce Grand Piano — muestras VCSL «Grand Piano, Kawai» (CC0), 3 capas, recortadas. Ver PROVENANCE.md\n"
           "<group> ampeg_release=0.45 amp_veltrack=55\n";
    const int vels[3] = { 1, 3, 4 };
    const Layer layers[3] = { { 1, 54 }, { 55, 96 }, { 97, 127 } };
    for (int li = 0; li < 3; ++li)
    {
        std::vector<S> layer;
        for (const auto& s : found) if (s.vel == vels[li]) layer.push_back (s);
        std::sort (layer.begin(), layer.end(), [] (const S& x, const S& y) { return x.key < y.key; });
        for (size_t i = 0; i < layer.size(); ++i)
        {
            const int k = layer[i].key;
            const int lo = i == 0 ? 21 : (layer[i - 1].key + k) / 2 + 1;
            const int hi = i + 1 == layer.size() ? 108 : (k + layer[i + 1].key) / 2;
            AudioData a;
            std::string err;
            if (! readWav (gVcsl + "/" + folder + layer[i].file, a, err)) die (err);
            if (a.sampleRate != kRate) a = resample (a, kRate);
            const double maxSec = k < 48 ? 3.0 : k < 72 ? 2.2 : 1.4;
            // Recorte común a los dos canales: inicio por el umbral del canal izquierdo y fundido final de 0,8 s.
            float p = 0;
            for (const auto& ch : a.channels) for (float v : ch) p = std::max (p, std::fabs (v));
            size_t start = 0;
            while (start < a.channels[0].size() && std::fabs (a.channels[0][start]) < p * 0.003f && std::fabs (a.channels.back()[start]) < p * 0.003f) ++start;
            start = start > 48 ? start - 48 : 0;
            const size_t end = std::min (a.channels[0].size(), start + (size_t) (maxSec * sr));
            AudioData o;
            o.sampleRate = kRate;
            for (auto& ch : a.channels)
            {
                Buf c (ch.begin() + (long) start, ch.begin() + (long) end);
                fadeOut (c, std::min (0.6, c.size() / sr * 0.4));
                // Nivel: se conserva el natural de cada capa; solo una ganancia global fija (-1 dB sobre el original).
                gain (c, 0.89f);
                o.channels.push_back (c);
            }
            if (o.channels.size() > 2) o.channels.resize (2);
            const std::string rel = "samples/grand/p_k" + std::to_string (k) + "_v" + std::to_string (li + 1) + ".wav";
            w.writeStereo (rel, o, vcsl (folder + layer[i].file, "remuestreo a 48 kHz, recorte a " + std::to_string ((int) (maxSec * 10) / 10.0).substr (0, 3) + " s con fundido de 0,6 s, -1 dB"));
            sfz << "<region> lokey=" << lo << " hikey=" << hi << " pitch_keycenter=" << k << " lovel=" << layers[li].lo << " hivel=" << layers[li].hi << " sample=../" << rel << "\n";
        }
    }
    w.text ("instruments/grand-piano.sfz", sfz.str());
    PackManifest m;
    m.id = "cauce.keys.grand";
    m.version = "1.0.0";
    m.name = "Cauce Grand Piano";
    m.author = "CAUCESOUND (mapa y edición) / Versilian Studios (grabación)";
    m.description = "Piano de cola real (Kawai, VCSL CC0) con tres capas de fuerza, muestreado cada tercera menor. Versión recortada para móvil.";
    m.license = "CC0-1.0";
    m.licenseFile = "LICENSE.txt";
    m.minEngine = "0.9.0";
    m.sources.push_back ({ "Versilian Community Sample Library (VCSL) — Grand Piano, Kawai", "Versilian Studios LLC", "https://github.com/sgossner/VCSL", kVcslCommit, "CC0-1.0", kRetrieved,
                           "CC0 1.0: uso comercial y redistribución permitidos. Capas v1, v3 y v4 de «Sustains»; sin muestras de release ni de pedal." });
    PackInstrument gp { "grand-piano", "Cauce Grand Piano", "keys", "melodic", "instruments/grand-piano.sfz",
                        "Piano acústico real con tres capas. Sustain con el pedal (CC64). Notas recortadas a 1,4-3 s para ahorrar memoria (las notas muy largas se apagan antes que en un piano real).", "cc0",
                        { "Acoustic", "Piano", "Warm", "Jazz Rap", "R&B" }, { "jazzrap", "rnb", "pop", "boombap", "ballad" }, {}, 32, false, 0.0f, -2.0f, 60, 21, 108 };
    m.instruments = { gp };
    w.finish (m, "Cauce Grand Piano 1.0.0\n\nMuestras de VCSL (Versilian Studios LLC), CC0 1.0 Universal (dominio público).\n"
                 "https://creativecommons.org/publicdomain/zero/1.0/  —  https://github.com/sgossner/VCSL\n"
                 "El mapa SFZ y la edición (recortes, fundidos) son de CAUCESOUND y se publican también bajo CC0.\n",
             "# Procedencia — Cauce Grand Piano 1.0.0\n\nVCSL en el commit " + std::string (kVcslCommit) + " (consultado " + kRetrieved + ").\n");
}
// ==================================================================================================================
// Cauce Electric Bass (Karoryfer «Black And Blue Basses», bajo «darkblack», CC0)
// ==================================================================================================================
const char* kBbbCommit = "6e7d674cdb41be7a54dbccb15472401ad01099b9";
int karoryferKey (const std::string& note) // «e2» de Karoryfer = MIDI 28 (E de la 4.ª cuerda). Comprobado midiendo la altura.
{
    static const std::map<char, int> base = { { 'c', 0 }, { 'd', 2 }, { 'e', 4 }, { 'f', 5 }, { 'g', 7 }, { 'a', 9 }, { 'b', 11 } };
    int pc = base.at (note[0]);
    size_t i = 1;
    if (note[i] == 'b') { --pc; ++i; }
    return std::stoi (note.substr (i)) * 12 + pc;
}

void buildElectricBass (const std::string& out)
{
    const std::string folder = "Samples/darkblack/reg/";
    if (gBbb.empty() || ! fs::exists (gBbb + "/" + folder)) { std::printf ("(sin Black And Blue Basses: se omite Cauce Electric Bass)\n"); return; }
    static const char* names[12] = { "c", "db", "d", "eb", "e", "f", "gb", "g", "ab", "a", "bb", "b" };
    const char* dyn[3] = { "p", "mf", "f" };
    const Layer layers[3] = { { 1, 58 }, { 59, 100 }, { 101, 127 } };
    std::vector<int> keys;
    for (int k = 23; k <= 62; k += 3) keys.push_back (k); // cada tercera menor, de B0 a D4
    auto fileFor = [&] (int k, int d) { return std::string ("darkblack_") + names[k % 12] + std::to_string (k / 12) + "_" + dyn[d] + "_rr1.wav"; };
    // Primera pasada: pico global (se conserva la diferencia natural de nivel entre capas y notas).
    float gpk = 0;
    std::map<std::string, AudioData> cache;
    for (int k : keys) for (int d = 0; d < 3; ++d)
    {
        AudioData a;
        std::string err;
        const std::string f = fileFor (k, d);
        if (karoryferKey (f.substr (10, f.find ('_', 10) - 10)) != k) die ("mapa de notas incoherente: " + f);
        if (! readWav (gBbb + "/" + folder + f, a, err)) die ("no se pudo leer " + f + ": " + err);
        if (a.sampleRate != kRate) a = resample (a, kRate);
        for (const auto& ch : a.channels) for (float v : ch) gpk = std::max (gpk, std::fabs (v));
        cache[f] = std::move (a);
    }
    const float g = 0.9f / gpk;
    PackWriter w (out + "/cauce.bass.electric");
    std::ostringstream sfz;
    sfz << "// Cauce Electric Bass — Karoryfer «Black And Blue Basses» (CC0), bajo darkblack, dedos, 3 capas. Ver PROVENANCE.md\n"
           "<group> ampeg_release=0.09 amp_veltrack=45\n";
    for (int d = 0; d < 3; ++d)
        for (size_t i = 0; i < keys.size(); ++i)
        {
            const int k = keys[i];
            const int lo = i == 0 ? 12 : k - 1, hi = i + 1 == keys.size() ? 72 : k + 1;
            const std::string f = fileFor (k, d);
            const AudioData& a = cache[f];
            Buf m (a.channels[0].size(), 0.0f);
            for (const auto& ch : a.channels) for (size_t j = 0; j < m.size(); ++j) m[j] += ch[j] / (float) a.numChannels();
            float p = 0;
            for (float v : m) p = std::max (p, std::fabs (v));
            size_t start = 0;
            while (start < m.size() && std::fabs (m[start]) < p * 0.004f) ++start;
            start = start > 24 ? start - 24 : 0;
            const double maxSec = k < 35 ? 2.6 : k < 47 ? 2.2 : 1.8;
            Buf c (m.begin() + (long) start, m.begin() + (long) std::min (m.size(), start + (size_t) (maxSec * sr)));
            fadeOut (c, 0.5);
            gain (c, g);
            const std::string rel = "samples/ebass/eb_k" + std::to_string (k) + "_" + dyn[d] + ".wav";
            char proc[160];
            std::snprintf (proc, sizeof proc, "remuestreo a 48 kHz, mono, recorte a %.1f s con fundido de 0,5 s, ganancia global %.2f dB", maxSec, 20.0 * std::log10 (g));
            w.writeBuf (rel, c, std::string ("Karoryfer Black And Blue Basses (CC0) `") + folder + f + "` → " + proc);
            sfz << "<region> lokey=" << lo << " hikey=" << hi << " pitch_keycenter=" << k << " lovel=" << layers[d].lo << " hivel=" << layers[d].hi << " sample=../" << rel << "\n";
        }
    w.text ("instruments/electric-bass.sfz", sfz.str());
    PackManifest m;
    m.id = "cauce.bass.electric";
    m.version = "1.0.0";
    m.name = "Cauce Electric Bass";
    m.author = "CAUCESOUND (mapa y edición) / Karoryfer Samples (grabación)";
    m.description = "Bajo eléctrico real tocado con dedos (Karoryfer, CC0), tres capas de fuerza, muestreado cada tercera menor.";
    m.license = "CC0-1.0";
    m.licenseFile = "LICENSE.txt";
    m.minEngine = "0.9.0";
    m.sources.push_back ({ "Karoryfer Black And Blue Basses (bajo «darkblack», articulación reg)", "Karoryfer Samples", "https://github.com/sfzinstruments/karoryfer.black-and-blue-basses",
                           kBbbCommit, "CC0-1.0", kRetrieved, "Archivo «license» del repositorio: CC0 1.0 Universal. Se usa solo la toma rr1 de las dinámicas p, mf y f." });
    PackInstrument eb { "electric-bass", "Cauce Electric Bass", "bass", "melodic", "instruments/electric-bass.sfz",
                        "Bajo eléctrico real (dedos) con tres capas de fuerza. Monofónico como un bajo de verdad.", "cc0",
                        { "Electric", "Fingered", "Warm", "Real" }, { "boombap", "jazzrap", "rnb", "funk", "pop", "latin" }, {}, 2, true, 0.0f, -2.0f, 36, 12, 72 };
    m.instruments = { eb };
    w.finish (m, "Cauce Electric Bass 1.0.0\n\nMuestras de Karoryfer Samples, «Black And Blue Basses», CC0 1.0 Universal (dominio público).\n"
                 "https://creativecommons.org/publicdomain/zero/1.0/  —  https://github.com/sfzinstruments/karoryfer.black-and-blue-basses\n"
                 "El mapa SFZ y la edición (recortes, fundidos) son de CAUCESOUND y se publican también bajo CC0.\n",
             "# Procedencia — Cauce Electric Bass 1.0.0\n\nBlack And Blue Basses en el commit " + std::string (kBbbCommit) + " (consultado " + kRetrieved + ").\n");
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3) { std::fprintf (stderr, "uso: cauce_packs <carpeta VCSL> <salida> [solo: drums|bass|piano|ebass] [carpeta Black And Blue Basses]\n"); return 2; }
    gVcsl = argv[1];
    const std::string out = argv[2];
    const std::string only = argc > 3 ? argv[3] : "";
    gBbb = argc > 4 ? argv[4] : "";
    fs::create_directories (out);
    if (only.empty() || only == "drums") buildDrums (out);
    if (only.empty() || only == "bass") buildBass (out);
    if (only.empty() || only == "piano") buildPiano (out);
    if (only.empty() || only == "ebass") buildElectricBass (out);
    return 0;
}
