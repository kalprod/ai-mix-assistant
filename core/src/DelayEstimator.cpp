#include "aimix/DelayEstimator.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace aimix
{
DelayEstimator::DelayEstimator (int maxInputLength)
    : maxLength (maxInputLength),
      fft (nextPowerOfTwoOrder (2 * maxInputLength)),
      specRef ((size_t) fft.getSize()), specTarget ((size_t) fft.getSize()),
      cross ((size_t) fft.getSize()), weighted ((size_t) fft.getSize())
{
}

DelayEstimate DelayEstimator::estimate (const float* reference, const float* target, int n,
                                        int maxLag, bool usePhat) noexcept
{
    DelayEstimate result;
    assert (n <= maxLength);
    n = std::min (n, maxLength);
    maxLag = std::clamp (maxLag, 1, n - 2);

    const int N = fft.getSize();
    double energyRef = 0.0, energyTarget = 0.0;

    for (int i = 0; i < N; ++i)
    {
        const float r = i < n ? reference[i] : 0.0f;
        const float t = i < n ? target[i] : 0.0f;
        specRef[(size_t) i] = { r, 0.0f };
        specTarget[(size_t) i] = { t, 0.0f };
        energyRef += (double) r * r;
        energyTarget += (double) t * t;
    }

    if (energyRef < 1.0e-9 || energyTarget < 1.0e-9)
        return result;

    fft.forward (specRef.data());
    fft.forward (specTarget.data());

    for (int k = 0; k < N; ++k)
    {
        const auto& a = specRef[(size_t) k];
        const auto& b = specTarget[(size_t) k];
        // conj(a) * b
        const float re = a.real() * b.real() + a.imag() * b.imag();
        const float im = a.real() * b.imag() - a.imag() * b.real();
        cross[(size_t) k] = { re, im };

        const float mag = std::sqrt (re * re + im * im) + 1.0e-12f;
        weighted[(size_t) k] = usePhat ? std::complex<float> { re / mag, im / mag } : std::complex<float> { re, im };
    }

    fft.inverse (cross.data());
    if (usePhat)
        fft.inverse (weighted.data());

    const auto& search = usePhat ? weighted : cross;
    auto valueAt = [&] (const std::vector<std::complex<float>>& v, int lag) { return v[(size_t) (lag >= 0 ? lag : N + lag)].real(); };

    int bestLag = 0;
    float bestAbs = -1.0f;
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        const float a = std::abs (valueAt (search, lag));
        if (a > bestAbs)
        {
            bestAbs = a;
            bestLag = lag;
        }
    }

    // Parabolic sub-sample refinement on |r|.
    float frac = 0.0f;
    if (bestLag > -maxLag && bestLag < maxLag)
    {
        const float ym = std::abs (valueAt (search, bestLag - 1));
        const float y0 = bestAbs;
        const float yp = std::abs (valueAt (search, bestLag + 1));
        const float denom = ym - 2.0f * y0 + yp;
        if (std::abs (denom) > 1.0e-12f)
            frac = std::clamp (0.5f * (ym - yp) / denom, -0.5f, 0.5f);
    }

    const float coefficient = (float) (valueAt (cross, bestLag) / std::sqrt (energyRef * energyTarget));

    result.valid = true;
    result.lagSamples = (float) bestLag + frac;
    result.correlation = std::clamp (coefficient, -1.0f, 1.0f);
    result.polarityInverted = coefficient < 0.0f;
    return result;
}

} // namespace aimix
