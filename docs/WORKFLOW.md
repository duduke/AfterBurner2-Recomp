# Efficient local workflow

Read the live rules, checkpoint and ordered next tasks:

```sh
python3 tools/workflow.py context
rg -n 'topic|ROM-address' docs/ENGINE_NOTES.md src/main tests tools/oracle
sed -n 'START,ENDp' path/to/relevant/file
```

The context command extracts the rules, current checkpoint and next tasks
from ROADMAP;
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

The default is `-j min(8, logical CPU count)`; override with `-j N` when
sharing the machine. Do not change emulated clocks or weaken fixture/mutation
coverage to reduce wall time. The full oracle suite measured 1823.36 seconds
for the current native checkpoint; runtime varies with coverage and load.
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
