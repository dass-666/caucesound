// Cauce Sampler: mapa SFZ, carga de muestras y reproducción en tiempo real. Ver cs/Sampler.h y docs/CAUCE_SAMPLER.md.
#include "cs/Sampler.h"
#include "cs/Resampler.h"
#include "cs/Wav.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <sstream>

namespace cs
{
size_t SamplerProgram::bytes() const
{
    std::set<const SampleBuffer*> seen;
    size_t total = 0;
    for (const auto& r : regions)
        if (r.sample && seen.insert (r.sample.get()).second) total += r.sample->bytes();
    return total;
}

// --- SFZ -------------------------------------------------------------------------------------------------------------
namespace
{
std::string lower (std::string s) { for (char& c : s) c = (char) std::tolower ((unsigned char) c); return s; }

// Nota SFZ: número 0..127 o nombre (c4 = 60, c#4 / db4, octavas -1..9). -1 si no es válida.
int parseKey (const std::string& v)
{
    if (v.empty()) return -1;
    if (std::isdigit ((unsigned char) v[0]) || v[0] == '-')
    {
        char* endp = nullptr;
        const long n = std::strtol (v.c_str(), &endp, 10);
        if (endp == v.c_str() || *endp != 0 || n < 0 || n > 127) return -1;
        return (int) n;
    }
    static const int base[7] = { 9, 11, 0, 2, 4, 5, 7 }; // a b c d e f g
    const char letter = (char) std::tolower ((unsigned char) v[0]);
    if (letter < 'a' || letter > 'g') return -1;
    int semis = base[letter - 'a'];
    size_t i = 1;
    if (i < v.size() && v[i] == '#') { ++semis; ++i; }
    else if (i < v.size() && (v[i] == 'b' || v[i] == 'B') && i + 1 < v.size() && (std::isdigit ((unsigned char) v[i + 1]) || v[i + 1] == '-')) { --semis; ++i; }
    if (i >= v.size()) return -1;
    char* endp = nullptr;
    const long oct = std::strtol (v.c_str() + i, &endp, 10);
    if (*endp != 0 || oct < -1 || oct > 9) return -1;
    const int n = (int) ((oct + 1) * 12 + semis);
    return n >= 0 && n <= 127 ? n : -1;
}

bool parseNumber (const std::string& v, double& out)
{
    char* endp = nullptr;
    out = std::strtod (v.c_str(), &endp);
    return endp != v.c_str() && *endp == 0 && std::isfinite (out);
}

using Opcodes = std::map<std::string, std::string>;

std::string stripComments (const std::string& text)
{
    std::string out;
    out.reserve (text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/') { while (i < text.size() && text[i] != '\n') ++i; out += '\n'; continue; }
        if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '*')
        {
            i += 2;
            while (i + 1 < text.size() && ! (text[i] == '*' && text[i + 1] == '/')) { if (text[i] == '\n') out += '\n'; ++i; }
            ++i;
            continue;
        }
        out += text[i];
    }
    return out;
}

bool applyOpcode (SamplerRegion& r, const std::string& key, const std::string& val, std::vector<std::string>& warnings)
{
    double d = 0;
    auto num = [&] (double lo, double hi, double& dst) -> bool
    {
        if (! parseNumber (val, d)) { warnings.push_back ("valor no numérico: " + key + "=" + val); return false; }
        if (d < lo || d > hi) { warnings.push_back ("valor recortado: " + key + "=" + val); d = std::clamp (d, lo, hi); }
        dst = d;
        return true;
    };
    double x = 0;
    if (key == "sample") { std::string p = val; std::replace (p.begin(), p.end(), '\\', '/'); r.samplePath = p; return true; }
    if (key == "lokey" || key == "hikey" || key == "key" || key == "pitch_keycenter")
    {
        const int k = parseKey (lower (val));
        if (k < 0) { warnings.push_back ("nota no válida: " + key + "=" + val); return true; }
        if (key == "lokey") r.loKey = k;
        else if (key == "hikey") r.hiKey = k;
        else if (key == "key") { r.loKey = r.hiKey = r.keyCenter = k; }
        else r.keyCenter = k;
        return true;
    }
    if (key == "lovel") { if (num (1, 127, x)) r.loVel = (int) x; return true; }
    if (key == "hivel") { if (num (1, 127, x)) r.hiVel = (int) x; return true; }
    if (key == "lorand") { if (num (0, 1, x)) r.loRand = (float) x; return true; }
    if (key == "hirand") { if (num (0, 1, x)) r.hiRand = (float) x; return true; }
    if (key == "seq_length") { if (num (1, 100, x)) r.seqLength = (int) x; return true; }
    if (key == "seq_position") { if (num (1, 100, x)) r.seqPosition = (int) x; return true; }
    if (key == "tune") { if (num (-2400, 2400, x)) r.tuneCents += (float) x; return true; }
    if (key == "transpose") { if (num (-127, 127, x)) r.tuneCents += (float) (x * 100.0); return true; }
    if (key == "pitch_keytrack") { if (num (-1200, 1200, x)) r.pitchKeytrack = (float) x; return true; }
    if (key == "volume") { if (num (-144, 24, x)) r.gainDb = (float) x; return true; }
    if (key == "pan") { if (num (-100, 100, x)) r.pan = (float) x; return true; }
    if (key == "amp_veltrack") { if (num (-100, 100, x)) r.ampVeltrack = (float) x; return true; }
    if (key == "ampeg_attack") { if (num (0, 100, x)) r.attack = (float) x; return true; }
    if (key == "ampeg_hold") { if (num (0, 100, x)) r.hold = (float) x; return true; }
    if (key == "ampeg_decay") { if (num (0, 100, x)) r.decay = (float) x; return true; }
    if (key == "ampeg_sustain") { if (num (0, 100, x)) r.sustain = (float) x; return true; }
    if (key == "ampeg_release") { if (num (0, 100, x)) r.release = (float) x; return true; }
    if (key == "loop_mode" || key == "loopmode")
    {
        const std::string v = lower (val);
        if (v == "no_loop") r.loop = SamplerRegion::Loop::NoLoop;
        else if (v == "one_shot") r.loop = SamplerRegion::Loop::OneShot;
        else if (v == "loop_continuous") r.loop = SamplerRegion::Loop::Continuous;
        else if (v == "loop_sustain") r.loop = SamplerRegion::Loop::Sustain;
        else warnings.push_back ("loop_mode desconocido: " + val);
        return true;
    }
    if (key == "loop_start" || key == "loopstart") { if (num (0, 1e10, x)) r.loopStart = (Frame) x; return true; }
    if (key == "loop_end" || key == "loopend") { if (num (0, 1e10, x)) r.loopEnd = (Frame) x; return true; }
    if (key == "offset") { if (num (0, 1e10, x)) r.offset = (Frame) x; return true; }
    if (key == "end") { if (num (-1, 1e10, x)) r.end = (Frame) x; return true; }
    if (key == "group") { if (num (-2147483647.0, 2147483647.0, x)) r.group = (int) x; return true; }
    if (key == "off_by") { if (num (-2147483647.0, 2147483647.0, x)) r.offBy = (int) x; return true; }
    if (key == "trigger")
    {
        const std::string v = lower (val);
        if (v == "attack") r.releaseTrigger = false;
        else if (v == "release") r.releaseTrigger = true;
        else warnings.push_back ("trigger no admitido: " + val + " (solo attack y release)");
        return true;
    }
    return false;
}
} // namespace

bool parseSfz (const std::string& rawText, SamplerProgram& out, std::string& error)
{
    const std::string text = stripComments (rawText);
    enum Level { None, Control, Global, Master, Group, Region };
    Level level = None;
    Opcodes control, global, master, group, region;
    std::vector<SamplerRegion> regions;
    std::vector<std::string> warnings;
    std::set<std::string> unknown;
    int headers = 0;

    auto flushRegion = [&]
    {
        if (level != Region) return;
        SamplerRegion r;
        Opcodes merged = global;
        for (const auto& [k, v] : master) merged[k] = v;
        for (const auto& [k, v] : group) merged[k] = v;
        for (const auto& [k, v] : region) merged[k] = v;
        // pitch_keycenter por defecto = 60; «key» lo fija también. Aplicar «key» antes que pitch_keycenter.
        if (auto it = merged.find ("key"); it != merged.end()) applyOpcode (r, "key", it->second, warnings);
        for (const auto& [k, v] : merged)
        {
            if (k == "key") continue;
            if (! applyOpcode (r, k, v, warnings)) unknown.insert (k);
        }
        if (r.samplePath.empty()) { warnings.push_back ("región sin sample: se ignora"); return; }
        std::string dp = control.count ("default_path") ? control["default_path"] : std::string();
        std::replace (dp.begin(), dp.end(), '\\', '/');
        r.samplePath = dp + r.samplePath;
        if (r.loKey > r.hiKey || r.loVel > r.hiVel) { warnings.push_back ("región vacía (rango invertido): " + r.samplePath); return; }
        regions.push_back (std::move (r));
    };

    std::istringstream in (text);
    std::string line;
    while (std::getline (in, line))
    {
        size_t i = 0;
        while (i < line.size())
        {
            while (i < line.size() && std::isspace ((unsigned char) line[i])) ++i;
            if (i >= line.size()) break;
            if (line[i] == '<')
            {
                const size_t close = line.find ('>', i);
                if (close == std::string::npos) { error = "cabecera sin cerrar: " + line; return false; }
                const std::string h = lower (line.substr (i + 1, close - i - 1));
                flushRegion();
                ++headers;
                if (h == "control") { level = Control; control.clear(); }
                else if (h == "global") { level = Global; global.clear(); master.clear(); group.clear(); }
                else if (h == "master") { level = Master; master.clear(); group.clear(); }
                else if (h == "group") { level = Group; group.clear(); }
                else if (h == "region") { level = Region; region.clear(); }
                else { level = None; warnings.push_back ("cabecera no admitida: <" + h + ">"); }
                i = close + 1;
                continue;
            }
            if (line[i] == '#') { warnings.push_back ("directiva no admitida: " + line.substr (i)); break; }
            const size_t eq = line.find ('=', i);
            if (eq == std::string::npos) { error = "línea no válida: " + line; return false; }
            std::string key = lower (line.substr (i, eq - i));
            while (! key.empty() && std::isspace ((unsigned char) key.back())) key.pop_back();
            if (key.empty() || key.find_first_of (" \t<>") != std::string::npos) { error = "opcode no válido: " + line; return false; }
            size_t vs = eq + 1, ve = vs;
            if (key == "sample" || key == "default_path")
            {
                // El nombre de archivo puede tener espacios: llega hasta el siguiente «opcode=», cabecera o fin de línea.
                ve = line.size();
                for (size_t k = vs; k < line.size(); ++k)
                {
                    if (line[k] == '<') { ve = k; break; }
                    if (std::isspace ((unsigned char) line[k]))
                    {
                        size_t m = k;
                        while (m < line.size() && std::isspace ((unsigned char) line[m])) ++m;
                        size_t n = m;
                        while (n < line.size() && (std::isalnum ((unsigned char) line[n]) || line[n] == '_')) ++n;
                        if (n > m && n < line.size() && line[n] == '=') { ve = k; break; }
                    }
                }
            }
            else
                while (ve < line.size() && ! std::isspace ((unsigned char) line[ve]) && line[ve] != '<') ++ve;
            std::string val = line.substr (vs, ve - vs);
            while (! val.empty() && std::isspace ((unsigned char) val.back())) val.pop_back();
            switch (level)
            {
                case Control: control[key] = val; break;
                case Global: global[key] = val; break;
                case Master: master[key] = val; break;
                case Group: group[key] = val; break;
                case Region: region[key] = val; break;
                default: break;
            }
            i = ve;
        }
    }
    flushRegion();
    if (headers == 0) { error = "no es un archivo SFZ (sin cabeceras)"; return false; }
    for (const auto& [k, v] : control)
        if (k != "default_path") unknown.insert (k);
    for (const auto& k : unknown) warnings.push_back ("opcode no admitido: " + k);
    out.regions = std::move (regions);
    out.warnings.insert (out.warnings.end(), warnings.begin(), warnings.end());
    return true;
}

// --- Caché y carga ----------------------------------------------------------------------------------------------------
namespace { std::mutex gCacheMutex; }

SampleCache& sampleCache()
{
    static SampleCache c;
    return c;
}

std::shared_ptr<const SampleBuffer> SampleCache::get (const std::string& path, int rate, std::string& error)
{
    {
        std::lock_guard<std::mutex> l (gCacheMutex);
        for (auto& e : entries)
            if (e.path == path && e.rate == rate)
                if (auto b = e.buf.lock()) return b;
    }
    AudioData a;
    WavInfo info;
    if (! readWav (path, a, error, &info)) return nullptr;
    if (a.numFrames() == 0) { error = "muestra vacía"; return nullptr; }
    if (a.numFrames() > (Frame) rate * 60) { error = "muestra de más de 60 s (use streaming en el futuro)"; return nullptr; }
    if (a.sampleRate != rate) a = resample (a, rate);
    auto b = std::make_shared<SampleBuffer>();
    b->sampleRate = rate;
    b->channels = std::min (2, a.numChannels());
    b->file = path;
    auto conv = [] (const std::vector<float>& src, std::vector<int16_t>& dst)
    {
        dst.resize (src.size());
        for (size_t i = 0; i < src.size(); ++i)
        {
            const float v = std::isfinite (src[i]) ? std::clamp (src[i], -1.0f, 1.0f) : 0.0f;
            dst[i] = (int16_t) std::lround (v * 32767.0f);
        }
    };
    conv (a.channels[0], b->left);
    if (b->channels == 2) conv (a.channels[1], b->right);
    std::lock_guard<std::mutex> l (gCacheMutex);
    entries.erase (std::remove_if (entries.begin(), entries.end(), [] (const Entry& e) { return e.buf.expired(); }), entries.end());
    entries.push_back ({ path, rate, b });
    return b;
}

size_t SampleCache::residentBytes()
{
    std::lock_guard<std::mutex> l (gCacheMutex);
    size_t total = 0;
    for (auto& e : entries) if (auto b = e.buf.lock()) total += b->bytes();
    return total;
}

void SampleCache::purge()
{
    std::lock_guard<std::mutex> l (gCacheMutex);
    entries.erase (std::remove_if (entries.begin(), entries.end(), [] (const Entry& e) { return e.buf.expired(); }), entries.end());
}

namespace
{
// Ruta relativa segura: sin absolutas, sin «..», sin unidades de Windows. Un mapa nunca lee fuera de su carpeta.
bool safeRelative (const std::string& p)
{
    if (p.empty() || p[0] == '/' || p.find (':') != std::string::npos) return false;
    size_t s = 0;
    while (s <= p.size())
    {
        size_t e = p.find ('/', s);
        if (e == std::string::npos) e = p.size();
        const std::string part = p.substr (s, e - s);
        if (part == ".." ) return false;
        s = e + 1;
    }
    return true;
}
} // namespace

bool loadProgramSamples (SamplerProgram& program, const std::string& baseDir, int sampleRate, size_t maxBytes, std::string& error)
{
    std::map<std::string, std::shared_ptr<const SampleBuffer>> loaded;
    std::vector<std::string> missing;
    size_t total = 0;
    for (auto& r : program.regions)
    {
        if (loaded.count (r.samplePath)) continue;
        if (! safeRelative (r.samplePath)) { error = "ruta de muestra no permitida: " + r.samplePath; return false; }
        std::string err;
        auto b = sampleCache().get (baseDir + "/" + r.samplePath, sampleRate, err);
        if (! b) missing.push_back (r.samplePath + " (" + err + ")");
        else total += b->bytes();
        loaded[r.samplePath] = b;
        if (total > maxBytes)
        {
            error = "el instrumento necesita más memoria que el límite (" + std::to_string (maxBytes / (1024 * 1024)) + " MB)";
            return false; // los búferes locales se liberan aquí: nada queda cargado a medias
        }
    }
    for (auto& r : program.regions) r.sample = loaded[r.samplePath];
    program.missingSamples = std::move (missing);
    return true;
}

// --- Motor -----------------------------------------------------------------------------------------------------------
void SamplerEngine::prepare (std::shared_ptr<const SamplerProgram> program, double sampleRate)
{
    prog = std::move (program);
    sr = sampleRate;
    programGain = prog ? std::pow (10.0f, prog->volumeDb / 20.0f) : 1.0f;
    reset();
}

void SamplerEngine::reset()
{
    for (auto& v : voices) v = Voice();
    seqCounter.fill (0);
    noteVel.fill (0);
    pedal = false;
    counter = 0;
    rng = 0x2545F491u;
    stolen = 0;
}

float SamplerEngine::nextRandom()
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float) ((rng >> 8) * (1.0 / 16777216.0)); // [0, 1)
}

int SamplerEngine::logicalVoices() const
{
    int n = 0;
    for (const auto& v : voices) n += v.active && ! v.killing;
    return n;
}

int SamplerEngine::activeVoices() const
{
    int n = 0;
    for (const auto& v : voices) n += v.active;
    return n;
}

int SamplerEngine::tailSamples() const
{
    if (! prog) return 0;
    double m = 0;
    for (const auto& r : prog->regions)
    {
        double t = r.release;
        if (r.sample && (r.loop == SamplerRegion::Loop::OneShot || r.releaseTrigger)) t = std::max (t, 2.0 * (double) r.sample->frames() / sr);
        m = std::max (m, t);
    }
    return (int) (std::min (m, 20.0) * sr) + (int) (0.01 * sr);
}

void SamplerEngine::kill (Voice& v)
{
    if (! v.active || v.killing) return;
    v.killing = true;
    v.kill = 1.0f;
    v.killStep = 1.0f / (float) std::max (1.0, 0.005 * sr); // 5 ms: sin clic
}

void SamplerEngine::startRelease (Voice& v)
{
    if (! v.active || v.releasing) return;
    v.releasing = true;
    v.sustained = false;
    v.stage = 4;
}

SamplerEngine::Voice* SamplerEngine::freeVoice()
{
    for (auto& v : voices) if (! v.active) return &v;
    // Todas ocupadas (también por voces que se desvanecen): se reutiliza la más antigua que ya se está apagando.
    Voice* best = nullptr;
    for (auto& v : voices) if (v.killing && (! best || v.order < best->order)) best = &v;
    if (! best) for (auto& v : voices) if (! best || v.order < best->order) best = &v;
    return best;
}

void SamplerEngine::startVoice (const SamplerRegion& r, int key, int vel, int tag, double rateMul, float decayScale)
{
    if (! r.sample) return;
    const int poly = prog ? std::clamp (prog->polyphony, 1, kSamplerMaxVoices / 2) : 16;
    // Límite de polifonía: se roba primero la voz en release más antigua y si no, la más antigua. Las robadas se
    // desvanecen en 5 ms en una ranura aparte (hay el doble de ranuras que de polifonía).
    while (logicalVoices() >= poly)
    {
        Voice* victim = nullptr;
        for (auto& v : voices) if (v.active && ! v.killing && v.releasing && (! victim || v.order < victim->order)) victim = &v;
        if (! victim) for (auto& v : voices) if (v.active && ! v.killing && (! victim || v.order < victim->order)) victim = &v;
        if (! victim) break;
        kill (*victim);
        ++stolen;
    }
    Voice* slot = freeVoice();
    if (! slot) return;
    Voice v;
    v.active = true;
    v.key = key; v.vel = vel; v.tag = tag;
    v.region = &r;
    v.buf = r.sample.get();
    const double semis = (key - r.keyCenter) * (double) r.pitchKeytrack / 100.0 + r.tuneCents / 100.0;
    v.rate = v.targetRate = std::pow (2.0, semis / 12.0) * rateMul * ((double) v.buf->sampleRate / sr);
    v.pos = (double) std::min<Frame> (r.offset, std::max<Frame> (0, v.buf->frames() - 1));
    v.endFrame = r.end >= 0 ? std::min<Frame> (r.end + 1, v.buf->frames()) : v.buf->frames();
    // Fuerza: 1 - veltrack * (1 - (v/127)^2). Con capas por velocity, el carácter lo da la capa y esto solo afina.
    const double vn = std::clamp (vel, 1, 127) / 127.0;
    const double velGain = 1.0 - std::clamp ((double) r.ampVeltrack, 0.0, 100.0) / 100.0 * velSensitivity * (1.0 - vn * vn);
    const double g = velGain * std::pow (10.0, r.gainDb / 20.0) * programGain;
    const float p = std::clamp (r.pan / 100.0f, -1.0f, 1.0f);
    v.gainL = (float) (g * (p > 0 ? 1.0f - p : 1.0f));
    v.gainR = (float) (g * (p < 0 ? 1.0f + p : 1.0f));
    // Envolvente: ataque mínimo de 0,2 ms (sin clic aunque la muestra empiece fuera de cero).
    v.attackStep = 1.0 / std::max (1.0, std::max ((double) r.attack, 0.0002) * sr);
    v.holdLeft = r.hold * sr;
    v.sustainLevel = std::clamp (r.sustain / 100.0, 0.0, 1.0);
    v.decK = r.decay > 0 ? 1.0 - std::exp (-5.0 / (r.decay * sr)) : 1.0;
    // Release: llega a -60 dB en 'release' segundos (mínimo 5 ms para no cortar en seco).
    v.relK = 1.0 - std::exp (std::log (1e-3) / (std::max ((double) r.release * relMul, 0.005) * sr));
    v.stage = 0;
    v.env = 0;
    if (decayScale < 0.999f)
    {
        const double len = (double) (v.endFrame - (Frame) v.pos);
        v.fadeFrom = v.pos + len * decayScale * 0.35;
        v.fadeK = 1.0 / std::max (1.0, len * decayScale * 0.12);
    }
    v.order = ++counter;
    *slot = v;
}

void SamplerEngine::noteOn (int key, int velocity, int tag, double rateMul, float decayScale)
{
    if (! prog || key < 0 || key > 127) return;
    if (velocity <= 0) { noteOff (key); return; }
    velocity = std::min (velocity, 127);
    noteVel[(size_t) key] = (uint8_t) velocity;
    const float rnd = nextRandom();
    const uint32_t seq = seqCounter[(size_t) key]++;

    if (prog->mono)
    {
        // Legato con glide: la voz que suena cambia de nota sin volver a atacar (808 deslizado).
        if (prog->glideSeconds > 0.0f)
            for (auto& v : voices)
                if (v.active && ! v.releasing && ! v.killing)
                {
                    const SamplerRegion& r = *v.region;
                    if (key < r.loKey || key > r.hiKey) break; // fuera de la región: nueva voz
                    const double semis = (key - r.keyCenter) * (double) r.pitchKeytrack / 100.0 + r.tuneCents / 100.0;
                    v.targetRate = std::pow (2.0, semis / 12.0) * rateMul * ((double) v.buf->sampleRate / sr);
                    v.glideK = 1.0 - std::exp (-1.0 / (prog->glideSeconds * sr / 4.0));
                    v.key = key;
                    v.order = ++counter;
                    return;
                }
        for (auto& v : voices) kill (v);
    }

    bool started = false;
    for (const auto& r : prog->regions)
    {
        if (r.releaseTrigger || key < r.loKey || key > r.hiKey || velocity < r.loVel || velocity > r.hiVel) continue;
        if (rnd < r.loRand || rnd >= r.hiRand) continue;
        if ((int) (seq % (uint32_t) std::max (1, r.seqLength)) + 1 != r.seqPosition) continue;
        // Grupos de corte (charles abierto cortado por el cerrado, nota repetida que se apaga).
        if (r.group != 0)
            for (auto& v : voices)
                if (v.active && ! v.killing && v.region && v.region->offBy == r.group) kill (v);
        startVoice (r, key, velocity, tag, rateMul, decayScale);
        started = true;
        if (prog->mono) break;
    }
    (void) started;
}

void SamplerEngine::noteOff (int key)
{
    if (! prog || key < 0 || key > 127) return;
    for (auto& v : voices)
    {
        if (! v.active || v.releasing || v.killing || v.key != key) continue;
        if (v.region->loop == SamplerRegion::Loop::OneShot || v.region->releaseTrigger) continue;
        if (pedal) { v.sustained = true; continue; }
        startRelease (v);
    }
    if (pedal) return;
    const int vel = noteVel[(size_t) key] ? noteVel[(size_t) key] : 64;
    for (const auto& r : prog->regions)
        if (r.releaseTrigger && key >= r.loKey && key <= r.hiKey && vel >= r.loVel && vel <= r.hiVel)
            startVoice (r, key, vel, 0, 1.0, 1.0f);
}

void SamplerEngine::controlChange (int cc, int value)
{
    if (cc == 64)
    {
        const bool down = value >= 64;
        if (pedal && ! down)
        {
            pedal = false;
            for (auto& v : voices) if (v.active && v.sustained) startRelease (v);
        }
        pedal = down;
    }
    else if (cc == 120) allNotesOff (true);
    else if (cc == 123) allNotesOff (false);
}

void SamplerEngine::allNotesOff (bool hard)
{
    pedal = false;
    for (auto& v : voices)
    {
        if (! v.active) continue;
        if (hard) v = Voice();
        else if (v.region && (v.region->loop == SamplerRegion::Loop::OneShot || v.region->releaseTrigger)) continue; // los golpes terminan solos
        else startRelease (v);
    }
}

void SamplerEngine::releaseTag (int tag)
{
    for (auto& v : voices) if (v.active && v.tag == tag) kill (v);
}

void SamplerEngine::process (float* L, float* R, int n, int tag, float outL, float outR)
{
    if (! prog) return;
    constexpr float kInv = 1.0f / 32768.0f;
    for (auto& v : voices)
    {
        if (! v.active || (tag >= 0 && v.tag != tag)) continue;
        const SampleBuffer& b = *v.buf;
        const int16_t* dl = b.left.data();
        const int16_t* dr = b.channels == 2 ? b.right.data() : dl;
        const SamplerRegion& r = *v.region;
        const bool looping = (r.loop == SamplerRegion::Loop::Continuous || (r.loop == SamplerRegion::Loop::Sustain && ! v.releasing))
                             && r.loopEnd > r.loopStart && r.loopEnd < v.endFrame;
        const double loopEndPos = (double) r.loopEnd + 1.0, loopLen = (double) (r.loopEnd + 1 - r.loopStart);
        const Frame last = v.endFrame;
        const float gl = v.gainL * outL, gr = v.gainR * outR;
        for (int i = 0; i < n; ++i)
        {
            // Envolvente
            switch (v.stage)
            {
                case 0: v.env += v.attackStep; if (v.env >= 1.0) { v.env = 1.0; v.stage = v.holdLeft > 0 ? 1 : 2; } break;
                case 1: if (--v.holdLeft <= 0) v.stage = 2; break;
                case 2: v.env += (v.sustainLevel - v.env) * v.decK; if (std::fabs (v.env - v.sustainLevel) < 1e-5) { v.env = v.sustainLevel; v.stage = 3; } break;
                case 3: break;
                default: v.env -= v.env * v.relK; break;
            }
            if (v.stage == 3 && v.sustainLevel <= 0.0 && r.loop != SamplerRegion::Loop::OneShot) { v.active = false; break; }
            if (v.stage == 4 && v.env < 1e-4) { v.active = false; break; }
            if (looping && v.pos >= loopEndPos) v.pos -= loopLen;
            if (v.pos >= (double) (last - 1)) { v.active = false; break; }
            const Frame i0 = (Frame) v.pos;
            const float f = (float) (v.pos - (double) i0);
            auto at = [&] (const int16_t* d, Frame k) -> float { return (k < 0 || k >= last) ? 0.0f : (float) d[k] * kInv; };
            auto hermite = [&] (const int16_t* d)
            {
                const float xm1 = at (d, i0 - 1), x0 = at (d, i0), x1 = at (d, i0 + 1), x2 = at (d, i0 + 2);
                const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                return ((c3 * f + c2) * f + c1) * f + x0;
            };
            float a = (float) v.env;
            const double remain = (double) last - v.pos;
            if (! looping && remain < 64.0) a *= (float) (remain / 64.0);     // fundido final: nunca termina en escalón
            if (v.pos > v.fadeFrom) a *= (float) std::exp (-(v.pos - v.fadeFrom) * v.fadeK);
            if (v.killing) { a *= v.kill; v.kill -= v.killStep; if (v.kill <= 0.0f) { v.active = false; break; } }
            const float sl = hermite (dl);
            const float sr2 = b.channels == 2 ? hermite (dr) : sl;
            L[i] += sl * a * gl;
            if (R && R != L) R[i] += sr2 * a * gr;
            if (v.rate != v.targetRate) { v.rate += (v.targetRate - v.rate) * v.glideK; if (std::fabs (v.rate - v.targetRate) < 1e-9) v.rate = v.targetRate; }
            v.pos += v.rate;
        }
        if (! v.active) v = Voice();
    }
}
} // namespace cs
