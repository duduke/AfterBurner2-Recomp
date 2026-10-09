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
atomic wait for longer delays. This changes host synchronization cost only;
emulated clocks, bus order, comparisons and mutation coverage stay intact.
GPU/NPU offload does not serve this bottleneck: each literal instruction and
bus checkpoint depends on the preceding emulated state.

```sh
python3 tools/workflow.py run benchmark-cpu-continuation
```

This ROM-free benchmark measures 100,000 checkpoint handoffs. Its wall time
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
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./afterburner2 --frames 120
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=unavailable ./afterburner2 --frames 120
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
