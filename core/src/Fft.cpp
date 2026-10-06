#include "aimix/Fft.h"

#include <cassert>
#include <cmath>

namespace aimix
{
static constexpr double kPi = 3.14159265358979323846;

int nextPowerOfTwoOrder (int n) noexcept
{
    int order = 0;
    while ((1 << order) < n)
        ++order;
    return order;
}

Fft::Fft (int order) : size (1 << order), bitReversed ((size_t) size), twiddles ((size_t) size / 2)
{
    assert (order >= 1 && order <= 20);

    for (int i = 0; i < size; ++i)
    {
        int r = 0;
        for (int b = 0; b < order; ++b)
            if (i & (1 << b))
                r |= 1 << (order - 1 - b);
        bitReversed[(size_t) i] = r;
    }

    for (int k = 0; k < size / 2; ++k)
    {
        const double phase = -2.0 * kPi * k / size;
        twiddles[(size_t) k] = { (float) std::cos (phase), (float) std::sin (phase) };
    }
}

void Fft::forward (std::complex<float>* data) const noexcept { transform (data, false); }

void Fft::inverse (std::complex<float>* data) const noexcept
{
    transform (data, true);
    const float scale = 1.0f / (float) size;
    for (int i = 0; i < size; ++i)
        data[i] *= scale;
}

void Fft::transform (std::complex<float>* data, bool inv) const noexcept
{
    for (int i = 0; i < size; ++i)
    {
        const int j = bitReversed[(size_t) i];
        if (j > i)
            std::swap (data[i], data[j]);
    }

    // Plain float arithmetic rather than std::complex operator*, which carries
    // NaN/inf recovery branches in strict IEEE builds.
    for (int len = 2; len <= size; len <<= 1)
    {
        const int half = len >> 1;
        const int step = size / len;

        for (int start = 0; start < size; start += len)
        {
            for (int k = 0; k < half; ++k)
            {
                const auto& w = twiddles[(size_t) (k * step)];
                const float wr = w.real();
                const float wi = inv ? -w.imag() : w.imag();

                auto& a = data[start + k];
                auto& b = data[start + k + half];
                const float br = b.real() * wr - b.imag() * wi;
                const float bi = b.real() * wi + b.imag() * wr;
                const float ar = a.real(), ai = a.imag();
                a = { ar + br, ai + bi };
                b = { ar - br, ai - bi };
            }
        }
    }
}

//==============================================================================
MagnitudeSpectrum::MagnitudeSpectrum (int order)
    : fft (order), window ((size_t) (1 << order)), scratch ((size_t) (1 << order))
{
    const int n = fft.getSize();
    double windowSum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * kPi * i / n));   // periodic Hann
        windowSum += window[(size_t) i];
    }

    // |X[k]| of a sine with amplitude A centred on bin k is A * sum(w) / 2.
    const double amplitudeScale = 2.0 / windowSum;
    powerScale = (float) (amplitudeScale * amplitudeScale);
}

void MagnitudeSpectrum::accumulatePower (const float* input, float* powerAccumulator) noexcept
{
    const int n = fft.getSize();
    for (int i = 0; i < n; ++i)
        scratch[(size_t) i] = { input[i] * window[(size_t) i], 0.0f };

    fft.forward (scratch.data());

    for (int k = 0; k < n / 2; ++k)
    {
        const auto& c = scratch[(size_t) k];
        powerAccumulator[k] += (c.real() * c.real() + c.imag() * c.imag()) * powerScale;
    }
}

void MagnitudeSpectrum::powerToDb (const float* power, float* outDb, int numBins, float scale) noexcept
{
    for (int k = 0; k < numBins; ++k)
        outDb[k] = 10.0f * std::log10 (power[k] * scale + 1.0e-12f);   // floor at -120 dB
}

} // namespace aimix
