#pragma once

// A deterministic, synthetic multitrack session with known mix problems baked
// in. Used by the end-to-end tests, the CPU benchmark and the UI snapshot tool,
// so all three exercise the real Listener -> bus -> Master Engine pipeline.
//
//   Kick        clean reference
//   Bass        detuned stereo low end             -> pan.wide_low_end
//   Lead Vox    50 Hz rumble under the voice       -> eq.low_end
//   Guitar      mono, centred, same range as Keys  -> eq.masking + pan.separate
//   Keys        mono, centred, same range as Guitar
//   OH          drum overheads
//   Room        same drums, 37 samples later       -> phase.time_align
//   Pad         right channel polarity-flipped     -> phase.track_correlation
//   Synth Lead  square wave above full scale       -> gain.clipping

#include "MixEngine.h"
#include "TrackAnalyzer.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace aimix
{
class SyntheticSession
{
public:
    struct Track
    {
        std::string name;
        TrackRole role;
        std::function<void (float* left, float* right, int numSamples, int64_t position)> render;
    };

    explicit SyntheticSession (double sampleRate = 48000.0);

    const std::vector<Track>& getTracks() const noexcept { return tracks; }
    double getSampleRate() const noexcept { return sampleRate; }

    static constexpr int kRoomDelaySamples = 37;

private:
    double sampleRate;
    std::vector<Track> tracks;
};

// Runs every track through its own TrackAnalyzer + BusPublisher on a private
// process-local bus, sums them into a mix-bus analyser, and ticks a MixEngine
// at ~15 Hz of audio time. Returns the engine so callers can read its report.
struct SessionRun
{
    std::shared_ptr<SharedBus> bus;
    std::unique_ptr<MixEngine> engine;
    std::vector<std::unique_ptr<TrackAnalyzer>> analyzers;
    std::vector<std::unique_ptr<BusPublisher>> publishers;
    double audioSeconds = 0.0;
    double analyzerSeconds = 0.0;   // wall time spent in TrackAnalyzer::process
    double engineSeconds = 0.0;     // wall time spent in MixEngine::tick
    int engineTicks = 0;
    int blocksProcessed = 0;
};

struct SessionOptions
{
    bool autoRoles = false;    // Listeners report "Auto" and the engine detects roles
    bool masterOnly = false;   // only the mix-bus instance runs (no Listeners on tracks)
};

SessionRun runSyntheticSession (double seconds, int blockSize = 512, const std::string& busName = {}, int extraTracks = 0,
                                SessionOptions options = {},
                                const std::function<void (const MixEngine&)>& afterTick = {});

} // namespace aimix
