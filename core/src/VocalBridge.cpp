#include "cs/VocalBridge.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>

#ifdef CS_WITH_VOCALSHIFT
#include "core/VocalChain.h"
#include "core/state/FactoryPresets.h"
#endif

namespace cs::vocal
{
using nlohmann::json;

const char* pinnedVersion() { return "vocalshift-ai-studio@10e679c"; }

std::string Params::toJson() const
{
    return json { { "preset", preset }, { "semitones", semitones }, { "character", character }, { "magic", magic },
                  { "secondVoice", secondVoice }, { "secondGainDb", secondGainDb }, { "radical", radical }, { "spatial", spatial },
                  { "formantOffset", formantOffset }, { "brightnessOffset", brightnessOffset }, { "mixPercent", mixPercent },
                  { "pan", pan }, { "offsetMs", offsetMs } }.dump();
}

bool Params::fromJson (const std::string& text, Params& out)
{
    const json j = json::parse (text, nullptr, false);
    if (j.is_discarded() || ! j.is_object()) return false;
    Params p;
    bool ok = true;
    auto num = [&] (const char* k, float& v, float lo, float hi)
    {
        if (! j.contains (k)) return;
        if (! j[k].is_number()) { ok = false; return; }
        const double d = j[k].get<double>();
        if (! std::isfinite (d)) { ok = false; return; }
        v = (float) std::clamp (d, (double) lo, (double) hi);
    };
    auto flag = [&] (const char* k, bool& v) { if (j.contains (k)) { if (j[k].is_boolean()) v = j[k].get<bool>(); else ok = false; } };
    if (j.contains ("preset") && j["preset"].is_string()) p.preset = j["preset"].get<std::string>();
    num ("semitones", p.semitones, -24, 24);
    num ("character", p.character, 0, 100);
    num ("magic", p.magic, 0, 100);
    num ("secondGainDb", p.secondGainDb, -60, 6);
    num ("formantOffset", p.formantOffset, -12, 12);
    num ("brightnessOffset", p.brightnessOffset, -12, 12);
    num ("mixPercent", p.mixPercent, 0, 100);
    num ("pan", p.pan, -100, 100);
    num ("offsetMs", p.offsetMs, 0, 100);
    flag ("secondVoice", p.secondVoice);
    flag ("radical", p.radical);
    flag ("spatial", p.spatial);
    if (ok) out = p;
    return ok;
}

#ifndef CS_WITH_VOCALSHIFT
bool available() { return false; }
std::vector<PresetInfo> presets() { return {}; }
std::string process (const AudioData&, Frame, Frame, const Params&, const Sink&, std::atomic<float>*, const std::atomic<bool>*, Stats*, double)
{
    return "VocalShift no está incluido en esta compilación";
}
#else
bool available() { return true; }

namespace
{
::vs::VoiceParams toVs (const Params& p)
{
    ::vs::VoiceParams v;
    v.mode = p.radical ? ::vs::VoiceMode::radical : ::vs::VoiceMode::keepIdentity;
    v.route = p.secondVoice ? ::vs::VoiceRoute::secondVoice : ::vs::VoiceRoute::transform;
    v.semitones = p.semitones;
    v.character = p.character;
    v.magic = p.magic;
    v.formantOffset = p.formantOffset;
    v.brightnessOffset = p.brightnessOffset;
    v.mixPercent = p.mixPercent;
    v.secondGainDb = p.secondGainDb;
    v.pan = p.pan;
    v.offsetMs = p.offsetMs;
    v.spatial = p.spatial;
    v.improved = true;
    v.autoLevel = true;   // iguala la sonoridad de la voz procesada: la comparación A/B no premia el volumen
    v.safetyClip = true;
    return v;
}

Params fromVs (const ::vs::VoiceParams& v, const std::string& id)
{
    Params p;
    p.preset = id;
    p.semitones = v.semitones + v.fineCents / 100.0f;
    p.character = v.character;
    p.magic = v.magic;
    p.secondVoice = v.route == ::vs::VoiceRoute::secondVoice;
    p.secondGainDb = v.secondGainDb;
    p.radical = v.mode == ::vs::VoiceMode::radical;
    p.spatial = v.spatial;
    p.formantOffset = v.formantOffset;
    p.brightnessOffset = v.brightnessOffset;
    p.mixPercent = v.mixPercent;
    p.pan = v.pan;
    p.offsetMs = v.offsetMs;
    return p;
}
} // namespace

std::vector<PresetInfo> presets()
{
    std::vector<PresetInfo> out;
    for (const auto& f : ::vs::factoryPresets()) out.push_back ({ f.id, f.name, f.description, fromVs (f.params, f.id) });
    return out;
}

std::string process (const AudioData& in, Frame from, Frame length, const Params& p, const Sink& sink,
                     std::atomic<float>* progress, const std::atomic<bool>* cancel, Stats* stats, double tempoBpm)
{
    const int inCh = in.numChannels();
    if (inCh < 1 || in.sampleRate < 8000) return "audio no válido";
    if (from < 0 || length <= 0 || from + length > in.numFrames()) return "rango fuera del audio";
    if ((double) length / in.sampleRate > kMaxClipSeconds) return "la toma es demasiado larga para procesarla (máximo 10 minutos)";
    const auto t0 = std::chrono::steady_clock::now();

    constexpr int block = 512;
    ::vs::VocalChain chain;
    chain.prepare ((double) in.sampleRate, 2, block);
    if (tempoBpm > 0) chain.setTempoBpm (tempoBpm);
    chain.setParams (toVs (p), false);
    chain.reset();
    chain.setParams (toVs (p), false);
    const int latency = chain.latencySamples();
    const bool hasTail = p.secondVoice && p.spatial && p.magic > 0.0f;
    const Frame maxTail = hasTail ? (Frame) (kMaxTailSeconds * in.sampleRate) : 0;
    const Frame minTail = hasTail ? (Frame) (0.25 * in.sampleRate) : 0;

    std::vector<float> l ((size_t) block), r ((size_t) block), inter ((size_t) block * 2);
    float* io[2] = { l.data(), r.data() };
    const float* srcL = in.channels[0].data() + from;
    const float* srcR = (inCh > 1 ? in.channels[1].data() : in.channels[0].data()) + from;

    Stats st;
    st.latencySamples = latency;
    st.inputFrames = length;
    Frame fed = 0, skipped = 0, written = 0;
    const Frame total = length + latency + maxTail; // máximo de frames que se alimentan
    int quietBlocks = 0;
    while (fed < total)
    {
        if (cancel && cancel->load()) return "cancelado";
        const int n = (int) std::min<Frame> (block, total - fed);
        for (int i = 0; i < n; ++i)
        {
            const Frame f = fed + i;
            const bool inside = f < length;
            l[(size_t) i] = inside ? srcL[f] : 0.0f;
            r[(size_t) i] = inside ? srcR[f] : 0.0f;
            if (inside) st.inputPeak = std::max ({ st.inputPeak, std::fabs (l[(size_t) i]), std::fabs (r[(size_t) i]) });
        }
        chain.process (io, 2, n);
        fed += n;
        // Compensación de latencia: se descartan los primeros 'latency' frames de salida.
        int start = 0;
        if (skipped < latency) { start = (int) std::min<Frame> (n, latency - skipped); skipped += start; }
        const int out = n - start;
        if (out > 0)
        {
            float peak = 0;
            for (int i = 0; i < out; ++i)
            {
                float a = l[(size_t) (start + i)], b = r[(size_t) (start + i)];
                if (! std::isfinite (a)) a = 0;
                if (! std::isfinite (b)) b = 0;
                inter[(size_t) i * 2] = a;
                inter[(size_t) i * 2 + 1] = b;
                peak = std::max ({ peak, std::fabs (a), std::fabs (b) });
            }
            st.outputPeak = std::max (st.outputPeak, peak);
            if (! sink (inter.data(), out)) return "no se pudo escribir el resultado (¿almacenamiento lleno?)";
            written += out;
            // Cola: tras el final de la entrada se sigue hasta que la reverb decae por debajo de -72 dBFS.
            if (written > length)
            {
                quietBlocks = peak < 2.5e-4f ? quietBlocks + 1 : 0;
                if (written - length >= minTail && quietBlocks >= 8) break;
            }
            if (! hasTail && written >= length) break;
        }
        if (progress) progress->store ((float) std::min (1.0, (double) fed / (double) (length + latency)) * 0.98f);
    }
    st.outputFrames = written;
    st.tailFrames = std::max<Frame> (0, written - length);
    st.nonFiniteEvents = chain.nonFiniteEvents();
    st.clipAltered = chain.clipAlteredSamples();
    st.seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    if (written < length) return "el motor devolvió menos audio del esperado";
    if (stats) *stats = st;
    return {};
}
#endif
} // namespace cs::vocal
