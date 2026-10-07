#include "aimix/ChainRecommender.h"
#include "aimix/RuleEngine.h"

#include <algorithm>
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

int scorePlugin (const PluginInfo& p, const SlotDef& s, const std::vector<std::string>& usedFamilies, const std::string& preferredFormat)
{
    int score = 0;
    const bool clean = s.prefer == Prefer::CleanDigital;
    if (p.character == PluginCharacter::Analog)              score += clean ? 0 : 40;
    else if (p.character == PluginCharacter::AnalogInspired) score += 20;
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

const char* presetFor (TrackRole r, const std::string& slot, bool master)
{
    if (master)
    {
        if (slot == "bus_comp")   return "A \"Mix Bus\" or \"Gentle Glue\" preset, or the default.";
        if (slot == "program_eq") return "The default (flat) setting, then add the small boosts below.";
        if (slot == "tape")       return "A \"Mastering\" or \"Mix Bus\" preset at 30 ips.";
        return "The default setting, then set the ceiling and gain below.";
    }
    if (slot == "cleanup_eq") return "The default (flat) setting.";
    if (slot == "deesser")    return "A \"Vocal\" or \"Female/Male Vocal\" preset.";
    if (slot == "space")      return r == TrackRole::Vocal ? "A \"Vocal Plate\" or \"Slap\" preset." : "A short \"Room\" or \"Chorus\" preset.";
    switch (r)
    {
        case TrackRole::Vocal:  return "A preset named \"Lead Vocal\" or \"Vox\" if it has one, otherwise the default.";
        case TrackRole::Kick:   return "A \"Kick\" preset if it has one, otherwise the default.";
        case TrackRole::Snare:  return "A \"Snare\" preset if it has one, otherwise the default.";
        case TrackRole::Drums:  return "A \"Drum Bus\" or \"Drums\" preset if it has one, otherwise the default.";
        case TrackRole::Bass:   return "A \"Bass\" preset if it has one, otherwise the default.";
        case TrackRole::Guitar: return "A \"Guitar\" preset if it has one, otherwise the default.";
        case TrackRole::Keys:   return "A \"Piano\" or \"Keys\" preset if it has one, otherwise the default.";
        default:                return "The default setting.";
    }
}

std::vector<KnobTip> knobsFor (const ChainContext& c, const std::string& slot)
{
    const auto r = c.role;
    if (c.master)
    {
        if (slot == "bus_comp")
        {
            const bool squashed = c.measured && c.crestDb < 6.0f;
            return { { "Gain reduction", squashed ? "0-1 dB at most: the mix is already squashed" : "1-2 dB on the loudest parts" },
                     { "Ratio / Attack", "2:1, attack 10-30 ms, release Auto" },
                     { "Mix / Blend", "100%, or 50-70% for a lighter touch" } };
        }
        if (slot == "program_eq")
            return { { "Low boost", "+1 dB around 30-60 Hz" },
                     { "Top boost", "+1 dB shelf at 10-12 kHz" },
                     { "Drive / Input", "Unity: leave the level where it was" } };
        if (slot == "tape")
            return { { "Drive / Input", "Until peaks just soften, about 1-2 dB of saturation" },
                     { "Mix / Blend", "100%" } };
        return { { "Ceiling", "-1 dB true peak" },
                 { "Gain reduction", "2-3 dB on the loudest parts, no more" } };
    }

    if (slot == "cleanup_eq")
        return { { "High-pass", highPassFor (r) },
                 { "Cut", "-2 to -3 dB around 250-500 Hz if it sounds boxy" } };

    if (slot == "character_eq")
    {
        std::vector<KnobTip> k = { { "Drive / Input", "Push it until it just starts to colour, 1-3 dB hotter" } };
        switch (r)
        {
            case TrackRole::Vocal: k.push_back ({ "Tone / Colour", "+2 dB at 3-5 kHz for presence, +2 dB shelf at 10-12 kHz for air" }); break;
            case TrackRole::Kick:  k.push_back ({ "Tone / Colour", "+2 dB at 50-60 Hz for weight, +2 dB at 3-4 kHz for click" }); break;
            case TrackRole::Snare: k.push_back ({ "Tone / Colour", "+2 dB at 200 Hz for body, +2 dB at 5 kHz for crack" }); break;
            case TrackRole::Bass:  k.push_back ({ "Tone / Colour", "Boost and cut together at 60 Hz (the Pultec trick), +1 dB at 700 Hz for growl" }); break;
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
        else if (c.measured && c.levelSwingLu > 4.5f)
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
        return { { "Drive / Input", "Until it is 1-2 dB louder, then turn the output down to match" },
                 { "Mix / Blend", "30-50%" } };

    if (slot == "deesser")
        return { { "Frequency", "5-8 kHz, where the S sounds are" },
                 { "Gain reduction", "3-6 dB on the S sounds only" } };

    return { { "Mix / Blend", "15-25% if it is on the track; 100% on a send" },
             { "Decay / Time", r == TrackRole::Vocal ? "Plate 1.2-1.8 s, or a slap delay of 80-120 ms" : "Short, under 1.5 s" } };
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

ChainRecommendation recommendChain (const std::vector<PluginInfo>& library, const ChainContext& ctx)
{
    ChainRecommendation rec;
    rec.role = ctx.role;
    rec.master = ctx.master;

    std::vector<bool> used (library.size(), false);
    std::vector<std::string> usedFamilies;

    for (const auto& def : templateFor (ctx))
    {
        ChainSlot slot;
        slot.id = def.id;
        slot.label = def.label;
        slot.why = def.why;
        slot.optional = def.optional;

        struct Candidate { int index; int score; };
        std::vector<Candidate> candidates;
        for (size_t i = 0; i < library.size(); ++i)
        {
            const auto& p = library[i];
            if (used[i] || (p.functions & def.functions) == 0)
                continue;
            candidates.push_back ({ (int) i, scorePlugin (p, def, usedFamilies, ctx.preferredFormat) });
        }
        std::sort (candidates.begin(), candidates.end(), [&] (const Candidate& a, const Candidate& b)
        {
            if (a.score != b.score)
                return a.score > b.score;
            return library[(size_t) a.index].name < library[(size_t) b.index].name;
        });

        // The same plugin in another format (AU and VST3) is not a real alternative.
        std::vector<std::string> seenNames;
        for (const auto& cand : candidates)
        {
            const auto& p = library[(size_t) cand.index];
            if (std::find (seenNames.begin(), seenNames.end(), p.name) != seenNames.end())
                continue;
            seenNames.push_back (p.name);
            if (slot.pick < 0)
            {
                slot.pick = cand.index;
                slot.score = cand.score;
            }
            else if (slot.alternatives.size() < 2)
                slot.alternatives.push_back (cand.index);
        }

        if (slot.pick >= 0)
        {
            const auto& p = library[(size_t) slot.pick];
            slot.pickName = displayName (p);
            slot.pickCharacter = toString (p.character);
            slot.pickFamily = p.family;
            // A plugin is used once, in every format it was found in.
            for (size_t i = 0; i < library.size(); ++i)
                if (library[i].name == p.name && library[i].vendor == p.vendor)
                    used[i] = true;
            if (! p.family.empty())
                usedFamilies.push_back (p.family);
            if (p.character == PluginCharacter::Analog)
                ++rec.analogCount;
        }
        else if (! slot.optional)
            ++rec.missingCount;

        slot.preset = presetFor (ctx.role, slot.id, ctx.master);
        slot.knobs = knobsFor (ctx, slot.id);
        rec.slots.push_back (std::move (slot));
    }
    return rec;
}

} // namespace aimix
