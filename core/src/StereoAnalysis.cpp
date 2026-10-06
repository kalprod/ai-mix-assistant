#include "aimix/StereoAnalysis.h"

#include <algorithm>
#include <cmath>

namespace aimix
{
void StereoAccumulator::add (const float* l, const float* r, int n) noexcept
{
    double l2 = 0, r2 = 0, lr = 0;
    float pl = peakL, pr = peakR;

    for (int i = 0; i < n; ++i)
    {
        const double a = l[i], b = r[i];
        l2 += a * a;
        r2 += b * b;
        lr += a * b;
        pl = std::max (pl, std::abs (l[i]));
        pr = std::max (pr, std::abs (r[i]));
    }

    // M^2 + S^2 identities: M = (L+R)/2, S = (L-R)/2
    //   sum M^2 = (l2 + r2 + 2 lr) / 4,  sum S^2 = (l2 + r2 - 2 lr) / 4
    sumL2 += l2;
    sumR2 += r2;
    sumLR += lr;
    sumM2 += (l2 + r2 + 2.0 * lr) * 0.25;
    sumS2 += (l2 + r2 - 2.0 * lr) * 0.25;
    peakL = pl;
    peakR = pr;
    count += n;
}

float StereoAccumulator::correlation() const noexcept
{
    const double denom = std::sqrt (sumL2 * sumR2);
    if (denom < 1.0e-12)
        return 0.0f;
    return (float) std::clamp (sumLR / denom, -1.0, 1.0);
}

MidSideResult StereoAccumulator::midSide() const noexcept
{
    if (count == 0)
        return {};
    return midSideFromEnergies (sumM2 / (double) count, std::max (0.0, sumS2) / (double) count);
}

MidSideResult midSideFromEnergies (double m, double s) noexcept
{
    MidSideResult r;
    r.midEnergy = m;
    r.sideEnergy = s;
    const double eps = 1.0e-20;
    r.sideToMidDb = (float) std::clamp (10.0 * std::log10 ((s + eps) / (m + eps)), -100.0, 100.0);
    r.width = (m + s) > eps ? (float) (s / (m + s)) : 0.0f;
    return r;
}

float computePhaseCorrelation (const float* left, const float* right, int n) noexcept
{
    StereoAccumulator acc;
    acc.add (left, right, n);
    return acc.correlation();
}

MidSideResult computeMidSide (const float* left, const float* right, int n) noexcept
{
    StereoAccumulator acc;
    acc.add (left, right, n);
    return acc.midSide();
}

} // namespace aimix
