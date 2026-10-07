#include "TestFramework.h"
#include "aimix/ChainRecommender.h"
#include "aimix/PluginLibrary.h"

#include <algorithm>

using namespace aimix;

namespace
{
PluginInfo plug (const char* name, const char* vendor, const char* format = "AU", const char* category = "")
{
    PluginInfo p;
    p.name = name;
    p.vendor = vendor;
    p.format = format;
    p.id = std::string (format) + ":" + vendor + "." + name;
    p.reportedCategory = category;
    tagPlugin (p);
    return p;
}

const ChainSlot* slot (const ChainRecommendation& r, const char* id)
{
    auto it = std::find_if (r.slots.begin(), r.slots.end(), [&] (const ChainSlot& s) { return s.id == id; });
    return it == r.slots.end() ? nullptr : &*it;
}

std::vector<PluginInfo> studioLibrary()
{
    return {
        plug ("Pro-Q 3", "FabFilter", "VST3", "Fx|EQ"),
        plug ("Pro-L 2", "FabFilter", "VST3", "Fx|Dynamics"),
        plug ("Pro-C 2", "FabFilter", "VST3", "Fx|Dynamics"),
        plug ("UADx Pultec EQP-1A", "Universal Audio", "AU"),
        plug ("UADx 1176 Rev E", "Universal Audio", "AU"),
        plug ("UADx LA-2A Gray", "Universal Audio", "AU"),
        plug ("UADx SSL G Bus Compressor", "Universal Audio", "AU"),
        plug ("Pre 1973", "Arturia", "VST3"),
        plug ("J37 Tape", "Waves", "VST3"),
        plug ("AUNBandEQ", "Apple", "AU", "EQ"),
        plug ("AUPeakLimiter", "Apple", "AU", "Effect"),
        plug ("Rev PLATE-140", "Arturia", "VST3"),
        plug ("Pro-DS", "FabFilter", "VST3", "Fx|Dynamics"),
    };
}
}

TEST_CASE ("plugin library: name tokens split camel case and digits")
{
    const auto t = nameTokens ("UADx Pultec EQP-1A");
    CHECK (t.size() == 5);
    CHECK (t[0] == "uadx");
    CHECK (t[1] == "pultec");
    CHECK (t[2] == "eqp");
    CHECK (t[3] == "1");
    CHECK (t[4] == "a");
    const auto c = nameTokens ("ChannelEQ");
    CHECK (c.size() == 2 && c[0] == "channel" && c[1] == "eq");
}

TEST_CASE ("plugin library: well-known hardware models are tagged analog with their family")
{
    const auto pultec = plug ("UADx Pultec EQP-1A", "Universal Audio");
    CHECK (pultec.character == PluginCharacter::Analog);
    CHECK (pultec.family == "Pultec");
    CHECK (pultec.does (kFnEq));
    CHECK (pultec.subtype == "program_eq");
    CHECK (pultec.tagSource == TagSource::Catalog);

    const auto la2a = plug ("CLA-2A", "Waves");
    CHECK (la2a.family == "LA-2A" && la2a.subtype == "opto" && la2a.does (kFnCompressor));

    const auto fet = plug ("Comp FET-76", "Arturia");
    CHECK (fet.family == "1176" && fet.subtype == "fet");

    const auto neve = plug ("Pre 1973", "Arturia");
    CHECK (neve.family == "Neve" && neve.does (kFnEq) && neve.does (kFnPreamp));

    const auto tape = plug ("J37 Tape", "Waves");
    CHECK (tape.family == "Studer" && tape.does (kFnTape));

    const auto bus = plug ("SSL G-Master Buss Compressor", "Waves");
    CHECK (bus.family == "SSL" && bus.does (kFnCompressor) && ! bus.does (kFnEq));
}

TEST_CASE ("plugin library: clean digital plugins stay digital")
{
    const auto q = plug ("Pro-Q 3", "FabFilter");
    CHECK (q.character == PluginCharacter::Digital && q.does (kFnEq) && q.subtype == "parametric");
    const auto l = plug ("Pro-L 2", "FabFilter");
    CHECK (l.character == PluginCharacter::Digital && l.does (kFnLimiter));
    const auto apple = plug ("AUNBandEQ", "Apple");
    CHECK (apple.character == PluginCharacter::Digital && apple.does (kFnEq));
}

TEST_CASE ("plugin library: unknown plugins fall back to name words, then their category")
{
    const auto warm = plug ("Warm Vintage Compressor", "Someone");
    CHECK (warm.does (kFnCompressor));
    CHECK (warm.character == PluginCharacter::AnalogInspired);
    CHECK (warm.tagSource == TagSource::Keywords);

    const auto neveish = plug ("Neve Style Pre", "Someone");
    CHECK (neveish.character == PluginCharacter::Analog && neveish.family == "Neve");

    const auto cat = plug ("Zorblax", "Someone", "VST3", "Fx|Reverb");
    CHECK (cat.does (kFnReverb) && cat.tagSource == TagSource::Category);

    const auto none = plug ("Zorblax", "Someone", "VST3", "");
    CHECK (none.functions == 0);
}

TEST_CASE ("plugin library: function lists round-trip through text")
{
    const uint32_t f = kFnEq | kFnCompressor | kFnTape;
    CHECK (functionsToString (f) == "eq,compressor,tape");
    CHECK (functionsFromString ("eq,compressor,tape") == f);
    CHECK (characterFromString (toString (PluginCharacter::AnalogInspired)) == PluginCharacter::AnalogInspired);
}

TEST_CASE ("chains: a vocal chain puts analog models first and a clean EQ for the clean-up")
{
    const auto lib = studioLibrary();
    ChainContext ctx;
    ctx.role = TrackRole::Vocal;
    const auto r = recommendChain (lib, ctx);

    const auto* cleanup = slot (r, "cleanup_eq");
    CHECK (cleanup != nullptr && cleanup->pick >= 0);
    CHECK (lib[(size_t) cleanup->pick].character == PluginCharacter::Digital);

    const auto* character = slot (r, "character_eq");
    CHECK (character != nullptr && character->pick >= 0);
    CHECK (lib[(size_t) character->pick].family == "Neve");

    const auto* fast = slot (r, "compressor");
    CHECK (fast != nullptr && fast->pick >= 0 && lib[(size_t) fast->pick].family == "1176");
    const auto* smooth = slot (r, "compressor2");
    CHECK (smooth != nullptr && smooth->pick >= 0 && lib[(size_t) smooth->pick].family == "LA-2A");

    const auto* space = slot (r, "space");
    CHECK (space != nullptr && space->pick >= 0 && lib[(size_t) space->pick].family == "EMT");

    CHECK (r.analogCount >= 4);
    CHECK (r.missingCount == 0);
    for (const auto& s : r.slots)
        CHECK (s.knobs.size() >= 2 && s.knobs.size() <= 4 && ! s.preset.empty());
}

TEST_CASE ("chains: the master chain is bus comp, program EQ, tape, then a clean limiter")
{
    const auto lib = studioLibrary();
    ChainContext ctx;
    ctx.master = true;
    ctx.role = TrackRole::MasterBus;
    const auto r = recommendChain (lib, ctx);
    CHECK (r.slots.size() == 4);
    CHECK (r.slots[0].id == "bus_comp" && lib[(size_t) r.slots[0].pick].family == "SSL");
    CHECK (r.slots[1].id == "program_eq" && lib[(size_t) r.slots[1].pick].family == "Pultec");
    CHECK (r.slots[2].id == "tape" && lib[(size_t) r.slots[2].pick].family == "Studer");
    CHECK (r.slots[3].id == "limiter" && lib[(size_t) r.slots[3].pick].name == "Pro-L 2");
}

TEST_CASE ("chains: each plugin is used once and missing slots are counted")
{
    std::vector<PluginInfo> lib = { plug ("UADx 1176 Rev E", "Universal Audio", "AU"),
                                    plug ("UADx 1176 Rev E", "Universal Audio", "VST3") };
    ChainContext ctx;
    ctx.role = TrackRole::Vocal;
    const auto r = recommendChain (lib, ctx);
    int picks = 0;
    for (const auto& s : r.slots)
        if (s.pick >= 0)
        {
            ++picks;
            CHECK (s.alternatives.empty());   // the VST3 copy of the same plugin is not an alternative
        }
    CHECK (picks == 1);
    CHECK (r.missingCount == 2);   // clean-up EQ and character EQ
}

TEST_CASE ("chains: knob starting points follow the measurements")
{
    const std::vector<PluginInfo> lib = studioLibrary();
    ChainContext ctx;
    ctx.role = TrackRole::Vocal;
    ctx.measured = true;
    ctx.levelSwingLu = 6.0f;
    auto r = recommendChain (lib, ctx);
    const auto* comp = slot (r, "compressor");
    CHECK (comp != nullptr && comp->knobs[0].setting.find ("5-8 dB") != std::string::npos);

    ctx.role = TrackRole::Drums;
    ctx.crestDb = 20.0f;
    r = recommendChain (lib, ctx);
    comp = slot (r, "compressor");
    CHECK (comp != nullptr && comp->knobs[2].setting.find ("parallel") != std::string::npos);

    ctx.master = true;
    ctx.crestDb = 5.0f;
    r = recommendChain (lib, ctx);
    CHECK (r.slots[0].knobs[0].setting.find ("already squashed") != std::string::npos);
}

TEST_CASE ("plugin library: an EQ named 'Channel EQ' is not a channel strip")
{
    const auto p = plug ("ChannelEQ", "Someone", "AU", "EQ");
    CHECK (p.does (kFnEq));
    CHECK (! p.does (kFnCompressor));
    const auto strip = plug ("Vintage Channel Strip", "Someone");
    CHECK (strip.does (kFnChannelStrip) && strip.does (kFnCompressor) && strip.does (kFnEq));
}
