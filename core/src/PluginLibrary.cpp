#include "aimix/PluginLibrary.h"

#include <algorithm>
#include <cctype>

namespace aimix
{
namespace
{
std::string lower (std::string s)
{
    for (auto& c : s)
        c = (char) std::tolower ((unsigned char) c);
    return s;
}

// Lower-case letters and digits only: "UADx Pultec EQP-1A" -> "uadxpulteceqp1a".
std::string squash (const std::string& s)
{
    std::string out;
    for (char c : s)
        if (std::isalnum ((unsigned char) c))
            out += (char) std::tolower ((unsigned char) c);
    return out;
}

struct Words
{
    std::vector<std::string> tokens;
    std::string squashed;

    explicit Words (const std::string& s) : tokens (nameTokens (s)), squashed (squash (s)) {}

    bool hasToken (const char* t) const
    {
        return std::find (tokens.begin(), tokens.end(), t) != tokens.end();
    }

    // Patterns of four or more characters may sit anywhere in the squashed
    // name ("eqp1a" in "UADx Pultec EQP-1A"). Shorter ones ("api", "j37",
    // "l2") must be whole words, or neighbouring words run together.
    bool has (const char* pattern) const
    {
        const std::string p (pattern);
        if (p.size() >= 4)
            return squashed.find (p) != std::string::npos;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            std::string joined;
            for (size_t j = i; j < tokens.size() && joined.size() < p.size(); ++j)
            {
                joined += tokens[j];
                if (joined == p)
                    return true;
            }
        }
        return false;
    }
};

struct CatalogEntry
{
    std::vector<const char*> patterns;   // all must match the name
    const char* vendor;                  // squashed vendor fragment, or nullptr for any
    uint32_t functions;
    const char* subtype;
    PluginCharacter character;
    const char* family;
    const char* emulates;
};

constexpr auto A  = PluginCharacter::Analog;
constexpr auto AI = PluginCharacter::AnalogInspired;
constexpr auto D  = PluginCharacter::Digital;

// Well-known plugins. First match wins, so specific entries come first.
const std::vector<CatalogEntry>& catalog()
{
    static const std::vector<CatalogEntry> c = {
        // --- Compressors
        { { "la2a" },        nullptr, kFnCompressor, "opto", A, "LA-2A", "Teletronix LA-2A" },
        { { "white2a" },     nullptr, kFnCompressor, "opto", A, "LA-2A", "Teletronix LA-2A" },
        { { "fg2a" },        nullptr, kFnCompressor, "opto", A, "LA-2A", "Teletronix LA-2A" },
        { { "la3a" },        nullptr, kFnCompressor, "opto", A, "LA-3A", "UREI LA-3A" },
        { { "cl1b" },        nullptr, kFnCompressor, "opto", A, "Tube-Tech", "Tube-Tech CL 1B" },
        { { "1176" },        nullptr, kFnCompressor, "fet", A, "1176", "UREI 1176" },
        { { "cla76" },       nullptr, kFnCompressor, "fet", A, "1176", "UREI 1176" },
        { { "fet76" },       nullptr, kFnCompressor, "fet", A, "1176", "UREI 1176" },
        { { "black76" },     nullptr, kFnCompressor, "fet", A, "1176", "UREI 1176" },
        { { "fg116" },       nullptr, kFnCompressor, "fet", A, "1176", "UREI 1176" },
        { { "distressor" },  nullptr, kFnCompressor | kFnSaturation, "fet", A, "Distressor", "Empirical Labs Distressor" },
        { { "fatso" },       nullptr, kFnCompressor | kFnSaturation | kFnTape, "fet", A, "Fatso", "Empirical Labs Fatso" },
        { { "fairchild" },   nullptr, kFnCompressor, "vari_mu", A, "Fairchild", "Fairchild 670" },
        { { "puigchild" },   nullptr, kFnCompressor, "vari_mu", A, "Fairchild", "Fairchild 660" },
        { { "varimu" },      nullptr, kFnCompressor, "vari_mu", A, "Vari-Mu", "Manley Variable Mu" },
        { { "variablemu" },  nullptr, kFnCompressor, "vari_mu", A, "Vari-Mu", "Manley Variable Mu" },
        { { "tubesta" },     nullptr, kFnCompressor, "vari_mu", A, "STA-Level", "Gates STA-Level" },
        { { "dbx160" },      nullptr, kFnCompressor, "vca", A, "dbx", "dbx 160" },
        { { "vca65" },       nullptr, kFnCompressor, "vca", A, "dbx", "dbx 165A" },
        { { "api2500" },     nullptr, kFnCompressor, "vca", A, "API", "API 2500" },
        { { "api", "2500" }, nullptr, kFnCompressor, "vca", A, "API", "API 2500" },
        { { "33609" },       nullptr, kFnCompressor, "vca", A, "Neve", "Neve 33609" },
        { { "vcomp" },       nullptr, kFnCompressor, "vca", A, "Neve", "Neve 2254" },
        { { "2254" },        nullptr, kFnCompressor, "vca", A, "Neve", "Neve 2254" },
        { { "rs124" },       nullptr, kFnCompressor, "vari_mu", A, "EMI", "EMI RS124" },
        { { "shadowhills" }, nullptr, kFnCompressor, "opto", A, "Shadow Hills", "Shadow Hills Mastering Compressor" },
        { { "elysia", "mpressor" }, nullptr, kFnCompressor, "vca", A, "Elysia", "Elysia mpressor" },
        { { "gbus" },        nullptr, kFnCompressor, "vca", A, "SSL", "SSL G Bus Compressor" },
        { { "gmaster" },     nullptr, kFnCompressor, "vca", A, "SSL", "SSL G Bus Compressor" },
        { { "ssl", "bus" },  nullptr, kFnCompressor, "vca", A, "SSL", "SSL G Bus Compressor" },
        { { "virtualbuscompressor" }, nullptr, kFnCompressor, "vca", A, "SSL", "SSL G Bus Compressor" },
        { { "townhouse" },   nullptr, kFnCompressor, "vca", A, "SSL", "SSL G Bus Compressor" },
        { { "glue" },        nullptr, kFnCompressor, "vca", A, "SSL", "SSL G Bus Compressor" },
        { { "devilloc" },    nullptr, kFnCompressor | kFnSaturation, "", AI, "", "" },
        { { "kotelnikov" },  nullptr, kFnCompressor, "vca", D, "", "" },
        { { "renaissance" }, nullptr, kFnCompressor, "", AI, "", "" },
        { { "r", "compressor" }, "waves", kFnCompressor, "", AI, "", "" },
        { { "r", "vox" },    "waves", kFnCompressor, "", AI, "", "" },
        { { "r", "bass" },   "waves", kFnSaturation, "", AI, "", "" },

        // --- EQs and preamps
        { { "meq5" },        nullptr, kFnEq, "program_eq", A, "Pultec", "Pultec MEQ-5" },
        { { "pultec" },      nullptr, kFnEq, "program_eq", A, "Pultec", "Pultec EQP-1A" },
        { { "eqp1a" },       nullptr, kFnEq, "program_eq", A, "Pultec", "Pultec EQP-1A" },
        { { "puigtec" },     nullptr, kFnEq, "program_eq", A, "Pultec", "Pultec EQP-1A" },
        { { "pe1c" },        nullptr, kFnEq, "program_eq", A, "Tube-Tech", "Tube-Tech PE 1C" },
        { { "massivepassive" }, nullptr, kFnEq, "program_eq", A, "Manley", "Manley Massive Passive" },
        { { "maag" },        nullptr, kFnEq, "program_eq", A, "Maag", "Maag EQ4" },
        { { "sitral" },      nullptr, kFnEq, "program_eq", A, "Siemens", "Siemens Sitral W295b" },
        { { "sieq" },        nullptr, kFnEq, "console", A, "Siemens", "Siemens W295b" },
        { { "hitsville" },   nullptr, kFnEq, "program_eq", A, "Motown", "Motown EQ" },
        { { "helios" },      nullptr, kFnEq | kFnPreamp, "console", A, "Helios", "Helios 69" },
        { { "trident" },     nullptr, kFnEq | kFnPreamp, "console", A, "Trident", "Trident A-Range" },
        { { "pretrida" },    nullptr, kFnEq | kFnPreamp, "console", A, "Trident", "Trident A-Range" },
        { { "tg12345" },     nullptr, kFnEq | kFnCompressor | kFnChannelStrip, "console", A, "EMI", "EMI TG12345" },
        { { "curvebender" }, nullptr, kFnEq, "program_eq", A, "EMI", "Chandler Curve Bender" },
        { { "pre1973" },     nullptr, kFnEq | kFnPreamp, "console", A, "Neve", "Neve 1073" },
        { { "1073" },        nullptr, kFnEq | kFnPreamp, "console", A, "Neve", "Neve 1073" },
        { { "scheps73" },    nullptr, kFnEq | kFnPreamp, "console", A, "Neve", "Neve 1073" },
        { { "fg73" },        nullptr, kFnEq | kFnPreamp, "console", A, "Neve", "Neve 1073" },
        { { "1084" },        nullptr, kFnEq | kFnPreamp, "console", A, "Neve", "Neve 1084" },
        { { "1081" },        nullptr, kFnEq | kFnPreamp, "console", A, "Neve", "Neve 1081" },
        { { "veq" },         nullptr, kFnEq, "console", A, "Neve", "Neve 1066" },
        { { "88rs" },        nullptr, kFnEq | kFnCompressor | kFnChannelStrip | kFnPreamp, "console", A, "Neve", "Neve 88RS" },
        { { "prev76" },      nullptr, kFnPreamp | kFnSaturation, "", A, "Telefunken", "Telefunken V76" },
        { { "vt737" },       nullptr, kFnEq | kFnCompressor | kFnPreamp | kFnChannelStrip, "opto", A, "Avalon", "Avalon VT-737sp" },
        { { "api550" },      nullptr, kFnEq, "console", A, "API", "API 550A" },
        { { "550a" },        nullptr, kFnEq, "console", A, "API", "API 550A" },
        { { "550b" },        nullptr, kFnEq, "console", A, "API", "API 550B" },
        { { "api", "550" },  nullptr, kFnEq, "console", A, "API", "API 550" },
        { { "api", "560" },  nullptr, kFnEq, "console", A, "API", "API 560" },
        { { "api", "vision" }, nullptr, kFnEq | kFnCompressor | kFnPreamp | kFnChannelStrip, "console", A, "API", "API Vision console" },
        { { "echannel" },    nullptr, kFnEq | kFnCompressor | kFnChannelStrip, "console", A, "SSL", "SSL 4000 E channel" },
        { { "gchannel" },    nullptr, kFnEq | kFnCompressor | kFnChannelStrip, "console", A, "SSL", "SSL 4000 G channel" },
        { { "ssl", "channel" }, nullptr, kFnEq | kFnCompressor | kFnChannelStrip, "console", A, "SSL", "SSL 4000 channel" },
        { { "4000e" },       nullptr, kFnEq | kFnCompressor | kFnChannelStrip, "console", A, "SSL", "SSL 4000 E channel" },
        { { "ssl", "4000" }, nullptr, kFnEq | kFnCompressor | kFnChannelStrip, "console", A, "SSL", "SSL 4000 channel" },
        { { "ssl", "9000" }, nullptr, kFnEq | kFnCompressor | kFnChannelStrip, "console", A, "SSL", "SSL 9000 channel" },
        { { "bxconsole" },   nullptr, kFnEq | kFnCompressor | kFnChannelStrip, "console", A, "SSL", "Console channel" },
        { { "virtualmixrack" }, nullptr, kFnEq | kFnCompressor | kFnPreamp | kFnChannelStrip, "console", A, "", "" },
        { { "slickeq" },     nullptr, kFnEq, "console", AI, "", "" },
        { { "tdrnova" },     nullptr, kFnEq, "dynamic", D, "", "" },
        { { "proq" },        "fabfilter", kFnEq, "parametric", D, "", "" },
        { { "proc" },        "fabfilter", kFnCompressor, "vca", D, "", "" },
        { { "prol" },        "fabfilter", kFnLimiter, "precision", D, "", "" },
        { { "promb" },       "fabfilter", kFnCompressor | kFnMultiband, "", D, "", "" },
        { { "prods" },       "fabfilter", kFnDeesser, "", D, "", "" },
        { { "prog" },        "fabfilter", kFnUtility, "", D, "", "" },
        { { "pror" },        "fabfilter", kFnReverb, "", D, "", "" },
        { { "saturn" },      "fabfilter", kFnSaturation | kFnMultiband, "", AI, "", "" },
        { { "timeless" },    "fabfilter", kFnDelay, "", D, "", "" },
        { { "q10" },         nullptr, kFnEq, "parametric", D, "", "" },
        { { "req" },         nullptr, kFnEq, "parametric", AI, "", "" },
        { { "nectar" },      nullptr, kFnEq | kFnCompressor | kFnDeesser | kFnChannelStrip, "", D, "", "" },
        { { "neutron" },     nullptr, kFnEq | kFnCompressor | kFnChannelStrip, "", D, "", "" },
        { { "ozone" },       nullptr, kFnLimiter | kFnEq | kFnMultiband, "precision", D, "", "" },

        // --- Limiters
        { { "precision", "limiter" }, nullptr, kFnLimiter, "precision", D, "", "" },
        { { "precision", "maximizer" }, nullptr, kFnLimiter | kFnSaturation, "precision", D, "", "" },
        { { "l1" },          "waves", kFnLimiter, "", D, "", "" },
        { { "l2" },          "waves", kFnLimiter, "precision", D, "", "" },
        { { "l3" },          "waves", kFnLimiter | kFnMultiband, "precision", D, "", "" },
        { { "c4" },          "waves", kFnCompressor | kFnMultiband, "", D, "", "" },
        { { "c6" },          "waves", kFnCompressor | kFnMultiband, "", D, "", "" },

        // --- Tape and saturation
        { { "j37" },         nullptr, kFnTape | kFnSaturation, "", A, "Studer", "Studer J37" },
        { { "a800" },        nullptr, kFnTape | kFnSaturation, "", A, "Studer", "Studer A800" },
        { { "studer" },      nullptr, kFnTape | kFnSaturation, "", A, "Studer", "Studer A80" },
        { { "atr102" },      nullptr, kFnTape | kFnSaturation, "", A, "Ampex", "Ampex ATR-102" },
        { { "ampex" },       nullptr, kFnTape | kFnSaturation, "", A, "Ampex", "Ampex tape machine" },
        { { "kramer", "tape" }, nullptr, kFnTape | kFnSaturation, "", A, "Kramer", "Pye/Kramer tape" },
        { { "oxide" },       nullptr, kFnTape | kFnSaturation, "", A, "", "Tape machine" },
        { { "vtm" },         nullptr, kFnTape | kFnSaturation, "", A, "", "Tape machine" },
        { { "tapemachine" }, nullptr, kFnTape | kFnSaturation, "", A, "", "Tape machine" },
        { { "decapitator" }, nullptr, kFnSaturation, "", AI, "", "" },
        { { "radiator" },    nullptr, kFnSaturation | kFnEq | kFnPreamp, "", A, "Altec", "Altec 1567A" },
        { { "nls" },         nullptr, kFnSaturation, "console", AI, "", "" },

        // --- Space and modulation
        { { "emt140" },      nullptr, kFnReverb, "", A, "EMT", "EMT 140 plate" },
        { { "plate140" },    nullptr, kFnReverb, "", A, "EMT", "EMT 140 plate" },
        { { "emt250" },      nullptr, kFnReverb, "", A, "EMT", "EMT 250" },
        { { "lexicon" },     nullptr, kFnReverb, "", A, "Lexicon", "Lexicon 224" },
        { { "capitol", "chambers" }, nullptr, kFnReverb, "", A, "Capitol", "Capitol Studios chambers" },
        { { "re201" },       nullptr, kFnDelay | kFnTape, "", A, "Roland", "Roland RE-201 Space Echo" },
        { { "spaceecho" },   nullptr, kFnDelay | kFnTape, "", A, "Roland", "Roland RE-201 Space Echo" },
        { { "tape201" },     nullptr, kFnDelay | kFnTape, "", A, "Roland", "Roland RE-201 Space Echo" },
        { { "galaxy" },      nullptr, kFnDelay | kFnTape, "", A, "Roland", "Roland RE-201 Space Echo" },
        { { "echoboy" },     nullptr, kFnDelay, "", AI, "", "" },
        { { "primaltap" },   nullptr, kFnDelay, "", A, "Lexicon", "Lexicon Prime Time" },
        { { "microshift" },  nullptr, kFnModulation, "", A, "Eventide", "Eventide H3000" },
        { { "littleplate" }, nullptr, kFnReverb, "", AI, "", "" },
        { { "vintageverb" }, nullptr, kFnReverb, "", AI, "", "" },
        { { "nvelope" },     nullptr, kFnTransient, "", A, "Elysia", "Elysia nvelope" },

        // --- Apple's built-in Audio Units (clean digital)
        { { "aunbandeq" },   nullptr, kFnEq, "", D, "", "" },
        { { "augraphiceq" }, nullptr, kFnEq, "", D, "", "" },
        { { "auhipass" },    nullptr, kFnEq, "", D, "", "" },
        { { "auhighpass" },  nullptr, kFnEq, "", D, "", "" },
        { { "aulowpass" },   nullptr, kFnEq, "", D, "", "" },
        { { "auhighshelf" }, nullptr, kFnEq, "", D, "", "" },
        { { "aulowshelf" },  nullptr, kFnEq, "", D, "", "" },
        { { "auparametriceq" }, nullptr, kFnEq, "", D, "", "" },
        { { "audynamicsprocessor" }, nullptr, kFnCompressor, "vca", D, "", "" },
        { { "aumultibandcompressor" }, nullptr, kFnCompressor | kFnMultiband, "", D, "", "" },
        { { "aupeaklimiter" }, nullptr, kFnLimiter, "", D, "", "" },
        { { "aumatrixreverb" }, nullptr, kFnReverb, "", D, "", "" },
        { { "aureverb" },    nullptr, kFnReverb, "", D, "", "" },
        { { "audelay" },     nullptr, kFnDelay, "", D, "", "" },
        { { "ausampledelay" }, nullptr, kFnUtility, "", D, "", "" },
        { { "audistortion" }, nullptr, kFnSaturation, "", D, "", "" },
    };
    return c;
}

// Hardware names that mark an analog model, and what that hardware does when
// nothing else in the name says.
struct FamilyWord { const char* word; const char* family; uint32_t defaultFunctions; const char* subtype; };

const std::vector<FamilyWord>& familyWords()
{
    static const std::vector<FamilyWord> f = {
        { "neve", "Neve", kFnEq | kFnPreamp, "console" },
        { "ssl", "SSL", kFnEq | kFnCompressor | kFnChannelStrip, "console" },
        { "api", "API", kFnEq, "console" },
        { "pultec", "Pultec", kFnEq, "program_eq" },
        { "teletronix", "LA-2A", kFnCompressor, "opto" },
        { "urei", "1176", kFnCompressor, "fet" },
        { "fairchild", "Fairchild", kFnCompressor, "vari_mu" },
        { "manley", "Manley", kFnCompressor | kFnEq, "" },
        { "studer", "Studer", kFnTape | kFnSaturation, "" },
        { "ampex", "Ampex", kFnTape | kFnSaturation, "" },
        { "emi", "EMI", kFnEq | kFnCompressor, "" },
        { "abbey", "EMI", kFnEq | kFnCompressor, "" },
        { "chandler", "EMI", kFnEq | kFnCompressor, "" },
        { "telefunken", "Telefunken", kFnPreamp, "" },
        { "tubetech", "Tube-Tech", kFnCompressor | kFnEq, "" },
        { "avalon", "Avalon", kFnPreamp | kFnEq | kFnCompressor, "" },
        { "lexicon", "Lexicon", kFnReverb, "" },
        { "emt", "EMT", kFnReverb, "" },
        { "dbx", "dbx", kFnCompressor, "vca" },
        { "focusrite", "Focusrite", kFnEq | kFnPreamp, "console" },
        { "harrison", "Harrison", kFnEq | kFnChannelStrip, "console" },
    };
    return f;
}

struct FunctionWord { const char* word; uint32_t functions; };

const std::vector<FunctionWord>& functionWords()
{
    static const std::vector<FunctionWord> f = {
        { "eq", kFnEq }, { "equalizer", kFnEq }, { "equaliser", kFnEq }, { "filter", kFnEq },
        { "comp", kFnCompressor }, { "compressor", kFnCompressor }, { "compression", kFnCompressor },
        { "leveler", kFnCompressor }, { "leveling", kFnCompressor }, { "leveller", kFnCompressor },
        { "limiter", kFnLimiter }, { "limit", kFnLimiter }, { "maximizer", kFnLimiter }, { "maximiser", kFnLimiter },
        { "clipper", kFnLimiter },
        { "saturation", kFnSaturation }, { "saturator", kFnSaturation }, { "drive", kFnSaturation },
        { "distortion", kFnSaturation }, { "overdrive", kFnSaturation }, { "tube", kFnSaturation },
        { "valve", kFnSaturation }, { "exciter", kFnSaturation }, { "harmonics", kFnSaturation },
        { "tape", kFnTape | kFnSaturation }, { "reel", kFnTape | kFnSaturation },
        { "pre", kFnPreamp }, { "preamp", kFnPreamp }, { "strip", kFnChannelStrip },
        { "console", kFnChannelStrip | kFnSaturation },
        { "deesser", kFnDeesser }, { "transient", kFnTransient },
        { "reverb", kFnReverb }, { "verb", kFnReverb }, { "plate", kFnReverb }, { "hall", kFnReverb },
        { "chamber", kFnReverb }, { "spring", kFnReverb },
        { "delay", kFnDelay }, { "echo", kFnDelay },
        { "chorus", kFnModulation }, { "flanger", kFnModulation }, { "phaser", kFnModulation },
        { "tremolo", kFnModulation }, { "vibrato", kFnModulation }, { "doubler", kFnModulation },
        { "ensemble", kFnModulation }, { "rotary", kFnModulation },
        { "multiband", kFnMultiband | kFnCompressor },
        { "gain", kFnUtility }, { "utility", kFnUtility }, { "meter", kFnUtility }, { "analyzer", kFnUtility },
        { "tuner", kFnUtility }, { "gate", kFnUtility }, { "expander", kFnUtility }, { "trim", kFnUtility },
        { "imager", kFnUtility },
    };
    return f;
}

const char* const colourWords[] = { "vintage", "analog", "analogue", "warm", "tube", "valve", "tape", "console",
                                    "saturation", "saturator", "drive", "retro", "classic", "colour", "color" };

void applyCatalog (PluginInfo& p, const CatalogEntry& e)
{
    p.functions = e.functions;
    p.subtype = e.subtype;
    p.character = e.character;
    p.family = e.family;
    p.emulates.clear();
    if (e.emulates != nullptr && *e.emulates != 0)
        p.emulates.push_back (e.emulates);
    p.tagSource = TagSource::Catalog;
    p.tagConfidence = 0.95f;
}

uint32_t functionsFromCategory (const std::string& category)
{
    const auto c = lower (category);
    uint32_t f = 0;
    auto has = [&c] (const char* w) { return c.find (w) != std::string::npos; };
    if (has ("eq") || has ("filter"))  f |= kFnEq;
    if (has ("dynamics") || has ("compressor")) f |= kFnCompressor;
    if (has ("limiter") || has ("mastering")) f |= kFnLimiter;
    if (has ("distortion")) f |= kFnSaturation;
    if (has ("reverb")) f |= kFnReverb;
    if (has ("delay")) f |= kFnDelay;
    if (has ("modulation")) f |= kFnModulation;
    if (has ("analyzer") || has ("tools") || has ("restoration") || has ("spatial")) f |= kFnUtility;
    return f;
}
} // namespace

std::vector<std::string> nameTokens (const std::string& s)
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&] { if (! cur.empty()) { out.push_back (lower (cur)); cur.clear(); } };
    for (size_t i = 0; i < s.size(); ++i)
    {
        const unsigned char c = (unsigned char) s[i];
        if (! std::isalnum (c)) { flush(); continue; }
        if (! cur.empty())
        {
            const unsigned char prev = (unsigned char) cur.back();
            const bool digitEdge = (std::isdigit (c) != 0) != (std::isdigit (prev) != 0);
            // "ChannelEQ" -> channel, eq (a capital after a lower-case letter starts a word)
            const bool camel = std::isupper (c) && std::islower (prev);
            if (digitEdge || camel)
                flush();
        }
        cur += (char) c;
    }
    flush();
    return out;
}

void tagPlugin (PluginInfo& p)
{
    const Words name (p.name);
    const auto vendor = squash (p.vendor);

    // 1. Catalog of well-known plugins.
    for (const auto& e : catalog())
    {
        if (e.vendor != nullptr && vendor.find (e.vendor) == std::string::npos)
            continue;
        if (std::all_of (e.patterns.begin(), e.patterns.end(), [&] (const char* pat) { return name.has (pat); }))
        {
            applyCatalog (p, e);
            return;
        }
    }

    p.functions = 0;
    p.subtype.clear();
    p.family.clear();
    p.emulates.clear();
    p.character = PluginCharacter::Digital;
    p.tagSource = TagSource::Keywords;
    p.tagConfidence = 0.6f;

    // 2. What it does, from the words in its name.
    for (const auto& w : functionWords())
        if (name.hasToken (w.word))
            p.functions |= w.functions;
    if (name.squashed.find ("deess") != std::string::npos) p.functions |= kFnDeesser;
    if (name.squashed.find ("channelstrip") != std::string::npos) p.functions |= kFnChannelStrip;

    if (name.hasToken ("opto")) p.subtype = "opto";
    else if (name.hasToken ("fet")) p.subtype = "fet";
    else if (name.hasToken ("vca")) p.subtype = "vca";
    else if (name.squashed.find ("varimu") != std::string::npos || name.hasToken ("mu")) p.subtype = "vari_mu";

    // 3. Hardware words: an analog model.
    for (const auto& f : familyWords())
    {
        if (name.hasToken (f.word) || vendor == f.word)
        {
            p.family = f.family;
            p.character = PluginCharacter::Analog;
            p.tagConfidence = 0.8f;
            if ((p.functions & ~(uint32_t) kFnUtility) == 0)
                p.functions |= f.defaultFunctions;
            if (p.subtype.empty())
                p.subtype = f.subtype;
            break;
        }
    }

    // 4. Analog colour without a specific unit.
    if (p.character == PluginCharacter::Digital)
        for (const char* w : colourWords)
            if (name.hasToken (w))
            {
                p.character = PluginCharacter::AnalogInspired;
                break;
            }

    // 5. The plugin's own category.
    if (p.functions == 0)
    {
        p.functions = functionsFromCategory (p.reportedCategory);
        p.tagSource = TagSource::Category;
        p.tagConfidence = p.functions != 0 ? 0.4f : 0.2f;
    }
    if (p.does (kFnChannelStrip))
        p.functions |= kFnEq | kFnCompressor;
    if (p.does (kFnChannelStrip) && p.subtype.empty())
        p.subtype = "console";
}

const char* toString (PluginCharacter c) noexcept
{
    switch (c)
    {
        case PluginCharacter::Analog:         return "analog";
        case PluginCharacter::AnalogInspired: return "analog_inspired";
        case PluginCharacter::Digital:        return "digital";
    }
    return "digital";
}

PluginCharacter characterFromString (const std::string& s) noexcept
{
    if (s == "analog") return PluginCharacter::Analog;
    if (s == "analog_inspired") return PluginCharacter::AnalogInspired;
    return PluginCharacter::Digital;
}

const char* toString (TagSource t) noexcept
{
    switch (t)
    {
        case TagSource::Catalog:  return "catalog";
        case TagSource::Keywords: return "keywords";
        case TagSource::Category: return "category";
        case TagSource::User:     return "you";
    }
    return "category";
}

const char* functionName (PluginFunction f) noexcept
{
    switch (f)
    {
        case kFnEq:           return "eq";
        case kFnCompressor:   return "compressor";
        case kFnLimiter:      return "limiter";
        case kFnSaturation:   return "saturation";
        case kFnTape:         return "tape";
        case kFnPreamp:       return "preamp";
        case kFnChannelStrip: return "channel_strip";
        case kFnDeesser:      return "deesser";
        case kFnTransient:    return "transient";
        case kFnReverb:       return "reverb";
        case kFnDelay:        return "delay";
        case kFnModulation:   return "modulation";
        case kFnMultiband:    return "multiband";
        case kFnUtility:      return "utility";
    }
    return "";
}

std::string functionsToString (uint32_t functions)
{
    std::string out;
    for (int i = 0; i < kNumPluginFunctions; ++i)
        if (functions & (1u << i))
        {
            if (! out.empty()) out += ',';
            out += functionName ((PluginFunction) (1u << i));
        }
    return out;
}

uint32_t functionsFromString (const std::string& s)
{
    uint32_t f = 0;
    size_t start = 0;
    while (start <= s.size())
    {
        const auto end = std::min (s.find (',', start), s.size());
        const auto word = s.substr (start, end - start);
        for (int i = 0; i < kNumPluginFunctions; ++i)
            if (word == functionName ((PluginFunction) (1u << i)))
                f |= 1u << i;
        start = end + 1;
    }
    return f;
}

} // namespace aimix
