# Efficient local workflow

Read the live rules, checkpoint and ordered next tasks:

```sh
python3 tools/workflow.py context
rg -n 'topic|ROM-address' docs/ENGINE_NOTES.md src/main tests tools/oracle
sed -n 'START,ENDp' path/to/relevant/file
```

The context command extracts current text from ROADMAP and PHASE3D_PLAN;
there is no separate checkpoint to keep synchronized. Follow the roadmap's
instruction-level translation, MAME comparison, mutation and coverage rules.
Read relevant engine notes before making a port. Search first and read bounded
sections; generated `.inc` tables and entire historical documents are expensive
to put into an assistant's context.

## Checks and logs

```sh
python3 tools/workflow.py run quick       # ROM-free tests + Stage 1
python3 tools/workflow.py run joint       # current 120-frame joint oracle
python3 tools/workflow.py run full        # 600-frame joint + reset window
python3 tools/workflow.py run required    # all three required pre-commit checks
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

The default is `-j min(8, logical CPU count)` (eight jobs on the current
10-core, 32-GiB host); override it with `-j N` when sharing the machine.
Make runs independent gates concurrently while each joint oracle preserves
its ordered CPU/device grants. The CPU continuation uses release/acquire
atomic handoffs, spins briefly for short checkpoints, then parks with C++20
atomic wait for longer delays. Consecutive microsteps within one granted
budget now avoid handoffs, and the live host defers instruction startup to
the first grant. Sleeping-waiter registration avoids kernel notifications
for hot handoffs. This changes host synchronization cost only;
emulated clocks, bus order, comparisons and mutation coverage stay intact.
GPU/NPU offload does not serve this bottleneck: each literal instruction and
bus checkpoint depends on the preceding emulated state.

```sh
python3 tools/workflow.py run benchmark-cpu-continuation
```

This ROM-free benchmark measures 100,000 checkpoints within one grant.
The continuation can batch these without thread handoffs; this microbenchmark
is not a whole-game frame-rate measurement. Its wall time
depends on machine load and must never become an emulated clock input. The
complete `oracle-test` measured 407.91 seconds on this host after optimization;
the earlier successful suite measured 1380.15 seconds, before the added native
sound gates. These are whole-run observations, not a fixed performance promise.

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

For native desktop play use `make play-native`; this builds the release binary,
boots with physical coin/start pulses and shows measured FPS. Run
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


## Native sound execution

The focused sound gates are `oracle-sound-execution`,
`oracle-sound-execution-full`, `oracle-sound-irq` and
`oracle-sound-execution-mutations`. Their capture counterparts use the isolated
scheduler probe. `oracle-joint-sound-full` checks the 600-frame shared
main/sub/sound run with native Z80 grants and full video comparisons; regenerate
it with `oracle-capture-joint-sound-full`. All gates are included in
`oracle-test`; previous fixtures remain required. These gates assert device
read values and bus timestamps, while independent audio/device head-deadline
ownership remains the subsequent roadmap task.


## Independent audio/device scheduling

`oracle-joint-audio-full` checks all device head deadlines and IRQ/RESET
boosts after the joint initial context, with continuous raw PCM/FM samples
and independently computed stream batches. `oracle-sound-audio-irq` and
`oracle-sound-audio-timer-b` cover original-program IRQ diagnostics;
`oracle-audio-deadline-mutations` requires those gates before rejecting private
owner mutations. All are included in `oracle-test`; older gates remain.
Capture counterparts are `oracle-capture-joint-audio-full`,
`oracle-capture-sound-audio-irq` and `oracle-capture-sound-audio-timer-b`.
Complete capture commands before launching dependent comparisons. The read-only
probe records raw stream output before speaker gain/resampling, and all captures
remain ignored. `test-audio-deadlines` is the focused ROM-free clock/queue gate.
See ENGINE_NOTES for the remaining pre-window and mixed-output boundaries.

## Mixed speaker and game audio

`oracle-speaker` compares MAME's default LoFi 48 kHz speaker stream, including
float bits and signed PCM conversion. `oracle-speaker-edges` additionally
requires synthetic panning, both signs and clipping. `oracle-joint-speaker-full`
compares the same speaker batches produced by native CPU/device execution;
`oracle-speaker-mutations` requires these gates and rejects ten private changes.
All remain in `oracle-test`. Reproduce captures with
`oracle-capture-joint-speaker-full` and `oracle-capture-speaker-edges`; wait for
capture completion before running comparisons. The isolated scheduler probe
observes raw streams and speaker inputs without changing devices or scheduling.
Speaker capture is before optional OS effects, at unity host gain.

`verify-stage1` checks audible ROM execution, irregular render chunk equivalence,
and full sound-state recreation. The game loop uses 800 stereo frames per 60 Hz
tick; native speaker updates remain at 50 Hz behind a 20 ms playback buffer.
Commands from Stage 1 remain external transport inputs. Headless smoke checks:

```sh
python3 tools/workflow.py run all
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ./afterburner2 --frames 120
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=unavailable ./afterburner2 --frames 120
```

The second run should log audio-device unavailability and still advance the
sound board and finish normally. Audio queue backpressure does not drop samples.


## Coupled video ownership

`oracle-joint-video-owned` combines the existing 600-frame native speaker
capture with `oracle-joint-video-owned-directed`, which checks display changes,
pending globals, live scroll tables and effect pens with native sound/device
heads and mixed speaker output. Create its new fixture with
`oracle-capture-joint-video-owned-directed`, wait for successful completion,
then reuse it. The old video/pixel gates remain in `oracle-test`, alongside
`oracle-joint-video-mutations` with thirteen cases. To investigate only the
new timing-owner cases without rerunning unrelated mutations:

```sh
python3 tools/oracle/check_joint_video_mutations.py text-frontier first-scanline vblank-frontier
```

The complete mutation target and all required checks still run for final
acceptance. Initial video/CPU/boot boundaries and directed writes remain
explicit fixtures; this gate does not establish whole-host cold boot.


## Expanded main/sub coverage

`oracle-main-sub-coverage` is required by `oracle-test`. It combines the
independent 52-case wrapper/exception fixtures, expanded original-ROM
instruction/branch fixtures, retained nested IRQ entries, nine mutation
checks and the longer 1,200-frame joint gameplay replay. Reuse captures:

```sh
python3 tools/workflow.py run oracle-cpu-coverage oracle-cpu-coverage-check oracle-cpu-nested-irq
python3 tools/workflow.py run oracle-main-sub-coverage
```

To regenerate locally, use `oracle-capture-cpu-coverage`,
`oracle-capture-cpu-coverage-expanded`, `oracle-capture-cpu-nested-irq` and
`oracle-capture-joint-coverage-flight`. The expanded capture first plans
against the passed natural speaker census and runtime ROM images; all plan
metadata/images/captures stay in ignored `build/oracle/`. A stale plan or
missing PC/branch outcome fails `oracle-cpu-coverage-check`. The nested
fixture explicitly drives an IRQ6 pin after IRQ2 VPA lookup in the isolated
MAME build (`AB2_CPU_IRQ_FIXTURE`); normal captures leave this mode disabled.
Independent instruction contexts do not replace any state in the continuous
joint replay. Its timestamps decode the probe's legacy low-64 attoseconds
against source chronology, avoiding the 18.446-second wrap while preserving
absolute CPU nanosecond and bus assertions. Adjacent trace clocks must be
less than half an epoch apart; this is a capture-format bound.

The expanded manifest additionally translates 714 main PCs reached after the
old natural window, including the `$CD26` ABCD memory form. Preserve both
`oracle-cpu-coverage-legacy` (the original 4,464-case capture) and the current
5,340-case gate. Regenerate the former with `oracle-capture-cpu-coverage-legacy`;
its planner's `--legacy` option excludes only the explicitly labelled 714
new PCs and reproduces the previous instruction/branch plan. The newer capture
uses `cpu_coverage_expanded_full.bin`; the strict long replay uses
`joint_coverage_flight_expanded.bin` with fresh snapshots for all new PCs.

## Cold native startup and capture boundaries

`oracle-joint-cold` checks the focused power-on stream; `oracle-joint-cold-full`
checks the longer 603-frame observation window. Both require native CPU
execution, sound/device heads, speaker output and RGB from reset, with zero
supplied CPU contexts or execution phases. Physical DIP switches join the
button/axis fixtures in the cold input format. `oracle-cold-generated-check`
validates the 352 added boot translations against the local ROM image.

Regenerate with `oracle-capture-joint-cold` and
`oracle-capture-joint-cold-full`; complete each capture before its comparison.
The capture requests exit from a frame device callback and retains every
CPU phase through machine stop. Never call exit from a CPU tap to extend
final continuations: MAME eats the executing CPU's remaining clocks.

`oracle-joint-cold-boundaries` runs the full gate then matches the retained
initial 46 sub ISR phases and both terminal CPU bus boundaries, requiring
later native instruction/IRQ snapshots. `oracle-joint-cold-mutations` rejects
eight startup owner changes. All are required by `oracle-test`; older
fixtures and mutations remain. The finite stop can cut a new ISR; no future
state or completion is supplied. See ENGINE_NOTES for the exact bounds and
unobserved paths. Stage 1 game-loop replacement remains a subsequent task.


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

For the Space-input `$E514` stop, the retained recovery oracle and its
`full-throttle-speed-clamp` mutation pin the exact instruction. Run
`python3 tools/workflow.py run benchmark-native-full-throttle` for a separate
960-frame physical input smoke; its finite path does not naturally cover
the clamp. Rebuild with `native-release`, then launch `make play-native`.

For the left/right `$7784` stop, run
`python3 tools/workflow.py run benchmark-native-rolls`: 960 flight frames
alternate full horizontal endpoints with full then neutral throttle.
The exact orientation bit and neighbouring table selection are independently
pinned by recovery fixtures and mutations; the replay's passive entry counts
report which new paths it actually reaches.

For the missile-input `$19C78` stop, run
`python3 tools/workflow.py run benchmark-native-missiles`: 32 press/release
windows over 960 flight frames, neutral then full throttle. Its passive
indicator entry count distinguishes natural reach from the independently
verified exact-PC countdown fixtures. Rebuild `native-release` before
launching `make play-native`.

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
does not replace full required acceptance before committing.


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
names; ordinary recovery acceptance runs all retained mutations. Concurrent
mutations use separate source overlays and binaries, with passing baselines
checked first. No mutation is accepted merely because its baseline fails.

Operand seeds are applied after stack setup so a fixture whose A5 aliases
the supervisor stack retains its intended memory value. Indexed word/long
fixtures use the original path's aligned index; address errors are a separate
exception contract. The scripted-flight `DIVS.W #3,D0` fixtures cover signed
quotient/remainder, early and late overflow, flags and MAME cycle counts;
they do not establish support for zero or negative divisors.

After the new instructions and mutations pass, run the directed stage sweep,
refresh the complete recovery capture with `oracle-capture-recovery`, and run
`test`, `verify-stage1` and the complete `oracle-test` before committing. The
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
manifest/branch gate pass; mutation and whole-host checks remain required.
A fresh `oracle-capture-recovery` can always recreate the complete manifest.


The expanded recovery plan exceeds 50,000 individual fixtures, so the replay
and merger accept at most 100,000 records per corpus. Every planned record
must still be present and match exactly. Register/displacement MULS fixtures
cover variable Booth-transition clocks; displacement TAS fixtures retain
byte neighbours and test old-byte NZ flags and the written high bit.


## Native SDL3 platform

The native application requires SDL3 3.2 or newer. Make uses `pkg-config sdl3`
and CMake uses `find_package(SDL3 3.2 REQUIRED CONFIG COMPONENTS SDL3)` with
`SDL3::SDL3`. SDL3 supplies the window, keyboard events, texture presentation
and audio stream; `GameSession` still owns board clocks, RGB and PCM.

Use SDL3's driver environment names for native headless checks:

```sh
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ./build/afterburner2-release --stage 7 --frames 90 --profile
SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=unavailable ./build/afterburner2-release --frames 120
```

`tools/oracle/capture.sh` runs the external MAME executable and retains its
SDL2 driver environment names. Native rendering requests vsync, letterboxes
the original logical resolution and explicitly uses nearest texture sampling.
Audio queues original signed 16-bit stereo PCM into an SDL3 playback stream;
the input-side queue still bounds latency to approximately 100 ms. P pauses
the stream device, F5 clears queued PCM and restarts the selected board, and
a missing audio driver permits gameplay without a speaker.
