# Checkpoint history

Historical validation records moved from the roadmap. These retain their original
source/fixture scope; an earlier pass does not validate later changes. Current
status is in [ROADMAP](ROADMAP.md#current-checkpoint-2026-10-15); detailed ROM
evidence remains in [ENGINE_NOTES](ENGINE_NOTES.md).

The documentation-consolidation baseline required checks passed
on main: `test` (33.50 s), `verify-stage1` (73.68 s), and the complete
`oracle-test` (1823.36 s), including all 85 recovery mutations and retained
fixtures. Local logs are in `build/workflow/20261011T150418.391037Z/`.
The current natural-recording work adds literal ports and observation tools
while retaining every earlier fixture and all 10,525 earlier recovery bodies.
The natural-transition checkpoint required checks passed: `test` (34.89 s) and `verify-stage1`
(79.10 s) at `build/workflow/20261011T184425.284224Z/`, and a clean complete
`oracle-test` (2270.84 s) at `build/workflow/20261011T193215.016295Z/`.
The oracle rerun follows a repair to an ambiguous mutation selector; all
120 recovery mutations and 15 whole-host ownership mutations reject.
Older routine fixtures retain their documented input-race skips; zero-skip
whole-host and recovery results do not erase those exclusions.

The subsequent `$C618` multi-credit continue display fix adds three literal
ports, preserves all 12,382 preceding instruction bodies and all retained
fixtures, and passes the focused validation cadence. Its complete recovery
corpus and 10,000-frame multi-credit continue observations match MAME;
the three new instruction mutations reject. `test` (48.02 s) and
`verify-stage1` (78.07 s) pass at `build/workflow/20261011T202613.753296Z/`.
The rebuilt native game and complete 23-slot directed stage sweep also pass
at `build/workflow/20261011T202719.311646Z/`, with 13,800 input-window
frames and no stops. The natural comparison log is
`build/workflow/20261011T203052.179492Z/`.
The preceding complete oracle result above remains the last full checkpoint;
this local fix does not claim a fresh 123-mutation or full-suite run.

The historical `OEngine` Stage 1 reconstruction remains in regression tools,
not the live game. See [STAGE1_FIDELITY.md](STAGE1_FIDELITY.md).

The subsequent `$14D62` stage-1 full-throttle/cannon report adds one literal
`MOVEQ` fallback. Independent MAME fixtures pass and both operand/flag mutations
reject; the complete recovery corpus now covers 12,386 PCs and 77,794 fixtures
with zero differences/skips. The scripted input window does not reproduce the
user's exact trajectory; see ENGINE_NOTES for scope and validation.

The `$E570` canyon ground-target report exposes a nonempty award-queue
consumer. Its 18 missing instructions now match 146 independent MAME fixtures,
including all branch outcomes and countdown boundaries. Complete recovery
covers 12,404 PCs and 77,940 fixtures; see ENGINE_NOTES for shooting replay
and mutation validation scope.

### Daily progress — 2026-10-12

The 2026-10-12 native build includes the multi-credit continue fix (`$C618`),
full-throttle/cannon fallback (`$14D62`) and canyon ground-award consumer
(`$E570..$E5AE`), with independent MAME fixtures and retained mutations.
The ROM-font menu adds persistent window scaling, fullscreen and FPS/timing
settings. Its separate Sprites row provides Off, EPX, Scale3x, 2xSaI,
Super2xSaI and ScaleFX; native road/text coverage and palette shadows are
preserved. xBRZ awaits a compatible license. The CPU ScaleFX implementation
measured about 30 displayed FPS in a finite smoke; the subsequent GPU implementation is documented below.

Focused evidence and the remaining full-game acceptance limits are recorded in
[ENGINE_NOTES](ENGINE_NOTES.md#sprite-only-native-presentation-filters-2026-10-12).
The complete required main-push gate passes: `test` (4.74 s),
`verify-stage1` (45.51 s), and `oracle-test` (2653.29 s), including all 143
recovery mutations and 15 ownership mutations. Logs:
`build/workflow/20261011T220049.037304Z/`. Older routine fixtures retain their
documented input-race exclusions; recovery and whole-host checks have zero
unexplained differences/skips. Final filter/menu/build checks also pass after
formatting cleanup at `build/workflow/20261011T220319.186745Z/`.
ROMs, captures, screenshots and validation logs remain excluded from Git.

### Enhancement checkpoint — 2026-10-13

Main now uses SDL GPU compute for all five sprite scalers and native-size
composition, with embedded Metal/SPIR-V shaders and a CPU/software fallback.
Clouds, Trails, Explosions and Afterburner each have persisted Opaque (default)
or Translucent settings. Original board RGB, feedback and timing remain
unchanged. A finite GPU ScaleFX flight smoke reports 59.45–59.70 displayed FPS;
this is not sustained peak-load acceptance or a controlled kernel benchmark.

Controls now supports keyboard, normalized gamepad and raw joystick bindings,
axis/button/hat capture, deadzones, sensitivity, polarity, absolute throttle and
persisted GUID/type profiles. Hotplug, focus, reconnect and menu capture have
virtual-device coverage; named physical controllers and HOTAS still need testing.
Audio now supports six user-supplied WAV/FLAC mappings from `soundtrack/`, preview,
asynchronous decoding, Original fallback and separate music/effects/per-track
volumes. Original/100% preserves exact PCM. See
[music routing and remaining coverage](MUSIC_REPLACEMENT_PLAN.md).
Replacement music now uses a frontend-only -12 dB baseline for gameplay and
preview to balance mastered files against arcade effects. Current-song mapping
changes apply on resume without restarting the game. Full-scale additive
mixing, independent gains/mutes and the unchanged Original PCM pass focused
checks; see [native music presentation](ENGINE_NOTES.md#native-music-presentation-adapter-2026-10-13).

The latest complete `required` profile passed on 2026-10-13: `test` (7.31 s),
`verify-stage1` (55.33 s) and `oracle-test` (2896.63 s), including all 143 retained
recovery mutations, retained ownership mutations and four new music mutations.
Logs: `build/workflow/20261013T075232.029672Z/`. Historical routine input-race
exclusions remain; new music checks have zero skips. This full run precedes the
final explosion/flame presentation increment, whose focused checks pass:
`test`, `verify-stage1`, `verify-native-menu` and `native-release` at
`build/workflow/20261013T102805.986233Z/`; 570/570 synthetic GPU cases and all six
rejected GPU mutations at `build/workflow/20261013T101824.556086Z/`; and
4,128/4,128 native GPU readbacks, zero skips, at
`build/workflow/20261013T102941.774317Z/`. CMake builds, four targeted CTests and
90-frame Metal/software smokes also pass. These are retained results, not a new
validation run for this roadmap update. Detailed scope is in
[ENGINE_NOTES](ENGINE_NOTES.md#explosion-and-player-flame-presentation-transparency-2026-10-13).
Full-credit equivalence and the acceptance priorities below remain open.

The native Options menu includes **Video** for all display/graphics settings
and **DIP Switches** for persisted Lives/Credit, Ship Increase and Difficulty,
applied on a cold restart. Ship Increase exposes SWB:5 independently from the
3/4 ships setting at SWB:4, preserving existing preference values. Focused
settings/menu/build and ROM-free regression checks pass; the earlier Stage 1
result remains scoped to the unchanged session/engine. Alternate-setting
full-game acceptance remains open. Explosion translucency also covers the
player-crash fire/smoke streams at 128/255 opacity, and the optional player flame
is translucent only at the ROM's maximum throttle during flight. Its separate
exhaust sprites include the white cores at 128/255 opacity while the aircraft
and small nozzle glow retain their original appearance. See
[effect visibility and throttle scope](ENGINE_NOTES.md#explosion-visibility-and-full-throttle-only-flame-2026-10-13) and
[native DIP-switch menu](ENGINE_NOTES.md#native-dip-switch-menu-2026-10-13).
