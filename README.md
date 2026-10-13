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

**Full-game acceptance remains open:** standard single-credit completion,
continuous whole-credit CPU/device/RGB/audio comparison and sustained peak-load
performance still need verification. Xbox Bluetooth body and both trigger motors are physically confirmed on macOS;
other controllers/HOTAS, Windows/Linux hardware and GPU backends beyond the
recorded Metal/software checks remain open. See the
[roadmap checkpoint](docs/ROADMAP.md#current-checkpoint-2026-10-14) for scope.

Latest progress includes DIP and Video submenus, corrected explosion/full-throttle
flame translucency, balanced external music with live track changes, ground
correction checked during rolls in both directions, and SDL controller feedback.
The final feedback comparison preserves native pixels, audio and timing across
4,200 frames; eleven feedback mutations reject. These focused checks do not
replace full-game acceptance.

## Current enhancements

| Area | Implemented features |
| --- | --- |
| Menus | Arcade-font Start/Options/Quit and in-game pause menu; Video, Controls, Audio and DIP Switches submenus; keyboard, mouse and controller navigation |
| Display | Nearest, Linear, Integer and supported Pixel Art window scaling; fullscreen; FPS and frame/core/filter/presentation timing |
| Sprite filters | Off, EPX, Scale3x, 2xSaI, Super2xSaI and ScaleFX; SDL GPU compute/composition with a CPU/software fallback |
| Ground/horizon | Optional Corrected mode removes misplaced ground-color wedges during rolls; Original remains the default |
| Translucency | Independent Clouds, Missile Trails, Explosions and Afterburner settings; player flame is translucent only at full throttle during flight; crash fire/smoke included, aircraft/debris remain opaque |
| Controls | Keyboard, gamepad and raw joystick remapping; axes/buttons/hats; deadzone, sensitivity, polarity and full-axis throttle; hotplug and persisted device profiles |
| Controller feedback | Optional SDL body and Xbox trigger rumble; cannon, missile, player-impact and full-throttle cues; strengths, motor tests, device selection and lifecycle stops |
| Arcade settings | Lives/Credit (3 or 4), Ship Increase (credit-multiplied lives) and Difficulty; Apply & Restart, F5 or next launch applies saved DIP choices |
| Audio | Six WAV/FLAC track replacements, preview, live current-song switching, Original fallback; independent music/effects/per-track volume; replacement music balanced at -12 dB |
| Preferences and diagnostics | Saved video/input/audio/feedback/DIP preferences, physical-input recording, bounded launch checks and performance reporting |

Presentation enhancements preserve the native board output as the regression
reference. Effects default to Opaque, music to Original and feedback to Off.
Force-feedback stick effects, incoming-warning cues, true widescreen,
replacement sprite assets, CRT shaders, save states and new gameplay modes
remain **planned**. The [haptics notes](docs/HAPTICS_PLAN.md) cover current SDL
output and remaining platform hardware checks.
The [true widescreen plan](docs/WIDESCREEN_PLAN.md) covers expanded scene rendering,
visibility and HUD placement.

## Build and play

Requirements: your ROM dump, SDL3 **3.4+**, a C++20 compiler, and either Make with
`pkg-config sdl3` or CMake with the installed `SDL3::SDL3` package. Run from the
repository root so `roms/` and `soundtrack/` resolve correctly.

```sh
make native-release
./build/afterburner2-release
```

This opens the initial menu. `make play-native` instead cold-boots to flight with
physical coin/start pulses and enables performance reporting.

CMake alternative:

```sh
cmake -S . -B build/cmake
cmake --build build/cmake -j4
./build/cmake/afterburner2
```

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

Preferences live in SDL's application preference directory. `--settings-file
path` overrides their base path; input/audio/feedback use `.inputs`/`.audio`/`.haptics` suffixes.
It does not relocate `soundtrack/`. Useful launch options: `--frames 180`,
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
and [roadmap](docs/ROADMAP.md). [Engine notes](docs/ENGINE_NOTES.md) retain the
ROM evidence and verification history; [enhancement architecture](docs/ENHANCEMENT_ARCHITECTURE.md)
describes extension contracts. The Three.js prototype is retained only on
[`codex/threejs-game-engine`](https://github.com/duduke/AfterBurner2-Recomp/tree/codex/threejs-game-engine).
