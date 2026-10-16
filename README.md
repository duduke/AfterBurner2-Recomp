# AfterBurner2-Recomp

A playable, work-in-progress C++ reimplementation of SEGA's **After Burner II**
(1987), with optional modern presentation and control enhancements. The original
main/sub 68000 code is translated into C++; the original sound Z80 program runs
with reimplemented YM2151/SegaPCM devices. SDL3 handles the desktop frontend.
Independent MAME captures check the translated game against the arcade ROM.
Inspired by [Cannonball](https://github.com/djyt/cannonball).

Supply your own [ROM dump](roms/README.md). ROMs, original artwork and personal
soundtracks are not distributed. This project is not affiliated with SEGA.
See [LICENSE.txt](LICENSE.txt) and [third-party notices](docs/THIRD-PARTY-NOTICES.md).

## Current state

The native build supports cold boot, attract mode, coin/start, flight, weapons,
HUD, original audio, stage transitions and pause/restart. All 23 directed stage
slots have scripted coverage. A 74,452-frame Easy / 99-credit recording reaches
the ending and matches the retained MAME transition observations.
A complete playthrough has also been reported on macOS with Normal difficulty
and three ships; an input recording and independent comparison are still needed.

**Full-game acceptance remains open:** recorded standard single-credit completion,
continuous whole-credit CPU/device/RGB/audio comparison and sustained peak-load
performance still need verification. Xbox Bluetooth body and both trigger motors are physically confirmed on macOS;
other controllers/HOTAS, Windows/Linux hardware and GPU backends beyond the
recorded Metal/software checks remain open. See the
[roadmap checkpoint](docs/ROADMAP.md#current-checkpoint-2026-10-16) for scope.

Preview 2 adds Windows cooperative CPU fibers, checked release LTO/optimization,
precise frame-deadline waits, OpenAL Soft `alSourceRewindv` stream-restart fixes
and cross-platform CPU/video/widescreen/GPU-filter optimizations. Both the Apple
Silicon macOS and Windows x64 packages include SDL3 and OpenAL Soft 1.25.2
spatial audio. Windows x64 runtime execution, Vulkan GPU sprite filters,
fullscreen switching, OpenAL headphone HRTF and sustained `59.6374` FPS pacing
(Stage 1 and Stage 5 with 16:9 widescreen, Corrected ground, ScaleFX 3x,
translucency and Integer/Pixel-Art scaling) are now verified on Windows 11.
The macOS arm64 package on the release predates the Windows runtime/performance
commit; rebuilding and re-running the full macOS regression/oracle suite on the
updated source tree is the next step on the macOS side. See
[release notes](docs/RELEASE_NOTES.md).

The widescreen work expands the experimental 16:9 view beyond the original sprite
queue and depth-bucket limits, retains scenery as it crosses the wider edges,
and fixes reproduced rolling scenery, takeoff wave coverage and attract-cloud
corner artifacts. It also fixes the reported enemy-coordinate overflow stop
at PC `$15C10`. The original simulation remains the comparison reference;
the wider presentation still needs exhaustive scene and platform validation.

## Current enhancements

| Area | Implemented features |
| --- | --- |
| Menus | Arcade-font Enter Arcade/Options/Quit and in-game pause menu; Video, Controls, Audio and DIP Switches submenus; keyboard, mouse and controller navigation; slim scrollbars show hidden items and position |
| Widescreen | Experimental 16:9 Preview (398×224): additional road/tiles, recovered hidden sprites and retained side scenery with live roll projection; extended horizon and takeoff wave field; dynamic presentation lists recover requests dropped by native queue/depth limits; centered HUD and existing effects; Original remains the default |
| Display | Nearest, Linear, Integer and supported Pixel Art window scaling; fullscreen; FPS and frame/core/filter/presentation timing |
| Sprite filters | Off, EPX, Scale3x, 2xSaI, Super2xSaI and ScaleFX; SDL GPU compute/composition with a CPU/software fallback |
| Ground/horizon | Optional Corrected mode removes misplaced ground-color wedges during rolls; widescreen also extends verified ground/sea fill behind translucent clouds and handles clipped horizon edges. Original remains the default |
| Translucency | Independent Clouds, Missile Trails, Explosions and Afterburner settings; player flame is translucent only at full throttle during flight; crash fire/smoke included, aircraft/debris remain opaque |
| Controls | Keyboard, gamepad and raw joystick remapping; axes/buttons/hats; deadzone, sensitivity, polarity and full-axis throttle; hotplug and persisted device profiles |
| Controller feedback | Optional SDL body and Xbox trigger rumble; cannon, missile, player-impact and full-throttle cues; strengths, motor tests, device selection and lifecycle stops |
| Arcade settings | Lives/Credit (3 or 4), Ship Increase (credit-multiplied lives) and Difficulty; Apply & Restart, F5 or next launch applies saved DIP choices |
| Audio | Six WAV/FLAC track replacements, preview, live current-song switching, Original fallback; independent music/effects/per-track volume; replacement music balanced at -12 dB |
| Spatial audio (experimental) | Optional OpenAL Soft: four virtual cockpit speakers with headphone HRTF, 2.1/5.1 profiles, chip-rate interpolation, output-device selection and speaker test; Original SDL remains default |
| Preferences and diagnostics | Saved video/input/audio/feedback/DIP preferences, physical-input recording, bounded launch checks and performance reporting |

Presentation enhancements preserve the native board output as the regression
reference. Effects default to Opaque, music to Original and feedback to Off.
Force-feedback stick effects, incoming-warning cues, complete widescreen visibility,
replacement sprite assets, CRT shaders, save states and new gameplay modes
remain **planned**. The [haptics notes](docs/HAPTICS_PLAN.md) cover current SDL
output and remaining platform hardware checks.
Choose **Options → Video → Viewport → 16:9 Preview** to try the wider view.
The wider view now retains side scenery and recovers hidden sprite requests.
Choose **Ground → Corrected** for full-width sky correction during rolls.
The wider renderer now recovers requests discarded by the original 192-request
queue and full depth buckets, and renders a dynamic descriptor list. Native
object allocation and gameplay limits stay unchanged. All 23 stage slots and
recorded lifecycle paths have dedicated checks; exhaustive scene/platform
validation, 16:10 and ultrawide remain open. See the
[true widescreen plan](docs/WIDESCREEN_PLAN.md) for progress and remaining gates.

## Build and play

Download [v0.1.0-preview.2](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.2)
and extract the complete ZIP for your platform. Put your own extracted ROM
files in its `roms/` directory. Sources, notices and SHA-256 files accompany
the packages; ROMs, captures and personal settings are excluded.

The **Apple Silicon / macOS 26+** package opens with `Launch.command`.
SDL3 and OpenAL Soft are included, along with their
sources and notices. The preview is ad-hoc signed and **not notarized**; macOS
may require approval in Privacy & Security. Intel Macs are not included in this
build. See the package's `START-HERE.txt` for setup.

The **Windows 10/11 x64** package opens with `Launch.cmd`; keep `SDL3.dll` and
`OpenAL32.dll` beside the EXE. Run `Check-Continuations.cmd` for the ROM-free
fiber/worker checks, then `Launch.cmd --profile` for backend and timing output.
This unsigned package is built and runtime-verified on Windows x64 with Vulkan
GPU sprite filters, original SDL stereo, replacement music and OpenAL Soft 1.25.2
spatial audio (HRTF, 2.1 and 5.1 profiles).

Requirements: your ROM dump, SDL3 **3.4+**, a C++20 compiler, and either Make with
`pkg-config sdl3` or CMake with the installed `SDL3::SDL3` package. Run from the
repository root so `roms/` and `soundtrack/` resolve correctly.

```sh
make native-release
./build/afterburner2-release
```

This opens the initial menu. Choose **Enter Arcade** to watch the original boot
and attract sequence, then insert a coin and press Start (keyboard **5**, then
**Enter**). `make play-native` uses the same startup with performance reporting.
For the explicit automatic coin/start shortcut, use
`./build/afterburner2-release --boot-to-flight`.

CMake alternative:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake -j4
./build/cmake/afterburner2
```

CMake enables checked LTO and release optimization for Release and RelWithDebInfo
builds. For Windows with GCC/MinGW and Ninja, select Release explicitly (retain
your SDL3 installation's `SDL3_DIR` or `CMAKE_PREFIX_PATH` if needed):

```sh
cmake -S . -B build/windows-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows-release --target afterburner2 cpu_continuation_regression cpu_continuation_thread_regression -j4
./build/windows-release/afterburner2.exe --profile
ctest --test-dir build/windows-release -R "^cpu_continuation(_thread)?_regression$" --output-on-failure
```

Multi-configuration generators require `--config Release` at build time; their
default build is Debug. Use a compiler that supports the engine requirements
below.

Use the output directory selected by your generator (single-configuration builds
place the executable directly in the chosen build directory). `-DAB2_ENABLE_IPO=OFF` disables
LTO for toolchains without compatible linker support. The existing engine uses
`unsigned __int128`; a Windows compiler must support it, such as Clang targeting
x64. These changes do not establish MSVC compiler compatibility.

Windows uses cooperative fibers for emulated CPU continuations. `--profile`
reports `continuation=windows-fiber`, the renderer/backend, and `filters=gpu`
or `filters=cpu`. GPU filters use the embedded Vulkan/SPIR-V shaders on Windows;
the SDL3 build and graphics driver must support Vulkan. Direct3D 12 compute
shaders are not included. If GPU compute initialization fails, SDL tries its
available renderers, including accelerated presentation, with CPU filters.
Compare the same stage, video settings and audio settings when profiling.

Native runtime checks are currently recorded on macOS; cross-platform build and
physical-device acceptance must be established separately.

## Controls

| Keyboard default | Action |
| --- | --- |
| Arrow keys | Fly / bank / pitch |
| Z / X | Cannon / missiles |
| Space / Left Shift | Increase / reduce throttle |
| 5 / Enter | Coin / start |
| P / F5 | Pause / cold restart |
| Esc or F1 | Open / close menu |
| F11 / F3 | Fullscreen / cycle stats |

Gamepads default to left-stick flight, South/East weapons, triggers for throttle
and Back/Start for coin/start. Raw joysticks default to axes 0/1 and buttons 0–5.
Remap in **Options → Controls**: Enter captures, Backspace clears and Escape
cancels/returns. For throttle sliders, choose Full Axis and bind Boost toward
maximum; this replaces Brake. Identical device models share a GUID/type profile.

Enable rumble in **Options → Controls → Feedback**. Choose the output device,
turn Enabled On, and adjust body/trigger strengths independently. Motor tests
work with gameplay feedback Off. See the [motor mapping table](docs/HAPTICS_PLAN.md#motor-mapping)
for left/right body and trigger assignments. Cannon uses the right trigger and
missiles the left; Swap Triggers reverses them. Xbox Bluetooth body and both trigger
motors are physically confirmed on macOS; Windows/Linux hardware checks remain
open. Unsupported devices retain normal controls.

## Custom music

Put WAV/FLAC files directly in `soundtrack/`, then choose them in
**Options → Audio → Music Tracks**. The six tracks are Maximum Power, Final Take
Off, Red Out, Super Stripe, After Burner and City 202. Left/Right cycles choices;
Space or gamepad North previews while paused.

A loaded selection for the current song applies on resume and starts the file
from the beginning. Other mappings apply at their native cue. Music, effects and
track volumes range from 0–100%; gameplay and preview use the same -12 dB
replacement baseline. Original at 100% preserves exact native PCM. See
[music behavior and coverage](docs/MUSIC_REPLACEMENT_PLAN.md).

For optional spatial playback, open **Options → Audio → Output** and choose
Headphones HRTF, 2.1 Speakers or 5.1 Speakers. Interpolation lists the resamplers
supported by OpenAL Soft. Speaker Test plays front-left, front-right, rear-left,
rear-right. HRTF/actual output status appears below the options; unsupported
output falls back explicitly. 2.1 uses your speakers/receiver's bass management;
5.1 needs a configured surround device. Passing sounds and several sequence cues
still use direct stereo pending classification; music uses direct stereo by design.
Physical headphone localization and speaker-channel checks on Windows, Linux and
macOS remain open.

Build the optional dependency with `tools/audio/build_openal_soft.sh`, then
rebuild the game. CMake can also use an installed OpenAL Soft package. SDL-only
builds remain supported. See [spatial audio implementation and remaining
acceptance](docs/SPATIAL_AUDIO_PLAN.md).

Preferences live in SDL's application preference directory. `--settings-file
path` overrides their base path; input/music/output/feedback use
`.inputs`/`.audio`/`.output`/`.haptics` suffixes.
On macOS the default is
`~/Library/Application Support/AfterBurner2-Recomp/AfterBurner2/settings.cfg`.
`--settings-file` does not relocate `soundtrack/`. Useful launch options: `--frames 180`,
`--profile` and `--record path`. `make play-native STAGE=7` is a directed debug
entry, not natural stage progression.

## Development and documentation

```sh
python3 tools/workflow.py context
python3 tools/workflow.py run test verify-stage1 -j 4
python3 tools/workflow.py run required -j 4
```

`test` is ROM-free; `verify-stage1` adds ROM-backed integration; `required` also
runs the retained MAME oracles/mutations. Follow the roadmap's validation cadence
and preserve all fixtures. Never commit ROMs, captures, recordings or personal
music. GPU readback tests require a working graphics backend.

Start with the [documentation guide](docs/README.md), [workflow](docs/WORKFLOW.md)
and [summarized roadmap](docs/ROADMAP.md#summarized-roadmap). The
[enhancement backlog](docs/ENHANCEMENT_PLAN.md) separates delivered features from
future proposals. [Engine notes](docs/ENGINE_NOTES.md) retain the
ROM evidence and verification history; [enhancement architecture](docs/ENHANCEMENT_ARCHITECTURE.md)
describes extension contracts. The Three.js prototype is retained only on
[`codex/threejs-game-engine`](https://github.com/duduke/AfterBurner2-Recomp/tree/codex/threejs-game-engine).
