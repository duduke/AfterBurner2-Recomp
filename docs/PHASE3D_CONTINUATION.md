# Phase 3d continuation — remaining handlers and host acceptance

Phase 3d is **not complete**. In addition to the foundations, input mapping,
live flight/recovery and compound parts, player weapon controller/projectiles,
reload/HUD, world projection/explosion/ground effects, scaled player collision,
and the `$153E0..$156CE` enemy missile graph with shadows/trails now have
independent MAME execution checks. Observable ADC timing is checked separately.
Counts, coverage and limits are in `ENGINE_NOTES.md`. These ports remain
unwired. Keep `OLevelObjs` and its adapter until all replacement handlers and
the host boundary pass; the new routine ports do not establish host gameplay
equality.

## 1. Finish the input/player caller contract

- Live `$9810/$98C0`, shadow/exhaust, recovery `$9626/$964C`, and sequence
  `$1653A..$1657A/$1678E` are translated and independently checked. The live
  fuselage is slot 131 at `$C0C0`; the sequence compound is a different base.
  Finish live damage `$E662` and its death/debris transitions, and separate
  actual score/lives/stage-flow callees as named 3e boundaries.
- The independent handler capture now includes complete object and
  shared RAM at entry, D0..D7/A0..A6, main/sub multiplier and divider state,
  IO1 input and ordered RAM/I/O writes; ADC hardware timing is captured
  separately. Preserve that contract for the remaining graph. The existing
  dispatcher transcripts are external boundaries and cannot substitute for
  these handler oracles. Record changes made by the other CPU to actual
  read inputs; exclude active stacks and reject unrelated main writes.
- Trace the comparator IRQ `$01461C`, verified sampler `$014626`, its
  `$00B7F8` consumer call and the ADC conversion completion. Capture the
  original channel/result/IRQ timeline before connecting host input. Use
  local MAME hardware configuration for conversion timing.
- Add neutral, raw axis endpoints, throttle endpoints, button press/release
  and simultaneous-button fixtures plus ordinary gameplay records. Compare
  the six sample-ring bytes and the mapped arcade input bytes separately.

## 2. Connect verified flight and projection

- Translate the identified live handler graph instruction by instruction;
  keep all calculations at their original word/long widths and use the bus
  math chips. Use the verified `$00C5A8` register contract where called.
- Use the independently checked sequence updates `$1653A..$1657A` and
  projection `$1678E` when connecting sequence parts. Their caller/controller
  graph still needs translation; setup or part checks do not prove that flow.
- Verify shared bank/pitch/altitude, every compound object record, emitted
  requests, previous positions and the sub CPU's `$7800` size table.
- Prove deliberate mutations fail before wiring a real player through the
  30 Hz dispatcher and original two-display-frame sub handshake.

## 3. Complete the rest of 3d

- Extend beyond the verified player weapon graph and `$153E0..$156CE` enemy
  missile graph to the other enemy families (`$1586C`, `$15968` and later
  runtime jump-table targets). `$16C8A/$CFC4` reload/HUD is verified. Check
  held/released fire, launch side, cooldown/reload, exhaustion, target changes, removal,
  attachments and exact command/request/HUD write order.
- Enemy graph: `$88C2/$8AF2/$91C0` and ground effects are verified; `$9626`
  is player recovery. Continue formation/plane `$10E6A` and its transitions,
  other world variants (`$8946` onward), explosion initialization `$90AC`,
  projectile collision `$8C1C` and their reachable callees. Map
  runtime transitions beyond the reset template inventory. Use directed
  fixtures for handlers awaiting 3e stage/spawn dispatch, clearly labelled
  as routine coverage.
- Trace collision callers and scaled-size inputs, then translate damage,
  explosion and deletion paths. Keep score/lives/stage-flow requests as
  named unported 3e boundaries; introduce no native replacements.
- Connect verified sound producers and the timer consumer at their actual
  callers, then remove hand-picked native sound events. Routine sound-ring
  equality does not establish the host's command cadence.
- Retire native objects, flight, weapon rules, sprite-coordinate/history
  adapter and remaining replaced code only after their replacements pass.

## 4. Acceptance and delivery

Add a paired 30/60 Hz player/weapon oracle spanning ADC input, object records,
shared road targets, normal/interpolated sprite lists, draw/sound-latch events
and rendered RGB. Report identical/captured/skipped counts, mutation results
and uncovered paths per routine. Whole-credit equality stays in 3f.

Run `make test`, `make verify-stage1`, `make oracle-test` and a headless
launch. Update the roadmap, engine notes and fidelity boundaries. Complete
and merge the Phase 3d PR only after every 3d acceptance item is satisfied;
then draft the Phase 3e stage/game-flow plan. ROMs, disassembly output and
captures remain ignored.
