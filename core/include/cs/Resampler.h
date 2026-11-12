#pragma once
// Conversión de frecuencia de muestreo offline (importación). Sinc enventanado (Blackman-Harris, 32 cruces por cero
// por lado), con filtro antialias al bajar de frecuencia. No se usa en el hilo de audio.
#include "cs/AudioData.h"

namespace cs
{
AudioData resample (const AudioData& in, int targetRate);
}
