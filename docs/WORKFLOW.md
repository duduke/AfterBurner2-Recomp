# Efficient local workflow

Build/play basics are in the [project README](../README.md). Use this reference
for checks, captures and instruction recovery; see the [documentation guide](README.md)
for other topics.

Read the live rules, checkpoint and ordered next tasks:

```sh
python3 tools/workflow.py context
rg -n 'topic|ROM-address' docs/ENGINE_NOTES.md src/main tests tools/oracle
sed -n 'START,ENDp' path/to/relevant/file
```

The context command extracts the rules, current checkpoint and next tasks
from ROADMAP; historical checkpoints live in [CHECKPOINT_HISTORY](CHECKPOINT_HISTORY.md). Follow the roadmap's
instruction-level translation, MAME comparison, mutation and coverage rules.
Read relevant engine notes before making a port. Search first and read bounded
sections; generated `.inc` tables and entire historical documents are expensive
to put into an assistant's context.

## Checks and logs

```sh
python3 tools/workflow.py run quick       # ROM-free tests + Stage 1
python3 tools/workflow.py run joint       # current 120-frame joint oracle
python3 tools/workflow.py run full        # 600-frame joint + reset window
python3 tools/workflow.py run required    # full gameplay/core merge or milestone gate
python3 tools/workflow.py run oracle-subirq -j 4  # any focused make target
```

The runner executes real make targets every time, stops on the first failure,
preserves exit status, and writes full output plus timings/evidence in
`build/workflow/<UTC-run-id>/`. Chat output contains at most eight matching
result lines per successful target or eighteen final lines on failure.
These excerpts are navigation aids: inspect the log for detailed coverage,
unverified paths and diagnostics. JSON records describe checks that actually
ran; absent later targets have not passed. Checks are not cached or skipped.
The `full` profile supplements, rather than replaces, the required profile.
Profiles do not automatically capture or regenerate missing fixtures.

Use the [validation cadence in ROADMAP](ROADMAP.md#working-on-the-remaining-tasks-read-this-first)
to choose the gate. Local commits use applicable focused checks; documentation
edits need diff/command/link inspection. Gameplay/core merges and acceptance
milestones require the complete `required` profile. Shared execution, timing,
generator implementation, compiler or oracle changes require the full suite before accepting
the change. After ten code merges without a full pass, run a full integration
checkpoint. Milestones also require `full` and applicable natural recordings.

New or changed ports and gates still need passing independent MAME fixtures
and relevant rejected mutations before acceptance. Keep all existing fixtures
and mutation definitions. Reuse a logged result only when its relevant sources
and transitive dependencies, compiler/options, fixtures, probe and harness are
unchanged; report reused evidence explicitly. The runner has no automatic
result cache. `oracle-test` currently includes all retained mutations, so full
regression gates still run them; `--only` is available for focused mutation work.

The default is `-j min(8, logical CPU count)`; override with `-j N` when
sharing the machine. Do not change emulated clocks or weaken fixture/mutation
coverage to reduce wall time. The latest complete oracle suite measured
2653.29 seconds for the 2026-10-12 daily main checkpoint; runtime varies with
coverage and load.
`benchmark-cpu-continuation` measures host synchronization overhead, not game FPS.

`make test` and `make verify-stage1` now share incremental C++ objects under
`build/check_obj/`. Compiler dependency files cover transitive headers and
generated includes; changing compiler/options invalidates these objects.
Repeated checks rerun executables without recompiling unchanged sources.
`make clean-checks` removes only these check objects and executables when a
fresh build is wanted. Existing game and oracle build rules remain separate.

## Capture discipline

Reuse a valid capture when its inputs, probe and capture script still apply.
After changing capture semantics or relevant MAME instrumentation, regenerate
that target and record the change. Use short/focused captures during iteration
and the full acceptance corpus for final evidence. Never weaken oracle flags,
mutation checks or coverage requirements to shorten a run.

Use stable target filenames instead of accumulating debug copies. Keep any
new exploratory outputs under `build/oracle/`; remove them once their findings
are recorded and the canonical fixture has replaced them. Keep referenced
fixtures and hand-written local tools. Inspect space with `du -hd 1 build/oracle`
before launching large captures. No cleanup runs automatically.

No additional service or dependency is needed for this workflow: Python,
make and compiler dependency files provide the improvements locally. Full
logs, ROM images and captures remain under gitignored `build/`.

## Native settings menu

Build with `make native-release` and run `./build/afterburner2-release`
from the repository root. The initial menu offers Enter Arcade, Options and Quit.
Enter Arcade resumes the cold board without hidden frames or automatic inputs,
so the original boot and attract sequence is displayed. Insert a coin and press
Start to play (keyboard 5, then Enter; gamepad Back, then Start). Esc or F1 opens
the menu during play. Use arrows and Enter/Space, or click a row; Left/Right
changes an option. Esc returns from Options, then resumes the previous session.
Opening the menu pauses the board and audio and preserves a pre-existing pause.
P still pauses/resumes and F5 restarts outside the menu.

Options provides nearest-neighbour, smooth (SDL linear), integer nearest-neighbour
and SDL Pixel Art scaling (available with SDL 3.4+), a separate sprite filter,
desktop fullscreen and Off/FPS/Detailed statistics. Scaling preserves the native 320×224 aspect ratio;
integer mode uses whole-number multiples. F11 toggles fullscreen and F3 cycles
statistics during play. Detailed statistics show simulated and displayed FPS,
wall time per displayed frame, CPU time per simulated frame, sprite filter/upload
time and presentation time (including vsync wait). Values refresh once per second.

The Sprites row selects Off (default), EPX 2x, Scale3x, 2xSaI, Super2xSaI or
ScaleFX 3x. These filter the visible hardware sprite plane after native zoom,
then compose it over nearest-scaled road/tiles. Sprite-drawn scenery is included;
foreground tile/text coverage and palette shadows remain native. Window scaling
still applies to the completed image, so choose Nearest or Integer to retain a
crisp HUD. xBRZ awaits a compatible licensing route; it is not offered as an
unimplemented or approximate mode. The SaI pair uses standard names rather than
the unrelated CrispSal/SmoothSal names. ScaleFX uses GPU compute on the native GPU path, with a CPU/software fallback.
Use the detailed Filter timing to compare modes; backend and load affect performance.

Settings save immediately to `settings.cfg` in SDL's per-user preference directory
for organization `AfterBurner2-Recomp`, application `AfterBurner2`.
`--settings-file path` overrides that location. Unsupported or invalid settings
fall back to defaults; failed changes leave the previous selection and show a
notice. ROM fonts are decoded at runtime for the menu and overlay, independently
of the board's text RAM and palette. No extracted assets are checked in.

`--boot-to-flight`, `--stage` and bounded `--frames` runs bypass the initial menu.
`--menu-preview image.bmp` renders Options and exits for visual inspection.
`--frame-preview image.bmp --frames count` saves the last presented bounded frame;
use an ignored path under `build/oracle/` for any ROM-derived image.
Use `python3 tools/workflow.py run test-native-settings test-sprite-filters verify-native-menu`
for persistence and ROM-backed SDL menu checks; the latter uses dummy video/audio.
These cover navigation, SDL options, rejection, Start/Quit and pause/frame isolation.

For native desktop play use `make play-native`; this builds the release binary,
opens the normal arcade launcher and shows measured FPS. Automatic coin/start
is opt-in through `--boot-to-flight`; selected-stage debug launches retain their
explicit shortcut. Run
`python3 tools/workflow.py run benchmark-native benchmark-native-extended`
with other heavy tests stopped to measure board/render costs. The extended
profile covers 960 flight/weapon frames; it excludes window presentation and
does not prove full-credit gameplay. `verify-native-release-recovery` checks
directed instruction fixtures with the same release compiler options.

Recovery gates retain `build/oracle/cpu_recovery.bin` and also require the
current complete manifest capture `build/oracle/cpu_recovery_complete.bin`.
If the latter is missing or its plan changed, run
`python3 tools/workflow.py run oracle-capture-recovery oracle-recovery`.
The capture target preserves an existing baseline. Both captures must replay
successfully before their combined coverage/mutations count as evidence.


## Focused oracle targets

These retained subsystem gates help investigate failures. They supplement the
required full suite and keep their original fixture boundaries; see the matching
ENGINE_NOTES sections for inputs, skipped races, mutations and capture details.

| Area | Replay targets |
| --- | --- |
| Z80 execution | `oracle-sound-execution`, `oracle-sound-execution-full`, `oracle-sound-irq`, `oracle-sound-execution-mutations`, `oracle-joint-sound-full` |
| Audio/device deadlines | `test-audio-deadlines`, `oracle-joint-audio-full`, `oracle-sound-audio-irq`, `oracle-sound-audio-timer-b`, `oracle-audio-deadline-mutations` |
| Mixed speaker | `oracle-speaker`, `oracle-speaker-edges`, `oracle-joint-speaker-full`, `oracle-speaker-mutations` |
| Video ownership | `oracle-joint-video-owned`, `oracle-joint-video-owned-directed`, `oracle-joint-video-mutations` |
| Instruction/exception coverage | `oracle-cpu-coverage`, `oracle-cpu-coverage-check`, `oracle-cpu-coverage-legacy`, `oracle-cpu-nested-irq`, `oracle-main-sub-coverage` |
| Cold boot | `oracle-joint-cold`, `oracle-joint-cold-full`, `oracle-cold-generated-check`, `oracle-joint-cold-boundaries`, `oracle-joint-cold-mutations` |

Find the specific capture target in Makefile before regenerating; names are
not uniformly derived from the replay target. Finish captures before dependent
comparisons. Cold captures must retain CPU phases through machine stop: exiting
from a CPU tap consumes the executing CPU's remaining clocks and invalidates
continuation evidence. Never seed a replacement context to hide a mismatch.

## Autonomous whole host and live game

```sh
python3 tools/workflow.py run oracle-whole-host
python3 tools/workflow.py run oracle-whole-host-full
python3 tools/workflow.py run oracle-whole-host-expanded
python3 tools/workflow.py run oracle-whole-host-mutations
python3 tools/workflow.py run verify-whole-host
python3 tools/workflow.py run verify-whole-host-headless
```

The first two reuse task 5's cold captures. Generate the expanded physical
ADC/weapon fixture with `oracle-capture-whole-host-expanded` and wait for the
capture to finish before replaying it. No capture file drives the host's
instruction, grant, deadline or state selection. All three positive runs and
fifteen owner mutations are required by `oracle-test`. `verify-stage1` keeps
the reconstruction regression suite and also runs `verify-whole-host`.
The live executable uses the same board host; 5 inserts a coin, Enter starts,
F5 recreates a cold board, and arrow/Z/X/Space/Left Shift feed physical inputs.
The 121-frame cold fixture contains only silence, so the speaker gain mutant
uses the 603-frame gameplay fixture's nonzero stream. A silent gain mutation
is not counted as a successful rejection. CMake exposes the new oracles under
`ORACLE_TESTS` and the native game integration under `STAGE1_ROM_TESTS`.

The resolved throttle, roll and missile stops retain their exact-PC fixtures
and mutations. Focused input smokes are `benchmark-native-full-throttle`,
`benchmark-native-rolls` and `benchmark-native-missiles`; each covers 960 flight
frames and reports observed entries. These are finite discovery/performance
runs, not whole-credit acceptance.

For later-stage instruction discovery, run
`python3 tools/workflow.py run stage-input-sweep`. Each ordered ROM stage
slot gets a fresh cold board and a planned 600-frame (~10.06-second) window:
75 frames each of full up/down, full bank left/right, opposite-endpoint
roll gestures left/right, Vulcan and repeated missile presses. Later slots
use test-only operands for the original ROM stage-advance event and stage
initializer; they are directed discovery fixtures, not natural progression
or MAME acceptance. Entry failures stop that case before any input window.
Diagnostics read backing memory directly: diagnostic bus reads would change
CPU continuation timing. `RESULT` and `COVERAGE` lines distinguish completed
input frames, observed roll-angle frames and weapon routine entries.
To reproduce one slot, use
`STAGE_SWEEP_ARGS='--stage 3' python3 tools/workflow.py run stage-input-sweep`.
The target succeeds when discovery finishes, even if cases report `status=stop`;
inspect those lines before claiming gameplay coverage.

To play a selected stage, use `make play-native STAGE=7`, or launch
`./build/afterburner2-release --stage 7 --profile` directly. Stage numbers
are 1-based ordered ROM slots (1..23 for this set). `--stage` automatically
boots with physical coin/start pulses, waits for active flight, and invokes
the same directed original-ROM transition as the sweep. F5 restarts at the
selected stage. Without `STAGE`/`--stage`, normal startup and F5 stay unchanged.
This is a debug skip, not natural progression. Any newly reached unported
path still stops with its original missing-PC diagnostic.

Selected-stage startup also executes a preceding alternate-road palette event
when the previous stage's ROM program contains one, then waits for the original
IRQ blend to finish. This avoids unset canyon colors after skipping directly
to stages 5, 8, 13 or 17. Compare these fourteen-word gradients against original
MAME with `python3 tools/workflow.py run capture-native-stage-palette verify-native-stage-palette`.
Reuse the local captures with `verify-native-stage-palette`; the focused check
does not replace a full merge or milestone gate when the cadence requires one.


## Natural recordings and transition observations

Record manual play from cold boot with
`./build/afterburner2-release --record build/oracle/manual.inputs`.
Coin/Start and every control are recorded against physical board-frame
numbers, including automatic `--boot-to-flight` inputs when requested.
Pausing adds no frames; F5 starts a separate `.restartN` recording.
`--stage` is rejected when recording. A stopped recording retains the
attempted failing frame. Its JSON sidecar identifies the loaded ROM regions
by length/FNV-1a digest and declares fresh persistent state and switch bytes.

`AB2PHYS1` is a text format: magic line; declared frame count followed by
three decimal switch bytes; then exactly one row per frame containing the
zero-based frame index, active-low button byte and eight raw ADC bytes.
Recordings and observations belong under ignored `build/oracle/`.
There is no gameplay RAM, CPU context, stage selector or invulnerability input.

Generate a discovery attempt and observe it in MAME:

```sh
python3 tools/natural_inputs.py build/oracle/neutral.inputs --scenario neutral --frames 12000
NATURAL_INPUT=build/oracle/neutral.inputs NATURAL_REFERENCE=build/oracle/neutral.mame.csv \
  python3 tools/workflow.py run capture-natural-credit
python3 tools/workflow.py run build/natural_credit
NATURAL_INPUT=build/oracle/neutral.mame.csv.inputs NATURAL_OUTPUT=build/oracle/neutral.native.csv \
  python3 tools/workflow.py run natural-credit-replay
NATURAL_INPUT=build/oracle/neutral.mame.csv.inputs NATURAL_OUTPUT=build/oracle/neutral.native.csv \
  NATURAL_REFERENCE=build/oracle/neutral.mame.csv python3 tools/workflow.py run oracle-natural-transitions
```

MAME field overrides have input-update latency. Keep the original requested
controls separate from the actual port observations in `.mame.csv.inputs`;
native comparison uses those observed physical pins. The replay script
rejects unsupported service buttons/ADC channels and mismatched switch values.
The `continue-multi-credit` scenario inserts three coin pulses during the
first life-exhaustion/continue window, exercising the plural-credit display
when two credits remain at a later continue screen. Use `--frames 10000`
and the same capture/replay/comparison commands above. The original `continue`
scenario remains unchanged.
`natural-credit` builds then replays; build once and use
`natural-credit-replay` for independent concurrent runs because ordinary
make invocations share release configuration/object files.

The CSV compares **every** frame's timestamp, mode, ordered stage index,
lives, damage-controller state, raw score word, packed credit byte and the
carrier/refuel selectors at `$FFFF90B0/$FFFF90E0` (ROM dispatch reads
`$103F2/$1624E`).
Native observations use backing memory, not timed bus transactions.
`.events.csv` separately retains the first carrier/refuel, shared `$E224`, stage-advance,
lives and weapon producer entry per stage; entry does not establish sequence
completion or every action requested. The comparison rejects missing,
extra, shifted or changed intervals, with five deliberate gate mutations.
An equal CSV does not establish whole-credit CPU/bus/device/RGB/audio identity,
collision cause, high-score name entry or NVRAM persistence. No ending is
inferred from a completed input window or from directed stage discovery.

Other discovery scenarios are `continue`, `weave` (weapons and controls),
and `credit-stock` (Easy / 3× Credits, repeated physical coins before Start).
The latter is an unusual multi-credit configuration, not default-credit
acceptance. `NATURAL_SECONDS` bounds the emulated MAME window; use 1515 for
90000 frames. `tools/oracle/export_natural_prefix.py` can declare a
contiguous prefix ending at an observed transition while preserving its
source input/ROM metadata and hashes. Retain the original discovery attempts
and report any stops beyond that window separately.

For a successful ending capture, declare the window through its first return
to attract, replay that entire prefix from cold boot, then require the actual
mode/stage sequence as well as equal observations:

```sh
python3 tools/oracle/export_natural_prefix.py build/oracle/stock.inputs \
  build/oracle/stock.mame.csv build/oracle/ending --frames 74452
NATURAL_INPUT=build/oracle/ending.inputs NATURAL_OUTPUT=build/oracle/ending.native.csv \
  python3 tools/workflow.py run natural-credit-replay
NATURAL_INPUT=build/oracle/ending.inputs NATURAL_OUTPUT=build/oracle/ending.native.csv \
  NATURAL_REFERENCE=build/oracle/ending.mame.csv python3 tools/workflow.py run oracle-natural-ending
```

The ending gate requires ordered flight slots 0–22, modes 17–22, ranking
25/26 and return 0/1, without gaps, plus observed carrier selectors 0–12 and
refuel selectors 0–7. Five additional mutations remove a stage, ending mode,
attract return or terminal sequence selector while keeping both CSVs
equal; all must fail the semantic check. This remains acceptance of the named observation
stream, not continuous whole-host acceptance. The endpoint above applies to
the documented `credit-stock` recording; inspect a different run's actual
transitions before declaring its endpoint.

For passive instruction discovery across a natural window, use
`capture-natural-census` with the same original requested controls and
`NATURAL_REFERENCE`/`NATURAL_SECONDS` settings. The isolated MAME probe
retains a 32-bit-word bitmap per 68000 and exports `.main.pcs` / `.sub.pcs`
at the endpoint. It observes instruction entry without writing game RAM,
registers, pins or clocks. It mirrors PCs to physical 22-bit addresses and
retains every even site, including RAM. The original MAME build is untouched.
Compare its actual pins and every named frame observation against the
ordinary reference before using its PC list to extend literal ports. This
is discovery coverage, not a CPU/bus/timing comparison. Reset and interrupt
vector sites plus unchanged observations are checked in the short smoke.

## Extending instruction coverage after a live stop

Read the original-ROM continuation and add only literal instruction sites to
`tools/oracle/main_recovery_pcs.txt`; generate the recovery translation and
capture metadata with `tools/generate_input_schedule.py --graph recovery`.
For a focused independent MAME capture of new sites:

```sh
RECOVERY_PC_FILTER="$PWD/build/oracle/new_pcs.txt" \
RECOVERY_INCREMENT_CAPTURE=build/oracle/new_instructions.bin \
python3 tools/workflow.py run oracle-capture-recovery-increment build/oracle_cpu_coverage
./build/oracle_cpu_coverage build/oracle/new_instructions.bin
python3 tools/oracle/check_cpu_coverage_mutations.py --jobs 3 \
  --only mutation-name --recovery build/oracle/new_instructions.bin
```

`RECOVERY_PC_FILTER` is a newline-delimited hexadecimal PC manifest. It must
select nonempty sites already in the recovery manifest. It changes only the
focused capture; the normal coverage gate still requires every declared PC
and both measured branch outcomes. `--only` also accepts comma-separated
names; full recovery mutation acceptance runs all retained mutations. Concurrent
mutations use separate source overlays and binaries, with passing baselines
checked first. No mutation is accepted merely because its baseline fails.

After the new instructions and mutations pass, run the directed stage sweep,
refresh the complete recovery capture with `oracle-capture-recovery`, and run
the checks required by the validation cadence above. Full merge and milestone
gates still include `test`, `verify-stage1` and the complete `oracle-test`. The
full capture now allows 64 emulated seconds for the expanded fixture roster;
focused captures keep their bounded window. Keep older captures and coverage.


To retain a verified complete corpus and add independent increments without
recapturing its earlier fixtures, first preserve the complete capture and its
passing `.coverage.json`, then use:

```sh
python3 tools/oracle/merge_cpu_coverage.py build/oracle/merged.bin \
  build/oracle/retained.bin build/oracle/new_instructions.bin
./build/oracle_cpu_coverage build/oracle/merged.bin
```

The merger requires passing input replay censuses newer than their captures,
keeps the exact original plan entries and record bytes, writes atomically,
and records input hashes in an ignored `.merge.json`. It removes any stale
output census. The merged file is not accepted until replay and the complete
manifest/branch gate pass; relevant mutations and whole-host checks at the
applicable acceptance gate remain required.
A fresh `oracle-capture-recovery` can always recreate the complete manifest.


## Native SDL3 platform

The native application requires SDL3 3.4 or newer. Make uses `pkg-config sdl3`
and CMake uses `find_package(SDL3 3.4 REQUIRED CONFIG COMPONENTS SDL3)` with
`SDL3::SDL3`. SDL3 supplies the window, keyboard events, GPU rendering/compute
and audio stream; `GameSession` still owns board clocks, RGB and PCM.

Use SDL3's driver environment names for native headless checks:

```sh
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ./build/afterburner2-release --stage 7 --frames 90 --profile
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=unavailable ./build/afterburner2-release --frames 120
```

`tools/oracle/capture.sh` runs the external MAME executable and retains its
SDL2 driver environment names. Native rendering requests vsync, letterboxes
the original logical resolution and explicitly uses nearest texture sampling.
Audio queues signed 16-bit stereo presentation PCM into an SDL3 playback stream;
Original at unity retains exact native output, while optional music settings use
verified stems and replacement files.
the input-side queue still bounds latency to approximately 100 ms. P pauses
the stream device, F5 clears queued PCM and restarts the selected board, and
a missing audio driver permits gameplay without a speaker.

For the stage-1 full-throttle/cannon regression window, generate physical inputs
with `python3 tools/natural_inputs.py build/oracle/cannon_boost.inputs --frames 3600 --scenario cannon-boost`. This holds the same actions as
Space+Z from frame 480 and retains cold boot/default switches. It is a bounded
discovery window, not a recording of a user's exact input trajectory.

`--scenario ground-awards --frames 12000` uses the existing Easy/99-credit stock
boot, then full throttle/cannon, varying stick position and missile pulses for
ground-target discovery. It changes physical inputs/DIP settings only. Verify
actual award-queue execution before claiming ground-target coverage; it does
not reproduce a user's exact trajectory by itself.

### Experimental 16:9 preview

Video → Viewport → 16:9 Preview selects 398×224 presentation with a centered
native HUD/menu. Original is the default. Published sprites and wider road/tile
samples are rendered from immutable partial-frame snapshots; gameplay stays
native. Pre-publication culling can leave edge popping and outer horizon gaps.
See [WIDESCREEN_PLAN](WIDESCREEN_PLAN.md) for the remaining acceptance work.

```sh
python3 tools/workflow.py run verify-wide-geometry verify-wide-scene verify-wide-mutations -j 4
python3 tools/workflow.py run verify-wide-gpu
```

The GPU check requires a real device/display and compares all six filters under
opaque/translucent policies. `verify-wide-scene` generates its ignored local
fixture; CMake GPU checks also need that fixture first. Geometry/scene checks are
part of `verify-stage1`, mutations of `oracle-test`. All old fixtures remain.
Captures stay under `build/oracle/widescreen/`; never commit runtime ROM data.
`--profile` reports the added CPU scene cost as `wide_scene_ms`; compare Original
and Wide with the same audio/device setup before attributing pacing changes.

### SDL GPU sprite filtering

The native app requests an SDL GPU device with Metal/MSL or Vulkan/SPIR-V
support, then creates an SDL GPU 2D renderer. GPU compute shaders implement
all five sprite filters and protected background composition. Unsupported GPU
systems and dummy-video checks report an explicit CPU/software fallback.
The CPU board renderer and sprite feedback remain authoritative.

```sh
python3 tools/workflow.py run verify-gpu-sprite-mutations verify-gpu-native-menu
make shader-build
```

GPU checks require a real device/display service and fail if unavailable; they
do not silently skip. Ordinary `test` retains the CPU/reference coverage and
`verify-native-menu` uses dummy/software rendering. Shader rebuilding needs
`glslangValidator` and `spirv-cross`; normal Make/CMake builds use the embedded
shader header and need neither tool. Rebuild shaders after changing scaler
algorithms and repeat GPU comparison/mutations. GPU filter/upload statistics
measure CPU upload/command submission time, not isolated GPU execution time.

`verify-gpu-native-menu` additionally compares all five filters on sampled
native boot/launch/gameplay planes and retains the first 320x224 native fixture
under ignored `build/oracle/`. Use
`python3 tools/workflow.py run verify-gpu-native-fixture` to replay that retained
fixture after creating it with the native check. Existing fixtures are preserved;
new failures get separate frame/filter filenames.

### Optional original-art sprite transparency

Options → Clouds, Trails, Explosions and Afterburner independently select Opaque
or Translucent; all default to Opaque and persist in the existing settings file. These options
work with Sprites Off or any scaler. Known original cloud art uses 160/255
opacity, missile-trail art uses 112/255, explosions use 176/255 and player flame
pens use 192/255; gameplay and native board
pixels remain unchanged. GPU composition retains covered sprites, foreground
tile priority and native shadow coverage. A CPU/software fallback is available.

`verify-gpu-sprite-mutations` includes opaque, zero-opacity and translucent
layer overlaps plus separate rejected cloud/trail/explosion/afterburner opacity mutations.
`verify-gpu-native-menu` exercises all sixteen option combinations and all six
scaler modes on sampled native boot/attract/launch/gameplay scenes, with actual
cloud/trail/explosion/player-flame observations required. `verify-game-session` checks individual
scene capture against a separate native host across 781 frames, audio and the
pause/toggle/restart lifecycle. This is finite coverage; other ROM art families,
whole-credit visuals and peak-load performance remain open.

### Native input remapping

Use Options → Controls for keyboard, gamepad and joystick bindings. The SDL3
gamepad database is embedded for offline deployment. Preferences live beside
`settings.cfg` in `settings.cfg.inputs`; `--settings-file` also relocates them.
`python3 tools/workflow.py run test-native-input verify-native-menu native-release`
checks SDL virtual-device discovery/remapping/hotplug, UI and native compilation.
`test-native-input` is included in `test` and CMake CTest. Physical hardware
testing remains separate from virtual-device coverage. See README for bindings,
axis capture, full throttle, GUID sharing and reserved system shortcuts.

Music frontend checks: `python3 tools/workflow.py run test-native-music
verify-music-presentation oracle-music-routing oracle-music-mutations`. Generate
independent routing fixtures with `oracle-capture-music-routing` when absent or
when capture semantics change. The 1,800-frame fixture is required to exercise
actual FM effects voice ownership. Shared audio changes still require `required`.


### Optional ground horizon correction

Options → Video → Ground offers Original (default) and Corrected. This removes
misplaced ground fill on the sky side of the native horizon strip in eligible
normal-road flight; other modes retain Original. It does not change gameplay or
implement continuous 3D terrain. Focused checks:

```sh
python3 tools/workflow.py run test-ground-horizon verify-ground-horizon verify-ground-fixture oracle-video-rolls verify-ground-mutations -j 4
python3 tools/workflow.py run verify-ground-gpu -j 4
```

The GPU gate needs a working graphics backend. The native gate retains generated
before/after images and a corrected-plane fixture under `build/oracle/horizon/`;
1560 scripted frames test both roll directions and original RGB/PCM/time identity.

`verify-ground-fixture` additionally replays the retained MAME roll corpus with
actual sky/terrain witnesses for both bank directions. It requires the capture;
ordinary ROM-backed integration does not. Ambiguous endpoint lines retain Original.

### Controller feedback

```sh
python3 tools/workflow.py run test-native-haptics verify-feedback verify-feedback-fixture verify-feedback-mutations verify-native-menu -j 4
# Regenerate compact passive MAME witnesses when needed:
python3 tools/workflow.py run capture-feedback -j 4
```

`test` includes virtual SDL output/mixer checks; `verify-stage1` includes the
4,200-frame native RGB/PCM/time comparison. `oracle-test` includes MAME cue
witnesses, the retained attract-mode bus fixture and feedback mutations.
Captures stay in ignored `build/oracle/`; never commit them. Physical acceptance
is separate: test every exposed motor in Options → Controls → Feedback, then
exercise gameplay, pause, focus loss, restart and unplug on each target OS.
macOS Xbox Bluetooth body and both triggers are confirmed; Windows/Linux and
simple-haptic fallback hardware remain open.

## Optional spatial audio checks

```sh
python3 tools/workflow.py run test-spatial-audio verify-spatial-mutations -j4
python3 tools/workflow.py run verify-spatial-game -j4
python3 tools/workflow.py run oracle-spatial-routing verify-spatial-ownership-mutations -j4
```

Soft loopback needs the optional OpenAL Soft dependency, without physical audio
hardware. `capture-spatial-routing` regenerates the independent 1,800-frame MAME
fixture under `build/oracle/spatial-audio/`. Unit/playback checks are part of
`test`; ownership and retained mutation checks are part of `oracle-test`.
Physical headphone/2.1/5.1 acceptance is separate from offline loopback.
