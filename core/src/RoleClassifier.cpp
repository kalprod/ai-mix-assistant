#include "aimix/RoleClassifier.h"

#include <algorithm>
#include <cmath>

namespace aimix
{
namespace
{
constexpr float kSignalFloorDb = -70.0f;   // absolute floor: below this nothing is analysed
constexpr float kTailRangeDb   = 30.0f;    // hops this far under the recent peak are tails or bleed
constexpr float kDynamicRangeDb = 40.0f;   // level changes are measured within this range of the peak
constexpr int   kAverageFrames = 200;      // running mean, then ~8 s exponential average

}

void RoleClassifier::addFrame (const AnalysisPayload& p) noexcept
{
    const bool silentFlag = (p.flags & kFlagSilent) != 0;
    const float rms = std::max (p.rmsDb[0], p.numChannels > 1 ? p.rmsDb[1] : -120.0f);
    const float peak = std::max (p.peakDb[0], p.numChannels > 1 ? p.peakDb[1] : -120.0f);
    // Levels are measured relative to the track's own recent peak, so a quiet
    // track reads the same as a loud one.
    const float raw = silentFlag ? -120.0f : rms;
    maxLevelDb = std::max (raw, maxLevelDb - 0.02f);
    const float level = std::max (raw, maxLevelDb - kDynamicRangeDb);
    const bool hasSignal = ! silentFlag && rms > kSignalFloorDb && rms > maxLevelDb - kTailRangeDb;

    const float change = havePrev ? level - prevLevelDb : 0.0f;
    const bool countDynamics = havePrev && maxLevelDb > kSignalFloorDb
                               && (hasSignal || prevLevelDb > maxLevelDb - kTailRangeDb);
    prevLevelDb = level;
    havePrev = true;

    if (countDynamics)
    {
        ++dynamicsFrames;
        const float a = 1.0f / (float) std::min (dynamicsFrames, kAverageFrames);
        f.fluxDb += a * (std::abs (change) - f.fluxDb);
        f.onsetRate += a * ((change >= 10.0f ? 1.0f : 0.0f) - f.onsetRate);
        if (change > 0.5f)
        {
            ++riseFrames;
            f.riseDb += (change - f.riseDb) / (float) std::min (riseFrames, kAverageFrames);
        }
        else if (change < -0.5f)
        {
            ++fallFrames;
            f.fallDb += (-change - f.fallDb) / (float) std::min (fallFrames, kAverageFrames);
        }
    }

    if (! hasSignal)
        return;

    // Energy shares by region.
    const double binHz = p.sampleRate / kFftSize;
    double e[6] {};
    for (int k = 1; k < kNumFftBins; ++k)
    {
        const double hz = k * binHz;
        const double pw = std::pow (10.0, p.fftMagnitudeDb[k] / 10.0);
        const int r = hz < 90.0 ? 0 : hz < 250.0 ? 1 : hz < 1000.0 ? 2 : hz < 3500.0 ? 3 : hz < 8000.0 ? 4 : 5;
        e[r] += pw;
    }
    const double total = e[0] + e[1] + e[2] + e[3] + e[4] + e[5];
    if (total <= 1.0e-12)
        return;

    const double sideToMid = p.midEnergy > 1.0e-12 ? 10.0 * std::log10 (std::max (1.0e-12, (double) p.sideEnergy) / p.midEnergy) : -100.0;

    ++frames;
    const float a = 1.0f / (float) std::min (frames, kAverageFrames);
    auto avg = [a] (float& acc, double v) { acc += a * ((float) v - acc); };
    avg (f.sub, e[0] / total);
    avg (f.low, e[1] / total);
    avg (f.lowMid, e[2] / total);
    avg (f.mid, e[3] / total);
    avg (f.presence, e[4] / total);
    avg (f.air, e[5] / total);
    avg (f.crestDb, peak - rms);
    avg (f.sideToMidDb, std::clamp (sideToMid, -60.0, 20.0));
    f.frames = frames;

    if (frames < kFramesToDecide)
        return;

    const auto guess = classify (f);
    if (frames == kFramesToDecide)
    {
        decided = candidate = guess;
        candidateFrames = 0;
        return;
    }
    if (guess == decided)
    {
        candidate = decided;
        candidateFrames = 0;
    }
    else if (guess == candidate)
    {
        if (++candidateFrames >= kFramesToSwitch)
        {
            decided = guess;
            candidateFrames = 0;
        }
    }
    else
    {
        candidate = guess;
        candidateFrames = 1;
    }
}

TrackRole RoleClassifier::classify (const RoleFeatures& f) noexcept
{
    const float lows = f.sub + f.low;
    const float highs = f.presence + f.air;

    // Hits: the level jumps up suddenly and decays more slowly than it rose.
    const bool percussive = f.onsetRate > 0.03f && f.riseDb > 2.0f * f.fallDb;

    if (percussive)
    {
        if (lows > 0.55f && highs < 0.15f)
            // A kick attacks hard and dies away between hits; a plucked bass or
            // long 808 attacks softer or rings on, and mixes like a bass.
            return f.riseDb > 12.0f && f.fallDb >= 2.0f ? TrackRole::Kick : TrackRole::Bass;
        if (highs > 0.85f && lows + f.lowMid < 0.05f)
            return TrackRole::Drums;   // hats, cymbals, shakers
        if (lows > 0.40f)
            return TrackRole::Drums;   // kit, loop or overheads: kick and cymbals together
        if (highs > 0.15f)
            return TrackRole::Snare;   // body plus wires (also claps and toms with attack)
    }

    if (lows > 0.55f && f.sub > 0.2f)
        return TrackRole::Bass;   // a pad or chords in the low register have little below 90 Hz

    // A lead vocal: mono, energy in the low-mids/mids, and a level that swells
    // and falls with syllables (rather than plucked decays or a flat sustain).
    const bool mono = f.sideToMidDb < -20.0f;
    const bool syllabic = f.fluxDb > 1.5f && f.fluxDb < 9.0f && f.fallDb > 0.5f * f.riseDb;
    if (mono && syllabic && f.lowMid + f.mid > 0.55f && f.sub < 0.15f)
        return TrackRole::Vocal;

    return TrackRole::Unknown;   // another instrument
}

} // namespace aimix
