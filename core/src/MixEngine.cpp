#include "aimix/MixEngine.h"
#include "aimix/TrackAnalyzer.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <random>

namespace aimix
{
namespace
{
constexpr uint64_t kStaleAfterMs       = 2000;
constexpr uint64_t kActiveHoldMs       = 1500;
constexpr uint64_t kPeakHoldMs         = 3000;
constexpr uint64_t kDelayIntervalMs    = 400;
constexpr int      kDelayBudgetPerTick = 4;
constexpr float    kDelayCandidateMasking = 0.30f;

constexpr float kAnalysisAlpha = 0.08f;   // ~0.5 s time constant at 23 frames/s
constexpr float kDisplayAlpha  = 0.5f;
constexpr float kLevelAlpha    = 0.3f;
constexpr float kStereoAlpha   = 0.1f;

uint32_t randomToken()
{
    std::random_device rd;
    uint32_t t = 0;
    while (t == 0)
        t = rd();
    return t;
}

uint64_t pairKey (uint32_t a, uint32_t b) { return ((uint64_t) std::min (a, b) << 32) | std::max (a, b); }

float powerDb (double p) { return (float) (10.0 * std::log10 (p + 1.0e-12)); }
}

MixEngine::MixEngine (std::shared_ptr<SharedBus> b, RuleConfig config)
    : bus (std::move (b)), token (randomToken()), rules (config)
{
    latest = std::make_shared<MixReport>();
}

MixEngine::~MixEngine()
{
    stop();
    if (bus != nullptr)
        bus->releaseMaster (token);
}

void MixEngine::start (int intervalMs)
{
    stop();
    stopRequested = false;
    worker = std::thread ([this, intervalMs]
    {
        std::unique_lock<std::mutex> lock (threadLock);
        while (! stopRequested)
        {
            lock.unlock();
            tick (monotonicMillis());
            lock.lock();
            wake.wait_for (lock, std::chrono::milliseconds (intervalMs), [this] { return stopRequested; });
        }
    });
}

void MixEngine::stop()
{
    {
        std::lock_guard<std::mutex> lock (threadLock);
        stopRequested = true;
    }
    wake.notify_all();
    if (worker.joinable())
        worker.join();
}

std::shared_ptr<const MixReport> MixEngine::getLatestReport() const
{
    std::lock_guard<std::mutex> lock (reportLock);
    return latest;
}

void MixEngine::dismiss (const std::string& key)
{
    std::lock_guard<std::mutex> lock (dismissLock);
    pendingDismissals.push_back (key);
}

//==============================================================================
void MixEngine::drain (uint64_t nowMs)
{
    for (int i = 0; i < kMaxBusSlots; ++i)
    {
        auto& slot = bus->slot (i);
        auto& s = states[(size_t) i];

        if (slot.state.load (std::memory_order_acquire) != (uint32_t) SlotState::Active)
        {
            s.present = false;
            continue;
        }

        const auto generation = slot.generation.load (std::memory_order_relaxed);
        if (! s.present || s.generation != generation)
        {
            s = TrackState();
            s.present = true;
            s.generation = generation;
            s.lastFrameMs = nowMs;
        }

        while (const auto* p = slot.ring.tryAcquireRead())
        {
            ingest (s, *p, nowMs);
            slot.ring.commitRead();
        }
    }
}

void MixEngine::ingest (TrackState& s, const AnalysisPayload& p, uint64_t nowMs)
{
    if (p.version != kPayloadVersion)
        return;

    auto& v = s.view;
    const bool silent = (p.flags & kFlagSilent) != 0;

    v.trackId = p.trackId;
    v.name.assign (p.trackName, strnlen (p.trackName, kMaxTrackNameChars));
    if (v.name.empty())
        v.name = "Track " + std::to_string (p.trackId + 1);
    s.chosenRole = p.role;
    v.numChannels = (int) p.numChannels;
    v.sampleRate = p.sampleRate;
    s.isMasterBus = (p.flags & kFlagMasterBus) != 0;
    if (s.isMasterBus)
        v.role = TrackRole::MasterBus;
    else if (p.role != TrackRole::Unknown)
        v.role = p.role;
    else
    {
        s.classifier.addFrame (p);
        v.role = s.classifier.role();
    }
    s.lastFrameMs = nowMs;
    s.framesReceived++;
    if (! silent)
        s.lastSignalMs = nowMs;

    for (int c = 0; c < 2; ++c)
    {
        const double ms = std::pow (10.0, p.rmsDb[c] / 10.0);
        s.meanSquare[c] += kLevelAlpha * (ms - s.meanSquare[c]);
        v.rmsDb[c] = powerDb (s.meanSquare[c]);

        if (p.peakDb[c] >= v.peakDb[c] || nowMs - s.peakHoldMs[c] > kPeakHoldMs)
        {
            v.peakDb[c] = p.peakDb[c];
            s.peakHoldMs[c] = nowMs;
        }
    }

    v.momentaryLufs = p.momentaryLufs;
    v.shortTermLufs = p.shortTermLufs;
    v.integratedLufs = p.integratedLufs;

    if (! silent)
    {
        v.correlation += kStereoAlpha * (p.phaseCorrelation - v.correlation);
        s.midEnergy += kStereoAlpha * (p.midEnergy - s.midEnergy);
        s.sideEnergy += kStereoAlpha * (p.sideEnergy - s.sideEnergy);
        v.sideToMidDb = midSideFromEnergies (s.midEnergy, s.sideEnergy).sideToMidDb;

        const float a = s.spectrumPrimed ? kAnalysisAlpha : 1.0f;
        const float d = s.spectrumPrimed ? kDisplayAlpha : 1.0f;
        for (int k = 0; k < kNumFftBins; ++k)
        {
            const float pw = std::pow (10.0f, p.fftMagnitudeDb[k] / 10.0f);
            s.analysisPower[(size_t) k] += a * (pw - s.analysisPower[(size_t) k]);
            s.displayPower[(size_t) k] += d * (pw - s.displayPower[(size_t) k]);
        }
        s.spectrumPrimed = true;
    }

    if ((p.flags & kFlagTimelineValid) && ! (p.flags & kFlagPartialFrame) && ! silent)
    {
        auto& h = s.history[(size_t) s.historyWrite];
        h.timeline = p.timelineSample;
        std::copy (std::begin (p.monoSnapshot), std::end (p.monoSnapshot), h.samples.begin());
        s.historyWrite = (s.historyWrite + 1) % (int) s.history.size();
    }
}

void MixEngine::updateDelay (PairView& pair, TrackState& a, TrackState& b, uint64_t nowMs, int& budget)
{
    auto& d = delays[pairKey (pair.trackA, pair.trackB)];

    if (budget > 0 && nowMs - d.lastUpdateMs >= kDelayIntervalMs)
    {
        // Most recent frame both tracks captured at the same timeline position.
        const FrameHistory* fa = nullptr;
        const FrameHistory* fb = nullptr;
        int64_t best = -1;
        for (const auto& ha : a.history)
            for (const auto& hb : b.history)
                if (ha.timeline >= 0 && ha.timeline == hb.timeline && ha.timeline > best)
                {
                    best = ha.timeline;
                    fa = &ha;
                    fb = &hb;
                }

        if (fa != nullptr)
        {
            --budget;
            d.lastUpdateMs = nowMs;
            const int maxLag = std::min (kSnapshotSize / 4, (int) (a.view.sampleRate * 0.010));   // +-10 ms
            const auto e = delayEstimator.estimate (fa->samples.data(), fb->samples.data(), kSnapshotSize, maxLag, true);
            if (e.valid)
            {
                d.lags[(size_t) (d.count % (int) d.lags.size())] = e.lagSamples;
                d.count++;
                d.correlation = d.count == 1 ? e.correlation : d.correlation + 0.3f * (e.correlation - d.correlation);
            }
        }
    }

    // Stable when the last three estimates agree to within a sample.
    if (d.count >= 3)
    {
        std::array<float, 3> last {};
        for (int i = 0; i < 3; ++i)
            last[(size_t) i] = d.lags[(size_t) ((d.count - 1 - i) % (int) d.lags.size())];
        std::sort (last.begin(), last.end());
        if (last[2] - last[0] <= 1.0f)
        {
            pair.delayValid = true;
            pair.delaySamples = last[1];
            pair.delayCorrelation = d.correlation;
        }
    }
}

TrackSummary MixEngine::summarise (const TrackState& s, uint64_t nowMs) const
{
    TrackSummary t;
    t.view = s.view;
    t.view.active = s.lastSignalMs != 0 && nowMs - s.lastSignalMs <= kActiveHoldMs;
    t.stale = nowMs - s.lastFrameMs > kStaleAfterMs;
    t.autoRole = ! s.isMasterBus && s.chosenRole == TrackRole::Unknown;
    t.roleDetecting = t.autoRole && ! s.classifier.hasDecided();

    t.view.bands = computeBandProfileFromPower (s.analysisPower.data(), kNumFftBins, kFftSize, s.view.sampleRate);

    const double binHz = s.view.sampleRate / kFftSize;
    for (int i = 0; i < kDisplaySpectrumPoints; ++i)
    {
        const double f0 = 20.0 * std::pow (1000.0, (double) i / kDisplaySpectrumPoints);
        const double f1 = 20.0 * std::pow (1000.0, (double) (i + 1) / kDisplaySpectrumPoints);
        int k0 = std::clamp ((int) std::floor (f0 / binHz), 1, kNumFftBins - 1);
        int k1 = std::clamp ((int) std::ceil (f1 / binHz), k0 + 1, kNumFftBins);
        float peak = 0.0f;
        for (int k = k0; k < k1; ++k)
            peak = std::max (peak, s.displayPower[(size_t) k]);
        t.displaySpectrumDb[(size_t) i] = powerDb (peak);
    }
    return t;
}

void MixEngine::tick (uint64_t nowMs)
{
    const auto started = std::chrono::steady_clock::now();
    auto report = std::make_shared<MixReport>();
    report->tick = ++tickCount;
    report->timestampMs = nowMs;

    isMaster = bus != nullptr && bus->tryBecomeMaster (token, nowMs);
    report->isActiveMaster = isMaster;

    if (isMaster)
    {
        bus->heartbeatMaster (token, nowMs);
        drain (nowMs);

        {
            std::lock_guard<std::mutex> lock (dismissLock);
            for (auto& k : pendingDismissals)
                rules.dismiss (k);
            pendingDismissals.clear();
        }

        MixSnapshot snapshot;
        std::vector<int> trackSlots;
        for (int i = 0; i < kMaxBusSlots; ++i)
        {
            const auto& s = states[(size_t) i];
            if (! s.present || s.framesReceived == 0)
                continue;

            auto summary = summarise (s, nowMs);
            summary.droppedFrames = bus->slot (i).droppedFrames.load (std::memory_order_relaxed);
            bus->slot (i).detectedRole.store (summary.autoRole && ! summary.roleDetecting
                                                  ? kDetectedRoleValid | (uint32_t) summary.view.role : 0u,
                                              std::memory_order_relaxed);

            if (s.isMasterBus)
            {
                report->hasMaster = true;
                report->master = summary;
                snapshot.hasMaster = ! summary.stale;
                snapshot.master = summary.view;
                continue;
            }

            report->tracks.push_back (summary);
            if (! summary.stale)
            {
                snapshot.tracks.push_back (summary.view);
                trackSlots.push_back (i);
            }
        }

        int budget = kDelayBudgetPerTick;
        for (size_t x = 0; x < snapshot.tracks.size(); ++x)
        {
            for (size_t y = x + 1; y < snapshot.tracks.size(); ++y)
            {
                const auto& a = snapshot.tracks[x];
                const auto& b = snapshot.tracks[y];
                if (! a.active || ! b.active)
                    continue;

                PairView pair;
                pair.trackA = a.trackId;
                pair.trackB = b.trackId;
                pair.masking = computeMaskingOverlap (a.bands, b.bands);

                if (pair.masking.score >= kDelayCandidateMasking)
                    updateDelay (pair, states[(size_t) trackSlots[x]], states[(size_t) trackSlots[y]], nowMs, budget);

                snapshot.pairs.push_back (pair);
            }
        }

        report->suggestions = rules.evaluate (snapshot);
        report->pairs = snapshot.pairs;
        std::sort (report->pairs.begin(), report->pairs.end(),
                   [] (const PairView& p, const PairView& q) { return p.masking.score > q.masking.score; });
    }

    report->lastTickMs = std::chrono::duration<float, std::milli> (std::chrono::steady_clock::now() - started).count();

    std::lock_guard<std::mutex> lock (reportLock);
    latest = std::move (report);
}

} // namespace aimix
