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
- The **68000 game logic is being studied from disassembly and re-authored
  as readable, modern C++**; the current Stage 1 behavior also contains native
  reconstructions. It drives reimplementations
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
  main.cpp, romloader.*      — app entry point, ROM chip loader
  engine/                    — ported 68000 game logic (one file per
                               original code module, once identified)
  hwvideo/                   — faithful reimplementations of the X-Board
                               sprite scaler, background/ground generator,
                               tilemap and palette hardware
  hwaudio/                   — YM2151 + SegaPCM sound chip emulation, Z80
                               sound program interpretation
  sdl2/                      — platform layer: window, input, audio output
  frontend/                  — menus / config (later)
roms/                        — where you place your ROM dump (gitignored)
docs/                        — hardware & porting reference notes
```

## Status

Playable Stage 1 reference experience with fighter combat, targeting, hits,
original sprite art, HUD and sound-ROM playback. The native stage clears after
3,600 logic ticks (120 seconds at the corrected 30 Hz cadence); Enter restarts it. Phase 3b replaced the native terrain with verified
arcade road/scenery ports and the hardware renderer. Phase 3c adds verified
frame/palette scheduling and both sprite lists through the arcade transform.

Formations, flight/collision rules, HUD and stage duration remain reconstructed.
Native controls do not feed arcade road attitude until the flight/input port
in 3d. Complete stage progression, carrier introduction,
attract mode and refueling remain future work; the full stage count has not
yet been verified. See [docs/STAGE1_FIDELITY.md](docs/STAGE1_FIDELITY.md),
[docs/ROADMAP.md](docs/ROADMAP.md), and [docs/PR_REVIEW.md](docs/PR_REVIEW.md).

## Building

Requires SDL2 and a C++20 compiler.

```bash
cmake -S . -B build
cmake --build build
./build/afterburner2
```

Controls: arrow keys move/bank/pitch, Z fires the cannon, X launches missiles,
Space increases throttle, Enter restarts Stage 1, and Escape exits. Display runs at 60 Hz and object logic at 30 Hz.

The Makefile is an alternative when CMake is unavailable: `make -j4`.
Run ROM-free engine checks with `make test` (or CTest with a CMake build).
For reproducible ROM-backed screenshots, run `make render-scene`, then
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
