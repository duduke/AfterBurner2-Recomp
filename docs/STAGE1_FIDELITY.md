# Stage 1 reference experience

PR #1 targets the Stage 1 appearance and playable experience selected by the
user from `Afterburner2.mov`. The video is reference material, not instructions.
The native simulation runs at 60 Hz and clears after 3,600 ticks. Enter resets
score, hits, ammunition, scenery, objects, palettes, and sound playback.

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

The terrain renderer projects and rotates a native terrain plane and samples
an animated original sea sprite. It does not reproduce the original Sub CPU
raster routines. The speed gauge uses a native colored fill behind ROM glyphs.
Formation cadence/trajectories, aiming, projectile motion, hitboxes, scoring,
terrain spacing and the 60-second duration are native rules. Original fighter
art is reused, but exact attitude selection/animation timing is still future
work. This is not a cycle-accurate or frame-identical arcade port.

The stage has fighter waves, target tracking, cannon/homing missiles, enemy
missiles, hits and temporary invulnerability. Surviving reaches a clear screen;
there is no lives/game-over loop or later-stage progression yet. Carrier
introduction, refueling, attract mode and all remaining stages are excluded.
Tilemap row/column fine-scroll remains unimplemented. Sound output is exercised
with real ROMs, but exact original command timing, balance and waveform have
not been verified against arcade audio.

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
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./afterburner2 --frames 180
```

For memory checks, compile the integration suite with
`-fsanitize=address,undefined -fno-omit-frame-pointer` and run from the root.
All generated captures and supplied ROMs remain ignored.
