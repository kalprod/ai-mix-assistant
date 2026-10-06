#include "aimix/Loudness.h"

#include <algorithm>

namespace aimix
{
static constexpr double kPi = 3.14159265358979323846;

void designKWeighting (double fs, Biquad& shelf, Biquad& hp) noexcept
{
    // Pre-filter (high shelf, ~+4 dB above 1.5 kHz). Analog prototype from
    // BS.1770 bilinear-transformed for arbitrary fs (same derivation as libebur128).
    {
        const double f0 = 1681.974450955533;
        const double G  = 3.999843853973347;
        const double Q  = 0.7071752369554196;
        const double K  = std::tan (kPi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0);
        const double Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;

        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }

    // RLB weighting (2nd-order high-pass at ~38 Hz).
    {
        const double f0 = 38.13547087602444;
        const double Q  = 0.5003270373238773;
        const double K  = std::tan (kPi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;

        hp.b0 = 1.0;
        hp.b1 = -2.0;
        hp.b2 = 1.0;
        hp.a1 = 2.0 * (K * K - 1.0) / a0;
        hp.a2 = (1.0 - K / Q + K * K) / a0;
    }
}

void LoudnessMeter::prepare (double fs, int channels) noexcept
{
    sampleRate = fs;
    numChannels = std::clamp (channels, 1, kMaxChannels);
    subBlockLength = std::max (1, (int) std::lround (fs * 0.1));

    for (int c = 0; c < kMaxChannels; ++c)
        designKWeighting (fs, shelf[(size_t) c], highPass[(size_t) c]);

    reset();
}

void LoudnessMeter::reset() noexcept
{
    for (auto& f : shelf)    f.reset();
    for (auto& f : highPass) f.reset();
    currentSubEnergy = 0.0;
    subBlockFill = 0;
    subEnergies.fill (0.0);
    subWrite = 0;
    subsAvailable = 0;
    histCount.fill (0);
    histEnergy.fill (0.0);
}

void LoudnessMeter::process (const float* const* channels, int channelsIn, int numSamples) noexcept
{
    const int nCh = std::min (channelsIn, numChannels);
    int pos = 0;

    while (pos < numSamples)
    {
        const int chunk = std::min (numSamples - pos, subBlockLength - subBlockFill);

        for (int c = 0; c < nCh; ++c)
        {
            auto& s = shelf[(size_t) c];
            auto& h = highPass[(size_t) c];
            const float* x = channels[c] + pos;
            double sum = 0.0;

            for (int i = 0; i < chunk; ++i)
            {
                const double y = h.process (s.process ((double) x[i]));
                sum += y * y;
            }
            currentSubEnergy += sum * channelWeight[(size_t) c];
        }

        subBlockFill += chunk;
        pos += chunk;

        if (subBlockFill == subBlockLength)
            finishSubBlock();
    }
}

void LoudnessMeter::finishSubBlock() noexcept
{
    subEnergies[(size_t) subWrite] = currentSubEnergy / subBlockLength;
    subWrite = (subWrite + 1) % kShortTermSubs;
    subsAvailable = std::min (subsAvailable + 1, kShortTermSubs);
    currentSubEnergy = 0.0;
    subBlockFill = 0;

    // Every 100 ms a new 400 ms gating block completes (75 % overlap).
    if (subsAvailable >= 4)
    {
        double blockEnergy = 0.0;
        for (int i = 1; i <= 4; ++i)
            blockEnergy += subEnergies[(size_t) ((subWrite - i + kShortTermSubs) % kShortTermSubs)];
        blockEnergy *= 0.25;

        const float l = energyToLufs (blockEnergy);
        if (l > kHistMinLufs)   // absolute gate
        {
            const int bin = std::clamp ((int) ((l - kHistMinLufs) * 10.0f), 0, kHistBins - 1);
            histCount[(size_t) bin] += 1;
            histEnergy[(size_t) bin] += blockEnergy;
        }
    }
}

float LoudnessMeter::meanOfLastSubBlocks (int count) const noexcept
{
    if (subsAvailable == 0)
        return kLufsFloor;

    const int n = std::min (count, subsAvailable);
    double sum = 0.0;
    for (int i = 1; i <= n; ++i)
        sum += subEnergies[(size_t) ((subWrite - i + kShortTermSubs) % kShortTermSubs)];

    // Until a full window has elapsed the missing time counts as silence,
    // which is what a meter that started at t = 0 shows.
    return energyToLufs (sum / count);
}

float LoudnessMeter::getMomentaryLufs() const noexcept  { return meanOfLastSubBlocks (4); }
float LoudnessMeter::getShortTermLufs() const noexcept  { return meanOfLastSubBlocks (kShortTermSubs); }

float LoudnessMeter::getIntegratedLufs() const noexcept
{
    uint64_t count = 0;
    double energy = 0.0;
    for (int b = 0; b < kHistBins; ++b)
    {
        count  += histCount[(size_t) b];
        energy += histEnergy[(size_t) b];
    }
    if (count == 0)
        return kLufsFloor;

    const float relativeGate = energyToLufs (energy / (double) count) - 10.0f;

    count = 0;
    energy = 0.0;
    for (int b = 0; b < kHistBins; ++b)
    {
        const float binCentre = kHistMinLufs + ((float) b + 0.5f) * 0.1f;
        if (binCentre >= relativeGate)
        {
            count  += histCount[(size_t) b];
            energy += histEnergy[(size_t) b];
        }
    }
    return count == 0 ? kLufsFloor : energyToLufs (energy / (double) count);
}

} // namespace aimix
