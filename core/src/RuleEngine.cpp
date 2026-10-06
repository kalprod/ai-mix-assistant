#include "aimix/RuleEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <unordered_map>

namespace aimix
{
namespace
{
std::string fmt (const char* format, ...)
{
    char buf[512];
    va_list args;
    va_start (args, format);
    std::vsnprintf (buf, sizeof (buf), format, args);
    va_end (args);
    return buf;
}

const char* q (const std::string& name) { return name.c_str(); }

bool isLowEndRole (TrackRole r) noexcept { return r == TrackRole::Kick || r == TrackRole::Bass; }

bool isPannable (TrackRole r) noexcept
{
    switch (r)
    {
        case TrackRole::Guitar: case TrackRole::Keys: case TrackRole::Synth:
        case TrackRole::Fx: case TrackRole::Unknown:
            return true;
        default:
            return false;
    }
}

float highPassFrequencyFor (TrackRole r) noexcept
{
    switch (r)
    {
        case TrackRole::Vocal:  return 100.0f;
        case TrackRole::Guitar: return 90.0f;
        case TrackRole::Snare:  return 80.0f;
        case TrackRole::Keys:   return 70.0f;
        case TrackRole::Fx:     return 150.0f;
        default:                return 60.0f;
    }
}

double bandShare (const BandProfile& p, int band)
{
    double total = 0.0;
    for (float db : p.powerDb)
        total += std::pow (10.0, db / 10.0);
    return total > 0.0 ? std::pow (10.0, p.powerDb[(size_t) band] / 10.0) / total : 0.0;
}

// Residual of each band above a straight-line fit of level vs. log-frequency
// across the active bands: a cheap "is this region sticking out of the
// track's own tonal balance" measure, independent of overall level.
std::array<float, kNumBands> spectralResidual (const BandProfile& p)
{
    std::array<float, kNumBands> residual {};
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = 0;
    for (int b = 0; b < kNumBands; ++b)
    {
        if (p.powerDb[(size_t) b] < p.peakBandDb - 40.0f)
            continue;
        const double x = std::log2 (bandCentreHz (b));
        const double y = p.powerDb[(size_t) b];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
        ++n;
    }
    if (n < 4)
        return residual;

    const double slope = (n * sxy - sx * sy) / std::max (1.0e-9, n * sxx - sx * sx);
    const double intercept = (sy - slope * sx) / n;
    for (int b = 0; b < kNumBands; ++b)
        residual[(size_t) b] = p.powerDb[(size_t) b] < p.peakBandDb - 40.0f
                                 ? 0.0f
                                 : (float) (p.powerDb[(size_t) b] - (intercept + slope * std::log2 (bandCentreHz (b))));
    return residual;
}

Suggestion make (std::string ruleId, std::string keySuffix, Category c, Severity s, float confidence,
                 std::initializer_list<const TrackView*> tracks)
{
    Suggestion out;
    out.ruleId = std::move (ruleId);
    out.key = out.ruleId + ":" + keySuffix;
    out.category = c;
    out.severity = s;
    out.confidence = std::clamp (confidence, 0.0f, 1.0f);
    for (auto* t : tracks)
    {
        out.trackIds.push_back (t->trackId);
        out.trackNames.push_back (t->name);
    }
    return out;
}

std::string idKey (const TrackView& t) { return std::to_string (t.trackId); }

//==============================================================================
// GAIN
void gainRules (const TrackView& t, const MixSnapshot& mix, const RuleConfig& cfg, std::vector<Suggestion>& out)
{
    if (! t.active)
        return;

    const float peak = t.maxPeakDb();
    const float rms = t.maxRmsDb();

    if (peak >= cfg.clipPeakDb)
    {
        auto s = make ("gain.clipping", idKey (t), Category::Gain, Severity::Critical, 0.95f, { &t });
        const float trim = -(peak - cfg.masterCeilingDb);
        s.title = fmt ("'%s' is clipping", q (t.name));
        s.detail = fmt ("Sample peaks reach %+.1f dBFS. Anything at or above 0 dBFS distorts on export and in any fixed-point stage.", peak);
        s.steps = {
            fmt ("Lower the clip gain or input trim of '%s' by %.1f dB, before any plugins.", q (t.name), -trim),
            "If the peaks come from a plugin in the chain, lower that plugin's output instead.",
            "Re-check the peak meter: it should now stay below -1 dBFS.",
        };
        s.action = { ActionType::AdjustGain, t.trackId, trim };
        out.push_back (std::move (s));
    }
    else if (rms >= cfg.hotRmsDb)
    {
        auto s = make ("gain.hot", idKey (t), Category::Gain, Severity::Warning, 0.8f, { &t });
        const float trim = cfg.targetTrackRmsDb - rms;
        s.title = fmt ("'%s' is gain-staged too hot", q (t.name));
        s.detail = fmt ("Average level is %.1f dBFS RMS. Analogue-modelled plugins are calibrated around %.0f dBFS RMS and will saturate earlier than intended.", rms, cfg.targetTrackRmsDb);
        s.steps = {
            fmt ("Add a trim at the top of the chain on '%s' and lower it by %.1f dB.", q (t.name), -trim),
            "Raise the fader by the same amount if you want the same balance in the mix.",
            "Re-check any compressor thresholds on the track: they will now need to be lower.",
        };
        s.action = { ActionType::AdjustGain, t.trackId, trim };
        out.push_back (std::move (s));
    }

    if (t.shortTermLufs > -70.0f && t.shortTermLufs < cfg.quietShortTermLufs)
    {
        auto s = make ("gain.quiet", idKey (t), Category::Gain, Severity::Info, 0.5f, { &t });
        const float boost = cfg.quietShortTermLufs + 15.0f - t.shortTermLufs;
        s.title = fmt ("'%s' is barely audible", q (t.name));
        s.detail = fmt ("Short-term loudness is %.1f LUFS, so it is probably buried under the rest of the mix.", t.shortTermLufs);
        s.steps = {
            fmt ("Solo '%s' against the mix and decide whether it is meant to be a background layer.", q (t.name)),
            fmt ("If not, raise its trim by about %.0f dB and rebalance with the fader.", boost),
        };
        s.action = { ActionType::AdjustGain, t.trackId, boost };
        out.push_back (std::move (s));
    }

    // No cross-track "too loud in the balance" rule: inserts are pre-fader in
    // most hosts, so a Listener cannot see fader moves (see docs/PLAN.md).
    (void) mix;
}

void masterRules (const TrackView& m, const RuleConfig& cfg, std::vector<Suggestion>& out)
{
    if (! m.active)
        return;

    if (m.maxPeakDb() >= cfg.masterCeilingDb)
    {
        auto s = make ("mix.headroom", "master", Category::Gain, Severity::Warning, 0.85f, { &m });
        const float trim = cfg.masterCeilingDb - 0.5f - m.maxPeakDb();
        s.title = "Mix bus has no headroom";
        s.detail = fmt ("Mix peaks hit %+.1f dBFS. Lossy encoders and inter-sample peaks need at least 1 dB below full scale.", m.maxPeakDb());
        s.steps = {
            fmt ("Lower all track faders together (or the mix bus input) by %.1f dB.", -trim),
            "If you are mastering in place, set the limiter ceiling to -1.0 dBTP.",
        };
        s.action = { ActionType::AdjustGain, m.trackId, trim };
        out.push_back (std::move (s));
    }

    if (m.integratedLufs > -70.0f && std::abs (m.integratedLufs - cfg.mixTargetLufs) > cfg.mixTargetToleranceLu)
    {
        const float diff = cfg.mixTargetLufs - m.integratedLufs;
        auto s = make ("mix.loudness", "master", Category::Gain, Severity::Info, 0.6f, { &m });
        s.title = diff > 0 ? "Mix is quieter than the loudness target" : "Mix is louder than the loudness target";
        s.detail = fmt ("Integrated loudness is %.1f LUFS; the target is %.1f LUFS. Streaming services will turn it %s by %.1f dB.",
                        m.integratedLufs, cfg.mixTargetLufs, diff > 0 ? "up (or not at all)" : "down", std::abs (diff));
        s.steps = diff > 0
            ? std::vector<std::string> { "Leave this for mastering if a mastering stage follows.",
                                         fmt ("Otherwise raise the limiter input on the mix bus by about %.1f dB.", diff) }
            : std::vector<std::string> { fmt ("Reduce the limiter input on the mix bus by about %.1f dB.", -diff),
                                         "Louder than target only costs dynamics once the platform normalises it." };
        s.action = { ActionType::AdjustGain, m.trackId, diff };
        out.push_back (std::move (s));
    }

    if (m.numChannels == 2 && m.correlation < 0.3f)
    {
        const bool critical = m.correlation < 0.0f;
        auto s = make ("mix.mono_compat", "master", Category::Phase, critical ? Severity::Critical : Severity::Warning, 0.7f, { &m });
        s.title = "Mix may collapse in mono";
        s.detail = fmt ("Mix-bus phase correlation averages %+.2f. Below 0 the left and right channels partly cancel on mono playback (phones, clubs, broadcast).", m.correlation);
        s.steps = {
            "Switch the monitor controller to mono and listen for elements that disappear.",
            "Check the per-track phase cards first: the culprit is usually one wide synth, reverb or stereo-miked source.",
            "Narrow that element or use a mid/side EQ to reduce the side channel below 200 Hz.",
        };
        out.push_back (std::move (s));
    }

    if (m.numChannels == 2 && m.sideToMidDb > -3.0f)
    {
        auto s = make ("mix.too_wide", "master", Category::Panning, Severity::Warning, 0.6f, { &m });
        s.title = "Mix is very wide";
        s.detail = fmt ("Side energy is only %.1f dB below mid energy. Typical mixes sit 6 to 12 dB lower.", -m.sideToMidDb);
        s.steps = {
            "Identify the widest elements with the channel rack's width readout.",
            "Reduce their stereo-widener amount or bring their pans in.",
        };
        out.push_back (std::move (s));
    }
}

//==============================================================================
// EQ (per track)
void eqTrackRules (const TrackView& t, const RuleConfig& cfg, std::vector<Suggestion>& out)
{
    if (! t.active || t.bands.totalPowerDb < -90.0f)
        return;

    if (! isLowEndRole (t.role) && t.role != TrackRole::Drums)
    {
        const float lowShare = energyShareBelow (t.bands, 120.0f);
        const float threshold = t.role == TrackRole::Unknown ? cfg.lowEndShareUnknown : cfg.lowEndShareKnown;
        if (lowShare > threshold)
        {
            const float hpf = highPassFrequencyFor (t.role);
            auto s = make ("eq.low_end", idKey (t), Category::Eq, lowShare > 0.45f ? Severity::Warning : Severity::Info,
                           0.5f + 0.5f * std::min (1.0f, lowShare), { &t });
            s.title = fmt ("Low-end build-up on '%s'", q (t.name));
            s.detail = fmt ("%.0f%% of its energy sits below 120 Hz, where it competes with kick and bass without adding anything you hear in the mix.", lowShare * 100.0f);
            s.steps = {
                fmt ("Add a high-pass filter on '%s' at %s, 12 or 18 dB/oct.", q (t.name), formatFrequency (hpf).c_str()),
                "Sweep it up while playing the full mix until the track starts to sound thin, then back off by a third.",
                "Compare the kick and bass clarity with the filter bypassed.",
            };
            s.action.type = ActionType::HighPass;
            s.action.trackId = t.trackId;
            s.action.frequencyHz = hpf;
            out.push_back (std::move (s));
        }
    }

    const auto residual = spectralResidual (t.bands);

    // Harshness: 2 - 4.4 kHz sticking out of the track's own tilt.
    int harshBand = -1;
    float harshExcess = 0.0f;
    for (int b = 13; b <= 17; ++b)
        if (residual[(size_t) b] > harshExcess) { harshExcess = residual[(size_t) b]; harshBand = b; }

    const float harshThreshold = cfg.harshExcessDb + (t.role == TrackRole::Vocal ? 3.0f : 0.0f);
    if (harshBand >= 0 && harshExcess > harshThreshold)
    {
        const float harshQ = std::min (4.0f, bandQ (harshBand));
        const float cut = -std::min (6.0f, harshExcess - 2.0f);
        auto s = make ("eq.harsh", idKey (t), Category::Eq, Severity::Info, std::min (1.0f, harshExcess / 12.0f), { &t });
        s.title = fmt ("'%s' is harsh around %s", q (t.name), formatFrequency (bandCentreHz (harshBand)).c_str());
        s.detail = fmt ("This region is %.1f dB above the track's own spectral tilt; the ear is most sensitive here, so it reads as fatiguing.", harshExcess);
        s.steps = {
            fmt ("Insert a bell (or dynamic EQ) band on '%s' at %s, Q %.1f.", q (t.name), formatFrequency (bandCentreHz (harshBand)).c_str(), harshQ),
            fmt ("Cut %.1f dB; with a dynamic EQ set it to act only on the loud notes.", -cut),
            "A/B at matched loudness: the track should sound smoother, not duller.",
        };
        s.action = { ActionType::EqBell, t.trackId, cut, bandCentreHz (harshBand), harshQ };
        out.push_back (std::move (s));
    }

}

//==============================================================================
// PANNING / STEREO (per track)
void stereoTrackRules (const TrackView& t, const RuleConfig& cfg, std::vector<Suggestion>& out)
{
    if (! t.active || t.numChannels < 2)
        return;

    const float lowShare = energyShareBelow (t.bands, 150.0f);
    if (lowShare > 0.3f && t.sideToMidDb > cfg.wideLowEndSideToMidDb && t.correlation >= cfg.correlationWarn)
    {
        auto s = make ("pan.wide_low_end", idKey (t), Category::Panning,
                       isLowEndRole (t.role) ? Severity::Warning : Severity::Info, 0.6f, { &t });
        s.title = fmt ("Stereo low end on '%s'", q (t.name));
        s.detail = fmt ("%.0f%% of the energy is below 150 Hz and the side channel is only %.1f dB under the mid. Low frequencies in the sides waste headroom and fold unpredictably to mono.",
                        lowShare * 100.0f, -t.sideToMidDb);
        s.steps = {
            fmt ("Insert a mid/side EQ or bass-mono utility on '%s'.", q (t.name)),
            "Make everything below 120 Hz mono (or high-pass the side channel at 120 Hz).",
            "Check that the track keeps its width above that frequency.",
        };
        s.action.type = ActionType::MonoBelow;
        s.action.trackId = t.trackId;
        s.action.frequencyHz = 120.0f;
        out.push_back (std::move (s));
    }

    if (t.correlation < cfg.correlationWarn && t.sideToMidDb > -40.0f)
    {
        const bool critical = t.correlation < cfg.correlationCritical;
        auto s = make ("phase.track_correlation", idKey (t), Category::Phase,
                       critical ? Severity::Critical : Severity::Warning, 0.8f, { &t });
        s.title = fmt ("'%s' is out of phase between left and right", q (t.name));
        s.detail = fmt ("Phase correlation is %+.2f. In mono the two sides cancel and the track drops in level or vanishes.", t.correlation);
        s.steps = {
            fmt ("Put '%s' in mono and compare the level to stereo.", q (t.name)),
            "If it is a stereo-miked or DI+amp source, invert the polarity of one side only.",
            "If it comes from a widener or chorus, reduce the width/amount until correlation stays above 0.",
        };
        s.action.type = ActionType::InvertPolarity;
        s.action.trackId = t.trackId;
        out.push_back (std::move (s));
    }
}

//==============================================================================
// PAIRS: masking (EQ + pan), time alignment and polarity
void pairRules (const PairView& p, const TrackView& a, const TrackView& b, const RuleConfig& cfg, std::vector<Suggestion>& out)
{
    if (! a.active || ! b.active)
        return;

    const std::string pairKey = std::to_string (std::min (a.trackId, b.trackId)) + ":" + std::to_string (std::max (a.trackId, b.trackId));
    const auto& m = p.masking;

    // Two mics on the same source (time-correlated) are supposed to share a
    // spectrum: that is a phase problem, not a masking one.
    const bool sameSource = p.delayValid && std::abs (p.delayCorrelation) >= cfg.minDelayCorrelation;
    // Below 150 Hz anything that is not kick or bass is handled by eq.low_end.
    const bool lowEndClash = m.worstBand >= 0 && bandCentreHz (m.worstBand) < 150.0f
                             && ! (isLowEndRole (a.role) && isLowEndRole (b.role));

    // EQ carve
    if (m.worstBand >= 0 && m.score >= cfg.maskingWarn && ! sameSource && ! lowEndClash)
    {
        const int band = m.worstBand;
        const int pa = mixPriority (a.role), pb = mixPriority (b.role);
        const bool cutA = pa != pb ? pa < pb
                                   : bandShare (a.bands, band) < bandShare (b.bands, band);
        const TrackView& victim = cutA ? a : b;
        const TrackView& keeper = cutA ? b : a;

        const bool critical = m.score >= cfg.maskingCritical && std::abs (m.levelDifferenceDb) < 6.0f;
        const float cut = -std::clamp (1.5f + 10.0f * (m.score - cfg.maskingWarn), 1.5f, 6.0f);
        const float f = bandCentreHz (band);

        auto s = make ("eq.masking", pairKey, Category::Eq, critical ? Severity::Critical : Severity::Warning,
                       std::min (1.0f, m.score), { &victim, &keeper });
        s.title = fmt ("'%s' and '%s' mask each other around %s", q (victim.name), q (keeper.name), formatFrequency (f).c_str());
        s.detail = fmt ("%.0f%% spectral overlap; the strongest clash is the %s-%s band, where the levels differ by only %.1f dB.",
                        m.score * 100.0f, formatFrequency (barkBandEdgesHz()[(size_t) band]).c_str(),
                        formatFrequency (barkBandEdgesHz()[(size_t) band + 1]).c_str(), std::abs (m.levelDifferenceDb));
        s.steps = {
            fmt ("Insert an EQ on '%s' (it has the lower priority here).", q (victim.name)),
            fmt ("Add a bell at %s, Q %.1f, and cut %.1f dB.", formatFrequency (f).c_str(), bandQ (band), -cut),
            fmt ("Play both tracks together: '%s' should come forward without '%s' sounding thin.", q (keeper.name), q (victim.name)),
            fmt ("If '%s' loses body, use a dynamic EQ band side-chained from '%s' instead of a static cut.", q (victim.name), q (keeper.name)),
        };
        s.action = { ActionType::EqBell, victim.trackId, cut, f, bandQ (band) };
        out.push_back (std::move (s));
    }

    // Pan apart
    if (m.score >= cfg.maskingPanThreshold && ! sameSource && isPannable (a.role) && isPannable (b.role)
        && a.sideToMidDb < cfg.monoSideToMidDb && b.sideToMidDb < cfg.monoSideToMidDb)
    {
        auto s = make ("pan.separate", pairKey, Category::Panning, Severity::Info, 0.6f, { &a, &b });
        s.title = fmt ("Pan '%s' and '%s' apart", q (a.name), q (b.name));
        s.detail = fmt ("Both are effectively mono and centred, and %.0f%% of their spectra overlap. Spatial separation unmasks them without EQ.", m.score * 100.0f);
        s.steps = {
            fmt ("Pan '%s' to about L 30.", q (a.name)),
            fmt ("Pan '%s' to about R 30.", q (b.name)),
            "Check the balance in mono afterwards: both should still be audible.",
        };
        s.action.type = ActionType::Pan;
        s.action.trackId = a.trackId;
        s.action.pan = -0.3f;
        out.push_back (std::move (s));
    }

    if (! p.delayValid || std::abs (p.delayCorrelation) < cfg.minDelayCorrelation)
        return;

    const float lag = p.delaySamples;
    const double fs = a.sampleRate > 0 ? a.sampleRate : 48000.0;

    if (std::abs (lag) >= cfg.minDelaySamples)
    {
        // Positive lag: B arrives later, so delay A (the earlier track).
        const TrackView& early = lag > 0 ? a : b;
        const TrackView& late  = lag > 0 ? b : a;
        const float samples = std::abs (lag);
        const float ms = (float) (samples * 1000.0 / fs);
        const bool inverted = p.delayCorrelation < 0;

        auto s = make ("phase.time_align", pairKey, Category::Phase,
                       std::abs (p.delayCorrelation) > 0.8f ? Severity::Critical : Severity::Warning,
                       std::abs (p.delayCorrelation), { &early, &late });
        s.title = fmt ("'%s' and '%s' are %.2f ms out of time", q (early.name), q (late.name), ms);
        s.detail = fmt ("They share the same source (correlation %+.2f) but '%s' arrives %.1f samples later, which comb-filters the sum%s.",
                        p.delayCorrelation, q (late.name), samples, inverted ? " and the polarity is inverted as well" : "");
        s.steps = {
            fmt ("Add a sample-delay plugin on '%s' and delay it by %.1f samples (%.2f ms).", q (early.name), samples, ms),
            fmt ("Alternatively nudge the '%s' audio %.2f ms earlier on the timeline.", q (late.name), ms),
        };
        if (inverted)
            s.steps.push_back (fmt ("Then invert the polarity of '%s'.", q (late.name)));
        s.steps.push_back ("Play both together: the low end should get fuller, not thinner.");
        s.action.type = ActionType::TimeAlign;
        s.action.trackId = early.trackId;
        s.action.delaySamples = samples;
        s.action.delayMs = ms;
        out.push_back (std::move (s));
    }
    else if (p.delayCorrelation < 0)
    {
        const TrackView& flip = mixPriority (a.role) <= mixPriority (b.role) ? a : b;
        const TrackView& other = &flip == &a ? b : a;
        auto s = make ("phase.polarity", pairKey, Category::Phase, Severity::Critical, std::abs (p.delayCorrelation), { &flip, &other });
        s.title = fmt ("'%s' is polarity-inverted against '%s'", q (flip.name), q (other.name));
        s.detail = fmt ("The two tracks are time-aligned but anti-correlated (%+.2f): together they cancel instead of adding up.", p.delayCorrelation);
        s.steps = {
            fmt ("Press the polarity (phi / 'phase') button on '%s'.", q (flip.name)),
            "Listen to both together: the shared frequencies should get louder and fuller.",
        };
        s.action.type = ActionType::InvertPolarity;
        s.action.trackId = flip.trackId;
        out.push_back (std::move (s));
    }
}

// Low-mid build-up is a property of the sum, not of any one track (most
// instruments have their fundamentals there), so it is detected on the mix bus
// and attributed to the biggest non-bass contributor in that band.
void mixMudRule (const MixSnapshot& mix, const RuleConfig& cfg, std::vector<Suggestion>& out)
{
    const auto& m = mix.master;
    if (! m.active)
        return;

    const auto residual = spectralResidual (m.bands);
    int band = -1;
    float excess = 0.0f;
    for (int b = 2; b <= 4; ++b)
        if (residual[(size_t) b] > excess) { excess = residual[(size_t) b]; band = b; }
    if (band < 0 || excess <= cfg.mudExcessDb)
        return;

    const TrackView* top = nullptr;
    const TrackView* second = nullptr;
    for (const auto& t : mix.tracks)
    {
        if (! t.active || isLowEndRole (t.role))
            continue;
        if (top == nullptr || t.bands.powerDb[(size_t) band] > top->bands.powerDb[(size_t) band]) { second = top; top = &t; }
        else if (second == nullptr || t.bands.powerDb[(size_t) band] > second->bands.powerDb[(size_t) band]) second = &t;
    }
    if (top == nullptr)
        return;

    const float f = bandCentreHz (band);
    const float cut = -std::min (5.0f, excess - 1.5f);
    auto s = make ("eq.mix_mud", std::to_string (band), Category::Eq, excess > cfg.mudExcessDb + 4.0f ? Severity::Warning : Severity::Info,
                   std::min (1.0f, excess / 10.0f), { top });
    if (second != nullptr)
    {
        s.trackIds.push_back (second->trackId);
        s.trackNames.push_back (second->name);
    }
    s.title = fmt ("Mix is muddy around %s", formatFrequency (f).c_str());
    s.detail = fmt ("The mix bus is %.1f dB above its own tonal balance at %s. The largest contributor%s %s%s%s.",
                    excess, formatFrequency (f).c_str(), second ? "s are" : " is", q (top->name),
                    second ? " and " : "", second ? q (second->name) : "");
    s.steps = {
        fmt ("On '%s', add a wide bell at %s, Q %.1f, cutting %.1f dB.", q (top->name), formatFrequency (f).c_str(), bandQ (band), -cut),
        second ? fmt ("If the mix is still boxy, make a smaller cut on '%s' at the same frequency.", q (second->name)) : std::string ("Re-check the mix bus spectrum."),
        "Judge it in the full mix, not solo: individual tracks will sound thinner but the mix clearer.",
    };
    s.action = { ActionType::EqBell, top->trackId, cut, f, bandQ (band) };
    out.push_back (std::move (s));
}

} // namespace

//==============================================================================
int mixPriority (TrackRole role) noexcept
{
    switch (role)
    {
        case TrackRole::Vocal: return 5;
        case TrackRole::Kick:  return 4;
        case TrackRole::Bass:  return 4;
        case TrackRole::Snare: return 3;
        case TrackRole::Drums: return 3;
        case TrackRole::Guitar: case TrackRole::Keys: case TrackRole::Synth: case TrackRole::Unknown:
            return 2;
        case TrackRole::Fx: return 1;
        case TrackRole::MasterBus: return 6;
        case TrackRole::NumRoles: break;
    }
    return 2;
}

RuleEngine::RuleEngine (RuleConfig c) : config (c) {}

std::vector<Suggestion> RuleEngine::evaluateRaw (const MixSnapshot& mix, const RuleConfig& cfg)
{
    std::vector<Suggestion> out;
    std::unordered_map<uint32_t, const TrackView*> byId;
    for (const auto& t : mix.tracks)
        byId[t.trackId] = &t;

    for (const auto& t : mix.tracks)
    {
        gainRules (t, mix, cfg, out);
        eqTrackRules (t, cfg, out);
        stereoTrackRules (t, cfg, out);
    }

    for (const auto& p : mix.pairs)
    {
        auto ia = byId.find (p.trackA), ib = byId.find (p.trackB);
        if (ia != byId.end() && ib != byId.end())
            pairRules (p, *ia->second, *ib->second, cfg, out);
    }

    if (mix.hasMaster)
    {
        masterRules (mix.master, cfg, out);
        mixMudRule (mix, cfg, out);
    }

    return out;
}

std::vector<Suggestion> RuleEngine::evaluate (const MixSnapshot& snapshot)
{
    auto raw = evaluateRaw (snapshot, config);

    std::unordered_set<std::string> seen;
    for (auto& s : raw)
    {
        seen.insert (s.key);
        auto& tr = trackers[s.key];
        tr.latest = std::move (s);
        tr.hits++;
        tr.misses = 0;
        if (tr.hits >= config.debounceTicks)
            tr.visible = true;
    }

    std::vector<Suggestion> out;
    for (auto it = trackers.begin(); it != trackers.end();)
    {
        auto& tr = it->second;
        if (seen.count (it->first) == 0)
        {
            tr.hits = 0;
            if (++tr.misses > config.holdTicks)
            {
                it = trackers.erase (it);
                continue;
            }
        }
        if (tr.visible && dismissed.count (it->first) == 0)
        {
            tr.latest.ageTicks = ++tr.age;
            out.push_back (tr.latest);
        }
        ++it;
    }

    std::sort (out.begin(), out.end(), [] (const Suggestion& x, const Suggestion& y)
    {
        if (x.severity != y.severity)     return x.severity > y.severity;
        if (x.confidence != y.confidence) return x.confidence > y.confidence;
        return x.key < y.key;
    });
    return out;
}

} // namespace aimix
