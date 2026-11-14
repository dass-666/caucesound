// 0.9.2: estudio de VocalShift como inserción en vivo (prueba de concepto OCULTA, no está en el catálogo).
// Lo que demuestran estas pruebas, en x86 y con señal sintética (no en un teléfono ni con voz real):
//  - el efecto declara su latencia (~150 ms) y el PDC por pista del motor la compensa durante la reproducción;
//  - process() no reserva memoria, ni siquiera al mover parámetros en marcha;
//  - el coste de CPU (factor de tiempo real) medido en esta máquina;
//  - el catálogo y addInsert NO lo ofrecen todavía (no se anuncia como disponible).
#include "Test.h"

#include "cs/Engine.h"
#include "cs/Processor.h"
#include "cs/Session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <memory>
#include <vector>

using namespace cs;

namespace
{
constexpr double kRate = 48000.0;

std::shared_ptr<AudioData> tone (double seconds)
{
    // Tono armónico de 196 Hz con vibrato y envolvente por sílabas: una «voz» sintética reconocible por correlación.
    auto a = std::make_shared<AudioData>();
    const Frame n = (Frame) (seconds * kRate);
    a->allocate (1, n);
    double ph = 0;
    for (Frame i = 0; i < n; ++i)
    {
        const double t = (double) i / kRate;
        ph += 2.0 * M_PI * 196.0 * (1.0 + 0.01 * std::sin (2 * M_PI * 5.0 * t)) / kRate;
        double s = 0;
        for (int h = 1; h <= 8; ++h) s += std::sin (ph * h) / h;
        const double env = 0.5 - 0.5 * std::cos (2 * M_PI * std::fmod (t, 0.5) / 0.5);
        a->channels[0][(size_t) i] = (float) (0.25 * s * env);
    }
    return a;
}

std::vector<float> render (const std::shared_ptr<AudioData>& a, const ProcessorPtr& fx, Frame at, Frame frames, int block)
{
    auto g = std::make_unique<RenderGraph>();
    g->keepAlive.push_back (a);
    TrackRT tr;
    tr.slot = 0;
    ClipRT c;
    c.data = a.get(); c.start = at; c.length = a->numFrames(); c.gain = 1.0f;
    tr.clips.push_back (c);
    if (fx) tr.inserts.push_back (fx);
    g->tracks.push_back (tr);
    Engine e;
    e.prepare (kRate, 1024);
    e.publish (std::move (g));
    e.seek (0);
    e.play();
    std::vector<float> out ((size_t) frames * 2), buf ((size_t) block * 2);
    for (Frame done = 0; done < frames; done += block)
    {
        const int n = (int) std::min<Frame> (block, frames - done);
        e.process (nullptr, 0, buf.data(), 2, n);
        std::copy (buf.begin(), buf.begin() + n * 2, out.begin() + (long) done * 2);
    }
    return out;
}

ProcessorPtr neutralPoC()
{
    auto fx = createProcessor ("cs.vocalshift.live");
    if (! fx) return nullptr;
    // Sin cambio de tono, sin segunda voz ni «magia»: la salida debería ser casi la entrada (para medir alineación).
    fx->setParameter (0, 0); fx->setParameter (2, 0); fx->setParameter (3, 0); fx->setParameter (4, -60);
    fx->prepare (kRate, 1024);
    return fx;
}
} // namespace

TEST (vocalshift_live_poc_is_hidden_from_catalog_and_addInsert)
{
    const auto types = effectTypes();
    CHECK (std::find (types.begin(), types.end(), "cs.vocalshift.live") == types.end());
    Engine e;
    Session s (e);
    const std::string dir = "/tmp/cs_test_vs_live_hidden";
    std::filesystem::remove_all (dir); // la carpeta de una ejecución anterior haría fallar createProject
    CHECK (s.createProject (dir + "/p", "vs_live_hidden").ok);
    const auto tr = s.addTrack ("Voz", "audio");
    CHECK (tr.ok);
    const auto r = s.addInsert (tr.id, "cs.vocalshift.live");
    CHECK (! r.ok);
}

TEST (vocalshift_live_poc_declares_latency_and_pdc_keeps_it_aligned)
{
    auto fx = neutralPoC();
    if (! fx) { MEASURE ("VocalShift no está compilado en este binario: prueba omitida"); return; }
    const int L = fx->latencySamples();
    MEASURE ("latencia declarada de la inserción en vivo: %d muestras = %.1f ms", L, L * 1000.0 / kRate);
    CHECK (L > 0 && L <= (int) (0.25 * kRate));
    const auto a = tone (3.0);
    const Frame at = 48000, frames = (Frame) (5 * kRate);
    const auto dry = render (a, nullptr, at, frames, 256);
    for (int block : { 192, 256 })
    {
        const auto wet = render (a, neutralPoC(), at, frames, block);
        // Desfase por correlación en la parte estable del clip (de 1,5 s a 3,5 s del proyecto).
        int bestLag = 9999;
        double best = -1e30;
        for (int lag = -600; lag <= 600; lag += 1)
        {
            double acc = 0;
            for (Frame i = 72000; i < 168000; i += 2) acc += (double) dry[(size_t) i * 2] * wet[(size_t) (i + lag) * 2];
            if (acc > best) { best = acc; bestLag = lag; }
        }
        MEASURE ("bloque %d: desfase seco/procesado durante la reproducción = %d muestras", block, bestLag);
        CHECK (std::abs (bestLag) <= 2);
        // Antes del clip no aparece audio adelantado (el PDC lee el contenido L muestras por delante, sin pre-eco).
        float pre = 0;
        for (Frame i = 0; i < at - 2400; ++i) pre = std::max (pre, std::fabs (wet[(size_t) i * 2]));
        CHECK (pre < 1e-3f);
    }
}

TEST (vocalshift_live_poc_process_does_not_allocate_and_measures_cpu)
{
    auto fx = createProcessor ("cs.vocalshift.live");
    if (! fx) { MEASURE ("VocalShift no está compilado en este binario: prueba omitida"); return; }
    fx->prepare (kRate, 256);
    const auto a = tone (10.0);
    std::vector<float> l (256), r (256);
    float* io[2] = { l.data(), r.data() };
    const Frame n = a->numFrames();
    cstest::allocationCount.store (0);
    cstest::countAllocations.store (true);
    const auto t0 = std::chrono::steady_clock::now();
    int k = 0;
    for (Frame done = 0; done + 256 <= n; done += 256, ++k)
    {
        std::copy_n (a->channels[0].data() + done, 256, l.data());
        std::copy_n (a->channels[0].data() + done, 256, r.data());
        if (k % 200 == 0)
        {
            // setParameter llega desde el hilo de control: no se cuenta. Lo que se mide es process() aplicando el cambio.
            cstest::countAllocations.store (false);
            fx->setParameter (0, (k / 200) % 2 ? 5.0f : 7.0f);
            cstest::countAllocations.store (true);
        }
        fx->process (io, 2, 256);
    }
    const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    cstest::countAllocations.store (false);
    MEASURE ("reservas de memoria en process(): %ld", cstest::allocationCount.load());
    CHECK (cstest::allocationCount.load() == 0);
    const double rtf = secs / ((double) n / kRate);
    MEASURE ("CPU x86 de esta máquina: %.3f s para 10 s de audio estéreo (factor de tiempo real %.3f = %.1f %% de un núcleo)", secs, rtf, rtf * 100);
    bool finite = true;
    for (float v : l) finite = finite && std::isfinite (v);
    CHECK (finite);
}
