#include "aimix/ChainRecommender.h"
#include "aimix/RuleEngine.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace aimix
{
namespace
{
enum class Prefer { Analog, CleanDigital };

struct SlotDef
{
    const char* id;
    const char* label;
    const char* why;
    uint32_t functions;                 // the plugin must do one of these
    std::vector<const char*> subtypes;  // preferred types, first is best
    std::vector<const char*> families;  // preferred hardware, first is best
    Prefer prefer = Prefer::Analog;
    bool optional = false;
};

SlotDef cleanupEq (const char* why)
{
    return { "cleanup_eq", "Clean-up EQ", why, kFnEq, { "parametric", "dynamic" }, {}, Prefer::CleanDigital };
}

std::vector<SlotDef> templateFor (const ChainContext& c)
{
    if (c.master)
        return {
            { "bus_comp", "Bus compressor", "Glues the mix together with 1-2 dB of gentle squeeze.",
              kFnCompressor, { "vca", "vari_mu" }, { "SSL", "Fairchild", "API" } },
            { "program_eq", "Master EQ", "Broad, musical tilts: a little weight down low and air up top.",
              kFnEq, { "program_eq" }, { "Pultec", "Manley", "Maag" } },
            { "tape", "Tape", "Rounds off sharp peaks and adds analog warmth.",
              kFnTape | kFnSaturation, {}, { "Studer", "Ampex" }, Prefer::Analog, true },
            { "limiter", "Limiter", "Sets the final loudness without letting anything clip.",
              kFnLimiter, { "precision" }, {}, Prefer::CleanDigital },
        };

    switch (c.role)
    {
        case TrackRole::Vocal:
            return {
                cleanupEq ("Removes rumble and boxy build-up before anything adds colour."),
                { "character_eq", "Character EQ", "Adds presence and air with a console-style tone.",
                  kFnEq | kFnPreamp, { "console", "program_eq" }, { "Neve", "API", "Pultec" } },
                { "compressor", "Fast compressor", "Catches the loudest words quickly.",
                  kFnCompressor, { "fet", "vca" }, { "1176", "SSL", "API" } },
                { "compressor2", "Smooth compressor", "Evens out the rest of the performance gently.",
                  kFnCompressor, { "opto", "vari_mu" }, { "LA-2A", "LA-3A", "Fairchild" }, Prefer::Analog, true },
                { "deesser", "De-esser", "Tames sharp S and T sounds.",
                  kFnDeesser, {}, {}, Prefer::CleanDigital, true },
                { "space", "Space", "A plate or short delay, best on a send so the dry vocal stays in front.",
                  kFnReverb | kFnDelay, {}, { "EMT", "Lexicon", "Roland" }, Prefer::Analog, true },
            };
        case TrackRole::Kick:
        case TrackRole::Snare:
            return {
                cleanupEq ("Cuts the boxy middle and anything below the useful low end."),
                { "character_eq", "Character EQ", c.role == TrackRole::Kick ? "Adds weight and a clear click."
                                                                             : "Adds body and crack.",
                  kFnEq | kFnPreamp, { "console" }, { "API", "Neve", "SSL" } },
                { "compressor", "Compressor", "Shapes the punch: slower attack lets the hit through.",
                  kFnCompressor, { "vca", "fet" }, { "API", "SSL", "1176" } },
                { "colour", "Saturation", "Adds harmonics so it cuts through on small speakers.",
                  kFnSaturation | kFnTape, {}, { "Studer", "Ampex", "Neve" }, Prefer::Analog, true },
            };
        case TrackRole::Drums:
            return {
                cleanupEq ("Clears mud from the kit before it hits the compressor."),
                { "character_eq", "Character EQ", "Broad tone for the whole kit.",
                  kFnEq | kFnPreamp, { "console" }, { "API", "SSL", "Neve" } },
                { "compressor", "Drum bus compressor", "Glues the kit and brings up the room.",
                  kFnCompressor, { "vca", "vari_mu" }, { "SSL", "API", "Fairchild" } },
                { "colour", "Tape / saturation", "Thickens the kit and softens harsh cymbal peaks.",
                  kFnTape | kFnSaturation, {}, { "Studer", "Ampex" }, Prefer::Analog, true },
            };
        case TrackRole::Bass:
            return {
                cleanupEq ("Removes sub rumble and boxy build-up."),
                { "character_eq", "Character EQ", "Low-end weight that stays tight.",
                  kFnEq | kFnPreamp, { "program_eq", "console" }, { "Pultec", "Neve", "API" } },
                { "compressor", "Compressor", "Keeps every note at the same level.",
                  kFnCompressor, { "opto", "fet" }, { "LA-2A", "1176", "dbx" } },
                { "colour", "Saturation", "Harmonics so the bass is heard on phones and laptops.",
                  kFnSaturation | kFnTape, {}, { "Ampex", "Studer", "Neve" }, Prefer::Analog, true },
            };
        default:
            return {
                cleanupEq ("Makes room: high-pass and clear the muddy middle."),
                { "character_eq", "Character EQ", "Analog tone and shine.",
                  kFnEq | kFnPreamp, { "console", "program_eq" }, { "Neve", "API", "SSL" } },
                { "compressor", "Compressor", "Steadies the level so it sits in one place.",
                  kFnCompressor, { "opto", "vca" }, { "LA-2A", "SSL", "1176" } },
                { "space", "Modulation / space", "Width and depth, best on a send.",
                  kFnModulation | kFnReverb | kFnDelay, {}, { "EMT", "Lexicon", "Roland" }, Prefer::Analog, true },
            };
    }
}

// Genre and era move the preferred hardware around and, for vintage styles,
// make the colour slots part of the chain rather than optional.
void tuneForStyle (std::vector<SlotDef>& slots, const ChainContext& c)
{
    const auto g = c.style.genre;
    const auto e = c.style.era;
    for (auto& s : slots)
    {
        const std::string id = s.id;
        if (c.role == TrackRole::Vocal && id == "compressor")
        {
            if (g == Genre::RnB)       { s.subtypes = { "opto", "vari_mu" }; s.families = { "LA-2A", "Fairchild", "1176" };
                                         s.label = "Main compressor"; s.why = "Smooth, even vocal level: the silky R&B sound."; }
            else if (g == Genre::Rock) { s.families = { "1176", "dbx", "API" }; }
        }
        if (c.role == TrackRole::Vocal && id == "compressor2" && g == Genre::RnB)
        {
            s.subtypes = { "fet", "vca" };
            s.families = { "1176", "SSL", "API" };
            s.label = "Peak catcher";
            s.why = "Grabs the occasional loud word the smooth compressor lets through.";
        }
        if ((c.role == TrackRole::Drums || c.role == TrackRole::Kick || c.role == TrackRole::Snare) && id == "compressor" && g == Genre::Rock)
            s.families = { "API", "1176", "SSL" };
        if (c.master && id == "bus_comp" && e == Era::Vintage)
        {
            s.subtypes = { "vari_mu", "opto" };
            s.families = { "Fairchild", "Vari-Mu", "SSL" };
        }
        if (c.master && id == "program_eq" && e == Era::Modern)
            s.families = { "Pultec", "Maag", "Manley" };
        if ((id == "colour" || id == "tape") && e == Era::Vintage)
            s.optional = false;
    }
}

int scorePlugin (const PluginInfo& p, const SlotDef& s, const std::vector<std::string>& usedFamilies,
                 const std::string& preferredFormat, int analogBonus)
{
    int score = 0;
    const bool clean = s.prefer == Prefer::CleanDigital;
    if (p.character == PluginCharacter::Analog)              score += clean ? 0 : analogBonus;
    else if (p.character == PluginCharacter::AnalogInspired) score += clean ? 20 : analogBonus / 2;
    else                                                     score += clean ? 40 : 0;

    for (size_t i = 0; i < s.families.size() && i < 3; ++i)
        if (! p.family.empty() && p.family == s.families[i])
        {
            score += 25 - 5 * (int) i;
            break;
        }
    for (const char* t : s.subtypes)
        if (p.subtype == t)
        {
            score += 10;
            break;
        }
    score += (int) (p.tagConfidence * 5.0f + 0.5f);
    if (! p.family.empty() && std::find (usedFamilies.begin(), usedFamilies.end(), p.family) != usedFamilies.end())
        score -= 30;
    if (p.favourite)
        score += 3;
    if (! preferredFormat.empty() && p.format == preferredFormat)
        score += 1;
    // A limiter, de-esser or utility in a tone slot is a poor fit even when it matches.
    if (s.functions != kFnLimiter && p.does (kFnLimiter) && ! p.does (kFnCompressor))
        score -= 40;
    if (p.does (kFnMultiband) && s.functions != kFnLimiter)
        score -= 10;
    return score;
}

std::string fmt (const char* f, double a)
{
    char buf[160];
    std::snprintf (buf, sizeof (buf), f, a);
    return buf;
}

const char* highPassFor (TrackRole r)
{
    switch (r)
    {
        case TrackRole::Kick:   return "High-pass at 25-30 Hz, only to clear rumble";
        case TrackRole::Bass:   return "High-pass at 30-35 Hz";
        case TrackRole::Vocal:  return "High-pass at 80-100 Hz";
        case TrackRole::Snare:  return "High-pass at 70-90 Hz";
        case TrackRole::Drums:  return "High-pass at 30-40 Hz";
        case TrackRole::Guitar: return "High-pass at 90-120 Hz";
        default:                return "High-pass at 60-100 Hz";
    }
}

// What a fitting factory preset is usually called, for when we can't see the
// plugin's own preset list.
std::string presetHint (const ChainContext& c, const std::string& slot)
{
    const std::string genre = c.style.isSet() && c.style.genre != Genre::Other ? std::string (genreName (c.style.genre)) + " " : "";
    if (c.master)
    {
        if (slot == "bus_comp")   return "\"" + genre + "Mix Bus\" or \"Gentle Glue\"";
        if (slot == "program_eq") return "\"Mastering\" or \"Air\"";
        if (slot == "tape")       return "\"Mastering\" or \"Mix Bus\" (30 ips)";
        return "\"" + genre + "Master\" or \"Transparent\"";
    }
    if (slot == "cleanup_eq") return "";
    if (slot == "deesser")    return "\"Vocal\" or \"De-ess Vocal\"";
    if (slot == "space")      return c.role == TrackRole::Vocal ? "\"Vocal Plate\" or \"Slap\"" : "\"Short Room\" or \"Chorus\"";
    if (slot == "colour" || slot == "tape") return "\"Warm\" or \"Tape\"";
    switch (c.role)
    {
        case TrackRole::Vocal:  return "\"" + genre + "Vocal\" or \"Lead Vocal\"";
        case TrackRole::Kick:   return genre.empty() ? "\"Kick\"" : "\"" + genre + "Kick\" or \"Kick\"";
        case TrackRole::Snare:  return "\"Snare\"";
        case TrackRole::Drums:  return "\"" + genre + "Drums\" or \"Drum Bus\"";
        case TrackRole::Bass:   return genre.empty() ? "\"Bass\"" : "\"" + genre + "Bass\" or \"Bass\"";
        case TrackRole::Guitar: return "\"Guitar\"";
        case TrackRole::Keys:   return "\"Piano\" or \"Keys\"";
        default:                return "";
    }
}

std::vector<std::string> roleWords (const ChainContext& c)
{
    if (c.master)
        return { "master", "mastering", "mix bus", "mixbus", "2bus", "2-bus", "stereo bus", "bus", "mix" };
    switch (c.role)
    {
        case TrackRole::Vocal:  return { "vocal", "vox", "voice", "lead", "rap", "sing", "verse", "hook" };
        case TrackRole::Kick:   return { "kick", "bd", "808" };
        case TrackRole::Snare:  return { "snare", "sd", "clap", "rim" };
        case TrackRole::Drums:  return { "drum", "kit", "room", "overhead", "oh" };
        case TrackRole::Bass:   return { "bass", "808", "sub" };
        case TrackRole::Guitar: return { "guitar", "gtr", "git", "acoustic" };
        case TrackRole::Keys:   return { "piano", "keys", "rhodes", "organ", "ep" };
        case TrackRole::Synth:  return { "synth", "pad", "pluck", "lead", "arp" };
        default:                return { "instrument", "general", "all purpose" };
    }
}

std::vector<std::string> slotWords (const std::string& slot)
{
    if (slot == "cleanup_eq")   return { "clean", "cut", "hpf", "high pass", "low cut", "corrective" };
    if (slot == "character_eq") return { "presence", "air", "warm", "tone", "color", "colour", "bright", "body" };
    if (slot == "compressor")   return { "fast", "punch", "control", "peak", "aggressive", "attack" };
    if (slot == "compressor2")  return { "smooth", "level", "gentle", "opto", "even", "slow" };
    if (slot == "deesser")      return { "de-ess", "deess", "de ess", "sibil", "ess" };
    if (slot == "space")        return { "plate", "room", "slap", "delay", "hall", "chamber", "verb" };
    if (slot == "colour" || slot == "tape") return { "tape", "warm", "saturat", "drive", "thick", "glue" };
    if (slot == "bus_comp")     return { "glue", "bus", "mix", "gentle" };
    if (slot == "program_eq")   return { "master", "air", "sweet", "polish", "open" };
    if (slot == "limiter")      return { "master", "loud", "limit", "transparent", "streaming" };
    return {};
}

std::string lowerCase (std::string s)
{
    for (auto& ch : s)
        ch = (char) std::tolower ((unsigned char) ch);
    return s;
}

// Short words must stand alone ("sd" in "SD Tight", not in "Wasd"); longer
// ones may sit inside a word ("vocal" in "Vocals").
bool nameHas (const std::string& lowerName, const std::string& word)
{
    if (word.size() >= 3)
        return lowerName.find (word) != std::string::npos;
    size_t pos = 0;
    while ((pos = lowerName.find (word, pos)) != std::string::npos)
    {
        const bool startOk = pos == 0 || ! std::isalnum ((unsigned char) lowerName[pos - 1]);
        const auto end = pos + word.size();
        const bool endOk = end >= lowerName.size() || ! std::isalpha ((unsigned char) lowerName[end]);
        if (startOk && endOk)
            return true;
        pos = end;
    }
    return false;
}

bool hasAny (const std::string& lowerName, const std::vector<std::string>& words)
{
    for (const auto& w : words)
        if (nameHas (lowerName, w))
            return true;
    return false;
}

std::string tweakSummary (const std::vector<KnobTip>& knobs)
{
    std::string out;
    for (size_t i = 0; i < knobs.size() && i < 2; ++i)
    {
        if (! out.empty()) out += "; ";
        auto setting = knobs[i].setting;
        if (! setting.empty() && setting[0] >= 'A' && setting[0] <= 'Z' && (setting.size() < 2 || ! (setting[1] >= 'A' && setting[1] <= 'Z')))
            setting[0] = (char) (setting[0] - 'A' + 'a');
        out += knobs[i].knob + ": " + setting;
    }
    return out;
}

std::vector<KnobTip> knobsFor (const ChainContext& c, const std::string& slot)
{
    const auto r = c.role;
    const bool set = c.style.isSet();
    const auto era = c.style.era;
    const auto genre = c.style.genre;
    const bool vintage = set && era == Era::Vintage;
    const bool modern = set && era == Era::Modern;
    const bool lowEndGenre = set && (genre == Genre::HipHop || genre == Genre::RnB || genre == Genre::Electronic);
    if (c.master)
    {
        const auto profile = profileFor (c.style);
        const float squashLimit = set ? profile.mixSquashedCrestDb : 6.0f;
        if (slot == "bus_comp")
        {
            const bool squashed = c.measured && c.crestDb < squashLimit;
            return { { "Gain reduction", squashed ? "0-1 dB at most: the mix is already squashed"
                                       : modern ? "2-3 dB on the loudest parts"
                                       : vintage ? "1 dB, just enough to glue" : "1-2 dB on the loudest parts" },
                     { "Ratio / Attack", vintage ? "Low ratio, slow attack, slow release" : "2:1, attack 10-30 ms, release Auto" },
                     { "Mix / Blend", "100%, or 50-70% for a lighter touch" } };
        }
        if (slot == "program_eq")
            return { { "Low boost", lowEndGenre ? "+1-2 dB around 40-60 Hz" : "+1 dB around 30-60 Hz" },
                     { "Top boost", vintage ? "Leave the top alone, or +0.5 dB at 10 kHz" : modern ? "+1-2 dB shelf at 10-12 kHz" : "+1 dB shelf at 10-12 kHz" },
                     { "Drive / Input", "Unity: leave the level where it was" } };
        if (slot == "tape")
            return { { "Drive / Input", vintage ? "Push it until you hear it: 2-3 dB of saturation" : "Until peaks just soften, about 1-2 dB of saturation" },
                     { "Mix / Blend", "100%" } };
        return { { "Ceiling", "-1 dB true peak" },
                 { "Gain / Loudness", set ? fmt ("Raise until the song reads about %.0f LUFS, with no more than 3 dB of reduction", profile.targetLufs)
                                          : "2-3 dB on the loudest parts, no more" } };
    }

    if (slot == "cleanup_eq")
        return { { "High-pass", highPassFor (r) },
                 { "Cut", "-2 to -3 dB around 250-500 Hz if it sounds boxy" } };

    if (slot == "character_eq")
    {
        std::vector<KnobTip> k = { { "Drive / Input", "Push it until it just starts to colour, 1-3 dB hotter" } };
        switch (r)
        {
            case TrackRole::Vocal:
                k.push_back ({ "Tone / Colour", vintage ? "+1-2 dB at 3-5 kHz, skip big air boosts, +1 dB at 150-200 Hz for warmth"
                                              : modern  ? "+2 dB at 3-5 kHz for presence, +2-3 dB shelf at 10-12 kHz for air"
                                                        : "+2 dB at 3-5 kHz for presence, +2 dB shelf at 10-12 kHz for air" });
                break;
            case TrackRole::Kick:
                k.push_back ({ "Tone / Colour", lowEndGenre ? "+3 dB at 50-60 Hz for weight, +2 dB at 3-4 kHz for click"
                                              : set && genre == Genre::Rock ? "+2 dB at 60-80 Hz, +3 dB at 3-5 kHz for the beater"
                                                                            : "+2 dB at 50-60 Hz for weight, +2 dB at 3-4 kHz for click" });
                break;
            case TrackRole::Snare: k.push_back ({ "Tone / Colour", "+2 dB at 200 Hz for body, +2 dB at 5 kHz for crack" }); break;
            case TrackRole::Bass:
                k.push_back ({ "Tone / Colour", lowEndGenre ? "Weight at 40-60 Hz (boost and cut together for the Pultec trick), keep it mono below 120 Hz"
                                                            : "Boost and cut together at 60 Hz (the Pultec trick), +1 dB at 700 Hz for growl" });
                break;
            case TrackRole::Drums: k.push_back ({ "Tone / Colour", "+1-2 dB at 60 Hz and a +1-2 dB shelf at 10 kHz" }); break;
            default:               k.push_back ({ "Tone / Colour", "+1-2 dB where it shines, usually 2-5 kHz" }); break;
        }
        k.push_back ({ "Output", "Match the level to bypass so you hear tone, not volume" });
        return k;
    }

    if (slot == "compressor" || slot == "compressor2")
    {
        std::string gr;
        std::string mix = "100%";
        const bool drums = r == TrackRole::Kick || r == TrackRole::Snare || r == TrackRole::Drums;
        if (drums && c.measured && c.crestDb < 8.0f)
            gr = "1-2 dB at most: it is already squashed";
        else if (drums && c.measured && c.crestDb > 18.0f)
        {
            gr = "8-10 dB, heavily";
            mix = "30-40%: parallel, so the hits stay sharp";
        }
        else if (slot == "compressor2")
            gr = "2-3 dB, slow and smooth";
        else if (c.measured && c.levelSwingLu > (set ? profileFor (c.style).levelSwingLu : 4.5f))
            gr = fmt ("5-8 dB on the loudest parts: the level swings %.1f LU", c.levelSwingLu);
        else
            gr = "3-4 dB on the loudest parts";

        const char* attack = slot == "compressor2"   ? "Slow attack and release: let it ride the level"
                           : r == TrackRole::Vocal ? "Fast attack, medium release"
                           : drums                 ? "Attack 10-30 ms so the hit gets through, fast release"
                                                   : "Medium attack, release Auto";
        return { { "Gain reduction", gr }, { "Attack / Release", attack }, { "Mix / Blend", mix } };
    }

    if (slot == "colour" || slot == "tape")
        return { { "Drive / Input", vintage ? "Push it: 2-4 dB of drive, you should hear it, then match the output"
                                    : modern ? "Subtle: about 1 dB of drive, then match the output"
                                             : "Until it is 1-2 dB louder, then turn the output down to match" },
                 { "Mix / Blend", vintage ? "50-100%" : "30-50%" } };

    if (slot == "deesser")
        return { { "Frequency", "5-8 kHz, where the S sounds are" },
                 { "Gain reduction", "3-6 dB on the S sounds only" } };

    return { { "Mix / Blend", "15-25% if it is on the track; 100% on a send" },
             { "Decay / Time", r != TrackRole::Vocal ? "Short, under 1.5 s"
                               : vintage ? "Plate 1.8-2.5 s, or a slap delay of 100-140 ms"
                               : modern  ? "Short plate 0.8-1.2 s, or a 1/8 note delay low in the mix"
                                         : "Plate 1.2-1.8 s, or a slap delay of 80-120 ms" } };
}
} // namespace

ChainContext chainContextFor (const TrackView& t, bool master)
{
    ChainContext c;
    c.role = master ? TrackRole::MasterBus : t.role;
    c.master = master;
    c.measured = t.active && t.maxPeakDb() > -90.0f;
    c.peakDb = t.maxPeakDb();
    c.crestDb = t.maxPeakDb() - t.maxRmsDb();
    c.levelSwingLu = t.levelSwingLu;
    c.sideToMidDb = t.sideToMidDb;
    return c;
}

std::string displayName (const PluginInfo& p)
{
    std::string s = p.name;
    std::string extra;
    if (! p.vendor.empty() && p.name.find (p.vendor) == std::string::npos)
        extra = p.vendor;
    if (! p.format.empty())
        extra += (extra.empty() ? "" : ", ") + p.format;
    if (! extra.empty())
        s += " (" + extra + ")";
    return s;
}

PresetAdvice choosePreset (const PluginInfo& p, const ChainContext& ctx, const std::string& slotId)
{
    PresetAdvice advice;
    const auto knobs = knobsFor (ctx, slotId);
    const auto role = roleWords (ctx);
    const auto slot = slotWords (slotId);
    const auto genre = ctx.style.isSet() ? genreKeywords (ctx.style.genre) : std::vector<std::string>();
    const auto era = ctx.style.isSet() ? eraKeywords (ctx.style.era) : std::vector<std::string>();

    int bestScore = 0;
    bool bestExact = false;
    for (const auto& preset : p.presets)
    {
        const auto n = lowerCase (preset);
        const bool r = hasAny (n, role);
        const bool g = hasAny (n, genre);
        const bool e = hasAny (n, era);
        const bool sl = hasAny (n, slot);
        // Named for a different genre or era ("Hip-Hop Glue" on an R&B mix): at best the closest.
        bool otherGenre = false, otherEra = false;
        if (ctx.style.isSet())
        {
            for (int k = 1; k < kNumGenres && ! g; ++k)
                otherGenre |= (Genre) k != ctx.style.genre && hasAny (n, genreKeywords ((Genre) k));
            for (int k = 0; k < kNumEras && ! e; ++k)
                otherEra |= (Era) k != ctx.style.era && hasAny (n, eraKeywords ((Era) k));
        }
        int score = (r ? 4 : 0) + (g ? 3 : 0) + (e ? 2 : 0) + (sl ? 2 : 0) - (otherGenre ? 3 : 0) - (otherEra ? 2 : 0);
        score = std::max (score, r || sl ? 1 : 0);
        // Exact: made for this kind of track, and for this style or this job.
        const bool exact = r && (g || e || sl || ! ctx.style.isSet()) && ! otherGenre && ! otherEra;
        if (score > bestScore || (score == bestScore && exact && ! bestExact)
            || (score == bestScore && exact == bestExact && score > 0 && preset.size() < advice.name.size()))
        {
            bestScore = score;
            bestExact = exact;
            advice.name = preset;
        }
    }

    if (bestScore > 0 && bestExact)
    {
        advice.match = PresetMatch::Exact;
        advice.text = "Load your preset \"" + advice.name + "\".";
        return advice;
    }
    if (bestScore > 0)
    {
        advice.match = PresetMatch::Closest;
        advice.text = "No saved preset made for this exact sound. Closest one you have: \"" + advice.name
                    + "\". Load it, then set " + tweakSummary (knobs) + ".";
        return advice;
    }

    advice.name.clear();
    advice.match = PresetMatch::NotFound;
    const auto hint = presetHint (ctx, slotId);
    if (slotId == "cleanup_eq" || hint.empty())
        advice.text = "Start from the default (flat) setting and set the knobs below.";
    else if (p.presets.empty())
        advice.text = "Look in its preset menu for one like " + hint + ". If there isn't one, start from the default and set the knobs below.";
    else
        advice.text = "None of your saved presets for it fit. Look in its preset menu for one like " + hint
                    + ", or start from the default and set the knobs below.";
    return advice;
}

std::string differenceNote (const PluginInfo& primary, const PluginInfo& option, const std::string& slotId)
{
    using C = PluginCharacter;
    const auto a = primary.character;
    const auto b = option.character;
    std::string colour;
    if (a == C::Analog && b == C::Digital)               colour = "Cleaner and more transparent: less colour, more precision";
    else if (a == C::Digital && b == C::Analog)          colour = "Warmer, with real analog colour";
    else if (a == C::Analog && b == C::AnalogInspired)   colour = "Analog-style colour, a little cleaner than a hardware model";
    else if (a == C::AnalogInspired && b == C::Analog)   colour = "More authentic analog warmth";
    else if (a == C::Digital && b == C::AnalogInspired)  colour = "Adds some analog-style warmth";
    else if (a == C::AnalogInspired && b == C::Digital)  colour = "Cleaner and more transparent";

    std::string feel;
    const auto& t = option.subtype;
    if (t != primary.subtype)
    {
        if (t == "fet")             feel = "faster and punchier, a more aggressive grab";
        else if (t == "opto")       feel = "slower and smoother, gentle levelling";
        else if (t == "vca")        feel = "tighter and more controlled: classic glue";
        else if (t == "vari_mu")    feel = "thick, slow glue with tube warmth";
        else if (t == "program_eq") feel = "broad, musical curves for big smooth boosts";
        else if (t == "console")    feel = "focused, punchy console tone";
        else if (t == "parametric") feel = "surgical, precise narrow moves";
        else if (t == "dynamic")    feel = "cuts only when the problem shows up";
    }
    if (feel.empty() && ! option.family.empty() && option.family != primary.family)
        feel = "a different hardware flavour (" + option.family + " instead of " + (primary.family.empty() ? std::string ("a generic model") : primary.family) + ")";

    if (! colour.empty() && ! feel.empty())
        return colour + "; " + feel + ".";
    if (! colour.empty())
        return colour + ".";
    if (! feel.empty())
    {
        auto f = feel;
        f[0] = (char) std::toupper ((unsigned char) f[0]);
        return f + ".";
    }
    (void) slotId;
    return "Does the same job with its own flavour; try it if option 1 doesn't sit right.";
}

ChainRecommendation recommendChain (const std::vector<PluginInfo>& library, const ChainContext& ctx)
{
    ChainRecommendation rec;
    rec.role = ctx.role;
    rec.master = ctx.master;

    const int analogBonus = ctx.style.isSet() ? profileFor (ctx.style).analogBonus : 40;
    std::vector<bool> used (library.size(), false);
    std::vector<std::string> usedFamilies;

    auto defs = templateFor (ctx);
    tuneForStyle (defs, ctx);

    for (const auto& def : defs)
    {
        ChainSlot slot;
        slot.id = def.id;
        slot.label = def.label;
        slot.why = def.why;
        slot.optional = def.optional;
        slot.knobs = knobsFor (ctx, slot.id);

        struct Candidate { int index; int score; };
        std::vector<Candidate> candidates;
        for (size_t i = 0; i < library.size(); ++i)
        {
            const auto& p = library[i];
            if (used[i] || (p.functions & def.functions) == 0)
                continue;
            candidates.push_back ({ (int) i, scorePlugin (p, def, usedFamilies, ctx.preferredFormat, analogBonus) });
        }
        std::sort (candidates.begin(), candidates.end(), [&] (const Candidate& a, const Candidate& b)
        {
            if (a.score != b.score)
                return a.score > b.score;
            return library[(size_t) a.index].name < library[(size_t) b.index].name;
        });

        // The same plugin in another format (AU and VST3) is not a real alternative.
        std::vector<Candidate> unique;
        std::vector<std::string> seenNames;
        for (const auto& cand : candidates)
        {
            const auto& p = library[(size_t) cand.index];
            if (std::find (seenNames.begin(), seenNames.end(), p.name) != seenNames.end())
                continue;
            seenNames.push_back (p.name);
            unique.push_back (cand);
        }

        if (! unique.empty())
        {
            const auto& first = unique.front();
            const auto& primary = library[(size_t) first.index];

            // Options 2 and 3 should sound different from option 1 (and from
            // each other): different hardware, type or character first.
            std::vector<Candidate> picks { first };
            auto distinct = [&] (const PluginInfo& p)
            {
                for (const auto& c : picks)
                {
                    const auto& q = library[(size_t) c.index];
                    const bool sameFamily = ! p.family.empty() && p.family == q.family;
                    const bool sameFeel = p.character == q.character && p.subtype == q.subtype;
                    if (sameFamily || sameFeel)
                        return false;
                }
                return true;
            };
            for (size_t i = 1; i < unique.size() && picks.size() < 3; ++i)
                if (distinct (library[(size_t) unique[i].index]))
                    picks.push_back (unique[i]);
            for (size_t i = 1; i < unique.size() && picks.size() < 3; ++i)
                if (std::none_of (picks.begin(), picks.end(), [&] (const Candidate& c) { return c.index == unique[i].index; }))
                    picks.push_back (unique[i]);

            for (size_t k = 0; k < picks.size(); ++k)
            {
                const auto& p = library[(size_t) picks[k].index];
                ChainOption o;
                o.pick = picks[k].index;
                o.name = displayName (p);
                o.character = toString (p.character);
                o.family = p.family;
                o.score = picks[k].score;
                o.preset = choosePreset (p, ctx, slot.id);
                if (k > 0)
                    o.note = differenceNote (primary, p, slot.id);
                slot.options.push_back (std::move (o));
            }

            const auto& o1 = slot.options.front();
            slot.pick = o1.pick;
            slot.score = o1.score;
            slot.pickName = o1.name;
            slot.pickCharacter = o1.character;
            slot.pickFamily = o1.family;
            slot.preset = o1.preset.text;

            // A plugin is used once, in every format it was found in.
            for (size_t i = 0; i < library.size(); ++i)
                if (library[i].name == primary.name && library[i].vendor == primary.vendor)
                    used[i] = true;
            if (! primary.family.empty())
                usedFamilies.push_back (primary.family);
            if (primary.character == PluginCharacter::Analog)
                ++rec.analogCount;
        }
        else
        {
            if (! slot.optional)
                ++rec.missingCount;
            slot.preset = "Start from the default setting and set the knobs below.";
        }

        rec.slots.push_back (std::move (slot));
    }
    return rec;
}

} // namespace aimix
