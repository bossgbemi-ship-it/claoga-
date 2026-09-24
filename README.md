# OJU — the vocal chain that listens

**Ojú** is Yoruba for *eye*. Press the brass eye, sing, and OJU listens to the vocal and sets up its whole chain for you. Then it explains in plain English what it heard and what it changed.

Made by Joseph · [madebyjoseph.com](https://madebyjoseph.com) · Bundle ID `com.madebyjoseph.oju`

![OJU](docs/oju.png)

| Platform | Formats |
|---|---|
| Windows 10/11 (x64) | VST3, Standalone |
| macOS 10.13+ (universal: Apple Silicon + Intel) | VST3, AU, Standalone |

Built with C++17, JUCE 8.0.15 (pinned through CMake FetchContent) and CMake. It uses no third-party plugins.

---

## Signal chain

```
Input gain → Low cut → Shape EQ → Tame → Press → Heat → Space → Amount → Output → Ceiling
             24 dB/oct  Body shelf  split-band  soft-knee   2x oversampled   delay + reverb   dry/wet   gain   soft, -0.3 dBFS
                        Mud (moveable) de-esser  comp + GR   asymmetric tube  tempo-synced    latency-
                        Presence    (adjustable  meter       (drive + mix)    Slap … 1/4      compensated
                        Air shelf    frequency) (+ parallel                   filtered repeats
                                                 in Extreme)
```

- Every module has an on/off switch. Switching crossfades over 30 ms, so it never clicks.
- All parameters are smoothed. Filters are topology-preserving SVFs that can move while audio plays without zipper noise.
- The latency is 4 samples at every sample rate and is reported to the host. The dry path of Amount and of Bypass is delayed by the same amount, so blends never comb-filter.
- Bypass is exposed to hosts as the plugin's bypass parameter.

## The brain

1. **Listen.** When you press the eye, the audio thread copies the input into a lock-free FIFO. A background thread runs a voice-activity gate with an adaptive noise floor, so **only moments where the artist is actually singing count**.
   - **Natural** needs about 4 s of singing and uses a 2048-point FFT. Its moves are gentle.
   - **Extreme** needs about 10 s and uses an 8192-point FFT with 75 % overlap. It detects sibilance from percentiles (the spectrum of the top 10 % most sibilant frames), makes stronger moves, and adds a parallel compression stage.
2. **Measure.** The brain measures:
   - Level: RMS percentiles p10/p50/p90/p95 of the voiced frames
   - Dynamics and crest factor
   - Rumble
   - Mud and boxiness: how much build-up, and the exact resonant frequency
   - Presence and harshness
   - Sibilance: severity and the S frequency
   - Air
   - Host tempo

   Tone is measured on non-sibilant frames, so S's aren't mistaken for air.
3. **Decide.** The brain maps these measurements onto targets for the selected style: **Afrobeats, Trap, R&B, Pop or Soul**. Every value is sent through the host with begin/end change gestures, so it is automatable and undoable.
4. **The Read.** OJU shows 4–6 lines, for example *"Muddy around 300 Hz — cut 6.0 dB"*, *"Sharp S's at 7.5 kHz — Tame set to 71%"* or *"Wide dynamics — Press at 3.5:1"*, plus one idea for the artist.

Changing the style or mode later re-targets from the last read without listening again. The read is saved with the session.

**A/B**: the button switches between two complete settings. The first switch copies A into B.

## Performance

Measured on one core at 48 kHz stereo with 256-sample blocks: **about 2 % CPU** in both Natural and Extreme, which is roughly 50× real time.

- Nothing in the audio callback allocates memory, takes a lock or touches a file. The test runner enforces this with an allocation guard.
- The analysis runs entirely off the audio thread.

---

## Building

### Windows

1. Install **Visual Studio 2022 or newer** with the *Desktop development with C++* workload. CMake comes with it.
2. Install **Git** from git-scm.com. CMake uses it to download JUCE.
3. Open the **x64 Native Tools Command Prompt for VS**, `cd` into this folder, and run:

```bat
build-windows.bat
```

The first build downloads JUCE and takes a few minutes. The output is:

```
build-win\OJU_artefacts\Release\VST3\OJU.vst3        ← the plugin (a folder)
build-win\OJU_artefacts\Release\Standalone\OJU.exe   ← the standalone app
```

To install in one step, run `build-windows.bat install` from an **Administrator** prompt. It copies `OJU.vst3` to `C:\Program Files\Common Files\VST3\`.

The plugin links the MSVC runtime statically, so users don't need the VC++ redistributable.

### macOS

1. Install **Xcode** from the App Store and open it once. The Command Line Tools also work.
2. Install **CMake**: `brew install cmake`.
3. In Terminal, from this folder, run:

```bash
./build-mac.sh            # build a universal Release (arm64 + x86_64)
./build-mac.sh install    # build, then copy VST3 + AU into ~/Library/Audio/Plug-Ins
```

The output is:

```
build-mac/OJU_artefacts/Release/VST3/OJU.vst3
build-mac/OJU_artefacts/Release/AU/OJU.component
build-mac/OJU_artefacts/Release/Standalone/OJU.app
```

To check the AU, run `auval -v aufx Ojue Mbyj`.

### Offline or a pinned JUCE checkout

```bash
cmake -S . -B build -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE-8.0.15
```

---

## Installing in your DAW

| DAW | Where the plugin goes | Then |
|---|---|---|
| **Reaper** (Win) | `C:\Program Files\Common Files\VST3\OJU.vst3` | *Options → Preferences → Plug-ins → VST → Re-scan*. Insert **VST3: OJU (Made by Joseph)** on the vocal track's FX chain. |
| **FL Studio** (Win) | `C:\Program Files\Common Files\VST3\OJU.vst3` | *Options → Manage plugins → Find more plugins* (tick *Verify plugins*). Star **OJU**, then load it in a Mixer insert slot on the vocal. |
| **Ableton Live** | Win: `C:\Program Files\Common Files\VST3\` · Mac: `~/Library/Audio/Plug-Ins/VST3/` | *Settings → Plug-Ins → turn on "Use VST3 Plug-in System Folders" → Rescan*. |
| **Logic Pro** | `~/Library/Audio/Plug-Ins/Components/OJU.component` | Logic scans it on launch. Use *Audio FX → Audio Units → Made by Joseph → OJU*. If it doesn't appear, open *Plug-in Manager* and click *Reset & Rescan Selection*. |
| **Reaper / Ableton** (Mac) | `~/Library/Audio/Plug-Ins/VST3/OJU.vst3` (or the AU) | Rescan as above. |

## Using it

1. Put OJU on the vocal track and pick a **style** and a **mode**.
2. Press the **eye**, then play the vocal or sing. The ring fills only while there is singing. Press the eye again to stop.
3. Read **The Read**, then tweak anything by hand. Every knob is a normal automatable parameter.

Tips:
- **The host must be sending audio.** In Logic, press play or record-arm/input-monitor the track. In the other DAWs, play the take or arm the track.
- **In Standalone**, JUCE mutes the input by default to prevent speaker feedback. The first tap on the eye tells you so. Put headphones on and tap again, and OJU unmutes and listens.
- Double-click any knob to reset it. Drag vertically or horizontally, or use the scroll wheel.
- The window resizes at a fixed aspect ratio, and everything is drawn as vectors, so it stays sharp at any size.

---

## Tests and validation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DOJU_BUILD_TESTS=ON
cmake --build build --target OJUTests
build/OJUTests_artefacts/Release/OJUTests <folder-for-screenshots>
```

The runner uses a synthetic singer: a gliding harmonic voice with formants, a planted 320 Hz mud resonance, 7.2 kHz sibilant bursts, breaths and room noise. It checks:

- Natural and Extreme Listen end-to-end: it finds the mud and the S frequency, gain-stages the level, and writes a 4–6 line Read.
- 3,000 ragged blocks (1 to 4,096 samples, including blocks larger than prepared) while style, mode, module, bypass and sample rate change and Listen starts and stops. The output must never contain NaN or Inf and must never exceed −0.3 dBFS.
- **Zero heap allocations inside `processBlock`**, enforced by a global `operator new` guard.
- Reported latency, sample-exact dry alignment, and a phase-coherent Amount blend.
- CPU cost, state save/restore (including The Read, the A/B slots and re-targeting after reload), mono tracks, and editor screenshots at three sizes.

**pluginval** at strictness level 10 passes on the VST3. CI (`.github/workflows/build.yml`) builds Windows, macOS (universal, with `auval`) and Linux, and runs the tests and pluginval on each. It also fails the build on any compiler warning in OJU's own code. Every run uploads the built plugins as artifacts.

## Project layout

```
Source/
  PluginProcessor.*     parameters, processBlock, brain → host gestures, A/B, state
  Parameters.*          every parameter ID, range and display format
  dsp/VocalChain.*      the whole real-time chain (+ Svf.h)
  brain/Listener.*      lock-free capture, voice gate, FFT analysis (background thread)
  brain/Brain.*         measurements → settings + The Read (pure, deterministic)
  ui/                   look and feel, brass eye, EQ curve, meters, cards, editor
Tests/OjuTests.cpp      headless test runner
docs/SHIPPING.md        what you still need before selling
```

## Licence notes

JUCE is dual-licensed: AGPLv3 or a commercial JUCE licence. **To sell OJU as closed source you need a JUCE licence**. The free *Starter* tier covers you up to its revenue limit; check the current terms at juce.com. See `docs/SHIPPING.md`.
