#pragma once

// Phase correlation and Mid/Side energy, as one-shot functions and as a
// running accumulator the Listener updates on the audio thread.

namespace aimix
{
struct MidSideResult
{
    double midEnergy  = 0.0;     // mean square of M = (L + R) / 2
    double sideEnergy = 0.0;     // mean square of S = (L - R) / 2
    float  sideToMidDb = -100.0f;// 10 log10(S / M), clamped to +-100 dB
    float  width = 0.0f;         // S / (M + S): 0 = mono, 0.5 = decorrelated, 1 = out of phase
};

float computePhaseCorrelation (const float* left, const float* right, int numSamples) noexcept;
MidSideResult computeMidSide (const float* left, const float* right, int numSamples) noexcept;
MidSideResult midSideFromEnergies (double midEnergy, double sideEnergy) noexcept;

struct StereoAccumulator
{
    double sumL2 = 0, sumR2 = 0, sumLR = 0;
    double sumM2 = 0, sumS2 = 0;
    float  peakL = 0, peakR = 0;
    long long count = 0;

    void reset() noexcept { *this = {}; }
    void add (const float* left, const float* right, int numSamples) noexcept;

    float correlation() const noexcept;   // 0 when either side is silent
    MidSideResult midSide() const noexcept;
};

} // namespace aimix
