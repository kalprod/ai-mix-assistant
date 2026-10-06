#include "TestFramework.h"
#include "aimix/RuleEngine.h"

#include <algorithm>

using namespace aimix;

namespace
{
TrackView track (uint32_t id, const char* name, TrackRole role)
{
    TrackView t;
    t.trackId = id;
    t.name = name;
    t.role = role;
    t.active = true;
    t.rmsDb[0] = t.rmsDb[1] = -20.0f;
    t.peakDb[0] = t.peakDb[1] = -8.0f;
    t.shortTermLufs = t.momentaryLufs = t.integratedLufs = -22.0f;
    t.correlation = 0.9f;
    t.sideToMidDb = -30.0f;
    // gentle pink-ish default profile, mid-focused
    for (int b = 0; b < kNumBands; ++b)
        t.bands.powerDb[(size_t) b] = -30.0f - 1.0f * (float) std::abs (b - 10);
    t.bands.peakBandDb = -30.0f;
    t.bands.totalPowerDb = -20.0f;
    return t;
}

const Suggestion* find (const std::vector<Suggestion>& list, const std::string& rule)
{
    auto it = std::find_if (list.begin(), list.end(), [&] (const Suggestion& s) { return s.ruleId == rule; });
    return it == list.end() ? nullptr : &*it;
}
}

TEST_CASE ("rules: clipping track gets a critical gain cut that lands at -1 dBFS")
{
    MixSnapshot mix;
    auto t = track (1, "Synth", TrackRole::Synth);
    t.peakDb[0] = 0.8f;
    mix.tracks.push_back (t);

    auto out = RuleEngine::evaluateRaw (mix, {});
    auto* s = find (out, "gain.clipping");
    REQUIRE (s != nullptr);
    CHECK (s->severity == Severity::Critical);
    CHECK (s->action.type == ActionType::AdjustGain);
    CHECK_NEAR (s->action.gainDb, -1.8, 1e-4);
    CHECK (! s->steps.empty());
    CHECK (find (out, "gain.hot") == nullptr);   // clipping supersedes "hot"
}

TEST_CASE ("rules: masking cut goes on the lower-priority track at the clashing band")
{
    MixSnapshot mix;
    mix.tracks = { track (1, "Vox", TrackRole::Vocal), track (2, "Gtr", TrackRole::Guitar) };
    PairView p;
    p.trackA = 1; p.trackB = 2;
    p.masking.score = 0.7f;
    p.masking.worstBand = 15;   // 2.7 - 3.15 kHz
    p.masking.levelDifferenceDb = 2.0f;
    mix.pairs.push_back (p);

    auto out = RuleEngine::evaluateRaw (mix, {});
    auto* s = find (out, "eq.masking");
    REQUIRE (s != nullptr);
    CHECK (s->action.type == ActionType::EqBell);
    CHECK (s->action.trackId == 2);   // guitar yields to the vocal
    CHECK (s->action.gainDb < -1.0f && s->action.gainDb >= -6.0f);
    CHECK_NEAR (s->action.frequencyHz, bandCentreHz (15), 1e-3);
    CHECK (s->severity == Severity::Warning);
    CHECK (find (out, "pan.separate") == nullptr);   // vocals are not panned away
}

TEST_CASE ("rules: two centred mono instruments that mask get a pan suggestion")
{
    MixSnapshot mix;
    mix.tracks = { track (1, "Gtr", TrackRole::Guitar), track (2, "Keys", TrackRole::Keys) };
    PairView p;
    p.trackA = 1; p.trackB = 2;
    p.masking.score = 0.6f;
    p.masking.worstBand = 8;
    mix.pairs.push_back (p);
    auto out = RuleEngine::evaluateRaw (mix, {});
    auto* s = find (out, "pan.separate");
    REQUIRE (s != nullptr);
    CHECK (s->category == Category::Panning);
    CHECK (s->action.pan < 0.0f);
}

TEST_CASE ("rules: stable inter-track delay produces a time-align action on the earlier track")
{
    MixSnapshot mix;
    mix.tracks = { track (4, "OH", TrackRole::Drums), track (5, "Room", TrackRole::Drums) };
    mix.tracks[0].sampleRate = mix.tracks[1].sampleRate = 48000.0;
    PairView p;
    p.trackA = 4; p.trackB = 5;
    p.delayValid = true;
    p.delaySamples = 37.0f;     // Room arrives later
    p.delayCorrelation = 0.85f;
    mix.pairs.push_back (p);

    auto out = RuleEngine::evaluateRaw (mix, {});
    auto* s = find (out, "phase.time_align");
    REQUIRE (s != nullptr);
    CHECK (s->action.type == ActionType::TimeAlign);
    CHECK (s->action.trackId == 4);
    CHECK_NEAR (s->action.delaySamples, 37.0, 1e-4);
    CHECK_NEAR (s->action.delayMs, 37.0 / 48.0, 1e-3);
    CHECK (s->severity == Severity::Critical);

    mix.pairs[0].delaySamples = -20.0f;   // now OH is the late one
    out = RuleEngine::evaluateRaw (mix, {});
    CHECK (find (out, "phase.time_align")->action.trackId == 5);
}

TEST_CASE ("rules: aligned but anti-correlated pair -> polarity flip")
{
    MixSnapshot mix;
    mix.tracks = { track (1, "Kick In", TrackRole::Kick), track (2, "Kick Out", TrackRole::Drums) };
    PairView p;
    p.trackA = 1; p.trackB = 2;
    p.delayValid = true;
    p.delaySamples = 0.2f;
    p.delayCorrelation = -0.9f;
    mix.pairs.push_back (p);
    const auto out = RuleEngine::evaluateRaw (mix, {});
    auto* s = find (out, "phase.polarity");
    REQUIRE (s != nullptr);
    CHECK (s->action.type == ActionType::InvertPolarity);
    CHECK (s->action.trackId == 2);   // lower priority track gets flipped
}

TEST_CASE ("rules: negative stereo correlation and wide low end on a single track")
{
    MixSnapshot mix;
    auto pad = track (1, "Pad", TrackRole::Synth);
    pad.correlation = -0.7f;
    pad.sideToMidDb = 5.0f;
    auto bass = track (2, "Bass", TrackRole::Bass);
    bass.sideToMidDb = -6.0f;
    for (int b = 0; b < kNumBands; ++b) bass.bands.powerDb[(size_t) b] = b < 2 ? -10.0f : -50.0f;
    bass.bands.peakBandDb = -10.0f;
    mix.tracks = { pad, bass };

    auto out = RuleEngine::evaluateRaw (mix, {});
    auto* c = find (out, "phase.track_correlation");
    REQUIRE (c != nullptr);
    CHECK (c->severity == Severity::Critical);
    auto* w = find (out, "pan.wide_low_end");
    REQUIRE (w != nullptr);
    CHECK (w->trackIds[0] == 2);
    CHECK (w->severity == Severity::Warning);
}

TEST_CASE ("rules: inactive tracks produce nothing")
{
    MixSnapshot mix;
    auto t = track (1, "Muted", TrackRole::Synth);
    t.active = false;
    t.peakDb[0] = 3.0f;
    t.correlation = -1.0f;
    mix.tracks.push_back (t);
    CHECK (RuleEngine::evaluateRaw (mix, {}).empty());
}

TEST_CASE ("rules: debounce, hold and dismissal")
{
    RuleConfig cfg;
    cfg.debounceTicks = 3;
    cfg.holdTicks = 2;
    RuleEngine engine (cfg);

    MixSnapshot bad;
    auto t = track (1, "Synth", TrackRole::Synth);
    t.peakDb[0] = 1.0f;
    bad.tracks.push_back (t);
    MixSnapshot good;
    good.tracks.push_back (track (1, "Synth", TrackRole::Synth));

    CHECK (engine.evaluate (bad).empty());
    CHECK (engine.evaluate (bad).empty());
    CHECK (engine.evaluate (bad).size() == 1);           // shown on 3rd consecutive hit
    CHECK (engine.evaluate (good).size() == 1);          // held
    CHECK (engine.evaluate (good).size() == 1);
    CHECK (engine.evaluate (good).empty());              // released after hold

    for (int i = 0; i < 3; ++i) engine.evaluate (bad);
    auto shown = engine.evaluate (bad);
    REQUIRE (shown.size() == 1);
    CHECK (shown[0].ageTicks >= 2);
    engine.dismiss (shown[0].key);
    CHECK (engine.evaluate (bad).empty());
}

TEST_CASE ("rules: results sorted by severity, JSON output escapes names")
{
    MixSnapshot mix;
    auto a = track (1, "Say \"hi\"", TrackRole::Synth);
    a.peakDb[0] = 0.5f;                       // critical
    auto b = track (2, "Quiet", TrackRole::Synth);
    b.shortTermLufs = -55.0f;                 // info
    mix.tracks = { b, a };

    RuleConfig cfg;
    cfg.debounceTicks = 1;
    RuleEngine engine (cfg);
    auto out = engine.evaluate (mix);
    REQUIRE (out.size() >= 2);
    CHECK (out.front().severity == Severity::Critical);
    CHECK (out.back().severity == Severity::Info);

    const auto json = toJson (out);
    CHECK (json.front() == '[' && json.back() == ']');
    CHECK (json.find ("Say \\\"hi\\\"") != std::string::npos);
    CHECK (json.find ("\"severity\":\"critical\"") != std::string::npos);
    CHECK (json.find ("\"type\":\"adjust_gain\"") != std::string::npos);
}
