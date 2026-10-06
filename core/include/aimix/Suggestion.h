#pragma once

// Structured output of the logic engine, consumed by the UI (and serialisable
// to JSON for logging, automation or a future ML re-ranker).

#include <cstdint>
#include <string>
#include <vector>

namespace aimix
{
enum class Severity : uint8_t { Info = 0, Warning = 1, Critical = 2 };
enum class Category : uint8_t { Gain, Eq, Panning, Phase, Dynamics, Depth, General };

// The order to work through a mix in. Cards are grouped and sorted by step so
// the user can go top to bottom:
//   1 gain staging, 2 EQ, 3 dynamics, 4 stereo placement, 5 depth, 6 final tip.
enum class MixStep : uint8_t { GainStaging = 1, Eq = 2, Dynamics = 3, Stereo = 4, Depth = 5, FinalTip = 6 };
constexpr int kNumMixSteps = 6;

enum class ActionType : uint8_t
{
    None, AdjustGain, EqBell, HighPass, Pan, MonoBelow, TimeAlign, InvertPolarity
};

struct SuggestedAction
{
    ActionType type = ActionType::None;
    uint32_t trackId = 0;          // track the action applies to
    float gainDb = 0.0f;           // AdjustGain, EqBell
    float frequencyHz = 0.0f;      // EqBell, HighPass, MonoBelow
    float q = 0.0f;                // EqBell
    float pan = 0.0f;              // Pan: -1 (L) .. +1 (R)
    float delaySamples = 0.0f;     // TimeAlign
    float delayMs = 0.0f;          // TimeAlign
};

struct Suggestion
{
    std::string key;               // stable identity, e.g. "eq.masking:3:7" (used for debouncing / dismissal)
    std::string ruleId;            // e.g. "eq.masking"
    Category category = Category::Gain;
    MixStep step = MixStep::GainStaging;
    Severity severity = Severity::Info;
    float confidence = 0.0f;       // 0 .. 1
    std::vector<uint32_t> trackIds;
    std::vector<std::string> trackNames;
    std::string title;             // one line, shown on the card header
    std::string detail;            // why: the measurement that triggered it
    std::vector<std::string> steps;// step-by-step guidance
    SuggestedAction action;        // machine-readable primary action
    int ageTicks = 0;              // how long it has been shown
    bool resolving = false;        // condition no longer measured; kept briefly to confirm the fix
    bool tip = false;              // general advice, not a measured problem
};

const char* toString (Severity) noexcept;
const char* toString (Category) noexcept;
const char* toString (MixStep) noexcept;          // "gain_staging"
const char* stepTitle (MixStep) noexcept;         // "Gain staging"
const char* stepSummary (MixStep) noexcept;       // one line: what this step is about
MixStep stepForRule (const std::string& ruleId) noexcept;
const char* toString (ActionType) noexcept;

std::string describeAction (const SuggestedAction& action);   // "EQ bell -3.0 dB @ 2.5 kHz, Q 2.1"
std::string formatFrequency (float hz);

std::string toJson (const Suggestion& s);
std::string toJson (const std::vector<Suggestion>& list);

} // namespace aimix
