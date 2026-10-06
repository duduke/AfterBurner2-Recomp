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

**Picking the next task**: Phase 3c is complete; 3d foundations are verified,
but 3d is not complete. Input, live-player, projectile and selected enemy
handlers now have independent checks. The directed host-composition checkpoint
PR #14 is merged. Natural IRQ2 interleaving and conditional paired
player/weapon/video/sound acceptance now pass at supplied caller boundaries.
IRQ4/shared-input composition and publication waits now pass without race
skips. Bounded IRQ4 wrapper/selected-callee and uninterrupted re-arm timing
now pass; remaining IRQ4 callee timing and the interrupted caller schedule
are still supplied. Resume at those boundaries and independent host scheduling; native Stage 1 remains active. Follow the current host acceptance plan in
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
      separate mapping checks. Actual host IRQ/conversion wiring remains.
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
      recorded in `ENGINE_NOTES.md`. Other callee timing remains supplied.
- [ ] Remove remaining IRQ4 palette/scenery/reload/HUD timing boundaries
      (`$14A86/$13F92/$1329E/$16C8A/$18444/$13E5C`),
      verify re-arm/dispatcher instruction scheduling across IRQs, then progress
      publication and sub execution without captured operand frontiers.
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
