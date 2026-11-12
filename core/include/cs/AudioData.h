#pragma once
// Audio en memoria, planar (un vector por canal). Inmutable una vez publicado al motor.
#include <cstdint>
#include <vector>

namespace cs
{
using Frame = int64_t; // posición o duración en muestras (frames) a la frecuencia del proyecto

struct AudioData
{
    int sampleRate = 48000;
    std::vector<std::vector<float>> channels; // channels[c][frame]

    int numChannels() const { return (int) channels.size(); }
    Frame numFrames() const { return channels.empty() ? 0 : (Frame) channels[0].size(); }

    void allocate (int numCh, Frame frames)
    {
        channels.assign ((size_t) numCh, std::vector<float> ((size_t) frames, 0.0f));
    }
};
} // namespace cs
