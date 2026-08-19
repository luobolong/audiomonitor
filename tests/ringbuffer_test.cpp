#include "core/ringbuffer.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message)
{
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expectSamples(const float* actual, const std::vector<float>& expected,
                   const std::string& message)
{
    for (size_t i = 0; i < expected.size(); ++i) {
        if (actual[i] != expected[i]) {
            expect(false, message + " at sample " + std::to_string(i));
            return;
        }
    }
}

void testEmptyRead()
{
    RingBuffer ring(2, 1000, 4);
    std::array<float, 8> output;
    output.fill(9.0f);

    expect(ring.read(output.data(), 4, 1.0f) == 0,
           "empty read returns zero frames");
    expectSamples(output.data(), std::vector<float>(8, 0.0f),
                  "empty read produces silence");
}

void testNormalWriteRead()
{
    RingBuffer ring(2, 1000, 4);
    const std::array<float, 8> input{1, 2, 3, 4, 5, 6, 7, 8};
    std::array<float, 8> output{};

    expect(ring.write(input.data(), 4) == 4,
           "a normal write publishes every frame");
    expect(ring.bufferedFrames() == 4, "normal queue reports its occupancy");
    expect(ring.availableFrames() == 0, "normal queue has no available frames");
    expect(ring.discontinuityGeneration() == 0,
           "a normal write is not an overflow");
    expect(ring.read(output.data(), 4, 1.0f) == 4,
           "a normal queue can be drained");
    expectSamples(output.data(), {1, 2, 3, 4, 5, 6, 7, 8},
                  "normal queue preserves stereo samples");
}

void testCompletelyFullBuffer()
{
    RingBuffer ring(2, 1000, 4);
    const std::array<float, 8> input{1, 2, 3, 4, 5, 6, 7, 8};
    std::array<float, 8> output{};

    expect(ring.write(input.data(), 4) == 4,
           "an exactly full write publishes every frame");
    expect(ring.bufferedFrames() == 4, "full queue reports its capacity");
    expect(ring.availableFrames() == 0, "full queue has no available frames");
    expect(ring.discontinuityGeneration() == 0,
           "an exactly full write is not an overflow");

    expect(ring.write(input.data(), 1) == 0, "a full queue rejects new frames");
    expect(ring.bufferedFrames() == 4,
           "a full queue keeps its existing occupancy");
    expect(ring.discontinuityGeneration() == 1,
           "a rejected full-queue write records a discontinuity");

    // The consumer discards everything on the discontinuity.
    expect(ring.read(output.data(), 4, 1.0f) == 0,
           "a full-queue overflow flushes the queued audio");
}

void testWraparound()
{
    RingBuffer ring(2, 1000, 5);
    const std::array<float, 8> first{1, 101, 2, 102, 3, 103, 4, 104};
    const std::array<float, 8> second{5, 105, 6, 106, 7, 107, 8, 108};
    std::array<float, 6> discarded{};
    std::array<float, 10> output{};

    expect(ring.write(first.data(), 4) == 4, "wrap setup write succeeds");
    expect(ring.read(discarded.data(), 3, 1.0f) == 3,
           "wrap setup read advances the consumer");
    expect(ring.write(second.data(), 4) == 4,
           "write spanning physical end succeeds");
    expect(ring.read(output.data(), 5, 1.0f) == 5,
           "wrapped queue drains all frames");
    expectSamples(output.data(), {4, 104, 5, 105, 6, 106, 7, 107, 8, 108},
                  "wraparound preserves stereo frame order");
}

void testPartialWriteIsFrameAligned()
{
    RingBuffer ring(2, 1000, 4);
    const std::array<float, 6> first{1, 11, 2, 12, 3, 13};
    const std::array<float, 6> second{4, 14, 5, 15, 6, 16};

    expect(ring.write(first.data(), 3) == 3, "partial-write setup succeeds");
    const uint64_t readBeforePartialWrite = ring.consumerCursor();
    expect(ring.write(second.data(), 3) == 1,
           "partial write publishes the one complete frame that fits");
    expect(ring.consumerCursor() == readBeforePartialWrite,
           "partial producer write does not advance the consumer cursor");
    expect(ring.producerCursor() == uint64_t{4},
           "partial write advances by complete frames, not samples");
    expect(ring.discontinuityGeneration() == uint64_t{1},
           "dropped packet suffix records a discontinuity");
}

void testPartialWriteSilence()
{
    RingBuffer ring(2, 1000, 4);
    const std::array<float, 4> input{1, -1, 2, -2};

    expect(ring.write(input.data(), 2) == 2, "silence partial-write setup succeeds");
    expect(ring.writeSilence(3) == 2,
           "silence partial write publishes the complete frames that fit");
    expect(ring.discontinuityGeneration() == uint64_t{1},
           "dropped silence suffix records a discontinuity");
    expect(ring.producerCursor() == uint64_t{4},
           "silence partial write advances by complete frames only");
}

void testUnderrunZeroFill()
{
    RingBuffer ring(2, 1000, 4);
    const std::array<float, 4> input{1, -1, 2, -2};
    std::array<float, 12> output;
    output.fill(9.0f);

    expect(ring.write(input.data(), 2) == 2, "underrun setup writes two frames");
    expect(ring.read(output.data(), 6, 1.0f) == 2,
           "underrun read returns only the buffered frames");
    expectSamples(output.data(), {1, -1, 2, -2, 0, 0, 0, 0, 0, 0, 0, 0},
                  "underrun zero-fills the requested shortfall");
    expect(ring.bufferedFrames() == 0, "underrun drains the queue");

    output.fill(9.0f);
    expect(ring.read(output.data(), 4, 1.0f) == 0,
           "a read from an empty queue returns zero frames");
    expectSamples(output.data(), std::vector<float>(8, 0.0f),
                  "an empty-queue read renders full silence");
}

void testOverflowAndConsumerStaleFlush()
{
    RingBuffer ring(2, 1000, 4);
    const std::array<float, 8> stale{1, 11, 2, 12, 3, 13, 4, 14};
    const std::array<float, 4> dropped{90, 190, 91, 191};
    const std::array<float, 4> fresh{7, 17, 8, 18};
    std::array<float, 8> output;

    expect(ring.write(stale.data(), 4) == 4, "stale queue setup succeeds");
    const uint64_t readBeforeOverflow = ring.consumerCursor();
    expect(ring.write(dropped.data(), 2) == 0, "full queue drops new frames");
    expect(ring.write(dropped.data(), 2) == 0,
           "a second full-queue packet is also dropped");
    expect(ring.consumerCursor() == readBeforeOverflow,
           "producer overflow never advances the consumer cursor");
    expect(ring.bufferedFrames() == 4,
           "overflow does not overwrite consumer-owned storage");
    expect(ring.discontinuityGeneration() == uint64_t{2},
           "each producer discontinuity has a distinct generation");

    output.fill(9.0f);
    expect(ring.read(output.data(), 4, 1.0f) == 0,
           "consumer flushes stale queued region on discontinuity");
    expectSamples(output.data(), std::vector<float>(8, 0.0f),
                  "stale-data flush renders silence");
    expect(ring.consumerCursor() == ring.producerCursor(),
           "consumer owns and performs the stale-region discard");

    expect(ring.write(fresh.data(), 2) == 2,
           "fresh audio can arrive after the consumer flush");
    output.fill(9.0f);
    expect(ring.read(output.data(), 4, 1.0f) == 2,
           "playback resumes with newly arriving audio");
    expectSamples(output.data(), {7, 17, 8, 18, 0, 0, 0, 0},
                  "old audio is not replayed after a long-stall overflow");
}

void testUpstreamDiscontinuity()
{
    RingBuffer ring(2, 1000, 4);
    const std::array<float, 4> input{1, 2, 3, 4};
    std::array<float, 4> output{};
    ring.write(input.data(), 2);
    ring.signalDiscontinuity();

    expect(ring.read(output.data(), 2, 1.0f) == 0,
           "upstream discontinuity also flushes queued audio");
    expect(ring.bufferedFrames() == 0,
           "upstream discontinuity leaves no stale occupancy");
}

void testRepeatedDiscontinuities()
{
    RingBuffer ring(2, 1000, 4);
    const std::array<float, 8> stale{ 1, 11, 2, 12, 3, 13, 4, 14 };
    const std::array<float, 2> fresh{ 50, 60 };
    std::array<float, 4> output{};

    for (uint64_t generation = 1; generation <= 32; ++generation) {
        expect(ring.write(stale.data(), 4) == 4,
               "repeated-discontinuity setup succeeds");
        expect(ring.write(stale.data(), 1) == 0,
               "a full queue drops each repeated packet deterministically");
        expect(ring.discontinuityGeneration() == generation,
               "each repeated drop publishes one new generation");
        output.fill(7.0f);
        expect(ring.read(output.data(), 2, 1.0f) == 0,
               "each new generation flushes stale audio exactly once");
        expectSamples(output.data(), std::vector<float>(4, 0.0f),
                      "repeated discontinuity flush is silence");
        expect(ring.bufferedFrames() == 0,
               "repeated discontinuity leaves the queue empty");

        expect(ring.write(fresh.data(), 1) == 1,
               "fresh audio is accepted after each flush");
        output.fill(0.0f);
        expect(ring.read(output.data(), 1, 1.0f) == 1,
               "fresh audio is readable after each flush");
        expectSamples(output.data(), { 50, 60 },
                      "fresh audio is not confused with a prior generation");
    }
}

void testNonPowerOfTwoCapacity()
{
    RingBuffer ring(2, 1000, 7);
    expect(ring.capacity() == 7, "seven-frame capacity is preserved");

    const std::array<float, 8> first{1, 101, 2, 102, 3, 103, 4, 104};
    const std::array<float, 8> second{5, 105, 6, 106, 7, 107, 8, 108};
    std::array<float, 4> discarded{};
    std::array<float, 12> output{};

    expect(ring.write(first.data(), 4) == 4,
           "non-power-of-two setup write succeeds");
    expect(ring.read(discarded.data(), 2, 1.0f) == 2,
           "non-power-of-two setup read advances the consumer");
    expect(ring.write(second.data(), 4) == 4,
           "non-power-of-two wrap write succeeds");
    expect(ring.read(output.data(), 6, 1.0f) == 6,
           "non-power-of-two queue drains the wrapped frames");
    expectSamples(output.data(),
                  {3, 103, 4, 104, 5, 105, 6, 106, 7, 107, 8, 108},
                  "non-power-of-two wrap preserves stereo frame order");
    expect(ring.bufferedFrames() == 0,
           "non-power-of-two queue drains completely");
}

void testMultiChannelFrameAlignment()
{
    RingBuffer ring(3, 1000, 4);
    const std::array<float, 9> first{1, 2, 3, 4, 5, 6, 7, 8, 9};
    const std::array<float, 6> second{10, 11, 12, 13, 14, 15};
    std::array<float, 3> discarded{};
    std::array<float, 12> output{};

    expect(ring.write(first.data(), 3) == 3,
           "three-channel setup write succeeds");
    expect(ring.read(discarded.data(), 1, 1.0f) == 1,
           "three-channel setup read advances by one complete frame");
    // Two slots are free after the setup read; both wrap the physical end.
    expect(ring.write(second.data(), 2) == 2,
           "three-channel wrap write succeeds");
    expect(ring.read(output.data(), 4, 1.0f) == 4,
           "three-channel wrapped queue drains all frames");
    expectSamples(output.data(),
                  {4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
                  "three-channel wrap preserves frame alignment");

    // Partial publication is also frame aligned: after three frames occupy
    // the queue, one complete frame fits and the remaining two complete
    // frames are dropped with a discontinuity.
    expect(ring.write(first.data(), 3) == 3,
           "three-channel partial-write setup succeeds");
    expect(ring.write(first.data(), 3) == 1,
           "three-channel partial write publishes only complete frames");
    expect(ring.discontinuityGeneration() == uint64_t{1},
           "three-channel dropped suffix records a discontinuity");
    expect(ring.producerCursor() == uint64_t{9},
           "three-channel partial write advances by complete frames only");

    std::array<float, 9> flushed{};
    expect(ring.read(flushed.data(), 3, 1.0f) == 0,
           "three-channel discontinuity flush renders silence");

    const std::array<float, 3> fresh{100, 101, 102};
    std::array<float, 3> recovered{};
    expect(ring.write(fresh.data(), 1) == 1,
           "three-channel fresh frame is accepted after the flush");
    expect(ring.read(recovered.data(), 1, 1.0f) == 1,
           "three-channel recovery reads the fresh frame");
    expectSamples(recovered.data(), {100, 101, 102},
                  "three-channel recovery preserves the complete frame");
}

void testDiscontinuityDuringCopy()
{
    // A producer thread publishes a discontinuity while the consumer thread
    // is inside read()'s copy loop. The consumer must discard the copied
    // samples, zero-fill the request, and catch its cursor up to the latest
    // published write cursor. The large request plus the per-sample volume
    // path keeps the copy window wide enough that a spinning producer
    // reliably signals inside it, and the handshake flags order the two
    // threads around it.
    constexpr uint32_t rounds = 4;
    RingBuffer ring(2, 1000, 1000000);
    const size_t capacity = ring.capacity();
    expect(capacity == 1000000, "mid-copy queue sizes to one million frames");

    std::vector<float> fillData(capacity * 2, 1.0f);
    std::vector<float> output(capacity * 2);
    std::atomic<uint32_t> producedRound{0};
    std::atomic<bool> copying{false};
    std::atomic<bool> quit{false};
    std::atomic<bool> producerOk{true};

    std::thread producer([&]() {
        for (uint32_t round = 1; round <= rounds; ++round) {
            if (ring.write(fillData.data(), capacity) != capacity) {
                producerOk.store(false, std::memory_order_release);
                return;
            }
            producedRound.store(round, std::memory_order_release);
            while (!copying.load(std::memory_order_acquire)) {
                if (quit.load(std::memory_order_acquire))
                    return;
            }
            ring.signalDiscontinuity();
            while (copying.load(std::memory_order_acquire)) {
                if (quit.load(std::memory_order_acquire))
                    return;
            }
        }
    });

    for (uint32_t round = 1; round <= rounds; ++round) {
        while (producedRound.load(std::memory_order_acquire) < round) {
            if (!producerOk.load(std::memory_order_acquire))
                break;
        }
        if (!producerOk.load(std::memory_order_acquire))
            break;

        std::fill(output.begin(), output.end(), 9.0f);
        copying.store(true, std::memory_order_release);
        const size_t got = ring.read(output.data(), capacity, 0.5f);
        copying.store(false, std::memory_order_release);

        expect(got == 0,
               "a discontinuity during the copy discards the copied samples");
        bool allZero = true;
        for (float sample : output) {
            if (sample != 0.0f) {
                allZero = false;
                break;
            }
        }
        expect(allZero,
               "a mid-copy discontinuity zero-fills the requested output");
        // Cursor and occupancy checks happen after the producer has finished
        // every round: releasing the copying flag lets the producer refill
        // for the next round, which legitimately advances the write cursor.
    }

    quit.store(true, std::memory_order_release);
    producer.join();
    expect(producerOk.load(std::memory_order_acquire),
           "the producer thread publishes every exact fill");
    expect(ring.discontinuityGeneration() == uint64_t{rounds},
           "each concurrent discontinuity publishes one generation");
    expect(ring.consumerCursor() == ring.producerCursor(),
           "the consumer catches its cursor up to the latest published end");
    expect(ring.consumerCursor() == uint64_t{rounds} * capacity,
           "the consumer cursor reaches the last published frame count");
    expect(ring.bufferedFrames() == 0,
           "the concurrent discontinuities leave no stale occupancy");

    // Recovery: post-discontinuity audio is readable and nothing stale is
    // replayed.
    const std::array<float, 4> fresh{1, -1, 2, -2};
    std::array<float, 8> recovered{};
    recovered.fill(9.0f);
    expect(ring.write(fresh.data(), 2) == 2,
           "fresh audio is accepted after concurrent discontinuities");
    expect(ring.read(recovered.data(), 4, 1.0f) == 2,
           "playback resumes after concurrent discontinuities");
    expectSamples(recovered.data(), {1, -1, 2, -2, 0, 0, 0, 0},
                  "recovery returns only post-discontinuity audio");
}

void testMonotonicCursors()
{
    RingBuffer ring(2, 1000, 64);
    constexpr size_t totalFrames = 1000000;
    // Chunk sizes sum to 80, so totalFrames is an exact multiple of the
    // pattern; the largest chunk (17) is well below the 64-frame capacity.
    static constexpr size_t kChunkPattern[] = {7, 11, 5, 13, 9, 15, 3, 17};
    std::array<float, 34> input{};
    std::array<float, 34> output{};
    size_t produced = 0;
    size_t consumed = 0;
    size_t patternIndex = 0;
    uint64_t lastWriteCursor = 0;

    while (produced < totalFrames) {
        const size_t frames =
            kChunkPattern[patternIndex++ % 8];
        for (size_t i = 0; i < frames; ++i) {
            const float value = static_cast<float>(produced + i);
            input[i * 2] = value;
            input[i * 2 + 1] = -value;
        }

        expect(ring.write(input.data(), frames) == frames,
               "monotonic producer write succeeds");
        produced += frames;
        const uint64_t writeCursor = ring.producerCursor();
        expect(writeCursor == uint64_t{produced},
               "producer cursor equals the total published frame count");
        expect(writeCursor > lastWriteCursor,
               "producer cursor is strictly monotonic");
        lastWriteCursor = writeCursor;

        expect(ring.read(output.data(), frames, 1.0f) == frames,
               "monotonic consumer read succeeds");
        for (size_t i = 0; i < frames; ++i) {
            const float value = static_cast<float>(consumed + i);
            if (output[i * 2] != value || output[i * 2 + 1] != -value) {
                expect(false, "monotonic sequence preserves frame order at frame "
                                  + std::to_string(consumed + i));
                break;
            }
        }
        consumed += frames;
    }

    expect(ring.consumerCursor() == uint64_t{totalFrames},
           "consumer cursor reaches the total consumed frame count");
    expect(ring.producerCursor() == uint64_t{totalFrames},
           "producer cursor reaches the total published frame count");
    expect(totalFrames > ring.capacity() * 1000,
           "cursors advance far beyond the physical capacity");
    expect(ring.discontinuityGeneration() == uint64_t{0},
           "monotonic progress never reports a discontinuity");
    expect(ring.bufferedFrames() == 0,
           "monotonic sequence drains completely");
}

void testNonPowerOfTwo4800()
{
    RingBuffer ring(2, 48000, 100);
    expect(ring.capacity() == 4800,
           "100 ms at 48 kHz sizes to a 4800-frame capacity");

    auto fillPattern = [](std::vector<float>& buf, size_t firstFrame) {
        for (size_t i = 0; i < buf.size() / 2; ++i) {
            const float value = static_cast<float>(firstFrame + i);
            buf[i * 2] = value;
            buf[i * 2 + 1] = -value;
        }
    };
    std::vector<float> first(3000 * 2);
    std::vector<float> second(2800 * 2);
    std::vector<float> fill(4800 * 2);
    std::vector<float> output(4800 * 2);
    std::vector<float> discard(1000 * 2);
    fillPattern(first, 0);
    fillPattern(second, 3000);
    fillPattern(fill, 0);

    expect(ring.write(first.data(), 3000) == 3000,
           "4800-capacity setup write succeeds");
    expect(ring.read(discard.data(), 1000, 1.0f) == 1000,
           "4800-capacity setup read advances the consumer");
    expect(ring.write(second.data(), 2800) == 2800,
           "4800-capacity wrap write crosses the physical end");
    expect(ring.bufferedFrames() == 4800,
           "4800-capacity queue is fully saturated");
    expect(ring.availableFrames() == 0,
           "saturated queue reports no available frames");
    expect(ring.discontinuityGeneration() == uint64_t{0},
           "an exact saturation write is not an overflow");

    expect(ring.read(output.data(), 4800, 1.0f) == 4800,
           "4800-capacity drain reads every wrapped frame");
    for (size_t i = 0; i < 4800; ++i) {
        const float value = static_cast<float>(1000 + i);
        if (output[i * 2] != value || output[i * 2 + 1] != -value) {
            expect(false, "4800-capacity wrap preserves frame order at frame "
                              + std::to_string(i));
            break;
        }
    }

    // Fully saturated rejection, flush, and recovery.
    expect(ring.write(fill.data(), 4800) == 4800,
           "4800-capacity refill succeeds");
    const uint64_t readBeforeOverflow = ring.consumerCursor();
    expect(ring.write(fill.data(), 1) == 0,
           "saturated 4800-capacity queue rejects new frames");
    expect(ring.consumerCursor() == readBeforeOverflow,
           "saturated rejection never advances the consumer cursor");
    expect(ring.discontinuityGeneration() == uint64_t{1},
           "saturated rejection publishes a discontinuity");
    std::vector<float> flushed(4800 * 2, 9.0f);
    expect(ring.read(flushed.data(), 4800, 1.0f) == 0,
           "saturated rejection flushes the queue on the next read");
    bool allZero = true;
    for (float sample : flushed) {
        if (sample != 0.0f) {
            allZero = false;
            break;
        }
    }
    expect(allZero, "saturated rejection flush renders silence");
}

void testMultiChannelVariants()
{
    for (size_t channels : { size_t{1}, size_t{2}, size_t{6}, size_t{8} }) {
        RingBuffer ring(channels, 1000, 4);
        auto makeFrame = [channels](std::vector<float>& buf, size_t offset,
                                    size_t frame) {
            for (size_t ch = 0; ch < channels; ++ch)
                buf[offset + ch] =
                    static_cast<float>(frame * channels + ch + 1);
        };

        std::vector<float> packet(3 * channels);
        for (size_t f = 0; f < 3; ++f)
            makeFrame(packet, f * channels, f);
        expect(ring.write(packet.data(), 3) == 3,
               "multi-channel setup write succeeds at "
                   + std::to_string(channels) + " channels");

        std::vector<float> discard(channels);
        expect(ring.read(discard.data(), 1, 1.0f) == 1,
               "multi-channel setup read advances one complete frame at "
                   + std::to_string(channels) + " channels");

        std::vector<float> packet2(2 * channels);
        for (size_t f = 0; f < 2; ++f)
            makeFrame(packet2, f * channels, 3 + f);
        expect(ring.write(packet2.data(), 2) == 2,
               "multi-channel wrap write succeeds at "
                   + std::to_string(channels) + " channels");

        std::vector<float> output(4 * channels);
        expect(ring.read(output.data(), 4, 1.0f) == 4,
               "multi-channel wrapped queue drains all frames at "
                   + std::to_string(channels) + " channels");
        bool aligned = true;
        for (size_t f = 0; f < 4; ++f) {
            for (size_t ch = 0; ch < channels; ++ch) {
                const float expected =
                    static_cast<float>((1 + f) * channels + ch + 1);
                if (output[f * channels + ch] != expected) {
                    aligned = false;
                    break;
                }
            }
            if (!aligned)
                break;
        }
        expect(aligned, "multi-channel wrap preserves frame alignment at "
                            + std::to_string(channels) + " channels");

        // Partial publication publishes complete frames only.
        std::vector<float> packet3(3 * channels);
        for (size_t f = 0; f < 3; ++f)
            makeFrame(packet3, f * channels, 6 + f);
        expect(ring.write(packet3.data(), 3) == 3,
               "multi-channel partial-write setup succeeds at "
                   + std::to_string(channels) + " channels");
        expect(ring.write(packet3.data(), 3) == 1,
               "multi-channel partial write publishes only complete frames at "
                   + std::to_string(channels) + " channels");
        expect(ring.discontinuityGeneration() == uint64_t{1},
               "multi-channel dropped suffix records a discontinuity at "
                   + std::to_string(channels) + " channels");

        std::vector<float> flushed(2 * channels, 9.0f);
        expect(ring.read(flushed.data(), 2, 1.0f) == 0,
               "multi-channel discontinuity flush renders silence at "
                   + std::to_string(channels) + " channels");
    }
}

void testRandomizedProducerConsumer()
{
    constexpr size_t totalFrames = 200000;
    RingBuffer ring(2, 1000, 512);
    std::atomic<bool> producerFailed{false};
    std::atomic<bool> consumerFailed{false};

    std::thread producer([&]() {
        std::mt19937 rng(12345);
        std::uniform_int_distribution<size_t> dist(1, 64);
        std::vector<float> packet(64 * 2);
        size_t produced = 0;
        while (produced < totalFrames) {
            if (consumerFailed.load(std::memory_order_acquire))
                return;
            const size_t chunk = std::min(dist(rng), totalFrames - produced);
            // availableFrames() is only a pacing hint; write()'s return
            // value below is the actual admission result.
            if (ring.availableFrames() < chunk) {
                std::this_thread::yield();
                continue;
            }
            for (size_t i = 0; i < chunk; ++i) {
                const float value = static_cast<float>(produced + i);
                packet[i * 2] = value;
                packet[i * 2 + 1] = -value;
            }
            if (ring.write(packet.data(), chunk) != chunk) {
                producerFailed.store(true, std::memory_order_release);
                return;
            }
            produced += chunk;
        }
    });

    std::thread consumer([&]() {
        std::mt19937 rng(54321);
        std::uniform_int_distribution<size_t> dist(1, 64);
        std::vector<float> packet(64 * 2);
        size_t consumed = 0;
        while (consumed < totalFrames) {
            if (producerFailed.load(std::memory_order_acquire))
                return;
            const size_t request = std::min(dist(rng), totalFrames - consumed);
            const size_t received = ring.read(packet.data(), request, 1.0f);
            if (received == 0) {
                std::this_thread::yield();
                continue;
            }
            for (size_t i = 0; i < received; ++i) {
                const float expected = static_cast<float>(consumed + i);
                if (packet[i * 2] != expected || packet[i * 2 + 1] != -expected) {
                    consumerFailed.store(true, std::memory_order_release);
                    return;
                }
            }
            consumed += received;
        }
    });

    producer.join();
    consumer.join();
    expect(!producerFailed.load(std::memory_order_acquire),
           "randomized producer publishes every admitted chunk");
    expect(!consumerFailed.load(std::memory_order_acquire),
           "randomized consumer observes ordered complete frames");
    expect(ring.discontinuityGeneration() == uint64_t{0},
           "randomized paced workload never reports a discontinuity");
    expect(ring.consumerCursor() == uint64_t{totalFrames},
           "randomized consumer drains the exact frame total");
    expect(ring.producerCursor() == uint64_t{totalFrames},
           "randomized producer publishes the exact frame total");
    expect(ring.bufferedFrames() == 0,
           "randomized workload drains the queue completely");
}

void testLongWraparoundSequence()
{
    RingBuffer ring(2, 1000, 5);
    std::array<float, 10> input{};
    std::array<float, 10> output{};
    int nextValue = 0;

    // Repeatedly cross the physical storage boundary while keeping the queue
    // below capacity. This exercises both split memcpy paths over many cycles.
    for (int cycle = 0; cycle < 200; ++cycle) {
        const size_t firstFrames = static_cast<size_t>((cycle % 4) + 1);
        for (size_t frame = 0; frame < firstFrames; ++frame) {
            input[frame * 2] = static_cast<float>(nextValue);
            input[frame * 2 + 1] = -static_cast<float>(nextValue);
            ++nextValue;
        }
        expect(ring.write(input.data(), firstFrames) == firstFrames,
               "long wraparound producer write succeeds");
        expect(ring.read(output.data(), firstFrames, 1.0f) == firstFrames,
               "long wraparound consumer read succeeds");
        int expected = nextValue - static_cast<int>(firstFrames);
        for (size_t frame = 0; frame < firstFrames; ++frame, ++expected) {
            expect(output[frame * 2] == static_cast<float>(expected)
                       && output[frame * 2 + 1] == -static_cast<float>(expected),
                   "long wraparound preserves frame order");
        }
    }
    expect(ring.discontinuityGeneration() == uint64_t{0},
           "wraparound without overflow does not report discontinuities");
    expect(ring.bufferedFrames() == 0,
           "long wraparound sequence drains completely");
}

void testCapacityOverflow()
{
    bool threw = false;
    try {
        // channels * frames overflows size_t during construction.
        RingBuffer invalid(std::numeric_limits<size_t>::max() / sizeof(float) + 1,
                           1000, 4);
    } catch (const std::length_error&) {
        threw = true;
    }
    expect(threw, "sample-count multiplication overflow is rejected");

    threw = false;
    try {
        // channels * frames fits in size_t, but the byte count
        // (samples * sizeof(float)) would overflow.
        RingBuffer invalid(std::numeric_limits<size_t>::max() / 4, 1000, 4);
    } catch (const std::length_error&) {
        threw = true;
    }
    expect(threw, "sample-buffer byte-count overflow is rejected");
}

void testInvalidConfiguration()
{
    bool threw = false;
    try {
        RingBuffer invalid(0, 48000, 50);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "zero-channel queues are rejected");

    threw = false;
    try {
        RingBuffer invalid(2, 0, 50);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "zero-rate queues are rejected");

    threw = false;
    try {
        RingBuffer invalid(2, 48000, 0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    expect(threw, "zero-duration queues are rejected");
}

void testActualSampleRateSizing()
{
    RingBuffer at44100(2, 44100, 50);
    RingBuffer at48000(2, 48000, 50);
    RingBuffer fractional(2, 44100, 25);

    expect(at44100.capacity() == 2205,
           "44.1 kHz queue uses the actual sample rate");
    expect(at48000.capacity() == 2400,
           "48 kHz queue uses the actual sample rate");
    expect(fractional.capacity() == 1103,
           "duration sizing rounds fractional frames up");
    expect(std::abs(at44100.capacityDurationMs() - 50.0) < 0.0001,
           "capacity duration reports physical storage duration");
}

void testVolumeAndSilenceWrite()
{
    RingBuffer ring(2, 1000, 4);
    const std::array<float, 4> input{1, -1, 0.5f, -0.5f};
    std::array<float, 8> output{};
    ring.write(input.data(), 2);
    ring.writeSilence(2);

    expect(ring.read(output.data(), 4, 0.5f) == 4,
           "volume test drains all frames");
    expectSamples(output.data(), {0.5f, -0.5f, 0.25f, -0.25f, 0, 0, 0, 0},
                  "volume applies per sample and silence remains silent");
}

void testRepeatedProducerConsumerProgress()
{
    constexpr size_t totalFrames = 20000;
    RingBuffer ring(2, 1000, 128);
    std::atomic<bool> producerFailed{false};
    std::atomic<bool> consumerFailed{false};

    std::thread producer([&]() {
        size_t produced = 0;
        std::array<float, 14> packet{};
        while (produced < totalFrames) {
            if (consumerFailed.load(std::memory_order_acquire))
                return;
            const size_t frames = std::min<size_t>(7, totalFrames - produced);
            // availableFrames() is only a diagnostic hint; the write()
            // return value below is the actual admission result.
            if (ring.availableFrames() < frames) {
                std::this_thread::yield();
                continue;
            }
            for (size_t i = 0; i < frames; ++i) {
                const float value = static_cast<float>(produced + i);
                packet[i * 2] = value;
                packet[i * 2 + 1] = -value;
            }
            if (ring.write(packet.data(), frames) != frames) {
                producerFailed.store(true, std::memory_order_release);
                return;
            }
            produced += frames;
        }
    });

    std::thread consumer([&]() {
        size_t consumed = 0;
        std::array<float, 10> packet{};
        while (consumed < totalFrames) {
            if (producerFailed.load(std::memory_order_acquire))
                return;
            const size_t request = std::min<size_t>(5, totalFrames - consumed);
            const size_t received = ring.read(packet.data(), request, 1.0f);
            if (received == 0) {
                std::this_thread::yield();
                continue;
            }
            for (size_t i = 0; i < received; ++i) {
                const float expected = static_cast<float>(consumed + i);
                if (packet[i * 2] != expected || packet[i * 2 + 1] != -expected) {
                    consumerFailed.store(true, std::memory_order_release);
                    return;
                }
            }
            consumed += received;
        }
    });

    producer.join();
    consumer.join();
    expect(!producerFailed.load(std::memory_order_acquire),
           "producer repeatedly makes progress without a false overflow");
    expect(!consumerFailed.load(std::memory_order_acquire),
           "consumer repeatedly observes ordered complete frames");
    expect(ring.discontinuityGeneration() == uint64_t{0},
           "coordinated SPSC progress does not report a discontinuity");
    expect(ring.bufferedFrames() == 0,
           "repeated producer/consumer run drains the queue");
}

} // namespace

int main()
{
    testEmptyRead();
    testNormalWriteRead();
    testCompletelyFullBuffer();
    testWraparound();
    testPartialWriteIsFrameAligned();
    testPartialWriteSilence();
    testUnderrunZeroFill();
    testOverflowAndConsumerStaleFlush();
    testUpstreamDiscontinuity();
    testRepeatedDiscontinuities();
    testNonPowerOfTwoCapacity();
    testNonPowerOfTwo4800();
    testMultiChannelFrameAlignment();
    testMultiChannelVariants();
    testDiscontinuityDuringCopy();
    testMonotonicCursors();
    testRandomizedProducerConsumer();
    testLongWraparoundSequence();
    testCapacityOverflow();
    testInvalidConfiguration();
    testActualSampleRateSizing();
    testVolumeAndSilenceWrite();
    testRepeatedProducerConsumerProgress();

    if (failures != 0) {
        std::cerr << failures << " RingBuffer assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "RingBuffer tests passed\n";
    return EXIT_SUCCESS;
}
