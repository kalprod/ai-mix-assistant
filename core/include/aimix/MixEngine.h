#pragma once

// Master Engine: the single consumer of the bus.
//
// Runs on its own low-priority thread (never the audio thread):
//   1. drain every active slot's ring (wait-free reads)
//   2. smooth per-track measurements (levels, spectra, correlation, M/S)
//   3. compute pairwise masking and, for related pairs, time offsets
//   4. run the rule engine, publish an immutable MixReport for the UI

#include "DelayEstimator.h"
#include "RoleClassifier.h"
#include "RuleEngine.h"
#include "SharedBus.h"

#include <array>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace aimix
{
constexpr int kDisplaySpectrumPoints = 64;

struct TrackSummary
{
    TrackView view;
    std::array<float, kDisplaySpectrumPoints> displaySpectrumDb {};   // log-spaced 20 Hz .. 20 kHz
    uint32_t droppedFrames = 0;
    bool stale = false;                                              // no frames recently (bypassed / removed)
    bool autoRole = false;                                           // role was detected, not chosen by the user
    bool roleDetecting = false;                                      // auto role: not enough signal heard yet
};

struct MixReport
{
    uint64_t tick = 0;
    uint64_t timestampMs = 0;
    bool isActiveMaster = false;         // false if another Master Engine owns the bus
    std::vector<TrackSummary> tracks;
    bool hasMaster = false;
    TrackSummary master;
    std::vector<PairView> pairs;         // sorted by masking score, highest first
    std::vector<Suggestion> suggestions; // sorted by severity, then confidence
    float lastTickMs = 0.0f;             // engine CPU time per tick
};

class MixEngine
{
public:
    explicit MixEngine (std::shared_ptr<SharedBus> bus, RuleConfig config = {});
    ~MixEngine();

    void start (int tickIntervalMs = 66);   // background thread, ~15 Hz
    void stop();

    // One engine iteration. Called by the thread, or directly by tests.
    void tick (uint64_t nowMs);

    std::shared_ptr<const MixReport> getLatestReport() const;
    void dismiss (const std::string& key);

    bool ownsBus() const noexcept { return isMaster; }

private:
    struct FrameHistory
    {
        int64_t timeline = -1;
        std::array<float, kSnapshotSize> samples {};
    };

    struct TrackState
    {
        bool present = false;
        bool isMasterBus = false;
        uint32_t generation = 0;
        uint64_t framesReceived = 0;
        uint64_t lastFrameMs = 0;
        uint64_t lastSignalMs = 0;
        uint64_t peakHoldMs[2] {};
        TrackView view;
        TrackRole chosenRole = TrackRole::Unknown;   // as set on the Listener; Unknown = Auto
        RoleClassifier classifier;
        double meanSquare[2] {};
        double midEnergy = 0.0, sideEnergy = 0.0;
        double loudnessMean = 0.0, loudnessVar = 0.0;
        int loudnessFrames = 0;
        std::array<float, kNumFftBins> analysisPower {}, displayPower {};
        bool spectrumPrimed = false;
        std::array<FrameHistory, 8> history;
        int historyWrite = 0;
    };

    struct DelayTrack
    {
        std::array<float, 5> lags {};
        int count = 0;
        float correlation = 0.0f;
        uint64_t lastUpdateMs = 0;
    };

    void drain (uint64_t nowMs);
    void ingest (TrackState& s, const AnalysisPayload& p, uint64_t nowMs);
    void updateDelay (PairView& pair, TrackState& a, TrackState& b, uint64_t nowMs, int& budget);
    TrackSummary summarise (const TrackState& s, uint64_t nowMs) const;

    std::shared_ptr<SharedBus> bus;
    uint32_t token;
    bool isMaster = false;
    uint64_t tickCount = 0;

    std::array<TrackState, kMaxBusSlots> states;
    std::map<uint64_t, DelayTrack> delays;
    DelayEstimator delayEstimator { kSnapshotSize };
    RuleEngine rules;

    mutable std::mutex reportLock;
    std::shared_ptr<const MixReport> latest;

    std::mutex dismissLock;
    std::vector<std::string> pendingDismissals;

    std::thread worker;
    std::mutex threadLock;
    std::condition_variable wake;
    bool stopRequested = false;
};

} // namespace aimix
