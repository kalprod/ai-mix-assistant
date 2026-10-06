#pragma once

// Time-delay offset between two signals via FFT cross-correlation.
//
//   r[k] = sum_n ref[n] * target[n + k]
//
// computed as IFFT(conj(FFT(ref)) * FFT(target)) with 2x zero padding (linear,
// not circular, correlation). Optional PHAT weighting (|C| normalisation)
// sharpens the peak for tonal material where plain correlation is ambiguous.
// The peak is refined to sub-sample precision with parabolic interpolation and
// its sign reveals a polarity inversion.

#include "Fft.h"

#include <complex>
#include <vector>

namespace aimix
{
struct DelayEstimate
{
    bool  valid = false;
    float lagSamples = 0.0f;     // > 0: target arrives later than reference
    float correlation = 0.0f;    // normalised coefficient at the peak, -1 .. +1
    bool  polarityInverted = false;
};

class DelayEstimator
{
public:
    explicit DelayEstimator (int maxInputLength);

    // Not allocation-free on first call only if n > maxInputLength (asserts).
    DelayEstimate estimate (const float* reference, const float* target, int numSamples,
                            int maxLagSamples, bool usePhat = true) noexcept;

private:
    int maxLength;
    Fft fft;
    std::vector<std::complex<float>> specRef, specTarget, cross, weighted;
};

} // namespace aimix
