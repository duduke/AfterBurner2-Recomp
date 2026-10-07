# Phase 3d — objects, input and flight

Phase 3d is **not complete**. This is the current implementation and
continuation plan. Detailed oracle results, mutation evidence, commands and
checkpoint history live in [ENGINE_NOTES.md](ENGINE_NOTES.md); completion
items live in [ROADMAP.md](ROADMAP.md). Read the roadmap's "Working on the
remaining tasks" before changing code.

[PR #22](https://github.com/duduke/AfterBurner2-Recomp/pull/22) merged on
2026-10-07. Its `147f310` checkpoint carries computed bounded
caller IRQ2/IRQ6 entry through the verified handler and RTE, restoring the
retained user stack before continuing caller state/time. Natural gameplay and
directed fixtures pass 1,673/1,673 services and 74,059/74,059 handler bus events,
zero skips, with 1,644 computed caller resumptions. The original capture explicitly marked
29 supplied post-RTE intervals. The next bounded checkpoint now computes
immediate exception selection/entry from restored SR and physical IPL;
all twelve original and fourteen subsequent returned-instruction boundaries
are now ported. The ten subsequent returns at `$8AFE/$B632` are now covered; expanded
coverage retains seven at `$8B02/$B636`.
See the IRQ4 RTE, returned-word and successor continuations in `ENGINE_NOTES.md`.

The previously verified IRQ4 callee graphs retain 3,519/3,519 envelopes and
8,099,444/8,099,444 predicted bus events, zero skips. Physical IPL comes from
bus-phase delivery and completion-tail clocks; bounded callers and omitted busy
spins carry computed state/time. IRQ4 RTE/USP and immediate entry now pass
714/714 services. The `$17146/$C6F8` continuation closes the IRQ4 callee graph:
4,830/4,830 envelopes and 9,567,126/9,567,126 bus events match, zero skips,
without supplied callee execution. Sub-write/input boundaries remain supplied.
Unported handler
resumptions, shared-RAM and ADC producer state, first-resume wait operands and sub progress remain
supplied boundaries. Phase 3d remains incomplete.

## Scope and current status

Phase 3c supplies shared arcade RAM, the interrupt/caller handshake, 30 Hz
object scheduling, palette routines, request publication, the sub CPU sprite
transform and normal/interpolated hardware blits. Phase 3d replaces
`OLevelObjs`, native flight/weapon rules, the sprite-coordinate/history
adapter and hand-picked sound cues once the verified replacements pass host
acceptance. `OEngine` still uses the native Stage 1 host; keep those producers
and their adapter until acceptance passes.

[PR #21](https://github.com/duduke/AfterBurner2-Recomp/pull/21) merged on
2026-10-07. `OMainIO` installs the timed ADC/mux, active-low digital input and
sound-latch endpoints in `OEngine`. The same production callbacks pass the IRQ and directed
ADC oracles. Native `tick()` supplies no CPU clock or interrupt boundary: it does
not call the sampler, input handler or reload producer. Their live wiring remains
pending independent scheduling. The bounded caller oracle now consumes
computed ADC results from the continuous production `OMainIO` timeline,
with CPU bus delivery and ADC transaction time distinguished. Physical
input frames and unported hardware transaction/frontier times remain supplied;
see the production input-producer continuation and PR #21 review continuation in
[ENGINE_NOTES.md](ENGINE_NOTES.md).

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
| Bounded caller IRQ2/IRQ6 resumptions | 1,673/1,673 services; 74,059/74,059 handler bus events; 1,644 computed caller resumptions; zero skips | 29 unported post-RTE intervals, shared-RAM/ADC producer state, first-resume wait operands and IRQ4 |
| Bounded RTE selection and immediate entry | 1,673/1,673 restored-mask selections; 17/17 immediate entries; zero skips; three mutations fail | 12 unported returned-instruction boundaries, IRQ4 returns, producer state/cadence and sub progress |
| Continuous production ADC producer and bounded caller | 1,656 computed ADC reads; 1,673/1,673 services; 74,059/74,059 bus events; 20,387,341/20,387,341 physical observations; zero skips; two mutations fail | External physical input frames, captured hardware transaction/frontier times, shared RAM and IRQ4/unported caller/sub progress |
| Conditional player/weapon/road/compositor pairing | 122/122 complete dispatchers; 240/240 road passes and RGB frames; zero skips; sequence/RNG entry routing and two mutations checked | 313 verified target writes, 405 unported target writes, 482 initial targets; caller/sub timing, road control and tile/palette feedback remain supplied |
| Closed IRQ4 game-flow/coin callee graphs | 4,830/4,830 envelopes; 9,567,126/9,567,126 bus events; zero skips; seven mutations fail | Initial CPU contexts, physical input/sub lanes and unexercised instruction/flag combinations; other caller/sub scheduling |
| Ten successor return boundaries | 10/10 exact CALL6 returns; 47,839/47,839 added natural instructions; 22 taken BGE fixtures; zero skips; two mutations fail | Seven further returns at `$8B02/$B636`, full handler/sub timing and shared producers |
| Fourteen returned-word boundaries | 14/14 baseline returns; 47,839/47,839 added instructions; zero skips; two mutations fail | Ten successor returns at `$8AFE/$B632`, shared inputs, full handler timing and sub progress |
| IRQ4 RTE and original twelve returned-instruction boundaries | 714/714 IRQ4 returns; 49,973/49,973 added instructions; zero skips; five mutations fail | Initial interrupted contexts, physical IPL, IRQ4 game-flow/sub frontiers; ten successor returns and full handler graphs |
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
  This earlier checkpoint supplied game-flow caller state/durations; the
  `$17146/$C6F8` continuation below now closes those callee boundaries.
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
- [x] Link computed bounded caller IRQ2/IRQ6 entry, handler execution and
  RTE back to caller state/time. Preserve the exception frame and retained
  USP; explicitly mark unported post-RTE intervals, including back-to-back
  exception entry. Shared-RAM/ADC producer state and IRQ4 remain supplied.
  See the caller/IRQ2/IRQ6 resumption checkpoint in `ENGINE_NOTES.md`.
- [x] Compute IRQ2/IRQ6 RTE selection at the final return prefetch, preserving
  the restored interrupt mask and supervisor stack into immediate exception
  entry. MAME retains the RTE bus PC until another instruction executes.
  Unported returned instructions and IRQ4 returns remain explicit boundaries;
  counts and mutation evidence are in `ENGINE_NOTES.md`.
- [x] Connect IRQ4 RTE/USP/restored-mask selection and immediate entry at the
  verified handler envelope, and port all six PCs behind the original twelve
  returned-instruction boundaries. Keep one scheduler through linked spins.
  The following checkpoint covers all fourteen returns at `$8AF8/$B62E`;
  game-flow callbacks and sub-write frontiers remain inputs. See the IRQ4 RTE
  continuation in `ENGINE_NOTES.md`.
- [x] Complete all fourteen CALL5 returned-instruction boundaries at `$8AF8`
  and `$B62E`. The baseline check verifies 14/14 matching computed resumptions,
  zero missing/skipped; added instructions match 47,839/47,839, zero skips.
  Enlarged coverage still has ten returns at `$8AFE/$B632` and full handler/
  callee/sub timing boundaries. See the returned-word continuation in
  `ENGINE_NOTES.md`.
- [x] Complete all ten CALL6 successor boundaries at `$8AFE/$B632`:
  10/10 exact computed resumptions, zero missing/skipped; 47,839 natural
  instruction executions and taken BGE fixtures match MAME. The expansion
  exposed seven returns at `$8B02/$B636`, now closed below; other
  handler/callee/sub scheduling remains pending. See the successor continuation in `ENGINE_NOTES.md`.
- [x] Close all seven further returns and all 31 subsequently exposed returns.
  Extend the literal caller port through `$8B02-$8C1A`, `$9556-$9572`,
  `$B636-$B654` (97 PCs). Both acceptance corpora require zero
  unported post-RTE returns: 2,694,300/2,694,300 instruction/entry records,
  7,910,253/7,910,253 caller bus phases, zero skips. Instruction-hook snapshots
  check internal ALU completion state; late selection uses physical delivery
  at the completed prefetch. RAM-target RTE prefetches are bus reads too.
  This closes the bounded world/publication component, not the remaining
  handler graphs, IRQ4 game-flow callbacks or sub execution.
- [x] Compute the IRQ4 `$14990/$E2DA` RTS callees instead of supplying
  callback registers and durations. Natural and directed captures check
  713/713 calls per entry, zero skips; both timing mutations fail. These
  entries are single RTS instructions, distinct from their adjacent helpers.
  See the RTS-callee continuation in `ENGINE_NOTES.md`.
- [x] Close the remaining IRQ4 `$17146/$C6F8` callee graphs. Their 1,507
  added literal instruction PCs compute attract/high-score, coin/credit,
  lamp, checksum, sound and dependent text/HUD/palette work. All ten service
  corpora pass 4,830/4,830 envelopes and 9,567,126/9,567,126 bus events,
  zero skips and no callee transcripts. Directed checks require all eight
  high-score states and fifteen coinage selectors. Seven mutations fail;
  coverage limits are listed in `ENGINE_NOTES.md`.
- [x] Compute the `$7BB6..$7C10` compound-follower continuation through
  animation, `$B628` publication and RTS, instead of cutting after the first
  instruction. Directed coverage requires all 19 PCs, stage selectors 1/7/2
  and animation operands 0/4/8/128. This is bounded handler timing; other
  handler graphs and external producer frontiers remain. See the follower
  continuation in `ENGINE_NOTES.md`.
- [x] Compute the shared-ready `$C082..$C098`, world-initializer
  `$88CA..$8944`, and publication-install `$B66E` continuations. Require
  both ready-bit branch states and all projection PCs; preserve owned
  caller RAM across IRQ2/6 instead of reseeding it from service snapshots.
  Natural and directed captures match; five mutations fail.
- [x] Link computed IRQ4 service/RTE with the caller scheduler; continue
  through the 67-PC sprite-clear and 99-PC trail graphs subsequently exposed.
  Require all PCs and both directions of the six trail branches. Track both
  sprite banks while keeping hardware-rendered cursor words explicit.
- [ ] Finish independent scheduling across caller/ISR resumptions, ADC input
  producers, publication wait boundaries and sub progress.
  Current IRQ contexts/frontiers, physical input bytes, unknown caller
  transcripts, sub-produced blit operands and hidden renderer feedback remain
  inputs. Advancing an arbitrary frame clock would invent IRQ latency and
  operand ordering.
- [x] Pair `OSubRoad` with the main target write frontiers and compositor at
  captured consumer/flip boundaries. All 240 road passes and RGB frames match,
  zero skips. Word provenance distinguishes verified, external and initial
  targets; road work can consume the next dispatcher's targets before sprite
  publication. See the road-pairing continuation in `ENGINE_NOTES.md`.
- [ ] Pair `OSubRoad` raster outputs with the computed bank/pitch/altitude
  targets and the compositor. Captured road raster, tile/palette inputs and
  hidden renderer feedback remain separate acceptance boundaries for the
  independently scheduled host. The conditional check above computes raster
  bytes, but retains target producer and caller/sub timing boundaries.

IRQ4 `$17146/$C6F8` now run their checked callee graphs with no execution
callbacks. Initial CPU contexts, physical input and sub frontiers remain
explicit boundaries. The sampler's actual order remains acknowledgement,
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
- [ ] Connect sound-ring producers and the timer consumer at their actual
  callers. Verify command order and latch timing before removing native sound
  events; routine ring equality does not establish producer cadence.

Each handler's independent capture contract includes complete object/shared
RAM, D0..D7/A0..A6, main/sub math-chip state, IO1 input and ordered CPU-owned
RAM/I/O writes. Preserve that contract during wiring, record other-CPU writes
to actual read inputs, exclude active stacks and reject unrelated main writes.

## 3. Preserve coverage limits and Phase 3e boundaries

`python3 tools/handler_inventory.py` reads the user's runtime ROM template
table and inventories production entry routing. The initial 254 records have
18 routed, 229 empty and seven unported handlers. This does not cover dynamic
handler replacement or establish formation/spawn behavior. The seven initial
entries and observed later caller boundaries are documented in `ENGINE_NOTES.md`.

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
