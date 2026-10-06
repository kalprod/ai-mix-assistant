#pragma once

// Heuristic rule system: analysis data in, actionable mixing suggestions out.
//
// Each rule is a pure function of a MixSnapshot (smoothed per-track and
// per-pair measurements). RuleEngine::evaluate() makes cards stable enough to
// act on:
//   * a condition must be seen for `debounceTicks` before its card appears
//     (occasional misses only slow it down, they don't restart the count);
//   * once shown, a card stays until the condition has been gone for
//     `clearTicks` of actual playback. After `resolvingTicks` it is marked
//     "resolving" so the UI can show that the fix is being confirmed;
//   * while the tracks a card is about are silent (transport stopped, a gap
//     in the arrangement) the card is frozen: silence is not a fix;
//   * rules use a looser threshold to clear a card than to raise it, so a
//     measurement hovering at a threshold doesn't toggle the card.

#include "AnalysisPayload.h"
#include "SpectralAnalysis.h"
#include "Suggestion.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace aimix
{
struct TrackView
{
    uint32_t trackId = 0;
    std::string name;
    TrackRole role = TrackRole::Unknown;
    int numChannels = 2;
    double sampleRate = 48000.0;

    float rmsDb[2]  { -120.0f, -120.0f };   // smoothed
    float peakDb[2] { -120.0f, -120.0f };   // peak-hold over the last few seconds
    float momentaryLufs = -144.0f;
    float shortTermLufs = -144.0f;
    float integratedLufs = -144.0f;
    float correlation = 1.0f;               // smoothed
    float sideToMidDb = -100.0f;            // smoothed
    BandProfile bands;                      // long-term averaged spectrum
    bool active = false;                    // carrying signal recently

    float maxPeakDb() const noexcept { return peakDb[0] > peakDb[1] ? peakDb[0] : peakDb[1]; }
    float maxRmsDb() const noexcept  { return rmsDb[0] > rmsDb[1] ? rmsDb[0] : rmsDb[1]; }
};

struct PairView
{
    uint32_t trackA = 0, trackB = 0;
    MaskingResult masking;
    bool  delayValid = false;     // estimate is stable across several frames
    float delaySamples = 0.0f;    // > 0: B arrives later than A
    float delayCorrelation = 0.0f;
};

struct MixSnapshot
{
    std::vector<TrackView> tracks;
    bool hasMaster = false;
    TrackView master;
    std::vector<PairView> pairs;
};

struct RuleConfig
{
    // gain
    float clipPeakDb        = -0.3f;
    float hotRmsDb          = -8.0f;
    float targetTrackRmsDb  = -18.0f;   // ~0 VU gain-staging target
    float quietShortTermLufs = -45.0f;
    float masterCeilingDb   = -1.0f;
    float mixTargetLufs     = -14.0f;
    float mixTargetToleranceLu = 3.0f;
    // eq
    float maskingWarn       = 0.55f;
    float maskingCritical   = 0.80f;
    float lowEndShareKnown  = 0.20f;   // share of energy < 120 Hz for non-bass roles
    float lowEndShareUnknown = 0.40f;
    float harshExcessDb     = 6.0f;
    float mudExcessDb       = 5.0f;
    // panning
    float maskingPanThreshold = 0.50f;
    float monoSideToMidDb   = -20.0f;
    float wideLowEndSideToMidDb = -12.0f;
    // phase
    float correlationWarn   = -0.10f;
    float correlationCritical = -0.40f;
    float minDelayCorrelation = 0.50f;
    float minDelaySamples   = 1.0f;
    // hysteresis (ticks are ~66 ms: the Master Engine runs at ~15 Hz)
    int debounceTicks = 4;     // ~0.25 s of a condition before its card appears
    int holdTicks = 10;        // a not-yet-shown condition is forgotten after this many misses
    int resolvingTicks = 15;   // ~1 s without the condition: card shows "looks fixed"
    int clearTicks = 45;       // ~3 s of playback without the condition: card removed
};

class RuleEngine
{
public:
    explicit RuleEngine (RuleConfig config = {});

    // Debounced evaluation; call once per engine tick.
    std::vector<Suggestion> evaluate (const MixSnapshot& snapshot);

    // Stateless evaluation of every rule (used by evaluate() and tests).
    // `showing` holds the keys of cards already on screen: their rules clear
    // at a looser threshold than they trigger at.
    static std::vector<Suggestion> evaluateRaw (const MixSnapshot& snapshot, const RuleConfig& config,
                                                const std::unordered_set<std::string>& showing = {});

    void dismiss (const std::string& key) { dismissed.insert (key); }
    void clearDismissed()                 { dismissed.clear(); }
    const RuleConfig& getConfig() const noexcept { return config; }

private:
    struct Tracker
    {
        Suggestion latest;
        int hits = 0;
        int misses = 0;
        int age = 0;
        bool visible = false;
    };

    RuleConfig config;
    std::unordered_map<std::string, Tracker> trackers;
    std::unordered_set<std::string> dismissed;
};

// Priority used to decide which of two masking tracks gets the EQ cut.
int mixPriority (TrackRole role) noexcept;

} // namespace aimix
