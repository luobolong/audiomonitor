#pragma once

#include "ringbuffer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

// Consumer-side controller for a RingBuffer that bridges two independent
// audio device clocks.
//
// A fixed-size queue alone only postpones clock drift: a slightly faster
// source eventually overflows it and a slightly slower source eventually
// underruns it. This reader keeps the queue near a target occupancy. Outside
// a small hysteresis window it consumes one extra or one fewer input frame
// and linearly maps that block to the exact frame count requested by WASAPI.
// The correction is bounded to one frame per callback and is normally idle.
//
// Startup and post-underrun playback wait for the target occupancy instead of
// repeatedly emitting short fragments separated by silence. The scratch
// buffer is allocated by the constructor; render() allocates no memory, takes
// no lock, and performs no blocking work.
class AdaptiveAudioBufferReader {
public:
    AdaptiveAudioBufferReader(const RingBuffer& ring,
                              size_t maxOutputFrames,
                              size_t targetFrames,
                              size_t hysteresisFrames,
                              float initialVolume = 0.0f)
        : m_channels(ring.channels()),
          m_capacity(ring.capacity()),
          m_maxOutputFrames(validatedMaxOutputFrames(maxOutputFrames)),
          m_targetFrames(std::clamp(targetFrames, size_t{1}, m_capacity)),
          m_lowWatermark(m_targetFrames > hysteresisFrames
                             ? m_targetFrames - hysteresisFrames
                             : size_t{1}),
          m_highWatermark(std::min(
              m_capacity,
              saturatingAdd(m_targetFrames, hysteresisFrames))),
          m_currentVolume(std::isfinite(initialVolume) ? initialVolume : 0.0f),
          m_scratch(checkedSampleCapacity(m_maxOutputFrames, m_channels))
    {
    }

    AdaptiveAudioBufferReader(const AdaptiveAudioBufferReader&) = delete;
    AdaptiveAudioBufferReader& operator=(const AdaptiveAudioBufferReader&) = delete;

    size_t targetFrames() const noexcept { return m_targetFrames; }
    size_t lowWatermark() const noexcept { return m_lowWatermark; }
    size_t highWatermark() const noexcept { return m_highWatermark; }
    bool isRebuffering() const noexcept { return m_rebuffering; }

    // Consumer only. Returns the number of real input frames consumed. A zero
    // return means the whole output block is silence and can be submitted with
    // AUDCLNT_BUFFERFLAGS_SILENT. A partial non-zero return is possible only
    // if a producer discontinuity races this callback; the next callback will
    // re-establish the target occupancy before playback resumes.
    size_t render(RingBuffer& ring,
                  float* dst,
                  size_t outputFrames,
                  float targetVolume) noexcept
    {
        if (!dst || outputFrames == 0 || outputFrames > m_maxOutputFrames
            || ring.channels() != m_channels) {
            return 0;
        }

        if (!std::isfinite(targetVolume))
            targetVolume = 0.0f;

        size_t buffered = ring.consumerBufferedFrames();
        if (m_rebuffering) {
            if (buffered < m_targetFrames) {
                silence(dst, outputFrames);
                m_currentVolume = 0.0f;
                return 0;
            }
            m_rebuffering = false;
        }

        size_t inputFrames = outputFrames;
        if (buffered > m_highWatermark
            && buffered >= outputFrames + 1) {
            // The source clock is leading: consume one extra frame.
            ++inputFrames;
        } else if (buffered < m_lowWatermark && outputFrames > 1
                   && buffered >= outputFrames - 1) {
            // The source clock is trailing: stretch one fewer frame.
            --inputFrames;
        }

        // Do not drain a short fragment and alternate it with silence. Keep
        // the fragment queued and rebuild the configured safety margin.
        if (buffered < inputFrames) {
            m_rebuffering = true;
            m_currentVolume = 0.0f;
            silence(dst, outputFrames);
            return 0;
        }

        const bool volumeStable = m_currentVolume == targetVolume;
        if (inputFrames == outputFrames && volumeStable) {
            const size_t consumed =
                ring.read(dst, outputFrames, targetVolume);
            if (consumed != outputFrames) {
                m_rebuffering = true;
                m_currentVolume = 0.0f;
            }
            return consumed;
        }

        const size_t consumed = ring.read(m_scratch.data(), inputFrames, 1.0f);
        resampleAndRamp(dst, outputFrames, inputFrames,
                        m_currentVolume, targetVolume);

        if (consumed != inputFrames) {
            m_rebuffering = true;
            m_currentVolume = 0.0f;
        } else {
            m_currentVolume = targetVolume;
        }
        return consumed;
    }

private:
    static size_t validatedMaxOutputFrames(size_t frames)
    {
        if (frames == 0 || frames == std::numeric_limits<size_t>::max())
            throw std::invalid_argument(
                "AdaptiveAudioBufferReader requires a valid output capacity");
        return frames;
    }

    static size_t saturatingAdd(size_t a, size_t b) noexcept
    {
        return b > std::numeric_limits<size_t>::max() - a
            ? std::numeric_limits<size_t>::max()
            : a + b;
    }

    static size_t checkedSampleCapacity(size_t maxOutputFrames,
                                        size_t channels)
    {
        // One additional input frame is needed for the speed-up correction.
        const size_t inputFrames = maxOutputFrames + 1;
        if (channels == 0
            || inputFrames > std::numeric_limits<size_t>::max() / channels) {
            throw std::length_error(
                "AdaptiveAudioBufferReader scratch buffer is too large");
        }
        const size_t samples = inputFrames * channels;
        if (samples > std::numeric_limits<size_t>::max() / sizeof(float)) {
            throw std::length_error(
                "AdaptiveAudioBufferReader scratch buffer is too large");
        }
        return samples;
    }

    void silence(float* dst, size_t frames) const noexcept
    {
        std::memset(dst, 0, frames * m_channels * sizeof(float));
    }

    void resampleAndRamp(float* dst,
                         size_t outputFrames,
                         size_t inputFrames,
                         float startVolume,
                         float endVolume) const noexcept
    {
        for (size_t outFrame = 0; outFrame < outputFrames; ++outFrame) {
            size_t firstFrame = 0;
            size_t secondFrame = 0;
            float fraction = 0.0f;

            if (outputFrames > 1 && inputFrames > 1) {
                const double position =
                    static_cast<double>(outFrame)
                    * static_cast<double>(inputFrames - 1)
                    / static_cast<double>(outputFrames - 1);
                firstFrame = static_cast<size_t>(position);
                secondFrame = std::min(firstFrame + 1, inputFrames - 1);
                fraction = static_cast<float>(position - firstFrame);
            }

            const float ramp = static_cast<float>(outFrame + 1)
                               / static_cast<float>(outputFrames);
            const float volume = startVolume
                               + (endVolume - startVolume) * ramp;
            for (size_t channel = 0; channel < m_channels; ++channel) {
                const float first =
                    m_scratch[firstFrame * m_channels + channel];
                const float second =
                    m_scratch[secondFrame * m_channels + channel];
                dst[outFrame * m_channels + channel] =
                    (first + (second - first) * fraction) * volume;
            }
        }
    }

    const size_t m_channels;
    const size_t m_capacity;
    const size_t m_maxOutputFrames;
    const size_t m_targetFrames;
    const size_t m_lowWatermark;
    const size_t m_highWatermark;
    float m_currentVolume;
    bool m_rebuffering = true;
    std::vector<float> m_scratch;
};
