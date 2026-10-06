// CPU footprint benchmark for the audio-thread analyser and the Master Engine.
//
//   aimix_bench            full run
//   aimix_bench --quick    shorter run (CI)

#include "aimix/SyntheticSession.h"
#include "aimix/TrackAnalyzer.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace aimix;
using clock_type = std::chrono::steady_clock;

namespace
{
struct DrainingSink final : PayloadSink
{
    std::unique_ptr<SpscRingBuffer<AnalysisPayload, 8>> ring = std::make_unique<SpscRingBuffer<AnalysisPayload, 8>>();
    bool drain = true;
    AnalysisPayload* acquire() noexcept override { return ring->tryAcquireWrite(); }
    bool hasSpace() const noexcept override { return ring->sizeApprox() < 8; }
    void commit() noexcept override { ring->commitWrite(); if (drain) { ring->tryAcquireRead(); ring->commitRead(); } }
    uint32_t trackId() const noexcept override { return 0; }
};

struct Result { double usPerBlock, worstUs, percentOfCore; };

Result benchAnalyzer (int blockSize, double seconds, bool masterPresent)
{
    const double fs = 48000.0;
    TrackAnalyzer a;
    a.prepare (fs, 2);
    DrainingSink sink;
    sink.drain = masterPresent;

    std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize);
    uint32_t seed = 1;
    for (int i = 0; i < blockSize; ++i)
    {
        seed = seed * 1664525u + 1013904223u;
        l[(size_t) i] = (float) (seed >> 9) / 8388608.0f - 1.0f;
        r[(size_t) i] = 0.7f * l[(size_t) i];
    }
    const float* ch[] = { l.data(), r.data() };

    const int blocks = (int) (seconds * fs / blockSize);
    double total = 0.0, worst = 0.0;
    for (int b = 0; b < blocks; ++b)
    {
        const auto t0 = clock_type::now();
        a.process (ch, 2, blockSize, { (int64_t) b * blockSize, true }, sink);
        const double us = std::chrono::duration<double, std::micro> (clock_type::now() - t0).count();
        total += us;
        worst = std::max (worst, us);
    }
    const double audioUs = (double) blocks * blockSize / fs * 1.0e6;
    return { total / blocks, worst, 100.0 * total / audioUs };
}
}

int main (int argc, char** argv)
{
    const bool quick = argc > 1 && std::strcmp (argv[1], "--quick") == 0;
    const double seconds = quick ? 10.0 : 60.0;

    std::printf ("Listener analyser (48 kHz stereo, %.0f s of audio per row)\n", seconds);
    std::printf ("  %-8s %-16s %14s %14s %12s %16s\n", "block", "master", "avg us/block", "worst us", "% one core", "block budget %");
    for (int bs : { 64, 128, 256, 512, 1024 })
        for (bool master : { true, false })
        {
            const auto r = benchAnalyzer (bs, seconds, master);
            const double budgetUs = bs / 48000.0 * 1.0e6;
            std::printf ("  %-8d %-16s %14.2f %14.2f %11.3f%% %15.2f%%\n", bs, master ? "draining" : "absent (orphan)",
                         r.usPerBlock, r.worstUs, r.percentOfCore, 100.0 * r.worstUs / budgetUs);
        }

    std::printf ("\nMaster Engine (synthetic session, ~15 Hz ticks, background thread)\n");
    std::printf ("  %-8s %18s %18s %20s\n", "tracks", "avg ms / tick", "engine % core", "listeners % core");
    for (int extra : { 0, 23, 54 })
    {
        auto run = runSyntheticSession (quick ? 4.0 : 12.0, 512, {}, extra);
        const int tracks = (int) run.analyzers.size() - 1;
        std::printf ("  %-8d %18.3f %17.3f%% %19.3f%%\n", tracks,
                     1000.0 * run.engineSeconds / std::max (1, run.engineTicks),
                     100.0 * run.engineSeconds / run.audioSeconds,
                     100.0 * run.analyzerSeconds / run.audioSeconds);
    }
    return 0;
}
