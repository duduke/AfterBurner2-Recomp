# Roadmap

Current status and acceptance priorities live here. Detailed discoveries and
historical checkpoint evidence live in [ENGINE_NOTES.md](ENGINE_NOTES.md);
commands live in [WORKFLOW.md](WORKFLOW.md). Completed plans and PR reviews
remain in Git history rather than separate status documents.

## Working on the remaining tasks (read this first)

These instructions are for anyone picking up the next task, including AI
assistants. The goal is gameplay **identical** to the arcade ROM, so the
bar is "matches MAME byte-for-byte", not "looks right".

**Where things are**

- [`docs/HARDWARE.md`](HARDWARE.md): the X-Board hardware, from MAME's
  `segaxbd.cpp` and the `segaic16` chips.
- [`docs/ENGINE_NOTES.md`](ENGINE_NOTES.md): what's known about the 68000
  game code (addresses, RAM layout, routines), and the write-up of each
  verified port. Read "Oracle testing", "Sub CPU road generator" and
  "Video oracle" before starting.
- `src/main/engine/subbus.*`, `osubsprites.*`, `osubroad.*`: the
  reference examples of a verified port. Copy their structure.
- `tests/oracle_*.cpp` and `tools/oracle/*.lua`: the matching tests and
  MAME capture scripts. Copy these too.
- `docs/STAGE1_FIDELITY.md`: the retained historical reconstruction tests;
  these do not drive the live application.

**Rules**

1. **Never invent behaviour.** Every number, table, timing and branch
   comes from the ROM disassembly. If something isn't understood yet, leave
   it unported and say so; don't fill the gap with a plausible guess. The
   historical Stage 1 reconstruction is retained only for regression checks;
   do not use its guessed rules to fill gaps in the live translated game.
2. **Translate instruction by instruction**, keeping the arcade's memory
   layout: the ported code reads and writes the same addresses through a
   bus object (`SubBus` and `MainBus`). Comment each block
   with the ROM address it came from. Refactoring into readable code comes
   later, once a test pins the behaviour.
3. **Every port gets an oracle test before it's wired into the game.** A
   Lua script captures the state before and the writes after the routine
   in MAME, and a C++ test replays the "before" state through the port and
   compares. Report results as "N of M identical, K skipped (reason)".
4. **Prove each new or changed oracle can fail.** Run relevant deliberate
   mutations against a passing baseline before accepting the port or test
   change. Retain earlier mutations; use the validation cadence below rather
   than rebuilding every unchanged mutant during each edit. Check coverage
   too: a path the capture never exercised is unverified, and must be listed
   as such in `ENGINE_NOTES.md`.
5. **Never commit ROM data or captures.** ROMs live in `roms/`, captures in
   `build/oracle/`; both are gitignored. Don't paste ROM bytes or large
   tables into source files either: read them from the ROM at runtime.
6. **Keep the docs current**: tick roadmap items off with their results,
   and add what you learned to `ENGINE_NOTES.md`.
7. **Validate according to the change and acceptance gate.** Local commits
   require the applicable focused checks in the cadence below. Gameplay/core
   merges, releases and acceptance milestones require `make test`,
   `make verify-stage1` and the complete `make oracle-test`. Preserve all
   fixtures and mutations; a focused pass is not whole-host acceptance.

**Validation cadence**

| Change or gate | Required validation |
| --- | --- |
| Documentation only | `git diff --check`; inspect changed links, commands and policy consistency. No engine suites unless executable behavior also changes. |
| Development and local code commits | Run `test` and affected oracle targets; add `verify-stage1` for engine, game-session or integration changes, and a relevant build/smoke for platform/UI changes. Record the targets and results; do not rerun unrelated long recordings after each edit. |
| New or changed literal ports, fixtures or comparison gates | Passing independent MAME baselines, relevant branch/operand coverage and relevant rejected mutations before wiring/accepting. For recovery ports, retain and replay the complete corpus with its manifest/branch gate; repeat the affected natural recording. |
| Shared CPU/flags/timing, bus, scheduler, device logic, generator implementation, compiler/options or oracle instrumentation changes | Full regression and all retained mutations before accepting the change, plus affected natural recordings. Regenerate captures when their semantics or instrumentation changed. Adding generated instruction bodies alone follows the literal-port row. |
| Gameplay/core merge, release or declared acceptance milestone | Run the complete `required` profile (`test`, `verify-stage1`, `oracle-test`) on the final tree. Milestones also run the supplemental `full` profile and applicable complete natural recordings. |
| Periodic integration checkpoint | Run the complete regression/mutation suite after ten code merges since the last full pass, even when each merge only required narrow checks. Documentation-only merges do not count. This is a validation trigger, not a scheduled automation. |

Reuse an earlier pass only while its relevant sources and transitive
dependencies, compiler/options, fixtures, capture probe and comparison/mutation
harness are unchanged. Cite its log and scope explicitly; do not present it
as a new run or carry it across an affected change. A failure requires repair
and rerunning the failed gate and dependent checks. Before a merge or milestone,
a focused repair does not replace a clean complete run when that gate requires
one. Existing tools do not implement automatic result caching.

The current `oracle-test` includes all retained mutation checks, so the full
regression gates above still execute them. Selective mutations are for focused
work; splitting the full regression and mutation commands or adding validated
hash-based caching is separate tooling work. Never delete fixtures, weaken
comparisons or skip unexplained failures to meet this cadence.

**Commands** (from the repository root)

```bash
make -j4                    # build the game: ./afterburner2
make test                   # ROM-free engine checks
make verify-stage1          # Stage 1 integration suite (needs roms/)
make oracle-capture         # record MAME reference captures into build/oracle/
make oracle-test            # check the ports against those captures

# Disassemble: build a flat CPU image, then a linear listing of a range
python3 tools/build_rom_image.py maincpu "$TMPDIR/main.bin"    # or subcpu
python3 tools/m68k_disasm.py "$TMPDIR/main.bin" 144A2 14820
```

`make oracle-capture` needs a MAME binary and the `aburner2` ROM set: set
`MAME=` and `ROMPATH=` (defaults are in `tools/oracle/capture.sh`). The
scripts were written for MAME 0.287. The disassembler is linear and
deliberately incomplete: "unknown opcode" on a real execution path means
the decoder needs extending; in a data table it's expected.

**Oracle lessons learned (avoid repeating them)**

- Compare **write sets**, not whole RAM snapshots: log only the CPU under
  test's own writes (filter on `CURPC`), so the other CPU's concurrent
  writes can't cause false failures. A record is a race, and is skipped,
  only if the other CPU changed a byte the port actually read.
- Exclude stack areas: exception frames are pushed while the PC still
  points at the interrupted instruction, so no PC filter catches them.
- Keep Lua tap handles in global variables, or they get garbage-collected
  and silently stop firing. `screen:vpos()` isn't available; derive scan
  lines from `manager.machine.time` (64 us per line).
- In a frame notifier, `screen:pixels()` is the image drawn from the state
  at the *previous* notifier, converted through the *current* palette.
- For IRQ mask fixtures, execute a real SR-writing instruction. MAME's
  debugger SR setter does not update pending interrupt selection.
- Start each MAME run with fresh NVRAM and config (`capture.sh` does), or
  boot isn't deterministic.
- The game logic runs at 30 Hz with an interpolated sprite list on the
  in-between frames. Don't port anything at 60 Hz.

## Current checkpoint (2026-10-15)

`main` contains the SDL3 native engine, merged at `1f584d7` from the validated
`play-native` implementation `2a0c76b`. `make play-native` remains the launch
target; there is no longer a separate play-native branch. Building on `fa30d2b`,
this checkpoint expands detached widescreen sprite capacity and scenery,
adds stage/lifecycle visibility checks, fixes reproduced roll/cloud/takeoff
artifacts and recovers the enemy-coordinate overflow instruction at `$15C10`.
Existing enhancements include OpenAL cockpit audio, DIP/Video menus, GPU effects,
replacement music and SDL body/trigger rumble. The retained `codex/threejs-game-engine` branch
exists locally and on origin; its prototype source and targets are absent from main.

| Area | Verified status | Limits |
| --- | --- | --- |
| Live game | `XBoardGame` / `XBoardHost` cold-boots the literal main/sub 68000 translations and original Z80 program; ROM code drives attract, coin/start, flight, weapons, HUD and sound | Unknown instructions stop explicitly; the declared natural ending/state stream passes, while whole-credit CPU/device/RGB/audio acceptance remains open |
| Whole host | Cold 121-/603-frame and expanded 781-frame MAME corpora; the expanded run matches 28,128,435 CPU snapshots, 73,593,282 bus phases, 55,982,080 RGB pixels and 627,841 stereo frames, zero supplied execution/state/read seeds and zero differences/skips; 15 owner mutations reject | Finite input/state coverage, not every operand, branch, DIP setting or reset/suspend path |
| Instruction recovery | 12,405 recovery PCs, both outcomes at 1,255 branches, 77,954 MAME fixtures identical with zero skips; all 146 retained recovery mutations reject at the macOS preview checkpoint; optimized native natural replays pass | Declared manifest only; future gameplay may expose more instructions |
| Natural transitions | Cold 74,452-frame Easy / 99-credit run visits all 23 ordered slots, six refuels, ending, ranking and return to attract; every frame matches MAME across ten observation columns. Default three-life failure/continue/weapons runs match 12,000/10,000/8,000 frames; new multi-credit continue recording matches 10,000 frames, zero skipped | Named RAM/timestamp stream only; standard single-credit completion, continuous whole-credit CPU/device/RGB/audio, name entry and NVRAM persistence remain open; carrier states 13–15 are directed-only |
| Stage discovery | All 23 ordered ROM slots complete 600 scripted input frames each, 13,800 frames total with zero stops | Debug entries bypass natural progression and do not recreate every previous-stage state; special stages can suppress actions |
| Selected-stage palettes | Stages 5, 8, 13 and 17 execute preceding ROM palette preparation; 56/56 gradient words match MAME, zero skipped | Directed debug startup, not full-credit acceptance |
| Native platform | SDL3 3.4+, Make/CMake builds, window/input/audio, pause/restart, ROM-font settings with Video, Controls, Audio and DIP Switches submenus, persisted preferences and Metal/software launch checks pass | Physical controller/HOTAS calibration, other GPU backends and every-stage peak-load performance remain unproved |
| Enhanced presentation | Five GPU sprite scalers plus native-size Off composition; independent cloud, trail, explosion and afterburner opacity; 630/630 synthetic cases and 4,128/4,128 sampled native readbacks identical, zero skips; six GPU mutations reject | Original-art effects only; finite Metal scenes, not whole-credit visual/performance acceptance |
| Music and volume | Six persisted WAV/FLAC track mappings, preview/fallback, independent music/effects and per-track gains; 42,148/42,148 and 119,374/119,374 passive MAME routing transactions identical, zero skips | Natural contexts cover two songs and overlapping effects; four songs and natural fades remain unverified |
| Spatial audio (experimental) | Optional OpenAL Soft headphone HRTF, 2.1/5.1 modes, chip-rate interpolation, direct stereo music and live output menu; 1,800 native frames unchanged, 117,509 MAME ownership observations exact, six controls reject | Unclassified voices remain stereo; physical localization/channels, complete sound/cabinet audit and sustained latency remain open |
| Ground/horizon correction | Optional Corrected mode preserves terrain while removing sky-side wedges; 160/160 retained MAME frames reconstruct exactly, with both roll directions and retained mutations checked | Original stays default; finite roll/Metal/software coverage, not every-stage visual acceptance |
| Controller feedback | Cannon/missile/player-impact/full-throttle cues; 4,200 native RGB/PCM/time frames unchanged, independent MAME witnesses and eleven rejected mutations; Xbox Bluetooth body and both triggers physically confirmed on macOS | Windows/Linux and simple-haptic fallback hardware, full gameplay/lifecycle feel, incoming warnings and advanced FFB remain open |
| Widescreen preview | Detached scenery retention, recovered Sub requests, unwrapped X, added horizon segments and dynamic presentation capacity beyond native queue/depth limits; original simulation stays unchanged | Native gameplay/object pools remain original; exhaustive object/scene audit, sustained performance, Windows/Linux and other aspects remain open |
| Extension foundation | `GameSession` exposes controls, pause/restart, completed RGB frames, passive sprite planes/individual layers and immutable partial-render scene snapshots, output PCM processing and optional music/effects stems and events | Complete widescreen scene coverage, new assets, gameplay modes and save states need deeper interfaces |
| Three.js | Prototype source and browser-specific tests retained only on `codex/threejs-game-engine` | Removed from main; arcade projection, true 3D world geometry, widescreen and standalone WASM remain future work |

Current enhancements include Video/Controls/Audio/DIP Switches menus, GPU sprite
filters, optional original-art translucency, input remapping and replacement
music. DIP choices apply on cold restart; music mappings for the active song
apply on resume. Replacement music uses a 1/4 (-12 dB) baseline. Basic SDL rumble
is implemented for gamepads/raw joysticks, including trigger
motors and simple haptic fallback. Bluetooth Xbox body and both trigger motors
are physically confirmed on macOS; Windows/Linux hardware acceptance remains open.

The macOS preview's fresh complete `required` profile passes at
`build/workflow/20261015T162805.304852Z/` (`test`, `verify-stage1`, complete
`oracle-test`, all retained mutations), including the final rolling scenery,
water-field and clipped-horizon fixes. All 23 directed stage slots preserve
22,494 counted native RGB/PCM/time frames. The four natural lifecycle replays
match all ten MAME observation columns and exact original image reconstruction
across 104,452 frames, zero skips. These finite checks do not close whole-credit
CPU/device/RGB/audio or physical platform acceptance. The
[macOS package notes](ENGINE_NOTES.md#macos-preview-packaging-2026-10-15)
record dependency, source, signing and smoke-test scope. See
[checkpoint history](CHECKPOINT_HISTORY.md) for retained counts, timings and logs,
[README](../README.md#current-enhancements) for the current feature list, and
[ENGINE_NOTES](ENGINE_NOTES.md) for detailed coverage and exclusions.

A subsequent live-play stop at decimal PC 89104 (`$15C10`, enemy-coordinate
overflow rollback) adds one verified instruction. Its 14 MAME fixtures and three
rejected mutations pass; complete recovery now covers 12,405 PCs and 77,954
fixtures. Focused test/integration, release and stage-18 checks pass; the retained
74,452-frame ending replay matches all ten MAME transition observation columns.
The complete release gate now includes this port; whole-credit acceptance remains
open. See [the recovery notes](ENGINE_NOTES.md#reported-enemy-coordinate-overflow-stop-at-15c10-2026-10-14).

The 16:9 preview's subsequent visibility/roll update recovers hidden Sub requests,
retains scenery beyond the native cull boundary and extends the ROM horizon
segments. Focused MAME, geometry, lifecycle, both-roll and GPU checks pass;
Original simulation stays unchanged; the later detached-capacity extension also
recovers requests discarded by the native 192-request queue and full depth
buckets. All 23 ordered stage slots and retained natural lifecycle paths have
dedicated validation targets. See
[visibility coverage](ENGINE_NOTES.md#wider-object-visibility-and-horizon-coverage-2026-10-15).
The [capacity and stage/lifecycle checks](ENGINE_NOTES.md#widescreen-capacity-and-stagelifecycle-checks-2026-10-15)
describe the detached queue expansion and the exact coverage of the new sweeps.
The earlier capacity checkpoint’s clean `required` and supplemental `full` targets
pass at `build/workflow/20261015T093902.465525Z/`, followed by the release target.
The stage sweep preserves 22,494 counted native RGB/PCM/time frames; the four
natural lifecycle replays match all ten retained MAME observation columns and
original image reconstruction across 104,452 frames, zero skips. This closes
those finite widescreen checks, not the broader whole-credit acceptance below.
An outer-edge attract/takeoff report then exposed a flight-only gate discarding
freshly submitted horizon extensions. The [follow-up fix and validation](ENGINE_NOTES.md#attracttakeoff-outer-strip-omission-2026-10-15)
retain those strips and add explicit non-flight coverage; complete visual scene
coverage remains distinct from the lifecycle progression checks.
Further reported non-flight blocks and horizon joins exposed flight-only sky
correction and lost interpolation history. The [follow-up coverage](ENGINE_NOTES.md#non-flight-fill-blocks-and-horizon-joins-2026-10-15)
now checks both side-band correction and matching halfway movement in attract
and takeoff, alongside the original native-output comparisons.
Further [screenshot-driven side-band checks](ENGINE_NOTES.md#screenshot-driven-outer-background-coverage-2026-10-15)
cover translucent cloud backgrounds, carrier sea fill and a bottom-clipped
departure horizon. Added ground fill requires an observed native-row color;
unknown silhouettes/source coverage remain explicit preview limitations.
The subsequent [rolling scenery, takeoff waves and banked cloud fixes](ENGINE_NOTES.md#rolling-scenery-takeoff-waves-and-banked-cloud-corners-2026-10-15)
use live shared camera data, repeat the native takeoff wave field at the wider
edges and classify clipped horizon corners. Directed rolls cover both actual
rotation directions; original RGB/PCM/time and protected-layer checks remain
enabled. These fixes do not close exhaustive widescreen acceptance.

## Next acceptance tasks

On 2026-10-15, the user reported finishing the game from start to finish on
macOS with the saved Normal / three-ship configuration and current enhancements.
See [the manual completion settings](ENGINE_NOTES.md#user-reported-macos-completion-2026-10-15).
This supplies manual completion evidence; an input recording and independent
whole-credit comparison remain outstanding.

1. **Standard single-credit completion:** the uninterrupted Easy / 99-credit
   recording reaches the ending naturally and matches every named observation.
   Obtain a successful default three-life physical recording as well. Keep
   the declared settings; do not substitute `--stage` for progression.
2. **Remaining transition paths:** the default failure, continue and scoring
   recordings pass. Extend collision-cause, high-score name entry, NVRAM
   persistence and alternate carrier/lifecycle coverage, reporting actual
   states/actions rather than assuming requested inputs succeeded.
3. **Close newly exposed instruction paths:** translate from ROM, add
   independent MAME fixtures and rejected mutations, preserve older coverage,
   then repeat the affected gameplay and required regression gates.
4. **Whole-credit comparison:** extend the passive MAME oracle to the same
   sustained physical-input runs; compare CPU/bus, device, RGB and audio state
   without supplied execution or replacement state.
5. **Configuration and lifecycle coverage:** test alternate DIP settings,
   additional operands/branch outcomes, and hardware reset/suspend paths.
6. **Sustained performance:** measure representative and peak-load stages with
   rendering, audio and presentation active. Optimization must preserve update
   order and observed behavior; finite benchmarks do not guarantee 60 fps.

Enhancements can be developed alongside this work using the contracts in
[ENHANCEMENT_ARCHITECTURE.md](ENHANCEMENT_ARCHITECTURE.md). Keep an original
mode as the regression reference; new features do not close full-game coverage.

## Natural full-game completion and transition coverage plan

This is planned acceptance work, not a claim that a complete credit has passed.
Start with physical-input recording and passive transition diagnostics, then
build a reproducible successful playthrough. Manual play can supply the first
successful recording; an automated input controller is optional later work.

1. **Record from cold boot.** Capture coin, Start, axes, throttle and weapons
   against physical board-frame numbers. Retain ROM identifiers, DIP settings,
   initial NVRAM/configuration and input-format version. Use normal gameplay;
   no stage skip, injected invulnerability or RAM/state patches. Store recordings
   and ROM-derived outputs under ignored `build/oracle/`.
2. **Observe natural transitions.** Add read-only diagnostics for stage,
   game mode, lives and transition events, using verified ROM producers/state.
   Observations must not perform timed bus transactions or change execution.
   Record ordinary stage boundaries, refueling and carrier sequences, and the
   ending with frame/time markers. Keep attempted inputs separate from actions
   actually observed, especially when a sequence disables weapons or rolls.
3. **Recover and replay failures.** Preserve the input recording, exact stopped
   PC and last completed frame. Translate each newly reached instruction from
   ROM; require independent MAME fixtures, both relevant branch outcomes and
   rejected mutations before wiring. Replay the complete input prefix from
   cold boot after each fix. Retain the stage sweep as supplementary discovery,
   not evidence of natural progression.
4. **Establish the MAME reference.** Replay the same frame-indexed physical
   inputs with the same ROMs, DIP switches and initial persistent state. Confirm
   that MAME reaches the ending; distinguish an unsuccessful player run from
   a native divergence. If the sequence cannot survive, record a successful
   physical-input sequence rather than altering game state to force completion.
5. **Extend continuous comparison.** Run both engines through the entire
   credit with independent CPU scheduling and progression. Compare CPU/bus
   state and timing, devices, RGB and audio throughout the observation window.
   Compact comparisons must cover the entire declared stream, not just stage
   boundaries; retain counts and mismatch positions. Reproduce mismatches from
   cold boot with detailed traces around the failure to bound storage. Captures
   remain passive assertions and must never supply execution or replacement
   native state. Require a deliberate mutation to prove the new gate can fail.
6. **Add transition/failure recordings.** A successful credit is only one path.
   Add recordings for collisions, death/respawn, exhausted lives, game over,
   continue, scoring, high-score entry and NVRAM persistence. Exercise alternate
   DIP settings and reset/suspend separately, documenting observed branches,
   repeated transitions and explicit exclusions.

**Acceptance:** a reproducible uninterrupted physical-input run reaches the
ending and matches MAME throughout its declared CPU/device/video/audio streams,
with zero unexplained differences or skipped intervals. Separate transition
and failure recordings also pass, and all retained regression/mutation gates
remain required at the validation cadence above. Report exactly which settings
and paths were exercised; one
successful credit does not establish every possible operand or input trajectory.
Publish commands, fixture metadata, results and exclusions in ENGINE_NOTES,
then update this roadmap. Do not commit ROM data, recordings or captures.

## Phase overview

| Phase | Status |
| --- | --- |
| 1: Scaffold | Complete |
| 2: Video hardware | Implemented; finite MAME raster and ownership comparisons pass |
| 3a–3c: buses, scenery, frame/sprite/palette pipeline | Implemented and retained in oracle coverage |
| 3d: input, objects, flight and host integration | Live translated host accepted for the declared corpora; unobserved paths remain open |
| 3e: stages and game flow | Directed stage entry and sampled gameplay pass; natural full-credit acceptance is the next milestone |
| 3f: full-game equivalence | Pending uninterrupted whole-credit comparisons |
| 4: Audio | Original Z80, YM2151/SegaPCM, deadlines and mixed speaker output match declared MAME corpora; full-credit command/audio coverage remains open |
| 5: Enhancements | Native settings, GPU scalers/composition, four opacity controls, input remapping and WAV/FLAC music/volume implemented; physical hardware, advanced display/assets and gameplay extensions remain open |

## Phase 5 — Enhancements Phase

Optional features, visual upgrades, and quality-of-life additions for After Burner II:

The foundation retains translated ROM logic. `GameSession` now provides
normalized controls, pause/restart, frame notifications and output audio
processing for application enhancements. See
[ENHANCEMENT_ARCHITECTURE.md](ENHANCEMENT_ARCHITECTURE.md) for current contracts
and the subsystem interfaces required for gameplay, widescreen and asset
enhancements. This does not close Phase 3e/full-credit coverage or complete
the menus and content features below.

The [Three.js direct-scene prototype](https://github.com/duduke/AfterBurner2-Recomp/tree/codex/threejs-game-engine)
is retained on its own branch. Main contains the native SDL application and
its validation tools; no browser prototype source or targets remain here.

### Implementation order — easiest to hardest

This is an estimated order for the remaining enhancement work, based on current
interfaces and the effort to implement and validate it. It is not a schedule;
adjacent items may change order after investigation. Completed menu, scaling,
sprite-filter, original-art opacity, input-remapping, music/volume and
native-performance foundations are excluded. Large features are split where their first useful increment is substantially easier than their
full scope. The detailed feature descriptions remain grouped by topic below.

| Order | Enhancement / increment | Difficulty | Main dependency or source of effort |
| --- | --- | --- | --- |
| 1 | Additional persisted menu options | Easy | Existing menu/settings hooks; music, effects and per-track volume controls are implemented |
| 2 | CRT shaders and display pacing improvements | Moderate | Presentation backend, shader integration and frame-time measurements |
| 3 | Controller/HOTAS auto-detection and presets | Moderate | SDL3 gamepad database and generic joystick defaults implemented; physical-device calibration/testing remains |
| 4 | Service access and high-score persistence | Moderate | Lives/credit, difficulty and Ship Increase DIP menu implemented; service UI and NVRAM persistence remain |
| 5 | Sprite extraction CLI | Moderate | ROM decoding, frame boundaries and reproducible asset identification |
| 6 | Gamepad rumble | Implemented; hardware checks open | SDL body/trigger output, verified weapon/impact/boost cues; macOS Bluetooth confirmed |
| 7 | Mission/stage selection and Time Attack | Hard | Verified stage entry, initialization and mode/timing interfaces |
| 8 | Replacement sprites, larger textures and full-color assets | Hard | Individual sprite data before clipping, asset mapping and enhanced composition |
| 9 | Replacement RGBA effects, soft edges and additional visual particles | Hard | Individual sprite composition, depth/priority, RGBA support and overlap tests |
| 10 | High-resolution HUD and smoother sprite rotation | Hard | HUD/asset interfaces, new art and stable sprite pivots/ordering |
| 11 | 120/144 Hz interpolated presentation | Hard | Scene/state history, interpolation and display pacing independent of simulation |
| 12 | True widescreen | Hard | Wider road/tile rendering, projection, culling, scenery coverage and HUD placement |
| 13 | Further ground/horizon projection improvements | Hard | Targeted roll-wedge correction implemented; continuous terrain projection and broader stage/transition coverage remain |
| 14 | Save states | Hard–very hard | Complete CPU/device/continuation serialization and deterministic resume |
| 15 | Missile near-miss scoring, combos and spatial sound cues | Hard–very hard | Verified trajectories/collision events, scoring policy and audio events |
| 16 | Expanded gameplay object counts and basic arranged mode | Very hard | Object ownership/capacity, stage sequencing, modifiers and balance; visual particles alone do not expand gameplay capacity |
| 17 | Optional 60 Hz gameplay updates | Very hard | Movement/timer/fire/spawn adaptation, rounding, collision order and balance |
| 18 | Flight-stick forces and moving-cabinet integration | Very hard | Verified telemetry, device protocols and physical hardware validation |
| 19 | Procedural environments and formations, new special stages, interactive landing/refueling | Very hard | New content/art, mission and behavior systems, transitions and extensive playtesting |
| 20 | Local two-player wingman mode | Very hard | Second player, camera/HUD, targeting, collisions, scoring and balance |
| 21 | Networked co-op / rollback | Highest | Multiplayer plus deterministic snapshots, synchronization, prediction and recovery |

Cannonball ecosystem adaptation is an investigation item rather than a single
implementation: select concrete features and place each in this order according
to its dependencies. Keep original-mode regressions throughout; enhancement
work does not close the separate full-game acceptance tasks.

### 1. Menus, UI & Convenience
- [x] Initial native settings menu: ROM-font Start/Options/Quit, pause/resume,
  SDL nearest/linear/integer/Pixel Art scaling, desktop fullscreen, FPS/frame-time
  statistics and persisted presentation settings. Esc/F1 opens the menu; F11/F3
  provide fullscreen/statistics shortcuts. Controls remapping, Audio submenus
  and four effect-opacity settings are also implemented. CRT shaders, calibration
  visualizers and cabinet options remain part of the broader menu work below.
- [ ] **Frontend and Configuration Menus (Seamless In-Game Integration):**
  - *Native Visual Aesthetic:* Render menus directly through the engine's tilemap/sprite/text pipeline (or a cohesive HUD/cockpit overlay) using arcade font tiles and palette themes, avoiding external OS windows or generic popups.
  - *In-Game Pause & Settings Overlay:* Instant in-game pause menu with real-time previews for audio volume sliders, CRT shader toggles, and stick deadzone visualizers.
  - *Pre-Flight / Mission Select Screen:* Attract/menu screen styled after a military briefing terminal to select game modes (Arcade, Roguelite, Time Attack), view high scores, and configure flight controls.
  - *Remaining Cabinet Access:* Difficulty, Lives/Credit and Ship Increase are implemented; coin ratios, service/sound test and high-score persistence remain.
  - *Seamless Persistence:* Instant save and load of player configuration and high scores to JSON/INI files.
- [ ] **Save States:** Save and load full game state snapshots on demand.
- [ ] **Cannonball Ecosystem Enhancements:** Evaluate and adapt relevant QoL features from Cannonball, Cannonball-SE, and Cannonball DX.

### 2. Controls & Hardware Integration
- [x] **Native SDL3 Input Remapping Foundation:** Keyboard, gamepad and joystick
  axes/buttons/hats, deadzone/sensitivity, polarity, absolute throttle, remappable
  Menu action, hotplug and persisted GUID/type profiles. SDL built-in gamepad
  mappings are supplemented with the offline community database. Virtual-device
  and native-menu tests cover this increment; physical device-specific calibration
  remains in the broader item below.
- [ ] **Comprehensive Input Remapping & Auto-Detection:**
  - *Flexible Device Support:* Full custom binding for keyboard, gamepads, flight sticks, and HOTAS setups with deadzone, linearity, and sensitivity calibration.
  - *Out-of-the-Box Presets:* Auto-detection and profile mappings for:
    - *Modern Gamepads:* Xbox Controller, PlayStation DualSense / DualShock 4, Nintendo Switch Pro Controller (via SDL GameController DB).
    - *Flight Sticks & HOTAS:* Thrustmaster T.Flight HOTAS / T.16000M, Logitech G Extreme 3D Pro, Turtle Beach VelocityOne, VKB sticks.
    - *Retro Arcade Controllers:* Sega Saturn USB / 8BitDo M30, Astro City Mini arcade stick, generic USB encoders.
  - *Throttle Mapping Modes:* Incremental throttle, absolute lever axis, or bumper/trigger acceleration and airbrake.
- [ ] **Force Feedback (FFB) & Haptic Rumble:**
  - *Implementation plan:* [SDL haptics and force feedback](HAPTICS_PLAN.md), covering basic rumble/haptics on Windows, Linux and macOS, plus Windows Xbox Series X trigger motor verification, capability fallbacks, passive gameplay cues and output lifecycle tests. Basic output is implemented; macOS Xbox Bluetooth body and both triggers are physically confirmed. Windows/Linux hardware acceptance and advanced FFB remain open.
  - [x] *Modern Gamepad Rumble:* Verified cannon, missile, player damage and full-throttle cues; independent body/trigger strengths, finite motor tests and lifecycle stops.
  - [ ] Incoming-warning cues and advanced force-feedback effects require separate verification.
  - *Flight Stick & DirectDrive Support:* Capability-driven spring/damper centering, recoil and bounded impacts; later resistance may use verified speed telemetry. G-load, stall and turbulence models remain speculative enhancement proposals.
  - *Moving Cabinet Emulation:* Translate arcade motor signals (ADC3/ADC4 feedback and Moving Deluxe cabinet motor commands) into motion rig telemetry (e.g. SimHub / UDP telemetry) or FFB output.

### 3. Graphics, Resolution & Display
- [x] **GPU Sprite/Background Composition and Original-Art Transparency:**
  GPU composition also runs with Sprites Off. Optional persisted Clouds, Trails, Explosions and
  Afterburner rows independently select Opaque (default) or Translucent. A passive
  native descriptor decoder retains overlapping layers; GPU blending can reveal
  sprites behind known effect assets while keeping native tile priority and
  shadow coverage. CPU board rendering/feedback remains authoritative, with a
  software fallback. Known asset/LOD identification, blend/ordering comparisons,
  rejected opacity mutations and finite Metal smoke evidence are in ENGINE_NOTES.
  Replacement RGBA art, soft edge textures, age-based trail fading and full-game
  asset/performance coverage remain future work.
- [x] **SDL GPU Sprite Scalers:** Native SDL 3.4 GPU renderer with compute
  filtering/composition for EPX, Scale3x, 2xSaI, Super2xSaI and ScaleFX.
  Preserve original board RGB/state and protected road/text/shadow coverage;
  keep CPU reference filters and an explicit software fallback. Embedded Metal
  and SPIR-V shaders avoid a runtime shader-tool dependency. Metal validation
  and finite performance evidence are recorded in ENGINE_NOTES; other GPU
  backends and sustained peak-load performance remain unverified.
- [x] Native desktop performance entry point (`make play-native`): verified
  release compiler options, physical cold boot to flight, and live simulation/
  display FPS diagnostics. Renderer clips road/tile/text work to the native
  partial rows, combines disjoint category passes and caches unchanged palette
  conversions. Original scheduler and sprite feedback remain authoritative.
  Short windowed flight reaches the original 59.64 Hz cadence; this does not
  establish full-credit or every-stage peak-load performance. See the native
  performance continuation in `ENGINE_NOTES.md` for checks and measurements.
- [ ] **Wide-Screen Support (16:9, 16:10, 21:9 Ultrawide):**
  - *Implementation plan:* [True widescreen](WIDESCREEN_PLAN.md): a staged 16:9-first plan for passive partial-frame export, wider road/tile/sprite rendering, visibility audits, corrected-ground/effect integration and frontend/CPU/GPU acceptance. Experimental 16:9 Preview is implemented: 398×224 road/tile/published-sprite rendering, centered HUD, existing CPU/GPU filters/effects and persisted Video setting. Detached scenery retention, wider Sub projection and horizon segment extension address the identified edge/roll gaps. Dynamic presentation lists also recover native request-capacity/depth-bucket drops. All 23 ordered slots and retained natural lifecycle paths have dedicated checks. Exhaustive object/scene coverage, sustained performance, 16:10 and ultrawide acceptance remain open. Original combat and gameplay object allocation stay unchanged; custom stages are separate.
  - *Scene follow-up:* Retained scenery uses live shared roll/position data; takeoff extends the native water field, and Corrected ground handles clipped and banked wide-plane corners. See [implementation and validation](ENGINE_NOTES.md#rolling-scenery-takeoff-waves-and-banked-cloud-corners-2026-10-15).
  - *Expanded Viewport:* Retain native 320×224 simulation; derive wider presentation dimensions from target display and pixel aspect, without stretching. See the plan for rounding and the separate 4:3-correction policy.
  - *Extended Road/Ground Raster:* Render immutable native scanline data across wider bounds in a separate presentation renderer; integrate the optional corrected-ground silhouettes without changing Sub CPU execution.
  - *Tilemap Surface Wrapping:* Leverage X-Board's 1024×512 virtual tilemap surface to render wider scrolling backgrounds without clipping.
  - *Projection & Culling:* Audit rejection before publication and retain additional renderable content where verified. Initial combat/spawn rules stay native; full-width formations are separate enhanced gameplay work.
  - *HUD Placement:* Keep the HUD in the centered native safe area initially; semantic edge anchoring is a later option, independent of pixel-aspect correction.
- [ ] **4K Ultra HD Support (3840×2160):**
  - *Existing Foundation:* SDL3 already presents the native raster through its GPU renderer, with a software fallback. High-resolution HUD/assets and the remaining display features below still need implementation.
  - *Integer Scaling:* Pixel-perfect 9× integer scale of native 320×224 raster with configurable pillarboxing or fill modes.
  - *Advanced Shaders:* High-density CRT emulation (scanlines, shadow mask, phosphor glow, curvature) and edge-preserving scalers (xBRZ, ScaleHQ).
  - *Crisp High-DPI HUD:* Optional native 4K text and instrument overlays to eliminate blurriness.
  - *Smooth Presentation:* Zero-copy frame presentation and sub-millisecond pacing for lockstep 60 Hz / 120 Hz refresh rates.
- [ ] **Upscaled & High-Resolution Sprites:**
  - *Implemented presentation filters:* The native Options → Video → Sprites row offers Off, EPX 2x, Scale3x, 2xSaI, Super2xSaI and ScaleFX 3x. Road/tiles, foreground text coverage and palette shadows retain their native presentation before the separate SDL window scaler. xBRZ is pending license compatibility. These filters do not implement the replacement-asset or rotation features below.
  - *Extraction CLI Tool:* CLI dump option in main binary (e.g. `--dump-sprites` / `--extract-sprites` to PNG) for AI upscaling or texture repainting.
  - *High-Res Replacement Pipeline:* Transparent loading and scaled rendering of external high-resolution sprite sheets.
  - *Smoother 360° Rotation Steps:* High-granularity roll/rotation steps via additional intermediate sprite banks or runtime software/GPU micro-rotation interpolation.
- [ ] **Complete Sprite Replacement Option:** Option to substitute alternate custom sprite sets.
- [ ] **Expanded Sprite Rendering and Assets:** Optional enhanced renderer
  supporting larger sprite textures, more visual sprites and full-color
  RGB/RGBA assets beyond the original hardware's size, descriptor-count and
  palette restrictions. Expose individual sprite/scene data before native
  clipping and composition, retaining original mode as the regression reference.
  Account for both the hardware's 256 descriptor slots and the game's 192-request
  cap; increasing renderer capacity alone does not increase game-produced sprites.
  The wider renderer now captures requests before the native queue rejection,
  recovers full-depth-bucket drops and uses dynamic descriptor storage. This
  removes those presentation bottlenecks without changing native object pools;
  larger/full-color/replacement assets remain separate work.
  Add visual particles, denser scenery and longer trails through an enhancement
  interface; additional gameplay objects require separate game-logic work.
  Measure sustained performance and preserve intended depth, HUD priority and
  shadow behavior. See [ENHANCEMENT_ARCHITECTURE.md](ENHANCEMENT_ARCHITECTURE.md).
- [ ] **Selective Sprite Translucency and Effects:** Start with independently
  configurable cloud and missile-trail translucency (original-art opacity is
  implemented above), then support replacement RGBA textures with soft cloud edges
  and gradually fading trails. Composite individual sprites in the correct
  depth/priority order so scenery and other sprites remain visible behind
  translucent effects. The implemented passive individual-layer decoder provides
  the starting interface; replacement RGBA assets and lifetime-based fading
  remain open. Preserve foreground/HUD protection and native shadow semantics,
  retain original mode, and validate overlapping effects and performance.
- [x] **Targeted Roll-Wedge Correction:** Options → Video → Ground selects Original
  (default) or Corrected. A passive horizon-art/roll observer corrects plain
  road-0 ground fill on the sky side of the published strip; native RGB/PCM,
  terrain below the strip, sprites and foreground priority remain unchanged.
  This is a targeted normal-road correction, not continuous 3D terrain. See
  [ground correction evidence](ENGINE_NOTES.md#horizon-correction-both-roll-directions-2026-10-13).
- [ ] **Further Ground/Horizon Rendering During Rolls:** Optional enhanced
  rendering to eliminate the blue triangular wedge above the horizon on the
  right during left rolls and on the left during right rolls. The prior roll
  investigation observed the wedge in MAME and matched 160 captured frames
  pixel-for-pixel; retain that behavior in original mode. Investigate the exact
  generating conditions in the scanline-based ground/bank pipeline before
  extending the targeted correction into continuous ground/horizon projection.
  Preserve ground patterns, bank/pitch/altitude response, sprite layering and
  HUD priority. Validate both roll directions, steep and inverted banks,
  representative stage backgrounds and sustained performance. This is ground
  rendering work, separate from sprite filtering; its estimated difficulty is
  Hard, alongside true widescreen.

### 4. Gameplay Modes & Mission Content
- [ ] **Optional 60 Hz Gameplay Update Mode:** Evaluate smoother motion and
  reduced input latency by increasing gameplay updates from the arcade's
  approximately 29.82 Hz to approximately 59.64 Hz, keeping the same game pace.
  Preserve the original 30/60 Hz logic/display cadence as the default reference.
  Adapt movement, acceleration, timers, weapon firing and spawning through
  explicit enhancement interfaces; assess integer rounding, collision ordering
  and sprite interpolation rather than simply doubling routine calls or
  halving constants. Compare input response, motion, gameplay balance and
  sustained performance against original mode, retaining its MAME regressions.
  First evaluate 120/144 Hz presentation interpolation with the original
  simulation as a smaller visual enhancement; this requires scene/state data
  beyond the completed RGB frame. See [ENHANCEMENT_ARCHITECTURE.md](ENHANCEMENT_ARCHITECTURE.md).
- [ ] **Random Gameplay Mode (Roguelite / Arranged Mode):**
  - *Seeded Runs:* Custom seed entry and daily challenge seeds for competitive, reproducible runs.
  - *Shuffled Stage Progression:* Dynamic stage sequencing with randomized time-of-day palettes and environment tilemaps.
  - *New Procedural Environment Types:*
    - *Night Sortie / Searchlight & Flak Storm:* Dark skies lit by tracer fire, afterburners, searchlights, and dense AAA flak curtains.
    - *Sea-Skimming Naval Fleet Assault:* Low-altitude anti-ship strike over stormy seas, wave-skimming under SAM radar nets to attack warships.
    - *Supersonic City Trench / Megacity Skyline Run:* Urban obstacle weaving through skyscraper canyons, monorails, and bridges.
    - *Severe Weather / Typhoon Penetration:* Heavy crosswinds, wind shear turbulence, and rain sheets requiring active stick correction.
  - *Procedural Formations & Hazards:* Randomized enemy entry angles, missile aggression, rock pillars, supply encounters, and surprise SP stages.
  - *Run Modifiers & Mutators:* Optional toggles (unlimited missiles, hyper speed, glass cannon one-hit death, fog of war, inverted flight).
- [ ] **Optional SP (Special) Stages:**
  - *Airborne Fortress / Arsenal Bird:* Multi-phase boss fight against a giant airborne aircraft carrier bristling with point defense and drone swarms.
  - *ICBM High-Speed Interception:* Maximum-afterburner pursuit and terminal precision strike against an ascending ballistic missile.
  - *Naval Fleet Flagship Strike:* Low-altitude wave-skimming penetration to neutralize a carrier battle group flagship.
  - *Megacity Canyon Dash:* Extreme-speed urban obstacle course threading needle gaps between illuminated skyscrapers.
  - *Typhoon Eye Penetration:* Fight through severe hurricane turbulence and lightning to intercept an elite ace squadron inside the calm eye.
- [ ] **Optional Interactive Landing & Refueling Sequences:**
  - *Aircraft Carrier Refueling Point:* Add carrier touch-and-go landing as an inbetween-missions refueling point.
  - *Interactive QTE & Fine Controls:* Short, arcade-paced (~10s) docking inputs:
    - *Pitch:* Up / Down (glide slope / boom altitude alignment)
    - *Bank:* Left / Right (centerline / drogue basket alignment)
    - *Throttle:* Increase / Decrease (airspeed matching / arrestor hook capture)
  - *Seamless Combat Return:* Smooth transition straight back to combat upon successful contact/touchdown.
- [ ] **Optional 2-Player Co-op / Competitive Wingman Mode:**
  - *Shared-Screen Wingman Flight:* Two F-14 Tomcats airborne simultaneously on screen with distinct liveries.
  - *Competitive Scoring:* Fly cooperatively through stages while competing for top score, kill count, accuracy, and combo chains.
  - *Dual HUD:* Split telemetry and instrument readouts for both players.
  - *Flight Options:* Toggle friendly fire and collision models (no-clip vs. wingtip turbulence).
  - *Multiplayer:* Local dual-controller couch co-op and optional rollback/lockstep peer-to-peer netplay.

### 5. Audio & Scoring Enhancements
- [x] **Experimental OpenAL Soft Interpolation & Cockpit Spatial Audio:**
  - [Implementation proposal](SPATIAL_AUDIO_PLAN.md): optional headphones/HRTF,
    2.1 and 5.1 profiles, four head-relative virtual cockpit speakers, direct
    stereo music, verified voice routing and selectable resampling.
  - Implemented chip-rate FM/PCM presentation, direct stereo music, live output
    profiles/resamplers and a four-speaker test; Original SDL/native PCM remains
    the reference. Physical platform listening, remaining voice classifications,
    cabinet wiring and discrete LFE acceptance remain open.
- [ ] **Missile Evasion Score Bonus & Dynamic Audio Cue:**
  - *Near-Miss Scoring:* Extra score awarded for close-proximity evasion of incoming homing missiles.
  - *Dynamic 3D Sound Cue:* Distinct stereo "whoosh" sound effect when missiles whistle past the cockpit on near misses.
  - *Evasion Combo Multiplier:* Escalating bonus points for consecutive last-second defensive maneuvers (barrel rolls / high-G breaks).
- [x] **Per-Track WAV/FLAC Music Replacement:**
  - Native Options → Audio → Music Tracks cycles Original and WAV/FLAC files
    directly in `soundtrack/`; no picker. Six persisted mappings, asynchronous
    decoding, file preview and Original fallback are implemented.
  - Independent global music/effects and six track volumes support 0–100%,
    5% steps, live gain changes and reset. File changes apply at the next cue.
  - Passive ROM-backed FM ownership/PCM stems preserve native execution and
    exact all-Original/100% PCM. MAME routing checks cover 781/1,800 frames;
    natural evidence includes two songs and FM effects voice stealing.
  - Four remaining songs' natural contexts and natural fades still need coverage.
    See [implementation and validation details](MUSIC_REPLACEMENT_PLAN.md).
- [x] **User-Provided CD Soundtrack Playback Foundation:**
  - The per-track submenu above supports user-provided WAV/FLAC versions of
    Sega Ages After Burner II (Saturn, 1996) music, preserving native effects.
  - Each track can select Original or a replacement; no soundtrack is bundled.
    Complete natural song/fade coverage remains open as documented above.

### SDL3 input remapping — 2026-10-12

Implemented frontend Controls menu with keyboard, normalized gamepad and raw
joystick axis/button/hat bindings, deadzones, sensitivity, axis polarity and
full-range joystick throttle. SDL3 handles device discovery and hotplug; the
pinned SDL_GameControllerDB supplements built-in common gamepad mappings.
Bindings persist per GUID/type and reconnect safely. Virtual SDL devices cover
remapping, neutral capture, focus, reconnect and malformed preferences; menu
integration exercises capture/clear/navigation. Physical gamepads and HOTAS
calibration across specific hardware remain to be tested. No ROM routines or
board-input format changed. See ENGINE_NOTES for focused validation scope.
