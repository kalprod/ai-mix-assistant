#include "TestFramework.h"
#include "aimix/TrackAnalyzer.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

using namespace aimix;

namespace
{
constexpr double kTwoPi = 6.283185307179586;

// Ring-backed sink, same mechanics as BusPublisher without a bus.
struct RingSink final : PayloadSink
{
    std::unique_ptr<SpscRingBuffer<AnalysisPayload, 8>> ring = std::make_unique<SpscRingBuffer<AnalysisPayload, 8>>();
    int dropped = 0;
    AnalysisPayload* acquire() noexcept override { return ring->tryAcquireWrite(); }
    void commit() noexcept override { ring->commitWrite(); }
    void markDropped() noexcept override { ++dropped; }
    uint32_t trackId() const noexcept override { return 3; }
};

struct StereoBlock
{
    std::vector<float> l, r;
    explicit StereoBlock (int n) : l ((size_t) n), r ((size_t) n) {}
    const float* const* ptrs() { p[0] = l.data(); p[1] = r.data(); return p; }
    const float* p[2] {};
};

void fillSine (StereoBlock& b, int64_t pos, double freq, double fs, float amp)
{
    for (size_t i = 0; i < b.l.size(); ++i)
        b.l[i] = b.r[i] = amp * (float) std::sin (kTwoPi * freq * (double) (pos + (int64_t) i) / fs);
}
}

TEST_CASE ("analyzer: process block is non-destructive (bit-identical input)")
{
    TrackAnalyzer a;
    a.prepare (48000.0, 2);
    RingSink sink;
    StereoBlock block (512);
    for (int i = 0; i < 512; ++i) { block.l[(size_t) i] = std::sin (i * 0.1f); block.r[(size_t) i] = std::cos (i * 0.07f); }
    const auto copyL = block.l, copyR = block.r;

    for (int k = 0; k < 64; ++k)
        a.process (block.ptrs(), 2, 512, { k * 512, true }, sink);

    CHECK (std::memcmp (copyL.data(), block.l.data(), copyL.size() * sizeof (float)) == 0);
    CHECK (std::memcmp (copyR.data(), block.r.data(), copyR.size() * sizeof (float)) == 0);
}

TEST_CASE ("analyzer: audio-thread path performs zero heap allocations")
{
    TrackAnalyzer a;
    a.prepare (48000.0, 2);
    RingSink sink;
    StereoBlock block (480);
    fillSine (block, 0, 440.0, 48000.0, 0.5f);
    AnalysisPayload out;

    const auto before = aimixtest::allocationCount.load();
    for (int k = 0; k < 2000; ++k)   // ~20 s of audio, ~470 frames emitted
    {
        a.process (block.ptrs(), 2, 480, { (int64_t) k * 480, true }, sink);
        while (sink.ring->tryPop (out)) {}
    }
    const auto after = aimixtest::allocationCount.load();
    CHECK (after == before);
    CHECK (a.getFramesEmitted() > 400);
}

TEST_CASE ("analyzer: frames are aligned to the timeline grid; first frame after a seek is flagged partial")
{
    TrackAnalyzer a;
    a.prepare (48000.0, 2);
    RingSink sink;
    StereoBlock block (512);
    fillSine (block, 0, 1000.0, 48000.0, 0.5f);

    int64_t pos = 1000;   // not a multiple of kHopSize
    std::vector<AnalysisPayload> frames;
    AnalysisPayload out;
    for (int k = 0; k < 40; ++k, pos += 512)
    {
        a.process (block.ptrs(), 2, 512, { pos, true }, sink);
        while (sink.ring->tryPop (out)) frames.push_back (out);
    }

    REQUIRE (frames.size() >= 8);
    CHECK ((frames[0].flags & kFlagPartialFrame) != 0);
    CHECK ((frames[1].flags & kFlagPartialFrame) == 0);
    for (auto& f : frames)
    {
        CHECK (f.timelineSample % kHopSize == 0);
        CHECK ((f.flags & kFlagTimelineValid) != 0);
        CHECK (f.trackId == 3);
    }
    for (size_t i = 1; i < frames.size(); ++i)
    {
        CHECK (frames[i].timelineSample == frames[i - 1].timelineSample + kHopSize);
        CHECK (frames[i].sequence == frames[i - 1].sequence + 1);
    }
}

TEST_CASE ("analyzer: payload carries correct RMS, peak, correlation and spectrum")
{
    const double fs = 48000.0;
    TrackAnalyzer a;
    a.prepare (fs, 2);
    a.setTrackName ("Lead Vox");
    a.setRole (TrackRole::Vocal);
    RingSink sink;
    StereoBlock block (512);
    const double binFreq = 40 * fs / kFftSize;   // bin-centred

    AnalysisPayload last {};
    for (int k = 0; k < 400; ++k)
    {
        fillSine (block, (int64_t) k * 512, binFreq, fs, 0.5f);
        a.process (block.ptrs(), 2, 512, { (int64_t) k * 512, true }, sink);
        while (sink.ring->tryPop (last)) {}
    }

    CHECK (std::string (last.trackName) == "Lead Vox");
    CHECK (last.role == TrackRole::Vocal);
    CHECK_NEAR (last.rmsDb[0], 20.0 * std::log10 (0.5 / std::sqrt (2.0)), 0.05);
    CHECK_NEAR (last.peakDb[1], 20.0 * std::log10 (0.5), 0.05);
    CHECK_NEAR (last.phaseCorrelation, 1.0, 1e-4);
    CHECK (last.sideEnergy < 1e-9f);
    CHECK_NEAR (last.fftMagnitudeDb[40], 20.0 * std::log10 (0.5), 0.1);
    CHECK (last.fftMagnitudeDb[80] < -60.0f);
    CHECK_NEAR (last.monoSnapshot[100], 0.5 * std::sin (kTwoPi * binFreq * (double) (last.timelineSample + 100) / fs), 1e-4);
    // 1.875 kHz at -6 dBFS: K-weighting's shelf adds ~+2 dB here, so expect about -4 LUFS.
    std::printf ("    short-term %.2f LUFS\n", last.shortTermLufs);
    CHECK (last.shortTermLufs > -5.0f && last.shortTermLufs < -3.0f);
}

TEST_CASE ("analyzer: mono input, silence flag, and drop-when-full with no FFT work")
{
    TrackAnalyzer a;
    a.prepare (48000.0, 1);
    RingSink sink;
    std::vector<float> zeros (512, 0.0f);
    const float* ch[] = { zeros.data() };

    for (int k = 0; k < 64; ++k)   // 16 frames, ring holds 8, nobody drains
        a.process (ch, 1, 512, { (int64_t) k * 512, true }, sink);

    CHECK (a.getFramesEmitted() == 8);
    CHECK (a.getFramesDropped() == 8);
    CHECK (sink.dropped == 8);

    AnalysisPayload out;
    REQUIRE (sink.ring->tryPop (out));
    CHECK ((out.flags & kFlagSilent) != 0);
    CHECK (out.numChannels == 1);
    CHECK (out.fftMagnitudeDb[10] == -120.0f);
}

TEST_CASE ("analyzer: track name handoff truncates safely and never splits UTF-8")
{
    AtomicTrackName n;
    n.set ("Überlange Spur mit sehr vielen Zeichen ü");
    char out[kMaxTrackNameChars];
    n.get (out);
    CHECK (std::strlen (out) <= 31);
    const auto last = (unsigned char) out[std::strlen (out) - 1];
    CHECK ((last & 0x80) == 0);   // ends on a complete ASCII char
}
