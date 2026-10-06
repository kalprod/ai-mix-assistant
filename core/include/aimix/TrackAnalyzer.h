#pragma once

// Listener-side analyser. Runs on the audio thread of every plugin instance.
//
// * Reads the block through const pointers only: the process block is
//   non-destructive by construction.
// * No allocation, locks or system calls after prepare(). (The heartbeat in
//   BusPublisher reads steady_clock, which is a vDSO call on Linux/macOS and
//   QueryPerformanceCounter on Windows.)
// * Accumulates kHopSize samples, then writes one AnalysisPayload directly into
//   the bus ring. Hops are aligned to the host timeline grid while the
//   transport plays, so every Listener's frame N covers the same timeline
//   samples, which is what makes inter-track cross-correlation meaningful.
// * If the ring is full (no Master, or Master stalled) the frame is dropped and
//   the FFT is skipped entirely, so an orphaned Listener costs almost nothing.

#include "AnalysisPayload.h"
#include "Fft.h"
#include "Loudness.h"
#include "SharedBus.h"
#include "StereoAnalysis.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace aimix
{
struct TimelineInfo
{
    int64_t samplePosition = 0;  // host timeline position of the block's first sample
    bool    valid = false;       // host supplied a position AND the transport is playing
};

// Track name shared between the message thread (writer) and audio thread
// (reader) without locks or data races: 32 bytes stored as 4 atomic words.
class AtomicTrackName
{
public:
    void set (const char* utf8) noexcept;
    void get (char (&out)[kMaxTrackNameChars]) const noexcept;

private:
    std::atomic<uint64_t> words[kMaxTrackNameChars / 8] {};
};

// Latest values for the Listener's own editor (written by the audio thread,
// read by the UI timer).
struct ListenerMeters
{
    std::atomic<float> rmsDb[2]  { -120.0f, -120.0f };
    std::atomic<float> peakDb[2] { -120.0f, -120.0f };
    std::atomic<float> momentaryLufs { kLufsFloor };
    std::atomic<float> shortTermLufs { kLufsFloor };
    std::atomic<float> integratedLufs { kLufsFloor };
    std::atomic<float> correlation { 0.0f };
    std::atomic<float> sideToMidDb { -100.0f };
};

float gainToDb (float gain) noexcept;   // floored at -120 dB

class TrackAnalyzer
{
public:
    TrackAnalyzer();

    void prepare (double sampleRate, int numChannels);   // allocates; call from prepareToPlay
    void reset() noexcept;

    void setTrackName (const char* name) noexcept   { name_.set (name); }
    void setRole (TrackRole r) noexcept             { role_.store ((uint8_t) r, std::memory_order_relaxed); }
    void setIsMasterBus (bool b) noexcept           { masterBus_.store (b, std::memory_order_relaxed); }

    // Real-time safe. `channels` is juce::AudioBuffer<float>::getArrayOfReadPointers().
    void process (const float* const* channels, int numChannels, int numSamples,
                  const TimelineInfo& timeline, PayloadSink& sink) noexcept;

    const ListenerMeters& getMeters() const noexcept { return meters; }
    uint64_t getFramesEmitted() const noexcept       { return framesEmitted.load (std::memory_order_relaxed); }
    uint64_t getFramesDropped() const noexcept       { return framesDropped.load (std::memory_order_relaxed); }

private:
    void emitFrame (PayloadSink& sink, int64_t frameStart, bool timelineValid) noexcept;
    void restartFrame (int alignedFill) noexcept;

    double sampleRate = 48000.0;
    int numChannels = 2;

    std::vector<float> fifo;          // mono (L+R)/2, kHopSize
    std::vector<float> powerScratch;  // kNumFftBins
    int  fifoFill = 0;
    bool partialFrame = false;
    bool firstHalfAnalysed = false;   // first FFT of the hop already accumulated

    bool    lastTimelineValid = false;
    int64_t expectedNextPosition = 0;
    int64_t freeRunningPosition = 0;

    StereoAccumulator stereo;
    LoudnessMeter loudness;
    MagnitudeSpectrum spectrum;

    uint64_t sequence = 0;
    AtomicTrackName name_;
    std::atomic<uint8_t> role_ { (uint8_t) TrackRole::Unknown };
    std::atomic<bool> masterBus_ { false };

    ListenerMeters meters;
    std::atomic<uint64_t> framesEmitted { 0 }, framesDropped { 0 };
};

} // namespace aimix
