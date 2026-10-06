#pragma once

// Small radix-2 complex FFT with precomputed tables. Allocates only in the
// constructor, so forward()/inverse() are real-time safe. In the plugin build
// juce::dsp::FFT could be swapped in; this one keeps the core JUCE-free and
// unit-testable on its own.

#include <complex>
#include <vector>

namespace aimix
{
class Fft
{
public:
    explicit Fft (int order);

    int getSize() const noexcept { return size; }

    void forward (std::complex<float>* data) const noexcept;
    void inverse (std::complex<float>* data) const noexcept;   // scaled by 1/N

private:
    void transform (std::complex<float>* data, bool inverse) const noexcept;

    int size;
    std::vector<int> bitReversed;
    std::vector<std::complex<float>> twiddles;   // exp(-2*pi*i*k/N), k < N/2
};

// Hann-windowed magnitude spectrum of a real block of Fft::getSize() samples.
// Output: N/2 bins in dBFS where a full-scale sine centred on a bin reads 0 dB.
class MagnitudeSpectrum
{
public:
    explicit MagnitudeSpectrum (int order);

    int getSize() const noexcept { return fft.getSize(); }

    // Adds the linear power spectrum of `input` to `powerAccumulator` (N/2 bins).
    void accumulatePower (const float* input, float* powerAccumulator) noexcept;

    static void powerToDb (const float* power, float* outDb, int numBins, float scale) noexcept;

private:
    Fft fft;
    std::vector<float> window;
    std::vector<std::complex<float>> scratch;
    float powerScale = 1.0f;
};

int nextPowerOfTwoOrder (int n) noexcept;

} // namespace aimix
