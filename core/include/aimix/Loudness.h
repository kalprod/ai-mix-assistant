#pragma once

// ITU-R BS.1770-4 / EBU R128 loudness.
//
//   K-weighting (high shelf + RLB high-pass), coefficients derived for any
//   sample rate.
//   Momentary  : 400 ms sliding window
//   Short-term : 3 s sliding window
//   Integrated : 400 ms blocks, 75 % overlap, absolute gate -70 LUFS,
//                relative gate -10 LU, accumulated in a fixed 0.1 LU histogram
//                so it runs forever in constant memory on the audio thread.
//
// Input layout matches juce::AudioBuffer<float>::getArrayOfReadPointers().

#include <array>
#include <cmath>
#include <cstdint>

namespace aimix
{
constexpr float kLufsFloor = -144.0f;   // reported for silence

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double z1 = 0, z2 = 0;

    double process (double x) noexcept   // transposed direct form II
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    void reset() noexcept { z1 = z2 = 0; }
};

void designKWeighting (double sampleRate, Biquad& highShelf, Biquad& highPass) noexcept;

inline float energyToLufs (double meanSquare) noexcept
{
    return meanSquare > 1.0e-15 ? (float) (-0.691 + 10.0 * std::log10 (meanSquare)) : kLufsFloor;
}

class LoudnessMeter
{
public:
    static constexpr int kMaxChannels   = 2;
    static constexpr int kShortTermSubs = 30;    // 30 x 100 ms
    static constexpr float kHistMinLufs = -70.0f;
    static constexpr float kHistMaxLufs = 10.0f;
    static constexpr int kHistBins      = 800;   // 0.1 LU resolution

    void prepare (double sampleRate, int numChannels) noexcept;
    void reset() noexcept;

    // Real-time safe. Channels beyond kMaxChannels are ignored.
    void process (const float* const* channels, int numChannels, int numSamples) noexcept;

    float getMomentaryLufs() const noexcept;
    float getShortTermLufs() const noexcept;
    float getIntegratedLufs() const noexcept;

private:
    void finishSubBlock() noexcept;
    float meanOfLastSubBlocks (int count) const noexcept;

    double sampleRate = 48000.0;
    int numChannels = 2;
    int subBlockLength = 4800;
    int subBlockFill = 0;

    std::array<Biquad, kMaxChannels> shelf {}, highPass {};
    std::array<double, kMaxChannels> channelWeight { 1.0, 1.0 };

    double currentSubEnergy = 0.0;                    // sum of weighted squares in the current 100 ms
    std::array<double, kShortTermSubs> subEnergies {};// mean squares of the last 30 sub-blocks
    int subWrite = 0;
    int subsAvailable = 0;

    std::array<uint32_t, kHistBins> histCount {};
    std::array<double, kHistBins>   histEnergy {};
};

} // namespace aimix
