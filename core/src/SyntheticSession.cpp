#include "aimix/SyntheticSession.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>

namespace aimix
{
namespace
{
constexpr double kTwoPi = 6.283185307179586;

struct Noise
{
    uint32_t state;
    explicit Noise (uint32_t seed) : state (seed) {}
    float next()   // white, -1 .. 1
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) ((double) state / 2147483648.0 - 1.0);
    }
};

// Same drum source for the overhead and the room mic.
struct DrumSource
{
    Noise noise { 1234u };
    std::vector<float> line = std::vector<float> (4096, 0.0f);
    int64_t written = 0;
    double fs;

    explicit DrumSource (double sampleRate) : fs (sampleRate) {}

    float sampleAt (int64_t n)
    {
        while (written <= n)
        {
            const double t = (double) (written % (int64_t) (fs * 0.25)) / fs;   // hit every 250 ms
            const float env = (float) std::exp (-t / 0.05);
            const float tone = (float) std::sin (kTwoPi * 190.0 * (double) written / fs);
            line[(size_t) (written % (int64_t) line.size())] = env * (0.5f * noise.next() + 0.4f * tone) * 0.5f;
            ++written;
        }
        return line[(size_t) (n % (int64_t) line.size())];
    }
};

float harmonicTone (double f0, double t, int harmonics, std::function<float (double)> weight)
{
    double s = 0.0;
    for (int h = 1; h <= harmonics; ++h)
        s += weight (f0 * h) / (double) h * std::sin (kTwoPi * f0 * h * t);
    return (float) s;
}
}

SyntheticSession::SyntheticSession (double fs) : sampleRate (fs)
{
    const double sr = fs;

    tracks.push_back ({ "Kick", TrackRole::Kick, [sr] (float* l, float* r, int n, int64_t pos)
    {
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) ((pos + i) % (int64_t) (sr * 0.5)) / sr;
            const double phase = kTwoPi * (50.0 * t + 70.0 * 0.03 * (1.0 - std::exp (-t / 0.03)));
            l[i] = r[i] = (float) (0.8 * std::exp (-t / 0.15) * std::sin (phase));
        }
    } });

    tracks.push_back ({ "Bass", TrackRole::Bass, [sr] (float* l, float* r, int n, int64_t pos)
    {
        static const double notes[] = { 55.0, 73.42, 65.41, 49.0 };
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (pos + i) / sr;
            const double f = notes[(int) t % 4];
            auto w = [] (double) { return 1.0f; };
            l[i] = 0.3f * harmonicTone (f, t, 6, w);
            r[i] = 0.3f * harmonicTone (f * 1.006, t, 6, w);   // detuned: wide low end
        }
    } });

    tracks.push_back ({ "Lead Vox", TrackRole::Vocal, [sr] (float* l, float* r, int n, int64_t pos)
    {
        auto formants = [] (double f)
        {
            auto peak = [f] (double c, double bw) { return 1.0 / (1.0 + std::pow ((f - c) / bw, 2.0)); };
            return (float) (0.2 + peak (700, 150) + 0.8 * peak (1200, 200) + 0.5 * peak (2600, 300));
        };
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (pos + i) / sr;
            const double f0 = 220.0 * (1.0 + 0.01 * std::sin (kTwoPi * 5.0 * t));
            const double env = 0.6 + 0.4 * std::sin (kTwoPi * 4.0 * t);
            const float voice = (float) (0.25 * env) * harmonicTone (f0, t, 18, formants);
            const float rumble = 0.2f * (float) std::sin (kTwoPi * 50.0 * t);   // mic-stand rumble
            l[i] = r[i] = voice + rumble;
        }
    } });

    auto chordTrack = [sr] (std::vector<double> chord, int harmonics, float gain)
    {
        return [sr, chord, harmonics, gain] (float* l, float* r, int n, int64_t pos)
        {
            for (int i = 0; i < n; ++i)
            {
                const double t = (double) (pos + i) / sr;
                float s = 0.0f;
                for (double f : chord)
                    s += harmonicTone (f, t, harmonics, [] (double) { return 1.0f; });
                l[i] = r[i] = gain * s;
            }
        };
    };

    tracks.push_back ({ "Guitar", TrackRole::Guitar, chordTrack ({ 196.0, 246.9, 293.7 }, 12, 0.12f) });
    // A few cents off the guitar: a separate instrument, not a second mic on it.
    tracks.push_back ({ "Keys",   TrackRole::Keys,   chordTrack ({ 196.0 * 1.003, 246.9 * 1.003, 329.6 }, 10, 0.12f) });

    auto drums = std::make_shared<DrumSource> (fs);
    tracks.push_back ({ "OH", TrackRole::Drums, [drums] (float* l, float* r, int n, int64_t pos)
    {
        for (int i = 0; i < n; ++i)
            l[i] = r[i] = drums->sampleAt (pos + i);
    } });

    auto roomNoise = std::make_shared<Noise> (99u);
    tracks.push_back ({ "Room", TrackRole::Drums, [drums, roomNoise] (float* l, float* r, int n, int64_t pos)
    {
        for (int i = 0; i < n; ++i)
        {
            const int64_t src = pos + i - kRoomDelaySamples;
            const float s = src >= 0 ? drums->sampleAt (src) : 0.0f;
            l[i] = r[i] = 0.7f * s + 0.01f * roomNoise->next();
        }
    } });

    auto padNoise = std::make_shared<Noise> (7u);
    tracks.push_back ({ "Pad", TrackRole::Synth, [sr, padNoise] (float* l, float* r, int n, int64_t pos)
    {
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (pos + i) / sr;
            const float x = 0.1f * harmonicTone (130.8, t, 8, [] (double) { return 1.0f; })
                          + 0.1f * harmonicTone (164.8, t, 8, [] (double) { return 1.0f; });
            l[i] = x;
            r[i] = -0.85f * x + 0.01f * padNoise->next();   // miswired polarity on one side
        }
    } });

    tracks.push_back ({ "Synth Lead", TrackRole::Synth, [sr] (float* l, float* r, int n, int64_t pos)
    {
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) (pos + i) / sr;
            const double ph = std::fmod (t * 880.0, 1.0);
            l[i] = r[i] = (ph < 0.5 ? 1.1f : -1.1f) * (float) (0.5 + 0.5 * std::sin (kTwoPi * 0.5 * t));
        }
    } });
}

//==============================================================================
SessionRun runSyntheticSession (double seconds, int blockSize, const std::string& busNameIn, int extraTracks,
                                SessionOptions options, const std::function<void (const MixEngine&)>& afterTick)
{
    static std::atomic<int> counter { 0 };
    const std::string busName = busNameIn.empty() ? "aimix_demo_" + std::to_string (counter++) : busNameIn;

    SyntheticSession session;
    const double fs = session.getSampleRate();
    auto tracks = session.getTracks();

    // Optional extra copies for load testing.
    for (int e = 0; e < extraTracks; ++e)
    {
        auto copy = tracks[(size_t) (e % (int) tracks.size())];
        copy.name += " #" + std::to_string (e + 2);
        tracks.push_back (copy);
    }

    SessionRun run;
    run.bus = SharedBus::open (busName, SharedBus::Backend::ProcessLocal);
    run.engine = std::make_unique<MixEngine> (run.bus);

    const int numTracks = (int) tracks.size();
    for (int t = 0; t <= numTracks; ++t)   // last one is the mix bus
    {
        if (options.masterOnly && t < numTracks)
        {
            run.publishers.push_back (nullptr);
            run.analyzers.push_back (nullptr);
            continue;
        }
        run.publishers.push_back (std::make_unique<BusPublisher> (run.bus));
        auto a = std::make_unique<TrackAnalyzer>();
        a->prepare (fs, 2);
        if (t < numTracks)
        {
            a->setTrackName (tracks[(size_t) t].name.c_str());
            a->setRole (options.autoRoles ? TrackRole::Unknown : tracks[(size_t) t].role);
        }
        else
        {
            a->setTrackName ("Mix Bus");
            a->setRole (TrackRole::MasterBus);
            a->setIsMasterBus (true);
        }
        run.analyzers.push_back (std::move (a));
    }

    std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize), ml ((size_t) blockSize), mr ((size_t) blockSize);
    const int64_t totalSamples = (int64_t) (seconds * fs);
    const uint64_t startMs = monotonicMillis();
    const int64_t tickEvery = (int64_t) (fs * 0.066);
    int64_t nextTick = tickEvery;

    using clock = std::chrono::steady_clock;

    for (int64_t pos = 0; pos < totalSamples; pos += blockSize)
    {
        const int n = (int) std::min<int64_t> (blockSize, totalSamples - pos);
        std::fill (ml.begin(), ml.begin() + n, 0.0f);
        std::fill (mr.begin(), mr.begin() + n, 0.0f);
        TimelineInfo tl { pos, true };

        for (int t = 0; t < numTracks; ++t)
        {
            tracks[(size_t) t].render (l.data(), r.data(), n, pos);
            const float* ch[] = { l.data(), r.data() };

            if (run.analyzers[(size_t) t] != nullptr)
            {
                const auto t0 = clock::now();
                run.analyzers[(size_t) t]->process (ch, 2, n, tl, *run.publishers[(size_t) t]);
                run.analyzerSeconds += std::chrono::duration<double> (clock::now() - t0).count();
            }

            for (int i = 0; i < n; ++i)
            {
                ml[(size_t) i] += 0.25f * l[(size_t) i];
                mr[(size_t) i] += 0.25f * r[(size_t) i];
            }
        }

        const float* mch[] = { ml.data(), mr.data() };
        run.analyzers.back()->process (mch, 2, n, tl, *run.publishers.back());
        run.blocksProcessed++;

        if (pos + n >= nextTick)
        {
            nextTick += tickEvery;
            const auto t0 = clock::now();
            run.engine->tick (startMs + (uint64_t) ((double) (pos + n) * 1000.0 / fs));
            run.engineSeconds += std::chrono::duration<double> (clock::now() - t0).count();
            run.engineTicks++;
            if (afterTick)
                afterTick (*run.engine);
        }
    }

    run.audioSeconds = (double) totalSamples / fs;
    return run;
}

} // namespace aimix
