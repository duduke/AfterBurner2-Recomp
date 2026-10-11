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
4. **Prove the test can fail.** Change one constant in the port, check the
   test fails, then revert. Check coverage too: a path the capture never
   exercised is unverified, and must be listed as such in `ENGINE_NOTES.md`.
5. **Never commit ROM data or captures.** ROMs live in `roms/`, captures in
   `build/oracle/`; both are gitignored. Don't paste ROM bytes or large
   tables into source files either: read them from the ROM at runtime.
6. **Keep the docs current**: tick roadmap items off with their results,
   and add what you learned to `ENGINE_NOTES.md`.
7. **Before committing**, run `make test`, `make verify-stage1` and
   `make oracle-test`, and confirm nothing got worse.

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

## Current checkpoint (2026-10-11)

`main` contains the SDL3 native engine, merged at `1f584d7` from the validated
`play-native` implementation `2a0c76b`. `make play-native` remains the launch
target; there is no longer a separate play-native branch. The retained
`codex/threejs-game-engine` branch exists locally and on origin.

| Area | Verified status | Limits |
| --- | --- | --- |
| Live game | `XBoardGame` / `XBoardHost` cold-boots the literal main/sub 68000 translations and original Z80 program; ROM code drives attract, coin/start, flight, weapons, HUD and sound | Unknown instructions stop explicitly; a complete natural credit is not yet accepted |
| Whole host | Cold 121-/603-frame and expanded 781-frame MAME corpora; the expanded run matches 28,128,435 CPU snapshots, 73,593,282 bus phases, 55,982,080 RGB pixels and 627,841 stereo frames, zero supplied execution/state/read seeds and zero differences/skips; 15 owner mutations reject | Finite input/state coverage, not every operand, branch, DIP setting or reset/suspend path |
| Instruction recovery | 10,525 instruction PCs, both outcomes at 1,060 branches, 68,294 MAME fixtures identical with zero skips; 85 recovery mutations reject; optimized native and Three.js core replays pass | Declared manifest only; future gameplay may expose more instructions |
| Stage discovery | All 23 ordered ROM slots complete 600 scripted input frames each, 13,800 frames total with zero stops | Debug entries bypass natural progression and do not recreate every previous-stage state; special stages can suppress actions |
| Selected-stage palettes | Stages 5, 8, 13 and 17 execute preceding ROM palette preparation; 56/56 gradient words match MAME, zero skipped | Directed debug startup, not full-credit acceptance |
| Native platform | SDL3 3.2+, Make/CMake builds, window/input/audio, pause/restart and no-speaker smoke checks pass | Short flight reaches the arcade's approximately 59.64 Hz cadence; every-stage peak-load performance remains unproved |
| Extension foundation | `GameSession` exposes controls, pause/restart, completed RGB frames and output PCM processing | Widescreen, new assets, gameplay modes and save states need deeper interfaces |
| Three.js | Optional original-art scene prototype shares the native core through a local bridge | Arcade projection; true 3D world geometry, true widescreen and standalone WASM remain future work |

Before publishing this documentation consolidation, all required checks passed
again on main: `test` (33.50 s), `verify-stage1` (73.68 s), and the complete
`oracle-test` (1823.36 s), including all 85 recovery mutations and retained
fixtures. Local logs are in `build/workflow/20261011T150418.391037Z/`.
Game source, build rules and oracle fixtures are unchanged from the merged
native implementation. Older routine fixtures retain their documented input-race
skips; zero-skip whole-host and recovery results do not erase those exclusions.

The historical `OEngine` Stage 1 reconstruction remains in regression tools,
not the live game. See [STAGE1_FIDELITY.md](STAGE1_FIDELITY.md).

## Next acceptance tasks

1. **Natural full-credit progression:** run uninterrupted physical coin/start
   and controls through the ordered stage sequence, refueling and carrier
   sequences to the ending. Do not substitute `--stage` for progression.
2. **Failure and recovery paths:** cover collisions, death/respawn, exhausted
   lives, game over, continue, scoring and high-score/NVRAM behavior in natural
   runs. Report exactly which transitions and states were observed.
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
remain required. Report exactly which settings and paths were exercised; one
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
| 5: Enhancements | Application hooks and Three.js prototype implemented; feature backlog follows |

## Phase 5 — Enhancements Phase

Optional features, visual upgrades, and quality-of-life additions for After Burner II:

The foundation retains translated ROM logic. `GameSession` now provides
normalized controls, pause/restart, frame notifications and output audio
processing for application enhancements. See
[ENHANCEMENT_ARCHITECTURE.md](ENHANCEMENT_ARCHITECTURE.md) for current contracts
and the subsystem interfaces required for gameplay, widescreen and asset
enhancements. This does not close Phase 3e/full-credit coverage or complete
the menus and content features below.

A local [Three.js direct-scene prototype](../prototypes/threejs/README.md)
now draws individual original-art sprite meshes and indexed ground/tile
geometry from native partial-clip state. GPU composition preserves palette,
priority and shadow semantics and reports a native RGB checksum diagnostic.
It retains the arcade projection; 3D world geometry, true widescreen and a
standalone WebAssembly build remain future work. This prototype does not
promote full-credit gameplay acceptance.

### 1. Menus, UI & Convenience
- [ ] **Frontend and Configuration Menus (Seamless In-Game Integration):**
  - *Native Visual Aesthetic:* Render menus directly through the engine's tilemap/sprite/text pipeline (or a cohesive HUD/cockpit overlay) using arcade font tiles and palette themes, avoiding external OS windows or generic popups.
  - *In-Game Pause & Settings Overlay:* Instant in-game pause menu with real-time previews for audio volume sliders, CRT shader toggles, and stick deadzone visualizers.
  - *Pre-Flight / Mission Select Screen:* Attract/menu screen styled after a military briefing terminal to select game modes (Arcade, Roguelite, Time Attack), view high scores, and configure flight controls.
  - *Integrated DIP Switch & Service Access:* Direct access to cabinet settings (difficulty, lives, coin ratios, sound test) in-game without editing text configs.
  - *Seamless Persistence:* Instant save and load of player configuration and high scores to JSON/INI files.
- [ ] **Save States:** Save and load full game state snapshots on demand.
- [ ] **Cannonball Ecosystem Enhancements:** Evaluate and adapt relevant QoL features from Cannonball, Cannonball-SE, and Cannonball DX.

### 2. Controls & Hardware Integration
- [ ] **Comprehensive Input Remapping & Auto-Detection:**
  - *Flexible Device Support:* Full custom binding for keyboard, gamepads, flight sticks, and HOTAS setups with deadzone, linearity, and sensitivity calibration.
  - *Out-of-the-Box Presets:* Auto-detection and profile mappings for:
    - *Modern Gamepads:* Xbox Controller, PlayStation DualSense / DualShock 4, Nintendo Switch Pro Controller (via SDL GameController DB).
    - *Flight Sticks & HOTAS:* Thrustmaster T.Flight HOTAS / T.16000M, Logitech G Extreme 3D Pro, Turtle Beach VelocityOne, VKB sticks.
    - *Retro Arcade Controllers:* Sega Saturn USB / 8BitDo M30, Astro City Mini arcade stick, generic USB encoders.
  - *Throttle Mapping Modes:* Incremental throttle, absolute lever axis, or bumper/trigger acceleration and airbrake.
- [ ] **Force Feedback (FFB) & Haptic Rumble:**
  - *Modern Gamepad Rumble:* SDL haptics/rumble cues for vulcan cannon fire, missile launches, afterburner ignition, incoming lock-on warnings, and explosion/flak hits.
  - *Flight Stick & DirectDrive Support:* Constant force centering and spring resistance scaling with airspeed/afterburner, pitch/roll G-load resistance, stall shudder, and turbulence.
  - *Moving Cabinet Emulation:* Translate arcade motor signals (ADC3/ADC4 feedback and Moving Deluxe cabinet motor commands) into motion rig telemetry (e.g. SimHub / UDP telemetry) or FFB output.

### 3. Graphics, Resolution & Display
- [x] Native desktop performance entry point (`make play-native`): verified
  release compiler options, physical cold boot to flight, and live simulation/
  display FPS diagnostics. Renderer clips road/tile/text work to the native
  partial rows, combines disjoint category passes and caches unchanged palette
  conversions. Original scheduler and sprite feedback remain authoritative.
  Short windowed flight reaches the original 59.64 Hz cadence; this does not
  establish full-credit or every-stage peak-load performance. See the native
  performance continuation in `ENGINE_NOTES.md` for checks and measurements.
- [ ] **Wide-Screen Support (16:9, 16:10, 21:9 Ultrawide):**
  - *Expanded Viewport:* Expand horizontal raster from 320×224 to 398×224 (16:9) or 526×224 (21:9) without stretching.
  - *Extended Road/Ground Raster:* Extend Sub CPU / `hwroad` scanline banking and horizon curves across wider display bounds.
  - *Tilemap Surface Wrapping:* Leverage X-Board's 1024×512 virtual tilemap surface to render wider scrolling backgrounds without clipping.
  - *Wide-Angle Projection & Culling:* Widen 3D camera frustum and sprite culling limits so enemy waves spawn and maneuver naturally across the full width.
  - *Flexible HUD Placement:* Toggle between classic 4:3 centered HUD or anchored widescreen HUD pushed to screen corners.
- [ ] **4K Ultra HD Support (3840×2160):**
  - *Existing Foundation:* SDL3 already presents the native raster through SDL_Renderer. High-resolution HUD/assets and the remaining display features below still need implementation.
  - *Integer Scaling:* Pixel-perfect 9× integer scale of native 320×224 raster with configurable pillarboxing or fill modes.
  - *Advanced Shaders:* High-density CRT emulation (scanlines, shadow mask, phosphor glow, curvature) and edge-preserving scalers (xBRZ, ScaleHQ).
  - *Crisp High-DPI HUD:* Optional native 4K text and instrument overlays to eliminate blurriness.
  - *Smooth Presentation:* Zero-copy frame presentation and sub-millisecond pacing for lockstep 60 Hz / 120 Hz refresh rates.
- [ ] **Upscaled & High-Resolution Sprites:**
  - *Extraction CLI Tool:* CLI dump option in main binary (e.g. `--dump-sprites` / `--extract-sprites` to PNG) for AI upscaling or texture repainting.
  - *High-Res Replacement Pipeline:* Transparent loading and scaled rendering of external high-resolution sprite sheets.
  - *Smoother 360° Rotation Steps:* High-granularity roll/rotation steps via additional intermediate sprite banks or runtime software/GPU micro-rotation interpolation.
- [ ] **Complete Sprite Replacement Option:** Option to substitute alternate custom sprite sets.

### 4. Gameplay Modes & Mission Content
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
- [ ] **Missile Evasion Score Bonus & Dynamic Audio Cue:**
  - *Near-Miss Scoring:* Extra score awarded for close-proximity evasion of incoming homing missiles.
  - *Dynamic 3D Sound Cue:* Distinct stereo "whoosh" sound effect when missiles whistle past the cockpit on near misses.
  - *Evasion Combo Multiplier:* Escalating bonus points for consecutive last-second defensive maneuvers (barrel rolls / high-G breaks).
- [ ] **Option: Sega Ages After Burner II (Saturn, 1996) Redbook Soundtrack:**
  - Optional support for high-quality CD audio tracks while preserving authentic YM2151/SegaPCM sound effects.
  - File-based music player mapping arcade music triggers to external user-provided audio files (e.g. in `res/music/`).
  - Seamless config toggle between authentic arcade FM/PCM music and CD soundtrack.
