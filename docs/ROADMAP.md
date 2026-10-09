# Roadmap

Phased plan, mirroring the order Cannonball's own architecture implies:
data/hardware layer first (since it's verifiable against MAME immediately),
then the game logic that drives it (which takes much longer and benefits
from having a working hardware layer to render into while porting).

## Working on the remaining tasks (read this first)

These instructions are for anyone picking up the next task, including AI
assistants. The goal is gameplay **identical** to the arcade ROM, so the
bar is "matches MAME byte-for-byte", not "looks right".

**Where things are**

- [`docs/HARDWARE.md`](HARDWARE.md): the X-Board hardware, from MAME's
  `segaxbd.cpp` and the `segaic16` chips.
- [`docs/ENGINE_NOTES.md`](ENGINE_NOTES.md): what's known about the 68000
  game code (addresses, RAM layout, routines), and the write-up of each
  verified port. Read "Oracle testing", "Sub CPU road generator" and
  "Video oracle" before starting.
- `src/main/engine/subbus.*`, `osubsprites.*`, `osubroad.*`: the
  reference examples of a verified port. Copy their structure.
- `tests/oracle_*.cpp` and `tools/oracle/*.lua`: the matching tests and
  MAME capture scripts. Copy these too.
- `docs/STAGE1_FIDELITY.md`: which parts of the current game are
  reconstructions waiting to be replaced.

**Rules**

1. **Never invent behaviour.** Every number, table, timing and branch
   comes from the ROM disassembly. If something isn't understood yet, leave
   it unported and say so; don't fill the gap with a plausible guess. The
   existing Stage 1 reconstruction (`olevelobjs`, `oroad`, `osprites`,
   `draw_stage1`) is what guessing produces, and it all has to be replaced.
2. **Translate instruction by instruction**, keeping the arcade's memory
   layout: the ported code reads and writes the same addresses through a
   bus object (`SubBus`, and `MainBus` once it exists). Comment each block
   with the ROM address it came from. Refactoring into readable code comes
   later, once a test pins the behaviour.
3. **Every port gets an oracle test before it's wired into the game.** A
   Lua script captures the state before and the writes after the routine
   in MAME, and a C++ test replays the "before" state through the port and
   compares. Report results as "N of M identical, K skipped (reason)".
4. **Prove the test can fail.** Change one constant in the port, check the
   test fails, then revert. Check coverage too: a path the capture never
   exercised is unverified, and must be listed as such in `ENGINE_NOTES.md`.
5. **Never commit ROM data or captures.** ROMs live in `roms/`, captures in
   `build/oracle/`; both are gitignored. Don't paste ROM bytes or large
   tables into source files either: read them from the ROM at runtime.
6. **Keep the docs current**: tick roadmap items off with their results,
   and add what you learned to `ENGINE_NOTES.md`.
7. **Before committing**, run `make test`, `make verify-stage1` and
   `make oracle-test`, and confirm nothing got worse.

**Commands** (from the repository root)

```bash
make -j4                    # build the game: ./afterburner2
make test                   # ROM-free engine checks
make verify-stage1          # Stage 1 integration suite (needs roms/)
make oracle-capture         # record MAME reference captures into build/oracle/
make oracle-test            # check the ports against those captures

# Disassemble: build a flat CPU image, then a linear listing of a range
python3 tools/build_rom_image.py maincpu "$TMPDIR/main.bin"    # or subcpu
python3 tools/m68k_disasm.py "$TMPDIR/main.bin" 144A2 14820
```

`make oracle-capture` needs a MAME binary and the `aburner2` ROM set: set
`MAME=` and `ROMPATH=` (defaults are in `tools/oracle/capture.sh`). The
scripts were written for MAME 0.287. The disassembler is linear and
deliberately incomplete: "unknown opcode" on a real execution path means
the decoder needs extending; in a data table it's expected.

**Oracle lessons learned (avoid repeating them)**

- Compare **write sets**, not whole RAM snapshots: log only the CPU under
  test's own writes (filter on `CURPC`), so the other CPU's concurrent
  writes can't cause false failures. A record is a race, and is skipped,
  only if the other CPU changed a byte the port actually read.
- Exclude stack areas: exception frames are pushed while the PC still
  points at the interrupted instruction, so no PC filter catches them.
- Keep Lua tap handles in global variables, or they get garbage-collected
  and silently stop firing. `screen:vpos()` isn't available; derive scan
  lines from `manager.machine.time` (64 us per line).
- In a frame notifier, `screen:pixels()` is the image drawn from the state
  at the *previous* notifier, converted through the *current* palette.
- For IRQ mask fixtures, execute a real SR-writing instruction. MAME's
  debugger SR setter does not update pending interrupt selection.
- Start each MAME run with fresh NVRAM and config (`capture.sh` does), or
  boot isn't deterministic.
- The game logic runs at 30 Hz with an interpolated sprite list on the
  in-between frames. Don't port anything at 60 Hz.

The bounded `z80dismblr` sound-ROM analysis pilot is complete: 158/158
observed instruction decodes match MAME, but trace import adds no code starts
in the 300-frame cold-boot window. Static graphs remain inspection aids;
no timing, interrupt or whole-host acceptance is advanced. See the tooling
pilot section at the end of `ENGINE_NOTES.md` for reproduction and exclusions.

**Current checkpoint and next task**: Physical main selection, observed main
producers/callees and sub reset startup now pass together in the 600-frame
joint corpus: 20,532,168 native records, 53,449,620 phases, zero differences/
skips, one initial context per CPU and zero later supplied sub phases.
Main supplies zero phases; sub supplies only the initial partial ISR's 46
phases at three PCs, with zero writes. Sound latch handshakes, sprite/road
bank swaps, sprite scaler feedback and partial screen frontiers also match.
Native Z80 progress now joins that contract: 10,634,678 sound bus records and
nanosecond timestamps, 819,024 computed sound grants, 172 NMI/RETN, and a
separate 2-entry MAME IRQ/RETI fixture all match. Sound state is never reseeded;
45 pre-window main boot writes remain external inputs. Native audio/device
head deadlines and IRQ/RESET boost owners now match in the joint window,
along with 314,376 PCM and 628,796 FM raw stereo samples and independently
computed batch timestamps/indexes. Thirteen owner mutations reject and
the complete required suite passes. Next extend unverified main/sub/sound
paths and cold-boot edges,
remaining whole-game/video integration. Mixed/resampled speaker output and
Stage 1 audio wiring now pass exact MAME comparisons; see the audio completion
checkpoint below. Tile/text
latches and partial RGB now match MAME in the joint natural/directed corpora;
see the current video acceptance result in ENGINE_NOTES.
Directed visible-area display, pending-global, scroll-table and effect-pen
coverage now passes; initial boundaries and unobserved states remain explicit.
Stage 1 remains the game loop. Earlier checkpoints below are historical; see the current
result in `PHASE3D_PLAN.md` and the last `ENGINE_NOTES.md` section.
Follow the [ordered remaining acceptance tasks](PHASE3D_PLAN.md#next-tasks-for-whole-host-acceptance).

**Picking the next task**: Phase 3c is complete; 3d foundations are verified,
but 3d is not complete. Input, live-player, projectile and selected enemy
handlers now have independent checks. The directed host-composition checkpoint
PR #14 is merged. Natural IRQ2 interleaving and conditional paired
player/weapon/video/sound acceptance now pass at supplied caller boundaries.
IRQ4/shared-input composition and publication waits now pass without race
skips. Bounded IRQ4 wrapper/selected-callee and uninterrupted re-arm timing
now pass; the selected IRQ4 callee graphs now compute their own timing.
Bounded re-arm/dispatcher/publication instruction phases and IRQ entry now
pass, including IRQ4 at every re-arm instruction. Physical IPL visibility and bounded caller scheduling now compute bus-phase
delivery and instruction completion tails; retained busy-spin gaps execute
from computed CPU state. PR #22 is merged: bounded IRQ2/IRQ6 returns now carry
computed CPU state through RTE into caller execution. Immediate post-RTE selection/entry is now computed from restored SR and
physical IPL. IRQ4 RTE/USP and immediate entry now pass 714/714 services;
the six PCs behind all twelve original returned-instruction boundaries are
ported. The fourteen subsequent returns at `$8AF8/$B62E` now pass; the ten successors at `$8AFE/$B632` also pass. The seven further returns at
`$8B02/$B636` and 31 subsequently exposed returns are now closed through the
world projection/release and publication producer graphs. Both the natural
600-frame and directed 120-frame corpora have zero unported post-RTE returns.
IRQ4 `$14990/$E2DA` now compute their single-instruction returns, with
713/713 calls per entry identical and zero skips. IRQ4 `$17146/$C6F8` now
compute their callee graphs, with 4,830/4,830 envelopes and
9,567,126/9,567,126 bus events identical, zero skips and no callee transcripts.
Other handler graphs, physical inputs and sub-write frontiers remain supplied. PR #21 is merged with oracle-checked production
ADC/mux, button and sound-latch endpoints; the fixed frame IRQ loop and
unverified input/reload calls were removed. Resume at the supplied boundaries and independent
host scheduling; native Stage 1 remains active. The caller oracle now takes
ADC results from the continuous production `OMainIO` timeline rather than
expected read bytes; hardware transactions/frontiers outside the bounded
caller remain supplied. Conditional player/weapon-to-road pairing now
computes displayed rasters and passes 240/240 RGB frames; per-target write
provenance distinguishes verified producers, unported callers and initial
values. Sequence initializer/prepare/hide and RNG now route through `OMainHost`.
The bounded caller now joins IRQ2/4/6 and closes `$C082/$C098`, world
initialization, sprite clears and the newly exposed `$AAEE` trail graph.
The subsequent sound/input checkpoints match 2,727,071/2,727,071 natural
instruction/IRQ records, zero differences/skips and zero unported post-RTE
boundaries. Timed `$14976/$B0AC` now consumes production ADC samples and physical
buttons at supplied frontiers. The combined capture reports 8,237 external
exit events (the earlier inventory had 8,024); these are boundary occurrences,
not unique instructions remaining. Supplied caller/sub/video producer frontiers
still prevent whole-host scheduling closure. Timed live flight/projection
`$98C0..$9C32` and its append successors now match 2,763,360/2,763,360 natural
instruction/IRQ records. Directed gates cover all 232 flight PCs plus five
exposed return instructions; natural exit events fall to 7,809. Next extend
caller/sub publication progress before whole-host paired road/player acceptance.
Timed shadow/exhaust `$9C34..$9D2A/$AE8C..$B0A8` adds 179 bounded
instruction PCs. The combined natural capture matches 2,773,349/2,773,349
records, with zero skips and post-RTE holes; external exit events fall to
6,693. Directed gates cover all 179 PCs, all 17 conditional branches and
the additional `$77AC` RTS; seven mutations are rejected. See the companion
continuation in `ENGINE_NOTES.md` for coverage and limits.
The sub publication waits/release now compute eight instruction PCs and
retain computed main handshake values. The natural 600-frame sub replay
matches 6,305,996/6,305,996 instructions and 15,895,267/15,895,267 phases,
with zero different/skipped accepted records. Cross-CPU delivery order,
main writer times and sub IRQ/callee frontiers remain inputs. The eight
publication caller instructions now compute BSR return stacks, request-word
shifts and carry branches, retaining their produced lanes across supplied
callee gaps. Independently timed bounded sprite/road progress now adds
1,136 literal PCs: all are covered by directed MAME checks. Natural 600-frame
execution matches 11,896,153/11,896,153 instructions and
27,415,741/27,415,741 bus phases; seven mutations are rejected. The subsequent
sub IRQ checkpoint computes entry, selection, ISR, RTE and retained resumption:
600-frame checks match 12,072,480 instructions/entries and 28,083,147 phases.
The arbiter matches 239,177 grants and 1,271,730 shared reads at labelled
producer/completion frontiers; nine sub and four arbiter mutations fail.
A separate cold-boot bus gate adds 33,234 grants, 418,054 natural shared reads,
64 labelled read-only mirror probes, both byte lanes and RAM retention across
six tagged sub reset entries; eight boot/shared mutations fail. Native boot
CPU execution and device initialization remain open.
Connect both bounded CPU streams under this policy and remove remaining
producer/caller/video/sound frontiers before whole-host acceptance.
See the sprite/road continuation in `ENGINE_NOTES.md` for branch limits;
whole-host acceptance remains pending.
The timed weapon controller/fire/allocation caller checkpoint adds 107 literal
PCs at `$9D34..$9DEA/$9DF8..$9F06`. Natural 600-frame execution matches
2,783,882/2,783,882 instruction/IRQ records and 8,210,437/8,210,437 bus events;
directed 240-frame acceptance covers every PC, both directions of all nineteen
conditional branches and the reserve/ammo/launch/append endpoints. Both have
zero differences/skips and zero unported post-RTE returns; three mutations
are rejected. Natural exit occurrences fall to 5,403. Pool-reset/HUD callee
execution and activated projectile continuations remain timed boundaries,
alongside main/sub producer scheduling and renderer ownership. Section 2
item 1 remains incomplete; see the timed weapon checkpoint in `ENGINE_NOTES.md`.
The flight/HUD/launch-sound caller continuation now adds 258 literal PCs for
`$9810/$9626/$964C`, `$CFC4/$D062` and `$9F7C`, retaining actual dispatcher,
allocator and sound-ring calls. Natural 600-frame execution matches
2,797,594/2,797,594 instruction/IRQ records and 8,242,928/8,242,928 bus events,
with zero unported post-RTE returns and 5,280 labelled external exits.
Directed coverage reaches every PC and both
directions of 21 conditionals/four DBFs; all comparisons have zero differences
or skips. The longer directed run exposes supplied post-RTE continuations at
`$6844/$14EE8`, outside these graphs. Remaining projectile/sequence/pool and
independent two-CPU/video/sound acceptance boundaries remain explicit;
native Stage 1 and section 2 item 1 remain unchanged. See the continuation in
`ENGINE_NOTES.md` for natural results and mutation evidence.
The two directed `$6844/$14EE8` post-RTE instructions now compute their bit
checks: directed host acceptance matches 2,055,260 instruction/IRQ records,
6,036,146 phases and all 260 host PCs, zero differences/skips or unported
post-RTE returns. Its strict closure gate is restored. Joint main/sub
publication progress now runs the native ports against one shared RAM instance
under the checked arbiter: 1,788,368 instructions, 4,818,916 phases and
182,481 bounded grant completions match; 327 producer writes are computed.
435 entry contexts, 141,969 other shared writers, device deadlines/boosts,
IRQ services and sub callee progress remain inputs. Five new mutations fail.
Next extend this joint stream through IRQ services and sprite/road/main caller
progress while retaining the 68000's private instruction-step debt. Whole-host
player/weapon/video/sound acceptance remains pending; native Stage 1 stays active.
See the joint publication continuation in `ENGINE_NOTES.md` for coverage limits.
The joint stream now executes the bounded sprite/road callees through
`OSubProgress`, retaining private internal-tail debt before each successor.
Natural 120 matches 2,420,931 instructions, 6,110,061 phases and 242,205
bounded grant completions, zero differences/skips. It computes 102,479 shared
writes; 39,817 other writers and 347 entry contexts remain supplied. Natural
joint coverage reaches 631/1,136 sprite/road PCs; directed standalone coverage
does not close the other joint paths. Next extend joint IRQ entry/service/RTE
and main caller/producer progress, then remove ADC/device/video/sound
frontiers before whole-host acceptance. Native Stage 1 remains active.
See the joint sprite/road continuation in `ENGINE_NOTES.md`.
The joint stream now computes sub IRQ selection, entry, VPA acknowledgement,
all thirteen service instructions and RTE into retained caller state. Natural
120 matches 2,491,840 instruction/IRQ records and 6,309,809 bus phases, with
88/88 entries/acknowledgements/restorations and zero differences/skips. Main
`$1458A..$145B8` now computes its twelve dispatcher instructions in the same
grants. There is one initial sub context; 4,630 main contexts and 36,902
shared writers remain supplied. Next join main IRQ/caller/producer progress
and device/video/sound ownership before whole-host acceptance. The initial
partial sub ISR and final parked user continuation remain labelled; native
Stage 1 remains active. See the joint IRQ/dispatcher continuation in
`ENGINE_NOTES.md`.
The runtime template inventory still identifies seven unported initial handlers.
The expanded joint main checkpoint now covers 679/1,429 main caller PCs and
92/103 added IRQ RAM-wrapper PCs. Natural 120 matches 3,045,337 records and
7,960,655 phases, computing 141,627 shared writes with 669 still supplied.
Main entry/device/RTE and unported callee gaps remain explicit. This corpus
precedes coin/start. Synchronized timer acknowledgements now pass, and the
fresh 600-frame live-gameplay replay closes the natural `$14840` RESET failure:
19,049,961 records, 49,054,764 native phases and 2,257,946 bounded grants match,
zero skips, including 568 retained sub IRQ services/returns. It covers
1,118/1,432 main PCs and 658/1,136 sub PCs. Main supplied execution still has
4,391,908 phases at 4,677 PCs; reset startup/TRAP also remains supplied.
Main IRQ entry/VPA/RTE now also passes the fresh 120-frame joint replay:
448 entries, acknowledges and returns, 432 retained entries/resumptions and
7,967,919 native phases, zero skips. The new 600-frame gameplay extension
passes 49,092,373 native phases and 2,893 main entries/acknowledges/RTEs,
including natural RESET; 4,357,192 main phases at 4,507 PCs remain supplied.
The physical main selection and producer/callee continuation now has a fresh
120-frame proof: all 448 entries/services/returns/resumptions are retained,
3,160,862 native records and 8,320,519 phases match, with zero supplied main
phases or later main context replacements. The 600-frame gameplay proof now
passes 20,532,151 native records and 53,449,565 phases, zero differences/
skips, zero supplied main phases and one initial main context. All 2,893
main entries/services/returns/resumptions use physical selection and retained
state; TRAP #12 and RESET-handler RTE also pass. This closed the main CPU checkpoint. The subsequent sub/video/sound
checkpoint passes 53,449,620 phases, zero differences/skips, computes all
observed sub reset startup, and verifies sound latch acknowledgements,
video banks, scaler feedback and partial screen updates. Native sound/device
timing, tile/text latch and RGB ownership, and unverified paths remain open.
Whole-host acceptance remains open; native Stage 1 remains the game loop.
Follow the current host acceptance plan in
[PHASE3D_PLAN.md](PHASE3D_PLAN.md). Take the first unchecked item in "Remaining
Phase 3 work" below. The milestones are ordered so each one builds on the
previous, starting with 3a (the main CPU's bus, the 315-5250 chip and a
main CPU oracle capture).

## Phase 1 — Scaffold (done)

- [x] Project structure, build system, license/legal framing
- [x] Generic ROM chip loader (CRC32-verified, de-interleaving)
- [x] `roms/roms.txt` + `Roms`/ROM-region loader for the exact
      `aburner2` ROM set (region assignment sourced from MAME's
      `ROM_START( aburner2 )`, verified end-to-end against the real ROM zip)
- [x] `docs/HARDWARE.md` hardware reference distilled from `segaxbd.cpp`

## Phase 2 — Video hardware layer (`hwvideo`)

Faithful, from-scratch reimplementations of the custom chips, validated
against MAME's behavior (not its code) and against real hardware video
captures where possible:

- [x] Tilemap/text layer renderer (`hwtiles`) + palette (`hwpalette`),
      including per-column/per-row fine scroll and the ALT tilemap set
      (`docs/HARDWARE.md` section 3.3)
- [x] Whole video layer pixel-identical to MAME's rendered frames (see
      Phase 3, "Video hardware layer verified")
- [x] Background/ground generator (`hwroad`, X-Board's equivalent of
      OutRun's "road")
- [x] Sprite scaler (`hwsprites`; position/scale/priority per entry)
- [x] Final screen compositor (`screen.cpp`) — road/tilemap/sprite draw
      order and priority-buffer logic, verified end-to-end with a
      hand-built test scene rendering real decoded ROM tile graphics
      through the full pipeline (roms → hwtiles → hwpalette → screen →
      SDL) to a window
- [x] Sub CPU memory map (`segaxbd_state::sub_map`) transcribed into
      `docs/HARDWARE.md` — notably narrower than main's (no tilemap/
      sprite/palette/I-O access at all), pointing at a dedicated
      road/perspective math coprocessor role, confirmed in Phase 3 below

## Phase 3 — Game logic (`engine`)

The actual port of the 68000 game code — entity/object management, flight
physics and scaling math, enemy/target AI, stage sequencing, HUD, attract
mode, high scores. This is the bulk of the effort and will be broken down
module-by-module once the main CPU's code is mapped out (Cannonball's
`o*.cpp` file-per-subsystem convention is the intended model). Unlike
Phase 2, there's no MAME driver to read for this part — it has to be
disassembled and read directly.

- [ ] Achieve nearly identical gameplay to the original arcade game across
      every stage, including flight response, enemy formations and timing,
      weapons, collisions, scoring, stage transitions, and refueling.
- [x] Disassembly tooling (`tools/build_rom_image.py`,
      `tools/m68k_disasm.py`) — a small from-scratch M68000 disassembler,
      since installing one wasn't available in this environment
- [x] Main CPU vector table mapped (reset, VBLANK, compare/timer IRQ, both
      TRAP vectors that are actually used)
- [x] Reset routine walked through — hardware init, an NVRAM
      signature/first-boot check, and what looks like the game's core
      object/task dispatch loop
- [x] VBLANK handler (the per-frame "engine tick") walked through — see
      `docs/ENGINE_NOTES.md` for the full breakdown (30/60Hz split,
      lamp updates, sprite draw-trigger timing, several not-yet-identified
      subsystem calls)
- [x] Sub CPU vector table, reset routine, and VBLANK handler mapped —
      confirms it's a dedicated road/perspective math coprocessor (no
      access to tilemap/sprite/palette/I-O hardware at all), with a
      counting-sort-then-drive-the-multiplier-chip routine that's the
      strongest evidence yet for that role
- [x] Main ↔ Sub handshake traced: Main CPU writes `$55555555` to
      `subram0[$70]` at `$0138F4` in `$01381E`, releasing Sub CPU from its
      reset spin-loop at `$000420`
- [x] Main ↔ Sub mutex mechanisms mapped:
      - `subram0[$D6]` bit 0: frame-update mutex (Main CPU VBLANK sets at
        `$014674`, clears at `$014798`; Sub CPU math engine `$000640` stalls
        during frame update)
      - `subram0[$00]` bit 0: pause mutex (Main CPU sets at `$0145DA` upon
        Pause switch input; Sub CPU VBLANK `$00055E` stalls while paused)
- [x] Sprite pipeline mapped end-to-end:
      - Main CPU objects emit 16-byte descriptors to unsorted buffer via
        `append_sprite` (`$00B628`)
      - Main CPU VBLANK swaps 4 double-buffer pointer pairs (`$2A3C04/0C/14/1C`)
      - Sub CPU counting sort (`$000640`) sorts by Z-distance into bucket lists,
        computes perspective with 315-5248 multiplier, and outputs sorted buffer
      - Main CPU blits sorted buffer to hardware Sprite RAM (`$00100000`) and
        triggers hardware scaler (`$00110001`)
- [x] Road / ground-scroll pipeline mapped end-to-end:
      - Main CPU sets stage (`subram0[$9C]`) and flight attitude (`subram0[$CE]/[$AE]-[$B6]`)
      - Sub CPU computes horizon scanline offset (`$000F70`), generates 224-line
        per-scanline raster curves (`$0EC200`/`$0EC400` at `$000F4C`), and
        flips hardware road RAM double-buffer at `$000EE001` (`$000464`)
- [x] Mapped VBLANK subsystem hooks:
      - `$016C8A`: missile reload / audio queue / HUD icon sequencer
      - `$018444`: stage events (refueling stages 5/13/20) and background
        scenery streaming to Tile RAM + roll-adjusted horizontal scroll register
- [x] Object/task system mapped (`$9FC0`–`$DF40`):
      - 254 entries × 64 bytes
      - Compound player jet (slots 0..3: fuselage, twin exhaust flames, weapon pod)
      - Player missile/vulcan projectiles
      - 3D enemy world-to-screen pipeline using SEGA 315-5248 hardware multiplier
- [x] Oracle harness: MAME runs the real code headless with Lua memory taps,
      and differential tests diff ported routines against its snapshots
      byte-for-byte (`tools/oracle/`, `make oracle-capture` /
      `make oracle-test`; see `docs/ENGINE_NOTES.md`, "Oracle testing")
- [x] Sub CPU sprite pass ported (`engine/osubsprites`, sub ROM
      `$000648`-`$0009E2`): byte-identical to the arcade on 1,199 of 1,200
      attract-mode passes. Three rare paths are not yet exercised by a
      capture. Not wired into the game yet — it needs real requests from
      ported main CPU object code.
- [x] Stage 1 reference experience (`olevelobjs`, `oroad`, `osprites`,
      `ohud`, `oengine`, and `Screen::draw_stage1`): real ROM art,
      palettes, HUD tiles and sound, but **native reconstructions, not
      ports** — invented spawns, trajectories, hitboxes, scoring and stage
      length; about 72 object slots used and a single-object player; `oroad`
      output is never displayed because the Stage 1 view replaces the road
      hardware path. Each piece is to be replaced by a verified port (see
      `STAGE1_FIDELITY.md`).
- [x] Sub CPU road generator ported (`engine/osubroad`, sub ROM
      `$00046A`-`$00052C`: bank/pitch easing, rotation, horizon, and the
      per-scanline raster that banks the ground): writes exactly the
      arcade's bytes on 1,798 of 1,800 attract-mode frames. The special
      sequence (`$0016D4`/`$001758`) is now ported and separately verified
      in 3b below.
- [x] Video hardware layer verified against MAME's rendered frames
      (`tests/oracle_video.cpp`): 300 of 300 attract-mode frames, and 100 of
      100 with sprites removed, are pixel-identical. Fixed on the way:
      `hwtiles` layer scroll (`(0xC0 - scroll) & 0x3FF`), row/column
      fine-scroll and the ALT tilemap set, the road chip's `$5F8` origin,
      and the sprite buffer swap.

### Remaining Phase 3 work, in order

Every port follows the same rule: translate the routine
instruction-by-instruction against the arcade memory layout, add an oracle
test that diffs it against MAME, and only then wire it into the game. Each
milestone ends with something visibly closer to the arcade in
`./afterburner2`.

**3a. Main CPU foundation**

- [x] `MainBus`, the main CPU's equivalent of `SubBus`: 22-bit map with
      work RAM, the shared sub RAM window, tile/text RAM, sprite RAM,
      palette, road control, I/O and the 315-5248/5249/5250 chips.
      Shared RAM and sub-side chips use the same `SubBus` instance;
      byte lanes, mirrors and buffer swaps have ROM-free checks.
      Road/scenery video endpoints are connected in 3b; sprite triggering and
      timed ADC scheduling remains for 3d.
- [x] 315-5250 compare/timer model: comparator, timer IRQ, and the
      main → sound command latch (offsets `$B`/`$F`); 2,438 of 2,438 MAME
      events identical, 0 skipped (see verified coverage in `ENGINE_NOTES.md`)
- [x] Main CPU write-set oracle capture (the `capture_subroad.lua`
      pattern, keyed on main CPU PCs), with races and stack writes
      handled the same way; ADC sampler `$014626`-`$01465A` matches 600 of
      600 attract records and 1,800 of 1,800 gameplay records, 0 skipped
- [x] Gameplay captures: drive MAME's inputs from Lua (coin, start,
      stick, throttle, buttons) on a fixed script so captures cover real
      play, not just attract mode. Main ADC, sub sprite/road, and video
      captures use `tools/oracle/gameplay.lua`; `make oracle-capture-main`
      regenerates the new captures, and `make oracle-test` checks them.

Phase 3a was merged in PR #4 (`559a2b7`). The main bus is now used by the
road/scenery/sprite ports; the frame loop is completed in 3c below, while
ADC scheduling awaits 3d.

**3b. Real road and scenery in the game (complete)**

- [x] Stage/road setup and palettes (`OStageSetup`): reset attitude/limits,
      runtime stage-selector table, palette descriptor lookup, both ground
      palette modes, master palette and scenery reset/road color expansion.
      168 of 168 setup calls identical, 0 skipped; includes separate directed
      alternate-palette and road-control-command captures.
- [x] Stage events and scenery scroll (`$018444`, `OScenery`), including
      `$018798` tile upload: 4,137 of 4,400 calls identical, 263 skipped for
      sub CPU input races. Regenerated captures use the active CPU stack.
- [x] 3b initially ran `OSubRoad::frame` once per host frame, after request processing
      and the preceding road RAM latch (3c corrects the caller to the ROM
      two-display-frame handshake). `MainBus` video endpoints send
      tiles/text/palettes and the latched road buffer to the hardware renderer.
      Request processing: 866 of 868 identical, 2 input races skipped;
      road initialization: 34 of 34 identical, 0 skipped.
- [x] Special road sequence `$0016D4`/`$001758`, including the comparator:
      600 of 600 directed MAME frames identical, 0 skipped. These are routine
      inputs, not proof of a complete carrier sequence.
- [x] Delete `draw_stage1`, `oroad`, generated sky/sea gradients, terrain
      sprites and the native speed-gauge overlay. The hardware compositor
      always draws road, tiles, text and sprites through its verified path.

The host still starts the native Stage 1 experience at raw selector `$10`;
credit/stage dispatch is 3e. Native player controls do not drive arcade road
attitude yet: that exact mapping is 3d, and the integration check exercises
bank targets directly through shared RAM. Objects, HUD and sound command timing remain reconstructed. Sprite and palette
scheduling are replaced in 3c below. See `ENGINE_NOTES.md` for boundaries,
coverage and mutation checks. `make test`, `make verify-stage1` and
`make oracle-test` pass; no ROM data or captures are committed.

**3c. Frame loop and sprite pipeline (complete)**

Implementation order and acceptance checks: [PHASE3C_PLAN.md](PHASE3C_PLAN.md).
Changes: [PR #6](https://github.com/duduke/AfterBurner2-Recomp/pull/6).

- [x] Port the main CPU VBLANK handler (`$014674`): the 30/60 Hz split,
      the double-buffer pointer swaps (`$2A3C04`-`$2A3C1C`), the
      frame-update mutex, and lamps; 180/240 scheduler records identical,
      60 concurrent sub-input races skipped. 3d/3e callees remain explicit hooks.
- [x] Port `append_sprite` (`$00B628`) and the sorted-list blit to sprite
      RAM (`$00B7C2`/`$00B7D2`), showing the interpolated `+$C00` list on
      alternate frames; 1,201/1,201 main sprite/setup records identical, 0 skipped.
- [x] Run `OSubSprites` in the game between the two, so every sprite on
      screen goes through the arcade's own sort and transform; 240/240 directed
      sub passes identical, covering skip-sort, spills and clipping. Paired
      output: 60 normal + 60 interpolated frames, RAM/draw/pixels identical.
- [x] Port the palette fade/flash (`$014A86`) and palette animation
      (`$013F92`) subsystems and their palette/small scheduler callees;
      2,200/2,200 records identical, 0 skipped. Native `OPalette` removed.
- [x] Run object logic at 30 Hz, as the arcade does (the reconstruction
      ran at 60 Hz). Native constants remain logic ticks: the temporary
      3,600-tick stage now lasts 7,200 display frames.

The sub CPU's `$000556` wait starts sprite preparation on a negative logic
counter; `$000452` delays road latch/generation until the following display
phase makes the counter zero. Normal gameplay captures advance by two display
frames per road call. This corrects 3b's host once-per-display scheduling and
the initial 3c plan. CPU instruction timing/interrupt stack emulation and the
3d/3e callees are outside the scheduler oracle's claim. The paired pipeline
oracle computes sub descriptors and main blits before comparing RAM and RGB;
it does not establish whole-credit equality.

All required Make checks and a headless launch pass. Four deliberate constant
mutations fail their targeted oracle checks. `OSprites` now only adapts native
producers to real ROM frame pointers and serialized requests; unsupported
native art/zoom tuples are omitted rather than given fabricated descriptors.
Full boundaries/coverage are in `ENGINE_NOTES.md`. Next: [PHASE3D_PLAN.md](PHASE3D_PLAN.md).

**3d. Objects and flight (retires `olevelobjs`)**

Implementation and continuation plan: [PHASE3D_PLAN.md](PHASE3D_PLAN.md).
Independent input, live-flight, compound-part, projectile, reload/HUD, world
effect and enemy-missile routine checks now pass. Host wiring, remaining
stage/credit caller boundaries and independent sound/input scheduling
remain incomplete; conditional paired acceptance is recorded below. [PR #11](https://github.com/duduke/AfterBurner2-Recomp/pull/11)
delivers the remaining independent handler graphs after PR #10's damage
companion checkpoint; the user authorized its merge before phase completion.
Resume host acceptance in a new PR using the continuation plan. See the per-capture results
and uncovered paths in `ENGINE_NOTES.md`.

[PR #12](https://github.com/duduke/AfterBurner2-Recomp/pull/12) adds the combined
comparator IRQ caller and physical input endpoint checks. The user authorized
its push and merge before phase completion; host scheduling and paired
player/weapon acceptance remain pending.

- [x] Object initializer and dispatch loop: 254 × 64-byte records from
      `$A000` to `$DF40` (preceded by the `$9FC0` sentinel), runtime templates,
      live active/handler reads and register contracts. Oracle-verified;
      not wired into the game while handlers remain unported.
- [ ] Input read: stick X/Y and throttle through the ADC, buttons, and
      their mapping to the arcade's input bytes. `$B0AC/$14976` and observable
      ADC timing pass routine checks. The complete `$1461C` IRQ caller now
      matches 7,058/7,058 calls and 45,176/45,176 continuous hardware events,
      0 skipped; physical axis endpoints and button edges have 720/720
      separate mapping checks. Physical endpoints are now installed through
      `OMainIO`; the bounded caller consumes computed conversions. Host CPU/IRQ
      scheduling and conversion-to-flight wiring remain.
- [x] Continuous production ADC producer at bounded caller services:
      `OMainIO` handles CPU bus delivery and ADC transaction time separately.
      1,656 computed ADC reads feed 1,673/1,673 IRQ2/IRQ6 services and
      74,059/74,059 bus events, zero skips; 20,387,341/20,387,341 physical
      observations pass. Two producer mutations fail. Physical inputs and
      unported hardware transaction/frontier times remain capture boundaries;
      this does not complete independently scheduled input wiring.
- [x] Production host I/O endpoints (`OMainIO`, PR #21): shared ADC/mux,
      active-low buttons and ordered sound-command buffering installed in
      `OEngine`. The same production callbacks pass 7,058/7,058 IRQ calls,
      45,176/45,176 hardware events and ordinary/directed ADC checks, zero
      skips; three endpoint mutations fail. Native ticks do not schedule these
      ports. Independent IRQ/input/reload cadence and paired host acceptance
      remain pending; see the PR #21 continuation in `ENGINE_NOTES.md`.
- [x] Comparator IRQ caller: acknowledgement, six-channel sampler and sound
      consumer checked together against MAME, including 946 sound-latch writes
      across ordinary and endpoint runs. All three mutations fail. Bus timestamps
      are supplied by the capture; independent host scheduling remains unverified.
- [x] Physical board timeline: independently generated scanline/EXCK/VBLANK
      and ADC deadlines match 669,845/669,845 ordinary, endpoint and directed
      MAME probe events, 0 skipped; three mutations fail. CPU transaction times
      remain supplied by MAME. CPU service latency and `OEngine` wiring are
      still pending; this does not complete the input or paired acceptance items.
- [x] Bounded CPU IRQ2/IRQ6 service timing: exception/IACK E phase, sampler,
      consumer, stack and RTE match 9,439/9,439 calls and 474,024/474,024 bus
      events, 0 skipped. Per-access and return times are predicted from CPU
      cycles; seven mutations fail. Pending IPL7/unmask-to-IPL1 has a real
      CPU-instruction fixture. The unported instruction's last bus boundary
      and entry context remain supplied; full CPU scheduling and host wiring
      are not established by this check. Board and CPU service timing are
      delivered together in merged [PR #13](https://github.com/duduke/AfterBurner2-Recomp/pull/13).

[PR #14](https://github.com/duduke/AfterBurner2-Recomp/pull/14) is the user-authorized
merged host-composition checkpoint (`363bce4`, merge `a8849a4`). Continue
from updated `main` on a new `codex/` branch. The subsequent conditional
acceptance checkpoint establishes `$B676` ready/register/yield behavior and
IRQ2 composition; independent publication progress and IRQ4 timing remain.

- [x] Directed host object composition: common explicit entry router plus
      live dispatcher/handler register and ordered-write checks match 480/480
      MAME windows, executing 53,404 verified callbacks, 0 skipped. Additional
      `$9D34/$B628` entry checks match 128/128 isolated calls. The real IPL7
      fixture and explicit unported caller transcripts do not establish natural
      cadence, IRQ interleaving or paired gameplay/video acceptance. `OEngine`
      retains its native producers; Phase 3d remains incomplete.
- [x] Conditional natural IRQ2 and paired acceptance checkpoint: 120/120 early
      dispatcher prefixes and 116/123 later prefixes identical; 7 sub-input
      races skipped, including every IRQ4-overlapped prefix. VBLANK composition
      matches 160/246 windows with 86 input races skipped. Computed requests,
      five road targets, sub size tables, both sprite lists/draw triggers and
      RGB match 226/240 paired frames; 14 raced-producer frames skipped.
      Continuous sound matches 1,259/1,259 calls and 256 latch writes without
      per-call ring resets. Eight mutations rejected. IRQ positions, ADC results,
      unknown callers and specified sub/compositor inputs remain supplied.
      Superseded for IRQ4/shared-input acceptance by the continuation below.
      Independent scheduling and native host retirement remain.
- [x] IRQ4/shared-memory and publication-wait composition: 120/120 separate
      complete dispatchers and 122/122 paired dispatchers identical, zero race
      skips and zero dispatcher exclusions. Paired IRQ2/IRQ4/IRQ6 counts are
      490/122/7. VBLANK matches 244/244 windows; both display lists/draw triggers
      and RGB match 240/240 frames, zero skips. Continuous sound matches
      1,250/1,250 calls and 255 latch writes. Three new mutations rejected.
      Sub-write lane provenance preserves
      computed main values; back-to-back IRQs share the first resumed operand.
      IRQ frontiers/registers, ADC results, unknown callers and sub/compositor
      inputs remain captured. Independent CPU timing/caller scheduling,
      road-raster pairing and native retirement remain unverified.
- [x] Bounded IRQ4 service envelope and selected callee timing: exception,
      mutex/phase, pointer swaps, BSR/JSR, DBF delays, register/stack return,
      service redirect, buttons/timers/bookkeeping/service edge and both
      sprite blits match 1,611/1,611 envelopes and 2,611,132/2,611,132
      predicted bus events, zero skips. `$14856` wait/re-arm matches
      16,349/16,349 uninterrupted steps (six IRQ-overlapped attempts excluded).
      Seven mutations fail. Coverage and remaining boundaries are
      in `ENGINE_NOTES.md`; complex callee clocks/state, sub blit operands and
      interrupted caller contexts remain supplied. IRQ4 timing is not complete.
- [x] IRQ4 road-palette callee timing: `$13BB6/$13BCC/$13BFA` now compute
      bus phases, runtime ROM reads, register effects and returns. 2,553/2,553
      road-palette calls match within 1,728/1,728 IRQ4 envelopes and
      2,950,710/2,950,710 predicted bus events, zero skips. Three mutations
      fail; natural, directed and selector/word-wrap endpoint coverage is
      recorded in `ENGINE_NOTES.md`. Delivered in
      [PR #18](https://github.com/duduke/AfterBurner2-Recomp/pull/18).
      Other callee timing remains supplied.
- [x] IRQ4 palette-cycle callee timing: `$1329E` computes unsigned selector
      branches, phase shifts/masks, runtime ROM transfers, fade/hardware
      destinations and register/return timing. 1,297/1,297 calls within
      2,325/2,325 envelopes and 3,475,316/3,475,316 predicted bus events
      identical, zero skips. Three mutations fail. Natural gameplay does
      not call this entry; directed coverage and limits are in `ENGINE_NOTES.md`.
- [x] Remaining IRQ4 palette/scenery/reload/HUD callee timing:
      `$14A86/$13F92/$16C8A/$18444/$13E5C` and their helper graphs compute
      bus phases, ROM operands, registers and returns. Alongside `$1329E`,
      18,769/18,769 calls match in 3,519/3,519 envelopes and
      8,099,444/8,099,444 predicted bus events, zero skips. Eight mutations
      fail; coverage and supplied sub-write frontiers are in `ENGINE_NOTES.md`.
- [x] Bounded re-arm/dispatcher/publication instructions and IRQ entry: ROM
      prefetches, operand cycles, CCR, consecutive caller boundaries and
      exception entry match 778,252/778,252 records and 2,181,954/2,181,954
      bus events, zero skips; 682,233/682,233 caller boundaries are identical.
      Directed IRQ4 exercises every re-arm instruction. Physical IPL visibility,
      external handler/ISR resumptions and unported caller contexts remain
      supplied; results and uncovered paths are in `ENGINE_NOTES.md`.
- [x] Physical IRQ visibility and bounded caller scheduling: bus-phase yields,
      instruction completion tails, generated timer/VBLANK IPL and chronological
      ADC completion are checked against MAME. Wait sampling gaps execute from
      computed CPU state. Counts, mutation evidence and external execution
      frontiers are in `ENGINE_NOTES.md`; this is not a full host scheduler.
- [x] Carry computed bounded caller IRQ2/IRQ6 entry through the verified
      handler and RTE, restoring the retained user stack before caller
      continuation. Natural and directed checks, explicit unported post-RTE
      boundaries and coverage limits are in `ENGINE_NOTES.md`.
- [x] Bounded IRQ2/IRQ6 RTE interrupt selection and immediate exception entry:
      restored SR, final-prefetch physical IPL, retained SSP and RTE bus-PC
      attribution are computed. 1,673/1,673 selections and 17/17 immediate
      entries match, zero skips; three mutations fail. Twelve unported returned
      instructions remain supplied; coverage is in `ENGINE_NOTES.md`. IRQ4 returns,
      producer cadence and sub progress remain supplied.
- [x] Conditional player/weapon-to-road/compositor pairing: 122/122 complete
      dispatchers, 240/240 road passes and 240/240 RGB frames match, zero skips.
      Per-word write frontiers pair targets even before sprite publication;
      313 target operands come from verified handlers, 405 from unported callers,
      and 482 from initial boundaries. Sequence initializer/prepare/hide and
      RNG route through the common host; two new mutations fail. Independent
      scheduling and live wiring remain pending; see `ENGINE_NOTES.md`.
- [x] Connect IRQ4 RTE/USP and restored-mask selection to the caller scheduler;
      port the six PCs behind all twelve original returned-instruction boundaries.
      Natural/directed IRQ4 returns: 714/714, zero skips; added caller
      instructions: 49,973/49,973, zero skips. That corpus exposed fourteen
      returns at `$8AF8/$B62E`, covered by the following checkpoint. Full
      handler/callee graphs and sub progress remain pending.
- [x] Complete the fourteen returned-word boundaries at `$8AF8/$B62E`:
      14/14 exact CALL5 return timestamps resume from computed CPU state,
      zero missing/skipped; 47,839/47,839 added instruction executions match.
      Expanded coverage exposes ten successor returns at `$8AFE/$B632`;
      full handler/callee timing, publication and sub progress remain pending.
- [x] Complete the ten CALL6 successor returns at `$8AFE/$B632`:
      10/10 computed resumptions, zero missing/skipped; 47,839/47,839 natural
      instructions match. Taken BGE fixtures and two failing mutations pass.
      That expansion exposed seven returns at `$8B02/$B636`, closed below.
      Other handler/callee timing and independent publication/sub progress
      remain pending.
- [x] Close those seven returns and every further exposed return: 38/38
      exact resumptions across the successive corpora, zero missing/skipped.
      Port 97 instruction PCs through world projection/release and publication.
      Natural/directed: 2,694,300/2,694,300 instruction/entry assertions and
      7,910,253/7,910,253 caller bus phases, zero skips; no unported post-RTE
      returns remain in either corpus. Preserve real internal-tail CPU state,
      late IRQ selection and RAM return prefetches. See `ENGINE_NOTES.md`.
- [x] Compute the compound-follower `$7BB6..$7C10` instruction continuation,
      including parent record copies, stage-selected animation, `$B628`
      publication and the return to the dispatcher. Directed acceptance
      requires all 19 PCs, three stage selectors and four animation operands;
      both constant mutations fail. Other handlers and external producers
      remain scheduling boundaries; see the follower continuation in
      `ENGINE_NOTES.md`.
- [x] Compute `$C082..$C098` ready-bit/append/return, the exposed
      `$88CA..$8944` world initializer, and `$B66E` publication installation.
      Natural 600-frame and directed branch/projection checks match MAME;
      five mutations fail. Keep owned caller RAM across linked IRQ2/6.
      Other graph exits and sub producers remain explicit; see
      `ENGINE_NOTES.md` for counts and coverage limits.
- [x] Join IRQ4 service/RTE into the same computed caller scheduler as
      IRQ2/6. Complete the exposed 67-PC sprite-clear graphs and 99-PC
      `$AAEE` trail/projection/release graph; require every PC and both
      directions of all six trail branches. Preserve sprite ownership
      through hardware bank swaps, with renderer cursor words explicit.
      See `ENGINE_NOTES.md` for oracle results and producer limits.
- [x] Compute the sound-ring producer `$B84C..$B884` instruction/bus/IRQ
      timing from supplied caller entry. Directed checks require all 15 PCs,
      reset/suppression branches, ring wrap and byte count/command endpoints;
      the exposed `$D0DA` post-RTE counter increment is also computed.
      Natural/directed producer calls match 252/252, zero skips; four
      mutations fail. Sound caller cadence remains pending; see
      `ENGINE_NOTES.md` for capture counts and coverage limits.
- [x] Compute the bounded button `$14976..$1498E` and axis/target
      `$B0AC..$B368` instruction graph, including `$D0DE` button call timing.
      Natural ADC results and mux-selected sample destinations feed the axis
      reads; physical buttons use `OMainIO`. Directed coverage requires all
      165 production-reachable PCs, every applicable conditional branch in
      both directions and axis/deadband/button endpoints. Six ROM-disabled
      debug PCs remain unverified. See `ENGINE_NOTES.md` for oracle counts,
      producer boundaries and generated/incremental port workflow.
- [x] Compute the bounded live-flight/projection graph `$98C0..$9C32`,
      preserving transitions, slew, flips, runtime tables, hardware math,
      perspective/rotation, history and calls/tail-jumps to timed `$B628` append.
      Natural/directed checks match with zero skipped records and post-RTE
      holes. Directed coverage requires all 232 flight PCs, both directions of
      all 25 conditional branches, slew/rotation/append endpoints and five
      exposed returned-instruction continuations. Full dispatcher/sub cadence
      and paired host acceptance remain pending; see `ENGINE_NOTES.md`.
- [x] Execute bounded sub sprite/road callees in the joint main/sub grant
      replay, retaining private internal-tail debt before successors. Natural
      120: 2,420,931 instructions, 6,110,061 phases and 242,205 bounded
      completions identical, zero skips. 102,479 shared writes are computed;
      39,817 writers and 347 entry contexts remain supplied. Joint coverage
      reaches 631/1,136 graph PCs; five mutations fail. IRQ, main caller,
      device/video/sound boundaries remain pending; see `ENGINE_NOTES.md`.
- [x] Join sub IRQ selection, exception entry, VPA acknowledgement, all
      thirteen service PCs and RTE with retained user resumption, plus the
      twelve main dispatcher PCs at `$1458A..$145B8`. Natural 120 matches
      2,491,840 instruction/IRQ records and 6,309,809 phases, zero skips;
      88/88 services restore interrupted state. One initial sub context,
      4,630 main contexts and 36,902 shared writers remain supplied. Eleven
      mutations fail; main IRQ/caller and device/video/sound closure remain
      pending. See `ENGINE_NOTES.md` for branch and capture boundaries.
- [x] Compute physical main IRQ selection and close the main producers/callees
      in the 600-frame joint stream: 20,532,151 records and 53,449,565 phases
      identical, zero differences/skips, zero supplied main phases, one initial
      main context, and 2,893 retained entries/services/returns/resumptions.
      TRAP #12 and RESET-handler RTE pass; nine mutations fail. It covers
      5,628/7,786 main PCs; other PCs/directions remain unverified. Sub startup,
      other device deadlines/sound and video/compositor ownership remain open.
- [ ] Independently schedule handler/caller and ISR resumptions, then
      progress publication and sub execution without
      captured operand frontiers.
- [ ] Player jet: flight model and compound parts (`$016892`,
      `$01653A`-`$01657A`), feeding the bank/pitch targets the sub CPU
      road generator reads. Live `$9810/$98C0`, recovery `$9626/$964C`,
      shadow/exhaust and sequence update/projection checks pass. Damage
      controllers through state 6, fuselage and debris match 1,792/1,792
      directed and 120/120 gameplay calls, 0 skipped. The companion emission,
      animation and pool-reset graph matches 1,408/1,408 directed and 2,400/2,400
      gameplay calls, 0 skipped; six mutations fail. Carrier/horizon graphs and
      refuel mechanics now have separate checks; refuel stage-flow and host
      replacement remain.
- [ ] Weapons: vulcan and missiles (`$0154B8`-`$015638`), missile reload
      and the HUD sequencer (`$016C8A`). Player projectile/controller/pool,
      reload/HUD, `$153E0..$156CE` and `$1586C..$160A6` enemy graphs pass
      routine checks. Stage spawn dispatch and host replacement remain.
- [ ] Enemies: world-to-screen pipeline and the known handlers
      (`$0088C2`, `$008AF2`, `$0091C0`); map the rest of the
      handler table. `$9626` is player recovery, not an enemy. Ground effects
      `$8D44/$8D58/$8E18/$8ECE` and player collision `$8CC8` are verified;
      world variants, explosion initialization, gate explosions and projectile
      hits now pass directed checks through the `$E542` score boundary.
      `$10E6A` is the horizon strip and is checked separately. Stage formation
      dispatch and host replacement remain.
- [x] Remaining independent handler graphs: carrier/horizon 2,235/2,240 directed (five sub-input races skipped),
      tracking/burst/debris 1,472 directed, world/hits 448 directed (four score
      prefixes), refuel mechanics 384 directed records identical, 0 skipped
      for those three groups.
      Ten deliberate mutations fail. Natural coverage and 3e boundaries are
      listed in `ENGINE_NOTES.md`; these routine ports remain unwired.
- [x] The game's random number generator `$00C5A8`, including zero-seed
      fallback and exact D0/D1 word/long returns. Natural and directed
      oracle checks pass, including the translated enemy missile caller.
- [ ] Collisions and damage (the per-object scaled sizes at `$7800` are
      a likely input), explosions, and losing a life. Debris allocation,
      waiting/animation/removal, projection and attitude callees are verified;
      `$E662/$E8D0/$E94E` states 0..6 now pass directed and bounded gameplay
      checks. Companion emission/animation/pool-reset routines are now checked;
      lives/HUD flow remains an explicit 3e boundary.
- [ ] Main → sound commands from the ported code instead of the
      reconstruction's hand-picked cues

**3e. Stages and game flow**

- [ ] Stage table: confirm the stage count and per-stage settings from
      the ROM (the roadmap's earlier "25 stages" is unverified)
- [ ] Stage scripts, formations and spawn tables for every stage
- [ ] Stage transitions, refuelling stages (5, 13, 20 per
      `ENGINE_NOTES.md`), the carrier take-off and landing sequences,
      and bonus stages
- [ ] Scoring, lives, continue, game over and the ending
- [ ] Attract mode, title, high-score table and entry, NVRAM
- [ ] Coins and credits, DIP switches and the service menu
- [ ] Cover the paths attract mode never reaches, with gameplay
      captures: the sprite pass's skip-sort flag, bucket spill and
      top-and-bottom clip; the road's `$201` override, recentering
      kick and the past-90-degree quadrant flag

**3f. Proving it: identical gameplay**

- [ ] Whole-game oracle: run the port and MAME from the same inputs and
      compare work RAM every frame, then the rendered frames, for a full
      credit through every stage
- [ ] Once a subsystem matches, refactor it into readable C++ with the
      oracle test kept as the safety net (Cannonball's approach)

See `docs/ENGINE_NOTES.md` for the detailed, evolving write-up — treat it
as "current best understanding," not verified fact, until a real port
exercises it.

## Phase 4 — Audio

The original Z80 sound program now drives YM2151/SegaPCM and SDL audio.
See the Stage 1 fidelity notes for the verified command and memory maps.

- [x] YM2151 FM synth, SegaPCM sample playback (`hwaudio/`) — carried over
      near-verbatim from Cannonball, since these are the literal same
      generic Yamaha/Sega chips, not OutRun-specific code (attribution
      preserved in each file's header)
- [x] Wiring the YM2151/SegaPCM chips (implemented in this phase)
      into an actual SDL audio output stream
- [x] Z80 sound ROM execution using the licensed floooh/chips core, original
      NMI latch and YM IRQ protocol, with music/engine/weapon/effect commands
- [ ] Exact command cadence and waveform comparison against the arcade

## Phase 5 — Enhancements Phase

Optional features, visual upgrades, and quality-of-life additions for After Burner II:

### 1. Menus, UI & Convenience
- [ ] **Frontend and Configuration Menus (Seamless In-Game Integration):**
  - *Native Visual Aesthetic:* Render menus directly through the engine's tilemap/sprite/text pipeline (or a cohesive HUD/cockpit overlay) using arcade font tiles and palette themes, avoiding external OS windows or generic popups.
  - *In-Game Pause & Settings Overlay:* Instant in-game pause menu with real-time previews for audio volume sliders, CRT shader toggles, and stick deadzone visualizers.
  - *Pre-Flight / Mission Select Screen:* Attract/menu screen styled after a military briefing terminal to select game modes (Arcade, Roguelite, Time Attack), view high scores, and configure flight controls.
  - *Integrated DIP Switch & Service Access:* Direct access to cabinet settings (difficulty, lives, coin ratios, sound test) in-game without editing text configs.
  - *Seamless Persistence:* Instant save and load of player configuration and high scores to JSON/INI files.
- [ ] **Save States:** Save and load full game state snapshots on demand.
- [ ] **Cannonball Ecosystem Enhancements:** Evaluate and adapt relevant QoL features from Cannonball, Cannonball-SE, and Cannonball DX.

### 2. Controls & Hardware Integration
- [ ] **Comprehensive Input Remapping & Auto-Detection:**
  - *Flexible Device Support:* Full custom binding for keyboard, gamepads, flight sticks, and HOTAS setups with deadzone, linearity, and sensitivity calibration.
  - *Out-of-the-Box Presets:* Auto-detection and profile mappings for:
    - *Modern Gamepads:* Xbox Controller, PlayStation DualSense / DualShock 4, Nintendo Switch Pro Controller (via SDL GameController DB).
    - *Flight Sticks & HOTAS:* Thrustmaster T.Flight HOTAS / T.16000M, Logitech G Extreme 3D Pro, Turtle Beach VelocityOne, VKB sticks.
    - *Retro Arcade Controllers:* Sega Saturn USB / 8BitDo M30, Astro City Mini arcade stick, generic USB encoders.
  - *Throttle Mapping Modes:* Incremental throttle, absolute lever axis, or bumper/trigger acceleration and airbrake.
- [ ] **Force Feedback (FFB) & Haptic Rumble:**
  - *Modern Gamepad Rumble:* SDL haptics/rumble cues for vulcan cannon fire, missile launches, afterburner ignition, incoming lock-on warnings, and explosion/flak hits.
  - *Flight Stick & DirectDrive Support:* Constant force centering and spring resistance scaling with airspeed/afterburner, pitch/roll G-load resistance, stall shudder, and turbulence.
  - *Moving Cabinet Emulation:* Translate arcade motor signals (ADC3/ADC4 feedback and Moving Deluxe cabinet motor commands) into motion rig telemetry (e.g. SimHub / UDP telemetry) or FFB output.

### 3. Graphics, Resolution & Display
- [ ] **Wide-Screen Support (16:9, 16:10, 21:9 Ultrawide):**
  - *Expanded Viewport:* Expand horizontal raster from 320×224 to 398×224 (16:9) or 526×224 (21:9) without stretching.
  - *Extended Road/Ground Raster:* Extend Sub CPU / `hwroad` scanline banking and horizon curves across wider display bounds.
  - *Tilemap Surface Wrapping:* Leverage X-Board's 1024×512 virtual tilemap surface to render wider scrolling backgrounds without clipping.
  - *Wide-Angle Projection & Culling:* Widen 3D camera frustum and sprite culling limits so enemy waves spawn and maneuver naturally across the full width.
  - *Flexible HUD Placement:* Toggle between classic 4:3 centered HUD or anchored widescreen HUD pushed to screen corners.
- [ ] **4K Ultra HD Support (3840×2160):**
  - *GPU Presentation Backend:* Hardware-accelerated presentation via SDL_Renderer (Metal / Vulkan / Direct3D 11 / OpenGL).
  - *Integer Scaling:* Pixel-perfect 9× integer scale of native 320×224 raster with configurable pillarboxing or fill modes.
  - *Advanced Shaders:* High-density CRT emulation (scanlines, shadow mask, phosphor glow, curvature) and edge-preserving scalers (xBRZ, ScaleHQ).
  - *Crisp High-DPI HUD:* Optional native 4K text and instrument overlays to eliminate blurriness.
  - *Smooth Presentation:* Zero-copy frame presentation and sub-millisecond pacing for lockstep 60 Hz / 120 Hz refresh rates.
- [ ] **Upscaled & High-Resolution Sprites:**
  - *Extraction CLI Tool:* CLI dump option in main binary (e.g. `--dump-sprites` / `--extract-sprites` to PNG) for AI upscaling or texture repainting.
  - *High-Res Replacement Pipeline:* Transparent loading and scaled rendering of external high-resolution sprite sheets.
  - *Smoother 360° Rotation Steps:* High-granularity roll/rotation steps via additional intermediate sprite banks or runtime software/GPU micro-rotation interpolation.
- [ ] **Complete Sprite Replacement Option:** Option to substitute alternate custom sprite sets.

### 4. Gameplay Modes & Mission Content
- [ ] **Random Gameplay Mode (Roguelite / Arranged Mode):**
  - *Seeded Runs:* Custom seed entry and daily challenge seeds for competitive, reproducible runs.
  - *Shuffled Stage Progression:* Dynamic stage sequencing with randomized time-of-day palettes and environment tilemaps.
  - *New Procedural Environment Types:*
    - *Night Sortie / Searchlight & Flak Storm:* Dark skies lit by tracer fire, afterburners, searchlights, and dense AAA flak curtains.
    - *Sea-Skimming Naval Fleet Assault:* Low-altitude anti-ship strike over stormy seas, wave-skimming under SAM radar nets to attack warships.
    - *Supersonic City Trench / Megacity Skyline Run:* Urban obstacle weaving through skyscraper canyons, monorails, and bridges.
    - *Severe Weather / Typhoon Penetration:* Heavy crosswinds, wind shear turbulence, and rain sheets requiring active stick correction.
  - *Procedural Formations & Hazards:* Randomized enemy entry angles, missile aggression, rock pillars, supply encounters, and surprise SP stages.
  - *Run Modifiers & Mutators:* Optional toggles (unlimited missiles, hyper speed, glass cannon one-hit death, fog of war, inverted flight).
- [ ] **Optional SP (Special) Stages:**
  - *Airborne Fortress / Arsenal Bird:* Multi-phase boss fight against a giant airborne aircraft carrier bristling with point defense and drone swarms.
  - *ICBM High-Speed Interception:* Maximum-afterburner pursuit and terminal precision strike against an ascending ballistic missile.
  - *Naval Fleet Flagship Strike:* Low-altitude wave-skimming penetration to neutralize a carrier battle group flagship.
  - *Megacity Canyon Dash:* Extreme-speed urban obstacle course threading needle gaps between illuminated skyscrapers.
  - *Typhoon Eye Penetration:* Fight through severe hurricane turbulence and lightning to intercept an elite ace squadron inside the calm eye.
- [ ] **Optional Interactive Landing & Refueling Sequences:**
  - *Aircraft Carrier Refueling Point:* Add carrier touch-and-go landing as an inbetween-missions refueling point.
  - *Interactive QTE & Fine Controls:* Short, arcade-paced (~10s) docking inputs:
    - *Pitch:* Up / Down (glide slope / boom altitude alignment)
    - *Bank:* Left / Right (centerline / drogue basket alignment)
    - *Throttle:* Increase / Decrease (airspeed matching / arrestor hook capture)
  - *Seamless Combat Return:* Smooth transition straight back to combat upon successful contact/touchdown.
- [ ] **Optional 2-Player Co-op / Competitive Wingman Mode:**
  - *Shared-Screen Wingman Flight:* Two F-14 Tomcats airborne simultaneously on screen with distinct liveries.
  - *Competitive Scoring:* Fly cooperatively through stages while competing for top score, kill count, accuracy, and combo chains.
  - *Dual HUD:* Split telemetry and instrument readouts for both players.
  - *Flight Options:* Toggle friendly fire and collision models (no-clip vs. wingtip turbulence).
  - *Multiplayer:* Local dual-controller couch co-op and optional rollback/lockstep peer-to-peer netplay.

### 5. Audio & Scoring Enhancements
- [ ] **Missile Evasion Score Bonus & Dynamic Audio Cue:**
  - *Near-Miss Scoring:* Extra score awarded for close-proximity evasion of incoming homing missiles.
  - *Dynamic 3D Sound Cue:* Distinct stereo "whoosh" sound effect when missiles whistle past the cockpit on near misses.
  - *Evasion Combo Multiplier:* Escalating bonus points for consecutive last-second defensive maneuvers (barrel rolls / high-G breaks).
- [ ] **Option: Sega Ages After Burner II (Saturn, 1996) Redbook Soundtrack:**
  - Optional support for high-quality CD audio tracks while preserving authentic YM2151/SegaPCM sound effects.
  - File-based music player mapping arcade music triggers to external user-provided audio files (e.g. in `res/music/`).
  - Seamless config toggle between authentic arcade FM/PCM music and CD soundtrack.

The synchronized hardware continuation now computes main timer acknowledges
`$1461C/$1466C` and natural `$14840` RESET cancellation/vector prefetches.
The focused coin/start window passes 1,160,234 retained records and
2,961,766 native phases, zero skips; its startup PEA/RTE/TRAP remains supplied.
Whole-host acceptance still requires all four closures in `PHASE3D_PLAN.md`.

### Sub startup, sound handshakes and video bank ownership checkpoint

The 600-frame joint replay now computes all 17 sub startup PCs, with one
initial context per CPU and zero later sub execution supplied. It matches
20,532,168 records and 53,449,620 phases, zero differences/skips, including
854 bank swaps, 568 frame sprite feedback checks, 575 partial screen updates
and 1,278 sound latch reads/NMI acknowledgement requests. Five mutations
are rejected; the existing 300-frame video oracle stays pixel-identical.
Sound CPU/YM/PCM execution and device deadlines, tile/text latches and full
RGB composition, and unexercised paths remain acceptance requirements.
See the final `ENGINE_NOTES.md` section for the exact fixture boundaries.


### Independent audio/device deadline checkpoint (before speaker integration)

- [x] Native heads and IRQ/RESET boost ownership in the current joint window:
  1,338,472 head assertions plus direct per-owner expiry/delivery checks match.
  Continuous PCM/FM clocks produce 314,376/628,796 raw stereo samples and
  6,216/423,102 batch times/indexes, zero differences/skips. Timer A/B IRQ
  diagnostics, thirteen new mutations and the complete required suite pass.
  The initial 33,030 boot grant groups and their 45 boosts, 45 boot main
  sound writes, initial CPU/video states, unobserved paths and mixed/resampled
  speaker output remain explicit boundaries. Game wiring is unchanged.
  See the independent audio/device ownership section in ENGINE_NOTES.


### Mixed speaker and game audio completion

- [x] MAME default LoFi resampling from native 62,500 Hz FM and 31,250 Hz
  PCM to 48,000 Hz stereo. Board gains are 0.15 FM and 0.35 PCM with direct
  left/right routing. All 482,881 stereo frames in 503 natural batches match
  MAME float bits; coupled native CPU/device execution produces these batches
  independently. Directed synthetic PCM tests cover both signs, left/right
  panning and clipping; ten mixer/resampler mutations reject.
- [x] `ArcadeAudio` uses the verified native sound CPU, YMFM, PCM and speaker
  path. Independent timer/mode callbacks, latch acknowledgement and fixed
  20 ms speaker updates replace the old 125 Hz audio block and invented
  command gap. A 20 ms playback buffer preserves speaker phase across
  irregular render requests. Stage 1 commands remain explicit external
  transport inputs; this does not promote reconstructed gameplay producers
  or main/sub cold boot to whole-host acceptance.
- [x] The game loop advances audio without a speaker device, recreates all
  sound state on restart, and applies SDL queue backpressure without silently
  dropping samples. PCM ROM tail bytes use MAME's ERASEFF region definition.
  Existing raw stream, deadline, IRQ, video and whole-host gates stay required.
  Final required `test`, `verify-stage1` and `oracle-test` all pass; the
  complete oracle suite takes 516.47 s. See ENGINE_NOTES for reproduction
  and final validation.
