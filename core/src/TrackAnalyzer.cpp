#include "aimix/TrackAnalyzer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace aimix
{
static_assert (kHopSize % kFftSize == 0, "hop must hold a whole number of FFT frames");
static constexpr float kSilenceGain = 3.1623e-5f;   // -90 dBFS

float gainToDb (float g) noexcept
{
    return g > 1.0e-6f ? 20.0f * std::log10 (g) : -120.0f;
}

void AtomicTrackName::set (const char* utf8) noexcept
{
    char buffer[kMaxTrackNameChars] {};
    if (utf8 != nullptr)
    {
        std::strncpy (buffer, utf8, kMaxTrackNameChars - 1);
        // Don't leave a split UTF-8 sequence at the end.
        int len = (int) std::strlen (buffer);
        while (len > 0 && ((unsigned char) buffer[len - 1] & 0xC0) == 0x80)
            buffer[--len] = 0;
        if (len > 0 && ((unsigned char) buffer[len - 1] & 0xC0) == 0xC0)
            buffer[--len] = 0;
    }
    for (int w = 0; w < kMaxTrackNameChars / 8; ++w)
    {
        uint64_t v;
        std::memcpy (&v, buffer + w * 8, 8);
        words[w].store (v, std::memory_order_relaxed);
    }
}

void AtomicTrackName::get (char (&out)[kMaxTrackNameChars]) const noexcept
{
    for (int w = 0; w < kMaxTrackNameChars / 8; ++w)
    {
        const uint64_t v = words[w].load (std::memory_order_relaxed);
        std::memcpy (out + w * 8, &v, 8);
    }
    out[kMaxTrackNameChars - 1] = 0;
}

//==============================================================================
TrackAnalyzer::TrackAnalyzer()
    : fifo ((size_t) kHopSize, 0.0f), powerScratch ((size_t) kNumFftBins, 0.0f),
      spectrum (nextPowerOfTwoOrder (kFftSize))
{
}

void TrackAnalyzer::prepare (double fs, int channels)
{
    sampleRate = fs;
    numChannels = std::clamp (channels, 1, 2);
    loudness.prepare (fs, numChannels);
    reset();
}

void TrackAnalyzer::reset() noexcept
{
    std::fill (fifo.begin(), fifo.end(), 0.0f);
    fifoFill = 0;
    partialFrame = false;
    lastTimelineValid = false;
    expectedNextPosition = 0;
    freeRunningPosition = 0;
    stereo.reset();
    loudness.reset();
}

void TrackAnalyzer::restartFrame (int alignedFill) noexcept
{
    std::fill (fifo.begin(), fifo.begin() + alignedFill, 0.0f);
    fifoFill = alignedFill;
    partialFrame = alignedFill > 0;
    firstHalfAnalysed = false;
    stereo.reset();
}

void TrackAnalyzer::process (const float* const* channels, int channelsIn, int numSamples,
                             const TimelineInfo& timeline, PayloadSink& sink) noexcept
{
    if (numSamples <= 0 || channelsIn <= 0)
        return;

    const float* left  = channels[0];
    const float* right = channelsIn > 1 ? channels[1] : channels[0];
    const int analysedChannels = std::min (channelsIn, numChannels);

    loudness.process (channels, analysedChannels, numSamples);

    // Keep frames on the timeline grid; after a seek, loop or transport start,
    // pad the new frame so it still ends on a multiple of kHopSize.
    if (timeline.valid)
    {
        if (! lastTimelineValid || timeline.samplePosition != expectedNextPosition)
        {
            const auto phase = (int) (((timeline.samplePosition % kHopSize) + kHopSize) % kHopSize);
            restartFrame (phase);
        }
        expectedNextPosition = timeline.samplePosition + numSamples;
    }
    else if (lastTimelineValid)
    {
        restartFrame (0);
    }
    lastTimelineValid = timeline.valid;

    int pos = 0;
    while (pos < numSamples)
    {
        // Stop at the half-hop boundary too, so the hop's two FFTs run in
        // different process calls instead of spiking a single block.
        const int boundary = fifoFill < kFftSize ? kFftSize : kHopSize;
        const int chunk = std::min (numSamples - pos, boundary - fifoFill);
        float* dest = fifo.data() + fifoFill;
        for (int i = 0; i < chunk; ++i)
            dest[i] = 0.5f * (left[pos + i] + right[pos + i]);

        stereo.add (left + pos, right + pos, chunk);
        fifoFill += chunk;
        pos += chunk;

        if (fifoFill == kFftSize && sink.hasSpace())
        {
            std::fill (powerScratch.begin(), powerScratch.end(), 0.0f);
            spectrum.accumulatePower (fifo.data(), powerScratch.data());
            firstHalfAnalysed = true;
        }

        if (fifoFill == kHopSize)
        {
            const int64_t frameStart = (timeline.valid ? timeline.samplePosition : freeRunningPosition) + pos - kHopSize;
            emitFrame (sink, frameStart, timeline.valid);
            fifoFill = 0;
            partialFrame = false;
            firstHalfAnalysed = false;
            stereo.reset();
        }
    }

    freeRunningPosition += numSamples;

    meters.momentaryLufs.store (loudness.getMomentaryLufs(), std::memory_order_relaxed);
    meters.shortTermLufs.store (loudness.getShortTermLufs(), std::memory_order_relaxed);
}

void TrackAnalyzer::emitFrame (PayloadSink& sink, int64_t frameStart, bool timelineValid) noexcept
{
    const double count = (double) std::max<long long> (1, stereo.count);
    const float rmsL  = (float) std::sqrt (stereo.sumL2 / count);
    const float rmsR  = (float) std::sqrt (stereo.sumR2 / count);
    const auto ms     = stereo.midSide();
    const float corr  = stereo.correlation();
    const float integrated = loudness.getIntegratedLufs();
    const bool silent = stereo.peakL < kSilenceGain && stereo.peakR < kSilenceGain;

    meters.rmsDb[0].store (gainToDb (rmsL), std::memory_order_relaxed);
    meters.rmsDb[1].store (gainToDb (rmsR), std::memory_order_relaxed);
    meters.peakDb[0].store (gainToDb (stereo.peakL), std::memory_order_relaxed);
    meters.peakDb[1].store (gainToDb (stereo.peakR), std::memory_order_relaxed);
    meters.integratedLufs.store (integrated, std::memory_order_relaxed);
    meters.correlation.store (corr, std::memory_order_relaxed);
    meters.sideToMidDb.store (ms.sideToMidDb, std::memory_order_relaxed);

    AnalysisPayload* p = sink.acquire();
    if (p == nullptr)
    {
        sink.markDropped();
        framesDropped.fetch_add (1, std::memory_order_relaxed);
        return;   // no consumer: skip the FFT work entirely
    }

    p->version = kPayloadVersion;
    p->trackId = sink.trackId();
    p->sequence = sequence++;
    p->timelineSample = frameStart;
    p->sampleRate = sampleRate;
    p->flags = (timelineValid ? kFlagTimelineValid : 0u)
             | (partialFrame ? kFlagPartialFrame : 0u)
             | (silent ? kFlagSilent : 0u)
             | (masterBus_.load (std::memory_order_relaxed) ? kFlagMasterBus : 0u);
    p->numChannels = (uint32_t) numChannels;
    p->role = (TrackRole) role_.load (std::memory_order_relaxed);
    std::memset (p->reserved, 0, sizeof (p->reserved));
    name_.get (p->trackName);

    p->rmsDb[0] = gainToDb (rmsL);
    p->rmsDb[1] = gainToDb (rmsR);
    p->peakDb[0] = gainToDb (stereo.peakL);
    p->peakDb[1] = gainToDb (stereo.peakR);
    p->phaseCorrelation = corr;
    p->midEnergy = (float) ms.midEnergy;
    p->sideEnergy = (float) ms.sideEnergy;
    p->momentaryLufs = loudness.getMomentaryLufs();
    p->shortTermLufs = loudness.getShortTermLufs();
    p->integratedLufs = integrated;

    if (silent)
    {
        std::fill (std::begin (p->fftMagnitudeDb), std::end (p->fftMagnitudeDb), -120.0f);
    }
    else
    {
        static_assert (kHopSize == 2 * kFftSize, "hop = two FFT frames");
        if (! firstHalfAnalysed)
        {
            std::fill (powerScratch.begin(), powerScratch.end(), 0.0f);
            spectrum.accumulatePower (fifo.data(), powerScratch.data());
        }
        spectrum.accumulatePower (fifo.data() + kFftSize, powerScratch.data());
        MagnitudeSpectrum::powerToDb (powerScratch.data(), p->fftMagnitudeDb, kNumFftBins, 0.5f);
    }

    std::memcpy (p->monoSnapshot, fifo.data(), sizeof (p->monoSnapshot));

    sink.commit();
    framesEmitted.fetch_add (1, std::memory_order_relaxed);
}

} // namespace aimix
