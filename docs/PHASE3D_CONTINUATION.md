# Phase 3d continuation — remaining handlers and host acceptance

Phase 3d is **not complete**. Independent MAME execution checks now cover
foundations, input mapping, live flight/recovery and compound parts, player
weapons/projectiles, reload/HUD, world projection/explosion/ground effects,
scaled player collision, pre-lives damage controllers, the damage companion
emission/animation/pool-reset graph, and the `$153E0..$156CE` enemy missile
graph with shadows/trails. The remaining carrier/horizon, tracking/burst/debris,
world/hit and refuel mechanic graphs now have separate checks below. Observable
ADC timing is checked separately.
Counts, coverage and limits are in `ENGINE_NOTES.md`. These ports remain
unwired. Keep `OLevelObjs` and its adapter until all replacement handlers and
the host boundary pass; the new routine ports do not establish host gameplay
equality.

The user authorized merging checkpoint [PR #10](https://github.com/duduke/AfterBurner2-Recomp/pull/10)
before phase completion. It extends PR #9's pre-lives damage controller and
fuselage checks with the companion emission/animation/pool-reset graph.
For the next continuation, start from current `main`, create a new `codex/`
branch, and resume the remaining work below. Do not mark Phase 3d complete
or start Phase 3e based on this checkpoint.

## 1. Finish the input/player caller contract

- The damage companion graph assigned by `$E662` is now translated through
  `$FB08/$FFBE`, `$FB16/$FB9C`, both emitted handler graphs and their
  allocation/removal paths. `$E7DA/$FBD4` have independent NOP/NOP/RTS checks.
  `$FBC4/$10310` resets only the ROM-defined emission pool; it has no lives,
  HUD or credit callee. Companion results: 1,408/1,408 directed and
  2,400/2,400 ordinary gameplay calls identical, 0 skipped; six mutations fail.
  These routine ports remain unwired.
- The remaining carrier graph `$10348..$10E68`, horizon strip
  `$10E6A..$10FFE`, enemy tracking/burst/debris `$1586C..$160A6`, world
  `$8946/$8C1C/$90AC/$9366/$93A0/$94B8`, and independent refuel mechanics
  now have instruction-level ports and directed MAME checks. Results and
  natural-call limits are in the latest `ENGINE_NOTES.md` section.
- **Next task:** finish the input/IRQ and host contract, sound cadence and
  paired acceptance below. Preserve the actual 3e caller boundaries:
  `$8C1C` reaches scoring `$E542`; refuel controller `$16236` starts with
  `$E224`; later damage `$EC78/$CF46` and credit/continue states remain 3e.
  The carrier's exact selector increment/HUD request is checked locally;
  it does not implement general stage progression. Refuel setup/part checks
  do not verify controller states after `$E224`.
- `OMainDamage` now independently checks `$E662`, intact/breakup
  `$E8D0/$E94E` states 0..6, fuselage `$F640`, and their projection/frame,
  trajectory, debris and attitude callees: 1,792/1,792 directed calls and
  120/120 ordinary gameplay calls identical, 0 skipped. Gameplay includes
  one intact initialization and 119 intact-controller calls, not a complete
  death/recovery sequence. The companion graph now has separate checks; continue
  later sequence/controller states at their named boundaries. `$EC78/$CF46`
  lives/HUD and later credit flow remain named 3e boundaries; reaching state 7 throws at that caller.
  Use `make oracle-capture-damage` for controller captures and
  `make oracle-capture-companion` for companion captures.
- Keep the controller coverage limits visible: its natural capture covers one
  intact initialization and controller entry states 0..4, ending with a
  state-5 request. Natural breakup and complete death/recovery remain
  unchecked. The damage text string clears spaces, so `$137E2`'s glyph
  branch is unverified. States 7..11, refuel stage-flow and host cadence are not established by
  the directed state-0..6 checks. Carrier results have separate coverage.
- Live `$9810/$98C0`, shadow/exhaust, recovery `$9626/$964C`, and sequence
  `$1653A..$1657A/$1678E` are translated and independently checked. The live
  fuselage is slot 131 at `$C0C0`; the sequence compound is a different base.
  Pre-lives damage controllers and their companion graph are checked but
  remain unwired. Preserve the verified carrier caller and refuel boundary, retaining actual
  score/lives/stage-flow callees as named 3e boundaries.
- The independent handler capture now includes complete object and
  shared RAM at entry, D0..D7/A0..A6, main/sub multiplier and divider state,
  IO1 input and ordered RAM/I/O writes; ADC hardware timing is captured
  separately. Preserve that contract when wiring the verified graphs. The existing
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
  projection `$1678E` when connecting sequence parts. The carrier controller now has separate checks; refuel controller
  flow after `$16236/$E224` remains Phase 3e.
- Verify shared bank/pitch/altitude, every compound object record, emitted
  requests, previous positions and the sub CPU's `$7800` size table.
- Prove deliberate mutations fail before wiring a real player through the
  30 Hz dispatcher and original two-display-frame sub handshake.

## 3. Complete the rest of 3d

- Tracking/retarget `$1586C/$158A8/$1592A`, all sixteen `$15968` runtime
  launch directions, bursts `$15C62..$15D36` and impact/debris through
  `$160A6` now have directed checks. Natural coverage remains bounded and
  does not establish all stage/spawn combinations or a whole credit.
- `$10E6A` is the fifteen-segment horizon strip, not an enemy formation.
  Its setup/projection/frame graph is now checked. World `$8946`, explosion
  initialization `$90AC`, gate explosion `$9366/$93A0/$94B8`, projectile-hit
  `$8C1C` and its marking helper `$9046` are checked. Four scoring paths
  stop before `$E542`; the prefix oracle does not verify score flow.
  Stage scripts, formations and spawn dispatch belong to 3e. The special
  player `$11116/$1112C` is gated by the stage selector/sequence flag and
  remains outside ordinary live flight, pending that caller's 3e contract.
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
the remaining Phase 3d work in a new PR. Mark the phase complete only after
every 3d acceptance item is satisfied; then draft the Phase 3e stage/game-flow
plan. ROMs, disassembly output and captures remain ignored.
