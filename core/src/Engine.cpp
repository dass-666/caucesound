#include "cs/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cs
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

void makeClick (std::vector<float>& out, int rate, double freq, float amp)
{
    const int len = (int) (0.030 * rate); // 30 ms
    out.assign ((size_t) len, 0.0f);
    for (int i = 0; i < len; ++i)
    {
        const double t = (double) i / rate;
        const double env = std::exp (-t * 160.0) * std::min (1.0, i / (0.0005 * rate)); // ataque 0.5 ms
        out[(size_t) i] = (float) (amp * env * std::sin (2 * kPi * freq * t));
    }
}

inline float approach (float current, float target, float step)
{
    if (current < target) return std::min (current + step, target);
    if (current > target) return std::max (current - step, target);
    return current;
}
} // namespace

Engine::Engine() : params ((size_t) kMaxTracks) {}

Engine::~Engine()
{
    collectGarbage();
    delete pending.exchange (nullptr);
    delete active;
}

void Engine::prepare (int sampleRate, int maxBlockSize)
{
    rate = sampleRate;
    maxBlock = std::max (16, maxBlockSize);
    for (auto* v : { &trackL, &trackR, &mixL, &mixR }) v->assign ((size_t) maxBlock, 0.0f);
    inScratch.assign ((size_t) maxBlock * 8, 0.0f);
    recScratch.assign ((size_t) maxBlock * 2, 0.0f);
    smGainL.assign ((size_t) kMaxTracks, 0.0f);
    smGainR.assign ((size_t) kMaxTracks, 0.0f);
    smoothStep = (float) (1.0 / (0.010 * rate)); // de 0 a 1 en 10 ms
    makeClick (clickHi, rate, 1760.0, 0.9f);
    makeClick (clickLo, rate, 1175.0, 0.6f);
}

void Engine::publish (std::unique_ptr<RenderGraph> graph)
{
    collectGarbage();
    // Si el hilo de audio no llegó a tomar el anterior, nunca lo vio: podemos liberarlo aquí.
    delete pending.exchange (graph.release(), std::memory_order_acq_rel);
}

void Engine::collectGarbage()
{
    RenderGraph* g = nullptr;
    while (trash.pop (&g, 1) == 1) delete g;
}

void Engine::adoptPendingGraph()
{
    RenderGraph* g = pending.exchange (nullptr, std::memory_order_acq_rel);
    if (! g) return;
    // Con el transporte en marcha, si el plan de eventos de un instrumento cambió, puede faltar el Note Off de una
    // nota que ya suena (p. ej. se borró mientras sonaba): se liberan sus voces para que ninguna quede atascada.
    if (prevRolling)
        for (const auto& t : g->tracks)
        {
            if (! t.instrument) continue;
            bool same = false;
            if (active)
                for (const auto& o : active->tracks)
                    if (o.instrument == t.instrument) { same = o.eventsHash == t.eventsHash; break; }
            if (! same) t.instrument->allNotesOff (false);
        }
    if (active && ! trash.push (&active, 1))
    {
        // Cola llena (el hilo de control no recoge basura): preferimos una fuga a liberar en tiempo real.
    }
    active = g;
}

uint64_t hashEvents (const std::vector<SchedEvent>& ev)
{
    uint64_t h = 1469598103934665603ull; // FNV-1a
    auto mix = [&] (uint64_t v) { h ^= v; h *= 1099511628211ull; };
    for (const auto& e : ev) { mix ((uint64_t) e.frame); mix (((uint64_t) e.status << 16) | ((uint64_t) e.data1 << 8) | e.data2); }
    return h;
}

void Engine::setLoop (bool on, Frame start, Frame end)
{
    loopStart.store (std::max<Frame> (0, start));
    loopEnd.store (std::max<Frame> (0, end));
    loopOn.store (on && end > start);
}

bool Engine::sendLive (int slot, uint8_t status, uint8_t data1, uint8_t data2)
{
    while (liveLock.test_and_set (std::memory_order_acquire)) {}
    const LiveMidi m { slot, status, data1, data2 };
    const bool ok = live.push (&m, 1);
    liveLock.clear (std::memory_order_release);
    return ok;
}

void Engine::notesOff (const RenderGraph* g, bool hard)
{
    if (! g) return;
    for (const auto& t : g->tracks) if (t.instrument) t.instrument->allNotesOff (hard);
}

void Engine::drainLive (const RenderGraph* g)
{
    LiveMidi m;
    while (live.pop (&m, 1) == 1)
    {
        if (! g) continue;
        for (const auto& t : g->tracks)
            if (t.slot == m.slot && t.instrument)
            {
                const MidiEvent ev { 0, m.status, m.d1, m.d2 };
                t.instrument->handleMidi (&ev, 1);
            }
    }
}

void Engine::renderInstrument (const TrackRT& t, Frame position, bool rolling, float* tl, float* tr, int n)
{
    Instrument& inst = *t.instrument;
    int cursor = 0;
    if (rolling && ! t.events.empty())
    {
        const Frame blockEnd = position + n;
        auto it = std::lower_bound (t.events.begin(), t.events.end(), position,
                                    [] (const SchedEvent& e, Frame f) { return e.frame < f; });
        while (it != t.events.end() && it->frame < blockEnd)
        {
            const int off = (int) (it->frame - position);
            if (off > cursor)
            {
                float* io[2] = { tl + cursor, tr + cursor };
                inst.process (io, 2, off - cursor);
                cursor = off;
            }
            const MidiEvent ev { 0, it->status, it->data1, it->data2 };
            inst.handleMidi (&ev, 1);
            ++it;
        }
    }
    if (cursor < n)
    {
        float* io[2] = { tl + cursor, tr + cursor };
        inst.process (io, 2, n - cursor);
    }
}

void Engine::startRecording (SpscRing<float>* ring, int channels)
{
    recRing.store (ring);
    recChannels.store (std::clamp (channels, 1, 2));
    recStart.store (-1);
    recDropped.store (0);
    recording.store (true);
}

void Engine::stopRecording()
{
    recording.store (false);
}

void Engine::snapSmoothersToTargets()
{
    smMaster = masterGain.load();
    const RenderGraph* g = active;
    if (! g) return;
    bool anySolo = false;
    for (const auto& t : g->tracks) anySolo |= params[(size_t) t.slot].solo.load (std::memory_order_relaxed);
    for (const auto& t : g->tracks)
    {
        const TrackParams& p = params[(size_t) t.slot];
        const bool audible = ! p.mute.load (std::memory_order_relaxed) && (! anySolo || p.solo.load (std::memory_order_relaxed));
        const float gn = audible ? p.gain.load (std::memory_order_relaxed) : 0.0f;
        const float pan = std::clamp (p.pan.load (std::memory_order_relaxed), -1.0f, 1.0f);
        smGainL[(size_t) t.slot] = gn * (pan > 0 ? 1.0f - pan : 1.0f);
        smGainR[(size_t) t.slot] = gn * (pan < 0 ? 1.0f + pan : 1.0f);
    }
}

void Engine::renderBlock (const RenderGraph* g, Frame position, bool rolling, float* L, float* R, int n)
{
    std::fill (L, L + n, 0.0f);
    std::fill (R, R + n, 0.0f);
    if (! g || ! rolling)
    {
        snapSmoothersToTargets();
        wasRolling = false;
        if (! g) return;
        // Parado: solo suenan los instrumentos (pads, teclado, colas de release), con la mezcla ya aplicada.
        bool anySoloStopped = false;
        for (const auto& t : g->tracks) anySoloStopped |= params[(size_t) t.slot].solo.load (std::memory_order_relaxed);
        for (const auto& t : g->tracks)
        {
            if (! t.instrument) continue;
            float* tl = trackL.data();
            float* tr = trackR.data();
            std::fill (tl, tl + n, 0.0f);
            std::fill (tr, tr + n, 0.0f);
            renderInstrument (t, position, false, tl, tr, n);
            float* io[2] = { tl, tr };
            for (const auto& fx : t.inserts) fx->process (io, 2, n);
            const float gl = smGainL[(size_t) t.slot], gr = smGainR[(size_t) t.slot];
            float peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                const float l = tl[i] * gl * smMaster, r = tr[i] * gr * smMaster;
                L[i] += l;
                R[i] += r;
                peak = std::max (peak, std::max (std::fabs (l), std::fabs (r)));
            }
            params[(size_t) t.slot].peak.store (peak, std::memory_order_relaxed);
        }
        (void) anySoloStopped;
        return;
    }
    // Al arrancar el transporte, la mezcla empieza ya en su valor (sin rampa desde 0), igual que la exportación.
    if (! wasRolling) { snapSmoothersToTargets(); wasRolling = true; }

    bool anySolo = false;
    for (const auto& t : g->tracks) anySolo |= params[(size_t) t.slot].solo.load (std::memory_order_relaxed);

    const Frame blockEnd = position + n;
    float* tl = trackL.data();
    float* tr = trackR.data();
    for (const auto& t : g->tracks)
    {
        std::fill (tl, tl + n, 0.0f);
        std::fill (tr, tr + n, 0.0f);
        bool any = false;
        for (const auto& c : t.clips)
        {
            if (c.start >= blockEnd) break; // ordenados por inicio
            const Frame cEnd = c.start + c.length;
            if (cEnd <= position || ! c.data) continue;
            const Frame from = std::max (position, c.start), to = std::min (blockEnd, cEnd);
            const AudioData& d = *c.data;
            const Frame srcFrames = d.numFrames();
            const float* s0 = d.channels[0].data();
            const float* s1 = d.numChannels() > 1 ? d.channels[1].data() : s0;
            for (Frame x = from; x < to; ++x)
            {
                const Frame rel = x - c.start;
                const Frame src = c.sourceOffset + rel;
                if (src < 0 || src >= srcFrames) continue;
                float gn = c.gain;
                if (rel < c.fadeIn) gn *= (float) rel / (float) c.fadeIn;
                if (c.fadeOut > 0 && rel >= c.length - c.fadeOut) gn *= (float) (c.length - 1 - rel) / (float) c.fadeOut;
                const int i = (int) (x - position);
                tl[i] += s0[src] * gn;
                tr[i] += s1[src] * gn;
            }
            any = true;
        }
        if (t.instrument) { renderInstrument (t, position, true, tl, tr, n); any = true; }

        float* io[2] = { tl, tr };
        for (const auto& fx : t.inserts) fx->process (io, 2, n);

        TrackParams& p = params[(size_t) t.slot];
        const bool audible = ! p.mute.load (std::memory_order_relaxed) && (! anySolo || p.solo.load (std::memory_order_relaxed));
        const float gn = audible ? p.gain.load (std::memory_order_relaxed) : 0.0f;
        const float pan = std::clamp (p.pan.load (std::memory_order_relaxed), -1.0f, 1.0f);
        const float targetL = gn * (pan > 0 ? 1.0f - pan : 1.0f);
        const float targetR = gn * (pan < 0 ? 1.0f + pan : 1.0f);
        float& gl = smGainL[(size_t) t.slot];
        float& gr = smGainR[(size_t) t.slot];
        float peak = 0.0f;
        if (! any && t.inserts.empty())
        {
            // Sin audio: solo avanzamos el suavizado (mismo resultado que procesar ceros).
            for (int i = 0; i < n; ++i) { gl = approach (gl, targetL, smoothStep); gr = approach (gr, targetR, smoothStep); }
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                gl = approach (gl, targetL, smoothStep);
                gr = approach (gr, targetR, smoothStep);
                const float l = tl[i] * gl, r = tr[i] * gr;
                L[i] += l;
                R[i] += r;
                peak = std::max (peak, std::max (std::fabs (l), std::fabs (r)));
            }
        }
        p.peak.store (peak, std::memory_order_relaxed);
    }

    // Master
    const float mt = masterGain.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        smMaster = approach (smMaster, mt, smoothStep);
        L[i] *= smMaster;
        R[i] *= smMaster;
    }
}

void Engine::process (const float* input, int inChannels, float* output, int outChannels, int frames)
{
    adoptPendingGraph();
    const RenderGraph* g = active;
    const Frame s = seekRequest.exchange (-1, std::memory_order_acq_rel);
    if (s >= 0) { pos = s; notesOff (g, false); }
    drainLive (g);

    float mPeak = 0.0f;
    int done = 0;
    while (done < frames)
    {
        int n = std::min (maxBlock, frames - done);
        const bool rolling = playing.load (std::memory_order_relaxed);
        if (prevRolling && ! rolling) notesOff (g, false); // stop: las notas pasan a release
        prevRolling = rolling;
        const bool looping = rolling && loopOn.load (std::memory_order_relaxed) && ! recording.load (std::memory_order_relaxed);
        const Frame lEnd = loopEnd.load (std::memory_order_relaxed);
        if (looping && pos < lEnd && pos + n > lEnd) n = (int) (lEnd - pos);
        float* L = mixL.data();
        float* R = mixR.data();
        renderBlock (g, pos, rolling, L, R, n);

        // Metrónomo (solo en vivo, no en la exportación)
        if (rolling && g && metronomeOn.load (std::memory_order_relaxed) && g->bpm > 0)
        {
            const double spb = 60.0 * rate / g->bpm;
            const Frame clickLen = (Frame) clickHi.size();
            const float mg = metronomeGain.load (std::memory_order_relaxed);
            Frame k0 = (Frame) std::floor ((double) (pos - clickLen) / spb);
            if (k0 < 0) k0 = 0;
            const Frame k1 = (Frame) std::floor ((double) (pos + n - 1) / spb);
            for (Frame k = k0; k <= k1; ++k)
            {
                const Frame b = (Frame) std::llround ((double) k * spb);
                const std::vector<float>& click = (g->beatsPerBar > 0 && k % g->beatsPerBar == 0) ? clickHi : clickLo;
                const Frame from = std::max (b, pos), to = std::min (b + (Frame) click.size(), pos + n);
                for (Frame x = from; x < to; ++x)
                {
                    const float v = click[(size_t) (x - b)] * mg;
                    L[x - pos] += v;
                    R[x - pos] += v;
                }
            }
        }

        // Entrada: grabación y monitorización
        const float* in = input ? input + (size_t) done * (size_t) inChannels : nullptr;
        if (in && inChannels > 0)
        {
            SpscRing<float>* ring = recRing.load (std::memory_order_acquire);
            if (rolling && recording.load (std::memory_order_acquire) && ring)
            {
                const int rc = recChannels.load (std::memory_order_relaxed);
                for (int i = 0; i < n; ++i)
                {
                    const float a = in[(size_t) i * (size_t) inChannels];
                    const float b = inChannels > 1 ? in[(size_t) i * (size_t) inChannels + 1] : a;
                    if (rc == 1) recScratch[(size_t) i] = a;
                    else { recScratch[(size_t) i * 2] = a; recScratch[(size_t) i * 2 + 1] = b; }
                }
                if (recStart.load (std::memory_order_relaxed) < 0) recStart.store (pos, std::memory_order_release);
                if (! ring->push (recScratch.data(), (size_t) (n * rc))) recDropped.fetch_add (n, std::memory_order_relaxed);
            }
            const float mon = monitorGain.load (std::memory_order_relaxed);
            if (mon > 0.0f)
                for (int i = 0; i < n; ++i)
                {
                    const float a = in[(size_t) i * (size_t) inChannels];
                    const float b = inChannels > 1 ? in[(size_t) i * (size_t) inChannels + 1] : a;
                    L[i] += a * mon;
                    R[i] += b * mon;
                }
        }

        float* out = output + (size_t) done * (size_t) outChannels;
        for (int i = 0; i < n; ++i)
        {
            float l = std::isfinite (L[i]) ? L[i] : 0.0f;
            float r = std::isfinite (R[i]) ? R[i] : 0.0f;
            mPeak = std::max (mPeak, std::max (std::fabs (l), std::fabs (r)));
            if (outChannels == 1) out[i] = 0.5f * (l + r);
            else
            {
                out[(size_t) i * (size_t) outChannels] = l;
                out[(size_t) i * (size_t) outChannels + 1] = r;
                for (int c = 2; c < outChannels; ++c) out[(size_t) i * (size_t) outChannels + (size_t) c] = 0.0f;
            }
        }
        if (rolling)
        {
            pos += n;
            if (looping && pos == lEnd)
            {
                pos = loopStart.load (std::memory_order_relaxed);
                notesOff (g, false); // ninguna nota cruza el reinicio del bucle
            }
        }
        done += n;
    }
    positionOut.store (pos, std::memory_order_release);
    masterPeakOut.store (mPeak, std::memory_order_relaxed);
}

void Engine::renderOffline (Frame from, float* left, float* right, Frame frames)
{
    adoptPendingGraph();
    snapSmoothersToTargets();
    const RenderGraph* g = active;
    if (g)
        for (const auto& t : g->tracks)
        {
            for (const auto& fx : t.inserts) fx->reset();
            if (t.instrument) t.instrument->reset();
        }
    Frame done = 0;
    while (done < frames)
    {
        const int n = (int) std::min<Frame> (maxBlock, frames - done);
        renderBlock (g, from + done, true, left + done, right + done, n);
        done += n;
    }
}
} // namespace cs
