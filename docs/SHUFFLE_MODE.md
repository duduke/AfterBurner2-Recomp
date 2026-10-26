# Shuffle mode

Choose **Shuffle** at startup, or **Change Mode → Shuffle** in the pause menu,
then **Start Shuffle**. It starts a fresh run with gameplay stages 1–22 in a random order, each appearing
once. Stage 23 stays last as the native finale. Current DIP settings apply.
Generated orders never start with scripted stages 5, 13 or 23. Stages 5 and 13
remain in the run; a rejected opening is reshuffled using the same seeded RNG.
Seeds that previously opened with either stage now produce a different order.
Score, lives, damage and weapons continue through the run and into the ending.

**F5** restarts the same order. Shuffle → **New Shuffle** starts a newly seeded
order; **Return to Arcade** restores a fresh original board. Practice remains a
separate menu choice. The title and gameplay overlay identify Shuffle. The
order is session state and is printed with its seed in the launch output.

For direct or reproducible launches:

```bash
./build/afterburner2-release --shuffle
./build/afterburner2-release --shuffle-seed 12345
```

Seeds are unsigned 32-bit integers, including zero. Shuffle cannot be combined
with `--stage`, `--practice-stage` or `--record`. The recording menu guard also
applies; natural recordings retain the original cold-board contract.

## Run policy

`Shuffle::RomView` owns a deep copy of the loaded ROM regions. It remaps the
stage selectors, descriptors, event programs, setup/DIP/music/text tables and
palette sources. It allocates copied data in the existing upper main-ROM
address space. Canonical ROM storage and instruction bytes remain unchanged.
`GameSession` binds the view before cold boot and releases it only after the
host is destroyed. Stage transitions execute continuously without board resets,
directed Practice skips or hidden transition frames.

This is an enhanced arrangement policy. The native progress counter retains
its original run positions, 0..22 (displayed as stages 1..23). The
hardcoded position-dependent collision, carrier and animation branches retain
their native run positions; they do not travel with a source stage. Source
event programs, setup and content move together. Difficulty still uses the
selected DIP bank. Stage 23 contains a scripted aircraft that requests the
ending independently of the stage counter, so the plan keeps it last. An early
stage-23 MAME test exposed that dependency; its failed capture is retained.

Music follows the source stage's original command history. A stage whose entry
only continues the preceding music requests that inherited command when its
new predecessor does not supply it, including when it opens the run. Adjacent
stages with the same inherited music keep the continue command; explicit song
and control commands stay intact. This avoids silent openings such as source
stage 12, which inherits Final Take Off from original stage 11.

Original command-18 events prepare the original next stage's palette. They
are omitted from copied event programs. At the audited initialization boundary,
Shuffle installs the destination's settled alternate palette when its original
predecessor requested that preparation. The original 128-update preblend is
replaced by this boundary installation. Other source palette animation remains
native. No score, life or progress values are patched by this preparation.

## Validation

Focused commands, using local ROMs and ignored MAME captures:

```bash
python3 tools/workflow.py run test-shuffle-rom verify-shuffle-rom verify-shuffle-rom-mutations
python3 tools/workflow.py run capture-shuffle-palette verify-shuffle-palette verify-shuffle-palette-mutations
python3 tools/workflow.py run verify-shuffle-session verify-shuffle-complete
python3 tools/workflow.py run capture-shuffle-run verify-shuffle-run
python3 tools/workflow.py run capture-shuffle-ending verify-shuffle-ending verify-shuffle-ending-mutation
python3 tools/workflow.py run verify-native-menu verify-native-shuffle verify-native-practice
```

The ROM-free policy check joins `test`; real-ROM remapping and the three-position
lifecycle check join `verify-stage1`. Full completion remains the explicit
`verify-shuffle-complete` target. Palette and 12,000-frame arranged-run MAME
comparisons and retained negative controls join `oracle-test`, including the
full ending capture's path-coverage control. The 100,000-frame native comparison
is the explicit `verify-shuffle-ending` target. The arranged MAME run receives
only the declared data view, order and physical inputs; it independently
executes the original CPU instructions with the same explicit palette policy. It does not
receive native expected states, CPU execution seeds or replacement RAM.

See [the evidence and coverage limits](ENGINE_NOTES.md#shuffle-mode-2026-10-23).
Finite arranged inputs do not establish every order, DIP combination or full
CPU/device/RGB/audio equivalence. Original-mode acceptance remains separate.
