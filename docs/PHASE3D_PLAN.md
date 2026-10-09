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

The four whole-host requirements remain distinct. The current joint checkpoint
closes the first row and observed sub startup in the 600-frame corpus; remaining ownership and
unverified paths still prevent whole-host acceptance:

| Required closure | Current joint boundary |
| --- | --- |
| IRQ services and retained caller resumptions | Closed in the 600-frame corpus: all 2,893 main entries/services/returns/resumptions and 568 sub services/returns use computed selection and retained state |
| Main producers and sub sprite/road progress | Zero supplied main phases; one initial context per CPU; all 17 sub startup PCs computed, zero later sub supplied phases/writes. Initial partial sub ISR and unexercised main/sub paths remain boundaries |
| ADC/buttons, device deadlines and sound timing | Native Z80, PCM/FM stream clocks, pending input/ADC/YM timers, periodic device heads and IRQ/RESET boosts pass in the joint window. Mixed/resampled speaker output and Stage 1 audio wiring now pass; boot CPU/frontier edges and unobserved paths remain explicit |
| Video ownership and compositor inputs | 854 bank swaps, 278 road controls, 568 sprite feedback frames and 575 natural partial updates match. Line-261 latches and joint RGB now match in natural/directed corpora: 53,329,920 pixels, 744 latches, zero differences/skips. Initial video boundaries, unobserved states and game integration remain open |

The preceding checkpoint and its exclusions are historical; the current main
closure result is recorded below. The original 600-frame expanded joint
experiment failed on natural RESET. The preceding continuation passed
19,055,747 native records,
49,092,373 native phases and 2,312,354 bounded grants, zero skips. It includes
2,893 main entries/acknowledges/RTEs, 2,614 retained main entries, 2,616
retained resumptions and 568 retained sub services/returns. Both main RESET
callbacks, synchronized sub cancellation and reset vectors/prefetches are computed;
startup PEA/RTE/TRAP execution was supplied. That checkpoint had 4,357,192
supplied main phases at 4,507 PCs. Those main boundaries are now closed by
the result below; preserve the remaining labelled device/sub boundaries and
never seed replacement CPU contexts on a computed mismatch.

The current strict joint replay matches 20,532,168 native records and
53,449,620 ordered phases, zero differences/skips. Main supplies zero
execution; each CPU has one initial context. All 17 sub startup PCs and
568 services/returns are computed; the only supplied sub phases are 46 at
three PCs in the ISR already running at the capture boundary, with zero
writes and no later reseeding. Main coverage is 5,628/7,786 PCs and sub
generated coverage is 658/1,136, plus the 17 startup PCs. Unobserved paths
remain unverified. Strict targets require main closure, sub continuity,
sound handshake and video frontier assertions; full replay requires RESET.
The latest joint sound/audio gates compute Z80 transactions and PCM/FM stream
batches from continuous state. Device head times and boosts are assertions
inside the joint CPU window; pre-window boot frontiers remain supplied.
Native Stage 1 remains active. See the final `ENGINE_NOTES.md` section for all boundaries.

## Next tasks for whole-host acceptance

Complete these in dependency order; each port needs MAME comparisons,
explicit coverage exclusions and a rejected mutation before game wiring.

1. **Native sound CPU progress and interrupt delivery — accepted for the
   current joint contract.** The 600-frame joint run computes Z80 grant
   completions and all sound transactions from reset: 10,634,678/10,634,678
   bus records and nanosecond timestamps match, zero differences/skips;
   819,024 computed sound grants, 172 NMI entries and 172 RETN. Main supplies
   3,049 checked sound host writes; 45 pre-window boot host writes remain
   explicit external inputs. No sound CPU/register/RAM state is reseeded.
   Computed latch/reset callbacks preserve synchronized NMI assert/clear.
   The separate original-program MAME fixture matches 44,767/44,767 bus
   records, 2 IRQ entries/RETI and retained caller registers. Seven sound
   execution mutations are rejected. Main/sub's 53,449,620 accepted phases
   and 40,714,240 RGB pixels still match. Unobserved instruction/interrupt
   combinations, IM0/IM2, HALT interrupt timing remain explicit; native device deadlines/FM stream ownership are covered
   by task 2 below. See the native sound
   continuation in ENGINE_NOTES for the exact contract and reproduction.
2. **Native audio/device deadlines and scheduler ownership — accepted for
   the current joint window.** The new 600-frame gate computes all 1,338,472 device head
   deadlines after the initial joint context, including pending input queues,
   ADC, YM, scanline, screen, tile-latch and 50 Hz sound-update timers. IRQ and
   RESET callbacks own boosts. Trace heads, outer delivery markers and quantum
   requests are assertions in that window, with no supplied device frontier
   driving it. Continuous PCM/FM state yields 314,376 raw PCM and 628,796 raw
   FM stereo samples, 6,216/423,102 batch timestamps/indexes, 1,159 synchronized
   mode callbacks (1,158 late) and 1,170 timer-A callbacks, all identical to
   MAME. Timer-A and timer-B diagnostics each match two IRQ/RETI entries;
   timer B phase comes from generated FM samples. Thirteen owner mutations
   reject; the complete required suite passes. Existing standalone PCM
   captures and their mutations remain required. The 33,030 pre-window boot
   grant groups still use supplied main/sub/frontier edges; the 45 boot sound
   host writes remain external. Mixed/resampled speaker output now passes 482,881 stereo frames/503
   batches bit-for-bit, including a coupled native replay, directed panning
   and clipping, and ten rejected mutations. Stage 1 audio wiring uses the
   verified devices, fixed speaker cadence and a 20 ms playback buffer.
   Its command FIFO is an external reconstruction transport, not acceptance
   of main/sub gameplay producers. Unobserved combinations and whole-game
   wiring still require tasks 4–6.
3. **Tile/text latches and full video composition.** Compute tile latch
   timing and latched scroll/page state. Feed independently owned text/tile,
   road, sprite, palette, priority and shadow inputs into the compositor at
   native partial update frontiers. Compare joint RGB output pixel-for-pixel
   with MAME; the existing standalone 300-frame pixel oracle is a regression
   check, not the joint ownership proof. Add directed visible-area display
   enable changes; the natural corpus did not detect their omission.
   **Observed-corpus acceptance now passes:** natural 120/600-frame and
   directed 120-frame joint runs compare 53,329,920 pixels and 744 native
   latches, zero differences/skips. Directed coverage includes pending
   globals, row/column and alternate tables, 60 blank clips, 174,670 shadow
   and 176,198 hilight pixels. Ten mutations are rejected, including omitted
   display frontiers and priority equality. Initial boundaries, supplied
   directed device stimuli and unobserved states remain explicit; Stage 1
   integration and whole-host acceptance are not promoted by this result.
4. **Remaining main/sub execution and exception coverage.** Exercise and
   verify the 2,158 main and 478 generated sub PCs absent from the current
   corpus, plus unobserved branch directions. Include IRQ wrapper PCs
   `$B836/$B83A/$B83E/$146D8/$14702`, nested higher-priority IRQ selection,
   divide exception paths and startup RTE privilege exceptions. Expand
   captures across gameplay states and reset sequences; never silently
   accept an unported path or seed replacement CPU state on a mismatch.
5. **Remove or explicitly bound fixture edges.** The cold bus-only gate now
   checks 33,234 grants, 418,054 natural shared reads, 64 labelled mirror
   probes, byte lanes and RAM retention across six tagged sub reset entries;
   eight mutations fail. Its CPU producers/completions and device frontiers
   remain supplied. Extend native instruction
   execution through cold boot and the initial partial sub ISR (46 supplied
   phases), and beyond the two parked final continuations. Prove boot port,
   bank/render and CPU initialization rather than promoting the current
   fixtures to startup acceptance. Retain only the explicitly defined ROM
   and external physical-input fixtures in the final acceptance contract.
6. **Whole-host acceptance and game integration.** Audio subsystem wiring
   into Stage 1 is complete and independently verified; the reconstruction
   gameplay producers remain outside that audio contract. Run the complete joint
   CPU/device/video/audio oracle with native grants, deadlines, IRQs and
   producers, zero supplied execution/state replacements/read seeds, and
   zero differences/skips within the declared coverage. Include directed
   mutations for each owner and expanded gameplay/reset coverage. Only then
   replace the reconstruction loop with the verified host and rerun ROM-free,
   Stage 1, all oracle, pixel/audio and headless integration checks.

The observed main IRQ/caller stream and sub reset startup are already
verified; these tasks extend coverage and close remaining ownership rather
than reopening those completed corpus checks. Whole-host acceptance remains
pending until the final contract and its exclusions are explicit.

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
- [x] Compute all fifteen sound-ring producer instructions at `$B84C..$B884`
  from supplied caller entry, including bus phases, CCR, reset/suppression,
  ring wrap, pending count and RTS. Directed endpoint gates and four mutations
  cover the producer and the newly exposed `$D0DA` returned counter instruction.
  Natural/directed producer calls match 252/252, zero skips.
  Producer caller cadence and arbitrary IRQ overlap remain pending; see the
  timed sound producer continuation in `ENGINE_NOTES.md`.
- [x] Compute bounded button/axis-to-target scheduling at `$14976/$B0AC`,
  including `$D0DE`. Physical button inputs use `OMainIO`; natural raw-axis
  reads use computed ADC samples and mux destinations. Directed coverage
  includes 165 production-reachable PCs, both applicable branch directions,
  axis clamps, signed deadbands, throttle DIP and control override endpoints.
  Initial lanes, fixture inputs, hardware frontiers and caller cadence remain
  explicit. Six ROM-disabled debug PCs are unverified. Use targeted short and
  directed captures first, then the combined natural capture; retain oracle
  validation per graph. See the timed input continuation in `ENGINE_NOTES.md`.
- [x] Compute bounded live flight/projection `$98C0..$9C32` through the
  already timed `$B628` append graph, including hardware math, table/flip,
  slew/history and transition paths. Directed gates require all 232 flight
  PCs, both directions of all 25 conditional branches, endpoints and five
  extra returned instructions. Natural 600-frame execution matches
  2,763,360/2,763,360 records, zero skips and post-RTE holes; exit events fall
  to 7,809. Caller/sub/publication cadence and paired host road acceptance
  remain supplied. See the timed flight continuation in `ENGINE_NOTES.md`.
- [x] Compute bounded shadow/exhaust `$9C34..$9D2A/$AE8C..$B0A8`
  through the existing append graph. Directed gates require all 179 PCs,
  both directions of all 17 conditional branches, byte/lifetime/bank/table
  endpoints and the extra `$77AC` RTS continuation. Natural 600-frame
  execution matches 2,773,349/2,773,349 records, zero skips and post-RTE
  holes; exit events fall to 6,693. Safe wait-only fixture injection avoids
  resuming partially completed stack instructions. Caller/sub/publication
  cadence remains supplied; see the companion continuation in `ENGINE_NOTES.md`.
- [x] Compute the eight sub publication wait/release instructions
  `$452/$456/$458/$556/$55A/$55C/$640/$646`, including ordered bus phases,
  CCR and RTS. Both signed waits consume retained two-CPU signal state;
  main RMW values are latched at checked read frontiers and writes computed.
  All PCs, branch directions and separate signed endpoint sets pass MAME;
  natural 600-frame execution matches 6,305,996/6,305,996 instructions.
  Shared-bus delivery order, main writer times and sub IRQ/callee entries
  remain supplied; see the sub publication continuation in `ENGINE_NOTES.md`.
- [x] Compute the eight sub publication caller instructions
  `$432/$436/$43A/$43E/$442/$446/$44A/$44E`: BSR return-stack writes,
  one-bit request shifts and carry-controlled optional sprite calls. Retain
  computed stack/request lanes across supplied callee/IRQ gaps. Natural and
  directed captures require all sixteen bounded PCs, both directions of five
  branches and separate readiness/shift endpoints. Road/sprite execution,
  sub IRQ timing and cross-CPU delivery remain supplied; see the sub caller
  continuation in `ENGINE_NOTES.md`.
- [x] Compute independently timed bounded sprite/road progress: road wrapper/
  initialization, sprite sort/transform/interpolation, bank/pitch/rotation,
  normal raster and special-road graphs (1,136 new PCs). Directed 360-frame
  acceptance covers every new PC and matches 6,171,481 instructions and
  13,787,074 bus phases; natural 600-frame acceptance matches 11,896,153
  instructions and 27,415,741 phases, zero differences/skips. Seven mutations
  are rejected. Sub IRQ, producer delivery, arbitration and renderer
  composition remain supplied; see `ENGINE_NOTES.md` for eight unexercised
  branch/loop directions and completion-tail limits.
- [x] Close the two unrelated directed post-RTE instructions at `$6844/$14EE8`.
  Directed host acceptance now requires strict whole-corpus return closure:
  2,055,260 records, 6,036,146 phases and all 260 host PCs match, zero skips
  and zero unported post-RTE returns. Both mask mutations are rejected;
  surrounding handlers and cleared-bit inputs remain explicit boundaries.
- [x] Join bounded main/sub publication progress under the checked arbiter,
  preserving native RMW operands and private instruction-step debt across
  grants. Natural 120 matches 1,788,368 native instructions, 4,818,916 phases,
  182,481 bounded completions and 1,271,730 shared reads, zero skips.
  327 native writes replace supplied producers; 435 entry contexts and
  141,969 other shared writers remain supplied. 22/24 selected PCs execute
  jointly; missed-count `$1486A` and optional BSR `$43E` lack joint coverage.
  IRQ/ADC/sub callee/device frontiers still prevent whole-host acceptance.
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
  Partial continuation: the timed dispatcher now joins the 107-PC weapon
  controller/fire/allocation graph through real JSR/BSR/RTS stack effects.
  The subsequent 258-PC continuation joins flight initialization/recovery,
  HUD/decimal refresh and missile initialization with its ROM sound lookup.
  `$9D2C` pool reset and remaining projectile/sequence continuations remain
  boundaries; see the caller continuations in `ENGINE_NOTES.md`.
- [ ] Connect live `$9810/$98C0`, shadow/exhaust, recovery `$9626/$964C` and
  sequence `$1653A..$1657A/$1678E`. The live fuselage is slot 131 at `$C0C0`;
  sequence compounds use a different base. Flight initialization/recovery now
  joins its real dispatcher; sequence composition and full host acceptance
  remain pending. Keep word/long arithmetic widths
  and the real multiplier/divider chips; retain `$C5A8`'s verified RNG contract.
- [ ] Compare every compound object record, previous position, emitted request,
  shared bank/pitch/altitude target and the sub CPU's `$7800` scaled-size table.
  Preserve 30 Hz logic and the original two-display-frame sub handshake.
- [ ] Connect verified weapon, projectile, reload and HUD routines at their
  actual callers. Retain held/released fire, launch-side, cooldown/reload,
  exhaustion, targeting, removal and attachment-transition checks. The timed
  allocator now calls `$CFC4/$D062` through its actual stack and bus phases;
  remaining projectile continuations and whole-host acceptance are pending.
- [ ] Connect sound-ring producers and the timer consumer at their actual
  callers. Verify command order and latch timing before removing native sound
  events; routine ring equality does not establish producer cadence. Recovery
  `$969A` and launch `$A022` now enter the checked producer through actual
  BSRs. Independent producer cadence/latch/waveform acceptance is pending.

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

## Sub IRQ and main/sub grant checkpoint — 2026-10-08

- [x] Compute independent sub IRQ selection, autovector entry, all thirteen
  handler PCs, MOVEM/RTE stack restoration and linked user resumption.
  Natural 600: 12,072,480 instruction/entry records and 28,083,147 bus phases;
  zero differences/skips, 599 exact returns, zero supplied tail continuations.
  Directed gates cover countdown/mutex/pause and all 1,136 sprite/road PCs.
- [x] Check native main/sub/Z80 grant budgets, quantum boost/expiry, overrun,
  zero-progress aborts and main/sub RAM delivery against a separate MAME
  scheduler observation build. 239,177 grants and 1,271,730 shared reads match;
  nine sub and four arbitration mutations fail. Producer/completion frontiers
  and other-device deadlines remain labelled inputs.
- [x] Extend joint publication execution through bounded sprite/road callees,
  retaining internal-tail debt before successor instructions. Natural 120:
  2,420,931 instructions, 6,110,061 phases and 242,205 bounded grant
  completions match, zero differences/skips. 102,479 shared writes are
  computed; 39,817 writers and 347 entry contexts remain supplied. Joint
  natural coverage reaches 631/1,136 graph PCs; the other 505 PCs and
  unexercised branch directions remain outside joint coverage. IRQ service,
  main producer/caller and device/video/sound frontiers remain explicit.
- [x] Join the bounded sub IRQ entry/service/RTE and VPA wait under the
  arbiter, retaining stack banks, registers and computed IRQ selection across
  grants. Natural 120: 88/88 services restore their interrupted state;
  2,491,840 instruction/IRQ records and 6,309,809 phases match, zero skips.
  All thirteen service PCs execute. Both countdown branches and DBF outcomes
  are covered; mutex-busy and arbitrary countdown endpoints remain outside
  joint natural coverage. Main `$1458A..$145B8` computes all twelve dispatcher
  PCs, active/inactive records and loop completion. 105,394 shared writes are
  computed; 36,902 remain supplied, with one initial sub and 4,630 main entry
  contexts. Main IRQ and handler/producer/device/video/sound closure remain.
- [ ] Compose both CPU instruction continuations under the checked arbiter,
  close main IRQ entry/service/RTE, replace remaining main producer/caller boundaries and complete paired
  player/weapon/video/sound acceptance. Whole-host acceptance is pending;
  native Stage 1 remains active. See `ENGINE_NOTES.md` for scope and sensitivity.
- [x] Expand joint main execution to existing caller ports and 103 literal
  main IRQ RAM-wrapper/sound-ring instructions. Natural 120 covers 679/1,429
  main PCs, including 92/103 added wrapper PCs; 3,045,337 records and
  7,960,655 phases match, zero differences/skips. Computed shared writes reach
  141,627; 669 remain supplied. This is an attract-mode checkpoint, with
  main entry/device/RTE and unported callee gaps still supplied. The longer
  original 600-frame experiment exposed RESET and failed (subsequently closed below); all four closure items above
  remain open. See the expanded joint main continuation in `ENGINE_NOTES.md`.

- [x] Join literal main IRQ2/4/6 entry, real VPA acknowledge and RTE into
  retained caller execution. Fresh 120 matches 448 entries/acknowledges/RTEs,
  432 retained entries and resumptions, 7,967,919 native phases and 367,179
  bounded grants, zero skips. All three levels occur; four mutations fail.
  Physical main IRQ selection and the unported service callee gaps remain
  explicit. See the joint main interrupt continuation in `ENGINE_NOTES.md`.

- [x] Extend joint main entry/VPA/RTE to the 600-frame live-gameplay stream.
  19,055,747 records, 49,092,373 native phases and 2,312,354 bounded grants
  match, zero skips; 2,893 entries/acknowledges/RTEs and natural RESET pass.
  Coverage is 1,121/1,436 main PCs and 658/1,136 sub PCs. Main selection and
  4,357,192 supplied main phases at 4,507 PCs still prevent CPU closure;
  the other three acceptance rows also remain open.
