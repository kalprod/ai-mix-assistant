#pragma once

// Builds a suggested insert chain for a track (or the mix bus) out of the
// plugins the user actually owns, analog models first.
//
// Each role has a template of slots (clean-up EQ -> character EQ ->
// compressor -> colour -> space; or bus comp -> program EQ -> tape -> limiter
// on the master). Every slot is filled with the best-scoring plugin from the
// library; the scoring is described in recommendChain() below. Knob starting
// points come from the track's measurements.

#include "aimix/AnalysisPayload.h"
#include "aimix/MixStyle.h"
#include "aimix/PluginLibrary.h"

#include <string>
#include <vector>

namespace aimix
{
struct TrackView;

struct ChainContext
{
    TrackRole role = TrackRole::Unknown;
    bool master = false;
    bool measured = false;          // the numbers below come from real audio
    float peakDb = -120.0f;
    float crestDb = 0.0f;           // peak minus RMS
    float levelSwingLu = 0.0f;      // spread of momentary loudness; 0 = unknown
    float sideToMidDb = -100.0f;
    std::string preferredFormat;    // "AU" or "VST3": breaks ties between copies of one plugin
    MixStyle style;                 // genre and era the mix is aimed at
};

ChainContext chainContextFor (const TrackView& t, bool master);

struct KnobTip
{
    std::string knob;      // "Drive / Input", "Gain reduction", "Tone / Colour", "Mix / Blend"
    std::string setting;   // "3-5 dB on the loudest words"
};

enum class PresetMatch : uint8_t
{
    Exact,      // a saved preset made for this role and style
    Closest,    // the nearest saved preset; tweak the knobs to finish
    NotFound,   // no suitable saved preset: start from the default
};

struct PresetAdvice
{
    std::string name;               // the preset to load ("" for NotFound)
    PresetMatch match = PresetMatch::NotFound;
    std::string text;               // one or two lines for the user
};

// One plugin choice for a slot. Option 1 is the best pick; options 2 and 3
// are different-sounding alternatives from the user's own plugins.
struct ChainOption
{
    int pick = -1;                  // index into the library
    std::string name;               // display name
    std::string character;          // "analog", "analog_inspired", "digital"
    std::string family;
    PresetAdvice preset;
    std::string note;               // options 2 and 3: how it differs from option 1
    int score = 0;
};

struct ChainSlot
{
    std::string id;        // "cleanup_eq", "character_eq", "compressor", ...
    std::string label;     // "Clean-up EQ"
    std::string why;       // one line: what this slot is for
    bool optional = false;

    int pick = -1;                 // index into the library, -1 = nothing suitable owned
    std::string pickName;          // "Pultec EQP-1A (UADx)"
    std::string pickCharacter;     // "analog", "analog_inspired", "digital"
    std::string pickFamily;
    int score = 0;
    std::vector<ChainOption> options;   // [0] = the pick above, then up to 2 alternatives

    std::string preset;            // option 1's preset advice, as text
    std::vector<KnobTip> knobs;    // 2-4 quick-tweak starting points (the target sound)
};

struct ChainRecommendation
{
    TrackRole role = TrackRole::Unknown;
    bool master = false;
    std::vector<ChainSlot> slots;
    int analogCount = 0;           // slots filled with an analog model
    int missingCount = 0;          // required slots with nothing owned
};

// Scoring for each slot (a plugin must do the slot's job to be considered):
//   analog model +40, analog-inspired +20 (30/15 for modern styles,
//   50/25 for vintage) (clean-up EQ and the master
//   limiter reverse this: clean digital +40);
//   preferred hardware family +25 / +20 / +15 by rank; matching type
//   (opto, FET, program EQ...) +10; tag confidence up to +5;
//   same hardware family as an earlier slot -30; favourite +3;
//   the format this instance runs as +1.
// Ties go to the plugin name, alphabetically. Each plugin is used once.
ChainRecommendation recommendChain (const std::vector<PluginInfo>& library, const ChainContext& ctx);

std::string displayName (const PluginInfo& p);   // "Pultec EQP-1A (UADx, AU)"

// Picks the saved preset of `p` that best fits the slot, role and style.
PresetAdvice choosePreset (const PluginInfo& p, const ChainContext& ctx, const std::string& slotId);

// One line on how `option` sounds next to `primary` in this slot.
std::string differenceNote (const PluginInfo& primary, const PluginInfo& option, const std::string& slotId);

} // namespace aimix
