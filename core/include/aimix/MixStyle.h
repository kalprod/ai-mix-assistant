#pragma once

// The genre and era the user is mixing toward. It tunes the analysis targets
// (loudness, dynamics, tone) and the plugin/preset picks.

#include <cstdint>
#include <string>
#include <vector>

namespace aimix
{
struct RuleConfig;

enum class Genre : uint8_t { Unset = 0, HipHop, RnB, Pop, Rock, Electronic, Other };
constexpr int kNumGenres = 7;

enum class Era : uint8_t { Modern = 0, OldSchool, Vintage };
constexpr int kNumEras = 3;

struct MixStyle
{
    Genre genre = Genre::Unset;
    Era era = Era::Modern;

    bool isSet() const noexcept { return genre != Genre::Unset; }
    bool operator== (const MixStyle& o) const noexcept { return genre == o.genre && era == o.era; }
    bool operator!= (const MixStyle& o) const noexcept { return ! (*this == o); }

    // For the shared bus: 0 = not chosen.
    uint32_t pack() const noexcept;
    static MixStyle unpack (uint32_t v) noexcept;
};

struct StyleProfile
{
    float targetLufs = -14.0f;        // integrated loudness of the finished master
    float mixSquashedCrestDb = 6.0f;  // mix peak-to-RMS below this: over-compressed
    float levelSwingLu = 4.5f;        // vocal/bass loudness spread that calls for compression
    float harshExcessDb = 6.0f;
    float mudExcessDb = 5.0f;
    float wetVocalSideToMidDb = -12.0f;
    int analogBonus = 40;             // how strongly analog models are preferred in chains
    const char* summary = "";         // one line: what this style aims for
};

StyleProfile profileFor (MixStyle style);

// The rule thresholds for a style (unchanged when no style is chosen).
RuleConfig applyStyle (RuleConfig base, MixStyle style);

const char* genreName (Genre) noexcept;   // "Hip-Hop"
const char* eraName (Era) noexcept;       // "Old School"
std::string styleName (MixStyle);         // "Hip-Hop, Modern"

// Words that preset names for this genre/era tend to contain, lower case.
std::vector<std::string> genreKeywords (Genre);
std::vector<std::string> eraKeywords (Era);

} // namespace aimix
