# Roadmap

Phased plan, mirroring the order Cannonball's own architecture implies:
data/hardware layer first (since it's verifiable against MAME immediately),
then the game logic that drives it (which takes much longer and benefits
from having a working hardware layer to render into while porting).

## Phase 1 — Scaffold (done)

- [x] Project structure, build system, license/legal framing
- [x] Generic ROM chip loader (CRC32-verified, de-interleaving)
- [x] `roms/roms.txt` + `Roms`/ROM-region loader for the exact
      `aburner2` ROM set (region assignment sourced from MAME's
      `ROM_START( aburner2 )`, verified end-to-end against the real ROM zip)
- [x] `docs/HARDWARE.md` hardware reference distilled from `segaxbd.cpp`

## Phase 2 — Hardware layer (`hwvideo` / `hwaudio`)

Faithful, from-scratch reimplementations of the custom chips, validated
against MAME's behavior (not its code) and against real hardware video
captures where possible:

- [x] Tilemap/text layer renderer (`hwtiles`) + palette (`hwpalette`) —
      global per-layer scroll only; the per-column/per-row fine-scroll
      raster tables and ALT-tilemap-per-row override are **not yet
      implemented** (see `hwtiles.hpp`'s header comment and
      `docs/HARDWARE.md` section 3.3) since there's no ported game logic
      yet to exercise/verify them against
- [x] Background/ground generator (`hwroad`, X-Board's equivalent of
      OutRun's "road")
- [x] Sprite scaler (`hwsprites`; position/scale/priority per entry)
- [x] Final screen compositor (`screen.cpp`) — road/tilemap/sprite draw
      order and priority-buffer logic, verified end-to-end with a
      hand-built test scene rendering real decoded ROM tile graphics
      through the full pipeline (roms → hwtiles → hwpalette → screen →
      SDL) to a window
- [x] YM2151 FM synth, SegaPCM sample playback (`hwaudio/`) — carried over
      near-verbatim from Cannonball, since these are the literal same
      generic Yamaha/Sega chips, not OutRun-specific code (attribution
      preserved in each file's header)
- [ ] Sub CPU memory map (`segaxbd_state::sub_map`) — not yet transcribed
      into `docs/HARDWARE.md`, needed before the sub-CPU logic port

Audio is deliberately deprioritized until the visual side and game logic
are further along (see "Deferred: audio" at the end of this document) —
picking it back up later doesn't block anything above.

## Phase 3 — Game logic (`engine`)

The actual port of the 68000 game code — entity/object management, flight
physics and scaling math, enemy/target AI, stage sequencing, HUD, attract
mode, high scores. This is the bulk of the effort and will be broken down
module-by-module once the main CPU's code is mapped out (Cannonball's
`o*.cpp` file-per-subsystem convention is the intended model). Unlike
Phase 2, there's no MAME driver to read for this part — it has to be
disassembled and read directly.

- [x] Disassembly tooling (`tools/build_rom_image.py`,
      `tools/m68k_disasm.py`) — a small from-scratch M68000 disassembler,
      since installing one wasn't available in this environment
- [x] Main CPU vector table mapped (reset, VBLANK, compare/timer IRQ, both
      TRAP vectors that are actually used)
- [x] Reset routine walked through — hardware init, an NVRAM
      signature/first-boot check, and what looks like the game's core
      object/task dispatch loop
- [x] VBLANK handler (the per-frame "engine tick") walked through — see
      `docs/ENGINE_NOTES.md` for the full breakdown (30/60Hz split,
      lamp updates, sprite draw-trigger timing, several not-yet-identified
      subsystem calls)
- [ ] Confirm the object/task table hypothesis and start mapping individual
      object types (see `docs/ENGINE_NOTES.md`'s "Open threads")
- [ ] Map the road/ground and sprite-object update subroutines
      (`$014A86`, `$013F92`)
- [ ] Sub CPU vector table + reset routine (nothing mapped yet)
- [ ] Once enough is understood: start the actual port, file-per-subsystem,
      into `src/main/engine/`

See `docs/ENGINE_NOTES.md` for the detailed, evolving write-up — treat it
as "current best understanding," not verified fact, until a real port
exercises it.

## Phase 4 — Polish

Frontend/config, input remapping, save states, and any of the quality-of-life
features Cannonball/Cannonball-SE/Cannonball DX added for OutRun, re-evaluated
for what actually make sense for After Burner II specifically.

## Deferred: audio

Picked back up once the visual side and game logic are further along —
none of this blocks Phase 3/4 above, since the engine can drive `hwvideo`
and stub sound calls in the meantime.

- [ ] Wiring the YM2151/SegaPCM chips (already implemented, see Phase 2)
      into an actual SDL audio output stream
- [ ] Z80 sound program interpretation (or reuse an existing clean-room Z80
      core, since the sound program itself is comparatively simple compared
      to the main 68000 game logic) — needs the compare/timer chip's
      command protocol (`segaic16_m.cpp`, not yet read) figured out first
- [ ] **Option: substitute Sega Ages After Burner II (Saturn, 1996) music
      tracks for the arcade FM/PCM soundtrack.** That release's music is
      redbook/CD-quality audio per stage, rippable to WAV/MP3 — a much
      simpler path than emulating the Z80 sound program's music sequencing
      for the *music* specifically. Mirrors Cannonball's own precedent of
      accepting external music files (`Roms::load_ym_data`) as an
      alternative to the original chip-driven soundtrack. Scope, if
      pursued:
      - A simple file-based music player (SDL_mixer or similar) alongside,
        not instead of, `hwaudio` — arcade sound *effects* (explosions,
        weapon fire, voice clips) still need the real YM2151/SegaPCM path,
        since Sega Ages doesn't supply replacements for those
      - A mapping from each arcade music cue (whatever the Z80 program
        signals for stage/attract/results music) to a corresponding
        track file
      - Same legal framing as ROMs: nothing is bundled — the user supplies
        their own legally-ripped tracks from a Saturn disc they own,
        dropped into e.g. `res/music/`, matching the `roms/` convention
      - A config toggle between "authentic chip audio" and "Sega Ages
        tracks" per Cannonball-style user preference, not a hard replacement
