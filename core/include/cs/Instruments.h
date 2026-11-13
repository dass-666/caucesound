#pragma once
// Instrumentos internos: Cauce Synth ("cs.synth") y Cauce Drums ("cs.drums").
// Se crean con createProcessor(typeId) (Processor.h). Aquí están los catálogos de presets y kits que comparten
// Android e iOS, para que ambas interfaces muestren exactamente lo mismo.
#include "cs/Processor.h"

#include <string>
#include <utility>
#include <vector>

namespace cs
{
constexpr int kSynthMaxVoices = 16;

struct SynthPreset
{
    std::string id, name, category; // category: "bass" | "lead" | "keys" | "pad"
    std::vector<std::pair<std::string, float>> values; // id de parámetro -> valor (el resto, por defecto)
};
const std::vector<SynthPreset>& synthPresets();
// Estado JSON (para InsertModel/InstrumentModel.state) del preset; vacío si no existe.
std::string synthPresetState (const std::string& presetId);

struct DrumKitInfo
{
    std::string id, name;
    std::vector<std::string> laneNames; // 8 nombres
};
const std::vector<DrumKitInfo>& drumKits();
int drumKitIndex (const std::string& kitId); // -1 si no existe

// Estado de carga de un instrumento de muestras (cs.sampler o Cauce Drums con un kit de paquete). Hilo de control.
struct SamplerStatus
{
    std::string key, name, error; // key: "<paquete>:<instrumento>"; vacío si no usa muestras
    bool loaded = false;
    size_t bytes = 0;
    int regions = 0, missingSamples = 0;
};
SamplerStatus samplerStatus (const AudioProcessor* p);

// Busca el índice de un parámetro por id (-1 si no existe).
int parameterIndex (const AudioProcessor& p, const std::string& id);
} // namespace cs
