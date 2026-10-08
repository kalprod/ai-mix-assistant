#include "aimix/MixStyle.h"
#include "aimix/RuleEngine.h"

namespace aimix
{
uint32_t MixStyle::pack() const noexcept
{
    return isSet() ? (0x100u | ((uint32_t) genre << 4) | (uint32_t) era) : 0u;
}

MixStyle MixStyle::unpack (uint32_t v) noexcept
{
    MixStyle s;
    if ((v & 0x100u) == 0)
        return s;
    const auto g = (v >> 4) & 0xfu;
    const auto e = v & 0xfu;
    if (g == 0 || g >= (uint32_t) kNumGenres || e >= (uint32_t) kNumEras)
        return s;
    s.genre = (Genre) g;
    s.era = (Era) e;
    return s;
}

StyleProfile profileFor (MixStyle style)
{
    StyleProfile p;
    if (! style.isSet())
        return p;

    // Loudness of the finished master, by genre (modern streaming practice),
    // then eased off for older styles, which keep more dynamics.
    switch (style.genre)
    {
        case Genre::HipHop:     p.targetLufs = -9.0f;  p.summary = "Heavy low end, upfront vocal, punchy drums"; break;
        case Genre::RnB:        p.targetLufs = -10.0f; p.summary = "Smooth, warm vocal, deep but soft low end"; break;
        case Genre::Pop:        p.targetLufs = -9.0f;  p.summary = "Bright, loud and polished, vocal on top"; break;
        case Genre::Rock:       p.targetLufs = -9.5f;  p.summary = "Big guitars and drums, mids that drive"; break;
        case Genre::Electronic: p.targetLufs = -8.5f;  p.summary = "Tight sub, wide synths, loud and clean"; break;
        case Genre::Other:
        case Genre::Unset:      p.targetLufs = -12.0f; p.summary = "Balanced and natural"; break;
    }

    switch (style.era)
    {
        case Era::Modern:
            p.mixSquashedCrestDb = 5.0f;
            p.levelSwingLu = 4.0f;          // vocals held steady
            p.harshExcessDb = 6.5f;         // bright top is part of the sound
            p.mudExcessDb = 4.5f;           // clean low mids
            p.wetVocalSideToMidDb = -14.0f; // drier vocals
            p.analogBonus = 30;
            break;
        case Era::OldSchool:
            p.targetLufs -= 2.0f;   // quieter, more dynamic
            p.mixSquashedCrestDb = 6.5f;
            p.levelSwingLu = 5.0f;
            p.harshExcessDb = 5.5f;
            p.mudExcessDb = 5.0f;
            p.wetVocalSideToMidDb = -12.0f;
            p.analogBonus = 40;
            break;
        case Era::Vintage:
            p.targetLufs -= 4.0f;
            p.mixSquashedCrestDb = 8.0f;    // let it breathe
            p.levelSwingLu = 5.5f;
            p.harshExcessDb = 5.0f;         // darker, softer top
            p.mudExcessDb = 6.0f;           // warm low mids are welcome
            p.wetVocalSideToMidDb = -10.0f; // more room and plate
            p.analogBonus = 50;
            break;
    }
    return p;
}

RuleConfig applyStyle (RuleConfig c, MixStyle style)
{
    if (! style.isSet())
        return c;
    const auto p = profileFor (style);
    c.mixTargetLufs = p.targetLufs;
    c.mixSquashedCrestDb = p.mixSquashedCrestDb;
    c.levelSwingLu = p.levelSwingLu;
    c.harshExcessDb = p.harshExcessDb;
    c.mudExcessDb = p.mudExcessDb;
    c.wetVocalSideToMidDb = p.wetVocalSideToMidDb;
    return c;
}

const char* genreName (Genre g) noexcept
{
    switch (g)
    {
        case Genre::HipHop:     return "Hip-Hop";
        case Genre::RnB:        return "R&B";
        case Genre::Pop:        return "Pop";
        case Genre::Rock:       return "Rock";
        case Genre::Electronic: return "Electronic";
        case Genre::Other:      return "Other";
        case Genre::Unset:      break;
    }
    return "Not set";
}

const char* eraName (Era e) noexcept
{
    switch (e)
    {
        case Era::Modern:    return "Modern";
        case Era::OldSchool: return "Old School";
        case Era::Vintage:   return "Vintage";
    }
    return "Modern";
}

std::string styleName (MixStyle s)
{
    if (! s.isSet())
        return "No genre chosen";
    return std::string (genreName (s.genre)) + ", " + eraName (s.era);
}

std::vector<std::string> genreKeywords (Genre g)
{
    switch (g)
    {
        case Genre::HipHop:     return { "hip", "hop", "hiphop", "rap", "trap", "808", "boom", "drill" };
        case Genre::RnB:        return { "rnb", "r&b", "soul", "smooth", "silky", "neo" };
        case Genre::Pop:        return { "pop", "radio", "sparkle", "shine" };
        case Genre::Rock:       return { "rock", "crunch", "punk", "metal", "grit" };
        case Genre::Electronic: return { "edm", "electro", "electronic", "house", "techno", "club", "dance" };
        case Genre::Other:
        case Genre::Unset:      break;
    }
    return {};
}

std::vector<std::string> eraKeywords (Era e)
{
    switch (e)
    {
        case Era::Modern:    return { "modern", "clean", "bright", "punch", "tight", "crisp" };
        case Era::OldSchool: return { "old", "school", "90s", "80s", "boom", "classic", "dusty" };
        case Era::Vintage:   return { "vintage", "warm", "tape", "70s", "60s", "classic", "retro", "tube", "analog" };
    }
    return {};
}

} // namespace aimix
