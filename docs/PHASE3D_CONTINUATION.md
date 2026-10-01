# Phase 3d continuation — input and live player next

Phase 3d is **not complete**. Verified foundations are the dispatcher and
initializer, RNG, sequence compound setup/initialization/hiding, and sound
ring producer/consumer. These ports remain unwired. Keep `OLevelObjs` and its
adapter until actual replacement handlers pass independent MAME checks.

## 1. Capture the live input/player contract

- Walk `$009810/$0098C0` and every reachable input, flight, projection,
  animation and request-emission callee. Record actual object slots from
  MAME rather than assuming the sequence compound is the live player.
- Extend the capture to isolate selected handlers, with complete object and
  shared RAM at entry, D0..D7/A0..A6, main/sub multiplier and divider state,
  input ports/ADC result, and their own ordered RAM/I/O writes. The existing
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

## 2. Port flight and projection

- Translate the identified live handler graph instruction by instruction;
  keep all calculations at their original word/long widths and use the bus
  math chips. Use the verified `$00C5A8` register contract where called.
- Independently port sequence updates `$01653A..$01657A` and projection
  `$01678E` if they are required at the chosen host boundary. Their setup
  checks alone do not prove projection, animation or the live flight model.
- Verify shared bank/pitch/altitude, every compound object record, emitted
  requests, previous positions and the sub CPU's `$7800` size table.
- Prove deliberate mutations fail before wiring a real player through the
  30 Hz dispatcher and original two-display-frame sub handshake.

## 3. Complete the rest of 3d

- Weapons `$0154B8..$015638` and `$016C8A` reload/HUD/audio: held/released
  fire, launch side, cooldown/reload, exhaustion, target changes, removal,
  attachments and exact command/request/HUD write order.
- Enemy graph: start with `$0088C2`, `$008AF2`, `$0091C0`, `$009626`; map
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
