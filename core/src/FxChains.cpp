#include "cs/FxChains.h"
#include "cs/Processor.h"

#include <algorithm>

namespace cs
{
namespace
{
bool available (const std::string& type)
{
    const auto t = effectTypes();
    return std::find (t.begin(), t.end(), type) != t.end();
}

const std::vector<EffectInfo>& allInfos()
{
    static const std::vector<EffectInfo> v = {
        { "cs.eq", "eq", "EQ de 3 bandas", "3-band EQ", "Graves, medios (con frecuencia) y agudos. Para limpiar o dar brillo.",
          "Lows, mids (with frequency) and highs. To clean up or add brightness." },
        { "cs.filter", "eq", "Filtro", "Filter", "Paso bajo o paso alto con resonancia. Quita graves de una voz o apaga los agudos.",
          "Low-pass or high-pass with resonance. Removes lows from a voice or darkens the highs." },
        { "cs.compressor", "dynamics", "Compresor", "Compressor", "Iguala el volumen: sube lo suave y controla los picos.",
          "Evens out the level: lifts quiet parts and controls peaks." },
        { "cs.reverb", "reverb", "Reverb", "Reverb", "Espacio y profundidad: coloca el sonido en una sala.", "Space and depth: places the sound in a room." },
        { "cs.delay", "delay", "Delay", "Delay", "Repeticiones (eco), con opción ping-pong entre izquierda y derecha.",
          "Echo repeats, with an optional left-right ping-pong." },
        { "cs.saturation", "distortion", "Saturación", "Saturation", "Calor y armónicos; con más drive, distorsión. Hace que el 808 se oiga en móviles.",
          "Warmth and harmonics; more drive gives distortion. Helps the 808 come through on phones." },
        { "cs.gain", "tools", "Ganancia", "Gain", "Sube o baja el nivel dentro de la cadena, sin color.", "Raises or lowers the level inside the chain, uncoloured." },
    };
    return v;
}
} // namespace

std::vector<EffectInfo> effectInfos()
{
    std::vector<EffectInfo> out;
    for (const auto& i : allInfos()) if (available (i.type)) out.push_back (i);
    return out;
}

const EffectInfo* findEffectInfo (const std::string& type)
{
    for (const auto& i : allInfos()) if (i.type == type) return available (type) ? &i : nullptr;
    return nullptr;
}

std::vector<ChainPreset> chainPresets()
{
    static const std::vector<ChainPreset> all = {
        { "vocal_urban", "Voz urbana", "Urban vocal", "Voz delante y con brillo: quita graves, comprime fuerte, un poco de calor, eco corto y sala pequeña.",
          "Upfront, bright vocal: low cut, firm compression, a little warmth, short echo and a small room.", "vocal",
          { { "cs.filter", { { "mode", 1 }, { "cutoffHz", 110 }, { "resonance", 0.1f } } },
            { "cs.eq", { { "lowDb", -2 }, { "midDb", 2 }, { "midHz", 2800 }, { "highDb", 4 } } },
            { "cs.compressor", { { "thresholdDb", -22 }, { "ratio", 5 }, { "attackMs", 4 }, { "releaseMs", 90 }, { "makeupDb", 5 } } },
            { "cs.saturation", { { "drive", 0.15f }, { "mix", 0.35f }, { "outputDb", 0 } } },
            { "cs.delay", { { "timeBeats", 0.5f }, { "feedback", 0.2f }, { "mix", 0.12f }, { "toneHz", 5000 }, { "pingPong", 0 } } },
            { "cs.reverb", { { "size", 0.35f }, { "damping", 0.5f }, { "mix", 0.12f }, { "width", 0.8f } } } } },
        { "vocal_clean", "Voz limpia", "Clean vocal", "Natural: corte de graves suave, compresión moderada y una sala discreta.",
          "Natural: gentle low cut, moderate compression and a discreet room.", "vocal",
          { { "cs.filter", { { "mode", 1 }, { "cutoffHz", 80 }, { "resonance", 0.05f } } },
            { "cs.eq", { { "lowDb", -1 }, { "midDb", 0 }, { "midHz", 1000 }, { "highDb", 2 } } },
            { "cs.compressor", { { "thresholdDb", -18 }, { "ratio", 3 }, { "attackMs", 10 }, { "releaseMs", 140 }, { "makeupDb", 3 } } },
            { "cs.reverb", { { "size", 0.4f }, { "damping", 0.55f }, { "mix", 0.1f }, { "width", 0.9f } } } } },
        { "vocal_double", "Segunda voz", "Backing vocal", "Detrás de la principal: sin graves, más oscura, eco ping-pong y más sala.",
          "Behind the lead: no lows, darker, ping-pong echo and more room.", "vocal",
          { { "cs.filter", { { "mode", 1 }, { "cutoffHz", 220 }, { "resonance", 0.05f } } },
            { "cs.eq", { { "lowDb", -4 }, { "midDb", -2 }, { "midHz", 1500 }, { "highDb", -1 } } },
            { "cs.compressor", { { "thresholdDb", -24 }, { "ratio", 4 }, { "attackMs", 8 }, { "releaseMs", 120 }, { "makeupDb", 3 } } },
            { "cs.delay", { { "timeBeats", 0.75f }, { "feedback", 0.25f }, { "mix", 0.18f }, { "toneHz", 4000 }, { "pingPong", 1 } } },
            { "cs.reverb", { { "size", 0.7f }, { "damping", 0.5f }, { "mix", 0.3f }, { "width", 1 } } } } },
        { "trap_808", "Trap 808", "Trap 808", "Saturación para que el 808 se oiga en altavoces pequeños y compresión lenta que conserva el golpe.",
          "Saturation so the 808 is heard on small speakers and slow compression that keeps the punch.", "bass",
          { { "cs.saturation", { { "drive", 0.35f }, { "mix", 0.6f }, { "outputDb", -2 } } },
            { "cs.compressor", { { "thresholdDb", -14 }, { "ratio", 3 }, { "attackMs", 25 }, { "releaseMs", 200 }, { "makeupDb", 2 } } },
            { "cs.filter", { { "mode", 0 }, { "cutoffHz", 7000 }, { "resonance", 0.05f } } } } },
        { "drums_punch", "Batería", "Drums", "Más pegada: realce de graves y agudos, medios despejados, compresión con ataque lento y un toque de calor.",
          "More punch: low and high lift, cleared mids, slow-attack compression and a touch of warmth.", "drums",
          { { "cs.eq", { { "lowDb", 2 }, { "midDb", -2 }, { "midHz", 400 }, { "highDb", 2 } } },
            { "cs.compressor", { { "thresholdDb", -16 }, { "ratio", 4 }, { "attackMs", 15 }, { "releaseMs", 120 }, { "makeupDb", 3 } } },
            { "cs.saturation", { { "drive", 0.1f }, { "mix", 0.5f }, { "outputDb", 0 } } } } },
        { "melody_space", "Melodía espacial", "Spacious melody", "Melodía amplia y lejana: sin graves que choquen con el 808, eco ping-pong y sala grande.",
          "Wide, distant melody: no lows clashing with the 808, ping-pong echo and a large room.", "melody",
          { { "cs.filter", { { "mode", 1 }, { "cutoffHz", 250 }, { "resonance", 0.1f } } },
            { "cs.delay", { { "timeBeats", 0.75f }, { "feedback", 0.4f }, { "mix", 0.25f }, { "toneHz", 5000 }, { "pingPong", 1 } } },
            { "cs.reverb", { { "size", 0.85f }, { "damping", 0.35f }, { "mix", 0.35f }, { "width", 1 } } } } },
    };
    std::vector<ChainPreset> out;
    for (const auto& p : all)
    {
        bool ok = true;
        for (const auto& s : p.steps) ok = ok && available (s.type);
        if (ok) out.push_back (p);
    }
    return out;
}

const ChainPreset* findChainPreset (const std::string& id)
{
    static const std::vector<ChainPreset> v = chainPresets();
    for (const auto& p : v) if (p.id == id) return &p;
    return nullptr;
}
} // namespace cs
