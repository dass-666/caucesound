// Medición de rendimiento del motor en la máquina donde se ejecuta. NO representa un teléfono:
// los resultados en ARM deben medirse en el dispositivo (ver docs/TESTING_ON_DEVICE.md).
#include "cs/Engine.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <random>

using namespace cs;

int main()
{
    const int rate = 48000, block = 192;
    std::printf ("Bench del motor (%d Hz, bloques de %d frames, estéreo)\n", rate, block);
    for (int tracks : { 4, 8, 16, 32 })
    {
        Engine e;
        e.prepare (rate, block);
        auto g = std::make_unique<RenderGraph>();
        std::mt19937 rng (1);
        for (int t = 0; t < tracks; ++t)
        {
            auto a = std::make_shared<AudioData>();
            a->allocate (2, rate * 10);
            for (auto& c : a->channels) for (auto& v : c) v = (float) (rng() % 2000) / 2000.0f - 0.5f;
            TrackRT tr;
            tr.slot = t;
            for (int k = 0; k < 4; ++k) { ClipRT c; c.data = a.get(); c.start = k * rate * 10; c.length = rate * 10; c.fadeIn = 480; c.fadeOut = 480; tr.clips.push_back (c); }
            g->keepAlive.push_back (a);
            g->tracks.push_back (tr);
        }
        e.publish (std::move (g));
        e.setMetronome (true);
        e.play();
        std::vector<float> out ((size_t) block * 2);
        const int blocks = rate * 30 / block; // 30 s de audio
        std::vector<double> us ((size_t) blocks);
        const auto t0 = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            const auto s = std::chrono::steady_clock::now();
            e.process (nullptr, 0, out.data(), 2, block);
            us[(size_t) b] = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - s).count();
        }
        const double total = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::sort (us.begin(), us.end());
        const double budget = 1e6 * block / rate;
        std::printf ("  %2d pistas: %.0fx tiempo real; por bloque p50 %.1f us, p99 %.1f us, max %.1f us (presupuesto %.0f us => p99 = %.2f%%)\n",
                     tracks, 30.0 / total, us[us.size() / 2], us[us.size() * 99 / 100], us.back(), budget, 100 * us[us.size() * 99 / 100] / budget);
    }
    return 0;
}
