#pragma once
// Utilidades DSP internas (sin reservas de memoria; aptas para el hilo de audio salvo que se indique).
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace cs::dsp
{
constexpr double kPi = 3.14159265358979323846;

// Ruido determinista (xorshift32): las mismas muestras en cada ejecución, útil para pruebas reproducibles.
struct Rng
{
    uint32_t s = 0x12345678u;
    explicit Rng (uint32_t seed = 0x12345678u) : s (seed ? seed : 1u) {}
    float next() // -1..1
    {
        s ^= s << 13; s ^= s >> 17; s ^= s << 5;
        return (float) ((double) s / 2147483648.0 - 1.0);
    }
};

// Biquad RBJ (forma directa II transpuesta).
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    void reset() { z1 = z2 = 0; }
    float process (float x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        if (! std::isfinite (z1) || ! std::isfinite (z2)) { z1 = z2 = 0; return 0.0f; }
        return (float) y;
    }
    enum Type { LowPass, HighPass, BandPass, Peak, LowShelf, HighShelf };
    void set (Type t, double sr, double f, double q, double gainDb = 0)
    {
        f = std::clamp (f, 10.0, sr * 0.49);
        q = std::max (0.05, q);
        const double w = 2 * kPi * f / sr, cw = std::cos (w), sw = std::sin (w);
        const double alpha = sw / (2 * q);
        const double A = std::pow (10.0, gainDb / 40.0);
        double B0 = 1, B1 = 0, B2 = 0, A0 = 1, A1 = 0, A2 = 0;
        switch (t)
        {
            case LowPass: B0 = (1 - cw) / 2; B1 = 1 - cw; B2 = (1 - cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case HighPass: B0 = (1 + cw) / 2; B1 = -(1 + cw); B2 = (1 + cw) / 2; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case BandPass: B0 = alpha; B1 = 0; B2 = -alpha; A0 = 1 + alpha; A1 = -2 * cw; A2 = 1 - alpha; break;
            case Peak: B0 = 1 + alpha * A; B1 = -2 * cw; B2 = 1 - alpha * A; A0 = 1 + alpha / A; A1 = -2 * cw; A2 = 1 - alpha / A; break;
            case LowShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) - (A - 1) * cw + s); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - s);
                A0 = (A + 1) + (A - 1) * cw + s; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - s;
                break;
            }
            case HighShelf:
            {
                const double s = 2 * std::sqrt (A) * alpha;
                B0 = A * ((A + 1) + (A - 1) * cw + s); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - s);
                A0 = (A + 1) - (A - 1) * cw + s; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - s;
                break;
            }
        }
        b0 = B0 / A0; b1 = B1 / A0; b2 = B2 / A0; a1 = A1 / A0; a2 = A2 / A0;
    }
};

// Saturación suave: lineal hasta 'knee' y se acerca a 1 sin superarlo. |y| < 1 siempre; NaN -> 0.
inline float softClip (float x)
{
    if (! std::isfinite (x)) return 0.0f;
    constexpr float knee = 0.8f;
    const float a = std::fabs (x);
    if (a <= knee) return x;
    const float over = (a - knee) / (1.0f - knee);
    const float y = knee + (1.0f - knee) * (over / (1.0f + over));
    return x < 0 ? -y : y;
}

// PolyBLEP: corrige la discontinuidad de diente de sierra / cuadrada (reduce el aliasing).
inline double polyBlep (double t, double dt)
{
    if (dt <= 0) return 0;
    if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
    return 0.0;
}

inline float dbToLin (float db) { return db <= -96.0f ? 0.0f : std::pow (10.0f, db / 20.0f); }
} // namespace cs::dsp
