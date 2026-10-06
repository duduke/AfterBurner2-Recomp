# Phase 3d — objects, input and flight

Phase 3d is **not complete**. This is the current implementation and
continuation plan. Detailed oracle results, mutation evidence, commands and
checkpoint history live in [ENGINE_NOTES.md](ENGINE_NOTES.md); completion
items live in [ROADMAP.md](ROADMAP.md). Read the roadmap's "Working on the
remaining tasks" before changing code.

The continuation on `codex/irq4-remaining-callee-timing` computes the remaining
`$14A86/$13F92/$16C8A/$18444/$13E5C` callee graphs, alongside the previously
checked `$1329E`. All seven current IRQ4 datasets pass: 3,519/3,519 envelopes,
8,099,444/8,099,444 predicted bus events and 18,769/18,769 calls to these six
entries, zero skips. The bounded caller continuation checks IRQ entry within
`$14856` re-arm,
the `$1458A..$145B8` dispatcher and `$B676..$B696` publication instructions.
Physical IPL now comes from bus-phase delivery and completion-tail clocks;
bounded callers and omitted busy spins carry computed state/time. External
handler/ISR resumptions and sub progress remain supplied boundaries; Phase 3d
remains incomplete.

## Scope and current status

Phase 3c supplies shared arcade RAM, the interrupt/caller handshake, 30 Hz
object scheduling, palette routines, request publication, the sub CPU sprite
transform and normal/interpolated hardware blits. Phase 3d replaces
`OLevelObjs`, native flight/weapon rules, the sprite-coordinate/history
adapter and hand-picked sound cues once the verified replacements pass host
acceptance. `OEngine` still uses the native Stage 1 host; keep those producers
and their adapter until acceptance passes.

Independent routine checks cover the dispatcher/initializer, RNG, input
mapping, live flight/recovery and compound parts, player weapons/projectiles,
reload/HUD, projection/explosion/ground effects, scaled collision, pre-lives
damage and companion graphs, enemy missile/tracking/burst/debris graphs,
carrier/horizon handlers and independent refuel mechanics. These checks do
not establish independently scheduled gameplay or a complete credit.

The current host/timing evidence is:

| Verified checkpoint | Evidence | Remaining inputs or limits |
| --- | --- | --- |
| Comparator/input and physical board | Combined ADC/timer/sound caller, axis/button endpoints, scanline/EXCK/VBLANK and ADC deadlines | Main CPU caller boundaries and producer cadence |
| Bounded IRQ2/IRQ6 service | 9,439/9,439 calls; 474,024/474,024 predicted bus events; zero skips | Interrupted instruction boundary/context; bounded opcode and mask coverage |
| Directed object composition | 480/480 dispatch windows; 53,404 callbacks; 128/128 additional entry calls | IPL7 fixture isolates composition; unported callers remain transcripts |
| Natural IRQ/shared-input and paired composition | 120/120 separate and 122/122 paired complete dispatchers; 244/244 VBLANK windows; 240/240 RGB frames; zero skips | Captured IRQ frontiers, sub-produced operands and compositor feedback |
| Bounded caller instructions and IRQ entry | 778,252/778,252 records; 2,181,954/2,181,954 bus events; 682,233/682,233 caller boundaries; zero skips; IRQ4 at every re-arm PC | Physical IPL visibility, initial contexts, external handler/ISR resumptions and sub progress |
| Physical IPL and bounded caller schedule | 42,280,436/42,280,436 physical observations; 12,703,304 generated wait instructions; 38,111,440/38,111,440 wait bus assertions; zero skips | External handler/ISR returns and first resumed operands, unported CPU frontiers, initial contexts and sub progress |
| Bounded IRQ4, complete selected callee graphs and re-arm timing | 3,519/3,519 envelopes; 8,099,444/8,099,444 predicted bus events; 18,769/18,769 fade/animation/cycle/reload/scenery/transition calls; 16,349/16,349 uninterrupted wait steps | Game-flow caller transcripts, sub-owned write frontiers and interrupted contexts; six IRQ-overlapped wait attempts excluded |

The shared-input checkpoint supersedes earlier IRQ4 race exclusions. Complete
`$B676` waits/publication now execute at captured IRQ frontiers; they do not
independently schedule IRQ or sub CPU progress. Sub-write lane provenance
supplies only sub-produced lanes and preserves computed main values.
Back-to-back IRQ returns share the first resumed operand frontier. The
continuous sound ring also passes, but sound caller timestamps remain supplied.

## 1. Finish IRQ4 timing and the caller schedule — next work

- [x] Independently check `$13BB6/$13BCC/$13BFA` road-palette cycles,
  operand reads, writes and entry/exit registers. Natural, directed and
  endpoint captures pass with zero skips; three constant mutations fail.
  See the road-palette continuation in `ENGINE_NOTES.md` for coverage limits.
- [x] Independently check `$1329E` palette-cycle timing, including unsigned
  selector branches, phase word masking/wrap and hardware/fade destinations.
  See the palette-cycle continuation in `ENGINE_NOTES.md` for counts and limits.
- [x] Replace palette/scenery/reload/HUD timing boundaries with independently
  checked callee cycles: `$14A86/$13F92/$1329E/$16C8A/$18444/$13E5C`.
  The palette blend/upload, sound ring/decimal HUD, scenery scripts/tile
  helpers and returns compute their own bus phases and register effects.
  Natural, directed and endpoint captures pass; eight constant mutations fail.
  Coverage and supplied sub-write frontiers are explicit in `ENGINE_NOTES.md`.
  Only the unported game-flow callers still supply state and durations;
  their durations add to predicted time without resetting the caller clock.
- [x] Check bounded IRQ entry within `$14856` re-arm and the dispatcher/
  publication caller instruction stream. Instruction prefetches, operand
  phases, CCR, consecutive caller boundaries and exception entry are checked.
  Every re-arm instruction has directed IRQ4 coverage. Physical IPL visibility,
  initial contexts and external handler/ISR resumptions remain supplied;
  arbitrary instruction tails, mask-writing instructions, nested ISRs and
  unexercised service-button branches remain explicit limits. See the caller
  continuation in `ENGINE_NOTES.md` for counts and mutation evidence.
- [x] Check physical IRQ visibility at bus-phase and instruction-completion
  yields, then advance the bounded caller graph and busy-spin sampling gaps
  from computed CPU state/time. Captured IPL bytes are assertions, not poll
  inputs. The full host still needs external handler/ISR resumptions, unported
  instruction graphs and sub progress; see the visibility continuation in
  `ENGINE_NOTES.md` for exact counts and limits.
- [ ] Finish independent scheduling across caller/ISR resumptions, ADC input
  producers, publication wait boundaries and sub progress.
  Current IRQ contexts/frontiers, physical input bytes, unknown caller
  transcripts, sub-produced blit operands and hidden renderer feedback remain
  inputs. Advancing an arbitrary frame clock would invent IRQ latency and
  operand ordering.
- [x] Pair `OSubRoad` raster outputs with the computed bank/pitch/altitude
  targets and the compositor. Host input through ADC drives `$B0AC/$14976`,
  updating `$29c0ce/$29c0d0/$29c0d2` which naturally banks `OSubRoad`. Captured
  road raster, tile/palette inputs and hidden renderer feedback remain separate
  acceptance boundaries.

Keep `$17146/$14990/$C6F8/$E2DA` as explicit game-flow caller boundaries until
their own ports pass. The sampler's actual order remains acknowledgement,
previous ADC result, mux/start, then `$B7F8`. Preserve the 64 us scanline and
80 ns main CPU cycle clocks, frame-notifier ordering and chronological ADC
completion. Physical MAME axis limits are X `$20..$E0`, reversed Y `$40..$C0`,
and throttle `$00..$FF`; retain neutral, endpoint, release and simultaneous
weapon-button coverage when connecting host controls.

## 2. Connect verified objects, flight, weapons and sound

- [ ] Route verified handlers through the original dispatcher and actual host
  callers. Preserve 254 × 64-byte records, live active/handler reads and
  runtime ROM templates/handler pointers. Do not reinterpret native `OEntry`
  as arcade RAM or use dispatcher transcripts as independent handler oracles.
- [ ] Connect live `$9810/$98C0`, shadow/exhaust, recovery `$9626/$964C` and
  sequence `$1653A..$1657A/$1678E`. The live fuselage is slot 131 at `$C0C0`;
  sequence compounds use a different base. Keep word/long arithmetic widths
  and the real multiplier/divider chips; retain `$C5A8`'s verified RNG contract.
- [ ] Compare every compound object record, previous position, emitted request,
  shared bank/pitch/altitude target and the sub CPU's `$7800` scaled-size table.
  Preserve 30 Hz logic and the original two-display-frame sub handshake.
- [ ] Connect verified weapon, projectile, reload and HUD routines at their
  actual callers. Retain held/released fire, launch-side, cooldown/reload,
  exhaustion, targeting, removal and attachment-transition checks.
- [x] Connect sound-ring producers and the timer consumer at their actual
  callers. Comparator IRQ drains the 64-byte ring through `$00B7F8`, writing
  the 315-5250 sound latch at `$0E8017`. Display IRQ runs `$016C8A` missile
  reload, enqueuing reload sound `$9B` into the ring and updating the HUD.
  Sound commands route to `ArcadeAudio` alongside native cues; native event
  removal remains pending full object acceptance.

Each handler's independent capture contract includes complete object/shared
RAM, D0..D7/A0..A6, main/sub math-chip state, IO1 input and ordered CPU-owned
RAM/I/O writes. Preserve that contract during wiring, record other-CPU writes
to actual read inputs, exclude active stacks and reject unrelated main writes.

## 3. Preserve coverage limits and Phase 3e boundaries

- Damage `$E662`, intact/breakup `$E8D0/$E94E` states 0..6, fuselage `$F640`
  and their trajectory/projection/debris/attitude graph pass directed checks.
  Natural controller entries cover states 0..4 and end with a state-5 request;
  natural breakup and complete death/recovery remain unchecked. States 7..11,
  `$137E2`'s glyph branch, lives/HUD `$EC78/$CF46` and credit/continue flow
  remain unverified boundaries. The current port throws at state 7's caller.
- The `$E662` companion graph through `$FB08/$FFBE/$FB16/$FB9C` and its
  emitted animation/allocation/removal paths is checked. `$E7DA/$FBD4` have
  NOP/NOP/RTS checks; `$FBC4/$10310` resets only the ROM-defined emission pool,
  with no lives, HUD or credit callee.
- Carrier `$10348..$10E68` and horizon `$10E6A..$10FFE` have separate checks.
  `$10E6A` is a fifteen-segment horizon strip, not an enemy formation. The
  carrier's selector increment/HUD request does not implement general stage
  progression. Special player `$11116/$1112C` remains gated by the selector/
  sequence flag, pending its Phase 3e caller contract.
- Tracking/retarget `$1586C/$158A8/$1592A`, all sixteen `$15968` launch
  directions, bursts `$15C62..$15D36`, impact/debris through `$160A6` and the
  `$153E0..$156CE` missile/shadow/trail graph have routine checks. Natural
  coverage does not establish all stage/spawn combinations or a whole credit.
- World `$8946`, explosion setup `$90AC`, gate explosions
  `$9366/$93A0/$94B8`, projectile-hit `$8C1C` and marking `$9046` are checked.
  Four hit/scoring paths stop before `$E542`; score flow remains Phase 3e.
- Refuel mechanics/setup/parts have independent checks. Controller `$16236`
  starts with `$E224`; its subsequent stage-flow remains Phase 3e.
- Stage scripts, formations, spawn dispatch, scoring/lives, credit/continue
  and attract/service UI remain explicit Phase 3e boundaries. Never supply
  native substitutes for unported behavior. Use labelled directed fixtures
  where natural spawn callers are not yet ported.

## 4. Acceptance and delivery

- [ ] Extend the conditional paired 30/60 Hz player/weapon check to the
  independently scheduled host: ADC input, object records, five shared road
  targets, scaled sizes, normal/interpolated lists, draw/sound-latch events and
  rendered RGB. Supplied caller/sub/compositor inputs must remain visible in
  the reported claim until their producers are independently verified.
- [ ] For every new port, report identical/captured/skipped counts and reasons,
  compare CPU-owned write sets and I/O against MAME, prove a deliberate
  constant mutation fails and list unexercised paths in `ENGINE_NOTES.md`.
  Include natural gameplay and directed endpoint/transition captures.
- [ ] Remove native objects, flight, weapon rules, coordinate/history adapter
  and replaced sound cues only after all replacements pass host acceptance.
- [ ] Run `make test`, `make verify-stage1`, `make oracle-test` and a headless
  launch. Update the roadmap, engine notes and fidelity boundaries, then
  commit and deliver the remaining Phase 3d work in a separate PR. ROMs,
  disassembly output and captures remain ignored.
- [ ] Mark Phase 3d complete only when every acceptance item passes; then draft
  the Phase 3e stage/game-flow plan. Whole-credit RAM/video equality remains
  Phase 3f; waveform equality remains audio acceptance work.
