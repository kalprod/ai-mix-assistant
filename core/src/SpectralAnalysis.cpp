#include "aimix/SpectralAnalysis.h"

#include <algorithm>
#include <cmath>

namespace aimix
{
const std::array<float, kNumBands + 1>& barkBandEdgesHz() noexcept
{
    // Zwicker critical-band edges (first edge lowered to 20 Hz).
    static const std::array<float, kNumBands + 1> edges {
        20, 100, 200, 300, 400, 510, 630, 770, 920, 1080, 1270, 1480, 1720,
        2000, 2320, 2700, 3150, 3700, 4400, 5300, 6400, 7700, 9500, 12000, 15500
    };
    return edges;
}

float bandCentreHz (int band) noexcept
{
    const auto& e = barkBandEdgesHz();
    band = std::clamp (band, 0, kNumBands - 1);
    return std::sqrt (e[(size_t) band] * e[(size_t) band + 1]);
}

float bandQ (int band) noexcept
{
    const auto& e = barkBandEdgesHz();
    band = std::clamp (band, 0, kNumBands - 1);
    return bandCentreHz (band) / (e[(size_t) band + 1] - e[(size_t) band]);
}

static float powerToDb (double p) noexcept { return (float) (10.0 * std::log10 (p + 1.0e-12)); }

template <bool InputIsDb>
static BandProfile bandProfileImpl (const float* bins, int numBins, int fftSize, double sampleRate) noexcept
{
    const auto& edges = barkBandEdgesHz();
    std::array<double, kNumBands> power {};
    const double binHz = sampleRate / fftSize;

    int band = 0;
    for (int k = 1; k < numBins; ++k)   // skip DC
    {
        const double f = k * binHz;
        if (f < edges[0])
            continue;
        while (band < kNumBands && f >= edges[(size_t) band + 1])
            ++band;
        if (band >= kNumBands)
            break;
        power[(size_t) band] += InputIsDb ? std::pow (10.0, bins[k] / 10.0) : (double) bins[k];
    }

    BandProfile p;
    double total = 0.0;
    for (int b = 0; b < kNumBands; ++b)
    {
        p.powerDb[(size_t) b] = powerToDb (power[(size_t) b]);
        total += power[(size_t) b];
        p.peakBandDb = std::max (p.peakBandDb, p.powerDb[(size_t) b]);
    }
    p.totalPowerDb = powerToDb (total);
    for (int b = 0; b < kNumBands; ++b)
        p.share[(size_t) b] = total > 0.0 ? (float) (power[(size_t) b] / total) : 0.0f;
    return p;
}

BandProfile computeBandProfile (const float* magnitudeDb, int numBins, int fftSize, double sampleRate) noexcept
{
    return bandProfileImpl<true> (magnitudeDb, numBins, fftSize, sampleRate);
}

BandProfile computeBandProfileFromPower (const float* power, int numBins, int fftSize, double sampleRate) noexcept
{
    return bandProfileImpl<false> (power, numBins, fftSize, sampleRate);
}

float energyShareBelow (const BandProfile& p, float hz) noexcept
{
    const auto& edges = barkBandEdgesHz();
    double below = 0.0, total = 0.0;
    for (int b = 0; b < kNumBands; ++b)
    {
        const double e = std::pow (10.0, p.powerDb[(size_t) b] / 10.0);
        total += e;
        if (edges[(size_t) b + 1] <= hz)
            below += e;
        else if (edges[(size_t) b] < hz)   // partial band: linear share by bandwidth
            below += e * (hz - edges[(size_t) b]) / (edges[(size_t) b + 1] - edges[(size_t) b]);
    }
    return total > 0.0 ? (float) (below / total) : 0.0f;
}

MaskingResult computeMaskingOverlap (const BandProfile& a, const BandProfile& b, float activeRangeDb) noexcept
{
    // O(bands) per pair using the cached normalised shares: with 64 tracks the
    // engine evaluates ~2000 pairs per tick, so no transcendental calls here.
    MaskingResult r;
    if (a.totalPowerDb <= -100.0f || b.totalPowerDb <= -100.0f)
        return r;

    float best = 0.0f;
    for (int k = 0; k < kNumBands; ++k)
    {
        const bool activeA = a.powerDb[(size_t) k] >= a.peakBandDb - activeRangeDb;
        const bool activeB = b.powerDb[(size_t) k] >= b.peakBandDb - activeRangeDb;
        if (! (activeA && activeB))
            continue;

        const float overlap = std::min (a.share[(size_t) k], b.share[(size_t) k]);
        r.bandOverlap[(size_t) k] = overlap;
        r.score += overlap;

        if (overlap > best)
        {
            best = overlap;
            r.worstBand = k;
            r.levelDifferenceDb = a.powerDb[(size_t) k] - b.powerDb[(size_t) k];
        }
    }
    r.score = std::min (r.score, 1.0f);
    return r;
}

} // namespace aimix
