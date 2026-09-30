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
- [x] Sub CPU memory map (`segaxbd_state::sub_map`) transcribed into
      `docs/HARDWARE.md` — notably narrower than main's (no tilemap/
      sprite/palette/I-O access at all), pointing at a dedicated
      road/perspective math coprocessor role, confirmed in Phase 3 below

The original Z80 sound program now drives YM2151/SegaPCM and SDL audio.
See the Stage 1 fidelity notes for the verified command and memory maps.

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
- [x] Sub CPU vector table, reset routine, and VBLANK handler mapped —
      confirms it's a dedicated road/perspective math coprocessor (no
      access to tilemap/sprite/palette/I-O hardware at all), with a
      counting-sort-then-drive-the-multiplier-chip routine that's the
      strongest evidence yet for that role
- [x] Main ↔ Sub handshake traced: Main CPU writes `$55555555` to
      `subram0[$70]` at `$0138F4` in `$01381E`, releasing Sub CPU from its
      reset spin-loop at `$000420`
- [x] Main ↔ Sub mutex mechanisms mapped:
      - `subram0[$D6]` bit 0: frame-update mutex (Main CPU VBLANK sets at
        `$014674`, clears at `$014798`; Sub CPU math engine `$000640` stalls
        during frame update)
      - `subram0[$00]` bit 0: pause mutex (Main CPU sets at `$0145DA` upon
        Pause switch input; Sub CPU VBLANK `$00055E` stalls while paused)
- [x] Sprite pipeline mapped end-to-end:
      - Main CPU objects emit 16-byte descriptors to unsorted buffer via
        `append_sprite` (`$00B628`)
      - Main CPU VBLANK swaps 4 double-buffer pointer pairs (`$2A3C04/0C/14/1C`)
      - Sub CPU counting sort (`$000640`) sorts by Z-distance into bucket lists,
        computes perspective with 315-5248 multiplier, and outputs sorted buffer
      - Main CPU blits sorted buffer to hardware Sprite RAM (`$00100000`) and
        triggers hardware scaler (`$00110001`)
- [x] Road / ground-scroll pipeline mapped end-to-end:
      - Main CPU sets stage (`subram0[$9C]`) and flight attitude (`subram0[$CE]/[$AE]-[$B6]`)
      - Sub CPU computes horizon scanline offset (`$000F70`), generates 224-line
        per-scanline raster curves (`$0EC200`/`$0EC400` at `$000F4C`), and
        flips hardware road RAM double-buffer at `$000EE001` (`$000464`)
- [x] Mapped VBLANK subsystem hooks:
      - `$016C8A`: missile reload / audio queue / HUD icon sequencer
      - `$018444`: stage events (refueling stages 5/13/20) and background
        scenery streaming to Tile RAM + roll-adjusted horizontal scroll register
- [x] Object/task system mapped (`$9FC0`–`$DF40`):
      - 254 entries × 64 bytes
      - Compound player jet (slots 0..3: fuselage, twin exhaust flames, weapon pod)
      - Player missile/vulcan projectiles
      - 3D enemy world-to-screen pipeline using SEGA 315-5248 hardware multiplier
- [x] Initial C++ engine subsystems scaffolded in `src/main/engine/`:
      - `oentry.hpp`: 64-byte object descriptor matching arcade layout
      - `osprites`: depth sorting and hardware sprite RAM blit
      - `oroad`: 224-line scanline horizon and roll curvature synthesizer
      - `opalette`: fade/flash and color cycling player
      - `olevelobjs`: 254-slot object manager, player jet compound flight model
      - `oengine`: frame tick coordinator connected to SDL2 keyboard inputs
- [ ] Continue porting individual game subsystems into `src/main/engine/`:
      - Enemy AI & formation spawning
      - Collision detection & hitboxes
      - Stage sequencing and terrain elevation profiles
      - Original stage timing and behavior beyond the Stage 1 reconstruction
- [x] Stage 1 reference experience: banked terrain/sea, ROM-backed HUD, native
      fighter formations, weapons, collisions, clear state and restart
      (see `STAGE1_FIDELITY.md` for remaining differences)

See `docs/ENGINE_NOTES.md` for the detailed, evolving write-up — treat it
as "current best understanding," not verified fact, until a real port
exercises it.

## Phase 4 — Polish

Frontend/config, input remapping, save states, and any of the quality-of-life
features Cannonball/Cannonball-SE/Cannonball DX added for OutRun, re-evaluated
for what actually make sense for After Burner II specifically.

## Audio

- [x] Wiring the YM2151/SegaPCM chips (already implemented, see Phase 2)
      into an actual SDL audio output stream
- [x] Z80 sound ROM execution using the licensed floooh/chips core, original
      NMI latch and YM IRQ protocol, with music/engine/weapon/effect commands
- [ ] Exact command cadence and waveform comparison against the arcade
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
