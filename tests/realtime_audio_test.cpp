#include "core/realtime_audio.h"

#include <array>
#include <cmath>
#include <iostream>

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
    return std::fabs(actual - expected) < 0.00001f;
}

void testUnityGainCopiesStereo()
{
    const std::array<float, 4> left{ 0.1f, -0.2f, 0.3f, -0.4f };
    const std::array<float, 4> right{ -0.5f, 0.6f, -0.7f, 0.8f };
    std::array<float, 4> outLeft{};
    std::array<float, 4> outRight{};

    realtime_audio::processStereo(
        left.data(), right.data(), outLeft.data(), outRight.data(), left.size(), 1.0f);

    expect(outLeft == left, "unity gain must copy the left channel");
    expect(outRight == right, "unity gain must copy the right channel");
}

void testVolumeMultiplication()
{
    const std::array<float, 3> left{ 1.0f, -0.5f, 0.25f };
    const std::array<float, 3> right{ -1.0f, 0.5f, -0.25f };
    std::array<float, 3> outLeft{};
    std::array<float, 3> outRight{};

    realtime_audio::processStereo(
        left.data(), right.data(), outLeft.data(), outRight.data(), left.size(), 0.4f);

    expect(closeEnough(outLeft[0], 0.4f) && closeEnough(outLeft[1], -0.2f)
               && closeEnough(outLeft[2], 0.1f),
           "volume must multiply every left sample");
    expect(closeEnough(outRight[0], -0.4f) && closeEnough(outRight[1], 0.2f)
               && closeEnough(outRight[2], -0.1f),
           "volume must multiply every right sample");
}

void testMissingInputSilencesWholeStereoCycle()
{
    const std::array<float, 3> right{ 0.2f, 0.3f, 0.4f };
    std::array<float, 3> outLeft{ 9.0f, 9.0f, 9.0f };
    std::array<float, 3> outRight{ 9.0f, 9.0f, 9.0f };

    realtime_audio::processStereo(
        nullptr, right.data(), outLeft.data(), outRight.data(), right.size(), 1.0f);

    expect(outLeft == std::array<float, 3>{}, "missing input must silence left output");
    expect(outRight == std::array<float, 3>{}, "missing input must silence right output");
}

void testFrameBoundsAndIndependentOutputs()
{
    const std::array<float, 4> left{ 1.0f, 2.0f, 3.0f, 4.0f };
    const std::array<float, 4> right{ 5.0f, 6.0f, 7.0f, 8.0f };
    std::array<float, 6> guardedLeft{ -11.0f, -11.0f, -11.0f, -11.0f, 1234.0f, 5678.0f };

    realtime_audio::processStereo(
        left.data(), right.data(), guardedLeft.data(), nullptr, left.size(), 1.0f);

    expect(guardedLeft[0] == 1.0f && guardedLeft[3] == 4.0f,
           "the available stereo output channel must be written");
    expect(guardedLeft[4] == 1234.0f && guardedLeft[5] == 5678.0f,
           "processing must not write beyond the requested frame count");

    realtime_audio::processStereo(
        left.data(), right.data(), guardedLeft.data(), nullptr, 0, 0.5f);
    expect(guardedLeft[4] == 1234.0f && guardedLeft[5] == 5678.0f,
           "a zero-frame cycle must not write output");
}

} // namespace

int main()
{
    testUnityGainCopiesStereo();
    testVolumeMultiplication();
    testMissingInputSilencesWholeStereoCycle();
    testFrameBoundsAndIndependentOutputs();
    return failures == 0 ? 0 : 1;
}
