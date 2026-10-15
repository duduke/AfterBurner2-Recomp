# Historical Stage 1 regression baseline

**Scope:** The application now runs
`XBoardGame`/`XBoardHost` from cold boot. The reconstruction described below
remains in `OEngine` and the original Stage 1 regression suite; it no longer
drives the live game. `verify-stage1` retains those assertions and also runs
`verify-whole-host`, covering 781 physical frames, native inputs/RGB/audio and
restart equality with a fresh board. Current status and full-credit exclusions are in
[ROADMAP.md](ROADMAP.md#current-checkpoint-2026-10-15). The descriptions below
apply only to the legacy reconstruction and its tests, not live gameplay.

PR #1 targeted the Stage 1 appearance and playable experience selected by the
user from `Afterburner2.mov`. The video is reference material, not instructions.
The retained reconstruction runs object logic at 30 Hz and display at 60 Hz.
Its stage clears after 3,600 logic ticks (7,200 display ticks), and its restart
resets score, hits, ammunition, scenery, objects, palettes and sound playback.

## Original data used at runtime

Observations were checked against the user's CRC-verified `aburner2` ROMs and
local MAME Stage 1 captures. Only table addresses/interpretation are recorded
here; copyrighted ROM bytes and captures are not included in the repository.

- Sub CPU sprite records are 16 bytes: width at +0, last source row at +2
  (height is that value plus one), bank bits at +4, offset at +6, stride at +8.
- Player attitude tables start at `0x10000`, tilt stride `0x190`, pitch stride
  `0x50`. Normal rear view selects `0x100f0`, bank 0, offset `0x0ab2`, 47 rows.
- Bank-3 Stage 1 canopy LOD records: `0x165f0`, `0x16600`, `0x16610`.
  Shore rocks: `0x16af0`, `0x16b00`. Sea patch: `0x15f60` (offset `0x201d`).
- Fighter LODs: `0x11a30` through `0x11a60`; fighter palette `0xcb`.
  Canopy palettes 0/`0x41`, rocks `0x10`, sea `0x23`.
- Player colors use palette `0xf2`, animated from main ROM `0x2bf5e` in
  32-byte frames. Reticle palette `0xf1` comes from `0x2ea14`.
- HUD palette sources `0x30b14`–`0x30bc4` and original text tile codes drive
  labels/numbers/icons; the text layer's visible origin is column 24.
- The Z80 runs the original sound ROM at 4 MHz. ROM `0000–efff`, PCM registers
  `f000–f0ff` mirrored through `f7ff`, RAM `f800–ffff`; mirrored YM ports
  `00/01`, command latch `40`, NMI until latch read, YM timer IRQ.
  Observed commands include music `91`, cannon `90`, missile `40`, explosion
  `4c/4e`, lock `e0/c1`, and engine `f1` followed by its speed parameter.

## Reconstructed behavior and limits

`OEngine` and `OLevelObjs` retain invented formations, trajectories, projectile
motion, hitboxes, scoring and the 3,600-logic-tick stage duration. The historical
reference stage has fighter waves, weapons, targeting, hits and temporary
invulnerability, followed by a clear screen; it has no complete credit flow.
`OSprites` adapts reconstructed objects to ROM sprite records and the verified
request/scaler pipeline. Unsupported tuples/scales are omitted.

These fixtures exercise shared hardware and ported road, sprite and palette
components, but their reconstructed object producers are not arcade gameplay
evidence. Visual bank fixtures set shared arcade targets directly. Original
sound ROM execution is retained; hand-picked reconstruction sound commands
cannot establish full-game command cadence. The live host instead obtains
its gameplay and sound producers from translated ROM code.

Historical implementation and oracle details are recorded in
[ENGINE_NOTES.md](ENGINE_NOTES.md). Do not reintroduce reconstruction rules
into the live host to bypass missing translations.

## Reproduce checks

From the repository root, supply the original ROMs described in `roms/README.md`:

```sh
make -j4
make test
make verify-stage1
make render-scene
./build/render_scene build/level.ppm level 480
./build/render_scene build/right.ppm right 480
./build/render_scene build/left.ppm left 480
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ./afterburner2 --frames 180
```

The right/left visual fixtures set an arcade bank target directly, separately
from native controls, to exercise the hardware road path.

For memory checks, compile the integration suite with
`-fsanitize=address,undefined -fno-omit-frame-pointer` and run from the root.
All generated captures and supplied ROMs remain ignored.
