#include "TestFramework.h"
#include "aimix/DelayEstimator.h"
#include "aimix/Fft.h"
#include "aimix/Loudness.h"
#include "aimix/SpectralAnalysis.h"
#include "aimix/StereoAnalysis.h"

#include <cmath>
#include <random>
#include <vector>

using namespace aimix;

namespace
{
constexpr double kTwoPi = 6.283185307179586;

std::vector<float> sine (double freq, double fs, int n, double amplitude, double phase = 0.0)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i)
        v[(size_t) i] = (float) (amplitude * std::sin (kTwoPi * freq * i / fs + phase));
    return v;
}

std::vector<float> noise (int n, unsigned seed, float amplitude = 0.5f)
{
    std::mt19937 rng (seed);
    std::uniform_real_distribution<float> d (-amplitude, amplitude);
    std::vector<float> v ((size_t) n);
    for (auto& x : v) x = d (rng);
    return v;
}

// Feeds a stereo signal through the meter in 512-sample blocks.
void feed (LoudnessMeter& m, const std::vector<float>& l, const std::vector<float>& r)
{
    for (size_t pos = 0; pos < l.size(); pos += 512)
    {
        const int n = (int) std::min<size_t> (512, l.size() - pos);
        const float* ch[] = { l.data() + pos, r.data() + pos };
        m.process (ch, 2, n);
    }
}

double dbToGain (double db) { return std::pow (10.0, db / 20.0); }

BandProfile profileOf (const std::vector<float>& x, double fs)
{
    MagnitudeSpectrum spec (10);
    std::vector<float> power (512, 0.0f), db (512);
    spec.accumulatePower (x.data(), power.data());
    MagnitudeSpectrum::powerToDb (power.data(), db.data(), 512, 1.0f);
    return computeBandProfile (db.data(), 512, 1024, fs);
}
}

//==============================================================================
TEST_CASE ("fft: forward + inverse round-trips")
{
    Fft fft (10);
    std::vector<std::complex<float>> data (1024), original;
    auto x = noise (1024, 1);
    for (int i = 0; i < 1024; ++i) data[(size_t) i] = { x[(size_t) i], 0.0f };
    original = data;
    fft.forward (data.data());
    fft.inverse (data.data());
    double err = 0;
    for (int i = 0; i < 1024; ++i) err = std::max (err, (double) std::abs (data[(size_t) i] - original[(size_t) i]));
    CHECK (err < 1e-5);
}

TEST_CASE ("fft: bin-centred full-scale sine reads 0 dB in the right bin")
{
    const double fs = 48000.0;
    const int bin = 100;
    auto x = sine (bin * fs / 1024.0, fs, 1024, 1.0);
    MagnitudeSpectrum spec (10);
    std::vector<float> power (512, 0.0f), db (512);
    spec.accumulatePower (x.data(), power.data());
    MagnitudeSpectrum::powerToDb (power.data(), db.data(), 512, 1.0f);

    int peak = 0;
    for (int k = 0; k < 512; ++k) if (db[(size_t) k] > db[(size_t) peak]) peak = k;
    CHECK (peak == bin);
    CHECK_NEAR (db[(size_t) bin], 0.0, 0.05);
    CHECK (db[(size_t) bin + 10] < -60.0f);
}

//==============================================================================
TEST_CASE ("lufs: 997 Hz full-scale sine in both channels reads 0.0 LUFS (BS.1770 calibration)")
{
    for (double fs : { 44100.0, 48000.0, 96000.0 })
    {
        LoudnessMeter m;
        m.prepare (fs, 2);
        auto x = sine (997.0, fs, (int) (fs * 5), 1.0);
        feed (m, x, x);
        CHECK_NEAR (m.getIntegratedLufs(), 0.0, 0.05);
        CHECK_NEAR (m.getMomentaryLufs(), 0.0, 0.05);
        CHECK_NEAR (m.getShortTermLufs(), 0.0, 0.05);
    }
}

TEST_CASE ("lufs: EBU Tech 3341 case 1 (stereo 1 kHz at -23 dBFS reads -23 LUFS)")
{
    const double fs = 48000.0;
    LoudnessMeter m;
    m.prepare (fs, 2);
    auto x = sine (1000.0, fs, (int) (fs * 20), dbToGain (-23.0));
    feed (m, x, x);
    CHECK_NEAR (m.getMomentaryLufs(), -23.0, 0.1);
    CHECK_NEAR (m.getShortTermLufs(), -23.0, 0.1);
    CHECK_NEAR (m.getIntegratedLufs(), -23.0, 0.1);
}

TEST_CASE ("lufs: EBU Tech 3341 case 3 relative gate (-36 / -23 / -36 dBFS -> -23 LUFS)")
{
    const double fs = 48000.0;
    LoudnessMeter m;
    m.prepare (fs, 2);
    auto quiet = sine (1000.0, fs, (int) (fs * 10), dbToGain (-36.0));
    auto loud = sine (1000.0, fs, (int) (fs * 60), dbToGain (-23.0));
    feed (m, quiet, quiet);
    feed (m, loud, loud);
    feed (m, quiet, quiet);
    CHECK_NEAR (m.getIntegratedLufs(), -23.0, 0.1);
}

TEST_CASE ("lufs: absolute gate ignores silence; short-term follows the signal")
{
    const double fs = 48000.0;
    LoudnessMeter m;
    m.prepare (fs, 2);
    auto tone = sine (1000.0, fs, (int) (fs * 10), dbToGain (-20.0));
    std::vector<float> silence ((size_t) (fs * 10), 0.0f);
    feed (m, tone, tone);
    feed (m, silence, silence);
    CHECK_NEAR (m.getIntegratedLufs(), -20.0, 0.1);
    CHECK (m.getShortTermLufs() <= kLufsFloor + 1.0f);
}

//==============================================================================
TEST_CASE ("masking: identical spectra overlap fully, disjoint spectra not at all")
{
    const double fs = 48000.0;
    auto a = noise (1024, 3);
    auto lowTone = sine (200.0, fs, 1024, 0.5);
    auto highTone = sine (6000.0, fs, 1024, 0.5);

    auto same = computeMaskingOverlap (profileOf (a, fs), profileOf (a, fs));
    CHECK (same.score > 0.95f);

    auto disjoint = computeMaskingOverlap (profileOf (lowTone, fs), profileOf (highTone, fs));
    CHECK (disjoint.score < 0.05f);

    // Level-independent: a quieter copy still masks fully, and the level
    // difference is reported.
    auto quiet = lowTone;
    for (auto& s : quiet) s *= 0.1f;
    auto scaled = computeMaskingOverlap (profileOf (lowTone, fs), profileOf (quiet, fs));
    CHECK (scaled.score > 0.95f);
    CHECK_NEAR (scaled.levelDifferenceDb, 20.0, 0.5);
    CHECK_NEAR (bandCentreHz (scaled.worstBand), 141.4, 1.0);   // 100-200 Hz band
}

TEST_CASE ("masking: partially overlapping material scores in between")
{
    const double fs = 48000.0;
    auto a = sine (500.0, fs, 1024, 0.5), b = sine (500.0, fs, 1024, 0.5);
    auto hi = sine (8000.0, fs, 1024, 0.5);
    for (size_t i = 0; i < b.size(); ++i) b[i] += hi[i];   // b = same tone + extra high content
    auto r = computeMaskingOverlap (profileOf (a, fs), profileOf (b, fs));
    CHECK (r.score > 0.4f && r.score < 0.6f);
}

TEST_CASE ("energy share below a frequency")
{
    const double fs = 48000.0;
    auto low = sine (60.0, fs, 1024, 0.5);
    CHECK (energyShareBelow (profileOf (low, fs), 120.0f) > 0.9f);
    auto high = sine (3000.0, fs, 1024, 0.5);
    CHECK (energyShareBelow (profileOf (high, fs), 120.0f) < 0.01f);
}

//==============================================================================
TEST_CASE ("mid/side and correlation: mono, anti-phase and decorrelated signals")
{
    auto x = noise (48000, 5);
    std::vector<float> inv (x.size());
    for (size_t i = 0; i < x.size(); ++i) inv[i] = -x[i];
    auto y = noise (48000, 6);

    auto mono = computeMidSide (x.data(), x.data(), 48000);
    CHECK (mono.sideToMidDb <= -99.0f);
    CHECK_NEAR (mono.width, 0.0, 1e-6);
    CHECK_NEAR (computePhaseCorrelation (x.data(), x.data(), 48000), 1.0, 1e-5);

    auto anti = computeMidSide (x.data(), inv.data(), 48000);
    CHECK (anti.sideToMidDb >= 99.0f);
    CHECK_NEAR (anti.width, 1.0, 1e-6);
    CHECK_NEAR (computePhaseCorrelation (x.data(), inv.data(), 48000), -1.0, 1e-5);

    auto wide = computeMidSide (x.data(), y.data(), 48000);
    CHECK_NEAR (wide.sideToMidDb, 0.0, 0.3);
    CHECK_NEAR (wide.width, 0.5, 0.02);
    CHECK_NEAR (computePhaseCorrelation (x.data(), y.data(), 48000), 0.0, 0.03);

    std::vector<float> silent (100, 0.0f);
    CHECK (computePhaseCorrelation (silent.data(), x.data(), 100) == 0.0f);
}

//==============================================================================
TEST_CASE ("delay: integer offsets either direction, with and without PHAT")
{
    const int n = 2048;
    auto src = noise (n + 200, 11);
    DelayEstimator est (n);

    for (int d : { 0, 1, 37, -12, 150 })
    {
        std::vector<float> ref ((size_t) n), tgt ((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            ref[(size_t) i] = src[(size_t) (i + 100)];
            tgt[(size_t) i] = src[(size_t) (i + 100 - d)];   // target delayed by d
        }
        for (bool phat : { true, false })
        {
            auto e = est.estimate (ref.data(), tgt.data(), n, 480, phat);
            CHECK (e.valid);
            CHECK_NEAR (e.lagSamples, d, 0.25);
            CHECK (e.correlation > 0.9f);
            CHECK (! e.polarityInverted);
        }
    }
}

TEST_CASE ("delay: sub-sample offset on band-limited material")
{
    const int n = 2048;
    const double fs = 48000.0, d = 10.5;
    std::vector<float> ref ((size_t) n, 0.0f), tgt ((size_t) n, 0.0f);
    std::mt19937 rng (3);
    std::uniform_real_distribution<double> fr (80.0, 6000.0), ph (0.0, kTwoPi);
    for (int k = 0; k < 40; ++k)
    {
        const double f = fr (rng), p = ph (rng);
        for (int i = 0; i < n; ++i)
        {
            ref[(size_t) i] += (float) (0.02 * std::sin (kTwoPi * f * i / fs + p));
            tgt[(size_t) i] += (float) (0.02 * std::sin (kTwoPi * f * (i - d) / fs + p));
        }
    }
    DelayEstimator est (n);
    auto e = est.estimate (ref.data(), tgt.data(), n, 100, false);
    CHECK_NEAR (e.lagSamples, d, 0.35);
}

TEST_CASE ("delay: polarity inversion and unrelated signals")
{
    const int n = 2048;
    auto a = noise (n, 21);
    std::vector<float> inv ((size_t) n);
    for (int i = 0; i < n; ++i) inv[(size_t) i] = -0.5f * a[(size_t) i];
    DelayEstimator est (n);

    auto e = est.estimate (a.data(), inv.data(), n, 480);
    CHECK (e.polarityInverted);
    CHECK (e.correlation < -0.95f);
    CHECK_NEAR (e.lagSamples, 0.0, 0.25);

    auto b = noise (n, 22);
    auto u = est.estimate (a.data(), b.data(), n, 480);
    CHECK (std::abs (u.correlation) < 0.15f);

    std::vector<float> silent ((size_t) n, 0.0f);
    CHECK (! est.estimate (a.data(), silent.data(), n, 480).valid);
}
