# PR #1 review — 2026-09-30

Reviewed branch `authentic-flight-model-and-sprites` at `5271b388` against
`main`, using `Afterburner2.mov` as the visual reference. The reference is
an appearance target; it does not contain project instructions.

## Defects corrected

- **P1: Vertical zoom lost bit 10.** `append_sprite` masked the 11-bit
  vertical zoom with `0x3FF`. At `0x400`, horizontal scale shrank while
  vertical scale reset to the hardware minimum (8x enlargement). Preserve
  all eleven bits, matching the hardware decoder.
- **P1: Source height was written as destination height.** The hardware
  height field counts output scanlines. All high-level callers provide
  source ROM rows, so shrinking a sprite walked into later ROM frames,
  drawing streaks/unrelated graphics. Convert height using the vertical
  zoom before writing the descriptor; raw descriptors retain their native
  hardware semantics.
- **P1: Simulation advanced once per display refresh.** Movement, spawns,
  weapon cooldowns, and animation ran faster on high-refresh displays.
  Advance the engine with a fixed 60 Hz accumulator, with bounded catch-up
  after pauses.
- **P2: Sky/sea colors used RGB555 encoding.** The palette decoder expects
  X-Board `sBGR BBBB GGGG RRRR`, producing rainbow bands from the old words.
  Pack all five bits per channel in the correct positions.
- **P2: Stage 1 mixed tropical and desert scenery colors.** The side of the
  screen selected palette 0 versus 4. Both sides now use tropical palette
  0; there is no implemented stage progression to justify desert colors.
- **P2: Flight smoothing stalled before neutral.** Integer division left
  roll/pitch up to three units from the target indefinitely. Snap the final
  small difference to its target.
- Missile ammunition/cooldown now changes only when a free slot actually
  receives a projectile.

## Remaining fidelity work

These are substantive gaps in the prototype, so PR #1 should not be described
as complete or arcade-accurate:

- Banking changes road horizontal offsets and grove frames, but does not
  rotate the sky/sea horizon as in the reference video. This needs a camera
  attitude projection, consistent across terrain and world objects.
- The sea renders mostly as a flat color rather than the reference's moving
  texture. Road scanline selection, placement, and colors need tracing
  against the original program.
- Score, hits, speed, stage, and missile inventory HUD elements are absent.
- Scenery positions, enemy waves, weapon trajectories, collisions, and the
  periodic tanker are synthesized demo behavior, not verified arcade scripts.
- No audio output is integrated into the application loop.
- ROM sprite offsets and palette choices are retained from the PR. Reading
  and rendering those assets does not independently verify every claimed
  aircraft identity or its original animation timing.

## Validation

`make -j4` builds on this macOS checkout with SDL2. Existing audio scaffold
warnings remain (unused values, signed comparison, and `M_PI` redefinition);
the original PR's zero-warning claim does not hold here. CMake is unavailable
on this machine, so the CMake/CTest configuration was not executed.

`make test` runs without ROMs. Synthetic sprite rows followed by poison rows
check source bounds and destination heights at zooms `0x100`, `0x200`,
`0x300`, `0x400`, and `0x580`. Other checks cover blue sky colors and returning
the stick to neutral. The original branch failed the scaling check before
fixing it.

`make render-scene` builds a headless capture tool. With the user's existing,
CRC-verified ROMs, capture level flight and left/right banks after exactly
240 simulation ticks. These are visual checks, not proof of complete arcade
fidelity. No ROM data or generated game screenshots are committed.
