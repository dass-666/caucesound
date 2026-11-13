// Cauce Sampler y paquetes (misión 0.9). Todo se prueba fuera de Android con muestras sintéticas generadas aquí:
// SFZ, capas por velocity, round robin, ADSR, sustain, polifonía y robo sin clics, afinación, independencia del
// tamaño de bloque, cero reservas en el hilo de audio, muestras ausentes o dañadas, límite de memoria, importación
// segura de ZIP, integración con proyectos (formato v4 solo cuando hace falta), cambio de proyecto y concurrencia.
#include "Test.h"

#include "cs/Api.h"
#include "cs/Archive.h"
#include "cs/Instruments.h"
#include "cs/Packs.h"
#include "cs/Sampler.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <thread>

using namespace cs;
using nlohmann::json;
namespace fs = std::filesystem;

namespace
{
constexpr int kSr = 48000;

void writeSine (const std::string& path, double hz, double seconds, float amp, int rate = kSr, int channels = 1, double decayTau = 0)
{
    AudioData a;
    a.sampleRate = rate;
    a.allocate (channels, (Frame) (seconds * rate));
    for (Frame i = 0; i < a.numFrames(); ++i)
    {
        const double t = (double) i / rate;
        const double env = decayTau > 0 ? std::exp (-t / decayTau) : 1.0;
        for (int c = 0; c < channels; ++c) a.channels[(size_t) c][(size_t) i] = (float) (amp * env * std::sin (2 * 3.141592653589793 * hz * t));
    }
    std::string err;
    fs::create_directories (fs::path (path).parent_path());
    CHECK_MSG (writeWav (path, a, WavFormat::Pcm16, err), err);
}

void writeText (const std::string& path, const std::string& text)
{
    fs::create_directories (fs::path (path).parent_path());
    std::string err;
    CHECK (atomicWriteFile (path, text, err, false));
}

// Frecuencia por cruces por cero (señal casi senoidal).
double measureHz (const float* x, size_t n, int rate)
{
    int crossings = 0;
    size_t first = 0, last = 0;
    for (size_t i = 1; i < n; ++i)
        if (x[i - 1] < 0 && x[i] >= 0) { if (! crossings) first = i; last = i; ++crossings; }
    if (crossings < 2) return 0;
    return (crossings - 1) * (double) rate / (double) (last - first);
}

double rmsOf (const std::vector<float>& v, size_t from, size_t to)
{
    double s = 0;
    to = std::min (to, v.size());
    for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
    return to > from ? std::sqrt (s / (double) (to - from)) : 0.0;
}

float maxStep (const std::vector<float>& v)
{
    float m = 0;
    for (size_t i = 1; i < v.size(); ++i) m = std::max (m, std::fabs (v[i] - v[i - 1]));
    return m;
}

std::shared_ptr<SamplerProgram> programFrom (const std::string& dir, const std::string& sfz)
{
    auto p = std::make_shared<SamplerProgram>();
    std::string err;
    CHECK_MSG (parseSfz (sfz, *p, err), err);
    // Como PackLibrary: «../samples/x.wav» desde instruments/ se resuelve contra la raíz del paquete.
    std::string root = dir;
    for (auto& r : p->regions)
        if (r.samplePath.rfind ("../", 0) == 0) { r.samplePath = r.samplePath.substr (3); root = fs::path (dir).parent_path().string(); }
    CHECK_MSG (loadProgramSamples (*p, root, kSr, 64u << 20, err), err);
    return p;
}

// Renderiza n frames con el motor (bloques de 'block'), notas ya enviadas.
std::vector<float> run (SamplerEngine& e, int n, int block = 256)
{
    std::vector<float> L ((size_t) n, 0.0f), R ((size_t) n, 0.0f);
    for (int off = 0; off < n; off += block)
    {
        const int m = std::min (block, n - off);
        e.process (L.data() + off, R.data() + off, m);
    }
    return L;
}

// Paquete de prueba completo en 'root/<id>' con un kit (36-43) y un instrumento melódico.
std::string buildTestPack (const std::string& root, const std::string& id = "test.pack", const std::string& version = "1.0.0")
{
    const std::string d = root + "/" + id;
    writeSine (d + "/samples/kick_soft.wav", 60, 0.3, 0.3f, kSr, 1, 0.08);
    writeSine (d + "/samples/kick_hard.wav", 60, 0.3, 0.8f, kSr, 1, 0.08);
    for (int i = 1; i <= 3; ++i) writeSine (d + "/samples/hat_rr" + std::to_string (i) + ".wav", 3000 + 500 * i, 0.08, 0.4f, kSr, 1, 0.02);
    writeSine (d + "/samples/keys_c4.wav", 261.6256, 2.0, 0.5f, 44100, 2, 0.8); // 44,1 kHz estéreo: se convierte al cargar
    writeText (d + "/instruments/kit.sfz",
               "<group> loop_mode=one_shot pitch_keytrack=0 amp_veltrack=30\n"
               "<region> key=36 hivel=80 sample=../samples/kick_soft.wav\n"
               "<region> key=36 lovel=81 sample=../samples/kick_hard.wav\n"
               "<group> key=39 seq_length=3 loop_mode=one_shot\n"
               "<region> seq_position=1 sample=../samples/hat_rr1.wav\n"
               "<region> seq_position=2 sample=../samples/hat_rr2.wav\n"
               "<region> seq_position=3 sample=../samples/hat_rr3.wav\n");
    writeText (d + "/instruments/keys.sfz", "<group> ampeg_release=0.3\n<region> lokey=36 hikey=96 pitch_keycenter=c4 sample=../samples/keys_c4.wav\n");
    writeText (d + "/LICENSE.txt", "CC0 1.0 (prueba)\n");
    PackManifest m;
    m.id = id; m.version = version; m.name = "Test Pack"; m.license = "CC0-1.0"; m.licenseFile = "LICENSE.txt"; m.minEngine = "0.9.0";
    PackInstrument kit; kit.id = "kit"; kit.name = "Test Kit"; kit.family = "drums"; kit.kind = "drumkit"; kit.sfz = "instruments/kit.sfz"; kit.tags = { "Trap" };
    PackInstrument keys; keys.id = "keys"; keys.name = "Test Keys"; keys.family = "keys"; keys.kind = "melodic"; keys.sfz = "instruments/keys.sfz"; keys.tags = { "Warm" };
    m.instruments = { kit, keys };
    std::string err;
    CHECK_MSG (fillPackFiles (d, m, err), err);
    writeText (d + "/cauce-pack.json", packManifestToJson (m));
    return d;
}

std::string zipFolder (const std::string& folder, const std::string& out, const std::string& prefix = {})
{
    std::vector<ZipSource> entries;
    for (const auto& e : fs::recursive_directory_iterator (folder))
        if (e.is_regular_file()) entries.push_back ({ prefix + fs::relative (e.path(), folder).generic_string(), e.path().string() });
    CHECK (writeStoredZip (entries, out).ok);
    return out;
}

void setupLibrary (const std::string& base)
{
    fs::create_directories (base + "/factory");
    buildTestPack (base + "/factory");
    packLibrary().configure (base + "/factory", base + "/user");
}
} // namespace

TEST (sampler_sfz_parser_inheritance_names_and_warnings)
{
    SamplerProgram p;
    std::string err;
    const std::string sfz =
        "// comentario\n<control> default_path=wav/\n<global> ampeg_release=0.5 volume=-3\n"
        "<group> lovel=1 hivel=64 /* bloque */ tune=10\n"
        "<region> sample=Piano C4 soft.wav lokey=c4 hikey=e4 pitch_keycenter=c#4\n"
        "<region> sample=b.wav key=60 transpose=1 unknown_op=3\n"
        "<group> lovel=65\n<region> sample=c.wav lokey=db2 hikey=127 loop_mode=loop_continuous loop_start=10 loop_end=200 trigger=release\n";
    CHECK_MSG (parseSfz (sfz, p, err), err);
    CHECK (p.regions.size() == 3);
    const auto& a = p.regions[0];
    CHECK (a.samplePath == "wav/Piano C4 soft.wav"); // espacios en el nombre y default_path
    CHECK (a.loKey == 60 && a.hiKey == 64 && a.keyCenter == 61);
    CHECK (a.loVel == 1 && a.hiVel == 64 && std::fabs (a.tuneCents - 10) < 1e-6 && std::fabs (a.release - 0.5f) < 1e-6 && std::fabs (a.gainDb + 3) < 1e-6);
    CHECK (p.regions[1].loKey == 60 && p.regions[1].hiKey == 60 && std::fabs (p.regions[1].tuneCents - 110) < 1e-6);
    CHECK (p.regions[2].loVel == 65 && p.regions[2].loKey == 37 && p.regions[2].loop == SamplerRegion::Loop::Continuous && p.regions[2].releaseTrigger);
    bool warned = false;
    for (const auto& w : p.warnings) warned |= w.find ("unknown_op") != std::string::npos;
    CHECK_MSG (warned, "un opcode desconocido debe avisarse, no ignorarse en silencio");
    SamplerProgram bad;
    CHECK (! parseSfz ("esto no es sfz", bad, err));
    CHECK (! parseSfz ("<region sample=x.wav", bad, err));
}

TEST (sampler_velocity_layers_round_robin_and_pitch)
{
    const std::string dir = cstest::tempDir ("smp_layers");
    const std::string pd = buildTestPack (dir);
    auto prog = programFrom (pd + "/instruments", "<group> loop_mode=one_shot pitch_keytrack=0 amp_veltrack=0\n"
                                                  "<region> key=36 hivel=80 sample=../samples/kick_soft.wav\n<region> key=36 lovel=81 sample=../samples/kick_hard.wav\n"
                                                  "<group> key=39 seq_length=3 loop_mode=one_shot\n<region> seq_position=1 sample=../samples/hat_rr1.wav\n"
                                                  "<region> seq_position=2 sample=../samples/hat_rr2.wav\n<region> seq_position=3 sample=../samples/hat_rr3.wav\n"
                                                  "<region> lokey=48 hikey=84 pitch_keycenter=60 sample=../samples/keys_c4.wav\n");
    CHECK (prog->missingSamples.empty());
    SamplerEngine e;
    e.prepare (prog, kSr);
    // Capas: fuerza 60 -> capa suave (0,3), 110 -> capa fuerte (0,8). amp_veltrack=0: el nivel es el de la capa.
    e.noteOn (36, 60);
    const double soft = rmsOf (run (e, 2400), 0, 2400);
    e.reset();
    e.noteOn (36, 110);
    const double hard = rmsOf (run (e, 2400), 0, 2400);
    MEASURE ("capa suave rms %.3f, capa fuerte rms %.3f", soft, hard);
    CHECK (hard > soft * 2.2);
    // Round robin secuencial: tres golpes seguidos = tres muestras distintas (3500, 4000, 4500 Hz) y vuelve a empezar.
    e.reset();
    std::vector<double> hz;
    for (int i = 0; i < 4; ++i)
    {
        e.noteOn (39, 100);
        auto x = run (e, 2400);
        hz.push_back (measureHz (x.data(), 2400, kSr));
        e.allNotesOff (true);
    }
    MEASURE ("round robin: %.0f %.0f %.0f %.0f Hz", hz[0], hz[1], hz[2], hz[3]);
    CHECK (std::fabs (hz[0] - 3500) < 60 && std::fabs (hz[1] - 4000) < 60 && std::fabs (hz[2] - 4500) < 60 && std::fabs (hz[3] - 3500) < 60);
    // Afinación por nota: la muestra de C4 (44,1 kHz, convertida a 48 kHz) tocada en C5 suena a 523,25 Hz.
    e.reset();
    e.noteOn (72, 100);
    auto x = run (e, 24000);
    const double f = measureHz (x.data() + 1000, 20000, kSr);
    MEASURE ("C4 -> C5: %.2f Hz (esperado 523,25)", f);
    CHECK (std::fabs (f - 523.25) < 1.0);
    e.reset();
    e.noteOn (60, 100);
    x = run (e, 24000);
    CHECK (std::fabs (measureHz (x.data() + 1000, 20000, kSr) - 261.63) < 0.6);
}

TEST (sampler_random_round_robin_is_deterministic_and_balanced)
{
    const std::string dir = cstest::tempDir ("smp_rand");
    const std::string pd = buildTestPack (dir);
    auto prog = programFrom (pd + "/instruments", "<group> key=40 loop_mode=one_shot\n<region> hirand=0.5 sample=../samples/hat_rr1.wav\n"
                                                  "<region> lorand=0.5 sample=../samples/hat_rr3.wav\n");
    auto pattern = [&]
    {
        SamplerEngine e;
        e.prepare (prog, kSr);
        std::string s;
        for (int i = 0; i < 200; ++i)
        {
            e.noteOn (40, 100);
            auto x = run (e, 960);
            s += measureHz (x.data(), 960, kSr) < 4000 ? 'a' : 'b';
            e.allNotesOff (true);
        }
        return s;
    };
    const std::string a = pattern(), b = pattern();
    CHECK (a == b); // reproducible: misma exportación cada vez
    const long na = std::count (a.begin(), a.end(), 'a');
    MEASURE ("aleatorio: %ld de 200 en la primera muestra", na);
    CHECK (na > 70 && na < 130);
}

TEST (sampler_note_off_release_sustain_pedal_and_one_shot)
{
    const std::string dir = cstest::tempDir ("smp_release");
    const std::string pd = buildTestPack (dir);
    auto prog = programFrom (pd + "/instruments", "<region> lokey=0 hikey=127 pitch_keycenter=60 ampeg_release=0.1 sample=../samples/keys_c4.wav\n"
                                                  "<region> key=36 loop_mode=one_shot sample=../samples/kick_hard.wav\n");
    SamplerEngine e;
    e.prepare (prog, kSr);
    e.noteOn (60, 100);
    run (e, 4800);
    e.noteOff (60);
    auto x = run (e, 9600); // 200 ms: el release de 100 ms ya llegó a -60 dB
    CHECK (rmsOf (x, 0, 480) > 0.05);
    CHECK (rmsOf (x, 7200, 9600) < 1e-3);
    CHECK (e.activeVoices() == 0);
    CHECK (maxStep (x) < 0.08); // sin escalón al soltar
    // Pedal: Note Off con el pedal pisado no corta; al levantarlo, sí.
    e.reset();
    e.controlChange (64, 127);
    e.noteOn (60, 100);
    run (e, 2400);
    e.noteOff (60);
    x = run (e, 9600);
    CHECK (rmsOf (x, 7200, 9600) > 0.05);
    e.controlChange (64, 0);
    x = run (e, 9600);
    CHECK (rmsOf (x, 7200, 9600) < 1e-3);
    // One-shot: Note Off inmediato no corta el golpe.
    e.reset();
    e.noteOn (36, 127);
    e.noteOff (36);
    x = run (e, 4800);
    CHECK (rmsOf (x, 2400, 4800) > 0.02);
}

TEST (sampler_polyphony_limit_steals_without_clicks_or_clipping)
{
    const std::string dir = cstest::tempDir ("smp_poly");
    const std::string pd = buildTestPack (dir);
    auto prog = programFrom (pd + "/instruments", "<region> lokey=0 hikey=127 pitch_keycenter=60 ampeg_release=0.4 volume=-12 sample=../samples/keys_c4.wav\n");
    prog->polyphony = 8;
    SamplerEngine e;
    e.prepare (prog, kSr);
    std::vector<float> all;
    for (int k = 0; k < 40; ++k)
    {
        e.noteOn (40 + k, 90);
        auto x = run (e, 240);
        all.insert (all.end(), x.begin(), x.end());
        int logical = 0;
        CHECK (e.activeVoices() <= kSamplerMaxVoices);
        (void) logical;
    }
    auto tail = run (e, 4800);
    all.insert (all.end(), tail.begin(), tail.end());
    float peak = 0;
    bool finite = true;
    for (float v : all) { peak = std::max (peak, std::fabs (v)); finite &= std::isfinite (v); }
    MEASURE ("40 notas con polifonía 8: robadas %d, pico %.3f, salto máx. %.4f", e.stolenVoices(), peak, maxStep (all));
    CHECK (finite);
    CHECK (e.stolenVoices() >= 30);
    CHECK (maxStep (all) < 0.06); // el robo se desvanece en 5 ms
}

TEST (sampler_is_block_size_independent_and_allocation_free)
{
    const std::string dir = cstest::tempDir ("smp_rt");
    const std::string pd = buildTestPack (dir);
    auto prog = programFrom (pd + "/instruments", "<region> lokey=0 hikey=127 pitch_keycenter=60 ampeg_release=0.2 sample=../samples/keys_c4.wav\n"
                                                  "<group> key=39 seq_length=3 loop_mode=one_shot\n<region> seq_position=1 sample=../samples/hat_rr1.wav\n"
                                                  "<region> seq_position=2 sample=../samples/hat_rr2.wav\n<region> seq_position=3 sample=../samples/hat_rr3.wav\n");
    auto render = [&] (int block)
    {
        SamplerEngine e;
        e.prepare (prog, kSr);
        std::vector<float> L (48000, 0.0f), R (48000, 0.0f);
        int pos = 0, next = 0, i = 0;
        cstest::countAllocations = true;
        const long before = cstest::allocationCount.load();
        while (pos < 48000)
        {
            // Eventos en los mismos frames absolutos sea cual sea el bloque (el motor parte bloques en cada evento).
            const int ev = next * 3000;
            if (pos == ev && ev < 48000) { e.noteOn (i % 2 ? 39 : 55 + (i % 7), 60 + (i * 13) % 60); if (i > 2) e.noteOff (55 + ((i - 3) % 7)); ++i; ++next; }
            const int until = std::min ({ 48000, pos + block, next * 3000 > pos ? next * 3000 : 48000 });
            e.process (L.data() + pos, R.data() + pos, until - pos);
            pos = until;
        }
        const long allocs = cstest::allocationCount.load() - before;
        cstest::countAllocations = false;
        CHECK_MSG (allocs == 0, "reservas en el hilo de audio: " + std::to_string (allocs));
        return L;
    };
    const auto a = render (64), b = render (1024), c = render (7);
    float d = 0;
    for (size_t i = 0; i < a.size(); ++i) d = std::max ({ d, std::fabs (a[i] - b[i]), std::fabs (a[i] - c[i]) });
    CHECK (d == 0.0f);
}

TEST (sampler_missing_and_corrupt_samples_are_reported_not_fatal)
{
    const std::string dir = cstest::tempDir ("smp_missing");
    const std::string pd = buildTestPack (dir);
    writeText (pd + "/samples/broken.wav", "RIFF1234WAVEnope");
    auto p = std::make_shared<SamplerProgram>();
    std::string err;
    CHECK (parseSfz ("<region> key=60 sample=../samples/kick_hard.wav\n<region> key=61 sample=../samples/nothere.wav\n<region> key=62 sample=../samples/broken.wav\n", *p, err));
    for (auto& r : p->regions) r.samplePath = r.samplePath.substr (3); // relativas a la raíz del paquete
    CHECK_MSG (loadProgramSamples (*p, pd, kSr, 64u << 20, err), err);
    CHECK (p->missingSamples.size() == 2);
    SamplerEngine e;
    e.prepare (p, kSr);
    e.noteOn (61, 100); e.noteOn (62, 100); // mudas, sin fallo
    e.noteOn (60, 100);
    CHECK (rmsOf (run (e, 2400), 0, 2400) > 0.05);
    // Una ruta que sale de la carpeta se rechaza.
    auto evil = std::make_shared<SamplerProgram>();
    CHECK (parseSfz ("<region> key=60 sample=../../etc/passwd\n", *evil, err));
    CHECK (! loadProgramSamples (*evil, pd, kSr, 64u << 20, err));
    // Límite de memoria: un instrumento entra entero o no entra.
    auto big = std::make_shared<SamplerProgram>();
    CHECK (parseSfz ("<region> key=60 sample=samples/keys_c4.wav\n", *big, err));
    CHECK (! loadProgramSamples (*big, pd, kSr, 10000, err));
    CHECK (big->regions[0].sample == nullptr);
    MEASURE ("límite de memoria: %s", err.c_str());
}

TEST (sampler_pack_manifest_validation_and_library)
{
    const std::string base = cstest::tempDir ("smp_lib");
    setupLibrary (base);
    const auto packs = packLibrary().list();
    CHECK (packs.size() == 1 && packs[0].complete && packs[0].factory);
    InstalledPack ip;
    PackInstrument pi;
    CHECK (packLibrary().findInstrument ("test.pack:kit", ip, pi) && pi.kind == "drumkit");
    CHECK (! packLibrary().findInstrument ("test.pack:nope", ip, pi));
    std::vector<std::string> bad;
    std::string err;
    CHECK (packLibrary().verify ("test.pack", bad, err));
    std::string e2;
    auto prog = packLibrary().loadInstrument ("test.pack:keys", kSr, e2);
    CHECK_MSG (prog && prog->regions.size() == 1 && prog->missingSamples.empty(), e2);
    auto prog2 = packLibrary().loadInstrument ("test.pack:keys", kSr, e2);
    CHECK (prog.get() == prog2.get()); // caché: no se carga dos veces
    MEASURE ("Test Keys: %zu bytes en RAM (PCM 16 bits)", prog->bytes());
    // Manifiestos inválidos
    PackManifest m;
    CHECK (! parsePackManifest (R"({"format":"cauce-pack","formatVersion":1,"id":"../x","version":"1.0.0","name":"a","license":"CC0-1.0","licenseFile":"LICENSE.txt","files":[],"instruments":[]})", m, err));
    CHECK (! parsePackManifest (R"({"format":"cauce-pack","formatVersion":9,"id":"x","version":"1.0.0","name":"a","license":"CC0-1.0","licenseFile":"LICENSE.txt","files":[],"instruments":[]})", m, err));
    CHECK (! parsePackManifest (R"({"format":"cauce-pack","formatVersion":1,"id":"x","version":"1.0","name":"a","license":"CC0-1.0","licenseFile":"LICENSE.txt","files":[],"instruments":[]})", m, err));
    CHECK (! parsePackManifest (R"({"format":"cauce-pack","formatVersion":1,"id":"x","version":"1.0.0","name":"a","license":"CC0-1.0","licenseFile":"LICENSE.txt","files":[{"path":"../evil.wav","bytes":1,"sha256":"0000000000000000000000000000000000000000000000000000000000000000"}],"instruments":[]})", m, err));
    CHECK (compareVersions ("1.10.0", "1.9.3") > 0 && compareVersions ("1.0.0", "1.0.0") == 0);
}

TEST (sampler_pack_zip_import_is_safe_and_atomic)
{
    const std::string base = cstest::tempDir ("smp_zip");
    packLibrary().configure ("", base + "/user");
    const std::string src = buildTestPack (base + "/src", "zip.pack");
    const std::string good = zipFolder (src, base + "/good.zip", "zip.pack/");
    auto r = packLibrary().importZip (good, false);
    CHECK_MSG (r.ok, r.error);
    CHECK (r.packId == "zip.pack" && r.files > 5);
    InstalledPack ip;
    CHECK (packLibrary().find ("zip.pack", ip) && ip.complete && ! ip.factory);
    // Repetirlo sin «reemplazar» falla y no toca lo instalado.
    CHECK (! packLibrary().importZip (good, false).ok);
    CHECK (packLibrary().importZip (good, true).ok);
    // Zip slip
    {
        std::vector<ZipSource> e = { { "cauce-pack.json", src + "/cauce-pack.json" }, { "../../evil.wav", src + "/samples/kick_hard.wav" } };
        CHECK (writeStoredZip (e, base + "/slip.zip").ok);
        auto s = packLibrary().importZip (base + "/slip.zip", true);
        CHECK (! s.ok && s.error.find ("peligrosa") != std::string::npos);
        CHECK (! fs::exists (base + "/evil.wav"));
    }
    // Archivo no declarado en el manifiesto
    {
        writeText (base + "/extra/x.txt", "hola");
        std::vector<ZipSource> e;
        for (const auto& f : fs::recursive_directory_iterator (src)) if (f.is_regular_file()) e.push_back ({ fs::relative (f.path(), src).generic_string(), f.path().string() });
        e.push_back ({ "notes/x.txt", base + "/extra/x.txt" });
        CHECK (writeStoredZip (e, base + "/extra.zip").ok);
        auto s = packLibrary().importZip (base + "/extra.zip", true);
        CHECK (! s.ok && s.error.find ("no declarado") != std::string::npos);
    }
    // Ejecutable u otro tipo no admitido
    {
        std::vector<ZipSource> e = { { "cauce-pack.json", src + "/cauce-pack.json" }, { "lib/evil.so", src + "/LICENSE.txt" } };
        CHECK (writeStoredZip (e, base + "/so.zip").ok);
        CHECK (! packLibrary().importZip (base + "/so.zip", true).ok);
    }
    // Hash que no coincide (muestra alterada después de firmar el manifiesto)
    {
        const std::string tam = base + "/tampered";
        fs::copy (src, tam, fs::copy_options::recursive);
        writeSine (tam + "/samples/kick_hard.wav", 61, 0.3, 0.8f, kSr, 1, 0.08);
        auto s = packLibrary().importZip (zipFolder (tam, base + "/tam.zip"), true);
        CHECK (! s.ok && s.error.find ("SHA-256") != std::string::npos);
    }
    // ZIP truncado
    {
        std::string bytes;
        CHECK (readFile (good, bytes));
        std::string err;
        CHECK (atomicWriteFile (base + "/trunc.zip", bytes.substr (0, bytes.size() / 2), err, false));
        CHECK (! packLibrary().importZip (base + "/trunc.zip", true).ok);
    }
    // Nada a medias en la carpeta de usuario: solo el paquete instalado.
    int entries = 0;
    for (const auto& e : fs::directory_iterator (base + "/user")) { (void) e; ++entries; }
    CHECK (entries == 1);
    // Borrado: un paquete importado sí; uno de fábrica no.
    std::string err;
    CHECK (packLibrary().remove ("zip.pack", err));
    CHECK (! packLibrary().find ("zip.pack", ip));
}

TEST (sampler_project_integration_v4_only_when_used_and_undo)
{
    const std::string base = cstest::tempDir ("smp_proj");
    setupLibrary (base);
    Engine engine;
    engine.prepare (kSr, 512);
    Session s (engine);
    const std::string folder = base + "/song";
    CHECK (s.createProject (folder, "Song", kSr).ok);
    const auto keys = s.addTrack ("Keys", "synth");
    const auto clip = s.model().findTrack (keys.id)->midiClips.front().id;
    CHECK (s.setClipNotes (clip, { { 60, 100, 0, 0, kPpq }, { 64, 90, 0, kPpq, kPpq }, { 67, 80, 0, 2 * kPpq, 2 * kPpq } }).ok);
    CHECK (s.save().ok);
    std::string text;
    CHECK (readFile (folder + "/project.json", text));
    CHECK (json::parse (text)["version"] == 3); // sin contenido 0.9: sigue siendo v3
    const std::string v3text = text;

    CHECK (! s.setSamplerInstrument (keys.id, "test.pack:kit").ok); // un kit no va en una pista melódica
    CHECK (! s.setSamplerInstrument (keys.id, "test.pack:missing").ok);
    CHECK (toJson (s.model()) == toJson (s.model()));
    const auto r = s.setSamplerInstrument (keys.id, "test.pack:keys");
    CHECK_MSG (r.ok, r.error);
    CHECK (s.model().findTrack (keys.id)->instrument.type == "cs.sampler");
    CHECK (s.save().ok);
    CHECK (readFile (folder + "/project.json", text));
    CHECK (json::parse (text)["version"] == 4);
    std::string legacy;
    CHECK (readFile (folder + "/project.v3.json", legacy) && legacy == v3text); // el original v3 se conserva
    // Suena (exportación con el mismo motor).
    long clipped = 0;
    CHECK (s.exportMix (base + "/mix.wav", 32, 0, &clipped).ok);
    AudioData a;
    std::string err;
    CHECK (readWav (base + "/mix.wav", a, err));
    const double level = rmsOf (a.channels[0], 0, (size_t) a.numFrames());
    MEASURE ("pista Cauce Sampler exportada: rms %.4f, recortes %ld", level, clipped);
    CHECK (level > 0.01 && clipped == 0);
    // Deshacer vuelve a Cauce Synth y rehacer al sampler.
    CHECK (s.undo().ok);
    CHECK (s.model().findTrack (keys.id)->instrument.type == "cs.synth");
    CHECK (s.redo().ok);
    CHECK (s.model().findTrack (keys.id)->instrument.type == "cs.sampler");
    // Volver a un preset de Cauce Synth también es un paso deshacible.
    CHECK (s.loadSynthPreset (keys.id, "soft_keys").ok);
    CHECK (s.model().findTrack (keys.id)->instrument.type == "cs.synth");
    CHECK (s.undo().ok);
    CHECK (s.model().findTrack (keys.id)->instrument.type == "cs.sampler");
    CHECK (s.save().ok);
    s.close();

    // Reabrir: el instrumento sigue ahí.
    CHECK (s.openProject (folder).ok);
    CHECK (json::parse (s.stateJson())["samplers"][keys.id]["loaded"] == true);
    s.close();

    // Paquete desinstalado: el proyecto se abre, la pista queda muda y señalada, y el estado no se pierde al guardar.
    packLibrary().configure ("", base + "/user");
    CHECK (s.openProject (folder).ok);
    const json st = json::parse (s.stateJson());
    CHECK (st["missingInstruments"].size() == 1 && st["missingInstruments"][0] == "test.pack:keys");
    const std::string before = s.model().findTrack (keys.id)->instrument.state;
    CHECK (s.setTrackVolumeDb (keys.id, -2).ok);
    CHECK (s.save().ok);
    s.close();
    CHECK (s.openProject (folder).ok);
    CHECK (s.model().findTrack (keys.id)->instrument.state == before);
    s.close();
}

TEST (sampler_drum_kit_from_pack_on_drum_track)
{
    const std::string base = cstest::tempDir ("smp_drums");
    setupLibrary (base);
    Engine engine;
    engine.prepare (kSr, 512);
    Session s (engine);
    CHECK (s.createProject (base + "/beat", "Beat", kSr).ok);
    const auto d = s.addTrack ("Drums", "drums");
    const auto pat = s.model().findTrack (d.id)->patterns.front().id;
    for (int st = 0; st < 16; st += 4) CHECK (s.setPatternStep (d.id, pat, 0, st, st == 0 ? 120 : 70).ok);
    for (int st = 2; st < 16; st += 2) CHECK (s.setPatternStep (d.id, pat, 3, st, 90).ok);
    CHECK (s.exportMix (base + "/synth.wav", 32).ok);
    CHECK (! s.setDrumSampleKit (d.id, "test.pack:keys").ok); // un instrumento melódico no es un kit
    const auto r = s.setDrumSampleKit (d.id, "test.pack:kit");
    CHECK_MSG (r.ok, r.error);
    CHECK (s.exportMix (base + "/sampled.wav", 32).ok);
    AudioData a, b;
    std::string err;
    CHECK (readWav (base + "/synth.wav", a, err) && readWav (base + "/sampled.wav", b, err));
    const double ra = rmsOf (a.channels[0], 0, (size_t) a.numFrames()), rb = rmsOf (b.channels[0], 0, (size_t) b.numFrames());
    MEASURE ("kit sintetizado rms %.4f, kit de muestras rms %.4f", ra, rb);
    CHECK (rb > 0.01);
    // Canal propio para el bombo con el kit de muestras: el bus recibe la pieza.
    const json route = json::parse (handleCommand (s, engine, json { { "cmd", "routeDrumLane" }, { "trackId", d.id }, { "lane", 0 }, { "own", true } }.dump()));
    CHECK_MSG (route.value ("ok", false), route.dump());
    CHECK (s.setTrackMute (route["id"].get<std::string>(), true).ok);
    CHECK (s.exportMix (base + "/nokick.wav", 32).ok);
    AudioData c;
    CHECK (readWav (base + "/nokick.wav", c, err));
    CHECK (rmsOf (c.channels[0], 0, (size_t) c.numFrames()) < rb * 0.8);
    // Elegir un kit sintetizado quita el de muestras; deshacer lo devuelve.
    CHECK (s.setInstrumentParam (d.id, "kit", 5).ok);
    CHECK (s.model().findTrack (d.id)->instrument.state.find ("sampleKit") == std::string::npos);
    CHECK (s.undo().ok);
    CHECK (s.model().findTrack (d.id)->instrument.state.find ("sampleKit") != std::string::npos);
    s.close();
}

TEST (sampler_preview_voice_is_live_only)
{
    const std::string base = cstest::tempDir ("smp_preview");
    setupLibrary (base);
    Engine engine;
    engine.prepare (kSr, 512);
    Session s (engine);
    CHECK (s.createProject (base + "/p", "P", kSr).ok);
    const std::string before = toJson (s.model());
    const auto r = s.setPreviewInstrument ("test.pack:keys");
    CHECK_MSG (r.ok, r.error);
    const int slot = std::stoi (r.id);
    CHECK (slot == (int) s.model().tracks.size());
    CHECK (engine.sendLive (slot, 0x90, 60, 100));
    std::vector<float> out (2 * 4800, 0.0f);
    engine.process (nullptr, 0, out.data(), 2, 4800);
    float peak = 0;
    for (float v : out) peak = std::max (peak, std::fabs (v));
    MEASURE ("escucha previa: pico %.3f", peak);
    CHECK (peak > 0.02);
    CHECK (toJson (s.model()) == before); // el proyecto no cambia
    CHECK (! s.canUndo());
    CHECK (s.setPreviewInstrument ("").ok);
    s.close();
}

TEST (sampler_concurrent_kit_switching_and_project_changes_are_safe)
{
    const std::string base = cstest::tempDir ("smp_conc");
    setupLibrary (base);
    Engine engine;
    engine.prepare (kSr, 256);
    Session s (engine);
    const int baseline = Instrument::liveInstances();
    std::atomic<bool> stop { false };
    std::thread audio ([&]
    {
        std::vector<float> out (512);
        while (! stop.load()) engine.process (nullptr, 0, out.data(), 2, 256);
    });
    for (int round = 0; round < 6; ++round)
    {
        CHECK (s.createProject (base + "/c" + std::to_string (round), "C", kSr).ok);
        const auto d = s.addTrack ("Drums", "drums");
        const auto k = s.addTrack ("Keys", "synth");
        const auto pat = s.model().findTrack (d.id)->patterns.front().id;
        for (int st = 0; st < 16; ++st) s.setPatternStep (d.id, pat, st % 2 ? 3 : 0, st, 100);
        engine.play();
        for (int i = 0; i < 8; ++i)
        {
            CHECK (s.setDrumSampleKit (d.id, i % 2 ? "" : "test.pack:kit").ok);
            CHECK (s.setSamplerInstrument (k.id, "test.pack:keys").ok);
            engine.sendLive (1, 0x90, 60 + i, 100);
            CHECK (s.setPreviewInstrument (i % 3 ? "test.pack:keys" : "").ok);
            engine.collectGarbage();
        }
        engine.stop();
        s.close();
        engine.collectGarbage();
    }
    stop = true;
    audio.join();
    engine.collectGarbage();
    MEASURE ("instancias vivas tras cerrar: %d (antes %d)", Instrument::liveInstances(), baseline);
    CHECK (Instrument::liveInstances() == baseline);
}

TEST (sampler_performance_offline_voices)
{
    const std::string dir = cstest::tempDir ("smp_perf");
    const std::string pd = buildTestPack (dir);
    auto prog = programFrom (pd + "/instruments", "<region> lokey=0 hikey=127 pitch_keycenter=60 ampeg_release=1 sample=../samples/keys_c4.wav\n");
    prog->polyphony = 32;
    SamplerEngine e;
    e.prepare (prog, kSr);
    for (int k = 0; k < 32; ++k) e.noteOn (36 + k, 100);
    std::vector<float> L (256), R (256);
    const int blocks = 1500; // 8 s de audio
    const auto t0 = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b) { std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f); e.process (L.data(), R.data(), 256); }
    const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    const double audioSec = blocks * 256.0 / kSr;
    MEASURE ("32 voces estéreo: %.1f ms por segundo de audio (%.2f %% de un núcleo x86-64, sin medir en ARM)", 1000.0 * sec / audioSec, 100.0 * sec / audioSec);
    CHECK (sec / audioSec < 0.5);
}
