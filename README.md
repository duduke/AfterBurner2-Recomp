# AfterBurner2-Recomp (WIP)

A from-scratch, hand-ported reimplementation of SEGA's 1987 arcade game
**After Burner II**, in the spirit of Chris White's
[Cannonball](https://github.com/djyt/cannonball) — which did the same thing
for *OutRun*.

After Burner II is a trademark of SEGA. This project is not affiliated with
SEGA. No SEGA code, graphics, or audio is distributed here — the program
reads that data at runtime from an original ROM dump you must supply
yourself (see [roms/README.md](roms/README.md)). See [LICENSE.txt](LICENSE.txt).

## What "recomp" means here

This is **not** automated static/dynamic recompilation of the original
68000 machine code. There's no off-the-shelf tool for that on 68000, and the
output of such tools tends to be unmaintainable. Instead, following
Cannonball's proven approach:

- The original arcade ROM's **graphics tiles, sprites, background/road data,
  and audio (Z80 program + YM2151/SegaPCM samples) are reused as-is** as
  data, read straight out of the ROM images.
- The **68000 game logic is translated from disassembly into address-labelled
  C++**, with verified subsystems available for later readable refactoring.
  The live application runs these translations; the earlier Stage 1
  reconstruction remains in regression tools. The logic drives reimplementations
  of the X-Board hardware (sprite scaler, background generator, tilemap,
  palette) instead of an emulated CPU core.
- The result runs natively (no CPU emulation for the game logic itself),
  is portable, and is realistically modifiable — the entire point of the
  Cannonball approach vs. straight arcade emulation (MAME already does that
  well; this project exists to make the internals legible and hackable).

## Hardware target

After Burner II runs on SEGA's **X-Board** hardware (dual 68000 CPUs + Z80
sound CPU, hardware sprite scaler, custom background/ground generator,
YM2151 + SegaPCM sound). [MAME](https://github.com/mamedev/mame)'s
`src/mame/sega/segaxbd.cpp` driver is used throughout as the ground-truth
hardware reference (it covers the whole X-Board game family; only the
After Burner II-specific configuration is relevant here). See
[docs/HARDWARE.md](docs/HARDWARE.md) for the distilled reference used while
porting.

## Project layout

Mirrors Cannonball's structure, adapted for X-Board:

```
src/main/
  main.cpp, romloader.*      — SDL3 window/input/audio, ROM chip loader
  engine/                    — ported 68000 game logic (one file per
                               original code module, once identified)
  hwvideo/                   — faithful reimplementations of the X-Board
                               sprite scaler, background/ground generator,
                               tilemap and palette hardware
  hwaudio/                   — YM2151 + SegaPCM sound chip emulation, Z80
                               sound program interpretation
  frontend/                  — menus / config (later)
roms/                        — where you place your ROM dump (gitignored)
docs/                        — hardware & porting reference notes
```

## Status

The application now cold-boots the literal main/sub 68000 ports and original
Z80 sound program through one native X-Board scheduler. The ROM drives attract
mode, coin/start, flight, weapons, HUD, scenery and sound commands; the host
presents the hardware renderer and mixed speaker output.

The SDL3 native implementation is on `main`; `make play-native` is the primary
desktop launch target. All 23 debug-selected stage slots complete approximately
10 seconds of scripted inputs without a host stop. Whole-host MAME comparisons
pass for finite cold/gameplay corpora, and the instruction recovery manifest
passes its fixtures and mutations. **Natural full-credit progression and
across-stage peak-load performance remain unverified.** Unknown instructions
stop explicitly. See the [current checkpoint and next acceptance tasks](docs/ROADMAP.md#current-checkpoint-2026-10-11)
for exact coverage and limits.

The SDL application uses `GameSession`, an application API for normalized
controls, pause/restart, completed frame notifications and output audio
processing. It keeps the translated logic as the reference while Phase 5
features are added. See [Extending the translated game](docs/ENHANCEMENT_ARCHITECTURE.md)
for extension contracts and the deeper interfaces needed for new gameplay modes.

A separate [Three.js rendering prototype](prototypes/threejs/README.md) is
available with `make threejs-prototype` after installing its npm dependency.
The `codex/threejs-game-engine` branch is preserved locally and on origin.
It draws original-art sprite meshes and ground/tile geometry from the native
core through a local server; it is not yet a standalone browser build.

## Building

Requires SDL3 3.2 or newer and a C++20 compiler. Make discovers SDL3 through
`pkg-config sdl3`; CMake uses the installed `SDL3::SDL3` target.

For optimized native desktop play, run `make play-native` from the repository
root. This builds `build/afterburner2-release` with the verified `-O3 -flto`
options, cold-boots 600 original board frames with physical coin/start pulses,
then hands flight controls to the keyboard. No browser or server is needed.
Arrows fly, Z/X fire cannon/missiles, Space/Left Shift control throttle,
P pauses, F5 cold-restarts, and Escape exits. After a cold restart, use 5 for
coin and Enter for start. The window title and console show measured simulation
FPS; `--profile` also reports display FPS, board cost and presentation wait.
Launch the release binary without `--boot-to-flight` for the original attract
and coin/start sequence. Full-credit coverage remains incomplete; unported
instructions still stop explicitly. Use `make play-native STAGE=7` to start
at a selected ordered ROM stage slot (1..23), or pass `--stage 7` directly.
F5 repeats that selected startup. This debug skip uses fresh first-stage
accumulated state; it does not reproduce natural progression. See
[the current coverage checkpoint](docs/ROADMAP.md) for verified scope.

`python3 tools/workflow.py run benchmark-native benchmark-native-extended`
measures unpaced native flight/weapon execution and rendering separately.
These benchmarks exclude window presentation and do not establish MAME
acceptance or performance across every stage and input trajectory.

```bash
cmake -S . -B build
cmake --build build
./build/afterburner2
```

Controls: arrow keys move/bank/pitch, Z fires the cannon, X launches missiles,
Space increases throttle and Left Shift reduces it. Press 5 to insert a coin,
Enter to start, P to pause/resume, F5 to cold-restart, and Escape to exit. The board advances at
its physical 16.768 ms frame period; logic cadence comes from the ROM.

The Makefile is an alternative when CMake is unavailable: `make -j4`.
Run ROM-free engine checks with `make test` (or CTest with a CMake build).
For historical reconstruction screenshots, run `make render-scene`, then
`./build/render_scene build/level.ppm`. Append `left` or `right` to capture
banking, or `combat` for weapon fire; an optional final argument selects the
tick count (for example, `./build/render_scene build/right.ppm right 480`).
`make verify-stage1` runs the optional ROM-backed integration suite; CMake users
can enable it with `-DSTAGE1_ROM_TESTS=ON`. `./afterburner2 --frames 180`
performs a bounded application run for launch checks. Run these commands from the repository root; ROMs are not included.

Ported arcade routines are verified against the original code running in
MAME: `make oracle-capture` records reference snapshots from your ROMs into
`build/oracle/` (needs a MAME binary; set `MAME=` and `ROMPATH=`), and
`make oracle-test` checks the ports against them byte-for-byte. See
"Oracle testing" in [docs/ENGINE_NOTES.md](docs/ENGINE_NOTES.md).

## Documentation

| Document | Purpose |
| --- | --- |
| [Roadmap](docs/ROADMAP.md) | Working rules, current acceptance, next tasks and enhancement backlog |
| [Workflow](docs/WORKFLOW.md) | Build/check/capture commands and missing-instruction recovery |
| [Engine notes](docs/ENGINE_NOTES.md) | Chronological ROM discoveries, fixtures, mutations and historical exclusions |
| [Hardware](docs/HARDWARE.md) | X-Board hardware reference |
| [Enhancement architecture](docs/ENHANCEMENT_ARCHITECTURE.md) | Available extension hooks and required deeper interfaces |
| [Historical Stage 1 tests](docs/STAGE1_FIDELITY.md) | Reconstruction regression scope and reproduction |
| [Third-party notices](docs/THIRD-PARTY-NOTICES.md) | Vendored/derived code and license references |
