# AfterBurner2-Recomp (WIP)

A native C++ reimplementation of SEGA's 1987 **After Burner II**, inspired by
[Cannonball](https://github.com/djyt/cannonball). The original main/sub 68000
instructions are translated into address-labelled C++ with ROM-derived code
generation and hand-translated modules. The original sound Z80 program runs
with reimplemented YM2151/SegaPCM devices under a common X-Board scheduler.
Independent [MAME](https://github.com/mamedev/mame) captures verify the ports.

After Burner II is a SEGA trademark; this project is not affiliated with SEGA.
ROM images, original graphics and soundtrack files are not distributed. Supply
your own ROM dump as described in [ROM setup](roms/README.md).
See [LICENSE.txt](LICENSE.txt) and [third-party notices](docs/THIRD-PARTY-NOTICES.md).

## Current status

The native application cold-boots the translated board and supports attract
mode, coin/start, flight, weapons, scenery, HUD and original audio. SDL3 handles
windowing, input, presentation and playback. The board advances at its physical
16.768 ms frame period; gameplay cadence remains ROM-controlled.

Finite whole-host MAME comparisons and retained instruction/mutation gates pass.
All 23 debug-selected stage slots complete approximately ten seconds of scripted
inputs. A 74,452-frame Easy / 99-credit physical-input recording reaches the
ending and matches ten MAME observation columns throughout. **Standard single-credit
completion, continuous whole-credit CPU/device/RGB/audio identity and across-stage
peak-load performance remain open.** Unknown instructions stop explicitly.
See the [roadmap checkpoint](docs/ROADMAP.md#current-checkpoint-2026-10-12)
for precise coverage, exclusions and the next acceptance tasks.

## Build and play

Requires SDL3 **3.4 or newer**, a C++20 compiler and either Make with
`pkg-config sdl3`, or CMake with the installed `SDL3::SDL3` package.
Run commands from the repository root so `roms/` and `soundtrack/` resolve there.

```sh
make play-native
```

This builds `build/afterburner2-release` using the verified `-O3 -flto` options,
then advances the original cold boot with physical coin/start pulses and hands
over flight controls. For the initial Start Game / Options / Quit menu:

```sh
make native-release
./build/afterburner2-release
```

Alternatively:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake -j4
./build/cmake/afterburner2
```

`make play-native STAGE=7` debug-selects an ordered stage slot (1–23). It uses
fresh first-stage accumulated state and does not reproduce natural progression.
`--frames 180` limits a launch check; `--profile` reports simulation/display FPS,
board cost, filter submission and presentation wait. `--record path` records
physical inputs; `--settings-file path` relocates frontend preferences.

## Controls and remapping

| Keyboard default | Action |
| --- | --- |
| Arrow keys | Fly / bank / pitch |
| Z / X | Cannon / missiles |
| Space / Left Shift | Increase / reduce throttle |
| 5 / Enter | Coin / start |
| P / F5 | Pause / cold restart |
| Esc or F1 | Open / close menu |
| F11 / F3 | Fullscreen / cycle stats |

Options → Controls supports keyboard, SDL3 gamepads and raw joysticks, with
remapping, dead zone, sensitivity, axis polarity and full-axis throttle. Enter
captures a binding; Backspace clears it; Escape cancels or returns. Axes must
cross neutral before capture. For a throttle slider, select Full Axis and bind
Boost toward maximum; this replaces Brake and uses the complete signed range.

Gamepads default to left-stick flight, South/East for weapons, triggers for
throttle and Back/Start for coin/start. D-pad/shoulder fallbacks cover pads without
those axes. Raw joysticks default to axes 0/1 and buttons 0–5; remap to suit your
stick. D-pad or joystick hats navigate menus, South/button 0 selects, East/button
1 returns. Bind Menu when Guide is unavailable. Keyboard remains available.
SDL3 auto-detects hotplug devices and uses built-in mappings plus a pinned offline
[SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB) snapshot.
Profiles follow SDL GUID and device type; identical models share a profile.
Physical gamepad/HOTAS validation remains outstanding; virtual-device tests pass.

## Graphics and sprite effects

Options provides window scaling, fullscreen and FPS/frame/core/filter/presentation
stats. The list scrolls with Up/Down; Left/Right changes values. Sprites selects
Off (default), EPX, Scale3x, 2xSaI, Super2xSaI or ScaleFX. xBRZ awaits a compatible
license. Sprite filtering and background composition use **SDL GPU compute** on
Metal or Vulkan, with a CPU/software fallback. GPU composition also runs with
Sprites Off. Native board rendering, sprite feedback, tile priority, text and
palette shadows remain authoritative.

Clouds, Trails, Explosions and Afterburner independently choose Opaque (default)
or Translucent. Original-art opacity is 160/255 for clouds, 112/255 for missile
trails, 176/255 for explosions and 192/255 for the player's flame. Explosion
assets come from runtime ROM animation tables; flame transparency applies only
to its palette pens within known aircraft assets, keeping the airframe opaque.
These affect presentation copies; they do not change gameplay or native RGB.
Replacement RGBA art, softer edges and age-based trail fading remain future work.

## Music and volume

Place your WAV/FLAC files directly in `soundtrack/`, then open Options → Audio.
Music Tracks lists Maximum Power, Final Take Off, Red Out, Super Stripe, After
Burner and City 202. Left/Right cycles Original and alphabetically sorted files;
Enter advances. **No file picker opens.** Space or gamepad North previews a file
while paused. Mapping changes apply at the next genuine native music start.

Music Volume, Sound Effects Volume and six Track Volumes are independent,
persistent 0–100% controls in 5% steps. Original is the default; at all-Original
100% the output remains exact native PCM. Files decode asynchronously to 48 kHz
stereo, loop while their native instance is active and fall back to Original if
unavailable. Limits: 256 MiB per input/decoded file, 512 MiB decoded cache.
Natural music evidence currently covers Maximum Power and Final Take Off with
overlapping effects; other song contexts and natural fades still need recordings.
See [music implementation and coverage](docs/MUSIC_REPLACEMENT_PLAN.md).

Preferences use SDL's application preference directory. Input/audio settings
have `.inputs` / `.audio` suffixes beside `settings.cfg`, or the explicit
`--settings-file` path. That option does not relocate `soundtrack/`.

## Validation and development

```sh
python3 tools/workflow.py context
python3 tools/workflow.py run test verify-stage1
python3 tools/workflow.py run required
```

`test` runs ROM-free regressions; `verify-stage1` adds ROM-backed integration.
`required` also runs the complete retained MAME oracle and mutation suite, using
local ignored fixtures. CMake enables ROM tests with `-DSTAGE1_ROM_TESTS=ON`.
GPU readback checks require a working graphics backend. Capture commands and
validation cadence are in [Workflow](docs/WORKFLOW.md) and [Roadmap](docs/ROADMAP.md).
Never commit ROMs, captures, recordings or personal audio.

The [Three.js prototype branch](https://github.com/duduke/AfterBurner2-Recomp/tree/codex/threejs-game-engine)
retains that experiment; its source, dependencies and targets are absent from main.

| Location / document | Purpose |
| --- | --- |
| `src/main/engine/` | Translated gameplay, scheduling and application API |
| `src/main/hwvideo/`, `hwaudio/` | Original board devices and passive presentation taps |
| `src/main/frontend/` | SDL GPU shaders, menu, input profiles and music playback |
| [Roadmap](docs/ROADMAP.md) | Checkpoint, rules and enhancement backlog |
| [Workflow](docs/WORKFLOW.md) | Build, test, capture and recovery commands |
| [Engine notes](docs/ENGINE_NOTES.md) | ROM evidence, validation results and exclusions |
| [Hardware](docs/HARDWARE.md) | X-Board reference |
| [Enhancement architecture](docs/ENHANCEMENT_ARCHITECTURE.md) | Extension contracts and deeper interfaces |
| [Historical Stage 1 tests](docs/STAGE1_FIDELITY.md) | Retained reconstruction regression scope |
