# Phase 3d — objects, input and flight

Phase 3c supplies shared arcade RAM, the main interrupt/caller handshake,
30 Hz object scheduling, ROM palette routines, request publication, the
sub CPU sprite transform and normal/interpolated hardware blits. This phase
replaces `OLevelObjs`, its temporary request adapter and native sound cues.
Credit/stage dispatch, scoring/lives flow and attract/service UI stay explicit
3e boundaries until their own ports are verified.

## 1. Object dispatcher and record layout

Walk the actual dispatch loop and initializer (`$009FC0..$00DF40` table,
254 records of 64 bytes; initializer `$01488E`). Enumerate handler pointers
from ROM at runtime. Capture each selected handler's entry registers, complete
object/shared RAM inputs, CPU-owned write sets, math-chip registers and I/O.
Handle active/free slots, compound attachments and handler transitions before
routing a player object through the new scheduler. Do not reinterpret the
native `OEntry` C++ struct as arcade memory.

## 2. Input, RNG and compound player

Connect the verified `$014626..$01465A` ADC sampler at its actual caller and
conversion boundary. Verify raw ADC lanes, button edges and the mapping into
arcade input bytes with neutral, both endpoints, release, throttle and simultaneous
button captures. Map and port the original RNG before any dependent handler.

Translate player `$016892` and `$01653A..$01657A`, including fuselage, exhaust
and weapon-pod attachments. Keep attitude/scale math on the real math chips;
compare the bank/pitch/altitude outputs consumed by `OSubRoad`, as well as
request records and the per-object scaled sizes at shared `$7800`. Remove
native flight and the coordinate/depth/history adapter only after those
comparisons pass. Preserve 30 Hz logic and the sub CPU's two-frame handshake.

## 3. Weapons and missile/HUD/audio boundary

Port `$0154B8..$015638` and the currently explicit `$016C8A` interrupt hook.
Capture held/released fire, launch sides, cooldown/reload, exhaustion, targeting,
projectile removal and attachment changes. Compare actual sound-latch command
order/timing, HUD writes and sprite requests. Retire native weapon rules and
hand-picked sound events as these ports replace them.

## 4. Enemy handlers, projection and collisions

Map the remaining handler table, starting with `$0088C2`, `$008AF2`, `$0091C0`
and their runtime transitions (`$009626` is player recovery). Translate
projection and per-object state changes
using runtime ROM tables and the verified RNG. Stage/spawn dispatch remains
3e; use clearly labelled directed object fixtures to exercise handlers whose
natural spawn conditions are not ported yet.

Trace collision callers and their use of scaled sizes before translating
damage, explosions, object deletion and life-loss requests. Compare complete
write sets and emitted commands. Leave unported score/lives/stage transitions
as named 3e callbacks rather than native substitutes.

## Acceptance and delivery

For every routine, compare MAME before-state replay against its own RAM writes
and I/O, report N/M identical with input-race skips, prove a deliberate constant
mutation fails, and list unexercised paths. Include both natural gameplay and
directed endpoint/transition captures; neither substitutes for a full credit.

Add a paired 30/60 Hz player/weapon check covering ADC input, object records,
road targets, both sprite lists, draw/latch events and resulting video. Re-run
`make test`, `make verify-stage1`, `make oracle-test` and a headless launch.
Update `ROADMAP.md`, `ENGINE_NOTES.md` and the fidelity notes, remove replaced
native code, then commit and deliver a separate PR. Whole-credit RAM/video and
waveform equality remain 3f/audio acceptance work.

Dispatcher/initializer, RNG, sequence compound setup/hiding and sound-ring
foundations and the remaining independent handler graphs now have oracle
checks. The comparator IRQ caller now has a combined ADC/timer/sound oracle
and physical endpoint/button mapping checks. Independently scheduled host
ADC/IRQ, sound producers' cadence and paired player/weapon acceptance remain
pending. Phase 3d remains incomplete; resume at
[PHASE3D_CONTINUATION.md](PHASE3D_CONTINUATION.md). The dispatcher establishes
traversal/callback contracts only. Each actual handler still needs its complete
entry RAM, math-chip/I/O inputs and independent execution oracle before wiring.

The user authorized merging the remaining handler-graph checkpoint,
[PR #11](https://github.com/duduke/AfterBurner2-Recomp/pull/11), before host
acceptance. Continue that acceptance in a separate PR; do not retire the
native object adapter or mark Phase 3d complete based on this checkpoint.

The board timing continuation adds an independently generated scanline,
EXCK/VBLANK and ADC timeline, with 669,845/669,845 MAME probe events identical
and three mutations rejected. CPU bus timestamps are still external inputs;
Full main-program CPU execution, gameplay wiring and paired acceptance
remain pending; the bounded service extension follows below. See the
continuation plan's board-timeline entry before changing `OEngine`.

The CPU service timing extension now checks IRQ2/IRQ6 exception entry,
E-clock IACK, the sampler/consumer and stack/return bus phases with predicted
CPU timestamps: 9,439/9,439 calls and 474,024/474,024 bus events identical,
0 skipped. Seven mutations fail. The unported main instruction's final bus
boundary and entry context remain external inputs; this is not full CPU
execution or gameplay wiring. Complete paired acceptance before retirement.


The host-composition checkpoint is merged in
[PR #14](https://github.com/duduke/AfterBurner2-Recomp/pull/14)
(implementation `363bce4`, merge `a8849a4`). Continue from updated `main`
on a new `codex/` branch. Host object composition now has a common verified
entry router and a directed IPL7 dispatcher fixture on live gameplay records. Its 480/480 windows match,
executing 53,404 verified callbacks, with 128/128 isolated `$9D34/$B628` entry
calls and four mutations rejected. Explicitly unported caller transcripts
remain inputs; masked fixture traversal does not verify natural host cadence,
IRQ interleaving or paired video/sound acceptance. Continue the remaining
host boundaries in `PHASE3D_CONTINUATION.md`; keep Phase 3d incomplete.
