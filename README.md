# AI Mix Assistant (JUCE · VST3 / AU / AAX)

One plugin binary, two modes:

* **Listener**: insert on every track. Measures the track (audio passes through bit-identical) and streams one analysis frame every 2048 samples to the Master Engine over a lock-free bus.
* **Master Engine**: insert on the mix bus. Collects every Listener, compares tracks against each other and the mix, and shows a channel rack plus diagnostic cards with step-by-step fixes.

![Master Engine view](docs/validation/ui_master.png)

## Layout

| Path | What |
|---|---|
| `core/` | JUCE-free C++17: bus, DSP, rule engine. Builds and tests with nothing but a compiler. |
| `core/include/aimix/AnalysisPayload.h` | The shared struct: track ID, RMS, peak, 512-bin FFT, phase correlation, M/S energy, LUFS, mono snapshot. |
| `core/include/aimix/SpscRingBuffer.h`, `SharedBus.h` | Phase 1: wait-free SPSC rings in a 64-slot shared-memory bus. |
| `core/src/Loudness.cpp`, `SpectralAnalysis.cpp`, `StereoAnalysis.cpp`, `DelayEstimator.cpp` | Phase 2: BS.1770 LUFS, Bark-band masking, Mid/Side, GCC-PHAT time offsets. |
| `core/src/TrackAnalyzer.cpp` | Listener audio-thread analyser (no allocation, const input). |
| `core/src/RuleEngine.cpp`, `MixEngine.cpp` | Phase 3: Master Engine thread and heuristic rules producing structured `Suggestion`s (JSON-serialisable). |
| `plugin/` | Phase 4: `AudioProcessor`, editor, `ui/ChannelRack`, `ui/DiagnosticCards`. |
| `tests/`, `bench/`, `tools/PluginChecks.cpp` | Phase 5: 40 unit tests, CPU benchmark, host-like checks + UI snapshots. |
| `docs/HOST_COMPATIBILITY.md` | What was validated, and the checklist for macOS/Windows/AAX hosts. |
| `docs/validation/` | Raw outputs of every check from the last run. |

## Build

```bash
# Core + tests + benchmark only (no JUCE needed)
cmake -S . -B build -DAIMIX_BUILD_PLUGIN=OFF
cmake --build build && ./build/aimix_tests && ./build/aimix_bench --quick

# Plugin (fetches JUCE 8.0.10). VST3 + Standalone everywhere, AU on macOS.
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target AIMixAssistant_All aimix_plugin_checks

# AAX: needs the Avid AAX SDK and PACE signing for Pro Tools release builds
cmake -S . -B build -DAIMIX_AAX_SDK_PATH=/path/to/aax-sdk
```

Use `-DAIMIX_JUCE_PATH=/path/to/JUCE` to build against a local JUCE checkout.

## Using it

1. Insert **AI Mix Assistant** on each track, ideally last in the chain. Set **Role** (Vocal, Kick, Bass...): roles decide which track yields when two of them clash.
2. Insert one more instance on the mix bus and set **Mode** to **Master Engine**.
3. Play the session. Cards appear after a condition has held for about 0.2 s and stay for about 0.7 s after it clears. Click a strip to filter cards to that track; **Dismiss** hides a card for the session.

Track names come from the host where it supports it (VST3, AU); the **Track** field overrides it.
