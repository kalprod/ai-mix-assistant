# Host compatibility and validation

Last run: 2026-10-06, Ubuntu 24.04 container (Intel Xeon 2.1 GHz, 4 vCPU), GCC 13.3, JUCE 8.0.10, Release build. Raw outputs are in `validation/`.

## Verified here

| Check | Result | Evidence |
|---|---|---|
| Core unit tests (ring buffer, shm bus across a real `fork()`, LUFS vs. EBU Tech 3341, masking, M/S, delay, analyser, role detection, rules, end-to-end) | 48/48 pass | `validation/unit_tests.txt` |
| Audio thread allocates nothing (core analyser, 2000 blocks; full JUCE `processBlock`, 3000 random-size blocks) | 0 allocations | `unit_tests.txt`, `plugin_checks.txt` |
| `processBlock` output bit-identical to input (random sizes 1..2048, seeks, transport start/stop) | pass | `plugin_checks.txt` |
| Bus layouts: mono and stereo accepted, mono→stereo and 5.1 rejected; zero latency, zero tail | pass | `plugin_checks.txt` |
| State save/restore (mode, role, track name), garbage state ignored | pass | `plugin_checks.txt` |
| Three real `AIMixProcessor` instances (2 Listeners + Master) exchanging data over the shared-memory bus | pass | `plugin_checks.txt` |
| **pluginval** v1.0.4, strictness 10, VST3 (Linux) | SUCCESS | `validation/pluginval.txt` |
| **Steinberg VST3 validator** (vst3sdk master, built from source) on the `.vst3` bundle | 47/47 pass | `validation/vst3_validator.txt` |
| Editor renders in both modes at default and minimum size (headless, Xvfb) | pass | `validation/ui_*.png` |

pluginval's own "vst3 validator" step was run separately: when pluginval passes the validator the inner `.so` path, current SDK validators reject it as "not a module directory". That is a pluginval/SDK path mismatch on Linux, not a plugin defect, and the validator passes when given the bundle.

## Not verified here (needs your machine)

| Item | Why not | How to verify |
|---|---|---|
| **AU** | macOS only | `auval -v aufx Aima Aimx`, then `pluginval --strictness-level 10` on the `.component`. Test in Logic Pro and in GarageBand (AU sandbox). |
| **AAX** | Needs the Avid AAX SDK (developer agreement) and PACE wrapping for Pro Tools retail | Build with `-DAIMIX_AAX_SDK_PATH`, run the AAX Validator (`aaxval`) and test in Pro Tools Developer build. |
| **Windows VST3** | No Windows toolchain here | Build with MSVC 2022; the shared-memory path uses `CreateFileMapping` (`Local\aimix_bus_v1`) and is compiled but not executed in this container. Run pluginval + VST3 validator. |
| **macOS VST3** | macOS only | Same as above; also check that `shm_open` is permitted when the host is sandboxed (see below). |
| Real DAWs | No hosts here | Recommended matrix: Ableton Live, Logic Pro, Pro Tools, Cubase/Nuendo, Reaper (incl. "run plugins in separate process"), Bitwig (per-plugin sandbox), FL Studio, Studio One. |

## Host behaviours the design accounts for

* **Plugin sandboxing / separate processes** (Bitwig, Reaper bridging, AU out-of-process): the bus lives in named shared memory, so Listeners in other processes still reach the Master. If `shm_open` is refused (macOS App Sandbox without an app-group name) the instance falls back to an in-process bus, which still works for hosts that load all plugins in one process. The Listener UI shows which backend is active.
* **macOS App Sandbox (AUv3, GarageBand)**: shm names must start with an app-group identifier (`<TeamID>.group/...`). Not implemented yet; tracked in the plan.
* **Crashed instances**: a slot whose owning process is dead is reclaimed when the bus is full (PID liveness check).
* **Two Master Engines**: only one owns the bus; the second shows a passive banner and takes over if the first stops heart-beating for 3 s.
* **Inserts are pre-fader** in most hosts, so the Listener does not see fader moves. Rules therefore never compare track balance across faders; levels are about gain staging, clipping and loudness only.
* **Bypass**: bypassed instances stop sending frames; their strip dims to "no signal" after 2 s and they drop out of the rules.
* **Offline bounce / faster than real time**: rings hold about 350 ms of frames; extra frames are dropped (counted in the Listener UI), never blocking the audio thread.
* **Timeline alignment**: hops are aligned to the host timeline grid while the transport plays, so cross-track delay estimation compares the same musical samples. Hosts that report no playhead position still get level/spectrum analysis but no time-alignment suggestions.

## CPU footprint (from `validation/cpu_bench.txt`)

* Listener: about **0.12 % of one core** per stereo instance at 48 kHz, half that when no Master is running (the FFT is skipped when nobody is listening). The two FFTs of a hop run in different process calls so no single block carries both.
* Master Engine (background thread, 15 Hz): about **1 % of a core with 9 tracks, 3 % with 32, 7 % with 63**.
* Worst-case per-block times in the benchmark (up to ~0.3 ms) are dominated by scheduler preemption in this shared VM; they are higher for the orphan case, which does strictly less work. Measure worst case on dedicated hardware before quoting it.
