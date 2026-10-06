#pragma once

// Simple synthetic sources for role-detection tests. Each renders stereo
// audio at 48 kHz for an absolute sample position, so tests can stream them
// block by block through a TrackAnalyzer.

#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "aimix/AnalysisPayload.h"

namespace testsignals
{
constexpr double kFs = 48000.0;
constexpr double kTwoPi = 6.283185307179586;

using Render = std::function<void (float* l, float* r, int n, int64_t pos)>;

struct Source
{
    std::string name;
    aimix::TrackRole expected;
    Render render;
};

// Deterministic white noise that depends only on the sample index.
inline float noiseAt (int64_t n, uint32_t seed = 1)
{
    uint32_t x = (uint32_t) n * 2654435761u ^ seed * 40503u;
    x ^= x >> 15; x *= 2246822519u; x ^= x >> 13; x *= 3266489917u; x ^= x >> 16;
    return (float) ((double) x / 2147483648.0 - 1.0);
}

inline double tIn (int64_t n, double period) { return std::fmod ((double) n / kFs, period); }

inline float kickAt (int64_t n)
{
    const double t = tIn (n, 0.5);
    const double ph = kTwoPi * (50.0 * t + 70.0 * 0.03 * (1.0 - std::exp (-t / 0.03)));
    return (float) (0.8 * std::exp (-t / 0.15) * std::sin (ph));
}

inline float snareAt (int64_t n)   // backbeat: hits on 2 and 4 at 120 BPM
{
    const double t = tIn ((int64_t) (n + (int64_t) (kFs * 0.5)), 1.0);
    // Noise with the lowest octave removed (first difference), like snare wires.
    const float wires = noiseAt (n, 7) - noiseAt (n - 1, 7);
    return (float) (0.35 * std::exp (-t / 0.12) * wires + 0.4 * std::exp (-t / 0.06) * std::sin (kTwoPi * 190.0 * t));
}

inline float hatAt (int64_t n)   // closed hats on 8th notes
{
    const double t = tIn (n, 0.25);
    const float hp = noiseAt (n, 3) - 2.0f * noiseAt (n - 1, 3) + noiseAt (n - 2, 3);   // second difference: bright
    return (float) (0.25 * std::exp (-t / 0.03) * hp);
}

inline float harmonic (double f0, double t, int harmonics, double rolloff = 1.0)
{
    double s = 0.0;
    for (int h = 1; h <= harmonics; ++h)
        if (f0 * h < kFs * 0.45)
            s += std::sin (kTwoPi * f0 * h * t) / std::pow ((double) h, rolloff);
    return (float) s;
}

inline float voiceAt (int64_t n)
{
    const double t = (double) n / kFs;
    const double f0 = 220.0 * (1.0 + 0.01 * std::sin (kTwoPi * 5.0 * t));
    double s = 0.0;
    for (int h = 1; h <= 20; ++h)
    {
        const double f = f0 * h;
        auto peak = [f] (double c, double bw) { return 1.0 / (1.0 + std::pow ((f - c) / bw, 2.0)); };
        const double w = 0.15 + peak (700, 150) + 0.8 * peak (1200, 200) + 0.5 * peak (2600, 300);
        s += w / h * std::sin (kTwoPi * f * t);
    }
    const double syllable = 0.55 + 0.45 * std::sin (kTwoPi * 4.0 * t);
    const bool inPhrase = std::fmod (t, 2.0) < 1.6;   // breath between phrases
    const float breath = 0.004f * noiseAt (n, 11);
    return inPhrase ? (float) (0.3 * syllable * s) + breath : breath;
}

inline std::vector<Source> sources()
{
    std::vector<Source> s;
    using aimix::TrackRole;

    s.push_back ({ "kick", TrackRole::Kick, [] (float* l, float* r, int n, int64_t pos)
        { for (int i = 0; i < n; ++i) l[i] = r[i] = kickAt (pos + i); } });

    s.push_back ({ "808", TrackRole::Bass,  // a long 808 rings like a bass and mixes like one
                 [] (float* l, float* r, int n, int64_t pos)
        { for (int i = 0; i < n; ++i)
          { const double t = tIn (pos + i, 1.0);
            l[i] = r[i] = (float) (0.8 * std::exp (-t / 0.25) * std::sin (kTwoPi * (45.0 * t + 90.0 * 0.01 * (1.0 - std::exp (-t / 0.01))))); } } });

    s.push_back ({ "snare", TrackRole::Snare, [] (float* l, float* r, int n, int64_t pos)
        { for (int i = 0; i < n; ++i) l[i] = r[i] = snareAt (pos + i); } });

    s.push_back ({ "hats", TrackRole::Drums, [] (float* l, float* r, int n, int64_t pos)
        { for (int i = 0; i < n; ++i) l[i] = r[i] = hatAt (pos + i); } });

    s.push_back ({ "drum loop", TrackRole::Drums, [] (float* l, float* r, int n, int64_t pos)
        { for (int i = 0; i < n; ++i)
          { const float d = 0.7f * kickAt (pos + i) + snareAt (pos + i) + hatAt (pos + i);
            l[i] = d + 0.6f * hatAt (pos + i + 5); r[i] = d; } } });

    s.push_back ({ "sustained bass", TrackRole::Bass, [] (float* l, float* r, int n, int64_t pos)
        { static const double notes[] = { 55.0, 73.42, 65.41, 49.0 };
          for (int i = 0; i < n; ++i)
          { const double t = (double) (pos + i) / kFs;
            l[i] = r[i] = 0.3f * harmonic (notes[(int) t % 4], t, 6); } } });

    s.push_back ({ "plucked bass", TrackRole::Bass, [] (float* l, float* r, int n, int64_t pos)
        { static const double notes[] = { 41.2, 41.2, 55.0, 49.0 };
          for (int i = 0; i < n; ++i)
          { const double t = (double) (pos + i) / kFs;
            const double tn = std::fmod (t, 0.5);
            l[i] = r[i] = (float) (0.5 * std::exp (-tn / 0.35)) * harmonic (notes[(int) (t * 2) % 4], t, 8, 1.3); } } });

    s.push_back ({ "vocal", TrackRole::Vocal, [] (float* l, float* r, int n, int64_t pos)
        { for (int i = 0; i < n; ++i) l[i] = r[i] = voiceAt (pos + i); } });

    s.push_back ({ "pad", TrackRole::Unknown, [] (float* l, float* r, int n, int64_t pos)
        { for (int i = 0; i < n; ++i)
          { const double t = (double) (pos + i) / kFs;
            l[i] = 0.08f * (harmonic (261.6, t, 8) + harmonic (329.6, t, 8) + harmonic (392.0, t, 8));
            r[i] = 0.08f * (harmonic (261.6 * 1.004, t, 8) + harmonic (329.6 * 0.997, t, 8) + harmonic (392.0 * 1.003, t, 8)); } } });

    s.push_back ({ "strummed guitar", TrackRole::Unknown, [] (float* l, float* r, int n, int64_t pos)
        { for (int i = 0; i < n; ++i)
          { const double t = (double) (pos + i) / kFs;
            const double env = std::exp (-std::fmod (t, 0.5) / 0.4);
            const float x = (float) (0.1 * env) * (harmonic (196.0, t, 14) + harmonic (246.9, t, 14) + harmonic (293.7, t, 14));
            l[i] = x; r[i] = 0.8f * x; } } });

    s.push_back ({ "piano", TrackRole::Unknown, [] (float* l, float* r, int n, int64_t pos)
        { static const double notes[] = { 261.6, 329.6, 392.0, 523.3 };
          for (int i = 0; i < n; ++i)
          { const double t = (double) (pos + i) / kFs;
            const double env = std::exp (-std::fmod (t, 0.25) / 0.5);
            const float x = (float) (0.3 * env) * harmonic (notes[(int) (t * 4) % 4], t, 10, 1.5);
            l[i] = x; r[i] = 0.6f * x + 0.2f * (float) (0.3 * env) * harmonic (notes[(int) (t * 4) % 4] * 1.002, t, 10, 1.5); } } });

    s.push_back ({ "synth lead", TrackRole::Unknown, [] (float* l, float* r, int n, int64_t pos)
        { static const double notes[] = { 440.0, 523.3, 587.3, 659.3 };
          for (int i = 0; i < n; ++i)
          { const double t = (double) (pos + i) / kFs;
            l[i] = r[i] = 0.15f * harmonic (notes[(int) (t * 2) % 4], t, 25); } } });

    return s;
}

} // namespace testsignals
