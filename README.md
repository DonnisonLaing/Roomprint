# Roomprint

A VST3 effect that makes a dry drum or instrument sound like it was recorded in a space.
It chains the things you were stacking by hand (Cab, Smart Reverb distance, tape/tube
saturation) behind one **Distance** macro.

```
In > Source (mic/cab) > Saturation > Distance engine > Room (ER + FDN tail) > Glue > Out
                                         |-- direct path (air absorption + level) --^
```

## Build

You need CMake 3.22+ and a C++17 compiler (Xcode on macOS, Visual Studio 2022 on Windows).
The first configure downloads JUCE 8.0.4 from GitHub.

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release --parallel

Output: `build/Roomprint_artefacts/Release/VST3/Roomprint.vst3`

Install by copying it to
- Windows: `C:\Program Files\Common Files\VST3`
- macOS: `~/Library/Audio/Plug-Ins/VST3` (macOS may quarantine an unsigned build:
  `xattr -dr com.apple.quarantine Roomprint.vst3`)

Ableton Live: Preferences > Plug-Ins > rescan. Put it on a track, or on a return with Mix at 100%.

No local toolchain? Push this folder to GitHub: the workflow in `.github/workflows/build.yml`
builds Windows and macOS VST3s and attaches them as downloadable artifacts.

DSP-only tests (no JUCE, ~3 s):

    cmake -S . -B build_dsp -DROOMPRINT_DSP_ONLY=ON && cmake --build build_dsp && ctest --test-dir build_dsp

## Controls

| Section | Control | What it does |
|---|---|---|
| Source | Voicing | Close / Ribbon / Room Mic / Radio / Cab. Filter curves + a short mic-offset comb |
| | Low Cut, High Cut | Pre-room filtering |
| | Resonance | Boost at the voicing's speaker/mic resonance |
| | Proximity | Low-end build-up of a close mic; fades as Distance rises |
| Saturation | Type | Tape (HF-weighted, head bump), Tube (asymmetric, sags), Transformer (LF-weighted) |
| | Position | Before or after the room |
| | Drive, Bias, Tone, Blend | Amount, even-harmonic asymmetry, tilt, parallel mix |
| Distance | Distance | Direct level down, early/late balance, pre-delay up, air absorption closes |
| | Depth | Scales how far the macro reaches |
| Room | Material | Wood / Concrete / Padded / Tiled: wall absorption and tail brightness |
| | Size | Shoebox from a small booth to a large hall (0.35x to 3.15x scale) |
| | Decay | Mid-band RT60, 0.1 to 3 s |
| | Diffusion, Width | Tail density, stereo width of the room return |
| | Low/High Cut | Filter the room return only |
| | Duck | Room dips under the input so transients stay clear |
| | Squash | Fast compressor on the room return (the "room mic pump") |
| Output | Oversampling | Off / 2x / 4x for the saturator. Latency is reported to the host |
| | Mix, Output | Dry/wet and trim |

## What's been verified

Verified in a sandbox without audio hardware or a DAW:
- DSP engine: stability under extreme and randomly automated settings, finite and bounded output,
  bit-identical output across host block sizes, exact dry-path latency alignment, aliasing falls
  with oversampling, RT60 tracks the Decay control within about 15%, material ordering,
  clean under ASan/UBSan, zero warnings with `-Wconversion -Wsign-conversion`.
- Plugin shell: type-checks against JUCE 8.0.4 headers.

Not yet done: a full linked VST3 build, loading in Ableton, and listening tests. Expect to
tweak level mapping and voicing curves by ear. They live in `Source/dsp/Engine.h`
(`mapDistance`) and `Source/dsp/SourceStage.h` (`getVoicing`).

## Layout

    Source/dsp/        framework-independent engine (Engine, Room, Saturator, SourceStage, Oversampler)
    Source/Plugin*.cpp JUCE processor, parameters, editor
    Tests/             DSP test suite
