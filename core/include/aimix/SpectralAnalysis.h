#pragma once

// Critical-band (Bark) spectral profiles and inter-track frequency masking.

#include <array>

namespace aimix
{
constexpr int kNumBands = 24;

const std::array<float, kNumBands + 1>& barkBandEdgesHz() noexcept;
float bandCentreHz (int band) noexcept;   // geometric centre
float bandQ (int band) noexcept;          // centre / bandwidth, for an EQ bell covering the band

struct BandProfile
{
    std::array<float, kNumBands> powerDb {};   // energy per critical band
    std::array<float, kNumBands> share {};     // linear energy normalised to sum 1 (cached for pairwise masking)
    float totalPowerDb = -120.0f;
    float peakBandDb   = -120.0f;
};

// magnitudeDb: N/2 bins from MagnitudeSpectrum (bin k = k * fs / fftSize).
BandProfile computeBandProfile (const float* magnitudeDb, int numBins, int fftSize, double sampleRate) noexcept;
// Same, from linear power per bin (avoids a dB round trip in the engine).
BandProfile computeBandProfileFromPower (const float* power, int numBins, int fftSize, double sampleRate) noexcept;

// Fraction of the profile's energy below `hz` (0..1).
float energyShareBelow (const BandProfile& p, float hz) noexcept;

struct MaskingResult
{
    float score = 0.0f;                         // 0 = disjoint spectra .. 1 = identical spectral shape
    std::array<float, kNumBands> bandOverlap {};// per-band contribution, sums to `score`
    int   worstBand = -1;                       // band contributing most overlap
    float levelDifferenceDb = 0.0f;             // A minus B in the worst band
};

// Frequency masking between two tracks.
//
// Each profile is normalised to a distribution over bands; a band only counts
// when it is "active" for both tracks (within activeRangeDb of that track's
// loudest band). The overlap is the histogram intersection
//     score = sum_k min(pA_k, pB_k)   over bands active in both,
// i.e. the share of spectral energy the two tracks put in the same critical
// bands, which is where they mask each other.
MaskingResult computeMaskingOverlap (const BandProfile& a, const BandProfile& b, float activeRangeDb = 30.0f) noexcept;

} // namespace aimix
