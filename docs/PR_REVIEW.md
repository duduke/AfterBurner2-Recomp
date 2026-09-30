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

## Stage 1 follow-up

The follow-up completes the user-selected **Stage 1 reference experience**:
rotating terrain/horizon, animated ROM-backed sea, correct bank-3 canopy/rocks,
player frame tables and F2 palette animation, score/hits/speed/stage/ammo HUD,
fighter combat, targeting, enemy missiles, a clear/restart flow, and original
Z80 sound program playback through YM2151/SegaPCM and SDL. Text rendering now
uses the hardware's -192-pixel X origin. Sprite destination height rounds up
to preserve the last sampled source row.

Native Stage 1 formation, terrain and collision rules remain reconstructed,
with a 60-second clear condition. Original stage scripts, exact enemy behavior,
carrier/attract/refueling sequences, all 25 stages, and tilemap fine-scroll
are outside this narrowed scope. See `STAGE1_FIDELITY.md` for evidence and limits.

## Validation

- Clean `make -j4` with SDL2 on macOS, without compiler warnings.
- `make test`: ROM-free descriptor scaling, palette and neutral-stick checks.
- `make verify-stage1`: CRC-verified user ROMs; a complete 3,600-tick combat
  session, scoring/ammo, stable HUD during bank, projected terrain, clear and
  restart; sound ROM produces 800 stereo frames per tick and non-silent output.
- The ROM-backed suite also passes AddressSanitizer and UndefinedBehaviorSanitizer.
- SDL video/audio application startup and normal shutdown using dummy drivers.
- Level and left/right-bank captures compared with the supplied movie and
  original MAME Stage 1 captures. These establish visual progress, not bit-exact
  arcade equivalence. Audio output is checked programmatically; exact waveform
  equivalence has not been established.

CMake is unavailable on this machine; its configuration was updated but not
executed. No ROM data, extracted memory dumps or game screenshots are committed.
