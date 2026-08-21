#include "core/adaptive_audio_buffer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

bool closeEnough(float actual, float expected)
{
    return std::fabs(actual - expected) < 0.0001f;
}

std::vector<float> sequence(size_t first, size_t count)
{
    std::vector<float> result(count);
    for (size_t i = 0; i < count; ++i)
        result[i] = static_cast<float>(first + i);
    return result;
}

void testStartupBuildsSafetyMargin()
{
    RingBuffer ring(1, 1000, 100);
    AdaptiveAudioBufferReader reader(ring, 10, 20, 5, 1.0f);
    const std::vector<float> first = sequence(0, 19);
    expect(ring.write(first.data(), first.size()) == first.size(),
           "startup setup write succeeds");

    std::array<float, 10> output;
    output.fill(99.0f);
    expect(reader.render(ring, output.data(), output.size(), 1.0f) == 0,
           "startup waits until the target occupancy is available");
    expect(std::all_of(output.begin(), output.end(), [](float sample) {
               return sample == 0.0f;
           }),
           "startup wait produces a complete silent block");
    expect(ring.consumerBufferedFrames() == 19,
           "startup wait does not consume a short fragment");

    const float twentieth = 19.0f;
    expect(ring.write(&twentieth, 1) == 1,
           "startup target can be completed");
    expect(reader.render(ring, output.data(), output.size(), 1.0f) == 10,
           "playback starts after the target occupancy is reached");
    for (size_t i = 0; i < output.size(); ++i) {
        const float ramp = static_cast<float>(i + 1) / output.size();
        expect(closeEnough(output[i], static_cast<float>(i) * ramp),
               "startup playback preserves order while fading in");
    }
    expect(!reader.isRebuffering(),
           "reader leaves rebuffering state after a complete block");
}

void testFastSourceConsumesOneExtraFrame()
{
    RingBuffer ring(1, 1000, 100);
    AdaptiveAudioBufferReader reader(ring, 10, 20, 5, 1.0f);
    const std::vector<float> input = sequence(0, 40);
    expect(ring.write(input.data(), input.size()) == input.size(),
           "fast-source setup write succeeds");

    std::array<float, 10> output{};
    expect(reader.render(ring, output.data(), output.size(), 1.0f) == 11,
           "high occupancy consumes one extra input frame");
    expect(closeEnough(output.front(), 0.0f)
               && closeEnough(output.back(), 10.0f),
           "speed-up correction preserves both block endpoints");
    expect(ring.consumerBufferedFrames() == 29,
           "speed-up correction advances the queue by eleven frames");
    for (size_t i = 1; i < output.size(); ++i) {
        expect(output[i] > output[i - 1],
               "speed-up correction remains monotonic");
    }
}

void testSlowSourceConsumesOneFewerFrame()
{
    RingBuffer ring(1, 1000, 100);
    AdaptiveAudioBufferReader reader(ring, 10, 20, 5, 1.0f);
    std::vector<float> input = sequence(0, 20);
    expect(ring.write(input.data(), input.size()) == input.size(),
           "slow-source setup write succeeds");

    std::array<float, 10> output{};
    expect(reader.render(ring, output.data(), output.size(), 1.0f) == 10,
           "initial target block consumes normally");

    input = sequence(20, 4);
    expect(ring.write(input.data(), input.size()) == input.size(),
           "slow-source follow-up write succeeds");
    expect(ring.consumerBufferedFrames() == 14,
           "slow-source setup is below the low watermark");
    expect(reader.render(ring, output.data(), output.size(), 1.0f) == 9,
           "low occupancy consumes one fewer input frame");
    expect(closeEnough(output.front(), 10.0f)
               && closeEnough(output.back(), 18.0f),
           "slow-down correction stretches the available endpoints");
    expect(ring.consumerBufferedFrames() == 5,
           "slow-down correction leaves one additional frame queued");
}

void testUnderrunRebuildsMarginWithoutDrainingFragment()
{
    RingBuffer ring(1, 1000, 100);
    AdaptiveAudioBufferReader reader(ring, 10, 20, 5, 1.0f);
    std::vector<float> input = sequence(0, 20);
    ring.write(input.data(), input.size());

    std::array<float, 10> output{};
    reader.render(ring, output.data(), output.size(), 1.0f);
    input = sequence(20, 4);
    ring.write(input.data(), input.size());
    reader.render(ring, output.data(), output.size(), 1.0f);
    expect(ring.consumerBufferedFrames() == 5,
           "underrun setup leaves a five-frame fragment");

    output.fill(99.0f);
    expect(reader.render(ring, output.data(), output.size(), 1.0f) == 0,
           "a too-short fragment triggers rebuffering");
    expect(reader.isRebuffering(),
           "reader reports rebuffering after an underrun");
    expect(ring.consumerBufferedFrames() == 5,
           "underrun does not discard the short fragment");
    expect(std::all_of(output.begin(), output.end(), [](float sample) {
               return sample == 0.0f;
           }),
           "underrun produces a complete silent block");

    input = sequence(24, 14);
    ring.write(input.data(), input.size());
    expect(reader.render(ring, output.data(), output.size(), 1.0f) == 0,
           "rebuffering continues below the target occupancy");
    const float finalSample = 38.0f;
    ring.write(&finalSample, 1);
    expect(reader.render(ring, output.data(), output.size(), 1.0f) == 10,
           "playback resumes once the target is rebuilt");
    expect(closeEnough(output.front(), 1.9f)
               && closeEnough(output.back(), 28.0f),
           "rebuffering resumes with the preserved fragment and fades in");
}

void testVolumeChangesAreRampedWithinOneBlock()
{
    RingBuffer ring(1, 1000, 100);
    AdaptiveAudioBufferReader reader(ring, 10, 10, 10);
    std::array<float, 20> input;
    input.fill(1.0f);
    ring.write(input.data(), input.size());

    std::array<float, 10> output{};
    expect(reader.render(ring, output.data(), output.size(), 1.0f) == 10,
           "fade-in consumes a complete block");
    for (size_t i = 0; i < output.size(); ++i) {
        expect(closeEnough(output[i], static_cast<float>(i + 1) / 10.0f),
               "fade-in gain advances smoothly across the block");
    }

    expect(reader.render(ring, output.data(), output.size(), 0.0f) == 10,
           "fade-out consumes a complete block");
    for (size_t i = 0; i < output.size(); ++i) {
        expect(closeEnough(output[i], 1.0f - static_cast<float>(i + 1) / 10.0f),
               "fade-out gain decreases smoothly across the block");
    }
}

} // namespace

int main()
{
    testStartupBuildsSafetyMargin();
    testFastSourceConsumesOneExtraFrame();
    testSlowSourceConsumesOneFewerFrame();
    testUnderrunRebuildsMarginWithoutDrainingFragment();
    testVolumeChangesAreRampedWithinOneBlock();

    if (failures != 0) {
        std::cerr << failures << " adaptive audio buffer assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "Adaptive audio buffer tests passed\n";
    return EXIT_SUCCESS;
}
