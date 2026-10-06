#pragma once

// Guesses what a track is (kick, snare, drums, bass, vocal or another
// instrument) from the analysis frames it publishes, so users don't have to
// pick a role by hand. Runs on the Master Engine thread, never the audio
// thread.
//
// It looks at a few seconds of signal and combines:
//   * where the energy sits (sub, low, low-mid, mid, presence, air),
//   * how percussive it is (sudden level jumps followed by slower decays),
//   * stereo width (lead vocals are almost always mono).
//
// These are heuristics, not a trained model: they are right for typical
// close-miked or programmed sources and the user can always override the role.

#include "AnalysisPayload.h"

namespace aimix
{
struct RoleFeatures
{
    float sub = 0, low = 0, lowMid = 0, mid = 0, presence = 0, air = 0;   // energy shares, sum ~1
    float fluxDb = 0;        // mean |level change| between consecutive hops
    float onsetRate = 0;     // share of hops that jump up by 10 dB or more (hits)
    float riseDb = 0;        // mean level increase over hops that get louder
    float fallDb = 0;        // mean level decrease over hops that get quieter
    float crestDb = 0;       // mean peak-to-RMS within a hop
    float sideToMidDb = -100;
    int frames = 0;          // frames with signal seen so far
};

class RoleClassifier
{
public:
    static constexpr int kFramesToDecide = 70;      // ~3 s of signal at 48 kHz
    static constexpr int kFramesToSwitch = 70;      // a new guess must hold this long to replace the old one

    void reset() noexcept { *this = RoleClassifier(); }

    // Feed every frame the track publishes, silent ones included.
    void addFrame (const AnalysisPayload& p) noexcept;

    // Unknown until enough signal has been heard; afterwards one of Kick,
    // Snare, Drums, Bass, Vocal, or Unknown meaning "another instrument".
    TrackRole role() const noexcept     { return decided; }
    bool hasDecided() const noexcept    { return frames >= kFramesToDecide; }
    const RoleFeatures& features() const noexcept { return f; }

    static TrackRole classify (const RoleFeatures& f) noexcept;

private:
    RoleFeatures f;
    int frames = 0, dynamicsFrames = 0, riseFrames = 0, fallFrames = 0;
    float prevLevelDb = -120.0f;
    float maxLevelDb = -120.0f;   // slowly decaying peak of the hop level
    bool havePrev = false;
    TrackRole decided = TrackRole::Unknown;
    TrackRole candidate = TrackRole::Unknown;
    int candidateFrames = 0;
};

} // namespace aimix
