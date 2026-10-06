#include "TestFramework.h"
#include "aimix/SyntheticSession.h"

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <thread>

using namespace aimix;

namespace
{
const Suggestion* find (const MixReport& r, const std::string& rule, const std::string& trackName = {})
{
    for (auto& s : r.suggestions)
        if (s.ruleId == rule
            && (trackName.empty() || std::find (s.trackNames.begin(), s.trackNames.end(), trackName) != s.trackNames.end()))
            return &s;
    return nullptr;
}
}

TEST_CASE ("e2e: synthetic session -> listeners -> bus -> master engine -> expected diagnoses")
{
    auto run = runSyntheticSession (10.0);
    auto report = run.engine->getLatestReport();
    REQUIRE (report != nullptr);

    CHECK (report->isActiveMaster);
    CHECK (report->tracks.size() == 9);
    CHECK (report->hasMaster);

    std::printf ("    %zu suggestions after %.0f s of audio:\n", report->suggestions.size(), run.audioSeconds);
    for (auto& s : report->suggestions)
        std::printf ("      [%-8s] %-24s %s  ->  %s\n", toString (s.severity), s.ruleId.c_str(), s.title.c_str(), describeAction (s.action).c_str());

    auto* clip = find (*report, "gain.clipping", "Synth Lead");
    CHECK (clip != nullptr);

    auto* align = find (*report, "phase.time_align", "Room");
    REQUIRE (align != nullptr);
    CHECK_NEAR (align->action.delaySamples, SyntheticSession::kRoomDelaySamples, 1.0);
    CHECK (align->trackNames[0] == "OH");   // the earlier mic gets delayed

    CHECK (find (*report, "phase.track_correlation", "Pad") != nullptr);
    CHECK (find (*report, "eq.low_end", "Lead Vox") != nullptr);
    CHECK (find (*report, "pan.wide_low_end", "Bass") != nullptr);

    auto* mask = find (*report, "eq.masking", "Keys");
    REQUIRE (mask != nullptr);
    CHECK (std::find (mask->trackNames.begin(), mask->trackNames.end(), "Guitar") != mask->trackNames.end());
    CHECK (find (*report, "pan.separate", "Guitar") != nullptr);

    // Kick is clean and must not be blamed for anything on its own.
    for (auto& s : report->suggestions)
        if (s.trackNames.size() == 1)
            CHECK (s.trackNames[0] != "Kick");

    // Machine-readable output for the UI / logs.
    std::ofstream ("e2e_suggestions.json") << toJson (report->suggestions);

    // The master's own analyser fed the mix-bus summary.
    CHECK (report->master.view.integratedLufs > -40.0f);
}

TEST_CASE ("e2e: second master engine on the same bus stays passive")
{
    auto run = runSyntheticSession (1.0);
    MixEngine second (run.bus);
    second.tick (monotonicMillis() + 10);   // first engine heart-beat recently (in virtual time)
    auto r = second.getLatestReport();
    CHECK (r->tracks.empty() || ! r->isActiveMaster);
}

TEST_CASE ("e2e: engine thread runs and publishes reports")
{
    auto bus = SharedBus::open ("aimix_t_thread", SharedBus::Backend::ProcessLocal);
    MixEngine engine (bus);
    engine.start (10);
    for (int i = 0; i < 200 && engine.getLatestReport()->tick < 3; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    engine.stop();
    CHECK (engine.getLatestReport()->tick >= 3);
    CHECK (engine.ownsBus());
}
