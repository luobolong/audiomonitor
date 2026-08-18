#pragma once

#include <cstddef>
#include <cstring>

namespace realtime_audio {

// Process one graph cycle of planar float32 stereo. PipeWire DSP ports expose
// one mapped buffer per channel, so a frame is one sample in each buffer.
// Missing either input channel invalidates the stereo cycle and produces
// silence; no audio from an older cycle is retained.
inline void processStereo(const float* inputLeft,
                          const float* inputRight,
                          float* outputLeft,
                          float* outputRight,
                          std::size_t frames,
                          float volume) noexcept
{
    if (frames == 0 || (!outputLeft && !outputRight))
        return;

    const std::size_t bytes = frames * sizeof(float);
    if (!inputLeft || !inputRight) {
        if (outputLeft)
            std::memset(outputLeft, 0, bytes);
        if (outputRight)
            std::memset(outputRight, 0, bytes);
        return;
    }

    if (volume == 1.0f) {
        if (outputLeft && outputLeft != inputLeft)
            std::memcpy(outputLeft, inputLeft, bytes);
        if (outputRight && outputRight != inputRight)
            std::memcpy(outputRight, inputRight, bytes);
        return;
    }

    for (std::size_t i = 0; i < frames; ++i) {
        if (outputLeft)
            outputLeft[i] = inputLeft[i] * volume;
        if (outputRight)
            outputRight[i] = inputRight[i] * volume;
    }
}

} // namespace realtime_audio
