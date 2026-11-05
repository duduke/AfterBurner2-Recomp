# AfterBurner2-Recomp

A playable, work-in-progress C++ reimplementation of SEGA's **After Burner II**
(1987), with optional modern presentation and control enhancements. The original
main/sub 68000 code is translated into C++; the original sound Z80 program runs
with reimplemented YM2151/SegaPCM devices. SDL3 handles the desktop frontend.
Independent MAME captures check the translated game against the arcade ROM.
Inspired by [Cannonball](https://github.com/djyt/cannonball).

Supply your own [ROM dump](roms/README.md). macOS Preview 6 and source builds also load
`roms/aburner2.zip` directly and offer a ZIP file picker when ROMs are missing. ROMs, original artwork and personal
soundtracks are not distributed. This project is not affiliated with SEGA.
Original project code is **GPL-3.0-or-later**. Third-party and historical
components retain their own licenses; this is not a blanket relicensing of them.
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
other controllers/HOTAS and physical Windows/Linux hardware acceptance remain
open. Linux Vulkan VM checks are recorded separately from hardware acceptance. See the
[roadmap checkpoint](docs/ROADMAP.md#current-checkpoint-2026-10-31) for scope.

Preview 7 adds Linux x86-64 / Steam Deck and ARM64 packages, plus native Vulkan
composition for RetroArch Slang and GPU sprite filters. macOS and Android retain
Preview 6, with 21:9 and Corrected ground as the shared default. Windows retains
its experimental Preview 5 package and unresolved FPS/audio limitations. See the
[release notes](docs/RELEASE_NOTES.md) and [roadmap](docs/ROADMAP.md).

The widescreen work expands the experimental 16:9 view beyond the original sprite
queue and depth-bucket limits, retains scenery as it crosses the wider edges,
and fixes reproduced rolling scenery, takeoff wave coverage, fast-roll sky
triangles, blue cloud gaps and attract-cloud corner artifacts. It also fixes the reported enemy-coordinate overflow stop
at PC `$15C10`. The original simulation remains the comparison reference;
the wider presentation still needs exhaustive scene and platform validation.

## Android and Android TV

An initial ARM64 Android host adds phone/tablet multi-touch controls, Android TV
launcher support, controller/remote menus and private ROM ZIP import. Build and
sideload instructions are in [Android preview](docs/ANDROID.md). SM-S908E / Android 16 checks cover import, touch layout, Hide/Show, finite
performance samples and independent MAME replays. Broader phone/TV acceptance
and sustained peak-load performance remain open.

## Linux and Steam Deck

[Linux Preview 7 downloads](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.7)
include separate ROM-free x86-64 and ARM64 packages and checksums. SDL3/OpenAL and the Vulkan Slang runtime are bundled. Full project source
companions remain local and in the private development release; corresponding runtime
source and notices remain available separately.

| Platform | Download |
| --- | --- |
| x86-64 / Steam Deck | [Linux x86-64](https://github.com/duduke/AfterBurner2-Recomp/releases/download/v0.1.0-preview.7/AfterBurner2-v0.1.0-preview.7-20261103.1-Linux-x86_64-SteamDeck-preview.tar.gz) |
| ARM64 desktop | [Linux ARM64](https://github.com/duduke/AfterBurner2-Recomp/releases/download/v0.1.0-preview.7/AfterBurner2-v0.1.0-preview.7-20261103.1-Linux-arm64-preview.tar.gz) |

Extract the game and run `Launch.sh`. For Steam Deck, add the x86-64 `Launch.sh`
as a Non-Steam Game and use a Gamepad template; first-run settings use
fullscreen/16:10. ARM64 uses windowed/Original desktop defaults.

For Slang shaders, download the [official RetroArch Slang shader pack](https://buildbot.libretro.com/assets/frontend/shaders_slang.zip),
extract the **entire ZIP** into a permanent folder, then open **Settings → CRT
and Shaders → Shader Preset** and select a `.slangp` from that folder. Keep all
subfolders, includes and textures together. No separate runtime installation is
needed. Existing preferences are preserved.

x86-64 targets Valve's sniper SDK / glibc 2.31; ARM64 targets Debian 12 /
glibc 2.36. Both have X11/Wayland and finite Vulkan presentation checks on VMs.
Physical Deck/ARM64 performance, controller feel and suspend/resume remain open;
the measured VMs do not sustain arcade speed. See the
[Linux setup, build and validation guide](docs/LINUX.md).

## Current enhancements

| Area | Implemented features |
| --- | --- |
| Menus | Arcade-font Arcade/Practice/Shuffle/Settings/Quit launcher; pause/resume, restart and mode selection; grouped display, graphics, CRT, controls, audio and arcade settings; keyboard, mouse and controller navigation |
| Stage Select / Practice | [Practice mode](docs/PRACTICE_MODE.md): fresh directed entry to 23 ROM stages, normal lives/damage, F5 retry and return to arcade; separate from natural recordings |
| Shuffle | [Shuffle mode](docs/SHUFFLE_MODE.md): stages 1–22 in random order with stage 23 as the finale, continuous score/lives, same-order F5 retry and fresh New Shuffle |
| Widescreen | Experimental 16:9 (398×224) and 16:10 (358×224) viewports: additional road/tiles, recovered hidden sprites and retained side scenery, world explosions, hit effects, player/enemy missile smoke and player damage emissions with live roll projection; extended horizon and takeoff wave field; dynamic presentation lists recover requests dropped by native queue/depth limits; centered HUD and existing effects; Original remains the default. Both widths use the integrated renderer in macOS Preview 5; current source also adds experimental 21:9 (522×224) |
| Display | Nearest, Linear, Integer and supported Pixel Art window scaling; high-DPI/fullscreen output; optional Original / Sharp 3x / Custom text-plane HUD ([atlas format](docs/HUD_PRESENTATION.md)); FPS and frame/core/filter/presentation timing |
| Sprite filters | Off, EPX, Scale3x and ScaleFX; SDL GPU compute/composition with a CPU/software fallback |
| CRT display | Off / Subtle / Arcade presets with independent scanline and RGB-mask strength; optional RetroArch `.slangp` presets on macOS Metal and Windows/Linux Vulkan (runtime bundled on Linux; shader packs downloaded separately); readable frontend menus; [setup and scope](docs/CRT_DISPLAY.md) |
| Player aircraft prototype | Optional [registered neutral/gentle-bank HD pack](docs/SPRITE_REPLACEMENT.md), included in macOS Preview 3; unsupported poses retain the original art |
| Ground/horizon | Corrected mode is the default on every platform and removes misplaced ground-color wedges during rolls; widescreen also extends verified ground/sea fill behind translucent clouds and handles clipped horizon edges. Original remains selectable |
| Translucency | Independent Clouds, Missile Trails, Explosions and Afterburner settings; player flame is translucent only at full throttle during flight; crash fire/smoke included, aircraft/debris remain opaque |
| Controls | Keyboard, gamepad and raw joystick remapping; axes/buttons/hats; live calibration display, deadzone, sensitivity, polarity and full-axis throttle; hotplug and persisted device profiles |
| Controller feedback | Optional SDL body and Xbox trigger rumble; cannon, missile, player-impact and full-throttle cues; strengths, motor tests, device selection and lifecycle stops |
| Arcade settings | Lives/Credit (3 or 4), Ship Increase (credit-multiplied lives) and Difficulty; Apply and Restart, F5 or next launch applies saved DIP choices |
| Audio | Six WAV/FLAC track replacements, preview, live current-song switching, Original fallback; independent music/effects/per-track volume; replacement music balanced at -12 dB |
| Spatial audio (experimental) | Optional OpenAL Soft: four virtual cockpit speakers with headphone HRTF, 2.1/5.1 profiles, chip-rate interpolation, output-device selection and speaker test; Original SDL remains default |
| Preferences and diagnostics | Saved video/input/audio/feedback/DIP preferences, physical-input recording, bounded launch checks and performance reporting |

Source builds and macOS Preview 5 use the integrated primary renderer for widescreen and detached
HUD/aircraft presentation. It shares the native partial-frame road/tile pass;
`--scene-renderer snapshot` selects the retained reconstruction reference.
The `16:9 Preview`, `16:10 Preview` and source-only `21:9 Preview` labels describe remaining widescreen
scene/platform acceptance, not renderer selection. Original remains the default
viewport; selecting a wide ratio uses the integrated renderer. See the
[Mac acceptance record](docs/ENGINE_NOTES.md#integrated-primary-renderer-macos-acceptance-2026-10-24).

Presentation enhancements preserve the native board output as the regression
reference. Effects default to Opaque, music to Original and feedback to Off.
Force-feedback stick effects, incoming-warning cues, complete widescreen visibility,
complete replacement animation, broader shader backends, save states and additional gameplay modes
remain **planned**. The [haptics notes](docs/HAPTICS_PLAN.md) cover current SDL
output and remaining platform hardware checks.
Choose **Settings → Display → Aspect Ratio → 16:9 Preview** to try the wider view.
Current source also offers **21:9 Preview (522×224)**, with 101 additional
columns per side and the same centered HUD/menu. Published packages predate it.
The wider view retains side scenery, smoke and effects beyond native screen-edge
removal while leaving gameplay intact, and recovers hidden sprite requests.
Choose **Settings → Graphics → Ground → Corrected** for full-width sky correction during rolls.
The wider renderer now recovers requests discarded by the original 192-request
queue and full depth buckets, and renders a dynamic descriptor list. Native
object allocation and gameplay limits stay unchanged. All 23 stage slots and
recorded lifecycle paths have dedicated checks; exhaustive scene/platform
validation and sustained ultrawide performance remain open. See the
[true widescreen plan](docs/WIDESCREEN_PLAN.md) for progress and remaining gates.

## Build and play

Download [Linux Preview 7](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.7),
[macOS or Android Preview 6](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.6)
and extract the complete desktop archive. Put your own extracted ROM files or
`aburner2.zip` in its `roms/` directory, or select a ZIP on startup. Android uses
the sideload APK and imports a ROM ZIP through its document picker; see
[Android setup](docs/ANDROID.md). Notices, exact source links and SHA-256 files
accompany the packages; ROMs, captures and personal settings are excluded.

The **Apple Silicon / macOS 26+** package opens with `Launch.command`.
SDL3 and OpenAL Soft are included with notices and exact source links; the
matching source companion is retained separately. The preview is ad-hoc signed and **not notarized**; macOS
may require approval in Privacy & Security. Intel Macs are not included in this
build. See the package's `START-HERE.txt` for setup.

**Windows downloads were withdrawn from the public release repository on
4 November 2026 pending signing and performance fixes.** The retained compact
Windows 10/11 x64 diagnostic package dated **20261028** opens with
`Launch.cmd`; keep all bundled DLLs beside the EXE. It includes librashader
and its DirectX/Microsoft C++ dependencies. Shader preset packs remain separate.
`Launch.cmd --profile` reports backend and timing output. Sources are a separate
companion download; the runtime ZIP has no regression binaries or general docs,
and consolidates required redistribution terms into `LICENSES.txt`.
This unsigned package is built and runtime-verified on Windows x64 with Vulkan
GPU sprite filters, original SDL stereo, replacement music and OpenAL Soft 1.25.2
spatial audio (HRTF, 2.1 and 5.1 profiles).
This refresh retains GPU sprite/scene composition with Slang and reuses the
native sprite raster for Sharp HUD ownership. **Windows FPS/audio drops remain
unresolved**, including 53–56 FPS dips and synchronized OpenAL starvation.
See [current Windows performance issues](docs/WINDOWS_PERFORMANCE.md) and
[shader setup](docs/CRT_DISPLAY.md#windows-vulkan). Windows OpenAL's 80 ms
reserve adds buffering and does not establish glitch-free acceptance.

Requirements: your ROM dump, SDL3 **3.4+**, a C++20 compiler, and either Make with
`pkg-config sdl3` or CMake with the installed `SDL3::SDL3` package. Run from the
repository root so `roms/` and `soundtrack/` resolve correctly.

```sh
make native-release
./build/afterburner2-release
```

This opens the initial menu. Choose **Arcade** to watch the original boot
and attract sequence, then insert a coin and press Start (keyboard **5**, then
**Enter**). `make play-native` uses the same startup with performance reporting.
For the explicit automatic coin/start shortcut, use
`./build/afterburner2-release --boot-to-flight`.

Choose **Practice** at startup or under **Change Mode** in the pause menu to select a starting stage, or
**Shuffle → Start Shuffle** for a continuous run through stages 1–22 in random
order and stage 23 last. **F5** retries the selected Practice stage or the same
Shuffle order; **New Shuffle** generates another order. Both modes offer
**Return to Arcade**. Direct launches support `--practice-stage 8`, `--shuffle`
and reproducible `--shuffle-seed 12345`. See [Practice](docs/PRACTICE_MODE.md)
and [Shuffle](docs/SHUFFLE_MODE.md) for behavior and recording restrictions.

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

For Windows LTO, use an LTO-capable compiler in a **new build directory**.
w64devkit GCC 16.2.0 is configured with `--disable-lto`; no compiler flag can
enable it. The local Windows build uses
[LLVM-MinGW 20261020 UCRT x64](https://github.com/mstorsjo/llvm-mingw/releases/tag/20261020)
(Clang 23.1.4 / LLD) with ThinLTO. Install Vulkan headers (the Vulkan SDK, or
Vulkan-Headers 1.4.341 under `build/deps/`) for the Windows shader host:

```sh
cmake -S . -B build/windows-lto -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER="C:/path/to/llvm-mingw/bin/clang++.exe" -DAB2_REQUIRE_IPO=ON -DSTAGE1_ROM_TESTS=ON
cmake --build build/windows-lto --target afterburner2 -j4
python tools/deps/fetch_librashader.py
./build/windows-lto/afterburner2.exe --shader-preset "C:/path/to/shaders_slang/crt/crt-easymode.slangp"
```

Set `SDL3_DIR`, `OpenAL_DIR`, and `AB2_VULKAN_INCLUDE_DIR` when these dependencies
are outside CMake's search paths. CMake copies SDL/OpenAL and available
LLVM-MinGW runtime DLLs beside the game. `AB2_REQUIRE_IPO=ON` fails configuration
if LTO cannot actually compile and link. [Windows shader setup](docs/CRT_DISPLAY.md#windows-vulkan)
includes validation commands and the scope of newer source-only optimizations.
Windows Preview 5 includes the shared ROM startup, menu and widescreen features.

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
Remap in **Settings → Controls → Button Bindings**: Enter captures, Backspace clears and Escape
cancels/returns. For throttle sliders, choose Full Axis under **Analog Settings**
and bind Boost toward maximum; this replaces Brake. Identical device models share a GUID/type profile.
Select a gamepad or joystick, then open **Input Monitor** to compare bound stick
and throttle input before and after dead zone and sensitivity are applied.
Remapped axis direction is reflected live; Input Monitor is disabled for Keyboard.

Enable rumble in **Settings → Controls → Feedback**. Choose the output device,
turn Enabled On, and adjust body/trigger strengths independently. Motor tests
work with gameplay feedback Off. See the [motor mapping table](docs/HAPTICS_PLAN.md#motor-mapping)
for left/right body and trigger assignments. Cannon uses the right trigger and
missiles the left; Swap Triggers reverses them. Xbox Bluetooth body and both trigger
motors are physically confirmed on macOS; Windows/Linux hardware checks remain
open. Unsupported devices retain normal controls.

## Custom music

Put WAV/FLAC files directly in `soundtrack/`, then choose them in
**Settings → Audio → Music Tracks**. The six tracks are Maximum Power, Final Take
Off, Red Out, Super Stripe, After Burner and City 202. Left/Right cycles choices;
Space or gamepad North previews while paused.

A loaded selection for the current song applies on resume and starts the file
from the beginning. Other mappings apply at their native cue. Music, effects and
track volumes range from 0–100%; gameplay and preview use the same -12 dB
replacement baseline. Original at 100% preserves exact native PCM. See
[music behavior and coverage](docs/MUSIC_REPLACEMENT_PLAN.md) and
[Sega Saturn (SEGA AGES) track naming](docs/MUSIC_REPLACEMENT_PLAN.md#sega-saturn-sega-ages-soundtrack-tracklist).

For optional spatial playback, open **Settings → Audio → Output Setup** and choose
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

## Sprite extraction

Run `python3 tools/workflow.py run extract-sprites` to export indexed PNGs and
`build/sprites/mapping.json` from your own ROMs. The mapping preserves frame
addresses, bank/offset/stride, dimensions and candidate LOD links, with editable
label/replacement fields. PNGs use grayscale pen previews; runtime palettes and
object names are not inferred. See [sprite extraction](docs/SPRITE_EXTRACTION.md)
for the format, limits and validation commands.

An opt-in [HD player-aircraft pack](docs/SPRITE_REPLACEMENT.md) replaces neutral
flight and the first gentle bank in both directions, with fixed image bounds
and registration. Other poses retain Original artwork:

```sh
./build/afterburner2-release --player-aircraft assets/player-aircraft/neutral-gentle-v1.aircraft
```

## Development and documentation

```sh
python3 tools/workflow.py context
python3 tools/workflow.py run main -j 4       # daily main functionality
python3 tools/workflow.py run required -j 4   # engine changes only
```

`main` covers the primary frontend and Arcade/Practice/Shuffle lifecycles.
`test` is ROM-free; `verify-stage1` retains broad ROM-backed integration;
`required` adds all retained MAME oracles/mutations for engine changes.
Non-engine releases use `main`, affected checks and package validation. Follow the roadmap's validation cadence
and preserve all fixtures. Never commit ROMs, captures, recordings or personal
music. GPU readback tests require a working graphics backend.

Start with the [documentation guide](docs/README.md), [workflow](docs/WORKFLOW.md)
and [summarized roadmap](docs/ROADMAP.md#summarized-roadmap). The
[enhancement backlog](docs/ENHANCEMENT_PLAN.md) separates delivered features from
future proposals. [Engine notes](docs/ENGINE_NOTES.md) retain the
ROM evidence and verification history; [enhancement architecture](docs/ENHANCEMENT_ARCHITECTURE.md)
describes extension contracts. The Three.js prototype is retained only on
[`codex/threejs-game-engine`](https://github.com/duduke/AfterBurner2-Recomp/tree/codex/threejs-game-engine).

See [COPYRIGHT.md](COPYRIGHT.md) for the original-code grant and third-party scope.
