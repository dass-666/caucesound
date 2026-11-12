// cs_render: renderiza plantillas a WAV (con el mismo motor que la exportación de la app) y mide cada render.
//   cs_render <carpeta> [plantilla...]      → <carpeta>/<plantilla>.wav + <carpeta>/metrics.json
//   cs_render --analyze <archivo.wav> <bpm>  → métricas de un WAV existente
// Las métricas son objetivas (niveles, espectro, dinámica, repetición); NO dicen si algo «suena bien».
#include "cs/Api.h"
#include "cs/Session.h"
#include "cs/Wav.h"

#include <nlohmann/json.hpp>
#ifdef CS_WITH_SOLO
#include "solo/BeatAnalyzer.h"
#endif
#include <thread>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>

using namespace cs;
using nlohmann::json;

namespace
{
struct Bq
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    static Bq make (bool high, double sr, double hz)
    {
        Bq q; const double w = 2 * M_PI * hz / sr, c = std::cos (w), al = std::sin (w) / (2 * 0.7071), a0 = 1 + al;
        if (high) { q.b0 = (1 + c) / 2 / a0; q.b1 = -(1 + c) / a0; q.b2 = q.b0; }
        else { q.b0 = (1 - c) / 2 / a0; q.b1 = (1 - c) / a0; q.b2 = q.b0; }
        q.a1 = -2 * c / a0; q.a2 = (1 - al) / a0; return q;
    }
    double run (double x) { const double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};
double db (double x) { return x > 1e-12 ? 20 * std::log10 (x) : -240.0; }

json analyze (const AudioData& a, double bpm)
{
    const double sr = a.sampleRate;
    const size_t n = (size_t) a.numFrames();
    const auto& L = a.channels[0];
    const auto& R = a.numChannels() > 1 ? a.channels[1] : a.channels[0];
    double peak = 0, sum = 0, mid = 0, side = 0; long clipped = 0;
    for (size_t i = 0; i < n; ++i)
    {
        peak = std::max (peak, (double) std::max (std::fabs (L[i]), std::fabs (R[i])));
        sum += 0.5 * ((double) L[i] * L[i] + (double) R[i] * R[i]);
        if (std::fabs (L[i]) >= 0.999f || std::fabs (R[i]) >= 0.999f) ++clipped;
        const double m = 0.5 * (L[i] + R[i]), s = 0.5 * (L[i] - R[i]);
        mid += m * m; side += s * s;
    }
    const double rms = std::sqrt (sum / std::max<size_t> (1, n));
    // Bandas (energía relativa del canal medio)
    const double edges[4] = { 60, 250, 2000, 8000 };
    double band[5] = {};
    {
        Bq lp[4], hp[4];
        for (int k = 0; k < 4; ++k) { lp[k] = Bq::make (false, sr, edges[k]); hp[k] = Bq::make (true, sr, edges[k]); }
        Bq lp2[4], hp2[4];
        for (int k = 0; k < 4; ++k) { lp2[k] = lp[k]; hp2[k] = hp[k]; }
        for (size_t i = 0; i < n; ++i)
        {
            const double x = 0.5 * (L[i] + R[i]);
            double below[4], above[4];
            for (int k = 0; k < 4; ++k) { below[k] = lp2[k].run (lp[k].run (x)); above[k] = hp2[k].run (hp[k].run (x)); }
            band[0] += below[0] * below[0];
            band[4] += above[3] * above[3];
            // bandas intermedias aproximadas: energía por encima del borde inferior menos por encima del superior
            for (int k = 0; k < 3; ++k) band[k + 1] += above[k] * above[k] - above[k + 1] * above[k + 1];
        }
    }
    double bt = 0; for (double v : band) bt += std::max (0.0, v);
    // «Altavoz pequeño»: nivel de lo que queda por encima de 150 Hz (paso alto de 4.º orden) respecto al total. Es una
    // aproximación de lo que reproduce el altavoz de un teléfono; no sustituye a escucharlo en uno.
    double small = 0, full = 0;
    {
        Bq a1 = Bq::make (true, sr, 150), a2 = a1;
        for (size_t i = 0; i < n; ++i) { const double x = 0.5 * (L[i] + R[i]); const double y = a2.run (a1.run (x)); small += y * y; full += x * x; }
    }
    // Dinámica: RMS en ventanas de 400 ms (p10..p95)
    std::vector<double> st;
    const size_t win = (size_t) (0.4 * sr);
    for (size_t i = 0; i + win <= n; i += win / 2)
    {
        double s = 0; for (size_t j = i; j < i + win; ++j) s += 0.5 * ((double) L[j] * L[j] + (double) R[j] * R[j]);
        st.push_back (db (std::sqrt (s / win)));
    }
    std::vector<double> sorted; for (double v : st) if (v > -60) sorted.push_back (v); // sin la cola en silencio
    std::sort (sorted.begin(), sorted.end());
    auto pct = [&] (double p) { return sorted.empty() ? 0.0 : sorted[(size_t) std::clamp (p * (sorted.size() - 1), 0.0, (double) sorted.size() - 1)]; };
    // Densidad de ataques: subidas de energía aguda (> 6 dB) en tramas de 10 ms
    Bq h1 = Bq::make (true, sr, 1500), h2 = h1;
    const size_t fr = (size_t) (0.01 * sr);
    double prev = 1e-9; int onsets = 0; std::vector<double> env;
    for (size_t i = 0; i + fr <= n; i += fr)
    {
        double s = 0; for (size_t j = i; j < i + fr; ++j) { const double y = h2.run (h1.run (0.5 * (L[j] + R[j]))); s += y * y; }
        s = s / fr + 1e-12;
        if (10 * std::log10 (s / prev) > 6 && s > 1e-6) ++onsets;
        prev = 0.7 * prev + 0.3 * s;
        env.push_back (std::sqrt (s));
    }
    // Repetición: correlación de la envolvente de cada bloque de 4 compases con el primero (1 = idéntico)
    json blocks = json::array();
    if (bpm > 0)
    {
        const size_t per = (size_t) std::lround (4 * 4 * 60.0 / bpm / 0.01);
        const size_t nb = env.size() / std::max<size_t> (1, per);
        for (size_t b = 1; b < nb; ++b)
        {
            double sa = 0, sb = 0, sab = 0, ma = 0, mb = 0;
            for (size_t i = 0; i < per; ++i) { ma += env[i]; mb += env[b * per + i]; }
            ma /= per; mb /= per;
            if (mb < 0.1 * ma) continue; // bloque de cola (silencio tras el contenido)
            for (size_t i = 0; i < per; ++i) { const double x = env[i] - ma, y = env[b * per + i] - mb; sab += x * y; sa += x * x; sb += y * y; }
            blocks.push_back (std::round (100 * sab / std::sqrt (sa * sb + 1e-30)) / 100);
        }
    }
    json bands = json::object();
    const char* names[5] = { "sub<60", "low60-250", "mid250-2k", "high2k-8k", "air>8k" };
    for (int k = 0; k < 5; ++k) bands[names[k]] = std::round (1000 * std::max (0.0, band[k]) / std::max (bt, 1e-30)) / 10;
    const double secs = n / sr;
    return { { "seconds", std::round (secs * 100) / 100 }, { "peakDb", std::round (db (peak) * 10) / 10 }, { "rmsDb", std::round (db (rms) * 10) / 10 },
             { "crestDb", std::round ((db (peak) - db (rms)) * 10) / 10 }, { "clippedSamples", clipped },
             { "shortTermP10Db", std::round (pct (0.1) * 10) / 10 }, { "shortTermP95Db", std::round (pct (0.95) * 10) / 10 },
             { "dynamicSpreadDb", std::round ((pct (0.95) - pct (0.1)) * 10) / 10 },
             { "sideToMidDb", std::max (-60.0, std::round ((10 * std::log10 ((side + 1e-30) / (mid + 1e-30))) * 10) / 10) },
             { "bandsPct", bands }, { "above150HzVsTotalDb", std::round (10 * (10 * std::log10 ((small + 1e-30) / (full + 1e-30)))) / 10 }, { "onsetsPerSecond", std::round (10 * onsets / std::max (secs, 1e-9)) / 10 },
             { "blockSimilarityToFirst", blocks } };
}
} // namespace

int main (int argc, char** argv)
{
    if (argc >= 4 && std::string (argv[1]) == "--analyze")
    {
        AudioData a; std::string err;
        if (! readWav (argv[2], a, err)) { std::fprintf (stderr, "%s\n", err.c_str()); return 1; }
        std::printf ("%s\n", analyze (a, std::atof (argv[3])).dump (2).c_str());
        return 0;
    }
    if (argc >= 3 && std::string (argv[1]) == "--catalog")
    {
        Engine e; e.prepare (48000, 1024);
        Session s (e);
        std::ofstream (argv[2]) << json::parse (handleCommand (s, e, "{\"cmd\":\"catalog\"}")).dump (1);
        return 0;
    }
    if (argc >= 4 && std::string (argv[1]) == "--state")
    {
        // Estado JSON de un proyecto recién creado con una plantilla (fixture para las pruebas de interfaz Android).
        Engine e; e.prepare (48000, 1024);
        Session s (e);
        const std::string dir = std::filesystem::temp_directory_path().string() + "/cs_render_state";
        std::filesystem::remove_all (dir);
        if (! s.createProject (dir, "Fixture", 48000, argv[2]).ok) return 1;
        std::ofstream (argv[3]) << json::parse (s.stateJson()).dump (1);
        return 0;
    }
    if (argc >= 3 && std::string (argv[1]) == "--fixtures05")
    {
        // Datos reales del motor para las pruebas de interfaz de la 0.5: presets de VocalShift, un proyecto con una
        // toma procesada, el resultado de analizar un beat y una familia de Solo AI.
        const std::string out = std::filesystem::absolute (argv[2]).string();
        std::filesystem::create_directories (out);
        Engine e; e.prepare (48000, 1024);
        Session s (e);
        auto cmd = [&] (const json& q) { return json::parse (handleCommand (s, e, q.dump())); };
        auto wait = [&] (const std::string& id)
        {
            for (;;)
            {
                const json j = cmd ({ { "cmd", "jobStatus" }, { "jobId", id } });
                const std::string st = j.value ("state", std::string());
                if (st == "done" || st == "failed" || st == "cancelled") return j;
                std::this_thread::sleep_for (std::chrono::milliseconds (5));
            }
        };
        const std::string dir = out + "/_proj05";
        std::filesystem::remove_all (dir);
        if (! s.createProject (dir, "Voz y beat", 48000, "").ok) return 1;
        // «Voz» sintética de 6 s (tono con vibrato en sílabas) y un beat sintético de Solo AI (92 BPM, La menor).
        AudioData v; v.sampleRate = 48000; v.allocate (1, 6 * 48000);
        double ph = 0;
        for (size_t i = 0; i < v.channels[0].size(); ++i)
        {
            const double t = (double) i / 48000.0, syl = std::fmod (t, 0.5);
            ph += (165.0 + 30.0 * std::sin (t * 1.3)) / 48000.0;
            double x = 0; for (int h = 1; h <= 12; ++h) x += std::sin (2 * 3.14159265358979 * ph * h) / h;
            v.channels[0][i] = (float) (x * 0.2 * (syl < 0.38 ? std::min (1.0, syl / 0.02) * std::min (1.0, (0.38 - syl) / 0.05) : 0.0));
        }
        std::string err;
        writeWav (out + "/_voz.wav", v, WavFormat::Float32, err);
        s.addTrack ("Voz"); const std::string voz = s.model().tracks.back().id;
        const auto ic = s.importWav (out + "/_voz.wav", voz, 0, "Toma de voz");
#ifdef CS_WITH_SOLO
        ::solo::SyntheticBeatSpec spec; spec.bpm = 92; spec.bars = 16; spec.sampleRate = 48000;
        const auto mono = ::solo::synthesiseBeat (spec);
        AudioData b; b.sampleRate = 48000; b.channels.assign (1, mono.samples);
        writeWav (out + "/_beat.wav", b, WavFormat::Float32, err);
        s.addTrack ("Beat"); const std::string beat = s.model().tracks.back().id;
        const auto ib = s.importWav (out + "/_beat.wav", beat, 0, "Beat importado");
        const json an = wait (cmd ({ { "cmd", "soloAnalyze" }, { "clipId", ib.id } }).value ("jobId", std::string()));
        std::ofstream (out + "/analysis.json") << an.dump (1);
        std::ofstream (out + "/state_beat.json") << json::parse (s.stateJson()).dump (1);
        cmd ({ { "cmd", "setHarmony" }, { "keyRoot", 9 }, { "minor", true }, { "source", "analysis" } });
        std::ofstream (out + "/family.json") << cmd ({ { "cmd", "soloProposals" }, { "bars", 8 }, { "seed", 2026 } }).dump (1);
        const json pick = cmd ({ { "cmd", "soloMelody" }, { "bars", 8 }, { "seed", 2026 }, { "proposal", 0 } });
        std::ofstream (out + "/family_pick.json") << pick.dump (1);
#endif
        std::ofstream (out + "/vocal_presets.json") << cmd ({ { "cmd", "vocalPresets" } }).dump (1);
        const json job = wait (cmd ({ { "cmd", "vocalProcess" }, { "clipId", ic.id }, { "params", json::parse (vocal::Params().toJson()) } }).value ("jobId", std::string()));
        std::ofstream (out + "/vocal_job.json") << job.dump (1);
        cmd ({ { "cmd", "clipVersion" }, { "clipId", ic.id }, { "original", false }, { "match", true } }); // comparando a volumen igualado
        std::ofstream (out + "/state_vocal.json") << json::parse (s.stateJson()).dump (1);
        // Escucha previa de una región (0.6): la prueba existe y está sonando.
        wait (cmd ({ { "cmd", "vocalPreview" }, { "clipId", ic.id }, { "params", json::parse (vocal::Params().toJson()) }, { "from", 48000 }, { "length", 96000 } }).value ("jobId", std::string()));
        cmd ({ { "cmd", "previewListen" }, { "clipId", ic.id }, { "on", true } });
        std::ofstream (out + "/state_vocal_preview.json") << json::parse (s.stateJson()).dump (1);
        std::ofstream (out + "/catalog.json") << cmd ({ { "cmd", "catalog" } }).dump (1);
        s.close();
        std::filesystem::remove_all (dir);
        std::filesystem::remove (out + "/_voz.wav"); std::filesystem::remove (out + "/_beat.wav");
        std::printf ("ok\n");
        return 0;
    }
    if (argc >= 3 && std::string (argv[1]) == "--solo")
    {
        // Ejemplo de Solo AI: plantilla de trap + la propuesta B (emocional) de 8 compases sobre la sección A.
        Engine e; e.prepare (48000, 1024);
        Session s (e);
        const std::string dir = std::filesystem::absolute (argv[2]).string() + "_proj";
        std::filesystem::remove_all (dir);
        if (! s.createProject (dir, "Solo", 48000, "trap").ok) return 1;
        const json r = json::parse (handleCommand (s, e, R"({"cmd":"soloMelody","startBar":2,"bars":8,"seed":2026,"proposal":1,"type":"chorus","character":"emotional"})"));
        std::printf ("%s\n", r.dump().c_str());
        if (! r.value ("ok", false)) return 1;
        long clipped = 0;
        if (! s.exportMix (argv[2], 24, 0, &clipped).ok) return 1;
        AudioData a; std::string err; readWav (argv[2], a, err);
        std::printf ("%s\n", analyze (a, s.model().bpm).dump (2).c_str());
        s.close();
        std::filesystem::remove_all (dir);
        return 0;
    }
    if (argc < 2) { std::fprintf (stderr, "uso: cs_render <carpeta> [plantilla...] | --analyze wav bpm | --state plantilla salida.json\n"); return 1; }
    const std::string out = std::filesystem::absolute (argv[1]).string();
    std::filesystem::create_directories (out);
    std::vector<std::string> ids;
    for (int i = 2; i < argc; ++i) ids.push_back (argv[i]);
    if (ids.empty()) for (const auto& t : projectTemplates()) ids.push_back (t.id);
    json all = json::object();
    for (const auto& id : ids)
    {
        Engine e; e.prepare (48000, 1024);
        Session s (e);
        const std::string dir = out + "/_proj_" + id;
        std::filesystem::remove_all (dir);
        auto r = s.createProject (dir, id, 48000, id);
        if (! r.ok) { std::fprintf (stderr, "%s: %s\n", id.c_str(), r.error.c_str()); return 1; }
        const std::string wav = out + "/" + id + ".wav";
        long clipped = 0;
        const auto t0 = std::chrono::steady_clock::now();
        r = s.exportMix (wav, 24, 0, &clipped);
        const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        if (! r.ok) { std::fprintf (stderr, "%s: %s\n", id.c_str(), r.error.c_str()); return 1; }
        AudioData a; std::string err; readWav (wav, a, err);
        json m = analyze (a, s.model().bpm);
        m["bpm"] = s.model().bpm;
        m["tracks"] = s.model().tracks.size();
        m["renderSecondsPerAudioSecond"] = std::round (10000 * secs / std::max (1e-9, a.numFrames() / 48000.0)) / 10000;
        m["exportClippedSamples"] = clipped;
        all[id] = m;
        s.close();
        std::filesystem::remove_all (dir);
        std::printf ("%s: %s\n", id.c_str(), m.dump().c_str());
    }
    std::ofstream (out + "/metrics.json") << all.dump (2);
    return 0;
}
