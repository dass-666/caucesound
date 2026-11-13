#pragma once
// Mixer Pro (misión 0.7): información de cada efecto para el selector por categorías y cadenas de efectos completas
// («presets de cadena»). Solo usa efectos que existen en effectTypes(): si un tipo no está compilado, la cadena no se
// anuncia. Datos puros, sin dependencias de plataforma (reutilizable en CauceSound Studio).
#include <map>
#include <string>
#include <vector>

namespace cs
{
struct EffectInfo
{
    std::string type;          // "cs.eq"
    std::string category;      // eq, dynamics, reverb, delay, modulation, distortion, vocal, tools
    std::string name, nameEn;
    std::string description, descriptionEn;
};
// Solo efectos realmente disponibles (los de effectTypes()). Orden: el del selector.
std::vector<EffectInfo> effectInfos();
const EffectInfo* findEffectInfo (const std::string& type);

struct ChainStep
{
    std::string type;
    std::map<std::string, float> params; // valores dentro del rango del efecto. Clave especial "timeBeats" (delay):
                                         // tiempo en negras, se convierte a ms con el tempo del proyecto al aplicar.
};
struct ChainPreset
{
    std::string id, name, nameEn, description, descriptionEn;
    std::string suits; // "vocal", "drums", "bass", "melody", "any" (sugerencia para la interfaz)
    std::vector<ChainStep> steps;
};
// Solo las cadenas cuyos efectos están todos disponibles. Validadas por pruebas (parámetros dentro de rango).
std::vector<ChainPreset> chainPresets();
const ChainPreset* findChainPreset (const std::string& id);
} // namespace cs
