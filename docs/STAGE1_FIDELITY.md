# Stage 1 reference experience

PR #1 targets the Stage 1 appearance and playable experience selected by the
user from `Afterburner2.mov`. The video is reference material, not instructions.
Phase 3c runs native object logic at 30 Hz and display at 60 Hz. The temporary
native stage still clears after 3,600 logic ticks (7,200 display ticks). Enter resets
score, hits, ammunition, scenery, objects, palettes, and sound playback.

## Original data used at runtime

Observations were checked against the user's CRC-verified `aburner2` ROMs and
local MAME Stage 1 captures. Only table addresses/interpretation are recorded
here; copyrighted ROM bytes and captures are not included in the repository.

- Sub CPU sprite records are 16 bytes: width at +0, last source row at +2
  (height is that value plus one), bank bits at +4, offset at +6, stride at +8.
- Player attitude tables start at `0x10000`, tilt stride `0x190`, pitch stride
  `0x50`. Normal rear view selects `0x100f0`, bank 0, offset `0x0ab2`, 47 rows.
- Bank-3 Stage 1 canopy LOD records: `0x165f0`, `0x16600`, `0x16610`.
  Shore rocks: `0x16af0`, `0x16b00`. Sea patch: `0x15f60` (offset `0x201d`).
- Fighter LODs: `0x11a30` through `0x11a60`; fighter palette `0xcb`.
  Canopy palettes 0/`0x41`, rocks `0x10`, sea `0x23`.
- Player colors use palette `0xf2`, animated from main ROM `0x2bf5e` in
  32-byte frames. Reticle palette `0xf1` comes from `0x2ea14`.
- HUD palette sources `0x30b14`–`0x30bc4` and original text tile codes drive
  labels/numbers/icons; the text layer's visible origin is column 24.
- The Z80 runs the original sound ROM at 4 MHz. ROM `0000–efff`, PCM registers
  `f000–f0ff` mirrored through `f7ff`, RAM `f800–ffff`; mirrored YM ports
  `00/01`, command latch `40`, NMI until latch read, YM timer IRQ.
  Observed commands include music `91`, cannon `90`, missile `40`, explosion
  `4c/4e`, lock `e0/c1`, and engine `f1` followed by its speed parameter.

## Reconstructed behavior and limits

Phase 3b replaced the native terrain projection, sea-sprite sampling,
canopy/shore patches and speed-gauge fill with verified road/scenery ports.
Road scanlines, tile uploads and ground palettes now reach the hardware
compositor through `MainBus`. The host selects ordinary flight (`$10`);
the full credit/stage dispatcher is still pending 3e. Until the exact player
port in 3d supplies shared attitude targets, the native stick does not bank
or pitch this road. The integration test sets arcade targets directly.

Phase 3c replaced native depth sorting, hardware descriptor encoding and
palette timing. `OMainFrame` preserves both interrupt branches, pointer swaps,
mutex, lamps and callee order. Sprite requests pass through `$00B628`, the
verified sub transform and `$00B7C2/$00B7D2`; palette animation/fade follow
runtime ROM pointers. The sub CPU transforms after the logic phase and
latches/generates road on the next display phase, once per two display frames.

`OSprites` remains a temporary native producer adapter: it resolves exact
bank/offset/stride/row tuples to real ROM records, inverts the ROM zoom formula
for its linear scale branch, converts native top/left coordinates, truncates
native depth to the request byte, and uses emission-slot position history.
It omits unsupported tuples/scales. Per-native-sprite priority is discarded in
favor of the arcade global priority. This is not verified object behavior.
Host boot still chooses ordinary flight and a terminal airframe palette phase;
credits, compound player setup and service/attract dispatch remain 3d/3e.

Phase 3d now has independent checks for input mapping/buttons, live flight,
recovery and compound parts, player projectiles, reload/HUD, world projection,
ground effects, scaled player collision and a paired enemy missile/shadow/trail
graph, alongside the earlier dispatcher/RNG/sound ring. Observable ADC result
and timer/mux timelines also match MAME. Damage controllers through state 6,
fuselage animation and debris match 1,792/1,792 directed and 120/120 ordinary gameplay
MAME calls, with no skips. Damage companion, carrier/horizon, enemy tracking/burst/debris and remaining
world handler graphs now have separate routine checks. Refuel stage-flow and
a natural death sequence remain unverified. These ports remain unwired until
the actual host ADC/IRQ caller and paired player/weapon acceptance pass.
`OLevelObjs`, its request adapter and native sound cues therefore remain.
PR #21 merged on 2026-10-07 with verified endpoint wiring only.
The host installs the verified timed ADC/mux, digital input and sound-latch
callbacks through `OMainIO`. Advancing physical time remains explicit; native
frame ticks do not manufacture sampler, flight or reload calls. Routine replay
checks use these same endpoint callbacks with supplied CPU bus timestamps.
This foundation does not change the native flight or combat experience.

Formation cadence/trajectories, aiming, projectile motion, hitboxes, scoring
and the 3,600-logic-tick duration remain native rules. Original fighter art is
reused, but exact attitude selection/animation timing is still future work.
This is not yet a frame-identical whole-game arcade port.

The stage has fighter waves, target tracking, cannon/homing missiles, enemy
missiles, hits and temporary invulnerability. Surviving reaches a clear screen;
there is no lives/game-over loop or later-stage progression yet. Carrier
introduction, refueling, attract mode and all remaining stages are excluded.
Tilemap row/column fine-scroll is implemented and video-oracle verified. Sound output is exercised
with real ROMs, but exact original command timing, balance and waveform have
not been verified against arcade audio.

## Reproduce checks

From the repository root, supply the original ROMs described in `roms/README.md`:

```sh
make -j4
make test
make verify-stage1
make render-scene
./build/render_scene build/level.ppm level 480
./build/render_scene build/right.ppm right 480
./build/render_scene build/left.ppm left 480
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./afterburner2 --frames 180
```

The right/left visual fixtures set an arcade bank target directly, separately
from native controls, to exercise the hardware road path.

For memory checks, compile the integration suite with
`-fsanitize=address,undefined -fno-omit-frame-pointer` and run from the root.
All generated captures and supplied ROMs remain ignored.

The Phase 3d damage companion continuation adds independent routine ports for
`$FB08/$FB16`, both allocated emission/animation graphs, removal and the
`$FBC4/$10310` emission-pool reset. These remain unwired, alongside the checked
pre-lives damage controller and debris routines. Their MAME checks do not
establish the host death/recovery sequence, sound cadence or player/weapon
30/60 Hz equality. Native `OLevelObjs` and its request adapter remain required.

The remaining handler graph continuation adds `OMainSequence`, tracking and
burst/debris enemy families, world variants and explosion setup. Directed
checks match MAME for 4,539 of 4,544 calls; five sub-input races are skipped. Projectile-hit checks stop
before scoring `$E542`; the refuel controller's first call `$E224` remains
Phase 3e. Carrier and refuel routine coverage does not establish stage flow.
Native flight, objects, sound cues and their host adapter remain in use.

[PR #11](https://github.com/duduke/AfterBurner2-Recomp/pull/11) is the authorized
handler-graph checkpoint. Its independent routine checks leave the native
Stage 1 experience unchanged. Phase 3d remains incomplete until the host
input/IRQ, sound cadence and paired player/weapon acceptance checks pass.

The comparator IRQ continuation verifies the full acknowledge/sample/start/
sound-consume chain against a continuous MAME hardware timeline: 7,058/7,058
calls and 45,176/45,176 events identical, including 946 sound-latch writes,
0 skipped. Separate physical axis endpoint and button mapping checks match
720/720 calls. This test supplies MAME bus timestamps and entry work RAM;
it does not independently schedule host IRQs or produce the sound ring from
ported gameplay. `OEngine` still runs native Stage 1 objects and sound cues.

The board-timeline continuation independently generates physical EXCK,
VBLANK and ADC deadlines from reset; 669,845/669,845 MAME probe events match,
0 skipped, and three mutations fail. Its frame period is 16.768 ms, derived
from the hardware clock. CPU transaction timestamps remain capture inputs;
The board oracle alone does not establish CPU IRQ service latency; the
bounded service check follows below. `OEngine` retains its native display-step
scheduling until host wiring and paired acceptance pass.
This checkpoint does not replace native controls, objects or sound cues.

The CPU service extension checks IRQ2/IRQ6 entry, ADC/sound accesses and
stack/return timings: 9,439/9,439 calls and 474,024/474,024 bus events match,
0 skipped, with seven mutations rejected. The unported main instruction's
last-bus boundary and entry context remain external inputs. Running
`./afterburner2` shows the existing Stage 1 experience; this timing port is
still separate from `OEngine`. Visible Phase 3d player/weapon changes require
host wiring and paired acceptance before the native producers are retired.


The host-wiring checkpoint is merged in
[PR #14](https://github.com/duduke/AfterBurner2-Recomp/pull/14)
(implementation `363bce4`, merge `a8849a4`). It adds `OMainHost` and executes
verified handlers through the original dispatcher in 480/480 matching directed MAME windows,
with 53,404 verified callbacks and no input-race skips. `$9D34` indicator
setup and direct `$B628` request emission have 128/128 isolated entry checks.
The fixture masks IRQs with a real CPU instruction and replays explicitly
unported caller outputs as external inputs. It isolates composition; natural
host cadence, IRQ interleaving, sound delivery and paired player/weapon/video
acceptance remain pending. `OEngine`, native Stage 1 rules and its adapter
remain in use; Phase 3d is incomplete.


The conditional natural IRQ and paired acceptance checkpoint adds 120/120
identical early dispatcher prefixes (324 IRQ2 services) and 116/123 later
prefixes (381 IRQ2 services in accepted windows, seven sub-input races).
At that checkpoint, all seven IRQ4-overlapped prefixes were raced and unverified;
the continuation below supersedes these exclusions. Composed
VBLANK matches 160/246 windows with 86 actual read-input races skipped.
Computed player/weapon requests and road targets feed the sub sprite port;
both hardware lists/draw triggers and RGB match 226/240 paired frames
(113 normal, 113 interpolated; 14 raced-producer frames skipped).
A continuous ring matches 1,259/1,259 sound calls and 256 latch writes with
zero external ring writes. Eight mutations fail. `$B676` now executes in the
host router, including its ready A0 contract and named wait/yield boundary.

These checks supply captured IRQ frontiers/registers, ADC results, unported
callers, sub size/blit/mutex operands and hidden renderer feedback. Sound
caller timestamps and road/tile/palette compositor inputs remain supplied.
They establish conditional composition, not independently scheduled gameplay
or waveform equality. Native `OLevelObjs`, controls, adapter and sound cues
remain active in `OEngine`; Phase 3d is incomplete. The next work is independent CPU/caller timing,
road-raster pairing and remaining stage/credit boundaries. Detailed evidence
and commands are in `ENGINE_NOTES.md`.


IRQ4/shared-memory continuation: complete natural dispatchers now include
publication waits and match 120/120 separately and 122/122 in the paired run,
zero differences/skips/excluded dispatchers. The paired run executes 490 IRQ2,
122 IRQ4 and seven IRQ6 services. Shared road/size operands are captured with
sub-write lane provenance; main-produced lanes must match. VBLANK matches
244/244 windows and both sprite lists/draw triggers/RGB match 240/240 frames,
zero skips. The continuous ring matches 1,250/1,250 calls and 255 latch writes.
These checks still supply IRQ frontiers/contexts, ADC results, unknown callers,
sub operands and hidden renderer feedback. Publication progresses by executing
captured interrupt boundaries. Independent CPU scheduling, the road raster
pair and remaining game-flow callers are unverified. `OEngine` continues to
use native Stage 1 controls, objects, adapter and sound cues.


The bounded IRQ4 timing checkpoint adds the actual interrupt wrapper,
selected small callees and both sprite blits with predicted CPU bus phases,
plus an uninterrupted `$14856` wait/re-arm oracle. Road-palette
`$13BB6/$13BCC/$13BFA` and palette-cycle `$1329E` now also predict their
operand/return phases; the latter has directed coverage only. The remaining
palette/reload/HUD/scenery/transition graphs now compute their own cycles,
registers and bus accesses too: 18,769/18,769 calls to those six entries match
within 3,519/3,519 IRQ4 envelopes, with 8,099,444/8,099,444 predicted bus events
and zero skips. Game-flow caller state/durations, sub-owned write frontiers,
physical inputs, sub-produced blit operands and interrupted caller contexts
remain external. No independent IRQ/caller/sub scheduler has been established,
and these timing ports are not wired into `OEngine`. Native Stage 1 controls,
objects, request adapter and sound cues remain active; Phase 3d is incomplete.

The bounded caller continuation now checks `$1458A..$145B8`,
`$14856..$14878` and `$B676..$B696` one instruction at a time, including
prefetch/operand phases, CCR, consecutive caller state and IRQ2/IRQ4/IRQ6
exception entry. Directed IRQ4 covers every re-arm instruction. Physical IPL
visibility, initial contexts, external handler/ISR resumptions and sub progress
remain supplied; this does not independently schedule the host. In particular,
MAME can deliver a pending scanline edge after a CPU's reported bus timestamp.
The exact counts and limits are in `ENGINE_NOTES.md`. These ports remain
outside `OEngine`; the native Stage 1 producers are still active.


The physical visibility continuation generates IPL at CPU bus-phase and
instruction-completion yields, including scanline timers deferred across
internal clocks. A bounded caller scheduler carries computed registers,
SR/SP, PC, time and selected IRQ into exception entry. Omitted re-arm and
publication busy spins execute from computed state and compare every bus
phase. Captured IPL is an assertion rather than a poll input; ADC results
are also checked against the board clock. Initial contexts, external handler/
ISR returns, unported main execution frontiers and sub progress remain supplied.
This does not establish the independently scheduled paired gameplay host.
Native Stage 1 controls, objects, request adapter and sound cues remain active.
Exact counts, mutation evidence and limits are in `ENGINE_NOTES.md`.

The caller/IRQ2/IRQ6 resumption checkpoint in merged
[PR #22](https://github.com/duduke/AfterBurner2-Recomp/pull/22) carries computed bounded caller
entry through the handler and RTE, including retained USP restoration, and
continues retained caller instructions and wait sampling gaps from that result.
Shared RAM and preceding ADC results remain supplied producer inputs. IRQ4,
unported returned instructions and back-to-back exception entry remain explicit
execution boundaries. This scheduler check does not enable live `OEngine`
flight, weapon, reload or sound producer cadence; native Stage 1 remains active.

The conditional road-pairing continuation now sends main target write-frontier
outputs through `OSubRoad` and renders its computed raster: 240/240 road passes
and RGB frames match MAME, zero skips. Target provenance retains 313 verified
handler writes, 405 unported caller writes and 482 initial values. Sequence
initializer/prepare/hide and RNG entries now route through `OMainHost`. These
checks retain caller/sub timing, road-control and tile/palette boundaries;
they do not enable independently scheduled player/weapon/enemy host execution
or remove native Stage 1 producers. The runtime template inventory identifies
seven unported initial handlers; formation/spawn and stage-flow remain explicit
boundaries.


The IRQ4 return continuation now retains computed SSP/USP, restored-mask
selection and immediate exception entry. The six instruction PCs behind the
original twelve unknown post-RTE boundaries are ported and independently
checked (49,973 matching instruction executions). That caller corpus
exposed fourteen returns at `$8AF8/$B62E`, plus explicit full-handler
and game-flow boundaries. Sub road/sprite write timing and publication progress
remain supplied; native Stage 1 has not been replaced by an independent host.

The next returned-word checkpoint covers all fourteen `$8AF8/$B62E` boundaries
from that corpus: 14/14 exact computed resumptions and 47,839/47,839 instruction
executions match, zero missing/skipped. Enlarged coverage retains ten successor
returns at `$8AFE/$B632`. Full handler/callee timing, publication progress and
independent sub execution remain pending; native Stage 1 is still active.

The successor checkpoint covers all ten `$8AFE/$B632` CALL6 returns:
10/10 exact computed resumptions, 47,839/47,839 natural instruction executions
and 22 taken BGE fixtures match, zero skips. That expansion exposed seven
returns at `$8B02/$B636`, covered by the following continuation. Other
handler/callee and publication/sub timing boundaries remain. Native Stage 1 remains active.


The post-RTE closure continuation ports 97 instruction PCs through world
projection/release and publication, closing those seven returns and all 31
further returns exposed while extending the graph. Natural 600-frame and
directed 120-frame checks match 2,694,300/2,694,300 instruction/entry records
and 7,910,253/7,910,253 caller bus phases, zero skips. Both corpora require
zero unported post-RTE returns. Late ALU state comes from real MAME instruction
snapshots, and native timing includes post-prefetch IRQ selection and RAM
return prefetches. Other handler/callee graphs, IRQ4 game-flow callbacks,
shared/sub producers and independent publication/sub cadence remain supplied;
the 16,413 natural graph exits are explicit boundaries outside this component.
This does not replace native Stage 1 or complete Phase 3d host scheduling.

The timed `$B84C..$B884` sound-ring producer now has a bounded instruction
check for enqueue, reset, suppression, index wrap and pending-byte overflow.
Its caller entries remain supplied, so this adds no independently scheduled
sound cadence to `OEngine`. The directed `$D0DA` counter return is computed;
other game-flow/physical input/sub frontiers remain explicit. Native Stage 1
and its sound cues remain active.

The timed `$14976/$B0AC` input graph now computes bounded button, bank,
pitch and throttle target execution. Natural samples come from the production
ADC timeline and mux destination; button reads use physical `OMainIO` inputs.
Caller cadence, ADC transaction/store times, shared inputs and sub progress
remain supplied. Directed raw-axis and accumulator fixtures are labelled;
six debug instructions disabled by ROM word `$B0AA` remain unverified. This
checkpoint does not replace the native Stage 1 input/flight host.

The timed live-flight/projection `$98C0..$9C32` graph now computes bounded
player state, math-chip results, attachment flips, history/projection writes
and request appends. Directed coverage includes all 232 PCs and five extra
returned-instruction continuations, with zero post-RTE holes. Target/caller
frontiers across unported graphs, sub inputs and publication cadence remain
supplied. This does not replace native Stage 1 flight or extend the paired
road/video claim to an independently scheduled whole host.

Timed shadow/exhaust companions `$9C34..$9D2A/$AE8C..$B0A8` now compute
bounded initialization, previous positions, scale/flip bits, lifetime,
runtime attachment tables, hardware shadow projection and request appends.
The natural combined capture matches 2,773,349/2,773,349 instruction/IRQ
records, zero skips and post-RTE holes. Caller entry/frontiers, sub progress
and publication cadence remain supplied; native Stage 1 flight is still active.

The bounded sub publication waits/release now compute eight instruction PCs,
including signed readiness conditions and main mutex waits. Their signal
ledger retains computed main writes and latched RMW reads; 600 natural frames
match 6,305,996/6,305,996 instructions and 15,895,267/15,895,267 bus phases.
Main write times, cross-CPU bus delivery order, sub IRQ/callee entries and
stack operands remain supplied. Native Stage 1 is still active, and neither
independent sub road/sprite scheduling nor whole-host gameplay is established.
