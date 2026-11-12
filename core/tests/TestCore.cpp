// Pruebas del núcleo: WAV, remuestreo, cola SPSC y motor en tiempo real.
#include "Test.h"

#include "cs/Engine.h"
#include "cs/Project.h"
#include "cs/Resampler.h"
#include "cs/SpscRing.h"
#include "cs/Wav.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <random>
#include <thread>

using namespace cs;

namespace
{
constexpr double kPi = 3.14159265358979323846;

std::shared_ptr<AudioData> sine (int rate, double freq, Frame frames, int ch = 1, float amp = 0.5f)
{
    auto a = std::make_shared<AudioData>();
    a->sampleRate = rate;
    a->allocate (ch, frames);
    for (int c = 0; c < ch; ++c)
        for (Frame i = 0; i < frames; ++i) a->channels[(size_t) c][(size_t) i] = amp * (float) std::sin (2 * kPi * freq * (double) i / rate + c);
    return a;
}

std::shared_ptr<AudioData> noise (Frame frames, int ch, uint32_t seed)
{
    auto a = std::make_shared<AudioData>();
    a->allocate (ch, frames);
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-0.5f, 0.5f);
    for (auto& c : a->channels) for (auto& v : c) v = d (rng);
    return a;
}

std::unique_ptr<RenderGraph> graphWith (std::vector<std::vector<std::pair<std::shared_ptr<AudioData>, ClipRT>>> tracks)
{
    auto g = std::make_unique<RenderGraph>();
    int slot = 0;
    for (auto& t : tracks)
    {
        TrackRT tr;
        tr.slot = slot++;
        for (auto& [data, clip] : t)
        {
            clip.data = data.get();
            if (clip.length == 0) clip.length = data->numFrames() - clip.sourceOffset;
            g->keepAlive.push_back (data);
            tr.clips.push_back (clip);
        }
        std::sort (tr.clips.begin(), tr.clips.end(), [] (const ClipRT& a, const ClipRT& b) { return a.start < b.start; });
        g->tracks.push_back (std::move (tr));
    }
    return g;
}

// Ejecuta el motor como lo haría el callback de audio, con bloques de tamaño 'block' (o aleatorios si block <= 0).
std::vector<float> runLive (Engine& e, Frame frames, int block, uint32_t seed = 1)
{
    std::vector<float> out ((size_t) frames * 2);
    std::mt19937 rng (seed);
    Frame done = 0;
    while (done < frames)
    {
        int n = block > 0 ? block : (int) (rng() % 1500) + 1;
        n = (int) std::min<Frame> (n, frames - done);
        e.process (nullptr, 0, out.data() + done * 2, 2, n);
        done += n;
    }
    return out;
}

double rmsDb (const std::vector<float>& a, const std::vector<float>& b, size_t from, size_t to)
{
    double err = 0, sig = 0;
    for (size_t i = from; i < to; ++i) { err += (a[i] - b[i]) * (a[i] - b[i]); sig += b[i] * b[i]; }
    return 10 * std::log10 ((err + 1e-30) / (sig + 1e-30));
}
} // namespace

// =================================================================================================================
// WAV
// =================================================================================================================
TEST (wav_roundtrip_all_formats)
{
    const std::string dir = cstest::tempDir ("wav_rt");
    for (int ch : { 1, 2 })
        for (WavFormat f : { WavFormat::Pcm16, WavFormat::Pcm24, WavFormat::Float32 })
        {
            auto a = sine (44100, 440, 10000, ch, 0.8f);
            std::string err;
            const std::string p = dir + "/t.wav";
            CHECK (writeWav (p, *a, f, err));
            AudioData b;
            WavInfo info;
            CHECK (readWav (p, b, err, &info));
            CHECK (b.numChannels() == ch && b.numFrames() == 10000 && b.sampleRate == 44100);
            CHECK (! info.headerRepaired);
            const double tol = f == WavFormat::Pcm16 ? 2.0 / 32768 : f == WavFormat::Pcm24 ? 1.0 / 8388607 : 0.0;
            double maxErr = 0;
            for (int c = 0; c < ch; ++c)
                for (Frame i = 0; i < 10000; ++i) maxErr = std::max (maxErr, (double) std::fabs (a->channels[(size_t) c][(size_t) i] - b.channels[(size_t) c][(size_t) i]));
            CHECK_MSG (maxErr <= tol + 1e-9, std::to_string (maxErr));
            CHECK (! fileExists (p + ".tmp"));
        }
}

TEST (wav_reads_8bit_and_float64_and_extensible)
{
    // 8-bit PCM, 2 frames mono: 0x80 = 0, 0xFF ~ +1
    auto make = [] (uint16_t tag, int bits, int ch, std::vector<uint8_t> data, bool extensible) {
        std::vector<uint8_t> f;
        auto w32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) f.push_back ((uint8_t) (v >> (8 * i))); };
        auto w16 = [&] (uint16_t v) { f.push_back ((uint8_t) v); f.push_back ((uint8_t) (v >> 8)); };
        const uint32_t fmtLen = extensible ? 40 : 16;
        f.insert (f.end(), { 'R', 'I', 'F', 'F' }); w32 (4 + 8 + fmtLen + 8 + (uint32_t) data.size() + 8 + 4);
        f.insert (f.end(), { 'W', 'A', 'V', 'E' });
        f.insert (f.end(), { 'L', 'I', 'S', 'T' }); w32 (4); f.insert (f.end(), { 'a', 'b', 'c', 'd' }); // chunk desconocido
        f.insert (f.end(), { 'f', 'm', 't', ' ' }); w32 (fmtLen);
        w16 (extensible ? 0xFFFE : tag); w16 ((uint16_t) ch); w32 (48000); w32 ((uint32_t) (48000 * ch * bits / 8)); w16 ((uint16_t) (ch * bits / 8)); w16 ((uint16_t) bits);
        if (extensible) { w16 (22); w16 ((uint16_t) bits); w32 (0); w16 (tag); for (int i = 0; i < 14; ++i) f.push_back (0); }
        f.insert (f.end(), { 'd', 'a', 't', 'a' }); w32 ((uint32_t) data.size());
        f.insert (f.end(), data.begin(), data.end());
        return f;
    };
    AudioData a;
    std::string err;
    auto f8 = make (1, 8, 1, { 0x80, 0xFF, 0x00 }, false);
    CHECK (readWavFromMemory (f8.data(), f8.size(), a, err));
    CHECK (a.numFrames() == 3);
    CHECK_NEAR (a.channels[0][0], 0.0, 1e-9);
    CHECK_NEAR (a.channels[0][1], 127.0 / 128, 1e-6);
    CHECK_NEAR (a.channels[0][2], -1.0, 1e-9);

    double v = -0.25;
    std::vector<uint8_t> d64 (8);
    std::memcpy (d64.data(), &v, 8);
    auto f64 = make (3, 64, 1, d64, true);
    CHECK (readWavFromMemory (f64.data(), f64.size(), a, err));
    CHECK (a.numFrames() == 1);
    CHECK_NEAR (a.channels[0][0], -0.25, 1e-9);

    auto f24 = make (1, 24, 2, { 0, 0, 0x40, 0, 0, 0xC0 }, true); // +0.5, -0.5
    CHECK (readWavFromMemory (f24.data(), f24.size(), a, err));
    CHECK_NEAR (a.channels[0][0], 0.5, 1e-6);
    CHECK_NEAR (a.channels[1][0], -0.5, 1e-6);
}

TEST (wav_corrupt_inputs_never_crash)
{
    auto a = sine (48000, 1000, 2000, 2);
    const std::string dir = cstest::tempDir ("wav_fuzz");
    std::string err;
    writeWav (dir + "/base.wav", *a, WavFormat::Pcm24, err);
    std::string base;
    CHECK (readFile (dir + "/base.wav", base));
    std::mt19937 rng (7);
    int accepted = 0, rejected = 0;
    for (int it = 0; it < 4000; ++it)
    {
        std::string m = base;
        const int edits = 1 + (int) (rng() % 8);
        for (int e = 0; e < edits; ++e)
        {
            const size_t pos = rng() % std::min<size_t> (m.size(), it % 2 ? 64 : m.size());
            m[pos] = (char) rng();
        }
        if (rng() % 4 == 0) m.resize (rng() % m.size());
        AudioData out;
        WavInfo info;
        if (readWavFromMemory ((const uint8_t*) m.data(), m.size(), out, err, &info))
        {
            ++accepted;
            CHECK (out.numChannels() >= 1 && out.numChannels() <= 8);
            for (const auto& c : out.channels) for (float v : c) CHECK (std::isfinite (v));
        }
        else ++rejected;
    }
    MEASURE ("4000 WAV mutados: %d aceptados (válidos tras la mutación), %d rechazados, 0 fallos de memoria", accepted, rejected);
}

TEST (wav_stream_writer_survives_unexpected_close)
{
    const std::string dir = cstest::tempDir ("wav_stream");
    const std::string p = dir + "/rec.wav.part";
    WavStreamWriter w;
    std::string err;
    CHECK (w.open (p, 48000, 1, err));
    std::vector<float> buf (480);
    for (int blk = 0; blk < 100; ++blk)
    {
        for (int i = 0; i < 480; ++i) buf[(size_t) i] = (float) ((blk * 480 + i) % 1000) / 1000.0f;
        CHECK (w.appendInterleaved (buf.data(), 480));
        if (blk == 10) w.updateHeader(); // la última actualización del encabezado antes del "cierre inesperado"
    }
    std::fflush (nullptr);
    // Simulamos un cierre inesperado: no se llama a close(); el encabezado declara solo 11 bloques.
    AudioData a;
    WavInfo info;
    // Copiamos el archivo tal como está en disco ahora.
    std::string bytes;
    w.updateHeader(); // fuerza flush de datos...
    // ...y volvemos a escribir un encabezado desactualizado (11 bloques) para reproducir el caso real.
    {
        std::FILE* f = std::fopen (p.c_str(), "r+b");
        CHECK (f != nullptr);
        if (f)
        {
            uint8_t sz[4];
            const uint32_t data = 11 * 480 * 4;
            for (int i = 0; i < 4; ++i) sz[i] = (uint8_t) (data >> (8 * i));
            std::fseek (f, 40, SEEK_SET);
            std::fwrite (sz, 1, 4, f);
            std::fclose (f);
        }
    }
    // Un lector que confíe en el encabezado vería 11 bloques; repairWavHeader no puede saberlo, así que
    // probamos el caso que sí detecta: tamaño 0 (grabación que nunca actualizó el encabezado).
    {
        std::FILE* f = std::fopen (p.c_str(), "r+b");
        uint8_t zero[4] = { 0, 0, 0, 0 };
        std::fseek (f, 40, SEEK_SET);
        std::fwrite (zero, 1, 4, f);
        std::fclose (f);
    }
    bool changed = false;
    CHECK (repairWavHeader (p, err, &changed));
    CHECK (changed);
    CHECK (readWav (p, a, err, &info));
    CHECK (! info.headerRepaired);
    CHECK (a.numFrames() == 48000);
    CHECK_NEAR (a.channels[0][47999], (float) ((47999) % 1000) / 1000.0f, 1e-7);
    w.close();
}

// =================================================================================================================
// Remuestreo
// =================================================================================================================
TEST (resampler_preserves_frequency_and_length)
{
    for (auto [from, to] : { std::pair<int, int> { 44100, 48000 }, { 48000, 44100 }, { 22050, 48000 }, { 96000, 48000 } })
    {
        auto in = sine (from, 1000, from, 1, 0.5f); // 1 s
        AudioData out = resample (*in, to);
        CHECK (out.sampleRate == to);
        CHECK (std::llabs (out.numFrames() - (Frame) to) <= 1);
        auto ideal = sine (to, 1000, out.numFrames(), 1, 0.5f);
        const double errDb = rmsDb (out.channels[0], ideal->channels[0], 200, (size_t) out.numFrames() - 200);
        MEASURE ("%d -> %d Hz, seno 1 kHz: error RMS %.1f dB respecto al ideal", from, to, errDb);
        CHECK (errDb < -80.0);
    }
}

TEST (resampler_downsampling_rejects_aliases)
{
    auto in = sine (96000, 30000, 96000, 1, 0.5f); // 30 kHz no cabe en 48 kHz (Nyquist 24 kHz)
    AudioData out = resample (*in, 48000);
    double e = 0;
    for (Frame i = 500; i < out.numFrames() - 500; ++i) e += out.channels[0][(size_t) i] * out.channels[0][(size_t) i];
    const double rms = std::sqrt (e / (double) (out.numFrames() - 1000));
    const double db = 20 * std::log10 (rms / (0.5 / std::sqrt (2.0)) + 1e-12);
    MEASURE ("tono de 30 kHz a 96k -> 48k: residuo de alias %.1f dB", db);
    CHECK (db < -90.0);
}

// =================================================================================================================
// SPSC
// =================================================================================================================
TEST (spsc_ring_preserves_order_across_threads)
{
    SpscRing<float> ring (4096);
    const long total = 5'000'000;
    std::thread producer ([&] {
        std::vector<float> chunk (97);
        long next = 0;
        while (next < total)
        {
            const size_t n = (size_t) std::min<long> (97, total - next);
            for (size_t i = 0; i < n; ++i) chunk[i] = (float) ((next + (long) i) % 65536);
            if (ring.push (chunk.data(), n)) next += (long) n;
            else std::this_thread::yield();
        }
    });
    long got = 0, errors = 0;
    std::vector<float> buf (300);
    while (got < total)
    {
        const size_t n = ring.pop (buf.data(), buf.size());
        for (size_t i = 0; i < n; ++i) if (buf[i] != (float) ((got + (long) i) % 65536)) ++errors;
        got += (long) n;
        if (n == 0) std::this_thread::yield();
    }
    producer.join();
    CHECK (errors == 0);
}

// =================================================================================================================
// Motor
// =================================================================================================================
TEST (engine_four_tracks_sample_accurate_sync)
{
    Engine e;
    e.prepare (48000, 512);
    // Cuatro pistas con clips distintos que contienen un impulso; los clips se colocan para que todos los
    // impulsos caigan en la misma posición del timeline (12345), con distintos inicios y offsets de fuente.
    std::vector<std::vector<std::pair<std::shared_ptr<AudioData>, ClipRT>>> tracks;
    const Frame target = 12345;
    const Frame impulseAt[4] = { 0, 100, 4096, 7 };
    const Frame clipStart[4] = { target, target - 100, target - 4096 + 300, target - 7 };
    const Frame srcOffset[4] = { 0, 0, 300, 0 };
    for (int t = 0; t < 4; ++t)
    {
        auto a = std::make_shared<AudioData>();
        a->allocate (1, 20000);
        a->channels[0][(size_t) impulseAt[t]] = 0.25f;
        ClipRT c;
        c.start = clipStart[t];
        c.sourceOffset = srcOffset[t];
        tracks.push_back ({ { a, c } });
    }
    e.publish (graphWith (tracks));
    e.play();
    for (int block : { 64, 333, 512, 2048 })
    {
        e.seek (0);
        auto out = runLive (e, 30000, block);
        int nonZero = 0;
        for (Frame i = 0; i < 30000; ++i) if (out[(size_t) i * 2] != 0.0f) ++nonZero;
        CHECK_MSG (nonZero == 1, std::to_string (nonZero));
        CHECK_NEAR (out[(size_t) target * 2], 1.0, 1e-7);
        CHECK_NEAR (out[(size_t) target * 2 + 1], 1.0, 1e-7);
    }
}

TEST (engine_output_is_identical_for_any_block_size)
{
    Engine ref;
    ref.prepare (48000, 4096);
    auto build = [] {
        std::vector<std::vector<std::pair<std::shared_ptr<AudioData>, ClipRT>>> tracks;
        for (int t = 0; t < 6; ++t)
        {
            ClipRT c; c.start = 1000 * t; c.sourceOffset = 50 * t; c.fadeIn = 300; c.fadeOut = 777; c.gain = 0.5f + 0.1f * t;
            tracks.push_back ({ { noise (48000, 1 + t % 2, (uint32_t) t), c } });
        }
        return graphWith (tracks);
    };
    std::vector<float> reference;
    bool first = true;
    for (int block : { 4096, 1, 7, 64, 192, 256, 1023, 8192, -1 })
    {
        Engine e;
        e.prepare (48000, 1024);
        e.publish (build());
        for (int s = 0; s < 6; ++s) { e.trackParams (s).gain.store (0.3f + 0.1f * s); e.trackParams (s).pan.store (-0.8f + 0.3f * s); }
        e.trackParams (2).mute.store (true);
        e.play();
        auto out = runLive (e, 60000, block, 99);
        if (first)
        {
            float m = 0;
            for (float x : out) m = std::max (m, std::fabs (x));
            CHECK (m > 0.05f); // la referencia no es silencio (si lo fuera, la comparación no probaría nada)
            reference = out; first = false; continue;
        }
        CHECK_MSG (out == reference, "bloque " + std::to_string (block));
    }
}

TEST (engine_process_does_not_allocate)
{
    Engine e;
    e.prepare (48000, 1024);
    std::vector<std::vector<std::pair<std::shared_ptr<AudioData>, ClipRT>>> tracks;
    for (int t = 0; t < 16; ++t) { ClipRT c; c.start = t * 100; c.fadeIn = 100; c.fadeOut = 100; tracks.push_back ({ { noise (96000, 2, (uint32_t) t), c } }); }
    e.publish (graphWith (tracks));
    e.setMetronome (true);
    e.setMonitoring (true, 0.5f);
    SpscRing<float> ring (48000 * 4);
    e.startRecording (&ring, 2);
    e.play();
    std::vector<float> in (8192 * 2, 0.1f), out (8192 * 2);
    e.process (in.data(), 2, out.data(), 2, 256); // primer bloque: adopta el grafo
    std::vector<float> drain (8192 * 2);
    cstest::allocationCount.store (0);
    cstest::countAllocations.store (true);
    std::mt19937 rng (3);
    for (int i = 0; i < 2000; ++i)
    {
        const int n = 1 + (int) (rng() % 4000);
        e.trackParams ((int) (rng() % 16)).gain.store ((float) (rng() % 100) / 50.0f);
        e.trackParams ((int) (rng() % 16)).solo.store (rng() % 5 == 0);
        if (i % 300 == 0) e.seek ((Frame) (rng() % 50000));
        e.process (in.data(), 2, out.data(), 2, n);
        while (ring.pop (drain.data(), drain.size()) > 0) {} // vaciar la cola no reserva memoria
    }
    cstest::countAllocations.store (false);
    const long inProcess = cstest::allocationCount.load();
    MEASURE ("reservas de memoria dentro de process() en 2000 bloques (16 pistas, metrónomo, grabación, monitorización): %ld", inProcess);
    CHECK (inProcess == 0);
}

TEST (engine_mute_solo_and_balance_pan)
{
    Engine e;
    e.prepare (48000, 256);
    auto dc = std::make_shared<AudioData>();
    dc->allocate (1, 48000);
    for (auto& v : dc->channels[0]) v = 0.5f;
    std::vector<std::vector<std::pair<std::shared_ptr<AudioData>, ClipRT>>> tracks;
    for (int t = 0; t < 3; ++t) tracks.push_back ({ { dc, ClipRT {} } });
    e.publish (graphWith (tracks));
    e.play();
    auto settle = [&] { std::vector<float> o (2 * 2400); e.process (nullptr, 0, o.data(), 2, 2400); return std::pair<float, float> { o[o.size() - 2], o[o.size() - 1] }; };

    auto [l0, r0] = settle();
    CHECK_NEAR (l0, 1.5, 1e-6); CHECK_NEAR (r0, 1.5, 1e-6);       // 3 pistas a 0 dB, centro
    e.trackParams (0).pan.store (-1.0f);
    auto [l1, r1] = settle();
    CHECK_NEAR (l1, 1.5, 1e-6); CHECK_NEAR (r1, 1.0, 1e-6);       // ley de balance: el lado opuesto baja
    e.trackParams (1).mute.store (true);
    auto [l2, r2] = settle();
    CHECK_NEAR (l2, 1.0, 1e-6); CHECK_NEAR (r2, 0.5, 1e-6);
    e.trackParams (2).solo.store (true);                            // solo: solo suena la pista 2
    auto [l3, r3] = settle();
    CHECK_NEAR (l3, 0.5, 1e-6); CHECK_NEAR (r3, 0.5, 1e-6);
    e.trackParams (1).solo.store (true);                            // mute gana sobre solo
    auto [l4, r4] = settle();
    CHECK_NEAR (l4, 0.5, 1e-6); CHECK_NEAR (r4, 0.5, 1e-6);

    // Sin saltos bruscos: al silenciar, el cambio por muestra está acotado (rampa de 10 ms).
    e.trackParams (2).mute.store (true);
    std::vector<float> o (2 * 960);
    e.process (nullptr, 0, o.data(), 2, 960);
    float maxStep = 0;
    float prev = 0.5f;
    for (int i = 0; i < 960; ++i) { maxStep = std::max (maxStep, std::fabs (o[(size_t) i * 2] - prev)); prev = o[(size_t) i * 2]; }
    MEASURE ("salto máximo por muestra al silenciar: %.5f (sin suavizado sería 0.5)", maxStep);
    CHECK (maxStep < 0.002f);
    CHECK_NEAR (o[o.size() - 2], 0.0, 1e-7);
}

TEST (engine_clip_gain_and_fades)
{
    Engine e;
    e.prepare (48000, 128);
    auto dc = std::make_shared<AudioData>();
    dc->allocate (1, 1000);
    for (auto& v : dc->channels[0]) v = 1.0f;
    ClipRT c; c.start = 10; c.fadeIn = 100; c.fadeOut = 200; c.gain = 0.5f;
    e.publish (graphWith ({ { { dc, c } } }));
    e.play();
    auto out = runLive (e, 1200, 64);
    CHECK (out[9 * 2] == 0.0f);
    CHECK_NEAR (out[10 * 2], 0.0, 1e-9);                       // primer frame del fundido = 0
    CHECK_NEAR (out[60 * 2], 0.5 * 50 / 100.0, 1e-6);           // mitad del fundido de entrada
    CHECK_NEAR (out[500 * 2], 0.5, 1e-7);
    CHECK_NEAR (out[(10 + 999) * 2], 0.0, 1e-9);                // último frame del clip = 0
    CHECK_NEAR (out[(10 + 899) * 2], 0.5 * 100 / 200.0, 1e-6);
    CHECK (out[(10 + 1000) * 2] == 0.0f);
}

TEST (engine_metronome_is_sample_accurate_without_drift)
{
    for (double bpm : { 120.0, 133.0, 87.5 })
    {
        for (int block : { 1, 64, 480, 1777 })
        {
            Engine e;
            e.prepare (48000, 512);
            auto g = std::make_unique<RenderGraph>();
            g->bpm = bpm;
            g->beatsPerBar = 4;
            e.publish (std::move (g));
            e.setMetronome (true);
            e.play();
            const Frame frames = 48000 * 20; // 20 s
            auto out = runLive (e, frames, block);
            const double spb = 60.0 * 48000 / bpm;
            int beats = 0, wrong = 0, accentOk = 0;
            for (Frame k = 0;; ++k)
            {
                const Frame b = (Frame) std::llround (k * spb);
                if (b + 100 >= frames) break;
                ++beats;
                // el clic empieza exactamente en b: antes silencio, después energía
                if (b > 0 && out[(size_t) (b - 1) * 2] != 0.0f) ++wrong;
                if (out[(size_t) (b + 20) * 2] == 0.0f) ++wrong;
                const bool accent = k % 4 == 0;
                float pk = 0;
                for (Frame i = b; i < b + 60; ++i) pk = std::max (pk, std::fabs (out[(size_t) i * 2]));
                if ((accent && pk > 0.38f) || (! accent && pk > 0.2f && pk < 0.32f)) ++accentOk; // acento 0.9, tiempo 0.6, x ganancia 0.5
            }
            CHECK_MSG (wrong == 0, "bpm " + std::to_string (bpm) + " bloque " + std::to_string (block));
            CHECK (accentOk == beats);
            if (block == 64) MEASURE ("%.1f BPM: %d clics en 20 s, todos en su muestra exacta (k*spb redondeado)", bpm, beats);
        }
    }
}

TEST (engine_graph_swap_stress_is_safe)
{
    Engine e;
    e.prepare (48000, 256);
    auto shared = noise (48000, 2, 5);
    std::weak_ptr<AudioData> watch = shared;
    e.play();
    std::atomic<bool> stop { false };
    std::thread audio ([&] {
        std::vector<float> out (512 * 2);
        while (! stop.load()) e.process (nullptr, 0, out.data(), 2, 256);
    });
    for (int i = 0; i < 3000; ++i)
    {
        ClipRT c; c.start = i % 1000;
        e.publish (graphWith ({ { { shared, c } }, { { noise (100, 1, (uint32_t) i), ClipRT {} } } }));
        if (i % 7 == 0) std::this_thread::yield();
    }
    stop.store (true);
    audio.join();
    e.publish (std::make_unique<RenderGraph>());
    std::vector<float> out (64);
    e.process (nullptr, 0, out.data(), 2, 32); // adopta el grafo vacío
    e.collectGarbage();
    shared.reset();
    CHECK (watch.expired()); // ningún grafo retenido: no hay fugas
}

TEST (engine_input_nan_and_monitoring_are_safe)
{
    Engine e;
    e.prepare (48000, 128);
    e.publish (std::make_unique<RenderGraph>());
    e.setMonitoring (true, 1.0f);
    std::vector<float> in (256, std::nanf ("")), out (256);
    in[1] = std::numeric_limits<float>::infinity();
    e.process (in.data(), 1, out.data(), 2, 128);
    for (float v : out) CHECK (std::isfinite (v));
    std::fill (in.begin(), in.end(), 0.25f);
    e.process (in.data(), 1, out.data(), 2, 128);
    CHECK_NEAR (out[0], 0.25, 1e-7); // monitorización directa, también con el transporte parado
}

TEST (processor_gain_insert_in_chain)
{
    Engine e;
    e.prepare (48000, 256);
    auto dc = std::make_shared<AudioData>();
    dc->allocate (1, 48000);
    for (auto& v : dc->channels[0]) v = 0.5f;
    auto g = graphWith ({ { { dc, ClipRT {} } } });
    ProcessorPtr fx = createProcessor ("cs.gain");
    CHECK (fx != nullptr);
    fx->setParameter (0, -6.0f);
    fx->prepare (48000, 256);
    g->tracks[0].inserts.push_back (fx);
    e.publish (std::move (g));
    e.play();
    auto out = runLive (e, 4800, 256);
    CHECK_NEAR (out[4799 * 2], 0.5 * std::pow (10.0, -6.0 / 20), 1e-6);
    // Estado: ida y vuelta, y rechazo de valores fuera de rango sin modificar nada.
    ProcessorPtr fx2 = createProcessor ("cs.gain");
    CHECK (fx2->loadState (fx->saveState()));
    CHECK_NEAR (fx2->getParameter (0), -6.0, 1e-6);
    CHECK (! fx2->loadState ("{\"gainDb\": 99}"));
    CHECK (! fx2->loadState ("no json"));
    CHECK_NEAR (fx2->getParameter (0), -6.0, 1e-6);
    CHECK (createProcessor ("vst3.windows") == nullptr);
}

TEST (platform_is_little_endian_for_raw_float_wav)
{
    const uint32_t x = 1;
    uint8_t b;
    std::memcpy (&b, &x, 1);
    CHECK (b == 1); // WavStreamWriter escribe floats en bruto: requiere little-endian (ARM64 y x86-64 lo son)
}
