#pragma once

// The fixed-layout record a Listener instance publishes to the Master Engine.
//
// It lives inside shared memory, so it must be trivially copyable, standard
// layout, contain no pointers and have the same layout in every plugin binary
// that opens the bus (enforced by kPayloadVersion + sizeof checks at open time).

#include <cstdint>
#include <type_traits>

namespace aimix
{
constexpr int kFftSize           = 1024;
constexpr int kNumFftBins        = kFftSize / 2;   // 512-bin magnitude spectrum
constexpr int kHopSize           = 2048;           // samples per payload (~43 ms @ 48 kHz)
constexpr int kSnapshotSize      = kHopSize;       // mono time-domain snapshot for cross-correlation
constexpr int kMaxTrackNameChars = 32;
constexpr uint32_t kPayloadVersion = 1;

enum class TrackRole : uint8_t
{
    Unknown = 0, Vocal, Kick, Snare, Drums, Bass, Guitar, Keys, Synth, Fx, MasterBus,
    NumRoles
};

const char* toString (TrackRole role) noexcept;

enum PayloadFlags : uint32_t
{
    kFlagTimelineValid = 1u << 0,  // timelineSample is a host timeline position (transport playing)
    kFlagPartialFrame  = 1u << 1,  // frame started after a seek; leading samples are zero padding
    kFlagSilent        = 1u << 2,  // peak below -90 dBFS for the whole hop
    kFlagMasterBus     = 1u << 3,  // published by the Master Engine's own mix-bus analyser
};

struct alignas (64) AnalysisPayload
{
    // --- identity / timing -------------------------------------------------
    uint32_t  version;              // kPayloadVersion
    uint32_t  trackId;              // stable per Listener instance (bus slot index)
    uint64_t  sequence;             // monotonically increasing per track
    int64_t   timelineSample;       // host timeline position of monoSnapshot[0]
    double    sampleRate;
    uint32_t  flags;                // PayloadFlags
    uint32_t  numChannels;          // 1 or 2 (analysed channels)
    TrackRole role;
    uint8_t   reserved[7];
    char      trackName[kMaxTrackNameChars];   // NUL-terminated UTF-8

    // --- level ---------------------------------------------------------------
    float rmsDb[2];                 // per channel, over the hop
    float peakDb[2];                // per channel sample peak, over the hop

    // --- stereo --------------------------------------------------------------
    float phaseCorrelation;         // -1 .. +1 (Pearson, zero-mean assumed)
    float midEnergy;                // mean square of (L+R)/2 over the hop
    float sideEnergy;               // mean square of (L-R)/2 over the hop

    // --- loudness (ITU-R BS.1770-4 / EBU R128) -------------------------------
    float momentaryLufs;
    float shortTermLufs;
    float integratedLufs;

    // --- spectrum ------------------------------------------------------------
    float fftMagnitudeDb[kNumFftBins];   // Hann-windowed, averaged over the hop, 0 dB = full-scale sine

    // --- time domain ---------------------------------------------------------
    float monoSnapshot[kSnapshotSize];   // (L+R)/2 for inter-track delay estimation
};

static_assert (std::is_trivially_copyable_v<AnalysisPayload>, "payload is memcpy'd across processes");
static_assert (std::is_standard_layout_v<AnalysisPayload>,    "payload layout must be predictable");
static_assert (sizeof (AnalysisPayload) % 64 == 0,           "payload should fill whole cache lines");

} // namespace aimix
