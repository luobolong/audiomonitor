#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <vector>

// Bounded lock-free SPSC queue for interleaved float32 audio frames.
//
// Cursor ownership is strict:
//   producer: writes only m_write; reads m_read
//   consumer: writes only m_read; reads m_write
//
// Both cursors are monotonically increasing uint64_t frame counters;
// arithmetic wrap is considered unreachable during the lifetime of the
// process (2^64 frames is ~12 million years at 48 kHz). Physical storage
// offsets are derived with cursor % capacity, so the capacity stays an
// arbitrary frame count derived from the sample rate and a duration
// (power-of-two capacity is not required).
//
// Synchronization is explicit acquire/release:
//   - The producer finishes writing samples before publishing the write
//     cursor with release.
//   - The consumer reads the write cursor with acquire before touching the
//     corresponding samples.
//   - The consumer publishes its advanced read cursor with release.
//   - The producer reads the read cursor with acquire before reusing any
//     storage. A cursor owned exclusively by the current thread is loaded
//     with relaxed ordering.
//
// Writes are frame based. If only part of a capture packet fits, the producer
// publishes the complete frames that fit and drops the remaining complete
// frames. Packet atomicity is deliberately not required for frame alignment.
// Any dropped frames increment a producer-owned discontinuity generation.
// When the consumer observes a generation change it catches up to the latest
// write cursor visible at that moment, discarding all audio queued up to
// that catch-up snapshot; audio published after the snapshot is eligible for
// the following read. This is intentional: after an overrun/discontinuity
// the monitor resumes from the freshest available audio instead of playing
// queued stale audio. The producer never overwrites unread storage and never
// advances the consumer cursor. If a discontinuity is published while the
// consumer is copying, the consumer re-checks the generation after the copy
// and discards the copied samples the same way instead of submitting stale
// audio.
//
// Capacity describes storage, not measured end-to-end latency or audio age.
//
// Producer/consumer methods perform no dynamic allocation, take no locks,
// use no blocking synchronization, and never throw. All shared atomic state
// used on the real-time path is guaranteed lock-free.
#ifdef _MSC_VER
// The over-aligned cursor members pad the object; that padding is intentional
// for false-sharing avoidance. Suppress C4324 for the whole class definition.
#pragma warning(push)
#pragma warning(disable : 4324)
#endif
class RingBuffer {
public:
    static_assert(std::atomic<uint64_t>::is_always_lock_free,
                  "RingBuffer cursors require lock-free uint64_t atomics");
    // Cursor values are narrowed to size_t only after being bounded by the
    // capacity, which fits size_t; this assertion guarantees those
    // conversions are representable on the host platform.
    static_assert(sizeof(size_t) <= sizeof(uint64_t),
                  "RingBuffer requires size_t to fit in uint64_t");
    // The storage is byte-copied as interleaved float32 PCM, so the sample
    // type must be exactly 32-bit IEEE-754.
    static_assert(sizeof(float) == 4,
                  "RingBuffer requires 32-bit float samples");
    static_assert(std::numeric_limits<float>::is_iec559,
                  "RingBuffer requires IEEE-754 float samples");

    RingBuffer(size_t channels, size_t sampleRate, size_t bufferDurationMs)
        : m_channels(validatedChannels(channels)),
          m_sampleRate(sampleRate),
          m_capacity(frameCapacity(sampleRate, bufferDurationMs)),
          // Largest read() request whose sample-count and byte-count
          // conversions (frames * channels, then * sizeof(float)) both fit
          // in size_t. The divisions are ordered so no intermediate
          // overflows; m_channels >= 1 is guaranteed by validatedChannels.
          m_maxRequestFrames(std::numeric_limits<size_t>::max() / sizeof(float)
                             / m_channels),
          m_buf(checkedSampleCapacity(m_channels, m_capacity))
    {
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
        if (wholeFrames > std::numeric_limits<size_t>::max() - partialFrames)
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

    // Approximate occupancy snapshots for diagnostics and tests only. Each
    // cursor is loaded independently, so neither function is an atomic
    // cross-cursor snapshot and both may report stale values. They never
    // participate in synchronization: producers rely on write()'s published
    // frame count and consumers on read()'s consumed frame count.
    size_t bufferedFrames() const noexcept
    {
        const uint64_t r = m_read.load(std::memory_order_acquire);
        const uint64_t w = m_write.load(std::memory_order_acquire);
        // r <= w holds by the queue invariant (the consumer never passes the
        // writer), so the unsigned difference never underflows.
        const uint64_t used = w - r;
        return static_cast<size_t>(std::min<uint64_t>(used, m_capacity));
    }

    size_t availableFrames() const noexcept
    {
        return m_capacity - bufferedFrames();
    }

    // Diagnostic cursor snapshots. Like the occupancy snapshots above, these
    // are approximate and must not drive synchronization.
    uint64_t consumerCursor() const noexcept
    {
        return m_read.load(std::memory_order_acquire);
    }

    uint64_t producerCursor() const noexcept
    {
        return m_write.load(std::memory_order_acquire);
    }

    uint64_t discontinuityGeneration() const noexcept
    {
        return m_discontinuityGeneration.load(std::memory_order_acquire);
    }

    // Producer only. Use for an upstream capture discontinuity even when the
    // current packet itself fits in the queue. The producer is the only
    // writer, so an owner-side relaxed load of the last published generation
    // plus a release store is equivalent to a release fetch_add without the
    // read-modify-write cost on the real-time path.
    void signalDiscontinuity() noexcept
    {
        const uint64_t generation =
            m_discontinuityGeneration.load(std::memory_order_relaxed);
        m_discontinuityGeneration.store(generation + 1,
                                        std::memory_order_release);
    }

    // Producer only. Returns the number of complete frames published.
    // For a valid non-null source, if the return value is less than frames,
    // the uncommitted suffix was dropped and a discontinuity was published.
    size_t write(const float* src, size_t frames) noexcept
    {
        if (frames == 0)
            return 0;

        if (!src) {
            signalDiscontinuity();
            return 0;
        }

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
    // shortfall. If the producer published a discontinuity before this call,
    // the consumer discards the stale queued region with its own read cursor
    // and returns silence. The consumer catches up to the latest write cursor
    // observed while handling the discontinuity; audio published after that
    // catch-up snapshot is eligible for the following read. The generation is
    // re-checked after the copy, so a discontinuity that arrives mid-copy
    // also discards the copied samples instead of submitting stale audio.
    size_t read(float* dst, size_t frames, float volume) noexcept
    {
        // Requests whose frame count cannot be converted to a sample count
        // (frames * channels) and then to a byte count
        // (frames * channels * sizeof(float)) are rejected rather than
        // overflowing the copy and zero-fill arithmetic below.
        if (!dst || frames == 0 || frames > m_maxRequestFrames)
            return 0;

        const uint64_t generation =
            m_discontinuityGeneration.load(std::memory_order_acquire);
        if (generation != m_consumerGeneration) {
            const uint64_t w = m_write.load(std::memory_order_acquire);
            m_read.store(w, std::memory_order_release);
            m_consumerGeneration = generation;
            zero(dst, frames * m_channels);
            return 0;
        }

        const uint64_t r = m_read.load(std::memory_order_relaxed);
        const uint64_t w = m_write.load(std::memory_order_acquire);
        // r <= w by the queue invariant, and w - r never exceeds the
        // capacity, so both conversions below are exact.
        const size_t have =
            static_cast<size_t>(std::min<uint64_t>(frames, w - r));
        const size_t readOffsetSamples = static_cast<size_t>(r % m_capacity)
                                         * m_channels;
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
        const uint64_t generationAfterCopy =
            m_discontinuityGeneration.load(std::memory_order_acquire);
        if (generationAfterCopy != generation) {
            const uint64_t latestWrite = m_write.load(std::memory_order_acquire);
            m_read.store(latestWrite, std::memory_order_release);
            m_consumerGeneration = generationAfterCopy;
            zero(dst, frames * m_channels);
            return 0;
        }

        // The consumer has finished reading every sample it will take from
        // m_buf and the generation still matches, so release the read region
        // immediately. Publishing the advanced read cursor before the
        // unrelated output zero-fill lets the producer reuse the released
        // storage as early as possible; the zero-fill only touches dst, not
        // m_buf. This store is not hoisted above the generation check above,
        // which must stay able to invalidate stale copied samples. When
        // nothing was consumed, no storage is released, so the store is
        // skipped to avoid an unnecessary write to the consumer cursor's
        // cache line on every empty callback.
        if (have != 0)
            m_read.store(r + have, std::memory_order_release);
        zero(dst + totalSamples, (frames - have) * m_channels);
        return have;
    }

private:
    // Frame counts on the public API stay size_t; cursor arithmetic uses the
    // monotonic uint64_t counters. toWrite is bounded by the capacity, which
    // fits in size_t, so the conversion is exact.
    template <typename Fill>
    size_t produce(size_t frames, Fill&& fill) noexcept
    {
        const uint64_t w = m_write.load(std::memory_order_relaxed);
        const uint64_t r = m_read.load(std::memory_order_acquire);
        const uint64_t used = w - r;
        const uint64_t space = used < m_capacity ? m_capacity - used : 0;
        const size_t toWrite =
            static_cast<size_t>(std::min<uint64_t>(frames, space));

        if (toWrite != 0) {
            const size_t writeOffsetSamples =
                static_cast<size_t>(w % m_capacity) * m_channels;
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

    static size_t validatedChannels(size_t channels)
    {
        if (channels == 0)
            throw std::invalid_argument("RingBuffer channels must be non-zero");
        return channels;
    }

    static size_t checkedMultiply(size_t a, size_t b)
    {
        if (a != 0 && b > std::numeric_limits<size_t>::max() / a)
            throw std::length_error("RingBuffer capacity is too large");
        return a * b;
    }

    static size_t checkedSampleCapacity(size_t channels, size_t frames)
    {
        if (channels == 0)
            throw std::invalid_argument("RingBuffer channels must be non-zero");
        const size_t samples = checkedMultiply(channels, frames);
        // The real-time path converts sample counts to byte counts with
        // samples * sizeof(float); validate that conversion here so every
        // runtime byte-size calculation is guaranteed not to overflow.
        if (samples > std::numeric_limits<size_t>::max() / sizeof(float))
            throw std::length_error("RingBuffer capacity is too large");
        return samples;
    }

    static void zero(float* dst, size_t samples) noexcept
    {
        if (samples != 0)
            std::memset(dst, 0, samples * sizeof(float));
    }

    // Immutable configuration. Cold data: no false-sharing concerns.
    const size_t m_channels;
    const size_t m_sampleRate;
    const size_t m_capacity;
    // Largest read() request whose sample-count and byte-count conversions
    // (frames * channels * sizeof(float)) both fit in size_t.
    const size_t m_maxRequestFrames;
    std::vector<float> m_buf;

    // Shared cursor/generation groups start on separate 64-byte-aligned
    // regions to avoid false sharing on architectures with conventional
    // 64-byte cache lines. m_read and m_consumerGeneration are consumer-owned
    // (the producer only reads m_read), so they share one cache line; m_write
    // and m_discontinuityGeneration are producer-owned (the consumer only
    // reads them) and each starts on its own cache line.
    static constexpr size_t kCacheLineBytes = 64;
    alignas(kCacheLineBytes) std::atomic<uint64_t> m_read{0};
    uint64_t m_consumerGeneration = 0;
    alignas(kCacheLineBytes) std::atomic<uint64_t> m_write{0};
    alignas(kCacheLineBytes) std::atomic<uint64_t> m_discontinuityGeneration{0};
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif
