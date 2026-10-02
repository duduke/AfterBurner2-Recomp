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

The user authorized merging checkpoint [PR #11](https://github.com/duduke/AfterBurner2-Recomp/pull/11)
before phase completion. It adds the remaining independent handler graphs to
PR #10's damage companion checkpoint: 7,409/7,424 calls identical, zero
differences, 15 sub-input races skipped; all ten new mutations fail.
After that merged checkpoint, the next continuation started from `main`
on a new `codex/` branch. Current open work is recorded below; do not mark
Phase 3d complete or start Phase 3e based on the handler checkpoint.

The user also authorized pushing and merging comparator IRQ checkpoint
[PR #12](https://github.com/duduke/AfterBurner2-Recomp/pull/12), implementation
commit `e2f973b`, before phase completion. It verifies the combined ADC/timer/
sound caller and physical endpoint mapping described below. Independent host
scheduling, object wiring and paired player/weapon acceptance remain next.

The board/CPU timing checkpoint [PR #13](https://github.com/duduke/AfterBurner2-Recomp/pull/13)
is merged on `main` (`ca90197`). The user also authorized merging the host
object-composition checkpoint [PR #14](https://github.com/duduke/AfterBurner2-Recomp/pull/14)
before phase completion. Implementation commit `363bce4` is merged on `main`
as `a8849a4`; `codex/phase3d-host-wiring` is a completed checkpoint branch.
The common entry router and directed live object-dispatch composition match
480/480 MAME windows, executing 53,404 verified callbacks; 128/128 additional
`$9D34/$B628` entry calls match. All four mutations fail. The IPL7 fixture
isolates composition and does not establish natural cadence. `OEngine` still
uses the retained native Stage 1 host. Phase 3d remains incomplete.

Start the next continuation from updated `main` on a new `codex/` branch.

Next: establish the live `$B676` register/wait publication contract and the
remaining explicit external caller boundaries listed in `ENGINE_NOTES.md`;
then compose IRQ/input, VBLANK/reload and sound producers at their actual
boundaries and prove paired player/weapon/sub/video acceptance. Keep the
unknown caller transcripts labelled as inputs until independently ported.

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
- The full `$1461C..$1466A` comparator IRQ caller is now translated and
  checked with a continuous hardware timeline: 7,058/7,058 calls and
  45,176/45,176 events identical, 0 skipped, including 946 sound-latch writes.
  Neutral/physical axis endpoints, throttle endpoints and weapon button
  combinations have separate `$B0AC/$14976` checks: 720/720 identical,
  0 skipped. Three deliberate mutations fail. The oracle supplies MAME bus
  timestamps and entry work RAM; this does not establish an independently
  scheduled host or the producers' cadence. Ports remain unwired.
- The physical board timeline now independently generates 64 us scanlines,
  main EXCK and the line-223/224 VBLANK pulse from reset, with chronological
  ADC completion. Ordinary, endpoint and directed probes match
  669,845/669,845 events, 0 skipped; three schedule mutations fail. Frame
  notifier ordering at line 224 is explicitly checked. CPU bus timestamps
  remain external inputs; this does not establish CPU IRQ service latency.
  `OEngine` remains unwired. The bounded service contract is now checked in
  the next entry; full main-program execution remains unported. See
  `ENGINE_NOTES.md` and `make oracle-capture-board`.
- Bounded CPU IRQ2/IRQ6 service now matches 9,439/9,439 calls and
  474,024/474,024 bus events, 0 skipped, computing access/return timestamps
  from 80 ns CPU cycles and the E-clock IACK phase. MOVEM/JSR/RTS/RTE stack
  phases, all sampler slots, disabled/even/empty/emitting sound paths and
  real IPL7 masking/IPL1 unmasking are checked; seven mutations fail.
  The interrupted instruction's last bus boundary and entry context still
  come from the capture; the board computes the level at that boundary.
  Completion tails cover observed opcodes; arbitrary
  instructions, all mask transitions, nested ISRs and independent full CPU
  execution remain unverified. Next: connect verified callers at those
  boundaries and complete paired player/weapon/producer/video acceptance.
  Use `make oracle-capture-service` / `make oracle-irq-service`.
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
- The comparator trace is complete: `$01461C` acknowledges before `$014626`
  reads the previous conversion; mux/start precede `$00B7F8`. Physical MAME
  axis limits are X `$20..$E0`, Y `$40..$C0` (reversed), throttle `$00..$FF`.
  `make oracle-capture-mainirq` captures ordinary and endpoint timelines plus
  separate mapped-axis/button records. Keep the bus-time/CPU-cycle boundary
  explicit when connecting host input and scheduling the verified caller.

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
