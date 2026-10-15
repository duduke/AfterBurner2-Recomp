# Full-game fidelity and transition acceptance

**Status: recording/transition foundation implemented; whole-credit fidelity open.**
Updated 2026-10-15 against [macOS preview v0.1.0-preview.1](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.1).

| Acceptance area | Current evidence | Remaining gate |
| --- | --- | --- |
| Physical input and passive observations | Versioned cold-boot recordings and ten-column MAME transition comparisons implemented | Preserve settings, ROM identifiers, initial persistent state and observed input latency for new runs |
| Natural progression | Easy / 99-credit recording reaches all 23 stages, refuels, ending, ranking and attract over 74,452 frames | Record a successful standard single-credit run; the user's Normal/three-ship completion is manual evidence |
| Failure and lifecycle | Ending, neutral, continue and weapons replays pass 104,452 frames; altered-reference control rejects | Extend collision causes, name entry, NVRAM and alternate carrier/reset paths |
| Instruction recovery | 12,405 PCs, 77,954 fixtures and 146 rejected recovery mutations; reported `$15C10` stop fixed | Recover newly exposed paths from ROM with independent fixtures and controls |
| Continuous fidelity | Finite whole-host CPU/bus/device/RGB/audio corpora pass, including expanded 781-frame coverage | Compare the entire successful credit across all streams without supplied execution/state |
| Configuration and performance | Directed stage/DIP/lifecycle checks and bounded macOS smokes | Broader DIP/operand/reset coverage and sustained peak-load measurements |

The ten-column natural stream and native image reconstruction do **not** establish
continuous MAME CPU/device/RGB/audio equality. The release's complete `required`
and supplemental `full` gates passed; counts, logs and exclusions are in
[the release checkpoint](ENGINE_NOTES.md#macos-preview-packaging-2026-10-15).

## Retained workflow

Steps 1–4 already supply the Easy / 99-credit transition recording and reference.
Reuse that foundation; extend it for the standard-credit recording and the
continuous comparison in step 5. An automated player remains optional.

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
remain required at the
[ROADMAP validation cadence](ROADMAP.md#working-on-the-remaining-tasks-read-this-first).
Report exactly which settings and paths were exercised; one successful credit
does not establish every possible operand or input trajectory.
Publish commands, fixture metadata, results and exclusions in ENGINE_NOTES,
then update [ROADMAP](ROADMAP.md). Do not commit ROM data, recordings or captures.

