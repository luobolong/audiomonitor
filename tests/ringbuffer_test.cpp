#include "core/ringbuffer.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
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

void testFullBuffer()
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
    expect(ring.read(output.data(), 4, 1.0f) == 4,
           "full queue can be drained");
    expectSamples(output.data(), {1, 2, 3, 4, 5, 6, 7, 8},
                  "full queue preserves stereo samples");
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
    const size_t readBeforePartialWrite = ring.consumerCursor();
    expect(ring.write(second.data(), 3) == 1,
           "partial write publishes the one complete frame that fits");
    expect(ring.consumerCursor() == readBeforePartialWrite,
           "partial producer write does not advance the consumer cursor");
    expect(ring.producerCursor() == 4,
           "partial write advances by complete frames, not samples");
    expect(ring.discontinuityGeneration() == 1,
           "dropped packet suffix records a discontinuity");
}

void testOverflowAndConsumerStaleFlush()
{
    RingBuffer ring(2, 1000, 4);
    const std::array<float, 8> stale{1, 11, 2, 12, 3, 13, 4, 14};
    const std::array<float, 4> dropped{90, 190, 91, 191};
    const std::array<float, 4> fresh{7, 17, 8, 18};
    std::array<float, 8> output;

    expect(ring.write(stale.data(), 4) == 4, "stale queue setup succeeds");
    const size_t readBeforeOverflow = ring.consumerCursor();
    expect(ring.write(dropped.data(), 2) == 0, "full queue drops new frames");
    expect(ring.write(dropped.data(), 2) == 0,
           "a second full-queue packet is also dropped");
    expect(ring.consumerCursor() == readBeforeOverflow,
           "producer overflow never advances the consumer cursor");
    expect(ring.bufferedFrames() == 4,
           "overflow does not overwrite consumer-owned storage");
    expect(ring.discontinuityGeneration() == 2,
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

    for (size_t generation = 1; generation <= 32; ++generation) {
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
    expect(ring.discontinuityGeneration() == 0,
           "wraparound without overflow does not report discontinuities");
    expect(ring.bufferedFrames() == 0,
           "long wraparound sequence drains completely");
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
    expect(ring.discontinuityGeneration() == 0,
           "coordinated SPSC progress does not report a discontinuity");
    expect(ring.bufferedFrames() == 0,
           "repeated producer/consumer run drains the queue");
}

} // namespace

int main()
{
    testEmptyRead();
    testFullBuffer();
    testWraparound();
    testPartialWriteIsFrameAligned();
    testOverflowAndConsumerStaleFlush();
    testUpstreamDiscontinuity();
    testRepeatedDiscontinuities();
    testLongWraparoundSequence();
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
