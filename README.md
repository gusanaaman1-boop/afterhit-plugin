# AFTERHIT

A drum insert that keeps the hit sharp and close, and lets a space bloom
*after* its attack and close again before the next hit.

Four controls — **HIT · SPACE · TAIL · GATE** — plus an ADVANCED drawer
(AFTER, SENSITIVITY, TONE, WIDTH, SYNC, OUTPUT). VST3, AU and Standalone,
mono or stereo, zero latency. C++20 / JUCE 9, native JUCE editor.

- Parameters, IDs and presets: [docs/PARAMETERS.md](docs/PARAMETERS.md)
- Audio path, mappings, measurements, limitations: [docs/DESIGN.md](docs/DESIGN.md)
- Test and validation record: [docs/TEST-REPORT.md](docs/TEST-REPORT.md)

## Build — macOS

Needs Xcode command-line tools, CMake ≥ 3.22 and JUCE 9 at `~/JUCE`
(or checked out into `./JUCE`).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

This builds and installs `AFTERHIT.vst3` / `AFTERHIT.component` into
`~/Library/Audio/Plug-Ins/` (turn that off with `-DAH_COPY_AFTER_BUILD=OFF`).
Universal binary (arm64 + x86_64), minimum macOS 10.13.

## Build — Windows

Visual Studio 2022 (Desktop C++), CMake ≥ 3.22, JUCE 9 at `%USERPROFILE%\JUCE`
or `.\JUCE`:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --target AfterHit_VST3 AfterHit_Standalone AfterHitTests AfterHitHostTests
build\AfterHitTests_artefacts\Release\AfterHitTests.exe
build\AfterHitHostTests_artefacts\Release\AfterHitHostTests.exe
```

Copy `build\AfterHit_artefacts\Release\VST3\AFTERHIT.vst3` to
`C:\Program Files\Common Files\VST3\`. The same steps run in CI:
`.github/workflows/windows.yml` (manual trigger or a `v*` tag).

## Checks

```bash
build/AfterHitTests_artefacts/Release/AfterHitTests          # DSP measurements
build/AfterHitHostTests_artefacts/Release/AfterHitHostTests  # host contract + editor
packaging/run-pluginval.sh                                   # pluginval, strictness 10
auval -v aufx Afht Naam                                      # Apple AU validation
build/AfterHitShot_artefacts/Release/AfterHitShot ui-shots   # render the editor to PNG
build/AfterHitRender_artefacts/Release/AfterHitRender in.wav out.wav "Wide Clap" space=35
```
