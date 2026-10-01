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
and `$009626`. Translate projection, formations and per-object state changes
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
foundations now have oracle checks. Phase 3d remains incomplete; resume at
[PHASE3D_CONTINUATION.md](PHASE3D_CONTINUATION.md). The dispatcher establishes
traversal/callback contracts only. Each actual handler still needs its complete
entry RAM, math-chip/I/O inputs and independent execution oracle before wiring.
