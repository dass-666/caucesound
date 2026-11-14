#include "cs/Engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <thread>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

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

Engine::Engine() : params ((size_t) kMaxTracks)
{
    for (auto& p : params) for (auto& v : p.send) v.store (0.0f);
}

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
    busL.assign ((size_t) (kMaxBuses * maxBlock), 0.0f);
    busR.assign ((size_t) (kMaxBuses * maxBlock), 0.0f);
    smSend.assign ((size_t) (kMaxTracks * kMaxBuses), 0.0f);
    smoothStep = (float) (1.0 / (0.010 * rate)); // de 0 a 1 en 10 ms
    makeClick (clickHi, rate, 1760.0, 0.9f);
    makeClick (clickLo, rate, 1175.0, 0.6f);
}

void Engine::publish (std::unique_ptr<RenderGraph> graph)
{
    collectGarbage();
    if (graph)
        for (auto& t : graph->tracks)
        {
            t.latency = 0;
            for (const auto& fx : t.inserts) if (fx) t.latency += std::max (0, fx->latencySamples());
        }
    // Si el hilo de audio no llegó a tomar el anterior, nunca lo vio: podemos liberarlo aquí.
    delete pending.exchange (graph.release(), std::memory_order_acq_rel);
}

void Engine::collectGarbage()
{
    RenderGraph* g = nullptr;
    while (trash.pop (&g, 1) == 1) delete g;
}

bool Engine::settle (int timeoutMs)
{
    const auto start = std::chrono::steady_clock::now();
    const auto deadline = start + std::chrono::milliseconds (std::max (0, timeoutMs));
    const uint32_t beat = blocksSeen.load (std::memory_order_relaxed);
    while (pending.load (std::memory_order_acquire) != nullptr)
    {
        const auto now = std::chrono::steady_clock::now();
        // Sin latido en 20 ms el audio está parado (o no hay hilo de audio, como en las herramientas offline): no
        // tiene sentido retener al hilo de control; el grafo anterior se liberará cuando el audio vuelva a procesar.
        const bool silent = blocksSeen.load (std::memory_order_relaxed) == beat && now - start >= std::chrono::milliseconds (20);
        if (now >= deadline || silent) { collectGarbage(); return false; }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    collectGarbage();
    return true;
}

void Engine::adoptPendingGraph()
{
    blocksSeen.fetch_add (1, std::memory_order_relaxed);
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

void Engine::resetTransport()
{
    countInRequest.store (-1);
    playing.store (false);
    loopOn.store (false);
    liveEpoch.fetch_add (1, std::memory_order_acq_rel);
    seekRequest.store (0);
    positionOut.store (0, std::memory_order_release);
    countInOut.store (0, std::memory_order_relaxed);
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
    const LiveMidi m { slot, status, data1, data2, liveEpoch.load (std::memory_order_acquire) };
    const bool ok = live.push (&m, 1);
    liveLock.clear (std::memory_order_release);
    return ok;
}

void Engine::notesOff (const RenderGraph* g, bool hard)
{
    if (! g) return;
    for (const auto& t : g->tracks) if (t.instrument) t.instrument->allNotesOff (hard);
}

void Engine::loopNotesOff (const RenderGraph* g)
{
    // Pistas sin latencia: reinicio del bucle. Las latentes ya soltaron sus notas en su propio punto (renderBlock).
    if (! g) return;
    for (const auto& t : g->tracks) if (t.instrument && t.latency == 0) t.instrument->allNotesOff (false);
}

void Engine::drainLive (const RenderGraph* g)
{
    LiveMidi m;
    const uint32_t epoch = liveEpoch.load (std::memory_order_acquire);
    while (live.pop (&m, 1) == 1)
    {
        if (! g || m.epoch != epoch) continue; // del proyecto anterior (p. ej. un pad pulsado al salir)
        for (const auto& t : g->tracks)
            if (t.slot == m.slot && t.instrument)
            {
                const MidiEvent ev { 0, m.status, m.d1, m.d2 };
                t.instrument->handleMidi (&ev, 1);
            }
    }
}

void Engine::renderInstrument (const TrackRT& t, Frame position, bool rolling, float* tl, float* tr, int n, int busOffset)
{
    Instrument& inst = *t.instrument;
    int cursor = 0;
    // Carriles con canal propio (Channel Rack): cada process() escribe esos carriles en la entrada de su bus, en la
    // misma posición del bloque. Sin enrutar (o con latencia en la pista) todo sale por la pista, como siempre.
    const bool routed = t.latency == 0 && t.hasLaneRoutes() && inst.auxLanes() > 0;
    auto aim = [&] (int at)
    {
        if (! routed) return;
        for (int l = 0; l < kMaxAuxLanes && l < inst.auxLanes(); ++l)
        {
            const int b = t.laneBus[l];
            if (b >= 0 && b < kMaxBuses) inst.setAuxOut (l, busL.data() + (size_t) b * (size_t) maxBlock + busOffset + at, busR.data() + (size_t) b * (size_t) maxBlock + busOffset + at);
            else inst.setAuxOut (l, nullptr, nullptr);
        }
    };
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
                aim (cursor);
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
        aim (cursor);
        inst.process (io, 2, n - cursor);
    }
    if (routed) for (int l = 0; l < kMaxAuxLanes && l < inst.auxLanes(); ++l) inst.setAuxOut (l, nullptr, nullptr);
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
        for (int b = 0; b < kMaxBuses; ++b)
            smSend[(size_t) (t.slot * kMaxBuses + b)] = std::clamp (p.send[b].load (std::memory_order_relaxed), 0.0f, 1.0f);
    }
}

// Suma la salida ya mezclada de una pista (post-fader) a los buses a los que envía.
void Engine::addSends (const TrackRT& t, const float* l, const float* r, int n, bool smooth)
{
    if (t.busIndex >= 0) return; // un bus no envía a otros buses (sin realimentación)
    const TrackParams& p = params[(size_t) t.slot];
    for (int b = 0; b < kMaxBuses; ++b)
    {
        float& sm = smSend[(size_t) (t.slot * kMaxBuses + b)];
        const float target = std::clamp (p.send[b].load (std::memory_order_relaxed), 0.0f, 1.0f);
        if (sm == 0.0f && target == 0.0f) continue;
        float* bl = busL.data() + (size_t) b * (size_t) maxBlock;
        float* br = busR.data() + (size_t) b * (size_t) maxBlock;
        for (int i = 0; i < n; ++i)
        {
            if (smooth) sm = approach (sm, target, smoothStep);
            bl[i] += l[i] * sm;
            br[i] += r[i] * sm;
        }
        if (! smooth) sm = target;
    }
}

// Suma a tl/tr (desde 'offset') el audio de los clips de la pista entre 'from' y 'from + count'. Devuelve si había clips.
bool Engine::renderClipAudio (const TrackRT& t, Frame from, int count, float* tl, float* tr, int offset)
{
    bool any = false;
    const Frame to = from + count;
    for (const auto& c : t.clips)
    {
        if (c.start >= to) break; // ordenados por inicio
        const Frame cEnd = c.start + c.length;
        if (cEnd <= from || ! c.data) continue;
        const Frame a0 = std::max (from, c.start), a1 = std::min (to, cEnd);
        const AudioData& d = *c.data;
        const Frame srcFrames = d.numFrames();
        const float* s0 = d.channels[0].data();
        const float* s1 = d.numChannels() > 1 ? d.channels[1].data() : s0;
        for (Frame x = a0; x < a1; ++x)
        {
            const Frame rel = x - c.start;
            const Frame src = c.sourceOffset + rel;
            if (src < 0 || src >= srcFrames) continue;
            float gn = c.gain;
            if (rel < c.fadeIn) gn *= (float) rel / (float) c.fadeIn;
            if (c.fadeOut > 0 && rel >= c.length - c.fadeOut) gn *= (float) (c.length - 1 - rel) / (float) c.fadeOut;
            const int i = offset + (int) (x - from);
            tl[i] += s0[src] * gn;
            tr[i] += s1[src] * gn;
        }
        any = true;
    }
    return any;
}

// 0.10 (experimental, solo afecta a pistas con inserciones que declaran latencia; hoy, únicamente la prueba de concepto
// oculta de VocalShift en vivo). El PDC hace que una pista con latencia L lea su contenido L frames por delante. Al
// pulsar Play o al saltar, las inserciones están vacías (o, peor, guardan audio del punto anterior): sin esto, los
// primeros L frames tras el arranque salían en silencio y, tras un salto, sonaba una cola del sitio anterior. Aquí se
// reinician y se «ceban» con el audio de [pos - calentamiento, pos + L) descartando la salida, de modo que el primer
// bloque audible ya es el contenido de 'pos'. Cuesta procesar (calentamiento + L) frames de golpe en ese bloque: es
// una ráfaga de CPU que hay que medir en el teléfono (ver docs/VOCALSHIFT_LIVE_STUDY.md). Las notas MIDI de pistas de
// instrumento con latencia no se ceban (se procesaría silencio).
void Engine::primeLatentTracks (const RenderGraph* g, Frame position)
{
    if (! g) return;
    float* tl = trackL.data();
    float* tr = trackR.data();
    for (const auto& t : g->tracks)
    {
        if (t.busIndex >= 0 || t.latency <= 0 || t.inserts.empty()) continue;
        for (const auto& fx : t.inserts) fx->reset();
        const Frame to = position + t.latency;
        Frame from = position - std::min<Frame> (t.latency, (Frame) rate / 4);
        while (from < to)
        {
            const int n = (int) std::min<Frame> (maxBlock, to - from);
            std::fill (tl, tl + n, 0.0f);
            std::fill (tr, tr + n, 0.0f);
            renderClipAudio (t, from, n, tl, tr, 0);
            float* io[2] = { tl, tr };
            for (const auto& fx : t.inserts) fx->process (io, 2, n);
            from += n;
        }
        primesOut.fetch_add (1, std::memory_order_relaxed);
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
        // Parado: solo suenan los instrumentos (pads, teclado, colas de release) y los buses (colas de la reverb
        // compartida), con la mezcla ya aplicada.
        bool anySoloStopped = false;
        for (const auto& t : g->tracks) anySoloStopped |= t.busIndex < 0 && params[(size_t) t.slot].solo.load (std::memory_order_relaxed);
        for (int b = 0; b < kMaxBuses; ++b)
        {
            std::fill_n (busL.data() + (size_t) b * (size_t) maxBlock, n, 0.0f);
            std::fill_n (busR.data() + (size_t) b * (size_t) maxBlock, n, 0.0f);
        }
        for (int pass = 0; pass < 2; ++pass)
            for (const auto& t : g->tracks)
            {
                const bool isBus = t.busIndex >= 0;
                if (pass == 0 ? (isBus || ! t.instrument) : ! isBus) continue;
                float* tl = trackL.data();
                float* tr = trackR.data();
                if (isBus)
                {
                    std::copy_n (busL.data() + (size_t) t.busIndex * (size_t) maxBlock, n, tl);
                    std::copy_n (busR.data() + (size_t) t.busIndex * (size_t) maxBlock, n, tr);
                }
                else
                {
                    std::fill (tl, tl + n, 0.0f);
                    std::fill (tr, tr + n, 0.0f);
                    renderInstrument (t, position, false, tl, tr, n);
                }
                float* io[2] = { tl, tr };
                for (const auto& fx : t.inserts) fx->process (io, 2, n);
                const float gl = smGainL[(size_t) t.slot], gr = smGainR[(size_t) t.slot];
                float peak = 0.0f;
                for (int i = 0; i < n; ++i)
                {
                    tl[i] *= gl;
                    tr[i] *= gr;
                    L[i] += tl[i] * smMaster;
                    R[i] += tr[i] * smMaster;
                    peak = std::max (peak, std::max (std::fabs (tl[i] * smMaster), std::fabs (tr[i] * smMaster)));
                }
                addSends (t, tl, tr, n, false);
                params[(size_t) t.slot].peak.store (peak, std::memory_order_relaxed);
            }
        (void) anySoloStopped;
        return;
    }
    // Al arrancar el transporte, la mezcla empieza ya en su valor (sin rampa desde 0), igual que la exportación.
    if (! wasRolling) { snapSmoothersToTargets(); wasRolling = true; }

    bool anySolo = false;
    for (const auto& t : g->tracks) anySolo |= (t.busIndex < 0 || t.laneChannel) && params[(size_t) t.slot].solo.load (std::memory_order_relaxed);
    for (int b = 0; b < kMaxBuses; ++b)
    {
        std::fill_n (busL.data() + (size_t) b * (size_t) maxBlock, n, 0.0f);
        std::fill_n (busR.data() + (size_t) b * (size_t) maxBlock, n, 0.0f);
    }

    float* tl = trackL.data();
    float* tr = trackR.data();
    // Dos pasadas: primero las pistas normales (que envían a los buses), después los buses.
    for (int pass = 0; pass < 2; ++pass)
    for (const auto& t : g->tracks)
    {
        if ((t.busIndex >= 0) != (pass == 1)) continue;
        std::fill (tl, tl + n, 0.0f);
        std::fill (tr, tr + n, 0.0f);
        bool any = false;
        if (t.busIndex >= 0)
        {
            std::copy_n (busL.data() + (size_t) t.busIndex * (size_t) maxBlock, n, tl);
            std::copy_n (busR.data() + (size_t) t.busIndex * (size_t) maxBlock, n, tr);
            any = true;
        }
        // PDC: la pista con latencia L lee su contenido L frames por delante (ver TrackRT::latency). Con el bucle
        // activo, la lectura adelantada también da la vuelta (y suelta sus notas en su propio punto de reinicio).
        auto renderContent = [&] (Frame from, int count, int offset)
        {
            any = renderClipAudio (t, from, count, tl, tr, offset) || any;
            if (t.instrument) { renderInstrument (t, from, true, tl + offset, tr + offset, count, offset); any = true; }
        };
        if (t.busIndex >= 0) {}
        else if (t.latency == 0 || ! blockLooping || position >= blockLoopEnd)
            renderContent (position + t.latency, n, 0);
        else
        {
            const Frame len = blockLoopEnd - blockLoopStart;
            Frame rp = position + t.latency;
            while (rp >= blockLoopEnd && len > 0) rp -= len;
            int done = 0;
            while (done < n)
            {
                const int k = (int) std::min<Frame> (n - done, blockLoopEnd - rp);
                renderContent (rp, k, done);
                done += k;
                rp += k;
                if (rp >= blockLoopEnd && len > 0)
                {
                    rp = blockLoopStart;
                    if (t.instrument) t.instrument->allNotesOff (false);
                }
            }
        }

        float* io[2] = { tl, tr };
        for (const auto& fx : t.inserts) fx->process (io, 2, n);

        TrackParams& p = params[(size_t) t.slot];
        // Los buses no se silencian por el solo de otra pista (si no, el solo quitaría la reverb compartida).
        const bool audible = ! p.mute.load (std::memory_order_relaxed)
                             && ((t.busIndex >= 0 && ! t.laneChannel) || ! anySolo || p.solo.load (std::memory_order_relaxed));
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
            std::fill (tl, tl + n, 0.0f);
            std::fill (tr, tr + n, 0.0f);
            addSends (t, tl, tr, n, true); // avanza el suavizado de los envíos
        }
        else
        {
            const float direct = t.sendsOnly ? 0.0f : 1.0f; // exportación de retornos de bus: solo alimenta envíos
            for (int i = 0; i < n; ++i)
            {
                gl = approach (gl, targetL, smoothStep);
                gr = approach (gr, targetR, smoothStep);
                tl[i] *= gl;
                tr[i] *= gr;
                L[i] += tl[i] * direct;
                R[i] += tr[i] * direct;
                peak = std::max (peak, std::max (std::fabs (tl[i]), std::fabs (tr[i])));
            }
            addSends (t, tl, tr, n, true);
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

namespace
{
// Desnormales a cero mientras se procesa (FTZ/DAZ en x86, FZ en ARM64) y se restaura al salir. Las colas de reverb,
// filtros y voces decaen hacia números desnormales, que en x86 cuestan decenas de veces más ciclos (picos de CPU
// justo cuando todo está en silencio). Se usa igual en vivo y en la exportación, para que ambas den lo mismo.
struct ScopedNoDenormals
{
#if defined(__aarch64__)
    uint64_t saved = 0;
    ScopedNoDenormals() { asm volatile ("mrs %0, fpcr" : "=r"(saved)); const uint64_t v = saved | (1ull << 24); asm volatile ("msr fpcr, %0" : : "r"(v)); }
    ~ScopedNoDenormals() { asm volatile ("msr fpcr, %0" : : "r"(saved)); }
#elif defined(__SSE__) || defined(_M_X64)
    unsigned int saved = 0;
    ScopedNoDenormals() : saved (_mm_getcsr()) { _mm_setcsr (saved | 0x8040); } // FTZ | DAZ
    ~ScopedNoDenormals() { _mm_setcsr (saved); }
#endif
};
} // namespace

void Engine::noteDspLoad (double seconds, int frames)
{
    if (frames <= 0 || rate <= 0) return;
    const float load = (float) (seconds * rate / frames);
    const float a = std::min (1.0f, (float) frames / (float) rate); // media móvil de ~1 s
    dspLoadAvg.store (dspLoadAvg.load (std::memory_order_relaxed) * (1.0f - a) + load * a, std::memory_order_relaxed);
    if (load > dspLoadPeak.load (std::memory_order_relaxed)) dspLoadPeak.store (load, std::memory_order_relaxed);
    if (load > 1.0f) dspOver.fetch_add (1, std::memory_order_relaxed);
}

void Engine::process (const float* input, int inChannels, float* output, int outChannels, int frames)
{
    // Medición de carga: steady_clock (vDSO, sin llamadas bloqueantes) al entrar y al salir.
    struct LoadTimer
    {
        Engine& e; int n; std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
        ~LoadTimer() { e.noteDspLoad (std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), n); }
    } loadTimer { *this, frames };
    ScopedNoDenormals noDenormals;
    adoptPendingGraph();
    const RenderGraph* g = active;
    const Frame s = seekRequest.exchange (-1, std::memory_order_acq_rel);
    if (s >= 0) { pos = s; notesOff (g, false); needPrime = true; }
    drainLive (g);

    float mPeak = 0.0f;
    int done = 0;
    while (done < frames)
    {
        int n = std::min (maxBlock, frames - done);
        const bool transportOn = playing.load (std::memory_order_relaxed);
        // Cuenta previa: mientras dura, el transporte está «en marcha» para la interfaz pero el motor no avanza,
        // no reproduce el arreglo ni graba; solo suenan los clics (y los instrumentos tocados en vivo).
        if (transportOn)
        {
            const Frame ci = countInRequest.exchange (-1, std::memory_order_acq_rel);
            if (ci > 0) { countInLeft = ci; countInTotal = ci; }
        }
        else countInLeft = 0;
        const bool counting = countInLeft > 0;
        if (counting && (Frame) n > countInLeft) n = (int) countInLeft;
        const bool rolling = transportOn && ! counting;
        if (prevRolling && ! rolling) notesOff (g, false); // stop: las notas pasan a release
        if (rolling && (! prevRolling || needPrime)) { primeLatentTracks (g, pos); needPrime = false; } // arranque o salto
        prevRolling = rolling;
        const bool looping = rolling && loopOn.load (std::memory_order_relaxed) && ! recording.load (std::memory_order_relaxed);
        const Frame lEnd = loopEnd.load (std::memory_order_relaxed);
        if (looping && pos >= lEnd)
        {
            // Cabezal por detrás de la región (seek, bucle acortado o activado tarde): vuelve al inicio del bucle en
            // lugar de reproducir silencio indefinidamente más allá del contenido.
            pos = loopStart.load (std::memory_order_relaxed);
            loopNotesOff (g);
        }
        if (looping && pos < lEnd && pos + n > lEnd) n = (int) (lEnd - pos);
        float* L = mixL.data();
        float* R = mixR.data();
        blockLooping = looping;
        blockLoopStart = loopStart.load (std::memory_order_relaxed);
        blockLoopEnd = lEnd;
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

        if (counting && g && g->bpm > 0)
        {
            // Mismos clics que el metrónomo, contados desde el inicio de la cuenta; suenan aunque el metrónomo esté
            // apagado (para eso se pidió la cuenta) y nunca más bajos que su volumen por defecto.
            const double spb = 60.0 * rate / g->bpm;
            const Frame at = countInTotal - countInLeft;
            const float mg = std::max (0.5f, metronomeGain.load (std::memory_order_relaxed));
            Frame k0 = (Frame) std::floor ((double) (at - (Frame) clickHi.size()) / spb);
            if (k0 < 0) k0 = 0;
            const Frame k1 = (Frame) std::floor ((double) (at + n - 1) / spb);
            for (Frame k = k0; k <= k1; ++k)
            {
                const Frame b = (Frame) std::llround ((double) k * spb);
                const std::vector<float>& click = (g->beatsPerBar > 0 && k % g->beatsPerBar == 0) ? clickHi : clickLo;
                const Frame from = std::max (b, at), to = std::min (b + (Frame) click.size(), at + n);
                for (Frame x = from; x < to; ++x)
                {
                    const float v = click[(size_t) (x - b)] * mg;
                    L[x - at] += v;
                    R[x - at] += v;
                }
            }
        }

        // Modo bocinas: atenúa lo que sale por el altavoz mientras se graba (solo la salida en vivo; la grabación
        // toma el micrófono más abajo y nunca lee L/R). Rampa de ~20 ms por bloque para no chasquear.
        {
            const bool recNow = rolling && recording.load (std::memory_order_relaxed);
            const float target = recNow ? recordDuckGain.load (std::memory_order_relaxed) : 1.0f;
            if (target != 1.0f || duckNow != 1.0f)
            {
                const float k = 1.0f - std::exp (-1.0f / (0.02f * (float) rate));
                for (int i = 0; i < n; ++i)
                {
                    duckNow += (target - duckNow) * k;
                    L[i] *= duckNow;
                    R[i] *= duckNow;
                }
                if (std::fabs (duckNow - target) < 1e-4f) duckNow = target;
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
                loopNotesOff (g); // ninguna nota cruza el reinicio del bucle
            }
        }
        if (counting) countInLeft -= n;
        done += n;
    }
    countInOut.store (countInLeft, std::memory_order_relaxed);
    processedOut.fetch_add (frames, std::memory_order_relaxed);
    positionOut.store (pos, std::memory_order_release);
    masterPeakOut.store (mPeak, std::memory_order_relaxed);
}

bool Engine::renderOffline (Frame from, float* left, float* right, Frame frames, const std::atomic<bool>* cancel)
{
    ScopedNoDenormals noDenormals;
    adoptPendingGraph();
    snapSmoothersToTargets();
    const RenderGraph* g = active;
    if (g)
        for (const auto& t : g->tracks)
        {
            for (const auto& fx : t.inserts) fx->reset();
            if (t.instrument) t.instrument->reset();
        }
    primeLatentTracks (g, from); // sin esto la exportación perdía los primeros L frames de una pista con latencia
    blockLooping = false; // la exportación es lineal
    Frame done = 0;
    while (done < frames)
    {
        if (cancel && cancel->load (std::memory_order_relaxed)) return false;
        const int n = (int) std::min<Frame> (maxBlock, frames - done);
        renderBlock (g, from + done, true, left + done, right + done, n);
        done += n;
    }
    return true;
}
} // namespace cs
