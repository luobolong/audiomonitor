#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <vector>

// Bounded SPSC queue for interleaved float32 audio frames.
//
// Cursor ownership is strict:
//   producer: writes only m_write; reads m_read
//   consumer: writes only m_read; reads m_write
//
// Writes are frame based. If only part of a capture packet fits, the producer
// publishes the complete frames that fit and drops the remaining complete
// frames. Packet atomicity is deliberately not required for frame alignment.
// Any dropped frames increment a producer-owned discontinuity generation.
// The consumer observes that generation and advances its own read cursor past
// all queued pre-discontinuity audio before playback resumes. The producer
// never overwrites unread storage and never advances the consumer cursor.
//
// Capacity describes storage, not measured end-to-end latency or audio age.
class RingBuffer {
public:
    static_assert(std::atomic<size_t>::is_always_lock_free,
                  "RingBuffer cursors require lock-free size_t atomics");

    RingBuffer(size_t channels, size_t sampleRate, size_t bufferDurationMs)
        : m_channels(channels),
          m_sampleRate(sampleRate),
          m_capacity(frameCapacity(sampleRate, bufferDurationMs)),
          m_buf(checkedSampleCapacity(channels, m_capacity))
    {
        if (channels == 0)
            throw std::invalid_argument("RingBuffer channels must be non-zero");
    }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    static size_t frameCapacity(size_t sampleRate, size_t bufferDurationMs)
    {
        if (sampleRate == 0)
            throw std::invalid_argument("RingBuffer sample rate must be non-zero");
        if (bufferDurationMs == 0)
            throw std::invalid_argument("RingBuffer duration must be non-zero");

        const size_t wholeSeconds = bufferDurationMs / 1000;
        const size_t remainingMs = bufferDurationMs % 1000;
        const size_t wholeFrames = checkedMultiply(sampleRate, wholeSeconds);
        const size_t partialProduct = checkedMultiply(sampleRate, remainingMs);
        const size_t partialFrames = partialProduct / 1000
            + (partialProduct % 1000 != 0 ? 1 : 0);
        if (wholeFrames > static_cast<size_t>(-1) - partialFrames)
            throw std::length_error("RingBuffer capacity is too large");
        return std::max<size_t>(1, wholeFrames + partialFrames);
    }

    size_t capacity() const noexcept { return m_capacity; }
    size_t channels() const noexcept { return m_channels; }
    size_t sampleRate() const noexcept { return m_sampleRate; }

    // Physical queue capacity expressed as a duration. This is not current
    // occupancy, device latency, or the age of the next sample.
    double capacityDurationMs() const noexcept
    {
        return static_cast<double>(m_capacity) * 1000.0
             / static_cast<double>(m_sampleRate);
    }

    // Approximate snapshots intended for diagnostics and invariant tests.
    // Only the producer uses availableFrames() to make write decisions.
    size_t bufferedFrames() const noexcept
    {
        const size_t r = m_read.load(std::memory_order_acquire);
        const size_t w = m_write.load(std::memory_order_acquire);
        const size_t used = w - r;
        return std::min(used, m_capacity);
    }

    size_t availableFrames() const noexcept
    {
        return m_capacity - bufferedFrames();
    }

    size_t consumerCursor() const noexcept
    {
        return m_read.load(std::memory_order_acquire);
    }

    size_t producerCursor() const noexcept
    {
        return m_write.load(std::memory_order_acquire);
    }

    size_t discontinuityGeneration() const noexcept
    {
        return m_discontinuityGeneration.load(std::memory_order_acquire);
    }

    // Producer only. Use for an upstream capture discontinuity even when the
    // current packet itself fits in the queue.
    void signalDiscontinuity() noexcept
    {
        m_discontinuityGeneration.fetch_add(1, std::memory_order_release);
    }

    // Producer only. Returns the number of complete frames published. If the
    // return value is less than frames, the uncommitted suffix was dropped and
    // a discontinuity was published.
    size_t write(const float* src, size_t frames) noexcept
    {
        if (!src || frames == 0)
            return 0;

        return produce(frames, [this, src](size_t writeOffsetSamples,
                                           size_t firstSamples,
                                           size_t totalSamples) {
            std::memcpy(m_buf.data() + writeOffsetSamples, src,
                        firstSamples * sizeof(float));
            if (totalSamples > firstSamples) {
                std::memcpy(m_buf.data(), src + firstSamples,
                            (totalSamples - firstSamples) * sizeof(float));
            }
        });
    }

    // Producer only. Behaves like write(), but publishes zero-valued frames.
    size_t writeSilence(size_t frames) noexcept
    {
        if (frames == 0)
            return 0;

        return produce(frames, [this](size_t writeOffsetSamples,
                                      size_t firstSamples,
                                      size_t totalSamples) {
            std::memset(m_buf.data() + writeOffsetSamples, 0,
                        firstSamples * sizeof(float));
            if (totalSamples > firstSamples) {
                std::memset(m_buf.data(), 0,
                            (totalSamples - firstSamples) * sizeof(float));
            }
        });
    }

    // Consumer only. Reads at most frames, applies volume, and zero-fills any
    // shortfall. On a producer discontinuity, this call discards the stale
    // queued region using m_read and returns silence; newly arriving audio is
    // eligible for the following call.
    size_t read(float* dst, size_t frames, float volume) noexcept
    {
        if (!dst || frames == 0)
            return 0;

        const size_t generation =
            m_discontinuityGeneration.load(std::memory_order_acquire);
        if (generation != m_consumerGeneration) {
            const size_t w = m_write.load(std::memory_order_acquire);
            m_read.store(w, std::memory_order_release);
            m_consumerGeneration = generation;
            zero(dst, frames * m_channels);
            return 0;
        }

        const size_t r = m_read.load(std::memory_order_relaxed);
        const size_t w = m_write.load(std::memory_order_acquire);
        const size_t have = std::min(frames, w - r);
        const size_t readOffsetSamples = (r % m_capacity) * m_channels;
        const size_t totalSamples = have * m_channels;
        const size_t firstSamples =
            std::min(totalSamples, m_buf.size() - readOffsetSamples);

        if (volume == 1.0f) {
            if (firstSamples != 0) {
                std::memcpy(dst, m_buf.data() + readOffsetSamples,
                            firstSamples * sizeof(float));
            }
            if (totalSamples > firstSamples) {
                std::memcpy(dst + firstSamples, m_buf.data(),
                            (totalSamples - firstSamples) * sizeof(float));
            }
        } else {
            for (size_t i = 0; i < firstSamples; ++i)
                dst[i] = m_buf[readOffsetSamples + i] * volume;
            for (size_t i = firstSamples; i < totalSamples; ++i)
                dst[i] = m_buf[i - firstSamples] * volume;
        }

        // If the producer reported a drop while this region was being copied,
        // do not submit the copied stale audio. The consumer still performs
        // the only write to m_read and catches up to the latest published end.
        const size_t generationAfterCopy =
            m_discontinuityGeneration.load(std::memory_order_acquire);
        if (generationAfterCopy != generation) {
            const size_t latestWrite = m_write.load(std::memory_order_acquire);
            m_read.store(latestWrite, std::memory_order_release);
            m_consumerGeneration = generationAfterCopy;
            zero(dst, frames * m_channels);
            return 0;
        }

        zero(dst + totalSamples, (frames - have) * m_channels);
        m_read.store(r + have, std::memory_order_release);
        return have;
    }

private:
    template <typename Fill>
    size_t produce(size_t frames, Fill&& fill) noexcept
    {
        const size_t w = m_write.load(std::memory_order_relaxed);
        const size_t r = m_read.load(std::memory_order_acquire);
        const size_t used = w - r;
        const size_t space = used < m_capacity ? m_capacity - used : 0;
        const size_t toWrite = std::min(frames, space);

        if (toWrite != 0) {
            const size_t writeOffsetSamples = (w % m_capacity) * m_channels;
            const size_t totalSamples = toWrite * m_channels;
            const size_t firstSamples =
                std::min(totalSamples, m_buf.size() - writeOffsetSamples);
            fill(writeOffsetSamples, firstSamples, totalSamples);
            m_write.store(w + toWrite, std::memory_order_release);
        }

        if (toWrite != frames) {
            signalDiscontinuity();
        }
        return toWrite;
    }

    static size_t checkedMultiply(size_t a, size_t b)
    {
        if (a != 0 && b > static_cast<size_t>(-1) / a)
            throw std::length_error("RingBuffer capacity is too large");
        return a * b;
    }

    static size_t checkedSampleCapacity(size_t channels, size_t frames)
    {
        if (channels == 0)
            throw std::invalid_argument("RingBuffer channels must be non-zero");
        return checkedMultiply(channels, frames);
    }

    static void zero(float* dst, size_t samples) noexcept
    {
        if (samples != 0)
            std::memset(dst, 0, samples * sizeof(float));
    }

    const size_t m_channels;
    const size_t m_sampleRate;
    const size_t m_capacity;
    std::vector<float> m_buf;

    alignas(64) std::atomic<size_t> m_read{0};  // consumer-owned write cursor
    alignas(64) std::atomic<size_t> m_write{0}; // producer-owned write cursor
    alignas(64) std::atomic<size_t> m_discontinuityGeneration{0}; // producer writes
    size_t m_consumerGeneration = 0; // consumer thread only
};
