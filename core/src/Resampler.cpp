#include "cs/Resampler.h"

#include <algorithm>
#include <cmath>

namespace cs
{
namespace
{
constexpr int kZeroCrossings = 32;
constexpr int kTablePhases = 1024; // resolución de la tabla del núcleo por cruce por cero
constexpr double kPi = 3.14159265358979323846;

double blackmanHarris (double x) // x en [-1, 1]
{
    const double t = (x + 1.0) * 0.5; // 0..1
    return 0.35875 - 0.48829 * std::cos (2 * kPi * t) + 0.14128 * std::cos (4 * kPi * t) - 0.01168 * std::cos (6 * kPi * t);
}

struct Kernel
{
    std::vector<float> table; // muestras del núcleo en [0, kZeroCrossings] con kTablePhases por unidad
    double cutoff = 1.0;      // fracción de Nyquist de la frecuencia menor

    explicit Kernel (double cut) : cutoff (cut)
    {
        const int n = kZeroCrossings * kTablePhases + 2;
        table.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            const double x = (double) i / kTablePhases; // en "cruces por cero"
            const double sinc = x == 0 ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double w = x >= kZeroCrossings ? 0.0 : blackmanHarris (x / kZeroCrossings);
            table[(size_t) i] = (float) (sinc * w);
        }
    }
    float at (double x) const // x >= 0 en cruces por cero
    {
        const double p = x * kTablePhases;
        const int i = (int) p;
        if (i >= kZeroCrossings * kTablePhases) return 0.0f;
        const float f = (float) (p - i);
        return table[(size_t) i] + f * (table[(size_t) i + 1] - table[(size_t) i]);
    }
};
} // namespace

AudioData resample (const AudioData& in, int targetRate)
{
    if (in.sampleRate == targetRate || in.numFrames() == 0)
    {
        AudioData copy = in;
        copy.sampleRate = targetRate;
        return copy;
    }
    const double ratio = (double) targetRate / in.sampleRate; // salida / entrada
    const double cutoff = std::min (1.0, ratio) * 0.95;          // margen para la banda de transición
    const Kernel k (cutoff);
    const Frame inFrames = in.numFrames();
    const Frame outFrames = (Frame) std::llround ((double) inFrames * ratio);
    const double halfWidth = kZeroCrossings / cutoff; // en muestras de entrada

    AudioData out;
    out.sampleRate = targetRate;
    out.allocate (in.numChannels(), outFrames);
    for (int c = 0; c < in.numChannels(); ++c)
    {
        const float* x = in.channels[(size_t) c].data();
        float* y = out.channels[(size_t) c].data();
        for (Frame j = 0; j < outFrames; ++j)
        {
            const double t = (double) j / ratio; // posición en la entrada
            const Frame first = std::max<Frame> (0, (Frame) std::ceil (t - halfWidth));
            const Frame last = std::min<Frame> (inFrames - 1, (Frame) std::floor (t + halfWidth));
            double acc = 0;
            for (Frame i = first; i <= last; ++i)
                acc += (double) x[i] * k.at (std::fabs ((t - (double) i) * cutoff));
            y[j] = (float) (acc * cutoff);
        }
    }
    return out;
}
} // namespace cs
