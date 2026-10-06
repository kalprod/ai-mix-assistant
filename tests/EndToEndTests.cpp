#include "TestFramework.h"
#include "aimix/SyntheticSession.h"

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <fstream>
#include <map>
#include <set>
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
    auto second = std::make_unique<MixEngine> (run.bus);   // ~4.5 MB: too big for a 1 MB Windows stack
    second->tick (monotonicMillis() + 10);   // first engine heart-beat recently (in virtual time)
    auto r = second->getLatestReport();
    CHECK (r->tracks.empty() || ! r->isActiveMaster);
}

TEST_CASE ("e2e: engine thread runs and publishes reports")
{
    auto bus = SharedBus::open ("aimix_t_thread", SharedBus::Backend::ProcessLocal);
    auto engine = std::make_unique<MixEngine> (bus);
    engine->start (10);
    for (int i = 0; i < 200 && engine->getLatestReport()->tick < 3; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (5));
    engine->stop();
    CHECK (engine->getLatestReport()->tick >= 3);
    CHECK (engine->ownsBus());
}

TEST_CASE ("e2e: Auto role tracks are detected by the master engine and reported back to the Listener")
{
    SessionOptions options;
    options.autoRoles = true;
    auto run = runSyntheticSession (8.0, 512, {}, 0, options);
    auto report = run.engine->getLatestReport();
    REQUIRE (report != nullptr);

    auto roleOf = [&] (const std::string& name) -> const TrackSummary*
    {
        for (auto& t : report->tracks)
            if (t.view.name == name) return &t;
        return nullptr;
    };
    for (const auto& [name, expected] : std::vector<std::pair<std::string, TrackRole>> {
             { "Kick", TrackRole::Kick }, { "Bass", TrackRole::Bass }, { "OH", TrackRole::Drums } })
    {
        auto* t = roleOf (name);
        REQUIRE (t != nullptr);
        CHECK (t->autoRole);
        CHECK (! t->roleDetecting);
        CHECK (t->view.role == expected);

        // The Listener can read the guess from its bus slot.
        const auto slotValue = run.bus->slot ((int) t->view.trackId).detectedRole.load();
        CHECK ((slotValue & kDetectedRoleValid) != 0);
        CHECK ((TrackRole) (slotValue & 0xff) == expected);
    }
}

TEST_CASE ("e2e: master bus only gives steady, useful cards on a full mix")
{
    SessionOptions options;
    options.masterOnly = true;
    std::map<std::string, int> changes;
    std::set<std::string> last;
    auto run = runSyntheticSession (20.0, 512, {}, 0, options, [&] (const MixEngine& engine)
    {
        std::set<std::string> now;
        for (auto& s : engine.getLatestReport()->suggestions)
            now.insert (s.key);
        for (auto& k : now)  if (! last.count (k)) changes[k]++;
        for (auto& k : last) if (! now.count (k))  changes[k]++;
        last = now;
    });

    auto report = run.engine->getLatestReport();
    CHECK (report->tracks.empty());
    CHECK (report->hasMaster);
    CHECK (! report->suggestions.empty());
    std::printf ("    %zu cards from the mix bus alone:\n", report->suggestions.size());
    for (auto& s : report->suggestions)
        std::printf ("      %-18s %s  ->  %s\n", s.ruleId.c_str(), s.title.c_str(), describeAction (s.action).c_str());

    for (auto& [key, n] : changes)
    {
        if (n > 1) std::printf ("      %s appeared/disappeared %d times\n", key.c_str(), n);
        CHECK (n == 1);   // each card appears once and stays
    }
}
