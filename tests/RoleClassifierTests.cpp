#include "TestFramework.h"
#include "TestSignals.h"
#include "aimix/RoleClassifier.h"
#include "aimix/SpscRingBuffer.h"
#include "aimix/TrackAnalyzer.h"

#include <algorithm>
#include <cstdio>
#include <memory>

using namespace aimix;

namespace
{
struct CaptureSink final : PayloadSink
{
    std::unique_ptr<SpscRingBuffer<AnalysisPayload, 8>> ring = std::make_unique<SpscRingBuffer<AnalysisPayload, 8>>();
    AnalysisPayload* acquire() noexcept override { return ring->tryAcquireWrite(); }
    void commit() noexcept override { ring->commitWrite(); }
    void markDropped() noexcept override {}
    uint32_t trackId() const noexcept override { return 1; }
};

RoleClassifier listen (const testsignals::Render& render, double seconds, float gain = 1.0f, int block = 512)
{
    TrackAnalyzer analyzer;
    analyzer.prepare (testsignals::kFs, 2);
    CaptureSink sink;
    RoleClassifier classifier;
    std::vector<float> l ((size_t) block), r ((size_t) block);
    for (int64_t pos = 0; pos < (int64_t) (seconds * testsignals::kFs); pos += block)
    {
        render (l.data(), r.data(), block, pos);
        for (int i = 0; i < block; ++i) { l[(size_t) i] *= gain; r[(size_t) i] *= gain; }
        const float* ch[] = { l.data(), r.data() };
        analyzer.process (ch, 2, block, TimelineInfo { pos, true }, sink);
        while (const auto* p = sink.ring->tryAcquireRead())
        {
            classifier.addFrame (*p);
            sink.ring->commitRead();
        }
    }
    return classifier;
}
}

TEST_CASE ("auto role: kick, snare, hats, loop, bass, vocal and instruments are recognised")
{
    for (const auto& src : testsignals::sources())
    {
        const auto c = listen (src.render, 8.0);
        std::printf ("    %-16s -> %s\n", src.name.c_str(), toString (c.role()));
        CHECK (c.hasDecided());
        if (c.role() != src.expected)
        {
            std::printf ("      expected %s\n", toString (src.expected));
            CHECK (c.role() == src.expected);
        }
    }
}

TEST_CASE ("auto role: the result doesn't depend on track level or host block size")
{
    for (const auto& src : testsignals::sources())
    {
        CHECK (listen (src.render, 8.0, 0.06f).role() == src.expected);        // -24 dB
        CHECK (listen (src.render, 8.0, 1.0f, 1024).role() == src.expected);
        CHECK (listen (src.render, 8.0, 1.0f, 64).role() == src.expected);
    }
}

TEST_CASE ("auto role: waits for enough signal, ignores silence, and doesn't flip-flop")
{
    const auto sources = testsignals::sources();
    const auto& kick = sources[0];

    auto early = listen (kick.render, 1.0);
    CHECK (! early.hasDecided());
    CHECK (early.role() == TrackRole::Unknown);

    // Long silence before the kick comes in.
    auto withGap = listen ([&] (float* l, float* r, int n, int64_t pos)
    {
        const int64_t start = (int64_t) (testsignals::kFs * 5.0);
        if (pos < start) { std::fill (l, l + n, 0.0f); std::fill (r, r + n, 0.0f); return; }
        kick.render (l, r, n, pos - start);
    }, 13.0);
    CHECK (withGap.role() == TrackRole::Kick);

    // A brief different section doesn't change an established decision.
    const auto& pad = sources[8];
    auto stable = listen ([&] (float* l, float* r, int n, int64_t pos)
    {
        const double t = (double) pos / testsignals::kFs;
        (t > 8.0 && t < 9.5 ? pad : kick).render (l, r, n, pos);
    }, 12.0);
    CHECK (stable.role() == TrackRole::Kick);
}
