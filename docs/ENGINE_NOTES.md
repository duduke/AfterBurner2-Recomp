# Engine reverse-engineering notes

Unlike the hardware layer (`docs/HARDWARE.md`, distilled from MAME's
driver), there's no existing reference for the actual 68000 *game code* —
this file tracks what's been figured out directly from disassembling the
ROM, and is expected to grow incrementally as more of it gets mapped out.
Treat everything here as "best current understanding," not verified fact,
until it's actually been exercised by a running port.

## Reading these notes

This is a chronological reverse-engineering journal. A checkpoint's pending
work and supplied boundaries describe that checkpoint, and may be superseded
later. Use [ROADMAP.md](ROADMAP.md#current-checkpoint-2026-10-24) for current
status and [WORKFLOW.md](WORKFLOW.md) for commands. The native SDL3 game is now
on main; the historical reconstruction no longer drives the application.

Current evidence entry points:

- [Autonomous whole host](#autonomous-whole-host-acceptance-and-game-integration-2026-10-09)
- [Extension contracts](../docs/ENHANCEMENT_ARCHITECTURE.md)
- [Native performance](#native-rendering-and-playable-release-continuation-2026-10-11)
- [Stage and instruction closure](#stage-and-gameplay-instruction-closure-2026-10-11)
- [SDL3 platform](#sdl3-native-platform-migration-2026-10-11)
- [Stage-skip palette correction](#stage-8-black-canyon-ground-after-debug-skipping-2026-10-11)
- [Full native validation](#play-native-publication-validation-2026-10-11)
- [Music routing, balance and live selection](#native-music-presentation-adapter-2026-10-13)
- [SDL controller feedback](#sdl-controller-feedback-2026-10-14)
- [DIP-switch menu](#native-dip-switch-menu-2026-10-13)
- [Explosion and full-throttle flame effects](#captured-afterburner-white-core-report-2026-10-13)

## Tooling

- `tools/build_rom_image.py {maincpu|subcpu} <out>` — reconstructs a flat,
  byte-addressable CPU program image from the interleaved ROM chip dumps in
  `roms/` (no build step required).
- `tools/m68k_disasm.py <binfile> <start_hex> <end_hex>` — a small
  from-scratch M68000 disassembler (not exhaustive, but raises on anything
  unrecognized rather than silently misdecoding — extend it as new
  instruction encodings show up; see its own header comment).

```bash
python3 tools/build_rom_image.py maincpu /tmp/maincpu.bin   # or $TMPDIR
python3 tools/m68k_disasm.py /tmp/maincpu.bin 144A2 14820
```

## Main CPU vector table

| Vector | Address | Notes |
|---|---|---|
| 0 (initial SSP) | `$00000000` | Never actually used as a stack pointer — see "RAM layout" below. Reset code sets a real A7 as its first act, so this is just an unused placeholder value baked into the ROM. |
| 1 (initial PC / reset) | `$000144A2` | Reset routine (see below) |
| 2-5, 25 (bus/addr/illegal/zerodiv/L1 IRQ) | `$00000400` | All point at the same generic handler — presumably a catch-all/spurious-condition trap, not yet examined |
| 26 (L2 IRQ, autovector) | `$0001461C` | Compare/timer chip interrupt — see below |
| 27 (L3 IRQ) | `$00000400` | Same generic handler as above |
| 28 (L4 IRQ, autovector — **VBLANK**) | `$00014674` | The main per-frame engine tick — see below |
| 29 (L5 IRQ) | `$00000400` | Generic handler |
| 30 (L6 IRQ, autovector) | `$0001466C` | Trivial: just re-writes D0 to the compare/timer chip (`$0E8012`) and returns — likely a second, differently-timed acknowledge path for the same chip, not yet fully understood |
| 31 (L7 IRQ, NMI) | `$00000400` | Generic handler |
| 42 (TRAP #10) | `$00014818` | Not yet examined |
| 44 (TRAP #12) | `$0001483C` | Called once during reset right after the hardware-init sequence; not yet examined |

## RAM layout (cross-checked against the hardware maps in Phase 3a)

**There is no separate general-purpose work RAM region on the main CPU** —
only the two documented NVRAM regions (`backup1`/`backup2`,
`docs/HARDWARE.md` section 2), and the game appears to use both for
*all* of its working state, not just persistent settings/high-scores:

- Nearly every RAM reference in the code so far uses 16-bit **absolute
  short** addressing (a bare `$8xxx`-`$Fxxx` operand) rather than a full
  32-bit address. On the 68000, that value sign-extends to 32 bits before
  the bus mask is applied — e.g. `$8050` → `$FFFF8050` → masked by the main
  map's `global_mask(0x3fffff)` → `$3F8050`, which lands in `backup1`'s
  mirror (`$3F8000-$3FBFFF`). Anything in `$8000-$BFFF` lands in that same
  `backup1` mirror; anything in `$C000-$FFFF` lands in the `backup2`
  mirror (`$3FC000-$3FFFFF`) instead.
- The reset routine explicitly sets the real stack pointer to `LEA
  ($FF00).W,A7` → `$3FFF00`, i.e. **the stack lives at the top of
  `backup2`**.
- Everything else observed so far — flag bytes, a lamp-state shadow byte,
  an ADC sample history buffer, a 64-byte-stride object/task table (see
  below) — resolves into the `backup1` mirror. Working theory: `backup1`
  is general variables + whatever persists across power cycles, `backup2`
  is stack + scratch. Needs revisiting once more of the code is mapped, but
  it explains why persistent-looking state (e.g. the NVRAM signature check
  in the reset routine, see below) and everyday game-state flags all show
  up in the same address neighborhood.
- Full addresses `$0029C0xx` and `$002A3Cxx` refer to **shared sub RAM**,
  not backup1/backup2. `$29C0D6` aliases subram0 `$2800D6` (sub CPU
  `$09C0D6`); `$2A3C00` is subram1 (sub CPU `$0A3C00`). These are two
  separate 16KB banks, independent of the main's battery-backed work RAM.
  `MainBus` preserves that distinction and shares the same bytes with
  its `SubBus` instance.

## Reset routine (`$000144A2`)

1. Sets `A6 = $0E4000` (the main-side hardware divider's base address) and
   uses it as an implicit base for the immediately-following hardware
   init — a sequence of writes to I/O chip 0/1 registers (`$14000C`,
   `$140007`, `$150000C`), the "force input" latch (`$160000`), and the
   compare/timer chip (`$0E8010`, `$0E8014`) — plausible power-on hardware
   setup, not yet fully decoded field-by-field.
2. `PEA` + a crafted stack frame + `RTE` to jump to `$1444EC` in supervisor
   mode with a specific SR value (`$0700` = all interrupts masked) —
   re-establishes `A6` and the real stack (`A7 = $3FFF00`), then executes
   a lone `TRAP #12` before continuing. Speculation: this might be
   deliberately re-entering via an exception frame to guarantee a known
   processor state (interrupt mask, supervisor mode) rather than assuming
   the hardware reset already left it that way.
3. Two init subroutine calls (`$0149DA`, `$013C6C` — not yet examined).
4. **NVRAM signature check**: reads a 32-bit value from `$80E0` (→
   `backup1` mirror), masks off some bits (`ANDI.L #$FFFFFFF3`), and
   compares against the fixed constant `$B230E861`. If it doesn't match,
   writes `$00000E10` to `$80DC` (immediately before the checked value) —
   almost certainly "is NVRAM already initialized with a valid
   signature/version; if not, seed it," the classic first-boot/cleared-
   battery pattern. **`$B230E861` is a strong candidate string/checksum to
   search for elsewhere** (e.g. in save-state or NVRAM-dump tooling later).
5. Several more subroutine calls interleaved with re-establishing
   `A6`/`A7` (suggesting some of those subroutines clobber them, or this
   is just defensive), and toggling bits in flag byte `$8B6F`.
6. `JSR ($B714).L` then, at `$014594`, reads a fixed word from ROM
   (`$0001C176`) into flag word `$8806` and enters an **object/task
   dispatch loop**:
   ```
   A5 = $9FC0                    ; table base (backup1 mirror)
   loop:
     A5 += 64                    ; next 64-byte entry
     if (entry.byte[0] & 0x80) == 0: (A0) = entry.long[16]; JSR (A0)
     if (--$8806.W) > 0: goto loop
   ```
   i.e. a fixed-size array of (at least) 64-byte entries, each with an
   "inactive" flag in its first byte's top bit and a function pointer at
   offset +16, called once per entry if active, for a CPU-loaded entry
   count. This is very likely the game's **object/entity table** — the
   same role Cannonball's `oentry.hpp`/`olevelobjs.cpp` play for OutRun —
   worth confirming and, if so, using as the anchor for mapping out
   individual game object types one at a time in `engine/`, mirroring
   Cannonball's file-per-subsystem convention.

## VBLANK handler (`$00014674`) — the per-frame engine tick

This is the actual "run one frame" entry point and the most valuable
routine mapped out so far:

1. Sets a "vblank pending" flag (`BSET #0,$29C0D6`).
2. Decrements a counter at `$2A3C00` and, depending on whether it goes
   negative, takes one of two paths — a **full path** and a **light
   path** that skips a chunk of work. This is very likely a 30Hz/60Hz
   split: some subsystems update every VBLANK, others every *other*
   VBLANK. (Matches the general pattern of Sega boards from this era,
   and Cannonball's own OutRun engine has an analogous even/odd-frame
   split.)
3. **Full path only**: four repeats of the same 6-instruction sequence,
   each against a different 8-byte-stride base (`$2A3C04/0C/14/1C`):
   swap the two 4-byte fields of that record. Reads as a double-buffer
   swap ("current" vs "pending" value) applied to 4 parallel records —
   plausibly the double-buffered scroll/position state for the tilemap
   hardware's 4 register sets (FG/BG/ALT-FG/ALT-BG, `docs/HARDWARE.md`
   section 3.3), though this hasn't been confirmed against where those
   values eventually get written to the actual `$0E90`-`$0E9E`-style
   hardware registers.
4. Calls two palette-animation subsystems (`BSR $014A86`, `BSR $013F92` —
   **not** road/sprite update as originally guessed when this was first
   written; see "The two per-VBLANK subsystem calls" below), then several
   more updates gated by individual flag bits (`$913D`, `$913E`, `$9140`)
   — a "only do this if something changed" dirty-flag pattern.
5. Two `JSR` through fixed addresses that look like they could be
   jump-table/mode-dispatch calls (`$16C8A`, `$18444`) — candidates for
   "per-stage" or "per-game-mode" update hooks; not yet confirmed.
6. Copies a lamp-state shadow byte (`$8B6D`) out to the real I/O chip 0
   port C register (`$140007`) — matches `docs/HARDWARE.md`'s documented
   lamp bit assignment exactly, and confirms lamps are updated once per
   frame from software-maintained state rather than written ad hoc.
7. A short fixed-count delay loop (`DBF` counting from `$39`) — likely
   timing-related, not yet clear what it's waiting on.
8. Conditionally (`BTST #1,$8B6F`) triggers the hardware sprite
   scaler's draw pass: `MOVE.B #$FF,($110001)` — matches
   `docs/HARDWARE.md` section 2's sprite draw-trigger address exactly.
   This is the concrete confirmation of *when in the frame* sprites get
   kicked off.
9. Clears the vblank-pending flag set in step 1, then branches on a
   system-state byte (`$8050`): the common case just restores registers
   and returns; the two special cases both look like deliberate
   "re-enter via a synthesized exception frame" jumps (one to `$1A130`)
   — plausibly a mode-transition mechanism (e.g. test-switch or
   coin-door state change forcing a full reinit), not yet confirmed.

## Sub CPU

Confirms the hardware doc's hypothesis (`docs/HARDWARE.md`'s sub CPU
memory map section): this really does read as a dedicated road/perspective
math coprocessor, not a second general-purpose game-logic CPU.

### Vector table

| Vector | Address | Notes |
|---|---|---|
| 0 (initial SSP) | `$000A4000` | A real, sane value this time (top of `subram1`'s 16KB, unlike the main CPU's placeholder 0) |
| 1 (initial PC / reset) | `$00000402` | |
| 2-5, all autovector levels except L4, all TRAPs except #10 | `$00000400` | Generic handler — `RTE` immediately, i.e. genuinely a no-op/spurious-condition catch, not examined further |
| 28 (L4 IRQ — VBLANK) | `$0000055E` | Sub CPU gets its own independent VBLANK interrupt, matching `docs/HARDWARE.md` section 1 (both CPUs get VBLANK level 4 separately) |
| 42 (TRAP #10) | `$0000062E` | **Byte-for-byte identical** to the main CPU's own TRAP #10 handler — reads as a shared utility ("set interrupt mask to D0 & 7"), not game-specific code; presumably common boilerplate linked into both CPU programs |

### Reset routine (`$402`)

Re-enters via a synthesized exception frame immediately (`PEA`+`MOVE.W
#$700,-(A7)`+`RTE`, the same "force a known SR/supervisor state" idiom
main CPU's reset used), lands at `$40E`, and sets `A6 = $09C000`. That
address is a mirrored alias of `subram0`'s base (`$080000`, mirror period
`$1C000`) — i.e. **`A6` is "my view of the RAM shared with the main
CPU,"** used as the base for practically everything that follows, exactly
like the main CPU's own `A6`-as-hardware-base convention (different chip,
same idiom).

After a `TRAP #10` (set interrupt mask) and clearing a byte at `subram0`
offset `$321`, it **spin-waits for a fixed 32-bit signature
(`$55555555`) at offset `$70`** (`112(A6)`) at `$000420` before proceeding:
```
000420: CMPI.L #$55555555,112(A6)
000428: BNE    $000420
00042A: CLR.L  112(A6)
```
**Confirmed on Main CPU**: Main CPU's reset sequence calls init routine
`$01381E` (via `BSR $01381C` at `$01452A`). After completing all subsystem
hardware setup and clearing sprite RAM, Main CPU writes this signature:
```
0138B4: CLR.L  ($0029C070).L        ; clear handshake slot during init
...
0138F4: MOVE.L #$55555555,($0029C070).L  ; signal Sub CPU to proceed
0138FE: RTS
```
(`$0029C070` is `subram0` offset `$70` in Main CPU's address space;
`112(A6)` is the exact same location in Sub CPU's space). As soon as Sub
detects the signature, it clears the slot back to 0 at `$00042A` and continues
its boot.

The rest of the reset routine is a state machine over a byte at offset
`$321` (values 0-4ish) that progressively zeroes a large block at hardware
address `$0EC800` (256+ words, well within the road control RAM region —
plausibly clearing all road-scanline entries before first use) and cycles
through calls that look like road-hardware self-test/init (`$5C4`, `$5EE`,
`$604`-family — each writes fixed patterns into `$0EC000`/`$0EC400`
before checking a completion bit at hardware address `$0EE001`, which is
inside the road *control register* range, `docs/HARDWARE.md` section
3.2 — consistent with "poke a test pattern, then read the control
register to see if the road hardware accepted it").

Sub CPU then enters its continuous processing loop at `$000432`:
1. Executes road geometry / scanline updates (`$5C4`, `$5EE`).
2. Calls the perspective / counting-sort math routine at `$000640` (see below).
3. Waits for hardware completion (`TST.W $7C00(A6)` at `$000452`).
4. Signals road buffer readiness: `BSET #1,214(A6)` (`subram0[$D6]` bit 1).
5. Triggers road RAM double-buffer swap: reads `$000EE001` (control register),
   then writes active road priority/bank byte (`MOVE.B 800(A6),(A1)`).
6. Runs stage road segment state machine and loops back to step 1.

### VBLANK & Inter-CPU Synchronization

There are **two distinct mutex / handshake flags** in `subram0`:

#### 1. Frame-Update Mutex (`subram0[$D6]`, bit 0)
- **Main CPU VBLANK entry/exit**:
  ```
  014674: BSET #0,($0029C0D6).L    ; lock Sub CPU during frame update
  ... (double-buffer swaps, palette anim, lamp updates, sprite draw trigger)
  014798: BCLR #0,($0029C0D6).L    ; release Sub CPU
  ```
- **Sub CPU background math engine (`$000640`)**:
  At the very start of the perspective / counting sort routine:
  ```
  000640: BTST #0,214(A6)          ; test subram0[$D6] bit 0
  000646: BNE  $000640             ; spin-wait while Main VBLANK is running
  ```
  This guarantees Sub CPU never mutates shared perspective/road data while
  Main CPU's VBLANK interrupt is reading it or updating hardware registers.

#### 2. Pause / VBLANK Hold Mutex (`subram0[$00]`, bit 0)
- **Sub CPU VBLANK handler (`$00055E`)**:
  ```
  00055E: MOVEM.L ... -(A7)
  000562: LEA ($0009C000).L,A6
  000568: BTST #0,0(A6)            ; test subram0[0] bit 0
  00056E: BNE  $000568             ; spin-wait if paused/held by Main
  ...
  000586: MOVE.W #$39,D0; DBF D0...; MOVEM.L (A7)+...; RTE
  ```
- **Main CPU pause handler (`$0145BA`-`$01460C`)**:
  Checked during input polling when the Pause switch (bit 3 of I/O chip 1
  port A at `$150001`) transitions:
  ```
  0145DA: BSET #0,($0029C000).L    ; lock Sub CPU VBLANK
  0145E2: BSR  $014856             ; wait for VBLANK loop
  ... (waits until Pause is released/toggled again)
  014604: BCLR #0,($0029C000).L    ; unlock Sub CPU VBLANK
  01460C: RTS
  ```

#### 3. Sprite Pipeline & Sub CPU Depth-Sorting (`$000640`)
The 4 double-buffered pointer pairs swapped in Main CPU VBLANK at `$01468E`
form an inter-CPU sprite pipeline:

| Offset in `subram0` | Sub CPU (`A6=$09C000`) | Main CPU (`$0029C000`) | Description |
|---|---|---|---|
| `$7C00` | `31744(A6)` | `$002A3C00` | Frame sync / VBLANK counter |
| `$7C04` / `$7C08` | `31752(A6)` | `$002A3C04` / `$002A3C08` | Unsorted Sprite Buffer pointers (Source) |
| `$7C0C` / `$7C10` | `31760(A6)` | `$002A3C0C` / `$002A3C10` | Unsorted Sprite Count pointers |
| `$7C14` / `$7C18` | `31768(A6)` | `$002A3C14` / `$002A3C18` | Sorted Sprite Buffer pointers (Destination) |
| `$7C1C` / `$7C20` | `31776(A6)` | `$002A3C1C` / `$002A3C20` | Sorted Sprite Count pointers |

1. **Emission by Game Objects**: During the main object loop (`$0145A0`),
   each active object calls `append_sprite` (`$00B628`), copying its 16-byte
   sprite descriptor into the **Unsorted Sprite Buffer** `($002A3C04)`.
   When all objects are processed, `$00B67E` writes the count to `($002A3C0C)`
   and mirrors it to `subram0[$58]`.
2. **Sub CPU Z-Sorting & Perspective**: Once the VBLANK mutex clears, Sub CPU
   reads the unsorted count from `31760(A6)` and buffer from `31752(A6)`.
   At `$000672`, it extracts the Z-distance byte (`MOVE.B (A2), D0`) and runs a
   **counting sort** into bucket arrays at `22528(A6)` (`$0A1800`), building a
   depth-sorted list of indices at `32000(A6)` (`$0A3D00`). It then iterates through
   the sorted indices, uses its hardware multiplier/divider (`$0E4000`) to compute
   perspective scaling, and stores the final sorted 16-byte descriptors into the
   **Sorted Sprite Buffer** `31768(A6)` (`$002A3C18`).
3. **Double-Buffer Pointer Swap**: At the top of Main CPU VBLANK (`$01468E`),
   Main CPU swaps each of the 4 pointer pairs so the newly sorted buffer becomes
   active for display while the next frame writes into the alternate buffer.
4. **Hardware Blit & Trigger**: Main CPU VBLANK (`$00B7D2` / `$00B7E4`) blits
   the active sorted buffer `($002A3C14)` directly into Hardware Sprite RAM at
   `$00100000`, then writes `$FF` to `$00110001` (`$014744`) to kick off the
   hardware line scaler!

#### 4. Road / Ground-Scroll Generation Pipeline
The road layer is completely generated by Sub CPU using flight parameters from Main:

1. **Main CPU Control & Flight Model**:
   - `subram0[$9C]`: Current stage index (stages 1..24).
   - `subram0[$CE]`: Desired roll / camera bank target from flight controls.
   - `subram0[$AE]`, `[$B0]`, `[$B2]`, `[$B4]`, `[$B6]`: Interpolated pitch,
     horizon position, horizon slew rate, and curve curvature.
   - `subram0[$9E]`, `[$60]`: Stage-specific curve and elevation parameters.
2. **Sub CPU Road Engine** (`$000432`-`$000538`):
   - At `$00046A`-`$0004A0`, Sub CPU reads stage index `subram0[$9C]` and
     dispatches to the appropriate ground/sky generator (`$000C0A` for standard
     stages 1/7/16, `$000CEA` for stage 3, etc.).
   - Computes horizon scanline position (`112 + horizon_offset` at `$000F70`,
     where 112 is the half-screen midpoint of the 224 scanlines).
   - Computes per-scanline road curve and writes the 224-word raster tables
     into Road Control RAM:
     - Horizontal offset table 1 & table 2 (`$0EC200`, `$0EC400` / `$0ECA00`, `$0ECC00`).
     - Priority/bank configuration byte into `800(A6)` (`subram0[$320]`).
3. **Double-Buffer Hardware Flip**:
   - After waiting for math completion (`TST.W $7C00(A6)` at `$000452`), Sub CPU
     sets the road ready flag `BSET #1, 214(A6)` (`subram0[$D6]` bit 1).
   - Reads the road control register at `$000EE001` (`TST.B (A1)` at `$000464`),
     which **flips the hardware road RAM double-buffer** so the newly generated
     scanline tables are drawn during next scanout.
   - Writes `subram0[$320]` to `$000EE001` to latch active road priority.

## The two per-VBLANK subsystem calls (`$014A86` and `$013F92`)

Both turned out to be **palette animation systems**, not road/sprite
object processing as originally guessed — fitting, for a game whose
signature visual is a constantly-flickering afterburner flame, and
between the two of them they account for essentially all of the "moving
colors without moving tiles" effects this hardware is capable of.

### `$014A86` — palette fade/flash effect

Gated by tile-RAM-resident state at `$0CDD00`-`$0CDD08` (working
variables, not display data — using a normally-visible RAM region as
scratch is a common trick when not all 16 tilemap pages are in use). On
trigger (a bit at `$0CDD07`):

- Snapshots two small chunks of palette RAM (around word offset `$700`
  and `$1D22`-ish, based on a rotating 0-3 slot index) into scratch space
  in tile RAM (4 possible slots, `$700`-long-word-strided, i.e. generous
  headroom for however much of each snapshot is actually used).
- Every subsequent call, for 32ish iterations of an inner 14-iteration
  loop, decodes 3 bits of a "which channels are still fading" selector
  byte (`$0CDD06`) and, for each of 3 packed 4-bit fields within each
  palette word (bits 0-3/4-7/8-11, each paired with an "active" flag at
  bit 12/13/14), decrements that field by 1 — but only every *other* call
  per field (the flag bit toggles each time, halving the effective fade
  rate) — until it can't decrement further.
- Counts down a duration byte (`$0CDD05`) each pass; once it hits zero,
  the effect is done and `$013F92` (below) resumes running normally
  (which explicitly checks `$0CDD05 != 0` and skips itself while a fade
  is active).

Reads as a general-purpose "fade a window of palette colors down, 3
independent channels, over N frames" utility — plausible uses: an
explosion flash fading out, or the afterburner flame's own color easing
between intensity levits. Not yet confirmed which specific in-game effect
calls it (would need to find where `$0CDD07`'s trigger bit gets set).

### `$013F92` — palette animation streaming

Runs a small state machine driven by a frame counter at `$8C2A` and a
"which palette set" selector at `$8D30`:

- Every 4 frames (`(frame>>2)<<5` — i.e. floor to a multiple of 4, then
  ×8), streams a new 14-word (7 longword) chunk from *three separate*
  fixed ROM tables (`$1413A`, `$14196`, `$141F2`, each indexed by both the
  frame-quarter and the palette-set selector) into three fixed palette
  RAM destinations (`$121D22`, `$121EA2`, `$121E42`).
- Additionally, exactly when the frame counter hits `30`, streams a
  *fourth* 7-longword chunk from yet another ROM table (`$2EA16`,
  indexed only by the palette-set selector this time) into palette RAM
  at `$121E22` — a slower, independent animation cycle layered on top of
  the other three.

This is squarely a **palette-cycling animation player**: several
independent ROM-authored color sequences, each looping through its own
frames at its own rate, continuously pushed into fixed palette slots.
Very likely covers things like the exhaust/flame flicker and the HUD
warning-light colors (`docs/HARDWARE.md`'s documented altitude/lock-on/
danger lamps, section 5, almost certainly have matching on-screen color
cues driven by exactly this kind of system) — not yet confirmed which
palette destination corresponds to which visual element.

## VBLANK Subsystem Hooks (`$016C8A` and `$018444`)

The two fixed function calls in Main CPU VBLANK were mapped out:

### `$016C8A` — Missile Reload / Weapon Replenishment Sequencer
- Gated by flag `$9102` / `$90FF`.
- Compares player missile count at `$8D66` against the stage maximum from ROM table (`$0003218A`/`$0003218C`).
- When reloading: decrements pending ammo delta `$9100`, increments `$8D66`, queues audio command `$9B` (missile reload beep) into the sound queue at `$890C` via `$00B84C`, and calls `$00CFC4` to update the HUD missile icons.

### `$018444` — Stage Event & Background Scenery Scroll Engine
- Checks current stage from `subram0[$9C]`:
  - Special stages 5, 13, 20 (`$05`, `$0D`, `$14`): Refueling / landing sequence state machine (`$9040`/`$9044`).
  - Standard combat stages 1, 7, 9, 15, 16 (`$01`, `$07`, `$09`, `$0F`, `$10`): branches to `$018798`.
- Scenery tile & scroll generator (`$018798`):
  - Streams scenery background tiles from `$00031252` into Tile RAM page at `$0C216C`.
  - Calculates camera roll-compensated horizontal scroll value from roll angle `subram0[$8E]` (`$0187E4`–`$0187F6`).
  - Latches result into the ALT-FG x-scroll register at `$000D0E9C` (`0x74E` word offset in text RAM, `docs/HARDWARE.md` section 3.3).

## Object / Task System (`$9FC0`)

The dispatcher starts A5 at the `$9FC0` sentinel, increments it **before**
testing the first record at `$A000`, and visits 254 records through the last
record starting at `$DF40` (last byte `$DF7F`). These are sign-extended
absolute.W work-RAM addresses. The runtime count at `$0001C176` is loaded
into `$8806` at `$014594`. Phase 3d's dispatcher oracle verifies this layout.

### 64-Byte Object Entry Layout
- `+0`: Bit 7: `1` = Active, `0` = Inactive. Bit 5: Horizontal flip.
  The low ten bits of the first **word** are request zoom/flags, not screen Y.
- `+2`: Request depth byte (used by the sub CPU bucket sort).
- `+3`: Request palette byte.
- `+4`: Longword: current screen X, Y coordinates.
- `+8`: Longword: previous screen X, Y coordinates (used for interpolation / deltas).
- `+12`: Longword: request frame-record pointer (copied by `append_sprite`).
- `+16`: Longword: **Per-object tick / update function pointer** (called by dispatch loop via `MOVEA.L 16(A5),A0; JSR (A0)`).
- `+20`: Size-table slot byte (replaces request byte +12 in `append_sprite`).
- `+36`: Object Z-distance / depth (used by Sub CPU counting sort) or bank angle.
- `+38`, `+40`: Velocity vectors (X, Y).
- `+42`: World 3D X coordinate.
- `+54`: Trajectory table / motion script pointer.
- `+60`, `+62`: Attachment offsets for multi-part objects.

### Core Object Types Mapped

1. **Sequence Player Compound**:
   - `$016976` reads the object base from ROM `$01C5C8` and adds `$300`.
     For this ROM that selects `$C400` (slot 144), followed by three parts;
     the earlier claim of slots 0..3 was incorrect.
   - `$016892` initializes fuselage, two exhaust parts and a weapon pod,
     assigning `$01653A`, `$01656A`, `$016572`, `$01657A` respectively.
     `$0169B2` **hides** four parts by clearing their active bit; it does
     not activate them. Setup, initialization and hiding now have independent
     directed write-set/register checks (see Phase 3d below).
   - `$01653A` invokes `$01678E` for sequence projection and emits requests.
     This alone is not the ordinary player's live flight model. The live
     player path `$009810/$0098C0` and its input/projection callees still
     require their own capture and port.
2. **Player Weapons (Missiles & Vulcan Cannons)**:
   - Routines at `$0154B8`, `$01552C`, `$015582`, `$015638`.
   - Spawns forward projectiles from jet origin, tracks velocity, checks target lock, decrements ammo `$8D66`.
3. **Enemy Aircraft & 3D Objects**:
   - Update routines at `$0088C2`, `$008AF2`, `$0091C0`, `$009626`.
   - Reads 3D world coordinates `(X, Y, Z)` from fields `+42` and `+36`, transforms coordinates using camera roll/pitch matrix at `subram0[$90]/[$92]` through SEGA 315-5248 multiplier at `$0E0000`, selects banking sprite frame from lookup table `$00033ED8`, and calls `append_sprite` (`$00B628`).

## Oracle testing: verifying ports against the real code

The goal is gameplay identical to the arcade, so ported routines are checked
byte-for-byte against the original code running in MAME, not by eye:

- `tools/oracle/capture.sh <script.lua> <out>` runs MAME headless with a Lua
  autoboot script that installs memory taps at chosen synchronization points
  and dumps RAM snapshots. Fresh NVRAM/config per run keeps boot
  deterministic. Captures derive from your own ROMs: they stay in
  `build/oracle/` (gitignored) and are regenerated, never committed.
- A test per ported routine replays each captured "before" state through the
  C++ port and diffs every region the routine writes against the captured
  "after" state. `make oracle-capture` then `make oracle-test`.
- Ported routines keep the arcade's memory layout (`engine/subbus.hpp` for
  sub CPU code) and are translated instruction-by-instruction. A readable
  rewrite can follow once the oracle test pins the behavior.
- Check coverage, not just pass counts: the capture only proves the paths it
  exercises (see "unverified paths" below). Prove the test can fail, too — a
  one-pixel change to the X origin makes 910 of 1,200 passes fail.

## Sub CPU sprite pass (`$000648`-`$0009E2`) — ported, verified

`src/main/engine/osubsprites.cpp`. On 1,200 passes captured from 30 seconds
of attract mode, 1,199 are byte-identical to the arcade in every region the
pass writes; the remaining one is skipped because the main CPU changed the
pass's inputs while it ran.

What it does each pass:

1. Bucket-sorts the main CPU's requests by their depth byte (256 buckets of
   at most 30; a full bucket spills deeper; past the last one the request is
   dropped), then drains buckets shallow-to-deep into an index list at
   `$7D00`.
2. Turns each request into a final 8-word hardware sprite descriptor
   (`$0007B8`): scaled size `= size * zoom / $7F` and hardware zoom
   `= $FE00 / zoom` via the 315-5248/5249 chips (zoom below `$40` uses a
   table at `$0009E4` that also picks a lower-detail frame record),
   centering, the hardware's `+$100`/`+$160` origins, and clipping —
   off-screen sprites get only the hide bit, partly off-screen ones get
   their ROM offset and draw direction adjusted. Scaled sizes go to a
   per-object table at `$7800` (likely read back for collisions).
3. Writes the count and end marker, then (`$000726`) a second copy of the
   list `$C00` bytes later with each sprite moved halfway back toward its
   previous-frame position.

**The game logic runs at 30 Hz.** Main CPU VBLANK shows the `+$C00`
(halfway) list on the frame it swaps buffers (`$00B7D2`) and the exact list
on the next (`$00B7C2`), so objects move smoothly at 60 Hz while only
updating every other frame. A port that runs object logic at 60 Hz cannot
match the arcade.

Formats (offsets within each 16-byte record):

- Request (main CPU `append_sprite`, `$00B628`, max `$C0` per frame): +0
  flags/zoom (bits 0-9 zoom, 0 = hidden; 10/11 opt out of X/Y
  interpolation; 12/13 vertical/horizontal flip; 14: Y is the bottom edge
  instead of the middle), +2 depth, +3 palette, +4/+6 X/Y, +8/+10 previous X/Y, +12
  byte: size-table slot, +12 long (low 24 bits): frame record pointer.
- Frame record (sub ROM): +0 width, +2 height, +4 descriptor word 0 (bank),
  +6 ROM offset, +8 pitch, +10 offset adjustment for vertical flip.

Unverified paths (translated but never exercised by the attract capture):
the skip-sort flag (`subram0[$3A]` bit 0), the bucket overflow spill, and a
sprite taller than the screen clipped at both top and bottom (`$000942`).
Gameplay captures should cover them.

## Sub CPU road generator (`$00046A`-`$00052C`) — ported, verified

`src/main/engine/osubroad.cpp`. On 1,800 frames captured from 50 seconds of
attract mode (which cycles through stages 1, 3, 5, 11 and others), 1,798
write exactly the bytes the arcade writes, with the same values; the other
two are skipped because the main CPU changed something the port read
mid-frame.

The banked ground is **not** a rotated plane: it's the road chip's
per-scanline horizontal scroll. Each frame, after flipping the road RAM
double buffer (`$000464`) and writing the road control register from
`subram0[$320]`, the sub CPU:

1. `$000C0A` (stages 1, 7, `$10`): eases bank values toward the target in
   `$CE` (zeroed while `$201` bit 0 is set): `$B4` follows 3/8 of it at up
   to 2/frame, `$B6` follows `$B4` at up to 1/frame, and `$AE`/`$B2` add a
   recentering kick when the target swings past `+-$B0`. `$000CEA` (also
   the entry for stages 3 and `$18`) stores `$B4 + $AE` into `$9E` and an
   8-entry history ring at `$620`.
2. `$000D0E`: clamps the pitch target (`-$D0`) to `[$EA, $EC]` and moves
   `$96` toward it by at most `$EE` per frame.
3. `$000D64`: sine/cosine of the roll byte `$9F` (signed-byte table at
   `$15D4`, cosine 64 entries earlier) into `$90`/`$92`, quadrant flags in
   `$A0`, a 32-step coordinate table at `$100`, and the horizon line `$82`
   (clamped to `+-111`) from pitch and altitude (`$C2`).
4. `$000F02`: the raster. The bank value from three frames ago picks one of
   four mirrored variants (bits 7-8) and a ramp (low 7 bits) from tables at
   `$11D4` (16.16 slope per line) and `$13D4` (ramp length). From the
   horizon line it writes road 0's scroll table (`$0EC4E0` is line 112)
   outward in both directions: the slope for the ramp's length, then flat.

At this initial milestone, the special sequence was unported. Phase 3b
below adds `$0016D4`/`$001758`, the comparator and directed sequence coverage.
Remaining normal-road coverage gaps include the `$201` override, recentering
kick and banks past 90 degrees; full carrier game flow remains Phase 3e.

Oracle notes from this port: the capture compares **write sets** rather
than whole snapshots, so the main CPU's concurrent writes can't cause false
failures, and a frame is a race only if the main CPU changed a byte the
port actually read. The sub CPU's stacks (top 256 bytes of subram1) are
excluded — VBLANK exception frames get pushed while the PC still points at
the interrupted instruction, so no PC filter can catch them — and windows
where the sub CPU writes from outside the road code (the main CPU resets it
once during boot) are dropped by the capture.

## Video oracle: the hardware layer, verified

`tests/oracle_video.cpp` renders captured video state through `Screen`
(`hwroad`, `hwtiles`, `hwsprites`, `hwpalette`) and compares RGB with the
frame MAME displayed. 300 of 300 attract-mode frames are pixel-identical,
and so are 100 of 100 captured with every sprite list emptied
(`AB2_ORACLE_NO_SPRITES=1`, which isolates the road and tilemaps). On a
mismatch the test writes up to three `build/oracle/video_diff_<frame>.ppm`
images.

Most of what the video hardware draws from isn't plain RAM, so
`tools/oracle/capture_frames.lua` snapshots each piece at the moment the
hardware takes it:

- **Sprites**: sprite RAM at the main CPU's draw-trigger write
  (`$110000`-`$11FFFF`), just before the swap.
- **Road**: CPU-visible road RAM at the sub CPU's write at `$000458`
  (just before it flips the buffer), committed at its control write at
  `$000466` together with the control byte.
- **Tilemap registers** (text RAM `$E80`-`$E9F`): as they were at scanline
  261, when the hardware latches them. Lua has no `vpos` here, so the latch
  time is the frame notifier's time plus 37 lines of 64 us.
- **Palette**: at the notifier where the pixels are read, not the one the
  state came from (below).

Timing lesson: MAME finishes the frame, flips the screen's double buffer,
then calls frame notifiers, so `screen:pixels()` at notifier N is the image
drawn from the state at notifier N-1. It converts pen indices to RGB through
the palette as it is at N, though. The INSERT COIN blink (the main CPU
blanks the text every 32 frames) exposed the one-frame offset, and the
exhaust glow's palette cycling exposed the palette timing.

Bugs this found: `hwroad` was missing the chip's `$5F8` horizontal origin
(the ground rendered as a solid fill), `hwtiles` had no row/column scroll
or ALT tilemap set (the attract mode's rocks are drawn through it), and the
sprite buffer swap is an exchange of the two halves, not a copy.

## Main CPU foundation (Phase 3a) — implemented and tested

`engine/mainbus.*` implements the 22-bit main map, big-endian byte/word/long
accesses, all documented RAM mirrors, main/sub ROM views, low-byte ADC and
I/O ports, math-chip byte masks, sprite draw swaps, and road control swaps.
The main's work RAM is independent of shared RAM. The sub-side math chips,
shared RAM and CPU-visible road RAM are the same instances used by `SubBus`.
`hwmath/io_expander.hpp` models the CXD1095's nibble directions on A-D,
individual directions on E, write-only control registers, and latched
outputs. I/O callbacks expose inputs, ADC selection, lamps and sound reset;
video writes/swap/control and timed ADC conversion are host endpoints.
This is a bus for literal ports, not a 68000 interpreter. Only aligned
word/long accesses are supported; CPU address-error exceptions are outside
its scope. The reconstruction does not call the new bus or routine yet.

**315-5250.** `hwmath/compare_timer.hpp` models signed bounds (in either
order), clamp/result flags, the 16-bit comparison history, the no-history
value alias, timer reload/enable aliases, read/write IRQ acknowledgements,
and the main-to-Z80 command latch with NMI acknowledgement on sound reads.
EXCK advances only on rising edges. The terminal value `$FFF` interrupts
on the following rising edge, even if counting is disabled, and reloads
from register 8's low 12 bits. The X-Board clocks only the main chip from
scanline parity; the sub comparator has no timer clock connection.
Sound writes are delivered at the host call boundary; a future CPU host
must schedule that boundary as MAME's synchronized delivery does.

`tools/oracle/capture_compare.lua` captures the real boot's bus traffic,
Z80 latch reads and board timer IRQ flag, plus directed sub-chip comparator
probes through a mirrored window. `tests/oracle_compare.cpp` replays these
operations and raster edges independently through the model:
**2,438 of 2,438 events identical, 0 skipped**, including 1,100 asserted
IRQ acknowledgements, 581 sound reads and 62 comparator writes. The initial
screen phase matters: MAME begins at line 224. The capture queries
`screen:time_until_pos(1)` for the first EXCK rising edge rather than
assuming the first edge is 64 us after reset. Frame checkpoints verify the
IRQ flag even while the CPU is not touching the chip.

Hardware-oracle coverage: below/inside/above signed bounds, reversed and
extreme bounds, all readable comparator registers and aliases, high/low
byte merges, clearing history, 18 history updates (only 16 visible bits),
no-history updates, mirrored sub accesses, the boot's `$FE0` timer reload
and enable, real write acknowledgements, and real sound-latch data.
Disabled-terminal IRQs, read acknowledgements, alternate reload/enable
aliases and `$F`/high-byte sound writes have ROM-free regression checks,
not directed MAME probes. History wrap after 256 updates, arbitrary timer
reloads, ADC conversion timing, whole-map rendered integration and complete
main IRQ handlers remain unverified. No claims of whole-game fidelity.

**Main CPU oracle.** `engine/omainadc.cpp` translates ROM
`$014626`–`$01465A` instruction by instruction: read the completed ADC
sample, store it in the six-byte ring at `$8892`, advance/wrap the index
at `$8890`, update `$8B6F`'s channel bits, write port C and start the next
conversion. It deliberately ends before `$014660`'s unported `$00B7F8`
call; IRQ entry/exit and acknowledgement belong to the complete handler.
The completed ADC byte is captured as an external input, not a fabricated
conversion. `capture_main.lua` snapshots all MainBus RAM before/after,
logs main-only write sets keyed on these PCs, and separately logs sub CPU
writes. A replay is a race only if the sub CPU wrote a byte the port read
(including writes restored to their original value). Main stack bytes
`$FE00`–`$FFFF` are excluded; writes from another main routine drop the
window. I/O write addresses, active byte masks/data and actual port-C
output are compared as well as written RAM bytes.

Results: **600 of 600 attract records identical, 0 skipped**; **1,800 of
1,800 gameplay records identical, 0 skipped**; no abnormal windows dropped.
All six ring slots and the wrap branch execute: 100 samples per slot/100
wraps in attract, 300 per slot/300 wraps in gameplay. Captures contain three
and eight distinct ADC byte values respectively. Negative/out-of-range
corrupt ring indices are not exercised. Input mapping/flight response and
the full timer/sound sequencer are still later ports.

**Scripted play.** `tools/oracle/gameplay.lua` inserts a coin at frame
360, presses Start at 420, then moves the stick through four fixed positions,
alternates throttle and holds Vulcan with periodic Missile presses from
frame 480. The real game supplies all behavior; no RAM cheats. A local MAME
snapshot at frame 900 confirms live Stage 1 play. `AB2_ORACLE_GAMEPLAY=1`
uses this same script for the main, sub sprite/road and video captures;
`AB2_ORACLE_SKIP` selects the capture window for the latter three.

Additional gameplay results: sprite pass **1,198 of 1,200 identical, two
skipped for input races**; road **1,800 of 1,800 identical, 0 skipped,
0 unported special sequences**; video **300 of 300 pixel-identical**.
These captures do not yet prove every rare sprite/road path or every stage.
The prior attract captures retain their documented results.

**Sensitivity checks.** Changing the ADC wrap constant from 6 to 5 makes
100 of 600 records differ. Changing the comparator's below-bound flag from
`$8000` to `$4000` makes 8 of 2,438 events differ. Both mutations were reverted
and the original oracles rerun. `make test` includes ROM-free MainBus/chip
checks; `make oracle-capture-main` regenerates all new hardware/main/gameplay
captures. `make oracle-test` includes them. Capture failures, Lua errors and
runs that expire without completion now return failure from `capture.sh`.
Captures and snapshots remain local and gitignored.

## Main scenery engine (Phase 3b, first port) — verified, not integrated

`engine/oscenery.*` translates the main CPU's `$018444` stage-event entry
and its helpers: `$01854C` scroll/palette state machine, `$01866C` tile-list
decoder, `$01871A` scenery initialization, `$018798` banked scenery and
segmented display, and `$018902` byte-tile uploader. It also translates the
`$013A74` blank-tile tail call. Each table/header stays in ROM and is read
through MainBus at runtime. Word overflow, signed ASR, byte-high attributes,
SUBQ carry/underflow, high-byte flag bits and inclusive DBF/DBEQ loops are
preserved. The segmented-display path uses the original `$0188C8` table;
its data is not pasted into source.

`tools/oracle/capture_scenery.lua` uses the main write-set format with magic
`AB2SCN01`. It starts on `$018444`'s `$9044` read, captures the outer stack
pointer, and ends on the matching RTS stack read. **Do not use opcode
prefetch as the return boundary**: the CPU can fetch past a DBF before the
loop finishes. Stack bytes are excluded; nested returns have a different
SP. Main writes from outside the routine/helpers drop the window. Sub CPU
writes are tracked separately, and a record is skipped only if they touch
a byte the replay read, even if the sub restored its previous value.
`tests/oracle_scenery.cpp` runs the port from each before snapshot and
compares exact write sets and values across all seven MainBus RAM regions.

Results (zero abnormal windows dropped):

- Attract, 2,000 consecutive calls: **2,000 of 2,000 identical, 0 skipped**;
  1,682 calls with writes, 895 tile uploads; raw selectors 0/1/2/3.
- Extended attract, 1,200 calls sampled every eight frames through frame
  9,600: **1,200 of 1,200 identical, 0 skipped**; 573 calls with writes,
  441 tile uploads and four palette updates; raw selectors 0/1/3/4/5/6/11,
  state-machine modes 0/4/8/12. The palette loop at `$0185DA` returns to
  `$0185CE`: source A2 resets for each iteration, while A3 advances.
- Scripted gameplay, 1,200 calls from frame 420: **937 of 1,200 identical,
  263 skipped for sub CPU input races**; 925 matched calls upload tile
  data. Raw selectors 13/14/15/16 include the segmented-display branch.
  These raw selector values must not be equated with displayed stage
  numbers or used to infer the game's stage count.

Total: **4,137 of 4,400 identical, 263 races skipped, zero different**.
Changing the `$0187F2` scroll adjustment from -4 to -3 makes 841 of the
2,000 consecutive attract records differ. The mutation was reverted and
the original routines rerun against all captures.

Unverified paths: selectors/event-table entries not listed above, every
possible timer/repeat transition, corrupted out-of-range table/count
inputs, and byte/word tile-list paths not separately counted by the
coverage report. The capture verifies calls, not full frames or rendered
integration. Race-skipped calls are not evidence of correctness. The
stage/road setup slices, special sub-road sequence and hardware-renderer
integration are now completed below. Full game frame scheduling is 3c;
credit/stage dispatch is 3e. No flight-coordinate bridge was invented.

`make oracle-capture-scenery` regenerates these three local captures;
`make oracle-test` runs their comparisons alongside the phase 3a baselines.
The class is now called by the game loop through shared `MainBus` endpoints.
Phase 3a is committed and merged via PR #4, merge commit `559a2b7`.

## Phase 3b completion: road/scenery setup and hardware integration

`OStageSetup::run` implements these literal main-ROM slices:

| Entry | Effect | Natural capture calls |
| --- | --- | ---: |
| `$13972` | Reset speed/distance, DIP-selected limits and pitch clamps | 5 |
| `$18918` | Copy the ordered stage selector list from runtime ROM | 5 |
| `$DDFE` | Clear bank/pitch state and eight history words | 3 |
| `$13CEE` | Resolve the stage's road/scenery descriptor | 6 |
| `$13D10` | Write normal or alternate ground/sky palettes | 6 |
| `$183DE` | Reset scenery/tile scroll state, tail-call road palette expansion | 1 |
| `$13C3E` | Expand 120 runtime byte selectors into 16-byte road color groups | 6 |
| `$13B1C` | Copy the master palette, 232 iterations of eight longs | 4 |

**36 of 36 natural setup calls identical, 0 skipped**. Four windows with
writes outside the selected routine (including interruptions) were discarded
by capture, not reported as matches. A separate directed run forces the
alternate ground-palette branch: **36 of 36 identical, 0 skipped**. A third
run invokes ROM commands `$E062`/`$E074` with MAME's debugger PC import and a
return address on the active stack: 30 calls to each command plus setup,
**96 of 96 identical, 0 skipped**. This is directed routine coverage, not
whole-credit playback; it can restart a partially executed host instruction.
Total across those three captures: **168 of 168 identical, 0 skipped**.
Changing the expansion loop from 120 to 119 makes seven natural setup
records differ; reverted before final checks.

The MAME core exposes user A7 as `USP`, supervisor A7 as `SP`, and no named
`A7` debugger register. Select with SR bit `$2000`. The scenery captures were
regenerated using this selection and retain **4,137/4,400 identical, 263
input-race skips**. Start CLR routines on the first data read: CLR.L writes
its low word before its high word, so starting on a high-word write misses
part of the transaction. Scenery and setup end on the outer RTS stack read,
not prefetch beyond a loop. RAM, write sets and I/O events are compared.

Sub-road additions:

- `$5C4` blanks both line-selector tables; `$5EE` initializes 224 road-0
  texture selectors. **34 of 34 real calls identical, 0 skipped**.
- `$436..$44A` shifts the two request words and calls those helpers on carry.
  **866 of 868 request windows identical, 2 skipped for main CPU writes to
  inputs**. Explicit other-CPU write sets catch even writes restored before
  the window ends. Reset-interrupted windows are excluded by the capture.
- `$16D4/$1758` initialize the second road, compute perspective/lateral
  fixed-point ramps, select texture transitions from ROM `$19E8`, update
  mode bits and advance distance/texture phase. `$1868` is TAS (the decoder
  now excludes TAS from TST); `$1736` drives the signed comparator, wrapping
  lateral position on out-of-range flags.
- **600 of 600 special-road frames identical, 0 skipped**. Directed cases
  cover sequence initialization, mode bits 3/6/7, signed distance around zero
  and the far endpoint `$4FEC0`, altitude boundaries, yaw and comparator
  limits. Inputs are explicitly injected before the actual MAME routine;
  no ROM tables or captured state are embedded in the C++ port. A changed
  special-road fill word makes all 600 records fail, then is reverted.

`OEngine` owns `MainBus::Memory` and its shared sub bus. Tile/text/palette
transactions go through video endpoints. A road swap copies the newly
latched MainBus buffer into HWRoad's CPU half and triggers HWRoad's swap;
the following generator writes prepare the next frame. Each host frame
processes road requests, latches/control-writes, runs one road frame and
calls the scenery engine. `Screen` always uses hardware road/tile/text/sprite
compositing. Deleted: `ORoad`, `draw_stage1`, native gradients, canopy/rock
terrain sprites, camera projection and speed-gauge overlay.

The host bootstraps the retained native Stage 1 experience at ordinary raw
selector `$10`, using the verified setup slices and ROM-derived gameplay
flags/player altitude. This is a host-selected subsystem start, not a port
of the complete reset/credit/stage dispatcher. Flight controls remain native;
no conversion of native degrees into arcade attitude RAM is guessed. The
integration test sets `$29C0CE` directly and verifies road changes while text
stays stable. Palette/object/HUD/sound scheduling and full sprites remain
3c–3e work. The special road can be driven by its real bus state, but a full
carrier sequence still requires those object/game-flow ports.

Unverified: stage descriptor entries beyond captured stages, every combination
of special-road yaw/distance/altitude, complete reset/credit paths, whole-frame
CPU scheduling and rendered gameplay equality. The existing normal-road,
sprite, ADC, compare/timer and video oracle baselines remain passing. Make
build, engine/bus regression checks and Stage 1 combat/restart/audio integration
pass. CMake targets are updated, but CMake is unavailable on this host.
Captures/ROMs remain local and ignored. `make oracle-capture-road-setup`
regenerates the new captures; `make oracle-test` includes them.

Phase 3c implementation/results follow below. The completed plan is recorded
in [PHASE3C_PLAN.md](https://github.com/duduke/AfterBurner2-Recomp/blob/1f584d75aef22bf6e487a502235bc61065738e94/docs/PHASE3C_PLAN.md); next work is [PHASE3D_PLAN.md](https://github.com/duduke/AfterBurner2-Recomp/blob/1f584d75aef22bf6e487a502235bc61065738e94/docs/PHASE3D_PLAN.md).

## Open threads to pick up next

- **Phase 3d**: capture the real object dispatcher/initializer, enumerate its
  runtime handler table, and replace native object state/flight/input. The
  sprite/frame/palette foundation is now implemented; handler coverage and
  whole-game behavior still require their own oracles.
- Find what sets `$0CDD07`'s trigger bit (the palette fade effect's
  activation) and what reads/sets `$8C2A`/`$8D30` (the palette-animation
  frame counter and palette-set selector).
- **RESOLVED**: Confirm VBLANK subsystem hooks: `$016C8A` is missile reload / HUD sequencer; `$018444` is stage event / background scenery scroll engine.
- **RESOLVED**: Map object dispatch table and structure: 254 entries × 64 bytes (`$9FC0`–`$DF40`), active player jet compound slots 0..3, 3D enemy world-to-screen pipeline mapped.
- **RESOLVED**: Sprite pipeline & Road / Ground-scroll generation mapped end-to-end.
- **RESOLVED**: Main ↔ Sub boot handshake and mutex mechanisms.

## Phase 3c — frame, sprite and palette ports

`OMainFrame::interrupt` translates `$014674..$0147E6`: BSET/BCLR mutex,
word counter with the signed-overflow-aware BGE phase, exact four pointer-pair
swap order, one-shot palette flags, callee order, lamp write, normal/interpolated
blit selection and service redirect. It returns an explicit redirect to the
future `$01A130` service dispatcher; it does not synthesize CPU stack/register
execution. The small callees `$0148B8` bookkeeping, `$0148BE` timers, `$01495C` buttons,
`$0147E8` service edge and `$014856` main-loop wait/re-arm are separate ports.
Waiting is exposed to the host instead of spinning a C++ thread.

`OMainSprites` translates `$00B628` (signed `$C0` limit, four longword copies,
then the object +20 size-slot byte replacing request +12), `$00B676`
publication, and `$00B7C2/$00B7D2` hardware blits. DBF copies **count + 1**
records, including the terminator; interpolation selects `+$C00`. The reset
pointer slice `$013832..$013882` is captured separately before TRAP #10.
Sprite RAM writes and draw triggers use `MainBus` video endpoints, keeping
bus and hardware CPU/latched halves in the same swap order.

`OMainPalette` replaces `OPalette`: `$014A86` trigger/capture/upload/fade,
`$013F92` animation with runtime pointer tables, `$0142DC` blend coefficients,
`$014424` color upload, `$013E5C` transition sequencing, and requested palette
helpers `$013BB6`, `$013BCC`, `$013BFA`, `$01329E`. No native palette cadence,
synthetic flame colors or embedded ROM tables remain. Red/green coefficient
continuation reads the **high byte** at `-2(A0)`, whereas blue shifts the full
word; this distinction is tested against MAME.

### Captures, coverage and test limits

`make oracle-capture-frame-sprites` regenerates the local references.
`make oracle-test` includes all of them, plus every existing baseline:

| Check | Identical / captured | Skipped |
| --- | ---: | ---: |
| Main sprite routines, natural | 600 / 600 | 0 |
| Main sprite routines, directed | 600 / 600 | 0 |
| Reset pointer slice | 1 / 1 | 0 |
| Palette/small scheduler callees, natural | 600 / 600 | 0 |
| Palette/small scheduler callees, directed | 1,600 / 1,600 | 0 |
| Interrupt scheduler, natural | 60 / 120 | 60 concurrent sub-input writes |
| Interrupt scheduler, directed | 120 / 120 | 0 |
| Sub sprite pass, directed write sets | 240 / 240 | 0 |
| Paired sub transform/main blit/draw/video | 120 / 120 | 0 |

The interrupt oracle compares the handler's own writes/I/O and exact callee
sequence. Captured callee RAM changes are replayed **only as external input
boundaries**, with tracing disabled: they are not credited as translated
handler behavior. Palette, small scheduler, scenery and sprite callees have
independent real execution/write-set tests. `$016C8A` missile/HUD/audio,
`$017146` attract/highscore and `$00C6F8` coin handling remain explicit 3d/3e
callbacks; host bindings currently leave them inactive. `$014990`/`$00E2DA`
are RTS-only. Interrupt stack/register preservation, instruction-cycle
interleaving and full credit/attract/service dispatch are outside this test.
Directed frame inputs hold the sub pause mutex, exercise all requested palette
flags, counter `$8000` overflow, both display phases and a service redirect.

Directed main sprite inputs exercise counts 0, `$BF`, `$C0`, `$C1` and both
blits, including empty and capacity-boundary lists. Sub captures use real ROM
frame pointers and test skip-sort, zero zoom/empty lists, 40 equal-depth
requests spilling across buckets (including the last depth), far-left and
vertical clipping, and the combined top/bottom path. Their newer format logs
CPU-owned write sets and detects main writes to actual read inputs. Negative
append counts, arbitrary malformed pointers/records, every tiny-zoom table
entry and every combination of clip/flip/interpolation flags are not covered.
Natural legacy sub baselines continue to pass (1,199/1,200 attract and
1,198/1,200 gameplay; remaining records are input races).

Directed palettes cover all eight fade-channel masks, trigger, capture banks,
active/terminal/idle fade, animation suppression, blend initialization and
terminal continuation, both upload destinations, both palette-cycle tables
and its no-op selector. The wait/re-arm capture includes ordinary `$FFFF`
and a missed-phase `$FFFE` input; bookkeeping includes a 32-bit wrap input. The service-edge routine matches natural
calls, but its switch-release transition is not exercised; whole service UI
and arbitrary negative/out-of-table animation selectors are unverified.
Two interrupted directed palette windows and 38 natural / 16 directed main
sprite windows were dropped by the capture's PC filter, separately from the listed input-race
skips and accepted records.

The paired capture keeps the sub before-state associated with its output
pointer, snapshots main blit inputs and untouched hardware RAM, and pairs
latched sprites/road/tile state with the subsequent MAME pixels. The test
**computes** the sub descriptors, substitutes those computed output lists
into the later main boundary's other-CPU state, runs the ported blit/draw, then
compares all 4 KiB of latched sprite RAM and all RGB pixels. **60 normal and
60 interpolated frames** are identical. This verifies the implemented
pipeline across captured boundaries, not whole-game object/CPU execution.

`python3 tools/oracle/mutation_check.py` compiles temporary changed copies
under ignored `build/oracle/mutations`, without modifying checked-in source.
It rejects the `$C0->$BF` append bound, eight-to-four fade-bank cycle,
one-to-two interrupt decrement and 30-to-29 bucket capacity. Failure counts
are recorded in its local logs; unmutated oracles pass. This makes the
sensitivity check reproducible without risking a mutated game binary.

### Host cadence and remaining native boundary

The earlier once-per-display road assumption was wrong. Sub `$000556` waits
for the negative main counter, then request processing and sprite transform
run. `$000452` waits while the main counter is positive. The next normal
VBLANK makes it zero, releasing `$000458`/`$000464`/`$000466` and road work.
Captured normal gameplay road and sprite passes advance by **two display
frames** (credit transitions can have short/long windows). The host now
preserves these two phases, road latch-before-generation, main draw-before-sub
transform and once-per-logic object production. This supersedes 3b's host
cadence description above and corrects the original 3c implementation plan.

Native `OLevelObjs` still chooses art, trajectory, combat and sound cues.
`OSprites` is now just its temporary serialized-request adapter: exact
bank/offset/stride/row tuples resolve to real aligned ROM records; linear
zoom inverts `$FE00/scale`; left/top positions convert using the transform's
origins. Native depth truncates to a byte, previous position uses emission-slot
history, and the arcade global priority replaces native per-sprite priority.
Unsupported art/zoom tuples are omitted, not fabricated. This adapter is not
an oracle-verified flight/object port and will disappear in 3d. `OEntry` is
explicitly labelled native state, not the arcade object's byte layout.

Native timers remain unchanged **logic tick counts**: stage clear occurs
at 3,600 logic ticks / 7,200 display frames (120 seconds), and inputs/actions
are delivered on logic ticks. Boot still selects raw flight `$10`, interpolation
and the terminal airframe palette phase; complete startup belongs to 3e.
HUD remains native. Flight input still does not drive arcade road attitude.
Make build, engine/bus checks, Stage 1 combat/restart/audio integration, the
full oracle suite, rendered-scene generation and headless launch pass.
CMake targets are updated but CMake remains unavailable on this host.
No ROM data or captures are committed. Next: [PHASE3D_PLAN.md](https://github.com/duduke/AfterBurner2-Recomp/blob/1f584d75aef22bf6e487a502235bc61065738e94/docs/PHASE3D_PLAN.md).


## Phase 3d — verified foundations; phase remains incomplete

`OMainObjects` ports `$01488E` initialization, `$014594..$0145B8`
dispatch, `$00C5A8` RNG, and sequence compound setup/initialization/hiding
(`$016976`, `$016892`, `$0169B2`). `OMainSound` ports the command-ring
producer entries `$00B840/$00B84C` and timer-interrupt consumer `$00B7F8`.
They are built and independently verified, but **not wired into OEngine**:
untranslated live handlers still prevent replacement of `OLevelObjs` and
its sprite adapter. No new gameplay/flight or sound-cadence equivalence is
claimed. See [PHASE3D_PLAN.md](https://github.com/duduke/AfterBurner2-Recomp/blob/1f584d75aef22bf6e487a502235bc61065738e94/docs/PHASE3D_PLAN.md).

The initializer reads the count and template pointers from `$01C176` at
runtime, copies 32 words per record, then overwrites byte +20 with its slot
index. The dispatcher reads bit 7 and each +16 handler pointer live; callee
changes to A5 and the RAM loop counter are preserved. Its termination uses
signed BGT after a word subtraction, including overflow semantics. The
oracle compares all D0..D7/A0..A6 registers at each callback boundary and at
completion, plus the dispatcher's own RAM writes. A7, SR/condition-code
outputs, instruction cycles and interrupt-stack preservation are outside
this port's contract.

RNG `$00C5A8` reads `$8802`, substitutes its ROM immediate when zero, uses
shift/add pairs, then folds the product halves with word ADD and SWAP.
Preserving the upper halves of D0/D1 matters; a native random API or a
simplified integer recurrence would not establish the same caller outputs.
Directed seeds include zero, one, sign-bit-only, all-ones and halfword
boundaries, with independently compared D0/D1 returns and RAM writes.

The sound ring at `$890C` is 64 bytes. `$8908/$890A` are producer/consumer
indices; byte `$8906` is the count; `$8902` enables it. The consumer advances
word `$8900`, emits only on an odd phase with a nonzero count, and writes the
actual main-side 315-5250 sound latch at `$0E8017`. Disabled entries clear
only their own pointer and the count. `$00B84C` suppresses enqueue when
shared selector `$9C` equals 1; `$00B840` does not. No capacity/drop rule is
invented: byte count overflow is retained. These are routine checks, not
proof of host ADC/timer cadence or replacement of native sound events.

### Oracle boundaries and coverage

`make oracle-capture-objects` regenerates the six ignored captures;
`make oracle-test` includes them. Captures compare CPU-owned write sets,
ordered I/O writes, and register contracts, detecting sub CPU writes to
actual port inputs. Dispatcher callback write/register transcripts are
external inputs only: passing that test does **not** verify any unported
handler, ADC sampler interrupt, HUD, stage or coin code inside the callback.
Interrupt writes during a callback are included in that external boundary;
interrupts between callbacks discard the window. Initializer copies and
isolated sequence initialization mask IRQs temporarily; their original mask
is restored with the routine's current condition flags preserved. These
captures do not claim original instruction interleaving or a full credit.

| Capture | Identical / captured | Skipped |
| --- | ---: | ---: |
| Attract dispatcher/initializer | 120 / 120 | 0 |
| Gameplay dispatcher/initializer | 240 / 240 | 0 |
| Directed dispatcher/initializer | 120 / 120 | 0 |
| Natural boot/attract RNG/sound leaves | 600 / 600 | 0 |
| Directed RNG/sound leaves | 600 / 600 | 0 |
| Isolated compound/RNG/sound calls | 168 / 168 | 0 |

Total: **1,848 of 1,848 identical, 0 skipped**. The dispatcher rows include
seven initializer executions and 473 completed dispatch loops. The isolated
row contains 24 executions each of initialization, setup, hiding, both sound
producer entries, RNG and sound consumer. Natural leaf capture ends before
the scripted coin/start pulses, so it is boot/attract coverage. Separately,
53 attract and 87 gameplay windows were dropped for interrupts between
callbacks; each natural/directed leaf capture dropped one interrupted window.
Directed dispatcher and isolated-call captures dropped none.

The directed dispatcher uses real RTS-only `$0148B6` targets to exercise an
active sentinel that must be skipped, empty tables, last-slot-only activity,
all-active tables and first-slot-only activity. Isolated leaf calls save
interrupted registers, push a real RTS return on the active stack and execute
the original ROM routine. Compound cases vary initial flag bytes, preserve
slot indices, verify attachment offsets/function-pointer changes and restore
A5. The original templates and handler pointer inventory are read at runtime;
no ROM tables or captures are committed.

The reset inventory has 254 templates (229 with a zero handler), with eleven
nonzero handler targets. The test prints their counts and every observed
runtime handler target. This is a reset/observed inventory, **not** a complete
map of every self-transitioning enemy handler. Per-handler complete RAM,
math-register and I/O input capture is still required before those ports.

Uncovered foundation paths: malformed/out-of-ROM templates, zero/negative
ROM counts, arbitrary handler changes to loop count/A5, every possible RNG
seed/period, malformed sound indices/counts and arbitrary compound bases.
The four directed dispatcher cases do not exercise every handler transition.
SR output flags and full interrupt timing remain outside this oracle.

This foundation checkpoint predates the independent handler ports described
below. The remaining 3d work is tracked in [historical PHASE3D PLAN](https://github.com/duduke/AfterBurner2-Recomp/blob/1f584d75aef22bf6e487a502235bc61065738e94/docs/PHASE3D_PLAN.md); Phase 3e
has not started.


Eight new source mutations are rejected: dispatcher active mask (88 failed
records), initializer size-index address (1), RNG shift (221), compound
initial palette (24), compound base offset (24), compound hide count (24),
sound enqueue count increment (39) and sound consumer ring mask (69).
The original four 3c mutations continue to fail. The consumer mask mutation
initially survived the endpoint-only fixtures; adding index 31 alongside
index 63 exposed it. All unmodified captures pass after regeneration.

`make -j4`, `make test`, `make verify-stage1`, the complete `make oracle-test`
suite and `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./afterburner2 --frames 180`
pass. Existing oracle mismatches/skips did not increase. CMake targets are
updated; this host still lacks CMake, so those targets were not executed.


## Phase 3d independent handler ports (host replacement pending)

`OMainInput`, `OMainPlayer`, `OMainWeapons` and `OMainWorld` now execute the
following original routines through `MainBus`. They are compiled into the
project and regression targets, but the game still uses `OLevelObjs` and the
native sprite producer. Phase 3d is not complete. PR #7 delivers this verified
checkpoint; the user authorized merging it and resuming the continuation plan
in a different session. This checkpoint merge does not satisfy the remaining
Phase 3d acceptance items.

- Input `$B0AC..$B4E0`: raw stick deadbands/clamps, chip divisions, throttle,
  forced bank/pitch, production attract stream and dormant debug recording.
  `$14976` samples gameplay button edges at its 30 Hz caller; it is distinct
  from the existing `$1495C` display-frame button sampler.
- Live player `$9810/$98C0`, projection `$9AE2`, request append `$B628`,
  shadow `$9C34/$9C9E`, exhaust `$AE8C/$AEE4/$AF08/$AF3C/$AFCC/$B05C`, recovery
  `$9626/$964C`, sequence updates `$1653A/$1656A/$16572/$1657A` and projection
  `$1678E`. The live fuselage is slot 131 at `$C0C0`; sequence setup uses a
  separate compound base. `$9626` was previously misclassified as an enemy.
- Weapon controller `$9D2C/$9D56`, button/cadence gates `$9DF8/$9E50`,
  allocations `$9E82/$9ED8`, pools `$9F08/$9F22/$9F44/$9F5E`, missile launch
  `$9F7C`, guided/free missile `$A234/$A382/$A53A`, trail allocation/init/update
  `$A9F2/$AA32/$AAEE`, vulcan `$A030/$AC4C`, removals `$AE1C/$AE2E/$AE32/$AE50`,
  refill `$16C8A`, missile HUD `$CFC4` and decimal helper `$D062`.
- World `$88C2/$8AF2`, explosion `$91C0`, free-list removal `$9556`, effects
  `$8D44/$8D58/$8E18/$8ECE`, and scaled player collision `$8CC8`.
- Enemy missile `$153E0/$154B8/$1552C/$15582/$15638/$156CE` and its translated
  target, movement, scale, projection, frame, sweep-collision, trail-ring and
  removal helpers `$14DC4/$14DF0/$14E14/$14E1E/$14FF2/$14FF4/$150C0/$1518A/
  $151B8/$15200/$1528A/$152A8/$15750/$1578C/$157BA/$157E8/$157F2/$1581A/
  $1583E/$15842`. Paired shadows `$1500C/$15026` and trails `$14EC8/$14EE2`
  are included. Other enemy missile families remain unported.

All frame/animation/trajectory tables are read from the user's ROM at runtime.
Guided and free missile jump-table displacements select explicit translated
C++ branches; no opcode interpreter runs inside these ports.

### Handler oracle contract

`capture_handlers.lua` records complete mapped RAM before/after, D0..D7 and
A0..A6, both CPUs' multiplier/divider inputs, the observed IO1 button port,
CPU-owned byte write sets and ordered RAM/chip/I/O writes. `AB2HND02` adds the
IO1 byte to the older capture format. `oracle_handlers.cpp` executes the C++
handler independently; it does not apply captured callback writes. Main math
outputs, register returns, own write sets, final written bytes and ordered
writes must match. Results include per-entry identical/different/skipped counts,
so a fully raced handler cannot disappear into an aggregate pass. Sub math state is an input, not a concurrent-output claim.

Only actual input races are skipped: the sub CPU must have written a byte the
C++ handler read. Main IRQ writes reject the window; active work-RAM stacks
are excluded using the actual active SP in delayed fixtures. A7, SR/condition-code outputs, instruction cycles and instruction
interleaving remain outside the routine contract. Isolated calls save and
restore fixture RAM/registers and temporarily mask main IRQs. They establish
routine coverage, not a natural stage/spawn caller or full-credit equality.

`$15750` deliberately discards its caller's return address on early exits.
Injecting that helper directly bypassed the artificial return and captured
unrelated execution; that invalid fixture was removed. Its exit paths are
checked inside the actual translated missile callers. Nested RTS instructions
finish an isolated capture only at the injected stack depth.

Ordered-write tests found that `CLR.L` and memory `ADD.L` write the low word
first on the 68000, while `MOVE.L` writes the high word first. Final-RAM-only
comparison missed this distinction. Expanded signed depth fixtures also
exposed an incorrect BLE interpretation after an overflowing word subtraction
in missile tracking; the port now preserves that signed branch behavior.

| Local capture | Identical / captured | Input-race skips |
| --- | ---: | ---: |
| `input` | 240 / 240 | 0 |
| `input_gameplay` | 360 / 360 | 0 |
| `input_directed` | 128 / 128 | 0 |
| `player_init` | 1 / 1 | 0 |
| `player` | 360 / 360 | 0 |
| `player_directed` | 128 / 128 | 0 |
| `player_parts` | 720 / 720 | 0 |
| `player_parts_gameplay` | 720 / 720 | 0 |
| `player_parts_directed` | 260 / 260 | 0 |
| `reload` | 240 / 240 | 0 |
| `reload_directed` | 210 / 210 | 0 |
| `weapon_control` | 480 / 480 | 0 |
| `weapon_control_directed` | 376 / 384 | 8 |
| `world` | 468 / 480 | 12 |
| `world_directed` | 248 / 256 | 8 |
| `buttons` | 240 / 240 | 0 |
| `buttons_directed` | 64 / 64 | 0 |
| `projectiles` | 480 / 480 | 0 |
| `projectiles_directed` | 384 / 384 | 0 |
| `missiles` | 720 / 720 | 0 |
| `missiles_directed` | 375 / 448 | 73 |
| `effects_directed` | 308 / 320 | 12 |
| `collision_directed` | 64 / 64 | 0 |
| `enemy_weapons_directed` | 1452 / 1600 | 148 |
| `enemy_parts_directed` | 246 / 256 | 10 |
| `enemy_effects` | 480 / 480 | 0 |
| `enemy_effects_gameplay` | 480 / 480 | 0 |

Total: **10,232 of 10,503 identical, 271 skipped, 0 different**. Boot/attract
`enemy_effects` has 480 calls covering enemy shadows/trails. Its gameplay
counterpart has 480 calls including the real `$153E0..$156CE` missile graph
and ground splash `$8D58/$8ECE`. Directed enemy fixtures cover 64 calls per
selected entry. They are labelled routine fixtures because stage/spawn flow
is not ported. The collision fixture includes strict width/height edges and
depth intervals; generic world fixtures alone did not prove hit coverage.

### ADC conversion timeline

The standalone ADC0804 model uses MAME's configured 1.25 MHz clock and 74
conversion clocks (59,200 ns). Conversion completion samples the selected
analog lane. Reads acknowledge INTR; busy writes clear INTR without restarting
conversion. The oracle advances the ADC alongside the already verified
315-5250 timer and compares observed reads, mux selection and timer IRQ.

`adc_timing`: **15,530 / 15,530 identical, 0 skipped**, including 3,529
completed conversions/reads. `adc_timing_directed`: **10,099 / 10,099
identical, 0 skipped**, including 2,322 conversions and 2,334 reads. Probes
exercise all six sampled channels, reads before/after completion, busy writes
and mux changes during conversion. MAME exposes no saved internal ADC state
in this build; internal result/INTR snapshots are not independently compared.
The evidence establishes observable conversion/read behavior, not the game's
host IRQ/ADC caller or input-to-render cadence. Host ADC integration remains
pending, as does live sound-consumer scheduling at `$1461C/$14626`.

### Remaining coverage and acceptance

Uncovered paths are kept explicit:

- Input: dormant ROM debug-switch recording branches are translated but not
  exercised by the production ROM; arbitrary attract/debug stream exhaustion
  and every possible accumulator overflow are not exhaustively covered.
- Player: damage/debris, companion and carrier controllers now have separate
  checks below. Refuel controller `$16236/$E224`, later credit/lives callers
  and host death/recovery remain unchecked; part fixtures do not prove them.
- Player weapons: the six guided and ten free-flight targets reachable from
  the production jump tables are translated. Arbitrary malformed indices,
  every target trajectory/velocity combination, pool corruption and repeated
  exhaustion over a full credit are not covered.
- Enemy/world: the remaining horizon/world/tracking/burst/debris graphs now
  have independent checks (see the latest handler continuation below).
  `$10E6A` is a horizon strip, not a plane/formation. Stage formation/spawn
  dispatch and the projectile-hit score caller `$E542` remain Phase 3e.
- ADC/sound: no host conversion/IRQ wiring or command cadence equality is
  claimed. The native sound cues still run.
- Host acceptance: native objects/flight/weapons and the history/coordinate
  adapter remain. A paired 30/60 Hz player/weapon RAM, road-target, draw/latch
  and rendered-RGB test is still required before replacement or merging 3d.

`make test`, `make verify-stage1` and the complete `make oracle-test` pass.
The existing Stage 1 audio RMS is 903.949. CMake definitions are updated;
CMake is unavailable on this host, so those definitions are not executed.

The initial isolated vulcan initializer fixtures all raced the sub CPU; the
natural initializer calls passed, but aggregate directed totals concealed the
coverage gap. Per-entry counts exposed it. Scheduling that artificial fixture
2 ms after the frame notifier avoids the race while leaving the sub CPU live:
all 64 calls for each of the six projectile entries now match with no skips.
This is a routine fixture phase, not a claim about original caller cadence.
The delayed calls also exercise stack pointers below the usual `$FE00` window;
stack exclusion follows MAME's active SP rather than treating return-address
pushes as object writes.

All **33 deliberate source mutations** are rejected (four previous 3c,
eight foundations, and 21 input/player/weapon/world/ADC mutations). New cases
cover deadband, live/sequence projection, exhaust selection, recovery, reload
sound, missile reserve/launch/guidance/hit, vulcan aim, trail width, enemy zoom,
sway/shadow, ground noise, collision damage and ADC duration. The vulcan
mutation now fails all 64 directed initializer records. Collision mutation
fails four hit records. Mutation logs and temporary variants remain ignored.
The build and 180-frame dummy-video/audio headless launch also pass.


## Phase 3d continuation: damage/debris callees (host replacement pending)

`OMainDamage` translates nineteen callees below the live `$E662` damage
entry through `MainBus`. The damage controller itself is still unported.
No native object, flight, combat or sound producer has been removed.

- `$E744` pops four free-list indices from `$8E7F`, activates the debris
  records and loads five trajectory pointers (fuselage plus four parts)
  from the runtime ROM table at `$E89E`. RNG chooses one of two orientations.
- `$E7E0` converts the live screen position into the damage coordinate
  system, using the divider, multiplier and runtime sine/cosine table.
- `$EBD2/$EBF6/$EC1A/$EC3E` wait for damage tick four and enter `$F766`.
  `$F766` reads sixteen-byte animation records from ROM, handles two-byte
  delay markers and end markers, computes depth/scale and projects debris.
  The first projection seeds previous position without emitting a request;
  subsequent projections call the already checked `$B628`. Memory long
  additions preserve the 68000's low-word-first write order.
- `$F85A` removes a part and returns its slot to the same free list.
- `$F878` projects damage coordinates and slews animation bank by at most
  96 per call. `$F93E` publishes the five player position/depth words.
- `$F58E` accelerates angular motion to +/-12; `$F95E/$F966` add RNG jitter;
  `$F99E/$F9CE` update speed; `$F9E2/$FAA0` update bank/pitch and road history.
  `$F9CE` intentionally shifts only the low word of the long MULU result.
- `$EC62` hides scale and requests state 7. `$ECAC` is the recovery
  transition *below* the lives caller at `$EC78`; it changes record handlers
  and resets the shared damage/roll words. It does not decrement lives.
  `$EC78`, its lives/HUD callee `$CF46`, and later credit/game-flow paths
  are named unported Phase 3e boundaries, not native replacements.

`make oracle-capture-damage` generates `damage_directed.bin`: **1,216 of
1,216 identical, 0 skipped, 0 different**, with **64/64 identical for each**
of `$E744`, `$E7E0`, `$EBD2`, `$EBF6`, `$EC1A`, `$EC3E`, `$EC62`, `$ECAC`,
`$F58E`, `$F766`, `$F85A`, `$F878`, `$F93E`, `$F95E`, `$F966`, `$F99E`,
`$F9CE`, `$F9E2` and `$FAA0`. The capture retains complete RAM/register/chip
inputs and independent ordered-write replay; no captured callback writes
are applied to the port. One interrupted capture window was rejected before
recording. All recorded calls were race-free.

Coverage counters report 32 allocations (16 per RNG orientation), 32
insufficient-pool calls, 192 waiting-part calls, 64 animation steps, 48 delay
markers, 8 end markers, 4 disabled removals and 4 draw-only calls. Waiting
handlers cover first-projection history seeding; the direct animation calls
cover subsequent request emission. Other fixtures cover angular limits,
positive/negative bank slew, pitch endpoints, speed word widths, RNG zero-seed
fallback and road-history wrap. Signed depth overflow fixtures exposed a real
port error: `$F7C0` BGE follows ADD.W and tests N==V. Testing only the wrapped
word's sign incorrectly kept negative overflow depth. The corrected port
preserves that branch contract.

The capture now excludes the active call's bounded stack even when it is
outside the usual work-RAM stack window. For leaf/isolated calls it no longer
excludes every work-RAM byte above SP. Damage injection defers when the
interrupted task's stack overlaps the scratch compound or allocated debris
records, so real object writes cannot disappear into stack filtering. The
initial animation fixtures only exercised markers; scanning runtime ROM
records to include actual animation steps made the coverage gap visible and
closed it. Coverage inspection runs before enabling the C++ trace and cannot
add artificial read dependencies to input-race decisions.

Six deliberate mutations fail: debris-handler stride (32 records), animation
pointer step (64), bank slew (40), projection shift (109), pitch limit (24),
and signed depth branch (24). The unmodified port passes the same capture.
Temporary variants and logs stay under ignored `build/oracle/`. Combined with
the earlier checkpoint, 39 deliberate mutations have been rejected.

Uncovered: the `$E662` damage initializer/caller, intact/breakup controllers
`$E8D0/$E94E`, fuselage animation `$F640`, carrier/refuel controllers, life
loss/HUD/credit flow, arbitrary timeline exhaustion, every zoom saturation
combination, corrupted/overflowing free lists and every arithmetic overflow.
A 120-second ordinary gameplay attempt captured no selected waiting/debris
handlers; its incomplete capture is not a passing natural-gameplay result.
Directed routines do not establish a real damage sequence or host cadence.
Input IRQ/ADC wiring, other enemy graphs, sound cadence and paired 30/60 Hz
player/weapon/video acceptance remain required before retiring `OLevelObjs`.

Make/CMake targets include the new source and capture. `make test`,
`make verify-stage1`, `make oracle-test`, the game build and the 180-frame
headless launch pass. Stage 1 audio RMS remains 903.949. CMake is unavailable
on this host; its updated definitions have not been executed. ROMs, binary
images, captures and mutation outputs remain ignored.

## Phase 3d continuation: pre-lives damage controllers and fuselage

`OMainDamage` now translates `$E662`, intact `$E8D0` and breakup `$E94E`
through controller states 0..6, plus `$F640` fuselage animation and the
remaining pre-lives frame/projection helpers `$F3E2/$F43C/$F4BA/$F528/$F5C0`.
These ports remain unwired; no native gameplay rules have been removed.

- `$E662` calls `$137E2` with the runtime damage text string, saves the bank,
  initializes control requests and damage coordinates, increments the hit
  count, and selects intact or breakup using the original selector, first-hit,
  shared flag and RNG branches. Breakup calls the existing sound producer and
  debris allocator, then falls directly into its first controller tick.
- `$E8D0/$E94E` preserve history and the enabled/draw-only gates, increment
  the original word ticks and dispatch through the runtime ROM branch tables.
  Intact states preserve the timed flight, falling/impact and hidden decay
  intervals. Breakup sets the death flag at tick 30, animates positive-depth
  fuselage until tick 60, then decays until tick 75. Both ultimately request
  state 7 through `$EC62`; the next `$EC78/$CF46` lives/HUD call throws a
  named unported Phase 3e boundary. Later states 8..11 are also explicit
  unported boundaries, not native substitutes.
- `$F640` indexes eight-byte fuselage records by `(tick-4)*8`, rather than
  advancing the sixteen-byte debris pointer. Completion sets the original
  object flag. The routine projects/publishes the shared fuselage/debris
  inputs and uses a signed byte sound countdown, RNG and runtime ROM pairs
  to call `$B84C`. SUBQ signed branches preserve overflow semantics.
- `$F528/$F5C0` select frames from runtime bank/pitch/angle tables. The latter
  preserves the long DIVU quotient/remainder before shifting its low word.
  `$F3E2/$F43C/$F4BA` retain the original chip accesses, shared position
  publication and jitter call order. `$137E2` writes paired text tile rows;
  the damage caller's string consists entirely of spaces.

`make oracle-capture-damage` now regenerates two independent write-set captures:

- `damage_directed.bin`: **1,792 of 1,792 identical, 0 skipped, 0 different**,
  **64/64 per entry** across the earlier nineteen routines and nine new entries
  (`$E662/$E8D0/$E94E/$F3E2/$F43C/$F4BA/$F528/$F5C0/$F640`). Each controller
  has eight active calls in every state 0..6, plus disabled and draw-only
  gates. Initializer fixtures produce 8 intact and 56 breakup calls. Five
  controller calls request state 7 without executing its unported caller.
  Fuselage coverage includes 16 waits, 24 animation records, 4 end markers,
  20 already-complete calls and 32 sound-wait/32 sound-select inputs.
- `damage_gameplay.bin`: **120 of 120 identical, 0 skipped, 0 different**,
  from ordinary coin/start/stick/fire input with no RAM fixtures. It contains
  **1/1 `$E662`** and **119/119 `$E8D0`**. Entry states 0..4 are exercised and
  the final record requests state 5. Eight interrupted capture windows were
  dropped before recording. This is a bounded intact damage sequence, not a
  full death/recovery or a natural breakup sequence.

Both retain complete object/shared RAM, entry/return D0..D7/A0..A6, math-chip
state, IO1 input and ordered writes; captured callback writes are never replayed.
Coverage inspection disables tracing so it cannot manufacture race dependencies.
The directed scratch/stack exclusion now covers the new controller/helper range.

Five meaningful mutations fail: fuselage start tick (28 differing calls),
breakup lifetime (4), intact impact height (5), frame divisor (269), and paired
text-row address (64). The unchanged source passes both captures. Two text
palette mutations survived because the damage string only clears spaces and
never reaches the glyph-write branch; this is an explicit coverage limit.
Including the earlier checkpoint, 44 meaningful mutations have been rejected.

Uncovered: the `$137E2` glyph branch and arbitrary strings; natural breakup,
a complete natural death/recovery sequence; all controller arithmetic overflow
combinations; damage companion `$FB08` and its emission/animation graph (the
alternate `$E7DA` companion is NOP/NOP/RTS, identified but not oracle-tested);
controller states 7..11, lives/HUD/credit flow and carrier/refuel controllers.
Runtime frame-table reads outside the fixtures' valid trajectories and arbitrary
zoom/sound combinations are not established. Other enemy graphs, actual host
ADC/IRQ and sound cadence, and paired 30/60 Hz acceptance remain required.

Make and CMake register the bounded gameplay capture alongside the expanded
routine capture. `make test`, `make verify-stage1`, `make oracle-test`, the game
build and the 180-frame headless launch pass; Stage 1 audio RMS remains 903.949.
CMake is unavailable on this host, so its updated test list is unexecuted.
ROMs, disassembly, captures and mutation variants remain ignored.

## Phase 3d continuation: damage companion emission graph

`OMainDamage` now translates the complete companion graph assigned by `$E662`,
including both allocation branches and their runtime transitions. These routines
remain unwired; native gameplay and the request adapter are still present.

- `$E7DA` and `$FBD4` are independently checked NOP/NOP/RTS leaves.
- `$FB08/$FFBE` pop one emission-pool slot and assign `$FFE6`; `$FB08` then
  assigns `$FB16` to the companion controller. `$FB16` preserves enabled and
  draw-only gates, damage-state thresholds and the speed/roll-dependent word
  doubling sequence. Unsigned division by `$600` keeps the remainder in the
  companion and uses the quotient to request a `$FBDA` allocation via `$FB9C`.
  Empty pools return without allocating. State six stops emission; state seven
  enters `$FBC4`.
- `$FBDA` seeds published damage world coordinates, projects both axes with
  the main multiplier/divider and enters `$FCB4`. That handler moves depth,
  ramps scale during its first eight ticks, uses the runtime lateral table,
  projects/clips and emits requests. State five switches to the runtime
  `$2BC34` animation through `$FDA6/$FDFA`, preserving alternating sound order.
- `$FFE6/$1003E` seed and animate at published screen coordinates, then wait
  at `$100DC` for damage state two. `$100F4/$10148` start the next animation
  and wait at `$101E6` for state four. `$101FE/$10252` follow published screen
  coordinates until state five removes the record. Initializers use runtime
  ROM frame/palette/duration/scale records and the checked `$B84C` producer;
  updates use the actual parent object depth and main multiplier.
- `$FF08` can call `$102F2` from horizontal clipping and return to its caller;
  the caller still executes its subsequent vertical/animation logic, as the
  ROM does. `$102F2` clears depth/zoom/active and returns the slot to the pool.
  Animation duration SUBQ/BGT preserves signed overflow rather than testing
  only the wrapped result's sign.
- `$FBC4/$10310` clear active bits and rebuild the pool using the ROM-defined
  start/count, clear `$8D4A`, and assign `$FBD4`. The callee has no lives/HUD
  or credit call; it belongs to the companion graph. Actual `$EC78/$CF46`
  and later credit boundaries remain unported Phase 3e flow.

`make oracle-capture-companion` regenerates two independent execution captures:

- `companion_directed.bin`: **1,408 of 1,408 identical, 0 different, 0 skipped**,
  **64/64 per entry** for `$E7DA`, `$FB08`, `$FB16`, `$FB9C`, `$FBC4`, `$FBD4`,
  `$FBDA`, `$FCB4`, `$FDA6`, `$FDFA`, `$FF08`, `$FFBE`, `$FFE6`, `$1003E`,
  `$100DC`, `$100F4`, `$10148`, `$101E6`, `$101FE`, `$10252`, `$102F2`, `$10310`.
  No interrupted windows were dropped. Fixtures cover enabled/draw-only gates,
  emitter states 0..7, empty/available pools, depth and position endpoints,
  animation wait/advance including signed SUBQ overflow, time thresholds,
  horizontal/vertical clipping, sound parity and ROM-defined pool cleanup.
- `companion_gameplay.bin`: **2,400 of 2,400 identical, 0 different, 0 skipped**,
  ordinary coin/start/stick/fire input with no RAM fixtures. Per-entry counts:
  `$FB08` 2, `$FB16` 228, `$FBDA` 125, `$FCB4` 1,464, `$FDFA` 430, `$FFE6` 2,
  `$1003E` 18, `$100DC` 60, `$10148` 17, `$101E6` 37, `$10252` 17. It exercises
  emitter entry states 0..7, animation transitions, removals, one pool-reset/idle
  transition inside `$FB16`, and the next initialization. Seventy interrupted
  windows were dropped before recording, separately from input-race skips.
  This verifies companion routine calls during a natural damage sequence, not
  independent execution of the parent lives/death/recovery graph or host cadence.

The captures retain complete RAM, D0..D7/A0..A6, both CPUs' math-chip inputs,
IO1 input, CPU-owned write sets and ordered RAM/chip/I/O writes. Captured writes
are not replayed. Directed calls defer if the interrupted stack overlaps the
scratch compound or any part of the complete emission pool; all ROM data and
captures stay ignored. Coverage inspection runs before tracing is enabled.

Six deliberate mutations fail: emission divisor (25 differing calls), allocated
handler (96), first animation duration (8), pool start index (136), projection
scale (215), and signed animation-duration branch (42). The unchanged source
passes; with the previous checkpoints, 50 meaningful mutations are rejected.

Uncovered: arbitrary animation-table exhaustion, corrupted/overflowing free
lists, all arithmetic/zoom saturation combinations, natural breakup's `$E7DA`
leaf, arbitrary parent depth/frame combinations, and host command/IRQ cadence.
Natural gameplay does not independently enter every initializer/helper: those
are directed entries or reachable callees of recorded handlers. The parent
controller states 7..11, lives/HUD/credit flow, carrier/refuel graphs, remaining
enemy families and paired 30/60 Hz player/weapon/RGB acceptance remain required.
Phase 3d is incomplete; this graph does not authorize retiring `OLevelObjs`.

Make and CMake register both companion captures. `make test`,
`make verify-stage1`, `make oracle-test`, the game build and the 180-frame
headless launch pass; Stage 1 audio RMS remains 903.949. The full oracle suite
reports no differences, retaining its documented input-race skips. CMake is
unavailable on this host, so its updated definitions remain unexecuted. ROMs,
disassembly, captures and mutation variants remain ignored.


## Phase 3d continuation: remaining independent handler graphs

Delivery checkpoint: [PR #11](https://github.com/duduke/AfterBurner2-Recomp/pull/11),
implementation commit `799884a`. The user explicitly authorized pushing and
merging this checkpoint before Phase 3d host acceptance is complete. Combined
new-graph results are 7,409/7,424 identical, zero differences, 15 sub-input
races skipped; all ten new mutations are rejected. Resume with the input/IRQ
caller, host integration, sound cadence and paired player/weapon acceptance
in [historical PHASE3D PLAN](https://github.com/duduke/AfterBurner2-Recomp/blob/1f584d75aef22bf6e487a502235bc61065738e94/docs/PHASE3D_PLAN.md). These ports remain unwired.

`OMainSequence` translates carrier `$10348..$10E68`: pool initialization,
selector wait, all sixteen runtime controller states, speed/pitch goals,
hull and paired aircraft/exhaust allocation, attachment IDs, projection,
frame changes, previous positions and removal. State dispatch reads branch
entries from ROM; sprite/text/palette/trajectory tables remain ROM reads.
Its terminal state preserves the exact selector increment, missile HUD
request and carrier text clear. This local caller check does not implement
general stage/credit progression.

The reset template `$10E6A` creates fifteen horizon segments, rather than an
enemy formation as earlier notes proposed. `$10EB0/$10ED8/$10FBC` implement
initial projection, shared rotation/strip step, optional drawing, frame
selection and palette. The special player `$11116/$1112C` is gated by a
sequence flag and selector sixteen; its caller, palette/road mode switches
and later states remain in the stage/game-flow inventory for Phase 3e.

`OMainWeapons::enemy` now also translates tracking/retarget
`$1586C/$158A8/$1592A`, all sixteen launch-direction branch entries under
`$15968`, their `$15A6A/$15AB6/$15B0A/$15B6E/$15BFE` handlers, burst
allocation `$15C62..$15CB0`, flight `$15D26/$15D36/$15D9E`, impact `$15E6C`
and `$15F48/$15F74/$16038` rotation/ground-debris paths through `$160A6`.
Existing verified gate, shadow, trail, collision, RNG and removal routines
are reused at their original calls. Byte SUBQ overflow and full-register
MOVEQ results are retained.

`OMainWorld::run` adds `$8946`'s world variant, projectile-hit `$8C1C`, hit
marking `$9046`, explosion setup `$90AC` and gate-explosion
`$9366/$93A0/$94B8`. A successful projectile hit calls score/game-flow
`$E542`. That callee remains explicit: absent a supplied boundary callback,
the port throws. The oracle stops before the ROM's BSR at `$8C9E`, compares
ordered writes, registers and chips through `$9046`, and checks the terminal
request. It does not replay score writes or claim a completed score call.
The new depth fixtures exposed an existing `$8CC8` collision bug: `$8CE2`
uses BLE after SUB.W, including signed overflow. The port now uses Z and
N xor V. A deliberate reversion fails one directed record.

Refuel setup `$161B8`, mechanics `$163C2/$163EC/$1641C/$164DA` and projection
`$169D8..$16B88` are translated and checked independently, including bank
sample-ring writes, comparator-limited alternating movement, signed DIVS,
pitch profile/sentinel selection, frame and palette tables, position history
and sprite requests. Controller `$16236` begins with `$E224` before any
local write, so it throws at that named Phase 3e stage caller. Its later
controller states, bonus/score/input/text/return-to-flight sequence remain
unported. Later damage `$ED90/$EE6A/$EFA6/$F04E` belong to credit/continue
flow, not independent flight mechanics; existing 3e boundaries remain.

| Capture | Identical / captured | Skipped | Scope |
| --- | ---: | ---: | --- |
| `sequence_directed` | 2,235 / 2,240 | 5 | 35 entries × 64 isolated fixtures; carrier states 0..15 via `$103EA` |
| `sequence_gameplay` | 2,400 / 2,400 | 0 | Early carrier setup, controller and paired parts; complete departure unverified |
| `enemy_tracking_directed` | 1,472 / 1,472 | 0 | 23 entries × 64 isolated fixtures; all sixteen launch directions |
| `enemy_tracking_gameplay` | 470 / 480 | 10 | Fresh-NVRAM scripted credit/attract run; bounded launch/tracking/debris calls |
| `world_graph_directed` | 448 / 448 | 0 | Seven entries × 64; four `$E542` boundary prefixes |
| `refuel_parts_directed` | 384 / 384 | 0 | Six entries × 64 isolated fixtures |

The new directed calls inject only at the ROM's `$1408A/$1408C` idle compare
loop, outside object-local stacks. Returning to an arbitrary CURPC could
repeat an RTS and corrupt the suspended caller. After recording, the harness
restores fixture inputs and its own RAM outputs, preserving bytes changed
by the other CPU. Captures remain independent MAME execution; no port output
is used to construct expected writes. Optional `AB2HND03` adds the terminal
caller field; existing `AB2HND01/02` captures remain supported.

Directed coverage includes carrier selector wait/ready and empty/available
pools; horizon enabled/disabled; tracking timer expiry/wait and `$80` SUBQ
overflow; insufficient/available burst pools; all sixteen launch selectors;
eight explosion types; hit pool empty/available and score request; alternating
refuel movement and fixed/scanned pitch profiles. The refuel fixtures split
32 movement updates/32 waits and 21 fixed/43 scanned pitch selections.

Coverage limits: no natural refuel helper or world-variant calls were reached
in the bounded fresh-NVRAM captures (120 seconds); stage/spawn coverage awaits
3e. Directed checks do not establish their live caller cadence. Carrier
natural coverage is early sequence only, not a complete landing/departure.
Every possible overflow, malformed table index, pool corruption, all
explosion types/animation ends, full stage trajectories, death/credit and
refuel sequences remain outside these fixtures. Host ADC/IRQ wiring,
30/60 Hz player/weapon RAM/road/draw/latch/RGB equality and sound command
cadence are still required. Phase 3d remains incomplete and `OLevelObjs`
is retained.

Reproduce directed/natural checks with `make oracle-capture-sequence`,
`make oracle-capture-enemy-tracking`, `make oracle-capture-world-graph` and
`make oracle-capture-refuel-parts`, then `make oracle-handlers`. Mutations use
`tools/oracle/mutation_check.py` and temporary source copies under ignored
`build/oracle/mutations`; production sources are never changed.

Ten new mutations are rejected: carrier speed clamp, attachment ID, horizon
step; tracking byte timer, burst velocity, ground-debris table; projectile-hit
width and collision overflow; refuel speed and attitude slew. Directed races
are five horizon calls; natural enemy races are one `$15BFE` and nine
`$15D9E` calls. These records are skipped only when the sub CPU changed an
actual input read by the port. All other compared records have zero differences.
Natural enemy entries: `$1586C` 5, `$1592A` 100, `$15968` 1, `$15A6A` 1,
`$15AB6` 16, `$15BFE` 19, `$15D9E` 338. This includes only launch selector
4; natural `$158A8/$15E6C` and the other launch directions remain uncovered.

Natural carrier controller entry states are 0..8 (114/20/20/22/23/13/16/6/10
calls); states 9..15 are covered only by directed fixtures. The capture drops
37 interrupted windows before recording its 2,400 completed calls. Natural
enemy capture drops 46 interrupted windows before recording 480 calls; the
replay separately skips ten sub-input races. The directed captures drop no
interrupted windows.

Validation: `make -j4`, `make test`, `make verify-stage1`, the complete
`make oracle-test` and a 180-frame headless launch pass. Stage 1 audio RMS remains 903.949. CMake is unavailable
on this host; its source/test definitions are updated but not executed.

## Phase 3d continuation: comparator IRQ caller and input endpoints

Delivery checkpoint: [PR #12](https://github.com/duduke/AfterBurner2-Recomp/pull/12),
implementation commit `e2f973b`. The user explicitly authorized pushing and
merging this checkpoint before Phase 3d completion. Host scheduling and paired
player/weapon acceptance remain pending.

`main_compare_interrupt` in `engine/omainadc.cpp` translates the observable
`$01461C..$01466A` caller: write D0.W to `$0E8012` to acknowledge IRQ,
save the modified register contract, run `$014626`'s sampler/mux/conversion
start, call `$00B7F8`'s sound consumer, and restore D0..D2/A0..A1. The existing
sampler and consumer ports are reused at their original calls. `$01466C`'s
acknowledgement uses the same word write; its separate L6 entry has no natural
coverage. A7, SR, exception frames and instruction cycles stay outside this
routine port.

`AB2_ORACLE_IRQ=1` extends `capture_adc_timing.lua` with IRQ entry/return
records (`AB2IRQ01`), complete work RAM, D0..D7/A0..A6 and ordered CPU-owned
work-RAM writes. Active stacks are excluded. A main write outside the IRQ or
its consumer discards that interrupted window. Sub CPU code cannot access
main work RAM, so this caller has no shared-input race exclusions.

`tests/oracle_mainirq.cpp` keeps one ADC0804 and one bus/315-5250 alive for
the complete capture, starting from reset. It advances physical timer edges
and conversion completion, then executes the full IRQ port at each recorded
caller boundary. ADC results, sample/mux writes, timer acknowledgement,
sound-latch values and ordered RAM/I/O writes are computed by the port and
compared with MAME. No captured ADC result or sound command is fed back as an
input. External analog input changes, entry work RAM/registers, I/O directions
and **bus timestamps are supplied by MAME**. Entry RAM includes the sound
ring produced by the ROM, so this verifies the consumer's caller and observable
timed output, not ported producers or independent host interrupt latency.

| Capture | Identical / captured | Skipped | Coverage |
| --- | ---: | ---: | --- |
| `mainirq` | 3,529 / 3,529 | 0 | Ordinary fresh-NVRAM input run, 473 sound-latch writes |
| `mainirq_endpoints` | 3,529 / 3,529 | 0 | Physical axis/throttle endpoints and weapon combinations, 473 sound-latch writes |
| `input_endpoints` | 360 / 360 | 0 | Independent `$B0AC` mapped-axis calls |
| `buttons_endpoints` | 360 / 360 | 0 | Independent `$14976` current/previous input bytes |

Each IRQ capture also matches **22,588 / 22,588 hardware timeline events**,
including 3,529 completed conversions; every sample-ring slot is exercised.
There are no interrupted IRQ windows in these captures. The mapped-axis
capture drops eight interrupted windows before recording its 360 calls; the
button capture drops none. Replay skips are zero for all four new captures.

The physical fixture uses MAME's configured X `$20..$E0`, Y `$40..$C0`
(reversed) and throttle `$00..$FF`, with neutral `$80`, press/release and all
four Vulcan/Missile combinations. `$B0AC` coverage includes each lane's
minimum, neutral and maximum. Button transitions observed are 0→1, 1→2,
2→3 and 3→0, plus holds of all four combinations. Every possible transition,
arbitrary ADC channel/ring corruption, nested IRQs and a full credit remain
uncovered. The existing raw-byte directed input fixtures remain separate.

Commands: `make oracle-capture-mainirq`, `make oracle-mainirq` (also included
in `make oracle-test`), and
`python3 tools/oracle/mutation_check.py irqack irqsamplewrap irqsoundring`.
The three temporary-source mutations fail 3,529, 1,176 and 224 IRQ records
respectively. Unmodified captures pass. ROM bytes and captures stay ignored.

Phase 3d remains incomplete. `OEngine` still uses native input/objects and
sound cues. Next: schedule this verified caller in the host, connect verified
objects/flight/weapons, and prove paired 30/60 Hz request/size-table/road/video
and sound-producer acceptance before retiring native code. Named score/lives/
stage boundaries remain Phase 3e.

Validation: `make -j4`, `make test`, `make verify-stage1`, the complete
`make oracle-test` and a 180-frame headless launch pass. Existing capture
results do not regress; Stage 1 audio RMS remains 903.949. CMake definitions
are updated, but CMake remains unavailable on this host.


## Phase 3d continuation: independently generated board timeline

`hwmath/xboard_timeline.hpp` implements the physical scheduling in MAME's
`segaxbd_state::scanline_tick`, `device_reset`, `update_main_irqs` and screen
configuration. Pixel clock is 50 MHz / 8; 400 pixels make a 64,000 ns line,
262 lines make a 16,768,000 ns frame (about 59.637 Hz). The board clocks only
the **main** 315-5250 EXCK with scanline bit zero, asserts both CPUs' VBLANK
on line 223 and clears it on line 224. The main pending level is 2 for timer,
4 for VBLANK and 6 for both; sub VBLANK is level 4. This model generates
both rising and falling clock levels, rather than replaying captured edges.

The fresh MAME screen begins at line 224. Board reset arms line zero at
2,432,000 ns, so the first EXCK rise is at 2,496,000 ns. The capture's phase
header is checked against this independently configured reset phase; it is
never supplied to the scheduler. ADC0804 completion is advanced at its own
59,200 ns deadline, in chronological order with raster events. Analog inputs
and mux changes after completion cannot change an already latched result.

A MAME capture exposed a same-timestamp ordering contract: the frame notifier
at line 224 runs **before** the scanline callback clears VBLANK. The timeline
has `advance_before` for this external input boundary and `advance` for
inclusive hardware delivery. A ROM-free check verifies this split, large
versus small time advances, ADC sampling and combined IRQ levels.

`AB2_ORACLE_BOARD=1` extends the existing ADC timeline as `AB2BRD01`, with
one observed VBLANK byte per event and probes one nanosecond after every
scanline. `tests/oracle_board.cpp` computes the hardware events from reset;
CPU bus transactions, their timestamps and external analog changes remain
inputs. It compares ADC reads, timer IRQ, mux and VBLANK at every record.
This MAME build exposes board IRQ/VBLANK save items but no timer/ADC device
save items: internal counter, EXCK and INTR are not independently observed.
The original IRQ oracle now uses the same timeline and still matches all
7,058/7,058 calls and 45,176/45,176 events, 0 skipped, 946 latch writes.

| Capture | Identical / captured | Skipped | Scanline probes | ADC completions |
| --- | ---: | ---: | ---: | ---: |
| `board` | 251,292 / 251,292 | 0 | 235,762 | 3,529 |
| `board_endpoints` | 251,292 / 251,292 | 0 | 235,762 | 3,529 |
| `board_directed` | 167,261 / 167,261 | 0 | 157,162 | 2,322 |

Combined: **669,845/669,845 identical, zero differences, zero skips**,
628,686 scanline probes and 72 simultaneous timer/VBLANK assertions.
Captures end at the final frame notifier, before that frame's VBLANK clear;
there are 2,400 assertions and 2,397 clears. Directed ADC cases cover busy
writes and mux changes around conversion completion. Endpoint inputs retain
the previous physical-axis/button fixture. Three temporary mutations fail:
a 1 ns error in the line increment causes 208,297 differing events; moving
VBLANK to line 222 causes 900; delivering the frame-notifier edge inclusively
causes 900. Unmodified sources pass.

Commands: `make oracle-capture-board`, `make oracle-board` (included in
`make oracle-test`), and `python3 tools/oracle/mutation_check.py boardclock
boardblank boardorder`. Sources, Make and CMake definitions are updated;
ROMs, probes and capture files remain ignored.

**Limit:** this establishes an independent physical board clock, not an
independently executed CPU host. This board oracle alone does not establish CPU transaction execution or
IRQ entry/return latency; the next section checks the bounded IRQ2/IRQ6
service window. Independent main-program execution, nested IRQs and the
ordering of an ADC completion exactly coincident with a raster callback
remain unverified.


`OEngine` still advances its retained native Stage 1 host on fixed display
steps and does not use this timeline. Do not silently change its refresh or
wire handlers at invented cycle offsets. Next: use the bounded service contract below to connect the verified
ADC/IRQ and object callers and complete paired player/weapon, sound-producer and rendered-RGB
acceptance. Phase 3d remains incomplete.

Validation: `make -j4`, `make test`, `make verify-stage1`, the full
`make oracle-test` and a 180-frame dummy-driver launch pass. Existing oracles
have zero new differences; Stage 1 audio RMS remains 903.949. CMake is not
installed on this host, so its updated targets were not executed.


## Phase 3d continuation: bounded CPU service timing

Delivery: [draft PR #13](https://github.com/duduke/AfterBurner2-Recomp/pull/13)
contains board commit `460b69b` and CPU service commit `f006d98`. This is the
requested combined timing checkpoint; Phase 3d remains incomplete.

`engine/omainirqservice.*` adds a cycle-timed service port for the main
68000's IRQ2/IRQ6 exception entry, ROM `$1461C/$1466C`, the `$B7F8` sound
consumer, and return. It uses the X-Board's 12.5 MHz main CPU clock: one
cycle is 80 ns. Timing comes from MAME's `m68000` bus/internal microcycles
and `vpa_sync/vpa_after`, with each arcade instruction identified by its
ROM PC. No captured per-access timestamp is supplied to this execution.

Exception entry writes the interrupted PC low word before SR and PC high,
reads the selected autovector, and accounts for handler prefetch. CPU-space
IACK synchronizes to the ten-clock E phase, including its one-clock delay
after synchronization. This produces variable entry latency. Handler timing
includes the
48-clock predecrement MOVEM (low words before high), the sampler's absolute
and indexed bus phases, word BCS's taken/untaken timing and sample wrap,
byte read/modify/write prefetch, and the actual mux/ADC-start order. The
20-clock JSR, sound consumer branch paths and latch write, 16-clock RTS,
52-clock postincrement MOVEM (including its extra dummy read), and 20-clock
RTE are all checked. IRQ6 uses its actual acknowledgement-only ROM entry.
Unlike the earlier functional ports, this oracle deliberately includes
exception/JSR/MOVEM stack transactions as CPU-owned outputs.

The service caller supplies the interrupted CPU context and the final bus
transaction of the otherwise unported instruction. Its remaining completion
clocks are computed from the runtime opcode and, for register shifts, the
source count sampled before the shift. Observed tails include register
shifts, long ALU/address arithmetic, EXG and immediate register BTST. The
capture showed why a last prefetch cannot always stand for a completed
instruction: those forms still do internal work afterwards; memory RMW
stores can also follow the final prefetch. This is a bounded caller profile,
not a complete decoder or interpreter for arbitrary main-program code.

`tools/oracle/capture_irq_service.lua` records `AB2SVC02` service windows,
entry registers/SSP/SR, full main work RAM, vector and data/stack transactions,
and post registers/SSP/SR/RAM through RTE's final prefetch. Outside the
window, external CPU device transactions and frame inputs maintain one
continuous board/ADC timeline from reset. `tests/oracle_irq_service.cpp`
compares **predicted** access and return timestamps, PC/address/lane/data,
register restoration and every CPU-written RAM byte. The board model computes the IRQ2/IRQ6 level at the supplied CPU
eligibility boundary; the captured level is only an assertion. ADC samples
and sound commands are computed by the service port; captured before-RAM supplies the
sound ring produced by the unported main program. Sub CPU code cannot access
these work-RAM/stack inputs; no input-race skips are needed.

| Capture | Identical / captured calls | Identical / captured bus events | Skipped | Ported latch writes |
| --- | ---: | ---: | ---: | ---: |
| `service` | 3,556 / 3,556 | 177,767 / 177,767 | 0 | 473 |
| `service_endpoints` | 3,556 / 3,556 | 177,767 / 177,767 | 0 | 473 |
| `service_directed` | 2,327 / 2,327 | 118,490 / 118,490 | 0 | 576 |

Combined: **9,439/9,439 calls and 474,024/474,024 bus events identical,
zero differences, zero skipped**. The corpus includes 9,367 IRQ2 services,
72 IRQ6 services and 1,522 predicted sound-latch writes. The separate
2,541 external hardware/probe records also match. Service duration spans
86..580 clocks, depending on E phase, IRQ entry and consumer branch. All
six sampler slots and wrap are exercised; the directed run has 582 each
of disabled, even-cadence and empty-queue entries, plus ring index 63 to 0
and command emission. Natural and endpoint captures contain ordinary
original execution and the earlier physical-axis/button fixture.

The directed mask fixture executes synthesized RAM `MOVE.W #imm,SR`,
NOP/BRA and restore/JMP instructions. It checks 97 mask probes, including
66 with an IRQ pending while IPL7 prevents service, then observes service
after a real unmask to IPL1. MAME's debugger SR setter does **not** call
`update_interrupt`; using it to stand for an unmask left the request pending
until another line update and was rejected as a fixture. Only the initial
supervisor privilege is selected by the fixture; actual mask/unmask and
mode restoration execute as CPU instructions. Its RAM/queue/PC redirections
are directed inputs and do not establish natural credit progression.

Seven temporary-source mutations fail: exception-entry clocks (27,589
bus events differ), VPA phase (56,573), ADC read timing (81,384), sample
wrap branch timing (12,857), sound-latch timing (12,096), interrupted shift
completion (770), and ignoring the IRQ mask (66 hardware/probe records).
The source is never modified by the mutation runner. Commands:
`make oracle-capture-service`, `make oracle-irq-service` (part of
`make oracle-test`), and `python3 tools/oracle/mutation_check.py serviceentry
servicevpa serviceadc servicewrap servicelatch servicetail servicemask`.

**Unverified boundaries:** the full main CPU loop is not independently
executed. Its last-bus boundary and entry RAM/registers remain supplied; the completion profile covers this corpus, not arbitrary
opcodes, all operand counts or MUL/DIV/STOP/trace/address/bus exceptions.
Only E phases 1/3/5/7/9 occur in these captures. IPL7 masking followed by IPL1
unmasking is checked; every mask transition and a nested higher-priority ISR
are not. Arbitrary IRQ edges during an instruction's internal tail and
level-seven NMI are outside this bounded selection contract. No frame input notifier lands inside a service window in this
corpus; that supported input-boundary path remains unexercised. ADC/raster
completion ties and whole-program instruction scheduling remain separate.
Sound producers and Z80 delivery/waveforms are still outside this check.

`OEngine` remains on the retained native Stage 1 host. The bounded CPU
service timing gap is now checked before gameplay wiring; it does not itself
replace input, flight, weapon, object or sound producers. Next: connect the
verified callers at their established CPU/board boundaries and prove paired
player/weapon, normal/interpolated requests, shared targets, latch/draw events
and rendered RGB before retiring native code. Phase 3d remains incomplete.

Validation for the combined board/service checkpoint: `make -j4`, `make test`,
`make verify-stage1`, the complete `make oracle-test`, all seven new service
mutations and a 180-frame dummy-driver launch pass. Existing oracle results
have no new differences; Stage 1 audio RMS remains 903.949. CMake definitions
are updated but unexecuted because CMake is unavailable. ROM data, generated
RAM fixtures and captures remain ignored.


## Phase 3d continuation: directed host object composition

Delivery: [PR #14](https://github.com/duduke/AfterBurner2-Recomp/pull/14),
implementation `363bce4`, merged on `main` as `a8849a4` at the user's request.
This is a completed composition checkpoint, not Phase 3d completion. The next
continuation starts from updated `main` on a new `codex/` branch and establishes
the live `$B676` register/wait publication contract before the remaining
IRQ/input, producer/latch and paired player/weapon/sub/video acceptance.

`engine/omainhost.*` provides a common entry router and composes the real
`$14594..$145B6` dispatcher with independently checked input, flight,
compound, weapon/projectile, world, damage and sequence ports. Entry lists
are explicit; internal branch labels and unknown pointers cannot fall through
to a guessed implementation. Unknown callers require a supplied boundary or
throw. Existing independent handler oracles now use this same router.
`OEngine` still uses the native Stage 1 producer pending paired acceptance.

The live dispatcher exposed two additional entry contracts. `$B628` routes
to the already translated register-preserving sprite emission port.
`$9D34` enters weapon-indicator setup after the two pool resets at
`$9D2C/$9D30`, then falls through to `$9D56`. `indicator_initialize` shares
that instruction translation with `initialize`; routing `$9D34` to the whole
initializer would reset live projectile pools. The new isolated entry capture
checks both with complete RAM/register/chip/IO inputs and ordered writes.

`tools/oracle/capture_objects.lua` adds opt-in `AB2OBJ02` records: the existing
RAM, dispatcher writes and callback boundaries plus initial math state and
per-callback input port and pre/post math state. Old `AB2OBJ01` captures and
capture modes remain supported. `tests/oracle_host_objects.cpp` executes
verified callbacks, compares entry/return registers, main math chips and all
ordered non-stack RAM/I/O writes, then compares the composed write set and
final dispatcher state. Only explicitly listed unported caller transcripts
are replayed as external inputs; downgrading a verified entry to such a
transcript fails the test. Main chip state and verified writes carry between
handlers without resetting to captured outputs. Sub chip state is a captured
external input; actual shared-memory input races are detected by read sets.

| Capture | Identical / captured windows | Executed verified callbacks | External callbacks | Skipped |
| --- | ---: | ---: | ---: | ---: |
| `host_objects` | 240 / 240 | 26,934 | 2,316 | 0 |
| `host_objects_endpoints` | 240 / 240 | 26,470 | 2,377 | 0 |
| `host_entries_directed` | 128 / 128 isolated calls | 64 each at `$9D34/$B628` | 0 | 0 |

Composition: **480/480 windows identical, 53,404 verified callbacks executed,
zero differences, zero skipped**. Executed callbacks include 480 each of
input `$B0AC`, live flight `$98C0`, shadow `$9C9E`, both exhaust handlers,
and horizon setup `$10EB0`; 200 weapon-controller `$9D56` calls; both
weapon-indicator transitions; missiles, trails, vulcan, ground effects,
world initialization/update and carrier parts. The two fixtures produce
6,720 direct `$B628` emissions. The router's remaining supported entries
retain their separate routine checks; inclusion in the router alone does not
establish their composed caller coverage.

**Directed fixture, not natural cadence:** an unmasked full dispatcher
traversal is routinely interrupted by the comparator IRQ. Dropping every
interrupted window produced no usable composition records. The new fixture
therefore executes synthesized `MOVE.W #$2700,SR; JSR dispatcher` instructions
on live gameplay RAM every second display notifier, restores SR with a real
instruction and resumes the saved program/register context. Initial
supervisor privilege is selected externally; masking itself executes on the
68000. The dispatcher is a main-loop range, not a subroutine: only its final
branch back to `$1458A`, outside the tested range, is replaced by RTS in the
fixture's ROM read tap to return to the synthesized caller. No ROM file is
modified. These extra directed traversals alter the gameplay run's cadence;
they do not prove naturally scheduled 30 Hz execution or a whole credit.
Both runs capture 240 windows with zero interrupted windows dropped.

At the directed checkpoint, the external caller allowlist was `$427C/$6252/$6466/$64D4/$65EC/$65FA/$6844/
$6D18/$71E4/$7BB6/$805A/$B676/$D0DA/$13236/$18E8A`. These outputs are inputs
to this check, not verified implementations. `$B676` already has a Phase 3c
RAM-effect port; its live A0/wait caller contract is still outside this router.
The dispatcher entry RAM/registers and unported callers remain external.
IRQ interleaving, independently scheduled input conversion, VBLANK/reload,
producer-to-latch cadence, sub handshake, paired normal/interpolated lists and
rendered RGB remain required before changing the gameplay host or retiring
`OLevelObjs`. Natural death/recovery, score/lives and refuel/credit flow retain
the previously named coverage limits. Phase 3d remains incomplete.

Commands: `make oracle-capture-host-objects`, `make oracle-host-objects`, and
`python3 tools/oracle/mutation_check.py hostreplay hostindicator hostappend
hostregisters`. The host composition check and isolated entry capture replay
are part of `make oracle-test`; capture data and fixture instructions stay
under ignored `build/oracle/`.


All four temporary-source mutations fail: replaying every handler externally
rejects 240/240 windows, resetting the pools at `$9D34` rejects its 64 isolated
calls, misrouting `$B628` rejects 240/240 windows, and discarding handler
register results rejects 240/240 windows. The mutation runner changes only
ignored temporary copies. Validation: `make -j4`, `make test`,
`make verify-stage1`, the complete `make oracle-test`, and a 180-frame
dummy-driver launch pass. Existing captures have no new differences;
Stage 1 audio RMS stays 903.949. CMake targets are updated but unexecuted
because CMake is unavailable. No ROM data or captures are tracked.

## Conditional natural IRQ and paired acceptance

The `codex/phase3d-irq-acceptance` checkpoint uses one natural MAME gameplay
run without synthesized dispatcher calls or IPL masking. It establishes
composition at supplied operand/caller boundaries, not independent execution
of the main instruction stream. `OEngine` retains native Stage 1 producers.

`$B676` is now executed by the host router rather than replayed: a positive
publication counter leaves A0 and memory unchanged; a ready counter loads A0
from `$2A3C0C`, publishes the request count and clears it. A named external
boundary can progress a wait once; a still-pending counter fails explicitly.
The directed 480/480 host checks now execute this callback. This contract
does not independently schedule the other CPU while waiting.

| Natural capture | Identical / recorded | Differences | Skipped |
| --- | ---: | ---: | ---: |
| Early dispatcher prefixes | 120 / 120 | 0 | 0 |
| Paired-run dispatcher prefixes | 116 / 123 | 0 | 7 sub read-input races |
| Complete VBLANK windows | 160 / 246 | 0 | 86 sub read-input races |
| Continuous sound calls | 1,259 / 1,259 | 0 | 0 |
| Paired player/weapon/sub/video frames | 226 / 240 | 0 | 14 raced-producer frames |

Early accepted prefixes service 324 IRQ2 calls, producing 174 sound-latch
writes. Later accepted prefixes service 381 IRQ2 calls and 177 latch writes;
including raced records, the harness executes 407 IRQ2 and seven IRQ4 calls.
**Every IRQ4-overlapped prefix is raced; IRQ4 interleaving is unverified.**
No IRQ6 occurs inside these prefixes. VBLANK is checked separately:
123 logic/interpolated and 37 normal windows are identical.
The later run executes 14,124 verified callbacks (including raced windows)
and 1,193 explicitly external callbacks. Coverage includes live player,
shadow/exhaust, input, weapon controller, missiles/shadows/trails, vulcan,
world/enemy/carrier handlers and direct sprite emissions. It does not add
natural death/recovery, refuel/credit or full stage-flow coverage.

The dispatcher capture ends before the publication wait. IRQ2 entry registers,
actual ADC result, IRQ operand/write frontiers and chronological sub-owned
scaled-size operands are supplied inputs. Verified handler and ISR writes
carry through without resets to captured outputs. Unported caller transcripts
remain explicit inputs. The capture excludes one window at `$B684` on frame
1233 when its publication frontier cannot be resolved; producer provenance
is invalidated and the corresponding display frames are omitted. Excluded
windows are not included in the identical/skipped denominators.

VBLANK executes palette, scenery, buttons/service edge, bookkeeping/timers,
reload and normal/interpolated sprite ports. Its unported callback allowlist
is `$17146/$14990/$C6F8/$E2DA`. Sub-produced blit lists/counts, the actual
mutex operand at `$14798` and the hidden sprite bank at draw time are supplied
boundary inputs. Other changed bytes actually read cause a race skip.
One incomplete shutdown VBLANK is excluded. Ordered main-owned writes, chip
state, reload registers, final write sets and IRQ I/O are compared.

This exposed two hardware/access details: MAME's sprite draw swap forces the
first CPU-visible word to `$FFFF`; `MainBus` now does the same. MAME's renderer
also modifies hidden-buffer word 7, so its hidden bank is supplied at the draw
boundary rather than claiming the port independently generates that feedback.
The long ADDQ writes in `$148B8/$148D8/$148F8` issue low word before high;
the frame port now preserves that event order. `MainBus::read32` explicitly
reads high then low, and the trace hook can service a captured IRQ before the
operand is sampled. This hook supplies timing; it does not predict it.

The continuous sound test initializes its ring only once and carries it
through 256 producer calls and 1,003 consumer calls, comparing ordered ring
and latch writes, registers, selectors, and final ring. All 256 latch events
match; there are zero external ring writes. Caller timestamps, enqueue D0 and selector are captured
inputs; the sound test does not replace them with the paired object outputs.
This verifies producer-to-consumer command order at those callers,
not independent scheduling, Z80 waveform equality or a full credit.

The paired video test consumes the **computed** dispatcher requests and five
main road target words, checking each against the actual sub input. It then
executes `OSubSprites`, compares its size table at sub-pass completion, and
computes both main blits. It checks producer provenance strictly: an unknown
producer fails; only an explicitly raced producer skips a pair. The 226
accepted frames comprise 113 normal and 113 interpolated lists/draw triggers;
all 226 RGB frames are pixel-identical, zero mismatched pixels. Road raster,
tile and palette state remain captured compositor inputs; a combined
`OSubRoad` raster path is still required before native retirement.

Formats are `AB2OBJ05` (natural prefixes), `AB2REQ03` (computed producer
results), `AB2FRM05` (VBLANK boundaries), `AB2PAIR2` (associated sub/video
frames) and the continuous sound stream. All remain in ignored `build/oracle/`.
`capture_paired.lua` coordinates object, frame, pipeline and sound taps in one
run; publication pointers associate producers with actual sub passes.

```bash
make oracle-capture-host-interleave
make oracle-capture-paired
make oracle-host-interleave
make oracle-paired                 # also included in make oracle-test
python3 tools/oracle/mutation_check.py drawendmarker framewordorder \
  publicationregister interleavedsample pairedproducer pairedblit \
  pairedframe streamsounds
```

All eight mutations are rejected: draw end marker, long-write order,
publication A0 result, IRQ sampled operand, computed request producer, paired
blit, composed frame and continuous sound command. Temporary mutated sources
and results remain ignored. Independent main instruction/caller timing,
IRQ4/shared-input acceptance, publication wait progress, road composition,
unknown stage/credit callers and complete-credit validation remain next.
Phase 3d is **not complete**.

Validation: `make -j4`, `make test`, `make verify-stage1`, the complete
`make oracle-test` (including this acceptance), and a 180-frame dummy-driver
launch pass. Stage 1 audio RMS remains 903.949. CMake definitions are updated
but unexecuted because CMake is unavailable. No ROMs or captures are tracked.


## IRQ4/shared-memory and complete publication composition

The `codex/phase3d-shared-interleave` continuation extends the preceding
conditional checkpoint. This is execution of verified main routines at
captured CPU boundaries, **not independently scheduled gameplay**.

The earlier race skips came from sub-produced projection words (`$29C088`
through `$29C098`, sine/cosine, roll/quadrant, scale, easing and projection
scratch), the `$29C630` bank-history index and ring, and `$29C0D6` mutex/road
readiness. `shared_inputs.lua` records ordered operands with byte-lane
provenance: sub writes set a lane's provenance and subsequent main writes
clear it. `oracle_shared_inputs.hpp` supplies only those sub-written lanes;
other lanes must equal the computed port state. Every allowed read's address,
mask and consumption order is checked. Main-owned road targets, object state,
request pointers/counts and sound-ring results are never supplied this way.
Actual read boundaries remain inputs; the test does not compute when the sub
CPU writes these values. Fields written by both CPUs remain explicit races
outside this captured operand contract.

IRQ4 supplies only its two mutex operands, the sub-produced blit list/count
and hidden sprite bank at draw. It executes the frame selector, pointer swaps,
verified palette/scenery/reload/button/timer/blit callbacks and frame tail.
The sub's road-ready bit 1 can change between mutex acquisition and release;
main lock bit 0 and other flags must retain their computed values. The frame
callback allowlist remains `$17146/$14990/$C6F8/$E2DA`. Ordinary object callers
remain explicitly external, including stage-flow `$18E8A` after publication.

A pending timer/VBLANK combination can produce IRQ6, then IRQ4, then IRQ2
without a main operand between returns. The old capture retained only one
pending return and silently lost earlier resume frontiers. The capture now
queues all pending returns and attaches them to the first genuine resumed
operand. Replay services every interrupt at that frontier before sampling it.
Capture-finalization errors are explicitly surfaced as `[LUA ERROR]`, so
`capture.sh` rejects partial captures instead of accepting silent memory-tap
callback failures. Exception-stack writes with the interrupted CURPC cannot
start a dispatcher window: natural captures start at `$14594`'s real counter
write. The capture closes on signed nonpositive dispatcher counts.

All scripts in the paired run share a debugger-read guard. Without it, one
script's snapshot of shared RAM appeared as another script's CPU operands,
including byte reads attributed to palette and blit callbacks. These are
observer reads, not CPU reads. Last CPU-owned write values are captured and
traced separately from live RAM, because the sub may overwrite a main result
after the main's last write. Ordered handler/ISR write transcripts still pin
intermediate writes; explicit unported main callers can update the final
write-value shadow without becoming verified routines. A ROM-free bus check
proves sub overwrites leave the main write-value trace intact.

`AB2OBJ06` retains prefix captures with operand provenance; `AB2OBJ07` captures
complete dispatchers including `$B676`. `AB2FRM06` adds the same operand
contract to VBLANK. Full publication waits execute captured IRQ2/IRQ4/IRQ6
services, then retry the actual port; there is no replay of publication
outputs. Paired producer requests and five road targets are saved from the
computed state before publication, associated at the actual publication write,
and used by the sub transform and both main blits. The formerly excluded
`$B684` window is covered by the complete capture; no dispatcher is dropped.

| Fresh capture | Identical / recorded | Differences | Skipped |
| --- | ---: | ---: | ---: |
| Early dispatcher prefixes | 120 / 120 | 0 | 0 |
| Separate complete dispatch/publication | 120 / 120 | 0 | 0 |
| Paired complete dispatch/publication | 122 / 122 | 0 | 0 |
| Complete VBLANK windows | 244 / 244 | 0 | 0 |
| Paired player/weapon/sub/video frames | 240 / 240 | 0 | 0 |
| Continuous sound calls | 1,250 / 1,250 | 0 | 0 |

The separate full capture executes 482 IRQ2, 120 IRQ4 and seven IRQ6 services,
214 latch writes and 13,890 verified object callbacks (1,283 explicit external
callbacks). The paired run executes 490 IRQ2, 122 IRQ4 and seven IRQ6 services,
217 ISR latch writes and 14,134 verified object callbacks (1,305 external).
The full dispatcher waits exercise normal-list IRQ4; the standalone VBLANK
capture checks 122 logic/interpolated and 122 normal windows. There are no
excluded dispatchers in either complete capture. One unfinished shutdown
VBLANK remains excluded. The 240 paired frames are 120 normal and 120
interpolated, all pixel-identical with zero mismatched pixels. The continuous
ring checks 255 producers, 995 consumers and 255 latch writes, with no external
ring writes. These sample intervals differ from the earlier prefix run, so
counts are not added to its historical denominators.

Independent scheduling is still unverified. At this checkpoint,
`OMainIrqService` predicted bounded IRQ2/IRQ6 cycles but did not implement
IRQ4/callee cycle timing; the bounded continuation below extends that coverage. The literal
routine ports have no general instruction/prefetch clocks, and unported main
callers are register/write transcripts, not executable CPU schedules. Advancing
an arbitrary frame clock while executing these calls would invent IRQ latency
and operand ordering. Next: port/check IRQ4 bus phases and callee timing,
`$14856` re-arm/caller execution and the interrupted instruction stream, then
independently schedule ADC results, publication and sub progress. Captured
sound caller timestamps, road raster/tile/palette inputs and hidden renderer
feedback remain separate boundaries. Native `OEngine` producers remain active;
Phase 3d is incomplete. Natural death/recovery, refuel/credit, remaining stage
callers and whole-credit coverage are unchanged.

Commands:

```bash
make oracle-capture-host-interleave
make oracle-capture-host-publication
make oracle-capture-paired
make oracle-host-publication
make oracle-paired
python3 tools/oracle/mutation_check.py irq4normalphase sharedhistory publicationcount
```


All three new temporary-source mutations are rejected: IRQ4 normal phase
`$100->$101` fails 122/122 paired dispatchers; bank-history scroll selection
`+4` fails 244/244 VBLANK windows; publication count `+1` fails 120/120
complete dispatchers. The original sources are unchanged by the mutation
runner. Validation: `make -j4`, `make test`, `make verify-stage1`, the complete
`make -j4 oracle-test`, and a 180-frame dummy-driver launch pass. Existing
oracle captures have no new differences; Stage 1 audio RMS remains 903.949.
CMake's test definitions are updated but not executed (CMake is unavailable).
ROMs, flattened images, diagnostic output and captures remain untracked under
ignored build/ or temporary paths.


## Bounded IRQ4 service and logic-wait timing

The `codex/phase3d-irq4-timing` continuation extends `OMainIrqService` through
main ROM `$14674..$147E6`. It independently computes 80 ns bus/internal cycles
for exception entry and E-clock IACK, mutex byte bit RMW, phase selection,
15-register MOVEM, four pointer swaps, conditional palette requests, BSR/JSR
stack phases, lamps, the 58-decrement display delay, conditional draw,
ordinary return and the service redirect to `$1A130` with SR `$0700`.
The `$8000` counter case uses SUBQ's N/V condition, not the sign of the
wrapped result. Memory-to-memory longs write high/low; predecrement register
longs write low/high. BSR's stack writes precede both target prefetches,
unlike absolute JSR. Every block retains its ROM address.

Six callee entries also execute with predicted operand/return cycles and
entry/exit register checks: `$1495C` buttons, `$148BE` timers, `$148B8`
bookkeeping, `$147E8` service-button edge, `$B7C2` normal blit and `$B7D2`
interpolated blit. Blits execute all four longs per descriptor and the
count+1 terminator, preserving the actual DBF and RTS phases. They compute
main-owned pointer loads; the sub-produced list/count are supplied at operand
boundaries. The physical IO1 input byte and sub mutex flags are inputs;
main lock bit 0 must retain its computed value.

**This is a bounded envelope, not a completely independently timed IRQ4.**
Complex palette/scenery/reload/HUD routines and explicit game-flow callbacks
remain supplied state/write transcripts plus elapsed CPU clocks. The boundary
receives the predicted callee start and return stack, and returns a duration;
it never resets the caller to a captured absolute timestamp. Tests assert
that predicted starts and entry registers match, then exclude these external
callee events from the verified-event denominator. Remaining timing entries
are `$14A86/$13F92/$13BCC/$13BB6/$13BFA/$1329E/$16C8A/$18444/$13E5C` and the
existing game-flow boundaries `$17146/$14990/$C6F8/$E2DA`. Missing boundaries
throw rather than inventing latency.

`capture_irq4_service.lua` records `AB2IRQ41` contexts, full mapped RAM,
root and callee data/stack events, callee start/return registers/timestamps,
and final RTE state. ROM prefetches contribute computed cycles, not supplied
per-fetch times. `oracle_irq4_service.cpp` compares PC/address/lane/direction/
data/time for every executed bus event, every timed leaf's entry/exit state,
and final registers/SP/SR/PC/time. Initial CPU completion boundary and IRQ4
selection remain supplied; observed instruction tails use the existing
completion profile. No main instruction interpreter or independent board IRQ
selection is claimed by this test. One unfinished shutdown service per run
is excluded by the capture, separately from accepted record counts.

| IRQ4 capture | Identical / recorded envelopes | Identical / predicted bus events | Skipped |
| --- | ---: | ---: | ---: |
| Natural (600 frames) | 597 / 597 | 1,038,908 / 1,038,908 | 0 |
| Directed (900 frames) | 897 / 897 | 1,475,405 / 1,475,405 | 0 |
| Directed edges (120 frames) | 117 / 117 | 96,819 / 96,819 | 0 |

Combined: **1,611/1,611 envelopes and 2,611,132/2,611,132 executed bus
events identical, zero differences, zero race skips**. The runs execute
5,900 timed leaf calls and supply 13,964 external callee boundaries.
Coverage includes 1,068 logic and 543 normal phases, 253 signed-overflow
counter cases, 543 normal and 941 interpolated blits, 127 skipped draws,
2,039 timer rollovers and two service redirects. Entry E phases 1/3/5/6/7/9
are exercised; phases 0/2/4/8 are not. Directed timer words start at 59;
palette requests are set, display interpolation alternates, frame counters
cycle through 0/1/$8000/$FFFF and the final fixture requests service mode.
The natural run retains ordinary coin/start/weapon inputs. These directed
RAM inputs do not establish natural game-flow or all callee timing.


`logic_step` translates `$14856..$14878` as one uninterrupted spin iteration
or the ready/re-arm/RTS path. It computes TST/BGE, CMPI/BEQ, optional missed
count ADDQ, re-arm, NOP, return stack phases and SR. A taken spin returns
PC `$14856`; it does not synthesize progress or consume a display frame.
`AB2WAIT1` records use the active USP/SSP: MAME's named SP is specifically
the supervisor stack, even while user code runs. IRQ exception writes are
excluded even when CURPC still names the interrupted wait instruction.
Natural nonnegative spins are sampled every 128th iteration; negative ready
paths are captured without sampling. The directed run checks words 0, 1,
`$7FFF/$FFFF/$FFFE/$8000`, missed counts `$FFFF` and 9, and carry-preserving SR.

| Logic-wait capture | Identical / recorded steps | Identical / recorded bus events | Excluded |
| --- | ---: | ---: | --- |
| Natural | 16,151 / 16,151 | 17,319 / 17,319 | 6 IRQ-overlapped steps |
| Directed | 198 / 198 | 726 / 726 | 0 |

Natural coverage is 15,861 sampled spins and 290 ready returns, four with
missed-frame increments. Directed coverage is 99 spins and 99 returns,
66 missed increments and 33 carry wraps. Accepted steps have zero differences
and zero race skips; six IRQ-overlapped attempts are explicitly outside the
step oracle. Neither a full interrupted wait nor the surrounding dispatcher
caller is independently scheduled. Service-button helper edge branches,
nested higher-priority interrupts, arbitrary interrupted opcodes/E phases,
sub execution deadlines and whole-credit game flow remain coverage limits.

Seven temporary-source mutations are rejected: BSR stack phase, signed
counter overflow selection, DBF delay, blit long word order, redirect SR,
short BGE untaken timing and re-arm value. No original source is changed by
the mutation runner. Commands:

```bash
make oracle-capture-irq4-service
make oracle-capture-logic-wait
make oracle-irq4-service
make oracle-logic-wait
python3 tools/oracle/mutation_check.py irq4bsr irq4overflow irq4delay irq4blit irq4redirect waitbranch waitrearm
```

Validation: `make -j4`, `make test`, `make verify-stage1`, the full
`make -j4 oracle-test` and a 180-frame dummy-driver launch pass. Stage 1 audio
RMS remains 903.949; existing paired RGB remains 240/240 pixel-identical.
CMake test definitions are updated but not executed because CMake is
unavailable. ROM images, disassembly output, captures and mutation artifacts
stay ignored. Native `OEngine` remains active and Phase 3d is incomplete.
Next: independently check the remaining IRQ4 callee cycles, then IRQ service
within the re-arm/caller stream and publication/sub progress, retaining the
explicit stage/credit and road-raster/compositor boundaries.

## IRQ4 road-palette timing continuation

`codex/irq4-road-palette-timing` removes three supplied callee timing/state
boundaries from `OMainIrqService`: `$13BB6` copies a full road palette bank,
`$13BCC` copies a byte-selected four-long palette into one of eight masked
destination banks, and `$13BFA` doubles the word stage index, writes one road
color, then repeats that color across 64 words. The timed translations follow
the ROM instructions and the local MAME m68000 bus/cycle implementation.
They preserve D0/D1 word and byte widths, signed indexed addresses, 14-clock
ASL.W #4 and 12-clock indexed LEA, high/low memory MOVE.L ordering, each
postincrement, taken/terminal DBF cycles and RTS stack/prefetch phases.
The repeated-color loop performs every original source read rather than
reusing the first value. Colors remain runtime ROM operands.

`AB2IRQ42` adds those ROM operand reads to the earlier envelope capture;
regenerate the three older IRQ4 datasets with `make oracle-capture-irq4-service`.
For the operand MOVE instructions, Lua excludes only the p+4 instruction
prefetch. The test rejects any executed operand that aliases that excluded
address. Every road-callee data/stack event is now in the predicted-event
denominator, and its entry/exit registers and times are computed and compared.
No captured callee post-state or elapsed duration is used for these entries.
Other callees still supply their own transcripts/durations without resetting
the predicted caller clock.

| Fresh IRQ4 capture | Identical / recorded envelopes | Identical / predicted bus events | Full / partial / repeated road calls | Skipped |
| --- | ---: | ---: | ---: | ---: |
| Natural (600 frames) | 597 / 597 | 1,039,702 / 1,039,702 | 4 / 0 / 2 | 0 |
| Directed (900 frames) | 897 / 897 | 1,667,210 / 1,667,210 | 673 / 673 / 673 | 0 |
| Directed edges (120 frames) | 117 / 117 | 121,899 / 121,899 | 88 / 88 / 88 | 0 |
| Road endpoints (120 frames) | 117 / 117 | 121,899 / 121,899 | 88 / 88 / 88 | 0 |

Combined: **1,728/1,728 envelopes, 2,950,710/2,950,710 predicted bus
events and 2,553/2,553 road-callee calls identical, zero differences and
zero race skips**. Each capture excludes one unfinished shutdown service,
outside these denominators. The larger directed fixture selects 225 source
bytes (`$00..$E0`), all eight destination banks, and stage words 0..24/$FFFF.
The additional endpoint fixture selects source bytes `$00/$01/$FE/$FF`,
destination bytes `$F8..$FF` (proving the low-three-bit mask), and stage
words 0/24/$FFFF/$8000 (signed negative indexing and doubling wrap).
Full-bank fixtures use eight aligned ROM source pointers. These are directed
RAM operand fixtures, not evidence of natural stage/spawn selection.

Coverage limits: partial-bank calls have directed coverage only; source
selectors `$E1..$FD`, other stage words and arbitrary full-bank source
pointers are unexercised. Nested interrupts, operand/prefetch aliasing and
general interrupted caller execution remain outside this bounded oracle.
The remaining timed callee boundaries are
`$14A86/$13F92/$1329E/$16C8A/$18444/$13E5C`; game-flow callers
`$17146/$14990/$C6F8/$E2DA`, sub-produced blit operands, physical input and
interrupted instruction contexts remain supplied. Independent IRQ/caller,
ADC/publication/sub scheduling, road-raster pairing and native host retirement
are unchanged; Phase 3d remains incomplete.

Three temporary-source constant mutations fail against the directed edge
capture: the full-bank DBF initializer `$1F->$20` changes 88/117 envelope
register results; the source selector shift timing `14->12` changes
89,275/121,899 bus events; adding two to the doubled stage index changes
8,645/121,899 events. The mutation runner leaves the original source intact.

```bash
make oracle-capture-irq4-service
make oracle-irq4-service
python3 tools/oracle/mutation_check.py irq4roadcount irq4roadshift irq4roadstage
```

Validation: `make -j4`, `make test`, `make verify-stage1`, the complete
`make -j4 oracle-test` and a 180-frame dummy-driver launch pass. Existing
oracle checks have zero new differences; their previously documented input
race skips remain. Stage 1 audio RMS is still 903.949. The CMake endpoint
test is registered but not executed because CMake is unavailable. ROM images,
captures, disassembly output and mutation artifacts remain ignored.


## IRQ4 palette-cycle timing continuation

`codex/irq4-palette-cycle-timing` removes the supplied `$1329E` callee
state/duration from `OMainIrqService`. The translation follows main ROM
`$1329E..$1331E` instruction by instruction: word subtraction by `$11`,
zero return, unsigned comparison against 6, phase ASL.W #3/#1 and masking,
signed ADDA.W, and seven postincrement MOVE.L transfers from runtime ROM.
The destination is hardware palette `$121E82` or fade RAM `$CDAB0`, selected
by the live `$CDD05` byte. It computes both branch widths, operand bus
phases, high/low word order, preserved upper D0 word, A0/A1 increments,
RTS stack reads and entry/exit clocks/registers. No captured cycle-callee
post-state or duration is consumed; remaining external durations still add
to the predicted caller clock without resetting it.

`AB2IRQ43` includes ROM source reads at `$132DA..$132E6/$13310..$1331C`
in the IRQ4 capture and verified-event denominator. As with the road
transfers, Lua excludes only the p+4 instruction prefetch; the test rejects
an executed operand alias at that address. Regenerate older IRQ4 captures.

| IRQ4 capture | Identical / envelopes | Identical / predicted bus events | Palette-cycle calls | Skipped |
| --- | ---: | ---: | ---: | ---: |
| Natural (600 frames) | 597 / 597 | 1,039,702 / 1,039,702 | 0 | 0 |
| Directed (900 frames) | 897 / 897 | 1,689,419 / 1,689,419 | 673 | 0 |
| Directed edges (120 frames) | 117 / 117 | 124,803 / 124,803 | 88 | 0 |
| Road endpoints (120 frames) | 117 / 117 | 124,803 / 124,803 | 88 | 0 |
| Cycle endpoints (600 frames) | 597 / 597 | 496,589 / 496,589 | 448 | 0 |

Combined: **2,325/2,325 envelopes, 3,475,316/3,475,316 predicted bus events
and 1,297/1,297 palette-cycle calls identical, zero differences, zero skips**.
Each run excludes one unfinished shutdown service outside these denominators.
The natural gameplay capture never requests `$1329E`; all new callee evidence
is directed. Existing directed runs execute only the ordinary hardware path.
The cycle endpoint fixture executes 57 zero exits, 277 ordinary transfers and
114 alternate transfers, including 189 fade destinations. It crosses selector
words `$11/$12/$16/$17/$10/$FFFF/0/$8000` and phase words
`0/1/$F/$10/$1F/$20/$7FFF/$8000/$FFFF`, covering unsigned branch boundaries,
phase masks and word wrap. The earlier fade routine executes normally from
directed fade-bank/request state; its own timing remains supplied.

Coverage limits: selectors/phase words beyond these fixtures, natural cycle
requests, nested interrupts and general interrupted caller execution remain
unverified. `$14A86/$13F92/$16C8A/$18444/$13E5C` are remaining callee timing
boundaries. Game-flow callers `$17146/$14990/$C6F8/$E2DA`, physical input,
sub-produced blit operands and interrupted CPU contexts remain supplied.
Independent IRQ/caller, ADC/publication/sub scheduling, road-raster pairing
and native Stage 1 retirement are unchanged; Phase 3d remains incomplete.

Three temporary-source mutations fail against the endpoint capture:
ordinary shift timing `12->10` changes 191,909/496,589 bus events;
phase masks `$1E0/$3E0->$1C0/$3C0` change 3,132/496,589 events;
unsigned selector threshold `6->5` changes 17,873/496,589 events.
Original source is left intact by the mutation runner.

```bash
make oracle-capture-irq4-service
make oracle-irq4-service
python3 tools/oracle/mutation_check.py irq4cycleshift irq4cyclemask irq4cyclebranch
```

Validation: `make -j4`, `make test`, `make verify-stage1`, the complete
`make -j4 oracle-test` and a 180-frame dummy-driver launch pass. Existing
oracles have no new differences; previously documented input race skips
remain. Stage 1 audio RMS remains 903.949. CMake registers the new endpoint
test but is unavailable here, so that path was not executed. ROM images,
captures, disassembly and mutation artifacts remain ignored.

## IRQ4 remaining callee timing continuation

`codex/irq4-remaining-callee-timing` replaces the supplied state/duration
boundaries for `$14A86/$13F92/$16C8A/$18444/$13E5C`. The earlier `$1329E`
translation remains computed and is checked again in every directed dataset.
`omainirqcallee.inc` expands 863 address-labelled ROM instructions into C++
control flow and typed bus/ALU operations in `omainirqcallee.cpp`; there is
no runtime opcode decoder or embedded palette/tile/animation table. Unknown
instruction addresses throw. Runtime operands include both main and sub ROM.

The callee graphs include fade trigger/capture/upload and DAC-channel decay;
airframe/flame/weapon animation and `$142DC/$14324/$14424` blend/upload;
`$B84C` sound enqueue, `$CFC4` missile HUD and `$D062/$D090` decimal conversion;
scenery event records, scroll/palette scripts, `$1866C/$18902` tile upload,
`$1871A/$18798` visibility and segmented display, `$13A74` tile clear; and
`$13E5C` quarter-rate transition completion. They compute instruction widths,
signed word indexing, preserved register upper words, unsigned underflow,
NZVC branches, short/word branch and DBF/DBEQ phases, memory CLR/RMW reads,
MOVE.L ordering, nested subroutine stacks, MOVEM dummy reads and RTS clocks.
MULU timing depends on the multiplier's set bits; DIVU timing follows its
restoring-division steps. Register BCHG/BSET low-half timing is 10 clocks,
which the directed fade/blend checks distinguish from 12. No instruction in
these graphs consumes X; the envelope's RTE restores the captured saved SR.
Outgoing callee CCR is not separately captured or exposed by this API.

`AB2IRQ46` extends the operand capture to these graphs. Instruction extensions
and prefetches remain clock phases; the capture excludes each operand
instruction's extension/prefetch address window. The replay rejects any
operand alias within a conservative ten-byte window after its instruction.
All callee data/stack events enter the verified denominator; entry/exit
D0..D7/A0..A6, clocks and return stack are computed and compared. No captured
post-state or duration is consumed for any of the six requested entries.
Unported game-flow durations still add to predicted caller time, never reset it.

Scenery reads sub-produced roll/pitch/history values during its execution.
The capture now records actual sub write lanes at main operand frontiers,
including operands inside the still-external game-flow calls. The replay
supplies those writes, preserving main-owned bit 0 when sub `$000458` sets
mutex bit 1. This is a **supplied visibility/order boundary**, not independent
sub CPU scheduling. Ordering solely by the CPUs' reported timestamps failed
seven cycle-endpoint envelopes: MAME's scheduler can execute one CPU ahead
of the other, so a sub write's timestamp alone does not determine its
visibility to a main read. No main-produced read value is replaced in these
callee graphs. Physical buttons and sub-produced blit count/list operands
retain their previous explicit contracts. Regenerate older IRQ4 captures.

| IRQ4 capture | Identical / recorded envelopes | Identical / predicted bus events | Skipped |
| --- | ---: | ---: | ---: |
| Natural, 600 frames | 597 / 597 | 1,127,594 / 1,127,594 | 0 |
| Directed, 900 frames | 897 / 897 | 1,822,833 / 1,822,833 | 0 |
| Directed edges, 120 frames | 117 / 117 | 138,810 / 138,810 | 0 |
| Road endpoints, 120 frames | 117 / 117 | 138,810 / 138,810 | 0 |
| Cycle endpoints, 600 frames | 597 / 597 | 1,050,305 / 1,050,305 | 0 |
| Callee endpoints, 600 frames | 597 / 597 | 2,264,579 / 2,264,579 | 0 |
| Scenery endpoints, 600 frames | 597 / 597 | 1,556,513 / 1,556,513 | 0 |

Combined: **3,519/3,519 envelopes, 8,099,444/8,099,444 predicted bus events,
16,576/16,576 newly computed callee calls and 2,193/2,193 palette-cycle
calls identical, zero differences and zero skips**. Thus all six requested
entries account for 18,769/18,769 checked calls. Each capture excludes one
unfinished shutdown service outside these denominators. All seven runs use
fresh config/NVRAM; directed values are RAM fixtures, not natural stage-flow
or producer-cadence evidence.

Coverage includes all eight fade upload banks and channel masks, triggered
and continuation capture banks, inactive fade/animation, blend preparation
and accumulation/completion, flame phases and negative throttle, animation
phase 30/31, transition upload variants and quarter-rate completion; reload
request priority, negative/zero/positive delay, exhaustion, 21 HUD entries,
decimal tens/hundreds, sound disable and queue wrap. Scenery fixtures exercise
valid ROM event records 0..15 (including invalid-selector returns at 11/12),
all scroll dispatch modes, timer underflow, palette scripts, byte and word tile
lists, tile clearing, view initialization/removal, segmented displays and
word-sized scroll arithmetic. The segmented display fixture derives its
length bound from the ROM header. Natural coverage is much narrower: most
active fade/blend/reload and scenery transitions are directed only.

Remaining coverage limits: arbitrary animation pointer selectors, fade
control words outside the documented fixture banks, all channel/color and
throttle combinations, arbitrary event-script pointers and malformed or
out-of-range segmented-display counts are unverified. An exploratory
out-of-range strip fixture underflowed DBF into 65,536 clears and crossed
nested IRQ6; that exploratory dataset is excluded from acceptance. Nested
interrupts, instruction-completion/mask boundaries and interrupted caller
execution remain unverified. Only the graph's used instruction forms are
supported; the instruction guard is not a general main CPU interpreter.
`$17146/$14990/$C6F8/$E2DA`, physical input, sub write visibility/frontiers,
sub-produced blit operands and interrupted contexts remain supplied.
Independent IRQ/caller/ADC/publication/sub scheduling, road-raster pairing
and native Stage 1 retirement remain Phase 3d work.

Eight temporary-source mutations fail: fade capture count `$3F->$3E`, flame
shift `4->3`, reload sound `$9B->$9A`, scenery blank `$00200020->$00210021`,
transition threshold `$20->$21`, and the earlier cycle shift/mask/unsigned
branch mutations. The reload mutation changes 18 bus events; the scenery
blank mutation changes 294,912. Shadow include files and mutated executables
stay under ignored `build/oracle/mutations`; original sources remain intact.

```bash
make oracle-capture-irq4-service
make oracle-irq4-service
python3 tools/oracle/mutation_check.py irq4fadecount irq4animateshift irq4reloadsound irq4sceneryblank irq4transitionend irq4cycleshift irq4cyclemask irq4cyclebranch
```

Validation: `make -j4`, `make test`, `make verify-stage1`, the full
`make -j4 oracle-test` and a 180-frame dummy-driver launch pass. Existing
oracle differences remain zero, with their previously documented race skips.
Stage 1 audio RMS remains 903.949. CMake registers both new endpoint tests
but is unavailable here, so that build path was not executed. ROM images,
captures, disassembly and mutation artifacts remain ignored. These timing
ports remain outside the active native `OEngine`; Phase 3d is incomplete.


## Bounded caller instructions and IRQ-entry continuation

`OMainIrqService::caller_step` translates one address-labelled instruction
at a time in `$1458A..$145B8`, `$14856..$14878` and `$B676..$B696`. It computes
ROM prefetches, operand/stack phases, short/word branches, CCR, active SP,
next PC and completion clocks. The dispatcher reads active flags and handler
pointers live; JSR returns the handler PC as an explicit boundary. Publication
reads the actual buffer pointer and request count. No handler or sub progress
is invented. Unknown PCs, traced contexts and unsupported IPL levels throw.

The CPU samples IPL at the instruction's selection phase, before completing
the final four-clock bus phase. The word RMW forms sample before their final
write. The resulting eligible level stays latched until exception entry;
selecting the board level again at the instruction end would change latency.
`enter`, shared with the existing full IRQ service port, receives the computed
completion time, next PC, registers and SR. It computes stack writes, E-clock
IACK synchronization, vector reads and both handler-prefetch clocks. The
supervisor stack remains supplied when interrupted code runs in user mode.

`capture_caller.lua` records `AB2CALL1` instruction prefetches and CPU-owned
operand/write/stack transactions, entry/final registers and CCR, the following
CURPC, physical IPL at each transaction, and any immediately following IRQ
entry. `oracle_caller.cpp` compares every address, lane, direction, masked data
and timestamp. Its IRQ context uses the caller's computed result; captured
completion/return state is compared rather than used for exception entry.
Consecutive retained caller instructions also compare register/SR/SP/time
continuity and preserve computed main write lanes on subsequent reads. Reads
across external handlers, ISR returns, sampling gaps and fixture resets remain
supplied operand/context boundaries. This is not a continuously executed host.

Natural nonnegative wait and positive publication spins are sampled every
128th iteration; immediately interrupted instructions are retained even when
unsampled. Sampling is outside the accepted-record denominator. Directed
fixtures run a real `MOVE.W #$2300,SR` and JSR before the physical line-223
edge, varying lead time. Ready words `$FFFF/$FFFE/$8000`, missed counts
`$FFFF/9`, and an inactive-record dispatcher are RAM fixtures, not natural
producer or game-flow claims. A real SR-writing instruction restores the
original mask after each fixture. Fixture resets cut in-flight or undecided
caller records explicitly; they never enter the accepted denominator.

Physical IPL visibility is still an input. The test also observes the ideal
`XBoardTimeline` VBLANK at the computed selection time. Directed endpoint
cases can finish a CPU bus phase beyond a pending scanline timer before MAME
delivers that timer; the actual captured IPL is retained and those supplied
edge frontiers are reported, without a guessed delay or skipped record.
Independent timer/ADC and scanline delivery at CPU boundaries remains the
next scheduling task.

| Caller capture | Identical / recorded instructions + IRQ entries | Identical / predicted bus events | Identical / consecutive caller boundaries | Skipped |
| --- | ---: | ---: | ---: | ---: |
| Natural, 600 frames | 467,278 / 467,278 | 1,336,198 / 1,336,198 | 410,301 / 410,301 | 0 |
| Directed, 600 frames | 310,974 / 310,974 | 845,756 / 845,756 | 271,932 / 271,932 | 0 |

Combined: **778,252/778,252 records, 2,181,954/2,181,954 bus events and
682,233/682,233 consecutive caller boundaries identical, zero differences,
zero skips**. Records comprise 773,708 instructions and 4,544 exception
entries. The natural run has 1,316 IRQ2, 486 IRQ4 and 14 IRQ6 entries; the
directed run has 2,199 IRQ2, 519 IRQ4 and ten IRQ6 entries. Directed selection
also checks 6,334 masked polls. Ideal VBLANK differs from supplied IPL at
zero natural and three directed selection frontiers. The directed capture
excludes 181 fixture reset cuts outside these denominators. Both captures
finish with zero unfinished instruction/entry records.

Directed IRQ4 coverage in the re-arm routine, in ROM order:

| Instruction PC | `$14856` | `$1485C` | `$1485E` | `$14866` | `$1486A` | `$1486E` | `$14876` | `$14878` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| IRQ4 entries | 282 | 158 | 15 | 6 | 3 | 9 | 2 | 5 |

Coverage includes both branch outcomes, complete 254-record dispatcher
loops, live handler fetches, both publication outcomes, ready/re-arm/RTS,
missed-word carry wrap, user and supervisor stacks, and IRQ2/IRQ4/IRQ6 entry.
Directed IRQ4 enters after all eight used wait/re-arm instruction PCs,
including CMPI/BEQ, ADDQ, counter write, NOP and RTS; the natural run only
interrupts the spin portion of this wait. Both dispatcher active branches
are natural; directed inactive loops target IRQ4 within record traversal.

Remaining limits: external handler/ISR return contexts and operands,
physical timer IRQ/scanline visibility, sub-produced progress, mask-writing
instruction timing, arbitrary completion tails/E phases, nested ISR graphs,
trace/NMI and service-button branches remain supplied or unverified. Arithmetic
overflow endpoints of dispatcher SUBQ and missed-count ADDQ are unexercised;
an exploratory overflow mutation did not fail and is not acceptance evidence.
A5/A6 initial LEA absolute assignments and BSR/dependent LEA entry-phase register
changes are normalized at the first observable bus phase. General reset,
credit/stage callers and the main program are not interpreted. The previous
whole-wait oracle still excludes its six interrupted attempts; this new
instruction oracle has its own captured denominators.

Commands:

```bash
make oracle-capture-caller
make oracle-caller
python3 tools/oracle/mutation_check.py callerrearm callerreturn callerpoll callercarry
```

Four temporary-source mutations are rejected: re-arm value `1->2`, BSR
return-PC high word `1->0`, IPL poll delayed by four clocks and missed-count
carry disabled. Mutation sources/binaries/logs remain ignored under
`build/oracle/mutations`; original sources are intact.

Validation: `make -j4`, `make test`, `make verify-stage1`, the full
`make -j4 oracle-test` and a 180-frame dummy-driver launch pass. Existing
oracle differences remain zero with their previously documented race skips;
Stage 1 audio RMS remains 903.949. CMake registers the new oracle but is
unavailable here, so that build path was not executed. ROM images, disassembly
and captures remain ignored. Native `OEngine` remains active and Phase 3d is
incomplete. Next: physical IRQ visibility, external handler/ISR resumptions,
ADC/publication/sub scheduling and road-raster/compositor pairing.


## Physical IRQ visibility and bounded caller scheduling

`hwmath/xboard_cpu_visibility.hpp` models the delivery frontiers in MAME's
`device_scheduler::timeslice`, `m68000_device::execute_run` and generated
`m68000-sif/sip` bus phases. The scheduler runs towards a timer deadline;
the CPU yields after a completed four-clock bus phase or an instruction's
internal completion tail. A bus access reached after internal clocks cross
an undelivered scanline still observes the previous IPL. Delivering every
edge at its ideal timestamp before that access is too early. Conversely,
ignoring an instruction-completion yield misses timer IRQs after register
shifts/long arithmetic. Both rules are tested, without an arbitrary delay.
The existing IRQ-entry completion calculation is shared unchanged in
`hwmath/m68000_completion.hpp`. A frame notifier is an external scheduler
boundary; an older bus frontier cannot roll its delivered board time back.

`capture_caller.lua` now emits `AB2CALL2` with explicit directed fixture
start/restore markers. Its `AB2VIS02` sidecar retains every bounded caller
bus phase, timer/ADC/mux accesses, scanline-adjacent transactions and frame
input changes. It records the preceding completed bus phase, and previous
opcode/shift source when crossing an instruction-completion frontier. ROM
opcode context stays in the ignored capture, not a source table. These
frontiers and hardware transactions outside the bounded caller graph are
**supplied external execution boundaries**. The board generates scanlines,
EXCK, timer IRQ, ADC deadlines and IPL from reset. Captured physical IPL and
ADC reads are assertions, never substitutes for generated values. Analog
inputs are sampled at the existing gameplay frame notifier; sub-frame host
input changes are outside this fixture.

`engine/omaincallerschedule.hpp` advances the verified instruction graph
from its own registers, PC, SR/SP and time. It keeps the selected interrupt
latched and enters the exception from computed completion/return state;
user-mode SSP remains an explicit input. `oracle_caller.cpp` uses generated
physical IPL for every poll and carries computed state across consecutive
instructions. It executes omitted `$14856/$1485C` and `$B676/$B67C` busy
spins up to the next retained assertion. Every generated bus transaction,
including prefetches and its previous-phase frontier, is compared against
the sidecar. Omitted spins are no longer supplied CPU contexts or times.

An external ISR can return into BGE before a retained status read. In the
directed `$8000` fixture IRQ4 changes the wait word to `$7FFF`; carrying RAM
from before that unported ISR produced twelve retained boundary failures
and 1,192 differing omitted-spin bus assertions. The replay now supplies the
first resumed wait operand once when the external boundary has not yet
established it, then carries it through all subsequent spins. These supplied
operands are counted explicitly; they are not skips or an independently
executed ISR. Within each scheduled caller segment, main-produced writes remain protected
from replacement.

| Capture, 600 frames each | Physical observations identical / captured | Independently executed omitted wait instructions | Omitted wait bus assertions identical / predicted | Retained instruction/IRQ records identical / captured | Skipped |
| --- | ---: | ---: | ---: | ---: | ---: |
| Natural | 16,121,180 / 16,121,180 | 4,642,412 | 13,927,870 / 13,927,870 | 467,278 / 467,278 | 0 |
| Directed | 26,159,256 / 26,159,256 | 8,060,892 | 24,183,570 / 24,183,570 | 310,974 / 310,974 | 0 |

Combined: **42,280,436/42,280,436 physical observations and
38,111,440/38,111,440 omitted-wait bus assertions identical, zero differences,
zero skips**. The bounded caller schedule computes 12,703,304 additional
wait instructions. Retained instructions/IRQ entries remain
778,252/778,252 and retained bus events 2,181,954/2,181,954, with zero skips.
Consecutive caller boundaries now match 734,677/734,677, including boundaries
reached through computed sampling gaps. There are 19,895 delayed-scanline
observations, 14,677 observed completion-tail contexts, 4,626 ADC reads,
3,515 IRQ2/1,005 IRQ4/24 IRQ6 entries and 6,334 directed masked polls.
The natural schedule supplies 481 first-resume wait operands and the directed
schedule supplies 79, **560 explicit external operands in total**. Directed
resets have 284 explicit context markers; the original 181 in-flight
fixture cuts remain outside the retained-record denominator. Both captures
finish with zero unfinished instructions or IRQ entries.

Coverage retains IRQ4 after all eight re-arm instructions, both dispatcher
active branches and publication outcomes, user/supervisor stacks and combined
IRQ6. The physical probes additionally cover timer assertions and clears,
VBLANK assertion/clear and ADC completion under deferred CPU delivery.
External handler/ISR resumptions, initial contexts, main program/game-flow
transcripts, previous bus/opcode/source frontiers across unported instruction
graphs, physical input bytes, sub writes and compositor feedback remain
supplied. Arbitrary opcode tails, mask-writing and nested ISR execution,
trace/NMI, unexercised service-button paths and independent main/sub execution
are not established. The native Stage 1 host remains active; this is a bounded
scheduling checkpoint, not full Phase 3d acceptance or whole-credit equality.

Commands:

```bash
make oracle-capture-caller
make oracle-cpu-visibility
make oracle-caller
python3 tools/oracle/mutation_check.py visibilityearly visibilityphase visibilitytail callerrearm callerreturn callerpoll callercarry
```

Three new temporary-source mutations fail: ideal-time delivery (two physical
mismatches before the incorrectly advanced clock regresses at a frame notifier),
a one-clock delivery shift (284 directed physical mismatches), and removed
shift completion tails (71 natural physical mismatches). The existing caller
re-arm/return/poll/carry mutations also fail, including a computed schedule
boundary failure for the shifted poll. Shadow headers, mutation binaries,
logs, ROM-derived opcode context and captures remain ignored under `build/`.


Validation: `make -j4`, `make test`, `make verify-stage1`, the complete
`make -j4 oracle-test` and a 180-frame dummy-driver launch pass. Existing
oracle differences remain zero with their previously documented race skips;
Stage 1 audio RMS remains 903.949. Make and CMake register the visibility
oracle; CMake is unavailable on this host, so that build path was not run.
ROM data, captures, opcode context and mutation artifacts remain ignored.
Native `OEngine` remains active. Next: establish external handler/ISR
resumptions, input producer cadence and sub progress across these bounded
caller segments, then extend paired road/video/sound host acceptance.

## PR #21 review continuation: timed host I/O endpoints

[PR #21](https://github.com/duduke/AfterBurner2-Recomp/pull/21) merged on
2026-10-07 as `426dd162589fe26f3b201b6aeffe1b09d5284335`, including correction
commit `de49172`. This checkpoint accepts the production endpoint wiring only;
Phase 3d host scheduling and gameplay acceptance remain incomplete.

`engine/omainio.hpp` installs the production ADC read/write, CXD1095 Port C
mux, active-low IO1 weapon-button and 315-5250 sound-command callbacks used by
`OEngine`. It owns the already verified `XBoardTimeline`/ADC0804 models. A
caller advances physical time explicitly; conversion writes start the ADC,
reads retain the preceding result until its 59,200 ns deadline, and busy
writes do not restart it. Conversion completion samples the selected lane.
Keyboard input preserves X `$20/$80/$E0`, reversed Y `$C0/$80/$40` for
Up/neutral/Down, and throttle `$00/$80/$FF`. `$B0AC`, when called at a verified
CPU boundary, applies its own gameplay clamps. Motor-feedback lanes 3/4
remain `$80`; absent lanes 5..7 use the driver's fallback `$10`.

The PR's six comparator calls at the start of each display tick were removed.
The natural endpoint capture has four or five calls per late frame-notifier
interval; neither a fixed count nor service at an ideal timer edge establishes
68000 interrupt latency. Native `tick()` does not advance this physical clock,
run the sampler/input handler or execute reload. Object initialization, sound
queue activation and the compound-player/register contracts remain with their
unported host/game-flow callers. Road fixtures again supply labelled arcade
bank targets. Native weapon sounds and HUD remain active.

`oracle_mainirq.cpp` and `oracle_adc_timing.cpp` now instantiate the same
`OMainIO` implementation instead of recreating its endpoint lambdas. The IRQ
check compares ordered CPU-owned RAM/I/O writes, register preservation, ADC
results/mux/timer IRQ assertions, and the actual buffered sound-command bytes.
The directed ADC check reads/writes through MainBus and covers pre-deadline
reads, busy writes and mux changes. ROM-free engine checks additionally verify
physical endpoint mapping, button release, conversion-time input changes,
callback order, restart and absence of invented service in native ticks.

| Dataset | Identical/captured | Skipped |
| --- | --- | --- |
| `mainirq` + `mainirq_endpoints` calls | 7,058/7,058 | 0 |
| Their continuous hardware events | 45,176/45,176 | 0 |
| `adc_timing` events | 15,530/15,530 | 0 |
| `adc_timing_directed` events | 10,099/10,099 | 0 |

The IRQ datasets include 946 checked sound-command callbacks. Fresh endpoint
and directed ADC captures under `build/oracle/pr21-review/` reproduce
3,529/3,529 calls, 22,588/22,588 hardware events and 10,099/10,099 directed
ADC events, respectively, zero skips. These repeats are not additional path
coverage. No ROM data or captures are committed.

`tools/oracle/check_io_mutations.py` compiles temporary production-header
variants and requires a completed oracle with mismatches, rather than accepting
a compilation failure, crash or missing capture as mutation evidence:

| Temporary mutation | Failed IRQ records | Failed hardware events |
| --- | --- | --- |
| Mux shift 2 → 3 | 1,931 | 1,931 |
| Immediate current-lane ADC read | 5 | 0 |
| Buffered sound byte XOR 1 | 473 | 0 |

Pristine production sources are never overwritten. Each mutation exits with
oracle status 1; the original code passes again. `make test`,
`make verify-stage1`, `make oracle-test` and a 180-frame dummy launch are the
required final regression checks.

**Limits:** CPU transaction timestamps and physical input changes are supplied
by MAME. Outside IRQ windows, effective mux writes and timer/ADC transactions
remain replayed external CPU actions; within IRQ windows the sampler computes
them and captured I/O directions are caller inputs. Sound-ring producer state
is supplied at IRQ entry. The checks establish production endpoint behavior
and command-buffer order, not independent IRQ selection/entry, producer cadence,
input-to-road/RGB composition or waveform timing. Live flight/reload/sound
scheduling and paired host acceptance remain unchecked in Phase 3d.

### Oracle instructions for agents continuing this work

1. Read the roadmap's working rules and [historical PHASE3D PLAN](https://github.com/duduke/AfterBurner2-Recomp/blob/1f584d75aef22bf6e487a502235bc61065738e94/docs/PHASE3D_PLAN.md) before editing. Port
   actual CPU callers instruction by instruction; do not replace the removed
   six-call loop with another arbitrary frame count or ideal-edge IRQ service.
   Use the verified board, CPU visibility and bounded IRQ/caller ports, keeping
   unported resumptions/sub progress explicit until independently checked.
2. From the repository root, set `MAME` and `ROMPATH` if the defaults in
   `tools/oracle/capture.sh` are unsuitable. Use the real `aburner2` ROM set and
   MAME 0.287. The wrapper creates fresh NVRAM/config and rejects incomplete
   captures and Lua errors. Keep all outputs under ignored `build/oracle/`.
3. For endpoint-only work, run:

   ```sh
   make oracle-capture-mainirq
   tools/oracle/capture.sh tools/oracle/capture_adc_timing.lua build/oracle/adc_timing.bin 900 45
   AB2_ORACLE_ADC_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_adc_timing.lua build/oracle/adc_timing_directed.bin 600 45
   make oracle-io
   python3 tools/oracle/check_io_mutations.py
   ```

4. Before enabling live scheduling, extend the oracle to execute that production
   scheduler. Generate matching natural and directed references, including
   inputs changing near conversion deadlines, masks/IRQ overlap, held/released
   and simultaneous buttons, ring wrap/disable, and reload transitions. Compare
   ordered CPU-owned writes, complete registers/chip state, ADC results, mux,
   IRQ service and sound-latch timestamps. Extend paired checks through shared
   road targets, raster outputs, sprite lists and rendered RGB. Captured results
   must be assertions, not injected answers for newly claimed computed behavior.
5. Bootstrap the existing full reference suite, including supplemental captures
   not generated by the umbrella target:

   ```sh
   make oracle-capture
   make oracle-capture-board oracle-capture-service
   make oracle-capture-host-interleave oracle-capture-host-publication
   make oracle-capture-paired oracle-capture-irq4-service
   make oracle-capture-logic-wait oracle-capture-caller
   make oracle-capture-caller-service
   make test
   make verify-stage1
   make oracle-test
   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./afterburner2 --frames 180
   ```

   Build the game with `make -j4` before the launch. Retain Lua taps globally,
   filter writes by CPU/PC, exclude stacks and skip a concurrent-CPU race only
   when it changed an input the port actually read. Record all other exclusions
   and supplied boundaries explicitly.
6. Mutate a production constant or scheduling rule, prove the oracle fails,
   restore it and rerun pristine checks. Changing only an assertion's expected
   value is insufficient. Report "N of M identical, K skipped (reasons)",
   uncovered paths and remaining supplied inputs in this document. Check off a
   roadmap milestone only when its own host acceptance has passed.


## Phase 3d continuation: caller/IRQ2/IRQ6 resumptions

[PR #22](https://github.com/duduke/AfterBurner2-Recomp/pull/22) merged on
2026-10-07 as `cecfdd2ba30db443f9e4c52b12e1613cfffa28ab`, containing checkpoint
commit `147f310`. This is an authorized bounded resumption checkpoint;
Phase 3d host scheduling and gameplay acceptance remain incomplete.

`OMainIrqService::finish` continues the existing instruction-by-instruction
handler port from `enter`'s computed registers, PC, SR, supervisor stack and
80 ns clock. `run` delegates to the same split path. `OMainCallerSchedule`
now retains the active user stack across exception entry, executes bounded
IRQ2/IRQ6 through RTE, restores USP when the saved S bit is clear, and carries
that result into subsequent caller instructions and omitted busy spins.
The scheduler rejects caller steps while the entered handler is pending and
keeps IRQ4 returns external: IRQ4 can redirect RTE and needs an independently
checked game-flow/USP contract.

`capture_caller.lua`'s opt-in `AB2CALL3` records handler bus transactions and
return assertions without a supplied handler CPU context or duration.
The replay supplies shared-RAM producer state at handler entry, but checks and
preserves all six computed exception-frame bytes rather than replacing them.
It compares ordered CPU-owned handler/stack/I/O writes and reads, the written
RAM lanes, complete D0..D7/A0..A6, SR, active SP, return PC and time. Handler
writes stay protected from replacement within a continued caller segment.
The preceding ADC result is a labelled supplied input; the independent physical
visibility sidecar still checks that result against the generated ADC timeline.
The new check does not establish ADC producer cadence or ring initialization.

The first linked replay found an unported boundary hidden by the previous
context reseeding: another exception can start immediately after RTE, before
a caller instruction runs. The capture now explicitly marks the first
post-RTE access outside the bounded caller graph, including an unported returned
instruction or back-to-back exception entry. These intervals are supplied
execution boundaries, not skipped service records. Completed RTE return
prefetches also establish the next caller's previous bus-phase frontier;
using the final RTE stack read instead produced false frontier differences.

| Dataset | Linked services identical / captured | Handler bus events identical / captured | Computed caller resumptions | Unported post-RTE boundaries | Skipped |
| --- | ---: | ---: | ---: | ---: | ---: |
| Natural gameplay, 600 frames | 1,330 / 1,330 | 58,919 / 58,919 | 1,304 | 26 | 0 |
| Directed boot/caller fixture, 120 frames | 343 / 343 | 15,140 / 15,140 | 340 | 3 | 0 |

Combined: **1,673/1,673 services and 74,059/74,059 handler bus events identical,
zero differences, zero skips**, with 1,644 computed caller resumptions and
29 explicitly supplied post-RTE intervals. Coverage includes 1,656 IRQ2 and
17 IRQ6 services, 1,670 user-mode and three supervisor-mode returns. Natural
inputs deliver coin/start and gameplay controls; the shorter directed dataset
covers boot and caller fixtures, not live gameplay input endpoints. The
existing natural/endpoint/directed service datasets separately pass the split
entry/finish API on **9,439/9,439 calls and 474,024/474,024 bus events**, zero
skips, retaining their sampler slots, sound ring wrap/disable/empty, mask and
completion-tail coverage.

The new caller datasets additionally pass 514,806/514,806 retained instruction/
IRQ records, 1,466,316/1,466,316 retained bus events and 477,697/477,697 consecutive
caller boundaries. They execute 6,142,730 omitted wait instructions with
18,427,992/18,427,992 bus assertions and check 20,373,762/20,373,762 physical
observations, all with zero differences/skips. Supplied inputs remain explicit:
1,673 shared-RAM entry snapshots, 1,656 preceding ADC results, 2,028 first-resume
wait operands and 44 directed fixture markers (27 in-flight fixture cuts are
outside the retained-record denominator). Both captures finish with zero
unfinished instructions, entries or linked services.

Two temporary-header constant mutations complete with oracle exit status 1:
flipping the restored SR carry bit fails all 343 directed service returns and
26 caller boundaries; restoring USP two bytes too high fails 340 service
returns and 12,916 caller boundaries. Pristine sources are never overwritten;
mutation headers, executables, logs and captures remain ignored under `build/`.

Commands:

```sh
make oracle-capture-caller-service
make oracle-caller-service
python3 tools/oracle/mutation_check.py callerresumesr callerresumeusp
make test
make verify-stage1
make -j4 oracle-test
make -j4
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./afterburner2 --frames 180
```

**Limits:** IRQ4 service/resumption and game-flow callers, nested ISR execution,
back-to-back exception selection/entry before a caller instruction, arbitrary
returned instruction graphs, physical input producer changes, ADC/mux actions
outside the checked services, sound-ring producer state/cadence, sub-write
frontiers and renderer feedback remain supplied or unverified. First-resume
shared wait operands are still external inputs even when CPU state/time carries
across RTE. This is a bounded IRQ2/IRQ6 scheduling checkpoint; it does not wire
live flight/reload/sound into `OEngine` or complete paired road/video acceptance.
Native Stage 1 remains active and Phase 3d remains incomplete.

Validation: `make -j4`, `make test`, `make verify-stage1`, the complete
`make -j4 oracle-test` and a 180-frame dummy-driver launch pass. Existing
oracle differences remain zero with their previously documented race skips;
Stage 1 audio RMS remains 903.949. Make and CMake register both new caller
captures and their physical visibility checks; CMake is unavailable on this
host, so that build path was not run. No ROM data or captures are tracked.
Next: independently establish IRQ4/unported handler resumptions and post-RTE
exception selection, producer cadence and sub progress before live host wiring.


## Phase 3d continuation: RTE selection and immediate exception entry

`OMainIrqService` now exposes the IRQ2/IRQ6 RTE selection phase at the final
return prefetch, four clocks before the computed return boundary. The bounded
`$1466A/$14672` port restores SR after the third stack read, advances the first
return prefetch, then samples physical IPL against the restored mask. This
ordering follows MAME's `m68000` RTE microcode; captured IRQ bytes remain
assertions, not selection inputs.

`OMainCallerSchedule` retains the computed supervisor stack as well as USP.
A selected immediate exception cannot execute a caller instruction first; entry
uses the computed return PC, SR, registers, clock and SSP. MAME attributes these
bus accesses to the RTE CURPC until another instruction executes. Using the
returned instruction PC gives correct writes/times but incorrect trace PCs.
IRQ4 execution/return remains an external boundary after the computed entry.

The opt-in `AB2CALL4` capture (`AB2_ORACLE_CALLER_RETURN=1`, with linked service
capture enabled) retains RTE physical visibility phases and asserts whether the
first post-RTE access starts an exception. Immediate entries use the existing
ordered exception write/vector-read comparator. The service entry RAM/ADC inputs
and first-resume wait operands retain their existing supplied-input labels.
Older capture formats and targets remain supported.

| Dataset | Services and RTE selections identical / captured | Handler bus events identical / captured | Immediate exception entries identical / captured | Unported returned-instruction boundaries | Skipped |
| --- | ---: | ---: | ---: | ---: | ---: |
| Natural gameplay, 600 frames | 1,330 / 1,330 | 58,919 / 58,919 | 14 / 14 | 12 | 0 |
| Directed boot/caller fixture, 120 frames | 343 / 343 | 15,140 / 15,140 | 3 / 3 | 0 | 0 |

Combined: **1,673/1,673 services and selections, 74,059/74,059 handler bus
events and 17/17 immediate exception entries identical, zero differences,
zero skips**. The previous 29 supplied post-RTE intervals are reduced to 12
unported returned-instruction boundaries; they are explicit boundaries, not
skipped service records. The existing 1,644 computed caller resumptions remain.
The captures include 1,656 IRQ2 and 17 IRQ6 services, 1,670 user-mode and three
supervisor-mode returns. Natural input delivers coin/start and gameplay controls;
the directed fixture covers boot/caller timing rather than live axis endpoints.

The combined captures also pass 514,823/514,823 retained caller/entry records,
1,466,401/1,466,401 retained bus events and 477,697/477,697 consecutive caller
boundaries. Omitted busy spins execute 6,142,730 computed instructions with
18,427,992/18,427,992 bus assertions. Physical visibility matches
20,387,341/20,387,341 observations. Supplied inputs remain 1,673 shared-RAM
snapshots, 1,656 preceding ADC results and 2,028 first-resume wait operands;
44 explicit fixture markers and 27 in-flight fixture cuts retain their previous
meaning. Both captures finish without unfinished instructions or entries.

Three temporary-header mutations return oracle status 1: `rtemask` uses the
handler's old mask and misses immediate IRQ4 selection; `rtefrontier` shifts the
selection timestamp by one 80 ns cycle and fails all 343 directed selection
assertions; `rtecurpc` substitutes the returned instruction PC and fails all 15
bus events across the three directed immediate entries. Pristine headers are
never overwritten; mutation artifacts and captures remain ignored under `build/`.

Commands:

```sh
make oracle-capture-caller-return
make oracle-caller-return
python3 tools/oracle/mutation_check.py rtemask rtefrontier rtecurpc
make test
make verify-stage1
make -j4 oracle-test
make -j4
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./afterburner2 --frames 180
```

**Limits:** this is bounded IRQ2/IRQ6 return selection, not a full CPU scheduler.
IRQ4 returns/redirection, nested/unported ISR graphs, the 12 returned-instruction
boundaries, physical input producer cadence, sound-ring producers, initial
contexts, first-resume shared operands, sub-write frontiers and renderer feedback
remain supplied or unverified. Immediate-entry captures exercise the pending
IRQ4 path; they do not establish every IRQ level/mask combination, level-seven
NMI, trace exceptions or an IPL edge between the two RTE return prefetches.
Native Stage 1 remains active; Phase 3d remains incomplete.

Validation: `make -j4`, `make test`, `make verify-stage1`, the complete
`make -j4 oracle-test` and a 180-frame dummy-driver launch pass. Existing
oracle differences remain zero with their documented race skips; Stage 1 audio
RMS remains 903.949. Make and CMake register the new captures and physical
visibility checks. CMake is unavailable on this host, so that build path was
not run. No ROM data, disassembly output or captures are tracked.

Next: establish IRQ4 and unported handler resumptions, then input/publication/sub
producer progress before independent host wiring and paired road/video acceptance.

## Phase 3d continuation: production ADC producer at bounded caller services

`OMainIO::before_bus` connects the previously checked CPU visibility model
to the production physical endpoints. It delivers pending board events at the
previous completed bus/instruction frontier while retaining the actual bus
transaction timestamp. ADC reads observe delivered conversion state; ADC
strobes start at the transaction timestamp. An internal CPU overrun can make
these times different. Call `before_bus` before every timed transaction; existing
direct board-clock users retain their previous behavior when this API is unused.
The ROM-free bus regression checks the conversion deadline and the old/new
result on either side of the delivery frontier.

`CpuVisibilityCapture` now uses production `OMainIO` ADC/mux endpoints
throughout the continuous physical timeline and retains its computed conversion
result at ADC read phases. The linked caller's ADC callback reads that computed
result at its predicted access timestamp. It no longer reads the expected
handler event's data. Expected ADC bytes remain assertions in both physical
and handler bus comparisons. No new ROM routine or capture format was added.

| Dataset | Linked services identical / captured | Handler bus events identical / captured | Computed ADC reads | Physical observations identical / captured | Skipped |
| --- | ---: | ---: | ---: | ---: | ---: |
| Natural gameplay, 600 frames (`caller_return`) | 1,330 / 1,330 | 58,919 / 58,919 | 1,316 | 16,132,928 / 16,132,928 | 0 |
| Directed caller fixture, 120 frames (`caller_return_edges`) | 343 / 343 | 15,140 / 15,140 | 340 | 4,254,413 / 4,254,413 | 0 |

Combined: **1,673/1,673 services, 74,059/74,059 handler bus events and
20,387,341/20,387,341 physical observations identical, zero skips**. All
1,656 linked IRQ2 ADC reads use computed conversions, with zero supplied ADC
result bytes. The existing 1,644 caller resumptions, 1,673 RTE selections and
17 immediate exception entries still pass. The continuous physical timelines
also check 2,670 ADC reads, including transactions outside linked services.

Two temporary production-header mutations fail: `calleradclane` changes the
ADC channel selection by one and produces 60 physical differences in the
directed capture; `calleradcclock` delays the ADC strobe by one second and
produces one physical difference. Both exit with status 1. A preliminary
one-millisecond strobe delay was not rejected by this capture; these observations
do not pin arbitrary sub-conversion timing shifts. The exact deadline regression
checks the separate transaction/delivery clock contract. Mutation artifacts and
captures remain ignored under `build/`; pristine source is not overwritten.

Commands:

```sh
make oracle-caller-return
python3 tools/oracle/mutation_check.py calleradclane calleradcclock
make test
make verify-stage1
make -j4 oracle-test
make -j4
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./afterburner2 --frames 180
```

**Limits:** the continuous physical producer replay still receives captured
hardware transaction times, mux/strobe writes and unported completion frontiers.
Bounded linked handler transactions are independently checked at predicted CPU
times against that replay; this is conditional composition, not a single
autonomously scheduled host. External physical input frames, 1,673 shared-RAM
snapshots, 2,028 first-resume wait operands, 12 returned-instruction boundaries,
IRQ4/unported handler resumptions, sub-write frontiers and renderer feedback
remain supplied. Existing independent endpoint checks retain neutral, extremes,
release and simultaneous weapon coverage; the directed caller fixture does not
establish live axis endpoint scheduling. Native Stage 1 remains active, and the
roadmap's complete input wiring item and Phase 3d remain unchecked.

Next: establish IRQ4/unported caller resumptions and independently scheduled
physical producer/publication/sub progress before live input-to-flight wiring.

Validation: `make test`, `make verify-stage1`, the complete
`make -j4 oracle-test`, `make -j4` and the 180-frame dummy-driver launch pass.
Existing oracle differences remain zero with their documented race skips;
paired composition still passes 240/240 rendered frames with zero skips, and
Stage 1 audio RMS remains 903.949. Make/CMake include the production bus
dependencies for physical visibility; CMake is unavailable on this host and
was not run. ROMs and captures remain untracked.

## Phase 3d continuation: sequence routing and conditional road composition

`OMainHost` now routes the independently verified object initializer `$1488E`,
sequence compound initializer `$16892`, prepare/hide `$16976/$169B2` and RNG
`$C5A8`. The object oracle calls these through the production entry router;
its six corpora pass 1,848/1,848 records, zero differences or skips. The
dispatcher transcript portions retain their original external-handler limits.
The already routed live fuselage `$9810/$98C0`, shadow/exhaust, recovery,
sequence handlers, weapon/projectile, reload/HUD and selected enemy/effect
graphs retain their independent routine checks. This adds entry routing;
it does not assign new host cadence to those handlers.

`AB2PAIR3` extends the natural paired video capture with the actual road pass
that produced the displayed buffer: shared/road RAM and sub math state at
`$466`, shared/road state and CPU-owned write sets at `$530`, other-main
shared writes during the window and the subsequent `$458/$466` display flip.
Active stack and sub VBLANK writes are excluded. Each of the five target
words records its main dispatcher index and producing write ordinal; an
uncaptured initial word is explicitly marked `$FFFFFFFF`. `AB2REQ04` exports
the main composition's ordered target writes and verified/external ownership.
The older request/video formats remain supported.

The first comparison exposed a real ordering boundary at display frame 1086:
the sub consumer saw bank zero while the published sprite producer retained
`-$70`. A next-dispatch input handler had written the new target before that
dispatcher's sprite publication. Per-word write provenance replaces the
incorrect assumption that the road and displayed sprites share one producer.
Each consumer word is asserted against its actual producing write; verified
port outputs are then installed as road inputs. Unported caller writes and
initial words remain explicitly supplied. The resulting `OSubRoad` shared
write set and complete raster must equal both its completion state and the
later displayed road buffer. The hardware compositor consumes this computed
raster, not the captured display bytes.

Natural gameplay through frame 1264:

- **122/122 complete dispatcher windows identical, zero skips**, executing
  14,134 verified callbacks and 1,305 explicit unported caller transcripts.
  Live fuselage/shadow and both exhaust updates have 122 calls each, weapon
  updates 83, vulcan projectiles 565, and missile/trail handlers remain included.
- **240/240 road passes identical, zero skips**, including shared/road write
  sets. The 1,200 target-word consumers use 313 verified handler writes,
  405 unported caller writes and 482 initial boundaries. These are consumer
  counts; one producing write may feed more than one road pass.
- **240/240 RGB frames pixel-identical, zero skips** with 120 normal and
  120 interpolated sprite lists, their hardware draw triggers and scaled sizes.
- **244/244 composed VBLANK windows and 1,250/1,250 continuous sound calls
  identical, zero skips**, with 255 producers, 995 consumers and 255 latches;
  sound caller times remain supplied.

Two temporary mutations fail with status 1. `hostsequenceinit` routes `$16892`
to prepare instead of initialization and fails 24 of 168 directed object calls.
`pairedroadcenter` shifts the port's road raster center by one word and fails
the paired road write-set comparison. Pristine sources remain unchanged;
all variants, ROM images and captures remain ignored under `build/`.

### Runtime handler inventory and formation/spawn boundaries

`tools/handler_inventory.py` reads `$1C176` and its template pointers from the
user's ROMs, and inventories the current production router without executing
handlers. Initial templates: **254 records, 12 distinct pointers; 18 routed
records, 229 empty and seven unported records**. Empty templates can become
active later; this is not a dynamic handler coverage claim. Slot 131 is the
live fuselage initializer, slot 130 the weapon initializer, slot 1 input and
slots 236..250 the fifteen horizon segments, not an enemy formation.

The seven unported initial handlers are:

| Initial slot | Template entry | Observed later entry / boundary |
| ---: | --- | --- |
| 0 | `$D0D2` | Sets handler `$D0DA`, then falls through; remains unported |
| 2 | `$804A` | Clears record +32, calls `$8898`, installs/falls through `$805A` |
| 97 | `$6838` | Calls `$6CF8`, installs/falls through `$6844` |
| 120 | `$425E` | Calls `$49AE`, initializes record fields, installs/falls through `$427C` |
| 251 | `$13236` | Unported caller; no inferred implementation |
| 252 | `$B66E` | Installs/falls through `$B676`; initializer remains outside the checked entry route |
| 253 | `$18E8A` | Unported stage-flow caller |

The natural dispatcher additionally reaches `$6D18/$71E4`; directed captures
also encounter `$6252/$6466/$64D4/$65EC/$65FA/$7BB6`. Formation/spawn dispatch,
dynamic activation, stage/credit/scoring and those callers require their own
instruction ports and independent captures. They cannot be replaced by native
formations or inferred from the known horizon/effect/enemy handlers.

Commands:

```sh
python3 tools/handler_inventory.py > build/oracle/handler_inventory.log
make oracle-capture-paired-road
make oracle-paired-road
python3 tools/oracle/mutation_check.py hostsequenceinit pairedroadcenter
make test
make verify-stage1
make -j4 oracle-test
make -j4
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./afterburner2 --frames 180
```

**Limits:** conditional consumer/flip composition retains IRQ/caller frontiers,
sub-state snapshots, unported and initial target producers, road control and
tile/palette/renderer feedback. Sequence initialization/recovery and weapon
transitions retain their independent directed checks; this natural slice does
not exercise every transition, formation, spawn, stage or a whole credit.
No native flight, objects, weapons, HUD or sound cues were removed. Live
`OEngine` scheduling and Phase 3d remain incomplete.

Validation: the build, `make test`, `make verify-stage1`, the complete
`make -j4 oracle-test` including the new paired-road target, both new mutation
checks and a 180-frame dummy-driver launch pass. Existing oracle differences
remain zero with their documented race skips; Stage 1 audio RMS remains
903.949. Make and CMake register the new composition tests; CMake is unavailable
on this host and was not run. No ROM data or captures are tracked.


### IRQ4 RTE and the twelve returned-instruction boundaries

`OMainCallerSchedule` now finishes the existing timed IRQ4 envelope, including
both `$147C6` and the service redirect `$147E6`, through the same restored-SR
selection used by IRQ2/6. Selection occurs between the two return prefetches,
320 ns before completion. It retains the supervisor stack and initial USP,
restores the active user stack, and retains RTE's CURPC for immediate exception
entry. A supervisor context that might return to user mode must supply its
initial USP; unknown USP is an error. IRQ4's four unported game-flow callbacks
remain explicit duration/state boundaries, rather than being silently omitted.
The caller oracle now keeps the same scheduler through consecutive instructions
and omitted spins instead of resetting SSP/USP at every assertion.

The original CALL4 corpus's twelve unknown returned instructions were seven
at `$8AF2`, plus one each at `$88C2`, `$B628`, `$15BFE`, `$7BB6`, and `$14EE2`.
These six instructions now execute with their ROM bus phases, flags, registers
and stack effects. CALL5 adds a distinct kind-7 graph-exit marker: after the
ported instruction, an unknown successor remains a declared boundary. This
avoids presenting a port of one instruction as a port of the whole handler.
The six added PCs execute 49,973 times (1,267 / 19,420 / 28,419 / 22 / 155 /
690 respectively), all identical, zero skips. All twelve original RTE-to-first-
instruction links carry computed state. Extended coverage exposes fourteen
additional unknown returned instructions: `$8AF8` nine times and `$B62E` five.

The new natural caller capture checks 517,281/517,281 instruction/entry records,
1,599,569/1,599,569 bus events, 456,096/456,096 consecutive boundaries,
1,345/1,345 IRQ2/6 services and restored-mask selections, and 59,564/59,564
handler events; zero skips. It executes 4,796,639 omitted wait instructions,
with 14,389,792 matching bus assertions. Physical visibility matches all
16,387,881 observations. Its explicit inputs still include 1,345 shared-RAM
snapshots, 1,681 first-resume wait operands and 58,824 unported graph exits.
ADC values are computed by production `OMainIO` (1,330 results); physical
transactions outside the bounded graph remain capture inputs.

IRQ4 return acceptance uses the production scheduler to compute entry,
handler envelope, RTE and any immediately selected exception entry. The
`AB2RTE42` sidecar records initial USP, physical IPL at the final RTE prefetch,
actual active stack/prefetched PC, the selected next exception and its bus
transactions. MAME exposes return PC + 2 during that final prefetch. Initial
interrupted-instruction completion, physical IPL, game-flow callbacks and
sub-owned writes are still supplied. This is conditional scheduling acceptance,
not an independently advancing main/sub host. Natural/directed counts are
597/597 and 117/117 IRQ4 returns, zero skips, including one `$1A130`/`$0700`
redirect. Immediate exception entries match 166/166 contexts and 830/830
bus phases, zero skips. Both corpora return to user mode; supervisor returns and a redirect
from an originally supervisor caller remain uncovered.

Reproduce with `make oracle-capture-scheduling-returns` and
`make oracle-scheduling-returns`. The new checks are included in Make/CMake
oracle suites. Run `python3 tools/oracle/mutation_check.py returnedcopy
returnedbsr irq4returnusp irq4returnmask irq4returnphase`: changed copy
addresses, BSR return PC, USP, restored mask and selection phase must fail.
Captures, sidecars and logs remain ignored under `build/oracle`.

**Independent CPU scheduling is still incomplete.** The remaining work is:

- Port timing and interrupted resumptions through the object-handler graphs,
  including the next `$8B02/$B636` successors, rather than resetting
  at graph exits.
- Port `$17146/$14990/$C6F8/$E2DA` IRQ4 callees with independently captured
  instruction timing/state, including their nested interrupt behavior.
- Port sub CPU caller, IRQ, road and sprite instruction timing. Existing
  `OSubRoad`/`OSubSprites` routine checks verify values, not write times.
- Advance both CPUs at verified yield boundaries against the production
  physical timeline; replace supplied sub write frontiers, first-resume wait
  operands, publication-wait progress and blit producer operands.

Native Stage 1 remains active. None of these remaining dependencies is replaced
with estimated durations or an arbitrary frame clock.

Validation for this continuation: `make test`, `make verify-stage1` (audio RMS
903.949), full `make -j4 oracle-test`, and `git diff --check` pass. The five
mutations above fail; the immediate-entry mask mutation rejects 15/15 directed
entries. New scheduling checks have zero skips; older standalone sprite and
scenery tests retain their documented input-race skips. CMake test registration
was updated, but CMake is unavailable in this environment and was not run.


### Fourteen returned-word boundaries: $8AF8 and $B62E

All fourteen CALL5 unknown returns are now computed: `$8AF8` nine times and
`$B62E` five. `$8AF8` executes `MOVE.W ($29C0AA).L,D1` in four bus phases
(16 clocks), preserving D1's upper word and X while replacing NZVC. `$B62E`
executes `CMPI.W #$C0,D0` in two prefetch phases (8 clocks), preserving D0
and X while computing word subtraction N/Z/V/unsigned-borrow flags. ROM
addresses and bus sequencing are checked against the ROM disassembly and
MAME's `move_w_adr32_dd` / `cmpi_w_imm16_ds` microcode. Both select physical
IPL before the final prefetch and run in the existing caller scheduler.

`AB2CALL6` adds these two instruction PCs to the capture and continuous
physical visibility checks. The oracle can also read the prior CALL5 corpus
as a baseline: each old unknown return must appear at the same PC/time in the
new run, with identical computed RTE registers/SR/active stack, followed by
matching instruction execution. Baseline state is an assertion, never a
scheduler or instruction input. A missing, reseeded or different boundary
fails the check. Result: **14/14 identical, 0 missing, 0 skipped**.

The natural 600-frame capture verifies `$8AF8` 19,420/19,420 times and `$B62E`
28,419/28,419 times; zero skips. Two IRQ entries occur at `$8AF8` and eight
at `$B62E`. Aggregate acceptance is 565,130/565,130 instruction/entry records,
1,734,137/1,734,137 bus events, 503,934/503,934 consecutive boundaries,
1,355/1,355 linked IRQ services/selections and 60,027/60,027 handler bus
transactions. Physical visibility matches 16,518,204/16,518,204 observations;
zero skips in each new check. All 14 requested resumptions now carry computed
CPU state; the new corpus has 1,330 linked resumed caller boundaries.

Coverage: the load sees 78 word values, NZVC 0/4 and X clear. The compare
sees 144 word values, NZVC 9 and both X states (24,024 clear / 4,395 set).
Negative load values, X-set loads, compare equality/positive-result/overflow
flag paths remain unexercised by this natural corpus; no claim is made for
those paths beyond the instruction translation. The existing routine and
caller checks retain their own directed coverage.

The enlarged captured graph exposes **ten further returned-instruction
boundaries**, `$8AFE` twice and `$B632` eight times. These successors are not
the fourteen covered here. Full handler timing and independent CPU scheduling
remain unfinished. Explicit inputs still include 1,355 shared-RAM snapshots,
1,681 first-resume wait operands and 58,829 graph exits. The production ADC
provides 1,340 computed results; external transaction/frontier times and
sub-CPU progress remain supplied. Native Stage 1 remains active.

Reproduce with `make oracle-capture-caller-words` followed by
`make oracle-caller-words`. The test compares `caller_words.bin` against
`caller_steps.bin`; both remain ignored under `build/oracle`. Make/CMake
oracle suites include the new check and visibility corpus. Mutations
`python3 tools/oracle/mutation_check.py returnedaltitude returnedcompare`
change the shared load address and comparison immediate; both must fail.

Validation: `make test`, `make verify-stage1` (audio RMS 903.949), full
`make -j4 oracle-test`, and `git diff --check` pass. The load-address mutation
rejects 19,422 bus phases; the compare-immediate mutation rejects 28,427
instruction/entry records, including eight interrupted comparisons. Source
mutations run only in ignored temporary copies. CMake registration was updated;
CMake remains unavailable, so that build path was not run.


### Ten successor returns: $8AFE and $B632

All ten CALL6 successor boundaries now execute from computed RTE state:
`$8AFE` twice and `$B632` eight times. `$8AFE` ports `SUB.W D1,36(A5)`:
extension prefetch, destination word read, next prefetch, and word write,
16 clocks total. It replaces X/N/Z/V/C, including borrow and signed overflow,
without modifying D1 or A5. IRQ selection occurs before the write, as in
MAME's `sub_w_dd_das` microcode. `$B632` ports `BGE.W $B654`, preserving
registers/flags and testing N==V: 10 clocks taken, 12 untaken, each with two
prefetches. ROM addresses and sequencing are independently checked against
the disassembly and MAME's `bge_rel16` microcode.

`AB2CALL7` extends the instruction/physical-visibility capture. The prior
CALL6 unknown-return states are assertions only; they cannot seed execution.
The baseline comparison requires the same PC/time, computed RTE registers,
SR and active stack, then matching instruction output/bus phases. Natural
result: **10/10 identical, 0 missing, 0 skipped**. The original fourteen
returned-word baseline continues to pass in its separate CALL6 check.

Natural 600-frame results: `$8AFE` 19,420/19,420 and `$B632` 28,419/28,419
instruction executions, zero skips. Aggregate acceptance is 612,976/612,976 instruction/entry records,
1,868,690/1,868,690 bus phases, 551,773/551,773 consecutive boundaries,
1,362/1,362 linked IRQ services/selections and 60,343/60,343 handler bus
transactions. Physical visibility matches 16,648,638/16,648,638 observations;
zero skips. There are four IRQ entries at `$8AFE` and three at `$B632`, and
1,340 resumed caller boundaries. All 19,420 subtraction writes are compared
at their predicted CPU phase. The SUB sees 78 source words and XNZVC 0/$19;
the natural BGE is untaken 28,419 times with N/V=1/0.

The 120-frame directed capture uses the existing explicit fixture cuts, a
real `MOVE.W #SR,SR`, and a JSR to the original `$B632`. Taken branches reach
`$B654`/RTS and return to the fixture; no speculative handler graph executes.
SR `$2300` and `$231A` exercise N/V=0/0 and 1/1, including retained X. The
oracle's `--require-taken` check requires both conditions. All 84,625/84,625
instruction/entry records, 254,657/254,657 bus phases, 261/261 IRQ2 services,
11,616/11,616 handler transactions and 3,369,113/3,369,113 visibility
observations match, zero skips. BGE taken/untaken counts are 22/3,032; SUB
matches 2,438 executions. There are five directed IRQ entries at BGE, 44
explicit fixture context cuts and 313 supplied first-resume wait operands.
This fixture has no immediate post-RTE exceptions, so CALL7 acceptance checks
selection assertions without inheriting the older corpora's nonzero immediate-
exception coverage requirement. The natural CALL7 corpus retains 15 such
exceptions; older IRQ2/6/4 tests keep their coverage requirements.

Uncovered paths: SUB flag combinations other than 0/$19 (including zero
and overflow results), and BGE N/V=0/1. Their
instruction translations remain in place, but these captures do not verify
those paths. Shared operand producers and full handler/sub timing remain
explicit inputs. The natural expansion now exposes seven further returns:
`$8B02` four times and `$B636` three. Native Stage 1 remains active; independent
publication and sub progress are still pending.

Reproduce with `make oracle-capture-caller-successors` and
`make oracle-caller-successors`. Make/CMake oracle suites include the natural
CALL6 baseline comparison, the taken-branch fixture, and both visibility
corpora. Captures/logs remain ignored under `build/oracle`. Mutation command:
`python3 tools/oracle/mutation_check.py successorsuboperand successorbranch`.
The changed SUB operand displacement fails 38,844 bus comparisons; the
changed taken branch destination fails 47 directed bus comparisons.

Validation: `make test`, `make verify-stage1` (audio RMS 903.949), full
`make -j4 oracle-test`, and `git diff --check` pass. Both successor mutations
fail in ignored temporary copies. The first full-suite attempt found the
older harness's nonzero immediate-exception requirement inappropriate for
the new taken-branch fixture (all compared states/phases already matched);
CALL7 now checks all selection assertions and explicitly requires both taken
N/V states for that fixture. The full suite was rerun successfully afterward.
Existing standalone sprite/scenery input-race skips remain unchanged; new
successor checks have zero skips. CMake test registration is updated, but
CMake is unavailable in this environment and was not run.


### Post-RTE graph closure: seven returns and every further exposed return

The seven CALL7 returns at `$8B02` (four) and `$B636` (three) now execute
from computed RTE state. Extending the graph exposed another 31 returns;
all were completed in this continuation rather than left as a new checkpoint:

| Earlier capture | Newly exposed returned PC | Exact computed resumptions |
| --- | --- | --- |
| CALL7 successors | `$8B02/$B636` | 7/7 |
| First extension | `$8B06/$B638` | 9/9 |
| Publication extension | `$8B0C/$8C1A` | 12/12 |
| Scale extension | `$8B30` | 4/4 |
| Sway extension | `$8B64` | 6/6 |

All **38/38 are identical, 0 missing, 0 skipped**. The final 600-frame
capture also passes each earlier return baseline independently: timestamps,
registers, SR and active stack are assertions of the computed RTE output,
not supplied contexts for the newly ported instruction. Both final natural
and directed corpora have **zero unported post-RTE returns**. The new
`--require-closed` gate fails if even one such marker remains.

`OMainIrqService::caller_step` adds 97 literal instruction PCs: the remaining
world projection at `$8B02-$8C1A`, clipping/release at `$9556-$9572`, the
publication producer at `$B636-$B654`. These translate the ROM's shifts, multiplier/divider accesses, indexed ROM/shared
lookups, signed clipping, animation payload/flip stores, publication copies,
count increment and stack returns. Every instruction uses the same operand
width, bus order, flags and address as MAME. This completes these bounded
components; it does not replace the other object handlers or time the
functional `OMainWorld` port as one instantaneous call.

Internal ALU completion needs a distinct CPU boundary. `$B636`'s ASL.W #4
has one four-clock prefetch followed by ten internal clocks; `$8B06`'s
ASR.W #5 has twelve internal clocks. Register-source long ADD/SUB and
ADDA.W complete four clocks after their prefetch. Long ASR adds four internal
base clocks plus twice its count; word ASR adds two plus twice its count.
MAME's `nbcr3`, `srrl5` and `roml3` select the interrupt before the final two
internal clocks, **after** prefetch completion. A physical edge delivered
when that bus completes must participate in selection. An edge beyond the
completed bus cannot be delivered inside the uninterrupted internal tail.

The visibility oracle forecasts that delivery through the production
`XBoardTimeline`/compare timer, rather than reading a captured IPL as an
input. `irq_at` copies timer/scanline state without advancing the live board
or invoking callbacks. Regression checks pin delivery exactly at the bus
boundary, including VBLANK, and ensure forecasting changes no live state.
The caller oracle additionally checks each internal selection at captured
instruction-end minus two clocks. Natural/directed selections pass
322,151/322,151 and 39,438/39,438 respectively, zero skips.

`AB2CALL9` uses MAME's instruction hook to capture the actual CPU state before
first-bus side effects and after internal ALU completion. Headless, read-only
debugger breakpoints execute `printf` and resume; they change no registers,
RAM or ROM. The capture reads the resulting register/SR/stack snapshots,
never synthesizes expected shift/arithmetic results. Exact earlier timestamps
and return states still match. Snapshot availability is mandatory for these
instructions, so a missing hook cannot silently fall back to incomplete
state. Earlier CALL formats retain their original compatibility checks.

The directed corpus also exposed a supervisor IRQ6 after `$B654`'s RTS,
returning to the real SR-writing fixture code at `$3FFD0A`, with restored
SR `$2300` and wrapped supervisor SP zero. The state, entry, RTE and
immediate selection already matched, but the service's former prefetch-only
clock advances omitted two mutable RAM instruction reads. RTE now reads RAM
targets through `MainBus` at its two prefetch phases. The linked caller oracle
compares both reads directly against MAME. The older `AB2SVC02` protocol
explicitly excluded its RAM trampoline's instruction reads; its test now
asserts those two addresses, data words and timestamps separately against
captured pre-service RAM and the recorded RTE end time: **2/2 identical,
0 skipped**. Its original data-bus assertions remain intact.

Final results, all with zero differences/skips:

| Assertion | Natural 600 frames | Directed 120 frames |
| --- | --- | --- |
| Instruction/IRQ-entry records | 2,391,867/2,391,867 | 302,433/302,433 |
| Caller bus phases | 7,028,756/7,028,756 | 881,497/881,497 |
| Consecutive computed boundaries | 2,372,582/2,372,582 | 300,920/300,920 |
| IRQ services/RTE selections | 1,858/1,858 | 321/321 |
| Handler bus transactions | 82,930/82,930 | 14,246/14,246 |
| Physical visibility observations | 21,655,111/21,655,111 | 3,976,979/3,976,979 |
| Unported post-RTE returns | 0 | 0 |

The natural capture retains 15 immediate exceptions and 1,843 user-mode
caller resumptions. The directed capture includes one immediate exception,
one supervisor-mode service return, 320 user returns and 44 explicit fixture
context cuts. Independently scheduled waits still match 4,796,639 instructions
and 14,389,792 bus assertions naturally, and 1,007,232 instructions and
3,021,650 bus assertions in the fixture. First resumed wait operands remain
supplied 1,681/313 times. ADC results are computed (1,843/320); physical input
bytes and shared-RAM producer snapshots remain external (1,858/321 snapshots).

Coverage includes every added instruction PC in the natural corpus. All
three release branches take both paths: `$8B08` 203/19,217, `$8BB0`
545/18,672 and `$8BB8` 437/18,235 taken/untaken; release runs 1,185 times.
The publication ASL sees 144 source words, 0..143, and XNZVC 0/$04.
The variable ASR.W only sees count two, with XNZVC 0/$11. Long ADD sees
XNZVC 0/$08/$11/$19 and long SUB 0/$11/$19. Other shift counts, overflowing
ASL sources, long signed-overflow flags and combinations not exercised here
remain unverified. Taken producer BGE retains N/V=0/0 and 1/1 fixture coverage;
natural N/V=1/0 is untaken. This is bounded corpus closure, not proof of every
possible future game state.

Eight deliberate mutations fail: depth operand displacement, ASL count,
internal selection time, long carry, projection operand address, clip target,
release bit and RAM return prefetch. Changed paths/timing can leave the bounded
transcript before aggregate summaries; the mutation runner accepts the
already-emitted failed bus/state assertions as differential evidence, not a
bare generic exception. The selection mutation rejects 28,419 instruction
records; carry rejects 19,716 consecutive boundaries; clip/release reject
1,090/2,370 bus phases. RAM-prefetch omission rejects the supervisor return.
All source variants, transcripts and logs remain ignored under `build/oracle`.

Reproduce with `make oracle-capture-caller-closure` and
`make oracle-caller-closure`; Make/CMake suites register both acceptance
corpora and their visibility checks. Mutation command:
`python3 tools/oracle/mutation_check.py closuredepth closureshift closurepoll closurecarry closureprojection closureclip closurerelease closureramprefetch`.
`make test`, `make verify-stage1` (audio RMS 903.949), full
`make -j4 oracle-test`, and `git diff --check` pass. The first full-suite run
identified the older service protocol's explicit RAM-prefetch exclusion;
its separate assertions were added and the entire suite rerun successfully.
Existing standalone sprite/scenery input-race skips remain unchanged.
CMake registration is updated; that build path was not run.

Remaining Phase 3d limits: other handler/caller graphs, IRQ4 game-flow callbacks
`$17146/$14990/$C6F8/$E2DA`, sub-generated width/animation/input operands,
publication/sub cadence and independently scheduled paired gameplay remain
external. The final natural corpus still records 16,413 explicit graph exits
outside this component (directed 1,010). These are ordinary unported execution
frontiers, distinct from the now-closed post-RTE markers. Native Stage 1 remains
active; no captures or ROM data are committed.


## IRQ4 RTS-callee scheduling continuation (`$14990/$E2DA`)

The next bounded independent-scheduling step removes two of the four IRQ4
callee transcripts. Runtime-ROM disassembly establishes that `$014990` and
`$00E2DA` are each a single RTS; the routines beginning at `$14992/$E2DC`
are separate entries and are not executed by these IRQ4 calls. Neither entry
implements credit, scoring, or other invented game flow.

`OMainIrqService::frame_leaf` now executes their two return-stack word reads
and eight prefetch clocks (16 clocks total) from the computed caller clock.
The existing IRQ4 call wrapper verifies the computed return address, advances
its stack, and continues without consuming callback registers or a supplied
duration. The leaf observer asserts entry/exit registers and timestamps;
ordered bus assertions include both stack words and their attribution.

The original directed fixtures intentionally bypass the credit/service
callbacks, so a new labelled `AB2_ORACLE_IRQ4_RTS_CALLEES=1` fixture keeps
`$8050` zero except for the existing service redirect. The actual ROM executes
`$C6F8` normally in MAME; its transcript remains an explicit external boundary
in the replay. This fixture does not claim independently produced inputs or
credit flow. `--require-rts` rejects a corpus missing either new entry.
The existing capture format already includes the return-stack transactions;
no expected reads or register results were synthesized.

| Corpus | IRQ4 envelopes identical/captured | Predicted bus events identical/captured | Calls per RTS entry |
| --- | --- | --- | --- |
| Natural 600-frame capture | 597/597 | 1,129,982/1,129,982 | 597 |
| Directed RTS 120-frame fixture | 117/117 | 143,218/143,218 | 116 |
| All eight IRQ4 service corpora | 3,636/3,636 | 8,245,050/8,245,050 | 713 |

All results have **0 skipped, 0 differences**. Both entries have their sole
instruction exercised: **713/713 identical per entry**. Reducing either
return's prefetch clocks from eight to six fails: `$14990` rejects 5,568 bus
assertions and `$E2DA` rejects 4,872. Temporary mutation sources and logs
remain ignored. Reproduce with `make oracle-capture-irq4-service`,
`make oracle-irq4-service`, and
`python3 tools/oracle/mutation_check.py irq4rts14990 irq4rtse2da`.

The larger `$17146/$C6F8` IRQ4 graphs, other caller/handler graphs, first
resumed publication operands, physical input frames, sub-write frontiers and
sub-produced blit operands remain supplied. These two returns reduce the
boundary count; they do not finish the independent-scheduling milestone.
Native Stage 1 remains active. Make and CMake register the new required corpus;
CMake itself was not run. Captures and ROM data are not committed.

Validation: `make test`, `make verify-stage1` (audio RMS 903.949), full
`make -j4 oracle-test`, `make -j4`, a 180-frame headless launch and
`git diff --check` pass. The full suite also retains 714/714 linked IRQ4
RTE assertions and 166/166 immediate-entry assertions, zero skips; its
natural return corpus exercises each new RTS 597 additional times. Existing
standalone input-race skips remain unchanged. The required-coverage gate was
also checked against the earlier directed corpus: it fails explicitly with
`missing RTS callee coverage`, preventing a bypassed callback path from
silently passing this new acceptance check.


## Closed IRQ4 `$17146/$C6F8` callee graphs

This continuation removes the final two IRQ4 execution transcripts. The
outer IRQ4 service now computes every selected callee's registers, ordered
bus phases and duration; it never invokes `Boundary`. The legacy callback
type remains in the caller API, but unsupported targets throw. The oracle
passes an empty boundary callback and has removed its former callee-write,
register and duration replay entirely. Expected callee entry/exit state and
bus events are assertions, not execution inputs.

`omainirqgameflow.inc` adds 1,507 address-labelled instruction translations.
Static ROM control-flow traversal follows both roots, every direct call and
branch, and the five indexed branch tables; embedded data is excluded. The
bounded callee engine has no runtime opcode decoder. ROM strings, coinage,
font/numeric and palette tables remain `MainBus` reads from the user's ROM.
The dependency graph covers:

- `$17146`: stage-3 attract/high-score display and stage-26 ranking,
  insertion, eight entry states, name editing, secret-name transitions,
  highlighted text, score/time/grade formatting and dependent tile/palette
  producers, including `$137E2/$13A2C/$13C3E/$1B658` and their reachable
  helpers. This does not port the callers that select these stages.
- `$C6F8`: service-credit and both coin edges, fifteen coinage selectors,
  ordinary/extended BCD credits and saturation, lamp pulse/cooldown, checksum
  and backup copies, sound requests through `$B840/$B84C`, and the coin
  path into `$CF46` lives/HUD and the already translated `$D062` formatter.
  This checks `$CF46` in this coin graph, not every death/lives/credit caller.

The shared instruction helpers now support variable-register BTST, long
indices, ROL, NEG/NOT, register-mask MOVEM and ABCD/SBCD with X, cumulative Z,
N/V/C. Coin credit arithmetic inherits computed X from `$148B8`'s long counter
increment through `$14990`'s RTS. The action fixture retains the real counter-wrap
case; suppressing that X fails the action corpus. Neither root consumes
uninitialized NZVC: it establishes those flags before the corresponding
branches. RTE restores the exception's saved SR as before.

Four timing distinctions exposed by real execution are now explicit:
predecrement MOVE prefetched before its reversed writes (including memory
sources); indexed JMP consumes fourteen clocks; register CLR.L consumes six;
ADDA/SUBA.L immediate consumes sixteen including its extensions. MOVEM keeps
reversed low/high predecrement stores and the restore's extra word read.
BCD corrections and cumulative zero follow the 68000 behavior, not host
integer decimal arithmetic. The natural and directed oracles compare exact
read/write addresses, masks, values, PCs, timestamps and root register state.

New capture operand lengths include game-flow ROM data reads while excluding
instruction prefetches. An initial filter erroneously classified RTS return
prefetches as data; it was corrected before acceptance captures were made.
The natural baseline retains its earlier CPU state and clocks; its verified
bus denominator grows because previously external callee reads/writes now
participate in assertions. No returned instruction or input read is supplied
from a new callee transcript.

Two labelled 600-frame fixtures complement the original eight service
corpora. `AB2_ORACLE_IRQ4_GAMEFLOW_ENDPOINTS=1` uses ROM-initialized high-score
records, rank/text pointers derived from `$17822`, stages 3/26, all eight
states, BCD limits, physical coin edges and all fifteen coinage selectors.
`AB2_ORACLE_IRQ4_GAMEFLOW_ACTIONS=1` additionally supplies editing/joystick
operands, physical fire/service-credit inputs, special-name operands,
stages 15..18 for the extended-credit HUD path and counter wrap. These are
explicit initial-state fixtures, not assertions of natural credit or attract
producer cadence. Their resulting writes come entirely from MAME/ported CPU
execution. `--require-gameflow` and `--require-actions` enforce the stage,
state and selector coverage; an inactive entry alone cannot satisfy them.

| Acceptance corpus | Envelopes identical/captured | Bus events identical/captured |
| --- | --- | --- |
| Natural 600 frames | 597/597 | 1,142,788/1,142,788 |
| Game-flow endpoints 600 frames | 597/597 | 681,246/681,246 |
| Game-flow actions 600 frames | 597/597 | 613,896/613,896 |
| All ten service corpora | 4,830/4,830 | 9,567,126/9,567,126 |

All are **0 different, 0 skipped**, with **0 supplied callee execution
boundaries**. `$17146` matches 4,830/4,830 root calls and `$C6F8` matches
1,907/1,907; the earlier directed service fixtures deliberately bypass the
credit calls. The separate linked return captures retain computed stack,
USP, restored-mask selection and immediate exception entry.

Seven deliberate mutations fail: stage selector (36,420 bus differences),
coin bit (53,782), ranking operand (13,205), lamp duration (296), BCD correction
(15,032), shift clocks (630,202), and inherited X (1,309). Some mutations
change the executed branch/event count; the runner requires explicit
bus/state differences when a changed path exits the graph, never a bare
exception. Variants and logs remain ignored under `build/oracle`.

Reproduce with `make oracle-capture-irq4-service`,
`make oracle-irq4-service`, and
`python3 tools/oracle/mutation_check.py gameflowstage gameflowcoin gameflowrank gameflowlamp gameflowbcd gameflowshift gameflowextend`.
Make and CMake register both new required corpora. Native Stage 1 remains
active. This closes the two IRQ4 callee graphs; initial interrupted contexts,
physical input bytes, first resumed publication operands, other caller/handler
graphs, sub-produced blit operands and sub-write/progress frontiers still
prevent claiming an independently scheduled whole host. No ROMs, capture
bytes or disassembly output are committed.

Coverage accounting commits observed PCs only when the entire IRQ4 envelope
is emitted; the unfinished trailing window cannot add verified coverage.
Fresh natural, endpoint and action coverage captures independently replay
597/597 envelopes each, zero skips, and together execute **1,384/1,507 added
instruction PCs**. Every added `$C6F8`/dependent coin/HUD instruction and the
`$137E2/$13A2C/$13C3E/$1B658` helpers is exercised. The remaining **123 PCs**
are translated but unexercised, grouped here by the path they belong to:

| Unexercised path | Instruction PCs |
| --- | --- |
| KEY/HIR name variants | `$173EC/$173F2/$173F8`, `$17444/$17448/$1744E` |
| Cursor ramp jump entries and first ramp write | `$17558`, `$1756C/$17570/$17574`, `$17578/$1757C/$1757E` |
| Intermediate performance-grade result | `$176AE/$176B2` |
| Third/fourth highlighted name characters | `$17994/$17996/$1799A/$1799E`, `$179B0/$179B2/$179B6` |
| Right-edge name highlight and wrap-to-zero | `$17A26/$17A2A/$17A2E/$17A32`, `$17AF6` |
| Backspace and final-letter completion | `$17B56/$17B58/$17B5C/$17B5E/$17B60/$17B66/$17B6C/$17B70`, `$17BA0/$17BA6` |
| Blink timeout and cursor glyph variant | `$17CA0`, `$17D26/$17D2A/$17D30/$17D34` |
| Six/eight-digit leading-blank formatting branches | 25 PCs within `$17E6E-$17EA6`, 28 within `$17F40-$17F90` |
| Alternate performance-grade glyph layout | 14 PCs within `$18046-$18074` |
| Attract scroll alternate clamp/update branches | 7 PCs within `$182A4-$182B6`, 7 within `$183BC-$183CC` |

These counts concern instruction execution, not every condition/CCR/value
combination: invalid BCD digits, arbitrary corrupted pointers, all long
arithmetic overflow combinations and nested IRQ6 during every newly translated
instruction remain unverified. Closure means no callee execution boundary or
unported reachable instruction is supplied by the acceptance replay; it does
not mean exhaustive behavior over all possible RAM states.

Final validation: `make test`, `make verify-stage1` (audio RMS 903.949),
`make -j4 oracle-test`, `make -j4`, a 180-frame headless launch and
`git diff --check` pass. Linked IRQ4 returns retain **714/714 identical** RTE
checks and **166/166 identical** immediate entries, zero skips, now without
callee execution transcripts. Existing standalone input-race skips remain
unchanged. CMake registration is updated; that build path was not run.

## Timed compound follower `$7BB6..$7C10`

The bounded caller now translates the eighteen instructions after `$7BB6`
instead of cutting execution at `$7BBC`. The nineteen-PC graph copies the
parent's previous position and byte/word attributes, compares the stage selector
against 7 and 1, selects a runtime ROM animation table, updates a positive
animation byte by four, emits through the already timed `$B628` producer and
returns through `$7C10` to the dispatcher. Registers, XNZVC, stack, operand
reads/writes, ROM prefetches, physical interrupt selection and bus timestamps
come from computed instruction execution. It has no handler execution callback.

Indexed memory-to-memory MOVE.L at `$7C06` takes two internal clocks **before**
its first extension prefetch, followed by seven bus phases. The initial capture
incorrectly counted six phases and omitted those leading clocks. Directed MAME
replay exposed that error; the capture and translation were corrected before
acceptance. Instruction-hook snapshots pin the entry/exit state, including LEA
register changes which occur before the first observable prefetch.

`AB2_ORACLE_CALLER_FOLLOWER=1` adds the follower instructions to the existing
CALL9 capture. `AB2_ORACLE_FOLLOWER_DIRECTED=1` calls the real ROM graph from the
existing mask-writing fixture, with a labelled scratch parent/follower, stages
1/7/2, animation bytes 0/4/8/128 and sprite counts 0/192. Scratch object RAM,
stage and producer count are restored at the explicit fixture cut. These initial
states do not claim natural spawn cadence. The oracle's `--require-follower`
gate requires all nineteen PCs and those stage/animation values; the earlier
closure corpus fails explicitly with `missing follower instruction coverage`.

The directed 120-frame corpus matches **211,030/211,030 instruction/IRQ-entry
records**, **615,762/615,762 caller bus events**, **209,728/209,728 consecutive
CPU boundaries**, **330/330 linked IRQ2/IRQ6 services** and
**14,561/14,561 service bus events**, all **0 different, 0 skipped**. It retains
zero unported post-RTE returns. All nineteen follower PCs execute; the first
three also see IRQ4 entries (8/8/6 respectively). Later follower IRQ boundaries,
positive animation underflow (1..3), every animation-table value, all CCR/input
combinations and nested IRQs at every PC remain unverified.

Two deliberate constant mutations fail: stage comparison 7→6 produces branch
state differences, and animation decrement 4→3 produces bus/register differences
and 39 differing consecutive CPU boundaries. Variants and logs stay ignored
under `build/oracle`. Reproduce with `make oracle-capture-caller-follower`,
`make oracle-caller-follower` and
`python3 tools/oracle/mutation_check.py followerstage followerage`.

The caller report now inventories **unported graph exit PCs** as well as
unported post-RTE PCs. A zero post-RTE count cannot conceal the remaining
handler cuts: the directed corpus still has 762 graph exits, including `$88CA`,
player/input/weapon entries and explicitly restored fixture code. Parent/shared
operands without a computed producer, physical input frames, first resumed wait
operands, IRQ4 service resumptions in this caller corpus and sub execution/
publication progress remain supplied. This is a bounded scheduling checkpoint;
independent whole-host scheduling remains unchecked and native Stage 1 remains
active. Make and CMake register both new corpora; ROMs, disassembly and captures
are not committed.

Fresh natural 600-frame acceptance matches **2,393,888/2,393,888 instruction/
IRQ-entry records**, **7,036,333/7,036,333 caller bus events**,
**2,374,913/2,374,913 consecutive CPU boundaries**, **1,858/1,858 linked
services** and **82,930/82,930 service bus events**, **0 different, 0 skipped**.
All nineteen follower PCs execute naturally: 155 entries/returns and 40 indexed
animation updates. The original closure corpus's 16,413 graph exits become
16,103, closing 310 cuts without changing the 1,858 services or any returned
CPU boundary. Physical visibility matches 21,662,465/21,662,465 observations.
The directed run adds IRQ4 entry coverage and enforced selector/byte endpoints.

The next continuation was `$C082/$C098`; its verified port and the subsequently
exposed initializer are documented below.

Validation: `make test`, `make verify-stage1` (audio RMS 903.949),
`make -j4 oracle-test`, `make -j4`, a 180-frame headless launch and
`git diff --check` pass. The new caller corpora have zero skips; existing
standalone other-CPU input-race skips remain unchanged. CMake test registration
is updated; that build path was not run. Work remains on the local
`codex/host-scheduling-follower` branch; this checkpoint does not complete
independent host scheduling or authorize native retirement.

### Timed shared-ready and world-initializer continuations (2026-10-07)

`OMainIrqService::caller_step` now translates all five instructions at
`$C082..$C098`: previous-position copy, byte BTST of `$29C200` bit 0,
conditional `$B628` sprite append, and RTS. The append call computes its
stack return address `$C098`; returned registers, XNZVC, instruction
prefetches and interrupt-selection phases are assertions.

Continue the newly exposed `$88CA..$8944` world-initializer graph (32 PCs
including its already ported `$88C2` entry). Its two projections execute
real multiplier/divider bus transactions, signed add/subtract, shifts and
viewport offsets. `$B66E` now installs `$B676` and falls into its computed
publication wait. These are scalar instruction operands; ROM tables are
still read at runtime.

The natural 600-frame corpus `caller_ready.bin` passes **2,446,398 of
2,446,398 instruction/IRQ records**, **7,220,004 of 7,220,004 bus phases**,
and **2,433,962 of 2,433,962 consecutive boundaries**, all with **0 different,
0 skipped**. Linked IRQ2/6 services pass **1,874/1,874**, including
**83,648/83,648** bus events. No unported post-RTE boundary remains within
that capture contract. Explicit other-graph exits fall from 16,103 to
**9,548**; this is an inventory of remaining work, not scheduling closure.

Natural ready-bit reads only observe bit 0 set. The directed 120-frame
`caller_ready_directed.bin` requires all five PCs and both bit/branch states,
including the no-append path: **210,903/210,903 records** and
**615,174/615,174 bus phases**, zero differences/skips. The directed world
initializer corpus requires all 32 PCs and signed projection operands:
**216,521/216,521 records**, **633,031/633,031 bus phases**, zero differences/skips.
Mutations `readybit`, `readyreturn`, `initprojection`, `initshift` and
`publicationinstall` each fail differential assertions.

RAM owned by the main caller and its known publication-wait word now survive
linked IRQ2/6 entry, service and RTE. Service snapshots assert those lanes;
they seed only other lanes. This reduces supplied first-resume operands in
the natural corpus from 1,681 to **474**. It does not make the remaining
shared-memory producers independent. `$29C200`, other handler graphs,
and timed sub-CPU progress remain explicit frontiers.

Reproduce with `make oracle-capture-caller-ready`, `make oracle-caller-ready`,
and `python3 tools/oracle/mutation_check.py readybit readyreturn
initprojection initshift publicationinstall`. The new captures and ROM
listings remain ignored under `build/oracle` or temporary directories.

### Linked IRQ4 and subsequent clear/trail graphs (2026-10-07)

The same caller replay now runs IRQ4 through `OMainCallerSchedule::finish`,
including computed callees, RTE mask/USP restoration, physical selection and
immediate entry. Format `AB2CAL10` records IRQ4 returns as assertions; a
companion `.irq4` file from the same MAME run supplies initial/unowned RAM
and sub-produced sprite/road frontiers. No callee registers or completion
times drive execution. Reject truncated linked services and selections.
The two Lua recorders share a snapshot guard; captures close after a complete
service and caller boundary, instead of dropping an in-flight linked IRQ4.

The newly encountered `$B698..$B7C0` graphs now execute their full and strided
sprite-RAM clears, sentinel writes, hardware bank flips, publication waits,
JSRs and returns. Coverage requires all **67 PCs**, including the DBF taken
and exhausted paths. A later IRQ4 return exposed `$AAEE`; port its complete
**99-PC** trail-update graph through `$AC4A` and slot-release `$AE32..$AE4E`,
including growth, ROM curve lookup, both projections, clipping and `$B628`
publication. Indexed destinations retain their internal address clocks;
register shifts/long arithmetic retain their completion tails and XNZVC.

The directed 120-frame trail capture passes **310,463/310,463 instruction/IRQ
records**, **914,356/914,356 bus phases**, and **309,317/309,317 consecutive
boundaries**, **0 different, 0 skipped**. Its gate requires all 99 PCs and
both directions of all six branches: taken/untaken `$AB06` 3/19, `$AB28`
5/14, `$ABCA` 6/13, `$ABD2` 10/3, `$AC2A` 1/2, `$AC36` 1/1. Fixtures use
scratch objects, signed/extreme scalar inputs and real SR-writing instructions;
44 initial/restore scheduler cuts are explicit. `traildrift` and `trailpool`
mutations fail; the refactored shift still rejects `initshift`.

Main-owned sprite RAM follows both hardware banks. A draw write moves ownership
with the exchanged list; MAME's `sega_xboard_sprite_device::draw` then writes
word 7 of every rendered record, including hidden records, up to the end marker.
Those published cursor words are explicitly transferred to the external video
producer. Every main CPU write, including word 7, remains a bus assertion.
Other retained bank words remain assertions. The old model falsely treated the
retired list's cursor bytes as still owned in the new CPU-visible bank.
This ownership correction adds no fabricated video writeback.

The final natural 600-frame corpus passes **2,700,760/2,700,760 instruction/
IRQ-entry records**, **7,969,775/7,969,775 caller bus events** and
**2,690,151/2,690,151 consecutive caller boundaries**, **0 different,
0 skipped**. IRQ4 matches **535/535 services** and **1,030,226/1,030,226
bus events**; IRQ2/6 matches **2,049/2,049 services** and **91,510/91,510
bus events**. All **2,584/2,584 RTE selections**, including 153 immediate
exceptions, and **22,570,488/22,570,488 physical visibility observations**
match. There are **zero unported post-RTE boundaries**. The retained wait
executes 4,798,253 instructions with 14,394,632/14,394,632 bus assertions.
The 8,024 other-graph exits remain explicit; none is silently skipped.
Coverage gates require every clear PC and complete natural trail returns.
`clearstride`, `clearloop` and `linked4return` mutations each fail.

Validation after all ports: `make -j4 test verify-stage1 all`,
`make -j4 oracle-test`, a 180-frame headless launch and `git diff --check`
pass. Stage 1 audio RMS remains 903.949. New corpora have zero skips;
existing standalone race exclusions retain their documented contracts.
CMake registrations are updated; the CMake build path was not run.

Reproduce with `make oracle-capture-caller-allirq` and `make oracle-caller-allirq`.
The natural corpus `caller_allirq_trail.bin` includes gameplay coin/start/input
through 600 frames; `caller_trail_directed.bin` supplies directed clipping and
release coverage. Both are registered in Make and CMake. Sub writes, physical
input frames, hardware cursor results, initial contexts and other handler graphs
remain explicit boundaries: this closes the selected ISR/returned graphs,
not the whole independently scheduled arcade host.

### Timed sound-ring producer continuation (2026-10-07)

`OMainIrqService::caller_step` now translates the fifteen instructions at
`$B84C..$B884`, including disabled-sound ring reset, stage-one suppression,
indexed enqueue, six-bit write-index wrap and pending-byte increment. Runtime
ROM disassembly supplies every branch, operand and constant. CPU state, XNZVC,
stack returns, instruction prefetches, operand bus phases and physical IRQ
selection are computed; no producer execution transcript supplies its result
or duration. Unported callers still supply the initial entry frontier.

`AB2_ORACLE_CALLER_SOUNDPRODUCER=1` extends the existing caller recorder and
read-only instruction-hook snapshots. Word branches infer their entry clocks
from the first bus: two internal clocks when taken, four when untaken.
The indexed byte store has two internal address clocks before its extension
prefetch. The visibility replay retains these producer phases and checks their
physical levels against production `OMainIO`/`XBoardTimeline`.

The labelled `AB2_ORACLE_SOUNDPRODUCER_DIRECTED=1` fixture executes the real
ROM with a real IPL7-setting instruction. It saves/restores sound-ring RAM and
the stage selector across explicit scheduler cuts. This isolates count overflow
from the timer consumer; natural capture supplies ordinary IRQ timing coverage.
`--require-soundproducer` requires all fifteen PCs, both directions of both
branches, ring indices 62/63, counts 0/$7F/$FF and command bytes
0/$7F/$80/$FF. The natural `--require-soundproducer-path` gate requires the
thirteen enqueue/suppression PCs without asserting disabled-reset coverage
or fixture endpoints. Disabled reset is only covered by the directed corpus.

The directed schedule exposed one post-RTE boundary at `$D0DA`: its literal
`ADDQ.W #1,$8B5A` now executes from computed return state, through operand
read/prefetch/write and CCR. Its following `$14976` sampler call remains an
ordinary unported caller frontier. Directed capture requires this added PC;
natural producer capture retains its earlier game-flow boundaries.

The 120-frame directed corpus matches **354,203/354,203 instruction/IRQ
records**, **1,032,308/1,032,308 caller bus phases** and
**352,524/352,524 consecutive CPU boundaries**, **0 different, 0 skipped**.
All **319/319 linked IRQ2 services**, **14,194/14,194 service bus events**,
**319/319 RTE selections** and **3,924,990/3,924,990 physical visibility
observations** match. There are **zero unported post-RTE boundaries**,
44 explicit fixture context cuts and 1,194 explicit other-graph exits.
The directed counter instruction executes 46 times.

Four deliberate mutations are rejected: ring mask `$3F`→`$1F` produces
15 consecutive-state differences; suppression stage 1→2 changes branch/CCR;
pending increment 1→2 produces four consecutive-state differences; the
returned counter increment 1→2 rejects 46 bus events. Temporary variants,
ROM listings and captures remain ignored.

Reproduce with `make oracle-capture-caller-soundproducer`,
`make oracle-caller-soundproducer` and
`python3 tools/oracle/mutation_check.py producerwrap producerstage producercount producercounterreturn`.
Make and CMake register both corpora and their physical visibility checks.
CMake itself was not run. Unexercised ring indices/count/CCR combinations,
interrupts at every producer instruction, arbitrary caller graphs, physical
input/sub frontiers and hardware cursor feedback remain explicit limits.
This checkpoint does not establish independent sound command cadence, a
whole-host schedule, audio waveform equality or retirement of native Stage 1.

Natural 600-frame acceptance matches **2,703,022/2,703,022 instruction/IRQ
records**, **7,976,261/7,976,261 caller bus phases** and
**2,692,200/2,692,200 consecutive boundaries**, **0 different, 0 skipped**.
IRQ2/6 services remain **2,049/2,049**, with **91,510/91,510 bus events**;
IRQ4 remains **535/535**, with **1,030,226/1,030,226 bus events**. All
**2,584/2,584 RTE selections** and **22,576,829/22,576,829 physical visibility
observations** match. No unported post-RTE boundary remains. The natural
corpus does not execute disabled reset, which the directed corpus covers.
Its 8,237 ordinary graph exits include the newly observed producer returns
to unported callers (the earlier inventory had 8,024); extra observed cuts
do not establish or invalidate independent execution outside this graph.

Natural producer coverage is **13/15 PCs**, with **213/213 entries and
returns identical, 0 skipped**: stage-one suppression takes/does not take
42/171 times. Directed coverage is **15/15 PCs**, with **39/39 entries and
returns identical, 0 skipped**: disabled reset takes/does not take 6/33 times,
and stage-one suppression 23/10 times. The older caller corpus explicitly
fails the new gate with `missing sound producer instruction`, proving that
absent graph coverage cannot silently pass.

Final validation: `make -j4 test verify-stage1 all`, full
`make -j4 oracle-test`, a 180-frame headless launch and `git diff --check`
pass. Stage 1 audio RMS remains 903.949. The new corpora have zero skips;
existing standalone other-CPU input-race exclusions retain their prior
contracts. Captures, mutation variants and ROM data remain ignored.


## Phase 3d continuation: timed input-to-target graph and faster port workflow

The bounded caller scheduler now executes buttons `$14976..$1498E`,
axis/target mapping `$B0AC..$B368` and the `$D0DE` BSR into the button routine.
The 171 address-labelled cases preserve ROM instruction order, operand widths,
CCR, indexed addressing, prefetch/write order, internal completion tails and
IRQ polls. Targets include bank/pitch and throttle-derived shared values.
`OMainIO` supplies active-low physical button reads; expected bus bytes remain
assertions. Visibility version `AB2VIS03` records the independent physical
button port alongside ADC inputs, while old `AB2VIS02` corpora remain readable.

The visibility replay retains each production ADC result and derives its raw
sample destination from the selected mux. It asserts the `$14632` store's
address and value against that result; the input graph then consumes these
computed sample bytes. This proves data and destination across the recorded
frontiers, not independent sampler cadence: ADC transactions, store times and
unported caller intervals remain captured. Initial raw stores invalidate sample
provenance explicitly. Directed fixtures at scratch A5 `$3FFC00` supply raw
axis and accumulator endpoints and are counted separately. Natural execution
has one initial throttle lane before its first computed sample. Shared RAM,
external physical input frames, sub writes and renderer cursor words remain
inputs. Native `OEngine` remains active; Phase 3d is incomplete.

Directed fixtures exercise all four physical weapon-button combinations,
X/Y clamp endpoints and adjacent values, signed accumulator deadbands,
cutscene/recovery/bank override/damage/pitch lock paths, both throttle DIP
settings and attract counter transitions at `$2EE/$348`. Stage selection and
attract counter vary independently so their conjunctions are covered.
Coverage requires all 165 production-reachable PCs and both directions of
all 29 applicable conditional branches. The six `$B2C0..$B2DA` debug front-end
PCs and opposite `$B2BC/$B364` debug gates are unverified: production ROM word
`$B0AA` is zero. The debug graph from `$B36A` is deliberately unported.
Directed IPL7 isolates endpoint paths; arbitrary IRQ overlap at every new
instruction remains pending even though natural interleavings are checked.

### Oracle results and failure evidence

| Corpus | Instruction/IRQ records | Caller bus phases | Consecutive boundaries | Skipped |
| --- | ---: | ---: | ---: | ---: |
| Targeted natural 120 frames | 4,236/4,236 | 11,635/11,635 | 4,145/4,145 | 0 |
| Directed 200 frames | 29,614/29,614 | 83,056/83,056 | 28,946/28,946 | 0 |
| Combined natural 600 frames | 2,727,071/2,727,071 | 8,043,168/8,043,168 | 2,716,240/2,716,240 | 0 |

The short natural run has 45 axis/button entries, 134 computed sample reads,
360 computed ADC stores, one initial lane and six initial raw writes.
Its 189,758 physical observations, 45 internal completion polls and one linked
IRQ2 service/45 service bus events match, with zero unported post-RTE returns.
It observes 129/171 PCs and reports 90 external graph exits at `$D0E2/$145B2`.
The directed run has 332 entries per input routine, 497 computed sample reads,
303 labelled fixture reads, one initial lane and 691 computed stores. All
367,418 physical observations, 189 internal polls and four linked IRQ2
services/180 service bus events match. It has 332 deliberate schedule cuts,
664 external exits (including the scratch trampoline), and zero post-RTE holes.
Coverage is 165/171 PCs; no unexercised production path is counted as verified.

The combined natural run has 277 axis/button entries, 790 computed sample
reads, one initial lane and 2,310 computed stores. Its 22,648,587 physical
observations and 340,590 internal polls match. All 2,593 RTE selections match,
including 153 immediate exceptions; IRQ2/6 services match 2,058/2,058 with
91,911/91,911 bus events, IRQ4 services 535/535 with 1,030,226/1,030,226 bus
phases. There are no unported post-RTE returns. It observes 140/171 input PCs;
the directed corpus covers the remaining production paths. Shared snapshots,
147,151 sub writes, 105,114 renderer cursor frontiers and 477 initial wait
operands remain supplied. All 8,237 external graph exits are reported.
These counts are capture-boundary events rather than unique unported
instructions; extending one graph can move its exit without reducing the event
total. They also include `$D0DA`, already ported but outside the natural capture
filter. The next useful Phase 3d batch is timed live flight `$98C0` (256 observed
entries) and its projection/append successors, joining input targets to player
writes before extending paired road acceptance. The high-frequency `$7650/$77AC`
world graph (952/948 exits) is another scheduling gap; frequency alone does not
establish the player/road path.

Five shadow-source mutations fail: timed deadband `$B116` threshold 2 to 3;
`$B168` resync clear mask 4 to 2; `$B2A6` divisor `$71` to `$72`; `$14986`
button inversion removed; and production mux reporting with its channel bit
flipped. Logs stay under ignored `build/oracle/`; the real source is unchanged.
The short natural corpus fails the full directed coverage gate with
`missing input instruction`, proving that a matching incomplete capture
cannot satisfy the stronger claim.

### Faster repeatable workflow

`tools/generate_input_schedule.py` reads a local flat main ROM and emits
fixed-address literal C++ cases plus shared C++/Lua instruction manifests.
Unknown instruction forms fail. Generated artifacts contain instruction
semantics, addresses and scalar timing metadata, no ROM bytes or data tables;
ROM tables remain runtime reads. Run `make oracle-input-generated-check` to
regenerate in check mode against the user's ignored ROM image. This generator
supports this graph's instruction subset; extending it needs disassembly and
phase review plus MAME evidence for every new form.

Use a graph-sized batch, keeping oracle validation before production wiring:

1. Generate/review its literal cases and capture manifest together.
2. Run a short natural replay and directed endpoint/branch fixtures.
3. Require coverage and zero post-RTE holes; reject deliberate mutations.
4. Run the combined natural capture and existing full suite once the batch is
   stable. Retain explicit graph exits and initial/external producer counts.

`make oracle-capture-caller-input` creates a targeted 120-frame natural capture,
a targeted 200-frame directed capture and a combined 600-frame natural capture.
`AB2_ORACLE_INPUT_ONLY=1` records this graph rather than every previously ported
caller instruction. The directed run completes at about 7.6% MAME speed with
one fixture per frame; recorded endpoints/branches still have mandatory gates.
The `-short`, `-directed` and `-full` capture targets can run separately while
iterating. `make oracle-caller-input` replays all three with coverage/closure gates.
`build/oracle_caller` now uses dependency-tracked object files under ignored
`build/oracle_obj`; unchanged sources are not recompiled. `make oracle-caller`
still replays the earlier corpus. Deferring every oracle until thousands of
ports are complete would hide both phase errors and missing fixture coverage;
these per-graph checks reduce turnaround without accumulating that uncertainty.

Final checks pass: `make test`, `make verify-stage1`, `make -j4 oracle-test`,
`make oracle-input-generated-check`, a dummy SDL video/audio 180-frame launch
and `git diff --check`. New input corpora have zero differences and zero
skips. The full suite retains existing read-set race skips in unrelated
captures, with no different records. CMake tests are registered but were not
configured here because `cmake` is unavailable. ROM images, captures, object
files and mutation artifacts remain ignored; this checkpoint is uncommitted.

## Phase 3d continuation: timed live flight and projection

The bounded caller scheduler now executes all 232 instruction PCs at
`$98C0..$9C32`: sequence/damage handler transitions, collision grace,
previous position, draw-only gates, X/Y and animation slew, bank quotient,
runtime pitch/attachment tables, flips, exhaust history, hardware products,
projection, rotation and perspective divisions. `$9ACE/$9ADC` use literal
BSR stack/prefetch timing, and flight calls/tail-jumps into the already timed
`$B628..$B654` request append graph. No native flight routine is substituted.
Tables at `$28BDA/$28C94/$33E98` remain runtime ROM reads.

`tools/generate_input_schedule.py --graph flight` emits fixed-address literal
cases and shared capture/coverage metadata. Added instruction forms are
MOVEQ, EXT.L, SWAP, BSET, ASL.W/ASL.L, ASR.L and register ADD.L/SUB.L.
Long register arithmetic has its four-clock completion tail; long shifts
retain the four-clock base tail plus two clocks per count. Variable-count
`$9B7A/$9B9C` use D1's real operand (the ROM sets it to ten), with separate
count and destination metadata for capture. The input generator's default
output remains byte-for-byte unchanged, checked against the user's ROM.
This is a bounded translation generator, not a general runtime CPU decoder.

Directed fixtures execute real ROM at IPL7 with scratch object A5 `$3FFC00`
and real math chips at A6 `$E4000`. They save each original byte once and
restore scratch records, request output, input/shared flags, history and
scalar operands. Physical RAM/ROM reads and chip results are asserted.
Fixtures cover both directions of all 25 flight conditional branches,
X/Y signed deltas -5/-4/-3/0/3/4/5, animation limits, bank quotient clamps,
attachment flips, table traversal, speed/decay paths, roll quadrants,
sequence/damage/collision/grace/draw-only gates and append counts
0/`$BF`/`$C0`/`$C1`. The grace word `$8D76` overlaps `$8D77`'s bit-2 append
gate; fixtures set the whole word so testing one does not erase the other.

The shared append routine runs for other objects too. Targeted captures retain
its already timed dispatcher/world/follower/return paths, and the closure
gate exposed five previously unported returned instructions: `$427C` BTST,
`$5D68` BTST, `$60CA/$7650` previous-position MOVE.L and `$18E8A` DIP-byte
MOVE. These five literal continuations have a separate metadata/coverage
inventory; the rest of those handler graphs remain external. Already timed
`$8AF2/$D0DA` and their successors are retained rather than marked external
because a capture filter omitted them.

The 600-frame natural capture was recorded before the five returned-instruction
continuations were added to the filter. It checks the 232-PC flight graph,
observes 211 of its PCs and zero of the five extra PCs, with zero unported
post-RTE returns. Directed gates require all 232 flight PCs, all five return
continuations, all applicable branch directions and the endpoint sets. Thus
all five extra instructions have directed evidence; the regenerated short
natural corpus observes three of them. The 600-frame corpus has no natural
execution evidence for them. Regeneration retains them in both capture types.
Arbitrary IRQ alignment at every flight PC is not proven; ROM shift count ten
is verified, not all 64 possible register shift counts.

Caller entries and target/shared-memory producers across unported gaps remain
supplied. The flight graph computes player fields, shared projection/history
writes and append output across its retained instruction chains, but does not
establish independent object dispatch or sub/road/publication cadence. The
conditional road pairing is unchanged. Native `OEngine` remains active and
Phase 3d remains incomplete.

### Oracle results

| Corpus | Instruction/IRQ records | Caller bus phases | Consecutive boundaries | Skipped |
| --- | ---: | ---: | ---: | ---: |
| Targeted natural 120 frames | 352,406/352,406 | 1,025,404/1,025,404 | 351,270/351,270 | 0 |
| Directed 200 frames | 1,032,322/1,032,322 | 3,013,441/3,013,441 | 1,024,025/1,024,025 | 0 |
| Combined natural 600 frames | 2,763,360/2,763,360 | 8,141,527/8,141,527 | 2,752,952/2,752,952 | 0 |

The short run has 44 flight/projection entries, observes 211/232 flight PCs and
3/5 additional continuation PCs, and matches 90/90 linked IRQ2 services,
4,008/4,008 service bus phases, 48,064 internal completion polls and
1,171,803 physical observations. The combined natural run has 256 flight
entries and 172 projections. All 22,743,959 physical observations and 343,342
internal polls match. IRQ2/6 match 2,063/2,063 services and 92,134/92,134 bus
events; IRQ4 match 535/535 services and 1,030,226/1,030,226 bus phases.
All 2,598 RTE selections match, including 153 immediate exceptions.
Every corpus requires zero unported post-RTE returns.

The natural boundary-event count falls from 8,237 to 7,809: `$98C0`'s 256
entries and `$9AE0`'s 172 returns are now inside the timed flight graph.
These remain event counts, not unique instructions. Sub writes, renderer
cursor frontiers and initial wait operands remain supplied as before; the
natural input graph still has 790 computed sample reads and one initial lane.

The directed run observes 232/232 flight PCs and 5/5 extra continuation PCs,
with 333 flight entries and 313 projections. All 3,210,611 physical
observations, 127,929 internal completion polls and 274 RTE selections match;
273 IRQ2 services/12,150 service bus phases and one IRQ4 service/869 phases
are retained, with zero post-RTE holes. The directed IRQ4 envelope has 553
supplied sub writes. Its 332 explicit scheduler cuts mark
fixture setup/restoration; ordinary graph exits remain reported. These
counts include the retained dispatcher/world/follower closure, not just
scratch-object execution.

The five main flight mutations reject a changed X slew threshold, division
operand, product shift count, vertical attachment SWAP and variable shift
count. Return-continuation mutations check Z polarity, the negative source
offset, both previous-position prefetches and the DIP-byte address. Temporary
variants and logs remain under ignored `build/oracle/mutations`; real source
is never changed. The `$427C` operand corpus does not distinguish bit 1 from
bit 2: changing that mask alone passes, so distinguishing flag patterns remain
unverified. The ROM-defined bit-1 mask is retained, with Z-polarity failure
evidence instead. This limitation is outside the flight branch coverage claim.

Use `make oracle-capture-caller-flight-short`, `-directed` or `-full` to record
one corpus, and `make oracle-caller-flight` to replay all three with mandatory
coverage and closure gates. `make oracle-flight-generated-check` checks the
237 literal cases and manifests against the user's ignored ROM image; the
core flight and extra-return inventories are distinct. Targeted flight
captures retain the dispatcher/world/follower closures needed by shared append,
so their speed is about 2.8–3.3% versus 1.86% for the combined capture. They
still avoid replaying every earlier caller graph while iterating. Keep the
combined natural replay and complete existing oracle suite for the stable
batch; do not defer oracle validation until the entire remaining graph is ported.

Final validation passes: `make test`, `make verify-stage1`,
`make -j4 oracle-test`, both generated-file checks, a dummy SDL video/audio
180-frame launch and `git diff --check`. All ten deliberate mutations above
are rejected (five main flight mutations and five continuation mutations).
The short natural corpus fails the full directed gate with `missing flight
instruction`, so an incomplete matching capture cannot satisfy it. New flight
corpora have zero differences and skips; unrelated full-suite read-set race
skips remain reported. CMake registrations were added but remain unconfigured
because CMake is unavailable here. ROMs, captures and mutation/build artifacts
remain ignored; changes are uncommitted.

## Phase 3d continuation: timed shadow and exhaust companions

The bounded caller translation adds 179 instruction PCs: 52 for shadow
initialization/update `$9C34..$9D2A`, and 127 for the three exhaust
initializers, primary/sibling/tail updates and shared append gate
`$AE8C..$B0A8`. Shadow init calls the real timed update via `$9C92`, update
calls the existing request append via `$9D26`, and exhaust calls it via
`$B0A4`. The stage/ready/damage/draw-only gates, previous-position copies,
handler writes, scale bits, signed lifetime decrement, companion record
addressing and hardware multiply/divide phases are literal ROM operations.
Tables at `$29018/$2939C` and their pointed records remain runtime ROM reads.
The sibling exhaust's BGT treats zero bank differently from the primary's
BGE; both branches retain their original signed conditions.

`generate_input_schedule.py --graph companion` emits the fixed-address cases
and shared timing/coverage manifest. New forms are ORI.W memory read/modify/
write and ADDQ.B with byte-width XNZVC, preserving D0's upper 24 bits.
Indexed MOVE.L into a displacement destination retains source extension,
source data, destination extension, write and final prefetch ordering.
Input and flight generated outputs remain unchanged.

Directed fixtures call eight entry points through an IPL7 SRAM trampoline.
They supply initial state only; MAME executes all ROM instructions, table
reads, math-chip transactions and appends. Scratch compounds use `$3FF400`
with parents at -64/-128 and initialized children at +64/+128. Per-byte
save-once restoration handles overlapping fields. Fixture gates require
all 179 PCs, both directions of every conditional branch, byte operands
`00/7F/80/FE/FF` at both ADDQ sites, bank `-5/-1/0/1/5` at both comparisons,
signed lifetime `0/1/2/8000/FFFF`, all four pitch rows, both table choices
and append counts `0/BF/C0/C1`.

The first directed fixture attempt was rejected by physical visibility:
late injections could suspend a just-completed RTS, then re-enter CURPC
with its already-popped stack. Moving scratch alone did not fix that.
Companion fixtures now inject only at the read/branch publication/wait
instructions `$B676/$B67C/$14856/$1485C`, which have no partial stack
side effects. Fixture numbering advances only on accepted injections.
These explicit CPU context cuts do not establish independent caller cadence.
The failed directed corpora are not validation evidence. The safe run then
exposed one post-RTE `$77AC` boundary, a literal RTS from another object
graph. It is tracked separately from the 179 companion PCs and validated
by the refreshed directed capture. The earlier natural captures did not
filter this PC, so natural execution of it is unverified in these corpora.

Natural validation is 120 frames (targeted) plus 600 frames (combined):

- Short: 345,465/345,465 instruction/IRQ records, 1,007,848/1,007,848
  bus events, 344,467/344,467 chained boundaries; 97/179 companion PCs.
  All 89 IRQ2 services and 3,963 phases, 89 RTE selections and 47,406
  internal completion selections match; 1,154,820 physical observations.
  There are zero post-RTE holes and 909 external exit events.
- Combined: 2,773,349/2,773,349 instruction/IRQ records,
  8,178,433/8,178,433 bus events, 2,764,055/2,764,055 chained boundaries;
  101/179 companion PCs. There are 258 shadow, 256 primary exhaust,
  256 sibling exhaust and 172 tail updates. All 2,065 IRQ2/6 services
  and 92,229 phases, 535 IRQ4 services and 1,030,226 phases, 2,600 RTE
  selections (153 immediate) and 343,517 internal completion selections
  match; 22,779,786 physical observations. Zero differences or skips,
  zero post-RTE holes, 6,693 external exit events versus the previous
  flight checkpoint's 7,809. This is event counting, not a unique PC census.

The natural paths do not cover 78/179 companion PCs. Exhaust table rows
outside the four directed rows, arbitrary combinations of fixture operands
and arbitrary IRQ overlap at every bus phase are not established by this
checkpoint. Independent caller
entry/cadence, external handler target changes, supplied sub writes/blit
operands, renderer feedback and publication/video frontiers remain pending.
The native `OEngine` flight remains active and the paired road/video evidence
is unchanged. Bounded companion equality does not complete Phase 3d.

The refreshed 320-frame directed capture matches 1,825,481/1,825,481
instruction/IRQ records, 5,374,375/5,374,375 bus events and
1,815,956/1,815,956 chained boundaries, zero differences/skips. It covers
179/179 companion PCs, both directions of all 17 conditional branches
and every endpoint gate above, plus 1,234 `$77AC` RTS observations. All
487 IRQ2 services/21,670 phases, 30 IRQ4 services/9,671 phases, 517 RTE
selections (four immediate), 252,290 internal completion selections and
5,672,117 physical observations match. The 213 injected calls produce
426 explicit context cuts; there are zero post-RTE holes and 9,008
external exit events. Requiring complete companion coverage on the short
natural corpus fails as intended; the path gate is not a completeness gate.

Seven temporary mutations are rejected: byte ADDQ replaced by word-width
flags (28 chained differences), sibling bank BGT changed to BGE (missing
computed bus phase), sibling frame lane 4 changed to 0 (160 bus differences),
primary ORI scale 7F changed to 7E (28 bus differences), shadow ASR8 changed
to ASR7 (247 instruction differences), `$77AC` stack pop 4 changed to 2
(21,356 chained differences), and indexed source prefetch AFAA changed to
AFAC (41 bus differences). Variants exist only under ignored
`build/oracle/mutations`; production sources are unchanged by mutation runs.

Validation passed `make oracle-caller-companion`, the existing full oracle
suite via `make -o oracle-caller-companion oracle-test`, `make test`,
`make verify-stage1`, all three generator checks, the 180-frame dummy SDL
headless run and `git diff --check`. The new corpora have zero differences
and zero skips. Unrelated existing corpora retain their documented read-set
race skips with zero different records. CMake registrations were added but
remain untested here because CMake is unavailable. Captures, ROM images and
mutation artifacts remain ignored. This checkpoint is uncommitted.

## Phase 3d continuation: bounded sub publication waits

`OSubPublication` translates eight sub CPU instruction PCs: `$452/$456`
(TST.W/BGT publication completion), `$458` (BSET road-ready bit 1),
`$556/$55A/$55C` (TST.W/BGE negative-ready wait and RTS), and `$640/$646`
(BTST/BNE main mutex wait). It computes the ROM prefetches, operand/stack
phases, byte lanes, CCR, next PC and 80 ns clocks. BGT exits at zero while
BGE keeps waiting. It does not replace road/sprite routines with a guessed
frame duration or implement the sub CPU's interrupt/trap service.

The matching capture records a two-CPU signal ledger. Main `$14674/$14798`
set/clear mutex bit 0, `$1467C` decrements the ready word, and `$1486E`
arms it to one. Each RMW operand is read from retained shared RAM at its
recorded read delivery frontier, compared, and latched for the later write.
Writes are computed from that operand, never from expected write bytes.
Sub signal reads retain the resulting RAM throughout instruction chains and
external IRQ/callee gaps. Only initialization/explicit fixtures and other
unported signal writers supply values. Stack operands remain entry inputs.

Cross-CPU local timestamps cannot determine shared-bus delivery order in
MAME. The short capture initially mismatched 39 mutex reads when a delivered
main write carried a later local timestamp than the observing sub read.
The capture now records transaction order separately from both CPU clocks;
that delivery order is a supplied hardware frontier, not an inferred delay.
Main operand-read timing, signal-write timing and CPU arbitration therefore
remain external even though their signal values are computed. This is a
bounded sub scheduling checkpoint, not independently scheduled gameplay.

Sub debugger snapshots explicitly select the sub CPU. Supervisor IRQ/trap
accesses are excluded even when CURPC still names the interrupted wait;
partial instruction records at those boundaries are explicit cuts. The
failed captures before these fixes are not validation evidence. Directed
fixtures supply `0/1/7FFF/8000/FFFF/FFFE` separately at each ready-read site.
They update RAM and the physical MAME tap value together and are labelled
external inputs in the ledger; they do not establish natural producer cadence.

All three corpora cover all eight PCs and both directions of all three
conditional branches, with zero different or skipped accepted records:

| Capture | Instructions identical | Bus phases identical | Chained boundaries identical | Main signal read/write assertions |
| --- | ---: | ---: | ---: | ---: |
| Natural 120 frames | 1,261,796 / 1,261,796 | 3,170,352 / 3,170,352 | 1,260,693 / 1,260,693 | 753 / 753 |
| Directed 160 frames | 1,661,912 / 1,661,912 | 4,181,002 / 4,181,002 | 1,660,704 / 1,660,704 | 1,018 / 1,018 |
| Natural 600 frames | 6,305,996 / 6,305,996 | 15,895,267 / 15,895,267 | 6,303,749 / 6,303,749 | 3,872 / 3,872 |

The short/full captures compute 402/2,081 main signal writes and check
351/1,791 latched main RMW reads. Each retains two initialization writes,
one partial IRQ cut and 1,103/2,247 sub entry/IRQ/callee frontiers.
There are 97/827 supplied cross-CPU delivery leads. Directed execution
computes 547 signal writes and 471 operand reads, with 14 supplied writes
(two initialization plus twelve fixtures), one partial IRQ cut, 1,208 sub
frontiers and 141 delivery leads. Both TST sites exercise all six readiness
endpoints separately; a single site's coverage cannot satisfy the other.

Six mutation checks are rejected: changing BGE to BGT produces 609,770
different phases; release bit 1 to bit 2, 26,561; the TST prefetch address,
804,733; RTS stack advancement, 370 different instructions; the main ready
decrement, 604,488 different chained boundaries; and the rearm value,
1,208,906. Mutation binaries and logs remain ignored under `build/oracle/`.

Unverified paths remain explicit: sub IRQ entry/RTE and interrupt selection,
the excluded partial instruction overlap, unported sub callees and their
road/sprite durations, independent cross-CPU arbitration, main signal
producer timing, and stack values outside the supplied entry contexts.
The active native `OEngine` is unchanged. This separate sub capture does not
reduce the previously reported 6,693 main graph exits, extend independent
host acceptance, or establish independently timed road/video publication.
The next bounded milestone is sub call/return and sprite/road progress.

Validation passes: the three `oracle-subpublication` replays, all six new
mutation checks, the existing `oracle-test` targets, `make test`,
`make verify-stage1`, the game build, the 180-frame dummy SDL headless run,
and `git diff --check`. Existing corpora retain their documented read-set
race skips with zero different records. CMake registrations were added but
remain untested because CMake is unavailable here. Captures, ROM images
and mutation artifacts remain ignored. This checkpoint is uncommitted.

## Phase 3d continuation: bounded sub publication callers

`OSubPublication` now translates the eight caller PCs
`$432/$436/$43A/$43E/$442/$446/$44A/$44E`, extending the previous eight
wait/release instructions. The four BSR.W sites compute the user-stack
return address, high/low stack writes, target prefetches and 18-cycle timing.
`$432` joins the retained `$556/$55A/$55C` readiness wait and RTS back to
`$436`. `$43E/$44A` enter the still-supplied `$5C4/$5EE` graphs; `$44E`
joins `$640/$646` before sprite execution remains external at `$648`.
The two memory LSR.W instructions shift the `$300/$302(A6)` request words
one bit, compute NZVC/X, and publish their RMW result. The two BCC.W
instructions compute both the optional-call and skip paths, including their
12/10-cycle timing and ordered prefetches. No road/sprite duration is guessed.

The `AB2SUBP3` capture additionally records unported sub shared-RAM writes
(including supervisor stack writes) and main writes to the two request words.
They are explicit supplied writer frontiers. Replay retains byte-lane ownership:
computed stack/request values and supplied writer values survive entry gaps;
expected operand reads can seed only previously unknown lanes. All three
corpora need zero such seeded operand words. Adjacent equal-time boundaries
must match the complete computed CPU state, including PC; a wrong target
cannot silently turn a consecutive instruction into an external entry.
Cross-CPU transaction delivery order, external writer data/times and unported
callee/IRQ contexts remain inputs, exactly as labelled in the replay output.

The first exploratory capture omitted BSRs because live USP has already
advanced to the decremented stack at the write tap. Its first-write filter
was corrected while the pre-instruction debugger snapshot retains entry USP.
Untaken BCC.W first-read attribution was also corrected to four cycles after
entry (taken branches and BSR writes start after two). Those exploratory
results are not acceptance evidence; fresh captures below use the corrected
boundaries. Existing capture targets regenerate the new schema; older
`AB2SUBP2` files must be recaptured.

| Capture | Instructions identical | Bus phases identical | Chained boundaries identical | Main signal read/write assertions |
| --- | ---: | ---: | ---: | ---: |
| Natural 120 frames | 1,263,778 / 1,263,778 | 3,176,964 / 3,176,964 | 1,262,998 / 1,262,998 | 753 / 753 |
| Directed 160 frames | 1,651,691 / 1,651,691 | 4,151,246 / 4,151,246 | 1,650,796 / 1,650,796 | 1,018 / 1,018 |
| Natural 600 frames | 6,309,438 / 6,309,438 | 15,906,759 / 15,906,759 | 6,307,734 / 6,307,734 | 3,872 / 3,872 |

All accepted records have zero differences and zero skips. Each corpus
requires all sixteen PCs and both directions of all five conditional
branches. Directed fixtures separately exercise `0/1/7FFF/8000/FFFF/FFFE`
at both readiness sites and both shift sites (24 labelled physical fixtures).
Those shifts can change subsequent real sprite-call activity, so directed
instruction totals supersede the previous fixture's totals. Natural full
coverage includes 571 `$432` calls, 569 calls at `$44E`, six at `$43E`,
twenty at `$44A`, and 594 readiness RTS instructions. Supplied entry USP
values are `$A3F74/$A3F7C/$A3F80`; arbitrary stacks are not established.

The short/directed/full streams retain 780/895/1,704 supplied sub entry,
IRQ or callee frontiers, 98/139/830 supplied delivery leads and one excluded
partial IRQ cut each. They compute 402/547/2,081 main signal writes and
check 351/471/1,791 main RMW operands. Their expanded ledgers supply
189,764/258,378/979,592 other shared-RAM writes, including initialization,
unported sub work, request writers and fixtures. These are boundary inputs,
not newly ported instructions or assertions of independently scheduled work.

Four deliberate port mutations are rejected using the directed corpus:
BSR return word `PC+4` to `PC+2` fails its stack phase and retained RTS chain;
`$44E` target `$640` to `$642` fails target prefetches and its next chain;
shift one bit to two fails 63 instructions, 102 phases and 23 chains;
inverting the BCC carry test fails 702 instructions and 1,404 phases.
The wrong-return/target variants stop at the resulting unported PC after
reporting mismatches. Mutation copies, executables and logs remain ignored
under `build/oracle/`; production constants are unchanged.

Unverified boundaries remain sub IRQ entry/RTE/selection, the partial IRQ
cut, `$5C4/$5EE` callee execution, sprite instructions from `$648`, road/control
work from `$45E`, external main producers, independent bus arbitration and
arbitrary entry stack/flag contexts beyond these corpora. Main caller graph
exit counts and the conditional road/video claim are unchanged. Native
`OEngine` stays active; this completes another bounded Phase 3d task, not
whole-host scheduling or Phase 3d acceptance.

Validation passes: all three refreshed `oracle-subpublication` corpora,
all four new mutation checks, `make test`, `make verify-stage1`,
`make -j4 oracle-test`, the game build, the 180-frame dummy SDL video/audio
launch and `git diff --check`. Existing oracle corpora retain their documented
read-set race skips with zero different records. CMake registrations are
unchanged and remain untested because CMake is unavailable here. ROM images,
captures and mutation artifacts remain outside tracked source. This checkpoint
is left uncommitted.


## Phase 3d continuation: independently timed bounded sprite/road progress

`OSubProgress` adds 1,136 literal instruction PCs to the sixteen publication
caller/wait PCs. The generated, address-labelled cases cover `$45E-$554`,
`$594-$62C`, `$648-$9E2`, `$C0A-$11D2`, `$16D4-$174E` and `$1758-$19E6`:
road dispatch and initialization, sprite counting sort/transform/interpolation,
bank/pitch/rotation updates, normal road rasterization and special-road work.
Every data/table read goes through `SubBus` into the user's runtime ROM.
`tools/generate_sub_schedule.py` emits only this bounded instruction subset;
unknown instruction forms fail. It generates the C++ cases, metadata and Lua
capture manifest together; `make oracle-subprogress-generated-check` requires
reproducibility against the local sub ROM. This supersedes the previous
checkpoint's external `$5C4/$5EE/$648/$45E` graph boundaries.

The port computes each instruction's ordered 16-bit bus phases, byte lanes,
NZVC/X, register/stack updates, branch targets and completion time at 80 ns
per CPU cycle. Phase sequences follow local MAME `m68000-sdf.cpp` microcode:
long RMW writes use low then high words, predecrement stack writes retain
their order, indexed JMP fetches its target without rereading the extension,
indexed LEA includes its internal idle between prefetches, and TAS writes
before its final prefetch. Variable register shifts include every internal
cycle; SUBX consumes X and retains sticky Z. BSR/JSR/RTS retain the computed
user-stack words across callees. No routine duration or 30/60 Hz work timer
is supplied to this component. Road swaps and control writes use production
`SubBus` callbacks with retained displayed/working buffers in replay.

The `AB2SUBT1` capture records measured sub instruction bus phases and
pre-instruction CPU snapshots, plus explicit main/unported-sub writer
transactions. Adjacent captured instruction times determine whether a chain
must retain computed state; a wrong computed completion time cannot turn a
chain into a supplied entry. Internal completion-tail CPU state is asserted
by the next instruction snapshot when contiguous. An interrupted tail or an
unported next boundary remains explicitly counted rather than claiming
unobserved final registers. Unknown operand-lane seeding is an error in all
three acceptance corpora. Main signal RMWs retain their latched input; other
producer values and cross-CPU delivery order remain supplied.

Directed fixtures run the actual ROM instructions from a RAM JSR trampoline
with a real SR-writing instruction, starting only at safe user-mode waits.
They save/restore shared RAM and CPU context and label every injected write
as an external fixture producer. During a fixture, readiness writes are held
at its labelled input so the live main CPU cannot re-arm the initialization
wait. Matrix inputs include sprite count/zoom/anchor/flip/interpolation,
clipping positions, stage/floor, recenter/kick thresholds, pitch clamps,
roll quadrants, horizon/ring history, road mode/distance endpoints and delay state.
The isolated trailing `$11D2` RTS is called directly: this verifies its
instruction, not a naturally reachable caller. No fixture executes a native
substitute for a sprite or road routine.

| Capture | Instructions identical | Bus phases identical | Consecutive states identical | Main signal assertions |
| --- | ---: | ---: | ---: | ---: |
| Natural 120 frames | 2,175,016 / 2,175,016 | 5,103,758 / 5,103,758 | 2,174,881 / 2,174,881 | 753 / 753 |
| Directed 360 frames | 6,171,481 / 6,171,481 | 13,787,074 / 13,787,074 | 6,170,826 / 6,170,826 | 2,164 / 2,164 |
| Natural 600 frames | 11,896,153 / 11,896,153 | 27,415,741 / 27,415,741 | 11,895,390 / 11,895,390 | 3,872 / 3,872 |

The natural full run covers 658/1,136 new PCs and computes 580 road swaps
and 568 control writes. It checks 809,112 completion-tail successor states;
181 tail successors remain supplied (returned PCs `$744/$754/$762/$798/
$7B2/$85A/$99E/$D74/$E3C`). It retains 763 entry/IRQ boundaries, two
excluded partial IRQ cuts, 2,081 computed main signal writes, 1,791 checked
main RMW reads, 405,556 supplied external writes and 360,433 supplied
cross-CPU delivery leads. These writer timestamps/order are boundary inputs,
not independent arbitration evidence. All intermediate exploratory files
stay ignored; only fresh corrected corpora are acceptance inputs.


Mutation proof uses ignored copies of the native port, never the production
constants. Six mutations on the 360-frame exploratory matrix reject sprite
origin `160 -> 161` (151,883 phase differences), road blank word
`$08000800 -> $08010800` (12,800), raster origin `$4B0 -> $4B1` (67,825),
special-road stripe `$080D080D -> $080C080C` (448), indexed JMP idle
`6 -> 4` cycles (8,391,143), and clearing the SUBX input carry (24,692).
A seventh request-limit mutation `$FF -> $FE` on the first corrected directed
capture rejects five instruction states and four consecutive states at the
255-request endpoint, even though its bus phases and branch target match.
These checks use explicit mismatch detection, without the all-PC coverage
gate being the reason for rejection. Copies, executables and logs stay under
ignored `build/oracle/`; no ROM data or captures enter tracked files.


All accepted records have zero differences, zero skips and zero supplied
unknown operand words. Directed acceptance requires all 1,136 new PCs, all
sixteen prior PCs, both directions of the five publication branches and
`0/1/7FFF/8000/FFFF/FFFE` at both readiness and request-shift sites. It
computes 861 road swaps and 837 control writes, checks 560,317 completion-tail
successor states and retains 76 supplied tail successors. Its ledger retains
655 entry/IRQ/fixture boundaries, 136 excluded partial IRQ/fixture cuts,
3,002,480 external writes and 82,136 cross-CPU delivery leads. The graph
fixture matrix has 135 calls; publication endpoints have 24 fixtures.

Across the three corpora, the taken directions at `$6E0` (BEQ), `$1744`
(BEQ) and `$18CA` (BNE) remain unexercised. The repeated/taken DBF iterations
at `$534/$10E2/$110E/$118A/$11B6` remain unexercised; their terminal
iterations pass. These paths remain unverified despite full instruction-PC
coverage. Arbitrary request/descriptor counts, all runtime ROM table indices,
every stage and arbitrary stack/flag contexts are not established by this
matrix. Completion-tail frontiers have the limitations described above.

This completes independently timed **bounded sprite/road instruction
progress**, not whole-host Phase 3d acceptance. Sub IRQ entry/RTE/selection,
main producer timestamps/operand data, cross-CPU bus arbitration and renderer
composition remain supplied. `OEngine` still uses native Stage 1. The next
bounded scheduling work is sub IRQ/resumption and independent delivery across
the verified main/sub publication components.

Validation passes: `make test`, `make verify-stage1`, all `oracle-test`
prerequisites and root checks (run in separate batches while capturing),
`make oracle-subprogress` with all three fresh-schema corpora, all seven
mutation checks, `make oracle-subprogress-generated-check`, the game build,
180-frame dummy SDL video/audio launch and `git diff --check`. Existing
oracles keep their documented read-set race skips and have zero differences.
CMake registrations were added but are untested because CMake is unavailable.
Changes are left uncommitted; ROMs/captures remain ignored.

### Independent sub IRQ and main/sub arbitration checkpoint (2026-10-08)

`OSubIrq` extends the bounded sprite/road stream with the initial `$420/$428`
user wait, IRQ4 exception entry, all thirteen `$55E..$592` handler PCs, MOVEM
save/restore and RTE. `instruction()` selects IRQ from a caller-supplied physical
IPL callback at the actual microcode selection point. Exception markers are
assertions in the linked oracle; they cannot replace a computed selection on a
continuous boundary. USP, SSP, registers, CCR, return PC and local time survive
each linked service. No opcode decoder or ROM tables are embedded.

Entry uses six internal clocks, the low return-PC write, autovector VPA E-clock
synchronization, SR/high-PC writes, vector reads and the two handler prefetches.
E phase follows **CPU total cycles**, with the initial CPU/wall-clock epoch
retained in `cycle_bias`; it does not assume that wall time divided by 80 is the
CPU counter. RTE restores SR before return prefetch and switches to the retained
user stack. The ISR polls byte `$9C000`, updates the signed countdown at
`$9C049`, sets pause bit 0 at `$9C048`, delays using `$39`/DBF, then restores all
fifteen saved registers. The discarded final MOVEM data read is retained.

`XBoardSubIrqSelection` delivers VBLANK at completed buses and instruction
completion tails. Internal clocks alone do not deliver an edge. The extended
port exposes selection points in publication and sprite/road instructions,
including RMW writes, branches, returns and post-prefetch register operations.
The stricter continuation test found and corrected `$99A BSET #4,D0`: the
low-register operation has **two**, not four, internal completion clocks in
MAME `m68000-sdf.cpp::bset_imm8_ds_df`. A continuation is identified by its
captured PC and explicit context-boundary markers, never by agreement with the
port's predicted duration. For adjacent internal-tail instructions, the next
actual MAME snapshot supplies the full completion-state assertion. This closes
the old `$99E` duration discontinuities; raw capture phase times and snapshots
are unchanged. Labelled fixture trampoline writes at `$A3E00/$A3E40` are CPU
context boundaries, distinct from RAM-only endpoint fixtures.

Fresh sub captures and the strengthened checks:

| Capture | Instructions including exception entry | Bus phases | Linked state/selection boundaries | Complete IRQ returns |
|---|---:|---:|---:|---:|
| Natural 120 | 2,314,880/2,314,880 | 5,655,572/5,655,572 | 2,314,870/2,314,870 | 119/119 |
| Directed 360 | 6,312,162/6,312,162 | 14,416,612/14,416,612 | 6,311,882/6,311,882 | 359/359 |
| Natural 600 | 12,072,480/12,072,480 | 28,083,147/28,083,147 | 12,072,468/12,072,468 | 599/599 |

Every accepted comparison is identical, zero different and zero skipped.
There are respectively 120/360/600 entries; each capture stops with one service
still active. Directed coverage requires all 1,136 sprite/road PCs, all thirteen
ISR PCs, both directions of the mutex/countdown/DBF branches, and countdown
bytes `0,1,2,$7F,$80,$FF`. Labelled main RAM writes hold the ISR mutex for two
polls and exercise both prior pause-bit values. Initial shared RAM is an
explicit input; no read assertions seed operand bytes. Natural 600 retains
809,293 complete tail continuations and zero supplied tail continuations.
The remaining 12 natural entry/callee frontiers include startup work outside
this bounded graph. Three/four natural and 120 directed incomplete capture or
fixture cuts are outside the accepted records, and are reported separately.

Nine sub-side mutations are rejected: E-clock epoch, RTE stack bank, restored
X bit, MOVEM word order, delay count, IRQ-mask eligibility, pause bit, omission
of completion-tail delivery, and restoring the old `$99A` four-clock tail.
Artifacts remain ignored under `build/oracle/subirq_mutations/`.

`XBoardBusArbiter` ports MAME `schedule.cpp`'s grant policy for the configured
main/sub/Z80 order. It keeps attosecond precision in a 128-bit host clock,
converts remaining grant time to whole CPU clocks, retains CPU overrun, contracts
later CPU targets after an earlier CPU yields, and handles zero-progress abort
returns. Default quantum is `1/6000 s`; main IRQ and sub-reset boosts request
four main clocks (320 ns) for 100 us. Quantum expiry is processed at outer timer
delivery, matching MAME, rather than at every inner grant. Shared transactions
are delivered in CPU execution order; they are not sorted by CPU-local time.

A bus-only capture was insufficient: a grant can have no program bus access,
and the first observed sub access can occur after main's local clock has
already advanced. `build_scheduler_probe.py` therefore compiles a separate,
read-only observation build using the user's existing macOS MAME build objects.
It never changes the source checkout, existing objects, or original binary.
Its ignored copy of `schedule.cpp` records timer boundaries, quantum requests,
all CPU budgets/returns, and a private grant counter exposed only to Lua.
The counter distinguishes repeated grants at the same global time, including
zero-progress aborts. The counter/logging do not change gameplay or scheduling.
The old bus-only inferred grant test was rejected and is not an acceptance gate.

The fresh 120-frame arbitration check matches **239,177/239,177 grant groups**,
**620,495 CPU runs**, **8,319,935 ordered main/sub phases**, and
**1,271,730/1,271,730 shared reads**, zero different/skipped. It includes 37,250
groups without captured program phases, 496 quantum requests, 4,030,442
cross-CPU local-clock leads, and 37 shared reads after a main write whose local
timestamp is later than the sub read's. All 142,296 producer writes are labelled
inputs; shared reads are assertions only. Budgets, global/local time, quantum,
and CPU order are computed and compared. Reversed CPU order, an extra budget
clock, a five-clock IRQ quantum, and a 60-us boost duration all fail. A 101-us
duration mutation was not distinguished by this capture's timer boundaries;
that nearby expiry sensitivity is not claimed verified.

**Scope:** the arbiter oracle supplies external device timer deadlines, board
boost-request times, and CPU completion/producer frontiers for unported work.
The sub stream independently computes its bounded instructions and IRQs; the
arbiter independently checks grant policy and RAM delivery from those labelled
frontiers. The two checks do not yet execute the full main/sub instruction
streams together under one native host. Main producer cadence, other caller
exits, complete video/renderer and sound ownership still block whole-host
acceptance. The native Stage 1 reconstruction remains active. The eight earlier
sprite/road branch-direction limits still apply; all-PC coverage does not
claim all branch directions, table indices, SR masks, reset/suspend scenarios,
or arbitrary device timer interactions.

Commands (captures/probe binary and copied MAME source stay ignored):

```bash
make oracle-capture-subirq-short oracle-capture-subirq-directed oracle-capture-subirq-full
make oracle-subirq
make oracle-capture-bus-arbiter MAME_SOURCE=/path/to/prebuilt/mame/source
make oracle-bus-arbiter
```

The scheduler probe currently expects the existing SDL3/macOS/Clang Release64
build layout and ABI. Make oracles, ROM-free checks, Stage 1 checks and generated
translation checks are the acceptance commands; CMake targets are also declared.

Validation for this checkpoint: `make test`, `make verify-stage1`,
`make oracle-test`, `make oracle-subprogress-generated-check`, the game build,
and `git diff --check` pass. The long-clock/zero-progress ROM-free checks also
pass. Existing oracle race exclusions remain reported by their original gates;
the new accepted IRQ and arbitration comparisons have zero skips. CMake is not
installed in this environment, so its declared targets were not executed.

### Timed weapon dispatcher/controller/fire/allocation checkpoint (2026-10-08)

The bounded caller now executes `$9D34..$9DEA` and `$9DF8..$9F06` rather
than supplying the weapon controller, missile/Vulcan gates and allocation
progress. `omainweaponschedule.inc` contains 107 literal instruction PCs;
`tools/generate_input_schedule.py --graph weapons` reads the user's ROM and
emits matching capture/coverage manifests. There is no runtime decoder,
embedded template data or native `OEntry` adapter. The existing independent
weapon routine oracles remain distinct from this caller/timing check.

The original `$145AC/$145B0` dispatcher reads slot 130's current handler and
pushes its actual return stack. `$9D84/$9D88` call the fire graphs, which use
shared button history, display/sequence/debug gates, signed ammo, cadence and
reserve limits. The allocator pops a runtime free-slot index, performs WORD
shifts/address arithmetic, activates its object, installs `$9F7C/$A030`,
updates launch-side/ammo/counters and calls the HUD or sound producer. Those
new projectile initializers remain outside this timed graph; activation of
later slots is preserved by the already ported live dispatcher. `$B628` and
`$B84C` execute their existing checked instruction graphs, retaining controller
registers, active stack, RAM ownership, CCR and time across the calls.

`$9D6A` has a BEQ target equal to its fall-through address. A program-address
heuristic incorrectly attributed its first bus to the untaken timing path.
The corrected capture uses the independent instruction-hook SR snapshot to
select the start offset; raw MAME bus observations remain untouched. Directed
branch coverage also uses that snapshot's Z flag to distinguish the two
paths. The capture now includes the already ported `$D0DA` in every sound
producer corpus: the weapon fixtures exposed one interrupted return there
that the earlier directed capture had unnecessarily marked external.

Natural 120-frame boot/attract replay: **465,074/465,074 instruction/IRQ
records**, **1,364,220/1,364,220 bus events**, zero different/skipped;
44 actual dispatcher calls and 90 controller-to-fire calls. It observes
60/107 new PCs and no allocations; it is not gameplay coverage.

The natural 600-frame gameplay continuation matches **2,783,882/2,783,882
instruction/IRQ records**, **8,210,437/8,210,437 bus events** and
**2,775,872/2,775,872 consecutive boundaries**, zero different/skipped and
zero unported post-RTE returns. It observes 105/107 new PCs, 172 dispatcher
calls, 346 controller-to-fire calls and seven fire-to-allocation calls. The
unobserved `$9D78/$9D80` damage-handler transition is covered by the directed
matrix. Explicit external graph-exit occurrences fall from 6,693 to 5,403;
these are occurrences, not unique instructions or proof of whole-host closure.

The corrected 240-frame directed capture matches **1,586,141/1,586,141
instruction/IRQ records**, **4,656,016/4,656,016 bus events** and
**1,581,806/1,581,806 consecutive boundaries**, zero different/skipped and
zero unported post-RTE returns. It has 159 actual dispatcher calls, 420
controller-to-fire calls and 80 fire-to-allocation calls. All 107 PCs, both
directions of the 19 conditional branches and every required endpoint pass.
The capture retains 316 explicit fixture context cuts and separately reports
158 incomplete fixture cuts outside accepted records. It also retains 3,480
external graph exits; this is bounded composition, not autonomous host timing.
The natural files predate the additional `$D0DA` capture address and retain
43/274 ordinary exit occurrences there. The corrected directed file computes
that instruction and its exposed IRQ return; new captures include it in the
sound-producer graph by default. Raw observations in the accepted files have
not been edited or synthesized.

Directed initial-state fixtures call six checked routine entries through a
real IPL7 RAM trampoline, injected only at completed read/branch wait sites.
They use arcade slot 130 (`$C080`), restore object/pool/HUD/sound RAM after
execution, and keep fixture/context cuts visible. Acceptance requires all
107 PCs, both directions of all 19 conditional branches, signed ammo
`0/20/$8000/$FFFF`, free-slot counts `0/24/25/$7F`, launch counter
`0/$3C/$7FFF/$FFFF`, and append counts `0/$BF/$C0/$C1`. These fixtures are
initial operands, not supplied handler outputs or natural spawn evidence.

Three mutations in ignored copies reject reserve `24 -> 25`, launch-side
increment `4 -> 5`, and Vulcan sound command `$90 -> $91` through actual
instruction-state/write mismatches. Production constants remain unchanged.
The launch-side mutation produces 168 instruction-state differences and 67
bus-event differences; the sound-command mutation produces 719 and 31. The
reserve mutation changes D1 at `$9E88` and later rejects the branch/bus
frontier at the 25-free-slot limit, before a full summary is possible. These
are behavioral mismatches, not missing-coverage rejections.
Copied sources, executables and captures stay under ignored `build/oracle/`.

**Boundaries:** `$9D2C` pool reset, `$CFC4` HUD/decimal execution, projectile
initializers/update graphs, damage/attachment transitions beyond the installed
handler, other caller exits, main/sub delivery and renderer producers remain
supplied. The `$9ED2` HUD call/return stack and its allocator return are
computed, but the HUD callee's instruction stream is not part of this check.
Existing IRQ4 reload/HUD tests remain independent. Input bytes at unowned
lanes, initial CPU/chip contexts, ISR/sub frontiers and fixture cuts remain
labelled inputs. Natural held/released-fire cadence is bounded by the supplied
host scheduling; arbitrary IRQ overlap, all pool indices/flag contexts and
whole-credit gameplay are not established. Native Stage 1 remains active;
Phase 3d and plan section 2 item 1 remain incomplete.

```bash
make oracle-capture-caller-weapon-short oracle-capture-caller-weapon-directed oracle-capture-caller-weapon-full
make oracle-caller-weapon oracle-weapon-generated-check
```

Validation passes: `make test`, `make verify-stage1`, the existing
`make oracle-test` suite and the new `make oracle-caller-weapon` acceptance
prerequisite, weapon/flight/companion generated checks, all three mutation
checks, game build, 180-frame dummy SDL video/audio launch and
`git diff --check`. Existing race exclusions remain reported by their original
oracles; the new accepted comparisons have zero skips. CMake is unavailable,
so the added CMake registrations are untested.
No ROM bytes, disassembly listings or captures are tracked.

### Flight recovery, HUD and launch-sound caller continuation (2026-10-08)

This continuation replaces supplied execution at flight `$9626..$98BE`,
HUD/decimal `$CFC4..$D09E`, and missile initializer `$9F7C..$A02E` with
258 literal instruction PCs in `OMainIrqService::caller_step`. Its generated
manifest and code come from the local ROM disassembly; runtime ROM lookups
remain bus reads. The previous weapon checkpoint's HUD and missile-initializer
boundaries are superseded for these ranges.

The live `$145AC/$145B0` dispatcher now executes fuselage initialization,
recovery and missile initialization. Recovery's `$969A` BSR and launch's
`$A022` BSR enter the checked `$B84C` sound producer, retaining registers,
CCR, stack, RAM ownership and instruction time. Launch sound is selected by
`$A01A/$A01E` from the ROM lookup at `$A716`, rather than a native cue.
The actual allocator `$9ED2` calls the HUD, which gates stage/ammunition,
clears its tile pairs, converts ammunition through `$D062/$D090` and writes
whole/half symbols. This extends the caller path independently of the
existing IRQ4 HUD routine oracle.

The newly used CPU forms include predecrement MOVEA, absolute-long JSR,
DBF, register logical shifts/rotate, byte OR, register immediate BTST and
non-overflowing unsigned DIVU. DIVU's operand-dependent microcode duration
is computed from initial registers and checked against raw MAME bus times.
Divide-by-zero and quotient overflow remain explicit unsupported contexts;
legitimate decimal input is masked to 16 bits and uses nonzero divisors.
ANDI.L register completion adds four internal clocks after prefetch, with
an interrupt-visibility regression at a hardware deadline.

Two capture errors were corrected before accepting the final corpora.
The short `$9FCA` BEQ can read PC+4 on either direction; start labeling now
uses MAME's instruction-hook SR snapshot, and the untaken short branch
records one prefetch rather than two. Immediate register BTST requires
two prefetches and a two-clock completion tail, so `$D054` is captured as
one complete instruction. The two-millisecond fixture window also cut off
some HUD/launch paths; host fixtures now allow four milliseconds. Final
gameplay/directed acceptance uses fresh captures with these corrections; exploratory captures
and any temporary diagnostic relabeling remain ignored and are not accepted
reference evidence.

Directed initial operands call seven ROM entries through a real IPL7 RAM
trampoline at safe completed wait sites, using the actual fuselage slot 131
and MainBus addresses. They restore object/work/HUD/sound/request lanes
and retain explicit context cuts. The matrix includes decimal endpoints
`0/1/9/10/99/100/101/999/10000/65535`, nonzero divisors, signed depth,
recovery counters/timers, stage gates, ammo cap, target-ring presence,
launch parity, sound enable/index and append capacity. These are directed
initial operands; natural dispatcher/allocator/sound calls are counted
separately by `--require-host-path`.

**Boundaries:** missile update `$A234`, Vulcan initialization/update,
remaining object/sequence/damage/pool-reset graphs, sub/renderer producers
and independent whole-host scheduling remain outside this new graph.
Caller replay still consumes labelled initial contexts, ISR/sub frontiers,
unowned input lanes and directed cuts. Arbitrary IRQ overlap, every launch
channel/pool state, whole-credit RAM/video equality and audio waveform
identity are not established. Native Stage 1 remains active; Phase 3d and
plan section 2 item 1 remain incomplete.

```bash
make oracle-capture-caller-host-short oracle-capture-caller-host-directed oracle-capture-caller-host-full
make oracle-caller-host oracle-host-generated-check
```

Natural 120-frame boot/attract replay matches **465,158/465,158
instruction/IRQ records** and **1,364,541/1,364,541 bus events**, zero
different/skipped and zero unported post-RTE boundaries. It does not establish
natural gameplay coverage of the new HUD/recovery/launch graph.

The fresh natural 600-frame gameplay replay matches **2,797,594/2,797,594
instruction/IRQ records**, **8,242,928/8,242,928 bus events** and
**2,789,702/2,789,702 consecutive boundaries**, zero different/skipped and
zero unported post-RTE boundaries. Physical visibility matches
**22,842,419/22,842,419 observations**. The actual dispatcher invokes flight
initialization twice and missile initialization five times; the allocator
calls the HUD five times, and five launch BSRs enter the sound producer.
Natural sound commands cover `$40/$41/$42/$43`. It observes **131/258 PCs**;
recovery, alternate gates and decimal endpoints require the directed matrix.
There are no fixture cuts. External graph-exit occurrences fall to **5,280**,
which counts boundary occurrences and does not establish whole-host closure.

The fresh 360-frame directed replay matches **2,054,080/2,054,080
instruction/IRQ records**, **6,030,246/6,030,246 bus events** and
**2,047,505/2,047,505 consecutive boundaries**, zero different/skipped.
All **258 PCs**, both directions of **21 conditional branches and four DBFs**,
and all ten required decimal endpoints pass. The physical timeline matches
**11,379,962/11,379,962 observations**. Actual dispatcher/allocator calls
include one flight initialization, seven HUD calls and seven missile
initializations; fifty recovery/launch sound calls cover commands
`$40/$41/$42/$43/$F1`. These counts include natural execution between
labelled fixtures; fixture entries themselves do not count as dispatcher calls.
The run retains 462 explicit fixture context cuts, with 231 incomplete fixture
cuts outside accepted records, and 5,338 external graph-exit occurrences.
It exposes two supplied post-RTE continuations outside the selected graphs:
`$6844` and `$14EE8`, each once. The directed acceptance target therefore
requires closure of every new graph PC, all branches/endpoints and exact
comparison, while reporting those two unrelated exits; it does not use the
whole-corpus `--require-closed` gate. Natural and older closure gates retain
that stricter requirement.

Four deliberate mutations in ignored source copies fail actual state/write
comparison: flight depth `$C00 -> $C01` produces 176 instruction and 176
bus-event differences; HUD upper symbol `$8BFD -> $8BFC` produces 51/51;
recovery command `$F1 -> $F2` produces 252/6; and launch ROM lookup
`$A716 -> $A717` produces 816/64. The positive directed run exits successfully
without whole-corpus closure, and all four mutants fail without that flag:
these are behavioral rejections, independent of the two reported external
post-RTE continuations. Production code/constants remain unchanged.

Validation: `make test`, `make verify-stage1`, the existing `make oracle-test`
suite, host/weapon/flight/companion generated checks, game build and a
180-frame dummy SDL video/audio launch pass. The new `oracle-caller-host`
prerequisite checks the short, natural and directed corpora. CMake is not
installed here; its added registrations have not been executed. All ROM
images, captures, exploratory normalization scripts, mutation copies and
executables remain under ignored `build/oracle/`.

### Joint publication grants and the two directed post-RTE exits (2026-10-08)

The directed host caller now computes `$6844` and `$14EE8`, the two unrelated
returned instructions exposed by the previous 360-frame capture. Both are
`BTST #1,$29C200`; their five ordered extension/operand/prefetch phases and
Z-bit updates are literal generated instructions. They preserve every other
CCR bit. The generator, Lua manifest and host coverage table include both;
`oracle-caller-host` now requires `--require-closed` on the directed corpus too.
This closes the returned instructions, not either surrounding handler graph.

Fresh directed 360-frame acceptance matches **2,055,260/2,055,260
instruction/IRQ records**, **6,036,146/6,036,146 bus events**, and
**2,048,684/2,048,684 consecutive boundaries**. Physical visibility matches
11,385,700 observations. All 260 host PCs, the existing 21 conditionals/four
DBFs and decimal endpoints pass; there are zero differences/skips and zero
unported post-RTE returns. The original 462 explicit fixture context cuts and
231 incomplete fixture cuts remain labelled; 5,341 ordinary graph exits remain.
The fresh natural 120-frame corpus matches 465,203 instruction/IRQ records,
1,364,766 bus events and 464,370 consecutive boundaries, with zero
unported post-RTE returns and 387 ordinary graph exits. Both new returned
instructions have independent constant-mutation checks: disabling their bit
mask (`2 -> 0`) rejects 234 and 946 instruction-state comparisons respectively.
Both tested operand bytes are `$FF`. Changing bit 1 to bit 0 did not distinguish the observed inputs; that nearby
sensitivity is not claimed. Cleared-bit and arbitrary CCR histories remain
outside this capture's demonstrated coverage; the replay reports input bytes.

The fresh natural 600-frame corpus matches **2,798,542/2,798,542
instruction/IRQ records**, **8,247,668/8,247,668 bus events**, and
**2,790,648/2,790,648 consecutive boundaries**, zero differences/skips or
unported post-RTE returns. Physical visibility matches 22,847,001 observations;
133/260 host PCs execute naturally. Its 5,282 ordinary graph-exit occurrences
are boundaries, not unique remaining instructions. No fixture cuts occur.

`XBoardCpuContinuation` lets the existing literal ports retain their C++ local
state across a CPU grant. Two cooperative worker threads park at the ports'
clock checkpoints; only the CPU granted by `XBoardBusArbiter` can resume.
A sub operand is therefore read from the actual shared MainBus/SubBus RAM
when its phase executes, and an RMW operand remains latched across intervening
main grants. No instruction is replayed against expected read bytes, and no
transaction list is reordered by CPU-local timestamp. Native Stage 1 is not
wired to this adapter pending full acceptance.

MAME `m68000.cpp::execute_run` first pays
`m_count_before_instruction_step`, then executes partial/full microcode and
carries negative remaining clocks into that private debt. It reports only the
granted clocks. Internal clocks before a bus are not a separate yield: the
following bus may execute past the grant target. The adapter retains that
execution frontier while its public CPU clock consumes whole granted clocks.
The grant deadline can lie between CPU clocks, particularly after the Z80
contracts it; the CPU target is computed from the arbiter's cycle budget,
not rounded directly from the wall-clock deadline. ROM-free checks cover
private bus/tail debt, indivisible pre-bus clocks, RMW retention, worker reuse,
unpaid-tail rejection, cancellation and propagation of a port error.

`capture_host_publication_schedule.lua` records the same read-only scheduler
probe as the independent arbiter oracle, adding executing PC and selected
first-bus CPU contexts. Those selected instructions have no register/CCR
changes before their first bus except the BSR stack decrement, which is
explicitly reversed by four bytes. Stack addresses are masked to each CPU's
physical map for phase identification. Sub supervisor accesses that retain a
publication CURPC are IRQ frontiers, not publication instructions.
An exploratory debugger snapshot approach was discarded: debugger `printf`
uses the visible CPU's symbols when another CPU's breakpoint action fires,
and `focus` changes breakpoint observation. The accepted capture reads the
executing device's state directly, without breakpoints or focus commands;
raw observations have not been normalized or edited.

The fresh natural 120-frame joint replay checks both main `$14856..$14878`
and sub publication caller/wait/release continuations under the computed
arbiter. Its exact results are:

| Joint check | Identical / captured | Skipped |
|---|---:|---:|
| Grant groups | 239,177 / 239,177 | 0 |
| Shared reads | 1,271,730 / 1,271,730 | 0 |
| Native instruction executions | 1,788,368 / 1,788,368 | 0 |
| Native ordered phases | 4,818,916 / 4,818,916 | 0 |
| Retained instruction contexts | 1,787,933 / 1,787,933 | 0 |
| Wholly bounded CPU grant completions | 182,481 / 182,481 | 0 |

All 8,319,935 main/sub phases are delivered in computed CPU order; 620,495
CPU runs and 496 board quantum requests match. There are 182,879 grants
resuming a native instruction, including grants paying debt without a new bus.
327 shared writes now come from the actual native ports; the remaining
141,969 shared writes are explicitly supplied producers. 435 instruction
entry contexts remain inputs. Only an observed unported CPU phase cuts a
retained instruction chain; predicted PC/time disagreement cannot create a
new entry context. Initial work/shared RAM is supplied once;
read assertions never seed operands. Wholly bounded grants return computed
completion times into the arbiter; grants crossing unported CPU work retain
supplied completion frontiers. Both CPU streams share one RAM instance.
The 37 reads after a main write with a later local timestamp still match.

**Coverage and limits:** 22/24 selected PCs execute jointly. Main missed-count
`$1486A` and sub optional normal-sprite BSR `$43E` do not execute in this
natural corpus; their prior independent directed instruction checks do not
establish joint scheduling of those paths. Arbitrary instruction tails,
IRQ entry/service/RTE/physical selection, sprite/road callees, ADC/button
producer cadence, sound CPU, reset/suspend, device timer deadlines and boost
request times remain external boundaries here. The current main port poll
is deliberately a supplied no-selection boundary in this joint test;
independent IRQ oracles remain separate. Unknown caller progress and video
ownership are not removed. This is joint bounded publication scheduling,
not whole-host gameplay, independent paired RGB/sound acceptance or Phase 3d
completion. Native Stage 1 remains active.

Three further mutations reject actual differences: exposing private debt as
public overrun rejects native grant completion (160 ns at the first reported
difference); main re-arm `1 -> 2` rejects its actual shared write; sub release
bit `1 -> 2` rejects its actual byte-lane write. Together with the two returned
bit tests, five new mutations fail. Mutation sources, binaries, transcripts,
ROM images and exploratory captures stay ignored under `build/oracle/`.

```bash
make oracle-capture-caller-host-short oracle-capture-caller-host-directed oracle-capture-caller-host-full
make oracle-caller-host oracle-host-generated-check
make oracle-capture-publication-schedule oracle-publication-schedule
python3 tools/oracle/mutation_check.py hostreturn6844 hostreturn14ee8 pubscheduledebt pubschedulerearm pubschedulerelease
```

Validation for this continuation: `make test`, `make verify-stage1`, the full
`make -j4 oracle-test` suite (including the joint publication prerequisite),
host/weapon/flight/companion generated checks, game build, 180-frame dummy
SDL video/audio launch and `git diff --check` pass. The strengthened explicit
continuity check also passes the joint corpus with the same counts. Existing
oracles retain their documented read-set race exclusions and have zero
differences; all new accepted comparisons have zero skips. CMake registrations
are declared but unexecuted because CMake is not installed. No ROM images,
tables or captures are tracked.


### Joint sprite/road callee progress (2026-10-08)

`oracle_publication_schedule` now runs `OSubProgress` for the existing bounded
sprite/road graphs inside the same arbiter and shared MainBus/SubBus memory
used by the publication callers. These are the already translated literal
instructions, not new implementations or transaction replay. Their operands
come from shared RAM and math/road endpoints at the granted phase. Supplied
sub writes now pass through the bus with their actual byte masks, and supplied
road-control reads retain their swap side effect; expected read bytes never
seed operands. Native Stage 1 remains unchanged.

The joint harness previously started a successor as soon as the worker had
finished. Sprite/road register-only arithmetic and shifts can finish with
private internal-tail clocks still unpaid by the current grant. The harness
now keeps that instruction active until the continuation's public clock has
paid the complete computed instruction duration. Its C++ locals, bus overrun
and RMW operands remain retained across grants. The existing ROM-free
continuation regression independently checks completed-but-unpaid tail debt.

Capture format `AB2ARB05` adds an explicit instruction start time. The sub
callee snapshots use read-only debugger breakpoints on the manifest PCs and
record registers before instruction effects; the first bus and the manifest's
literal internal-clock/branch offset determine start time. Only the sub CPU
uses debugger symbols; main publication snapshots continue reading the
executing main device directly. Each sub snapshot is emitted once per observed
instruction cycle counter. Linked snapshots are assertions of the retained
computed state, and cannot reset a chain on a predicted mismatch. IRQ entry
still makes an explicit supplied cut, including supervisor bus phases that
retain a user instruction CURPC. Regenerate the old-format joint capture with
`make oracle-capture-publication-schedule` before running the new replay.

The fresh natural 120-frame corpus has the same initial RAM and **all
201,927 captured grant records and 8,319,935 raw phases** as the previous
capture, after excluding the added snapshot fields. The complete scheduler
trace is byte-for-byte unchanged. Thus the debugger snapshots do not change
the observed bus ordering or grant schedule.

| Joint check | Identical / captured | Skipped |
|---|---:|---:|
| Grant groups | 239,177 / 239,177 | 0 |
| Shared reads | 1,271,730 / 1,271,730 | 0 |
| Native instruction executions | 2,420,931 / 2,420,931 | 0 |
| Native ordered phases | 6,110,061 / 6,110,061 | 0 |
| Retained instruction contexts | 2,420,584 / 2,420,584 | 0 |
| Wholly bounded CPU grant completions | 242,205 / 242,205 | 0 |

The replay now computes **102,479 shared writes**, up from 327. The remaining
**39,817 writes** are explicitly supplied producers; **347 entry contexts**
remain inputs, down from 435. There are 242,517 grants resuming an active
instruction, including grants paying private debt without another bus. All
620,495 CPU grants and 496 quantum requests match; the 37 shared reads after
a main write with a later CPU-local timestamp still match. No native
continuation remains parked at this accepted capture stop.

**Coverage and limits:** natural joint execution covers **631/1,136**
sprite/road manifest PCs. The replay requires the road wrapper, initial road
setup, sprite entry, bank/rotation graph and raster-writer representative PCs;
it reports every unexercised PC and taken/untaken branch counts. The other
505 PCs and any zero-count branch directions are not verified jointly. Of
93 exercised branch PCs, 15 are unconditional; 39 of the 78 conditional/DBF
PCs have only one direction in this natural corpus.
Independent directed sprite/road checks still cover all 1,136 PCs and their
previously documented limits; they do not establish joint scheduling of
those paths. Main missed-count `$1486A` and optional sub BSR `$43E` remain
unexercised jointly. End-of-graph register states without a bounded successor
remain outside the retained-context assertions. IRQ entry/service/RTE and
physical selection, main caller/producer progress, ADC/button cadence, sound
CPU, reset/suspend, timer deadlines, boost request times and renderer ownership
remain supplied. This extends bounded joint scheduling; it does not establish
independent paired RGB/sound acceptance or complete Phase 3d.

Five mutations reject actual computed differences: exposed private bus debt
rejects grant completion by 160 ns; main re-arm `1 -> 2` and sub release
`$200 -> $400` reject shared writes; sprite `$66A` address increment `2 -> 4`
rejects retained A2 at `$66C`; road `$F0A` subtract operand `6 -> 5` rejects
retained D0 at `$F0C`. Temporary variants and logs stay under ignored
`build/oracle/`; production port constants remain unchanged. An exploratory
sprite comparison threshold `$1E -> $1D` did not distinguish this natural
corpus and is not sensitivity evidence; the accepted sprite mutation changes
the actual address increment instead.

```bash
make oracle-capture-publication-schedule oracle-publication-schedule
python3 tools/oracle/mutation_check.py pubscheduledebt pubschedulerearm pubschedulerelease pubschedulesprite pubscheduleroad
```

Next join the sub IRQ entry/service/RTE and retained resumptions, and extend
main caller/producer execution under these same grants. Keep device and
renderer/sound frontiers explicit until their producers are verified.


Validation: `make test`, `make verify-stage1`, the full `make -j4 oracle-test`
suite, the final strengthened `make oracle-publication-schedule`, all five
listed mutation checks, game build, a 180-frame dummy SDL video/audio launch
and `git diff --check` pass. The joint comparisons have zero skips. Existing
standalone oracles retain their documented read-set race exclusions and have
zero differences. CMake is unavailable here; its updated source registration
has not been executed. ROMs, captures, mutation variants and their logs remain
ignored; no ROM data or captures are tracked.

## Joint sub IRQ entry/service/return and main dispatcher progress

The next natural joint checkpoint extends the publication/sprite/road replay
through sub IRQ entry, all thirteen service PCs and RTE, and through the twelve
main dispatcher PCs `$1458A..$145B8`. The interrupted sub state stays retained
through the exception and is checked after RTE, including PC, SR, D/A registers,
USP and SSP. Accepted IRQ entries are computed from that state, never reseeded
from their observed snapshots.

Capture format `AB2ARB07` records program and CPU-space phases separately and
includes both stack banks and the E-clock bias. The isolated MAME probe now
observes each device's registers before instruction effects using a read-only
saved snapshot array in its copied `m68000.cpp`. It needs no debugger hooks.
The installed MAME sources, objects and binary are unchanged. Regenerate older
captures with `make oracle-capture-publication-schedule`.

Sub IRQ selection samples the physical VBLANK line at the computed arbiter
global checkpoint, after delivering timers due at that checkpoint. Entry uses
the retained absolute CPU clock and E-clock bias. The VPA wait parks at the
computed ready time before observing the real CPU-space autovector read at
`$FFFFF8` (value `$001C`), then pays its five bus clocks and the remaining
internal clocks. It cannot expose the acknowledgement before its grant. The
ordinary continuation still retains indivisible bus overrun and private tail
debt until the successor can begin.

The initial capture boundary lies inside an ISR; that partial service remains
an explicit supplied frontier. Every subsequent accepted IRQ entry, service
and return is native. Capture stops between whole grant groups after the final
RTE and a resumed user instruction have completed. One final user continuation
is parked beyond the accepted final grant; no ISR remains active at the stop.

| Joint check | Identical / captured | Skipped |
|---|---:|---:|
| Grant groups | 239,177 / 239,177 | 0 |
| CPU grants | 620,495 / 620,495 | 0 |
| Shared reads | 1,271,764 / 1,271,764 | 0 |
| Native instruction/IRQ records | 2,491,840 / 2,491,840 | 0 |
| Native ordered phases | 6,309,809 / 6,309,809 | 0 |
| Retained contexts | 2,487,209 / 2,487,209 | 0 |
| Wholly bounded grant completions | 268,316 / 268,316 | 0 |
| Sub IRQ entries / VPA reads / RTE restorations | 88 / 88 each | 0 |

There are 8,320,117 total observed phases, including 88 CPU-space reads;
158,185 native writes, including **105,394 computed shared writes**; and
**36,902 supplied shared writes**. All 496 quantum requests agree. There are
269,405 grants resuming an active instruction and 1,761,745 computed sub IRQ
selection checkpoints. The 37 shared reads following a later-local-time main
write still agree. The complete scheduler trace remains byte-for-byte identical
to the preceding checkpoint; initial RAM and its 8,319,935 program phases are
unchanged prefixes. The new stop adds 94 program phases.

Supplied entry contexts are now **4,631: main 4,630, sub 1**. The increased main
count exposes returns from unported dispatcher handlers (4,401 active handler
calls) as explicit frontiers. The sub count falls to one initial context;
sub IRQ entry no longer cuts the retained chain. Main IRQ entry/service/RTE
and unported main handlers remain supplied.

**Coverage and limits:** all thirteen sub ISR PCs execute. `$574`, `$57C` and
`$58A` cover both directions (78/10, 9/1 and 5,016/88 respectively). The mutex
busy branch `$56E` covers only the non-busy direction (0/88). Boot `$420/$428`,
arbitrary interrupt masks/countdown endpoints, nested exceptions and immediate
post-RTE interrupt selection are not established by this natural joint corpus.
All twelve main dispatcher PCs execute; `$145A8` covers inactive/active paths
(6,775/4,401), `$145B6` covers loop/exit (11,132/44), and `$1485C` covers both
directions (332,398/44). Main missed-count `$1486A` and optional sub BSR `$43E`
remain absent. Sprite/road coverage stays 631/1,136 PCs: the other 505 PCs and
previously absent branch directions retain the preceding checkpoint's limits.
Directed standalone coverage does not establish joint execution of those paths.

All eleven mutation checks reject computed differences. The preceding five
debt/re-arm/release/sprite/road checks remain effective. Six additions change
the saved A-register value, busy-delay D0 constant, restored user SP, dispatcher
A5 stride, VPA internal clock count and VPA wait parking. They fail respectively
on a stack phase, retained D0, RTE SP, retained A5, a stack phase shifted by
80 ns and an acknowledgement outside its grant. Variants and logs remain
under ignored `build/oracle/`; production constants are unchanged.

```bash
make oracle-capture-publication-schedule oracle-publication-schedule
python3 tools/oracle/mutation_check.py pubscheduledebt pubschedulerearm pubschedulerelease pubschedulesprite pubscheduleroad pubscheduleirqsave pubscheduleirqdelay pubscheduleirqrestore pubscheduledispatch pubscheduleirqvpa pubscheduleirqwait
```

This is bounded joint scheduling acceptance. Main IRQ/caller producers,
ADC/button cadence, sound CPU, reset/suspend, timer deadlines, boost request
times and renderer ownership remain external frontiers. The checkpoint does
not wire the native graph into Stage 1 or establish independent paired
RGB/sound or whole-host acceptance. Next join main IRQ entry/service/RTE and
its caller/producer progress under these same grants.

Validation: `make test`, `make verify-stage1`, the full `make -j4 oracle-test`
suite, the strengthened `make oracle-publication-schedule`, all eleven listed
mutations, game build, a 180-frame dummy SDL video/audio launch and
`git diff --check` pass. Joint comparisons have zero differences and zero
skips; standalone suites retain their documented read-set race exclusions.
CMake is unavailable here, so its updated source registration was not executed.
ROMs, captures, probe copies and mutation artifacts remain ignored.

## Expanded joint main callers and IRQ RAM wrappers

The joint replay now admits the existing literal main caller graphs, rather
than only the dispatcher and publication wait. `generate_joint_main_manifest.py`
derives capture/replay membership from checked-in case labels and rejects a
stale manifest; it does not decode runtime opcodes or introduce behavior.
`generate_input_schedule.py --graph irqwrapper` additionally translates 103
instructions from `$1461C..$147C6` and `$B7F8..$B840`, excluding the named
device/exception frontiers below. MOVEM retains the reversed low/high stack
write order, restore dummy read and prefetches. The predecrement pointer swap
stores low first; the memory-to-memory pointer swap advances its source before
resolving its aliased destination. Flags and DBF endpoints remain literal.

Format `AB2ARB08` seeds all MainBus RAM and the main/sub multiply/divide
registers once at the initial boundary. Subsequent unported main math writes
update those same devices; native instructions compare their actual reads.
The `.inputs` sidecar records physical button-port frames independently of
CPU reads. These observations are allowed physical inputs, not computed ADC
cadence or ownership. The preceding `AB2ARB07` checkpoint remains supported.
Scheduler-prefix, initial work/shared RAM and **all 8,320,117 prior raw phases**
are unchanged in the new observation stream. Instrumentation adds no bus
transactions or changes to grant order.

The accepted fresh-NVRAM **120-frame corpus is attract mode**: `gameplay.lua`
delivers coin at frame 360 and start at 420. It exercises reused gameplay
routines, but its success is not live player/weapon whole-host acceptance.

| Joint check | Identical / captured | Skipped |
|---|---:|---:|
| Grant groups | 239,177 / 239,177 | 0 |
| CPU grants | 620,495 / 620,495 | 0 |
| Shared reads | 1,271,764 / 1,271,764 | 0 |
| Native instruction/IRQ records | 3,045,337 / 3,045,337 | 0 |
| Native ordered phases | 7,960,655 / 7,960,655 | 0 |
| Retained contexts | 3,042,113 / 3,042,113 | 0 |
| Wholly bounded grant completions | 356,913 / 356,913 | 0 |

The replay computes **141,627 shared writes**, leaving **669 supplied**, and
291,594 native writes overall. There are 359,353 grants resuming an active
instruction. All 496 quantum requests agree and the 37 later-local-time main
write/shared-read cases still agree. Sub IRQ closure remains 88 retained
entries, 88 real VPA reads and 88 exact RTE restorations.

Coverage is **679/1,429 main PCs**, including **92/103** added IRQ RAM-wrapper
PCs. The harness requires the save/restore, ADC-result RAM store, both IRQ4
logic/display branches and both busy-delay DBF outcomes. It reports all
unexercised main PCs and wrapper branch directions. The other 750 main PCs,
eleven wrapper PCs, zero-count directions and earlier 505 missing joint sub
PCs remain outside joint coverage. The initial partial sub ISR is supplied.
Main entry contexts are 3,223, sub contexts one. Compared with the initial
caller expansion's 786 main contexts, the extra wrapper execution exposes
more individual unported callee returns as boundaries; it does not silently
reseed on predicted mismatches. A census now reports every supplied PC and
its phase/write counts: 359,416 main phases at 1,236 PCs, including 78,188
writes; 46 sub phases at three initial ISR PCs, with no sub writes.

**Device and exception frontiers:** main IRQ entry and RTE, timer
acknowledgements `$1461C/$1466C`, ADC read/mux/strobe
`$14626/$14654/$1465A`, output callbacks `$1471A/$14766`, sprite draws
`$14744/$14790`, and synchronized sound-latch write `$B81E` remain external.
They cannot be accepted just because their data values match. Attempting to
join the timer acknowledge or mux write without its hardware owner exposed
an early grant end: the CPU reports fewer clocks while retaining private
instruction debt. In the first acknowledge experiment, grant 33,292 should
end at 538,949,840 ns rather than 538,950,080 ns. These failing experiments
are not acceptance evidence. The final wrapper membership excludes these
instructions until synchronized callbacks and aborts are independently modeled.
The remaining IRQ callees, main producers, deadline/boost times, sound CPU
execution and sprite/road/compositor feedback also remain external.

**Longer-run rejection:** the expanded 600-frame observation captures
1,309,114 nonempty grant groups and 53,446,773 main/sub phases, but replay
fails at grant 951,424. The retained sub `$67A` ADDA is interrupted by main
`$14840` RESET. MAME reads reset vector address zero at 7,094,206,800 ns while
the continuation incorrectly proceeds to its `$680` prefetch. MAME's
`segaxbd_state::m68k_reset_callback` pulses sub RESET; `reset_df` calls that
callback for both RESET assertion and release. The sub subsequently executes
the real reset-vector/boot sequence. This boundary needs a computed main
RESET producer, cancellation of the interrupted continuation and native sub
reset/boot ownership. Do not skip it, synthesize a resumption or present the
600-frame experiment as passing. This supplies concrete evidence for the
still-open device/reset blocker, rather than extending an attract-mode claim
to live gameplay.

Four new mutations reject actual differences: publication A0 stride `4 -> 6`
fails retained A0; inverted `$98C6` flight branch fails its program phase;
IRQ4 saved A6 low word `+1` fails a stack phase; `$14730` DBF decrement
`1 -> 2` fails retained D0. The preceding eleven joint mutations remain
separate evidence for the earlier checkpoint. No production constants were
changed by these temporary variants.

```bash
python3 tools/build_rom_image.py maincpu build/oracle/main_joint_image.bin
python3 tools/generate_input_schedule.py build/oracle/main_joint_image.bin --graph irqwrapper --check
python3 tools/generate_joint_main_manifest.py --check
make oracle-capture-joint-main-progress oracle-joint-main-progress
python3 tools/oracle/mutation_check.py jointmainstride jointmainflight jointmainirqsave jointmainirqdelay
# This longer joint experiment currently FAILS at RESET; preserve the failure.
make oracle-capture-joint-main-progress-full oracle-joint-main-progress-full
```

All four whole-host requirements remain open: full joint main IRQ/resumption;
remaining main producers and joint sub coverage; physical ADC/button cadence,
synchronized device yields/deadlines/reset/sound; and video/compositor
ownership. The next corrective work is native hardware synchronization and
RESET, then main entry/RTE under those same grants. This checkpoint does not
wire the ports into native Stage 1 or complete Phase 3d.

Validation: the full `make -j2 test verify-stage1 oracle-test` run passes,
including the new short joint target. The final stricter replay and freshly
regenerated canonical corpus pass with the same counts above; its complete
scheduler trace is byte-identical and every prior raw phase agrees. All four
new mutations, all six caller/wrapper generation checks, joint membership
check, game build, 180-frame dummy SDL video/audio launch and
`git diff --check` pass. Standalone tests retain documented race exclusions.
The separate 600-frame joint experiment fails at RESET as described above;
it is not part of the passing short acceptance claim. CMake is unavailable,
so its source/test registration has not been executed. ROMs, images, captures,
probe copies and mutation artifacts remain ignored; no commit was made.

## Synchronized timer acknowledges and natural RESET continuation

The retained CPU adapter now distinguishes two MAME operations. A device
synchronization steals the remaining public grant at its callback clock,
while retaining the completed microcode step's private bus/internal debt.
Hardware RESET cancels the parked instruction and clears that debt. These
follow `device_execute_interface::abort_timeslice` and
`m68000_device::device_reset`; regression checks cover a retained RMW,
interruptible VPA wait, an abort inside RESET's internal loop, and reuse after
cancellation. The initial checkpoint of an uninterrupted internal step must
still execute its bus before returning the aborted grant.

Main `$1461C/$1466C` timer acknowledgements now execute jointly. The native
315-5250 callback models the synchronized input-line request even when its
IRQ line was already clear. A fresh 120-frame attract capture passes
**239,177/239,177 grant groups, 7,962,095/7,962,095 native phases and
358,652/358,652 bounded grant completions**, zero skips. The 360 added
acknowledge instructions and their 1,440 phases close the previously observed
240 ns early-return discrepancy. Coverage is 94/105 IRQ-wrapper PCs and
681/1,432 main PCs (the newly admitted RESET PC is absent in this short
corpus). Disabling the early grant abort causes an out-of-grant phase at
`$1461C`; the mutation is rejected.

Main `$14840` RESET now ports its four initial clocks, assertion callback,
31 four-clock internal loop iterations, release callback and program
prefetch, with supervisor execution explicitly required (the natural main entry
has SR=$2100). RESET retains SR and selects the pending IRQ at rset5;
main physical IPL ownership remains an explicit separate blocker. Both callbacks produce their own quantum requests; their times
are assertions against MAME, rather than supplied producer times. The first
callback schedules one input-line queue. The second appends ASSERT/CLEAR to
that same queue before delivery. MAME's `interface_post_reset` resets the
queue during the first CLEAR, discarding its remaining entries: two callback
requests cause one actual sub reset execution. `interface_pre_reset` also
clears `total_cycles`, so native cancellation rephases the sub VPA E clock
at the reset frontier.

The sub reset state preserves D0..D7/A0..A6/USP, clears T, sets S and mask 7,
then reads SSP/PC vectors and both target prefetches, with two clocks between
them. `capture_subreset.lua` uses a read-only probe before reset microcode
starts; it does not force RESET or set CPU state. Its natural 600-frame
fixture passes **7/7 reset states and 42/42 ordered vector/prefetch phases**,
zero skips. Changing the initial eight clocks to six is rejected.

Joint sub ports expose their register state only when the corresponding
microcode checkpoint actually executes. This matters at `$67A`: after its
first prefetch the private bus debt can keep the next ADDA step from executing
when RESET arrives. Exporting the next step's updated A2 before that debt is
paid incorrectly preserves an extra `$10`; the joint mutation rejects it.
The RESET observer and normal instruction observer remain separate, so no
replacement entry snapshot can hide cancellation or retained-state errors.

The focused frame-400..440 corpus crosses coin/start and the formerly failing
RESET. It passes **991,666/991,666 scheduler groups, 3,762,891/3,762,891 raw
bus phases, 1,160,234/1,160,234 native instruction/IRQ records and
2,961,766/2,961,766 native phases**, zero skips. There are **146,533/146,533
bounded grant completions**, two computed main callbacks, one native sub
cancellation/reset sequence, and 40 retained sub IRQ services/returns. All
3,762,891 raw phases also agree with the earlier full observation stream;
adding reset probes changes no CPU transactions. Startup `$402..$41C` and
TRAP #10 `$62E..$63E` remain supplied execution, producing two additional
sub contexts. This window omits an earlier raster-entry PC, so
`--reset-window` reports all missing PCs without imposing the attract
corpus's raster-path census; `--require-reset` retains the full census.
The capture additionally checks the old interrupt mask to distinguish boot
PEA/TRAP stack writes from actual VBLANK entries after RESET.

The joint reader now streams one captured grant group at a time, retaining
its exact chronological phases and last-group completion rule. Full captures
contain over fifty million phases; future expected groups no longer need to
occupy gigabytes of RAM while the current group executes.

Commands:

```
make oracle-capture-subreset oracle-subreset
make oracle-capture-joint-reset-window oracle-joint-reset-window
make oracle-capture-joint-main-progress-full oracle-joint-main-progress-full
python3 tools/oracle/mutation_check.py jointmainirqabort subresetwait jointresetcallback jointresetregister jointresetclock
```

These closures do not establish whole-host acceptance. Main IRQ entry/RTE,
unported producers and reset startup, ADC/mux/deadline ownership, sound CPU
execution and video bank/compositor ownership still need joint closure.

The fresh **600-frame live-gameplay** replay now passes its full census and
`--require-reset`. This replaces the rejected long checkpoint; the original
pre-reset observation and failure log remain ignored local evidence.

| Full joint check | Identical / captured | Skipped |
| --- | ---: | ---: |
| Scheduler grant groups | 1,371,502 / 1,371,502 | 0 |
| Ordered main/sub bus phases | 53,446,773 / 53,446,773 | 0 |
| Shared reads | 7,665,892 / 7,665,892 | 0 |
| Native instruction/IRQ records | 19,049,961 / 19,049,961 | 0 |
| Native ordered phases | 49,054,764 / 49,054,764 | 0 |
| Retained instruction contexts | 19,026,321 / 19,026,321 | 0 |
| Wholly bounded grant completions | 2,257,946 / 2,257,946 | 0 |

All 568 sub IRQ entries have retained caller state, real VPA acknowledges and
exact RTE restorations. There is no active ISR at capture stop. Native main
RESET produces two callbacks, one synchronized cancellation and one reset
vector/prefetch sequence. The replay computes **1,190,140 shared writes**,
with **4,437 still supplied**, and 2,264,396 native writes overall.

Coverage grows to **1,118/1,432 main PCs**, **100/105 IRQ-wrapper PCs** and
**658/1,136 sub sprite/road PCs**. The remaining 314 main manifest PCs, five
wrapper PCs and 478 sub manifest PCs are not verified jointly by this
corpus; branch-direction counts remain printed explicitly. Supplied
execution remains substantial: **4,391,908 main phases at 4,677 PCs,
881,539 writes**, and **101 sub phases at 20 PCs, 12 writes** (initial partial
ISR and reset startup/TRAP). Main entry contexts are 23,637; sub contexts
three. Those values prevent a whole-host acceptance claim even though the
long natural RESET failure is now closed.

Validation: `make test`, `make verify-stage1`, `make oracle-test`, the full
joint replay, all five new hardware/RESET mutations, generated-manifest
checks, game build and 180-frame dummy-SDL run pass. The CMake registrations
are maintained but CMake is unavailable on this host. The final rset5 selection checkpoint is checked by rerunning the focused
RESET window; physical main IPL is still supplied by the existing main
caller boundary and is not claimed as native interrupt selection.


### Joint main interrupt entry, RTE and caller resumption

`OMainIrqService::enter_joint` now emits the complete exception transcript:
PC-low write, a real CPU-space autovector acknowledge at the computed VPA
frontier, saved SR/PC-high writes, vector reads and both handler prefetches.
The ten-cycle E-clock origin is supplied once at the initial main context,
retained thereafter and asserted against every later independent snapshot.
The existing data-only service oracle retains its original entry interface.
The acknowledge hook parks the native continuation until the E-clock edge
and charges the actual five-clock CPU-space phase, preserving grant debt.

`return_joint` ports `$1466A/$14672/$147C6/$147E6` literally: three frame
reads, SR restoration with MAME's `$A71F` mask, active SSP/USP selection, return prefetch, the IPL
selection checkpoint, and the second return prefetch. The joint harness
retains both main stack banks, checks them at every linked instruction,
and follows the computed return PC into the caller or next exception.
These four return addresses are part of the shared generated membership
(1,436 main instruction PCs), rather than capture-only exceptions.

The isolated MAME probe saves a read-only exception snapshot before
`state_interrupt` effects: interrupted registers/SR, both stack banks,
return PC, selected level and interrupted bus PC. It does not change CPU
state, clock counts or input lines. `AB2ARB09` adds explicit main IRQ entry
kinds and captures main CPU-space reads. The strict Make/CMake joint tests
require `--require-main-irq`, rejecting an older capture that cannot assert
these phases; legacy v7/v8 reading remains available for comparison.

The new natural 120-frame attract corpus passes **239,177/239,177 groups,
8,320,565/8,320,565 raw phases, 3,046,593/3,046,593 native records,
7,967,919/7,967,919 native ordered phases and 367,179/367,179 bounded grants**,
zero differences/skips. Main levels 2/4/6 occur **357/88/3** times. All
**448 entries, 448 VPA acknowledges and 448 RTEs** match; **432 entries and
432 post-RTE resumptions** carry retained caller state, and all 448 RTEs
start with retained service state. There are 2,382 supplied entry contexts
(2,381 main and one initial sub context). Computed shared writes remain
141,627 with 669 supplied; supplied main execution drops to 352,600 phases
at 1,168 PCs. Reserved SR bits, trace/address-error exceptions and the startup
`$147E6` return are absent in this short corpus. The raw scheduler groups and program-bus transcript are
unchanged from the earlier corpus; the extra 448 raw phases are real main
CPU-space reads. `$147E6` is absent in this attract window.

Four deliberate mutations fail different assertions: shifting VPA by one
clock, delaying the second handler prefetch by two clocks, restoring the
wrong RTE stack, and reading return PC+4 instead of PC+2. Their diagnostics
identify the mismatched phase or the retained caller state; no mutation is
accepted merely because execution leaves the bounded graph.

This closes exception microcode and selected return phases, **not physical
main IRQ selection or the full main service graph**. Main selection still
uses a supplied level; ordinary caller and RTE IPL polls remain zero until
the compare timer, VBLANK delivery and input-line synchronization have a
native owner. Unported service callees still cut continuity. ADC/mux and
sound deadlines, remaining producers/sub paths, reset startup, sprite/road
feedback and compositor ownership remain open. The Stage 1 host is unchanged.


The fresh **600-frame live-gameplay** continuation also passes, including
coin/start and natural main RESET. The short and full captures replace the
canonical `joint_main_progress` files only after passing; the earlier
accepted v8 baselines remain ignored as `*_before_main_irq`.

| Full joint assertion | Result |
| --- | --- |
| Scheduler groups | 1,371,502 / 1,371,502 identical; zero skips |
| Raw main/sub phases | 53,449,666 / 53,449,666 delivered in computed order |
| Native instruction/IRQ records | 19,055,747 / 19,055,747 identical; zero skips |
| Native ordered phases | 49,092,373 / 49,092,373 identical; zero skips |
| Bounded grant completions | 2,312,354 / 2,312,354 identical; zero skips |
| Shared reads | 7,665,892 / 7,665,892 identical; zero skips |
| Main IRQ2/4/6 entries | 2,307 / 568 / 18 |
| Main entry / VPA acknowledge / RTE | 2,893 / 2,893 / 2,893 matching records |
| Main retained entry / retained RTE / retained resumption | 2,614 / 2,893 / 2,616 records |
| Sub retained entry / VPA / exact RTE restoration | 568 / 568 / 568 |
| Main RESET callback / synchronized sub cancellation / vectors | 2 / 1 / 1 |
| Main instruction / IRQ RAM-wrapper coverage | 1,121 / 1,436; 100 / 105 |
| Sub sprite/road instruction coverage | 658 / 1,136 |
| Native / supplied shared writes | 1,190,140 / 4,437 |
| Supplied main execution | 4,357,192 phases at 4,507 PCs; 872,860 writes |
| Supplied sub execution | 101 phases at 20 PCs; 12 writes |
| Retained / supplied contexts | 19,037,168 / 18,579 |

The full raw stream adds exactly 2,893 main CPU-space acknowledges to the
previous accepted program-bus stream. All scheduler group and CPU-grant
counts remain unchanged. The entry/RTE addition moves 5,786 records and
37,609 phases into native execution; shared producer counts are unchanged.
`$147E6` is absent in both natural corpora, as are reserved-SR-bit and
trace/address-error returns. The 315 unexercised main manifest PCs and
478 sub sprite/road PCs remain unverified in this joint corpus; existing
independent directed checks do not convert them into joint acceptance.
Main services with unported callees retain explicit cuts. Individual
entry/RTE counts do not claim that every entire service or following caller
is native; the retained-entry/resumption and supplied-execution census
show the actual limits.

Validation: `make -j2 test verify-stage1 oracle-test`, strict short replay,
strict full replay with `--require-reset --require-main-irq`, generated
membership/opcode checks, four main entry/RTE mutations, game build and
180-frame headless run pass. Existing oracle fixtures retain their documented
race exclusions; the new joint checks have zero skips. CMake registrations
are updated; CMake is unavailable here. ROM data and captures remain ignored.


## Physical main selection and remaining producer/callee continuation

The joint main stream now contains 7,786 address-labelled PCs. The added
65 producer instructions cover `$B7C2/$B7D2` display-list copying,
`$1487A..$148FA` release/initializer/time counters, `$1495C` button history
and `$18902` byte-to-word tile copying. Another 2,287 instructions reproduce
the existing IRQ callee graphs with full instruction/program phases; 3,988
more main instructions come from the prior 600-frame supplied-execution
census. `$138BA` TRAP #12 and `$14842` RTE now use computed exception stacks
and vector/prefetch phases. Scalar PC inventory is checked in; ROM images
and captures remain ignored. These cases execute in the oracle scheduler;
native Stage 1 remains the game loop.

`XBoardMainIrqLines` follows `segaxbd_state::update_main_irqs` and the
per-line `device_input::set_state_synced` queue. Main compare-timer EXCK
and VBLANK changes come from `XBoardTimeline`; acknowledged line changes
are queued at the native CPU transaction clock and delivered at global
scheduler frontiers. Later changes on an already pending line drain at its
first synchronization deadline. Highest asserted line 2/4/6 is sampled at
the instruction's actual IPL selection phase. Exception entry also samples
IPL after its two internal clocks between handler prefetches, using the new
handler SR mask, matching MAME's `state_interrupt_df` `mmrw3` checkpoint.
A higher-priority arrival can therefore select another entry before the
first handler instruction. That nested-entry edge is not exercised by the
natural corpus and remains unverified. The ROM-free queue check
covers queued deadlines, combinations and coalesced changes.

The boot-port sidecar `AB2IRQ02` replays observed initialization writes and
acknowledges before frame 32. It initializes the timer counter by real
scanline clocks and boot register transactions, rather than a captured
counter or expected IRQ level. `AB2PIN02` supplies independent frame
observations of physical buttons and all eight analog lanes. ADC deadlines
and results use `ADC0804`; mux/direction writes use `IOExpander`. Port-C
sound-reset writes and 5250 sound writes synchronize/abort the main grant.
A newly armed ADC timer aborts only when it becomes the global head timer.
Other device timer deadlines remain supplied scheduler frontiers; using the
next scanline alone is incorrect when an audio/device timer expires sooner.
This computes the main CPU transaction stream; it does not close Z80 audio
execution or renderer/compositor ownership.

The fresh 120-frame checkpoint matches 3,160,862/3,160,862 instruction/IRQ
records and 8,320,519/8,320,519 ordered phases, zero differences/skips.
All 448 main entries, acknowledges, services, RTEs and resumptions remain
connected to computed caller state; 1,399,027 native main IPL polls select
all entries. There are zero supplied main phases/writes and exactly one
initial main context. It covers 1,852/7,786 main PCs. Unexercised PCs are
listed in the replay's `Unexercised joint main caller PCs` output and remain
unverified in this corpus; branch directions and gameplay-only instructions
require the full gameplay replay. Sub still has the initial partial ISR's
46 phases at three PCs; sound scheduling and non-main timer owners remain
explicit boundaries.

`--require-main-closed` rejects any supplied main phases, later main context
replacement or main IRQ without computed physical selection. Both Make and
CMake short/full joint targets require it; the full target also requires
natural RESET coverage. The fresh 600-frame live-gameplay corpus now passes:

| Assertion | Full result |
| --- | --- |
| Instruction/IRQ records | 20,532,151/20,532,151 identical |
| Ordered native phases | 53,449,565/53,449,565 identical |
| CPU grant groups | 1,371,502/1,371,502 identical |
| Bounded grant completions | 2,655,794/2,655,794 identical |
| Native writes | 3,145,935 computed |
| Main IRQ2/4/6 | 2,307/568/18 entries |
| Main physical selection | All 2,893 entries, 9,012,714 native IPL polls |
| Main service/ack/RTE/resumption | 2,893 each, all retained |
| Main supplied execution | Zero phases, PCs or writes; one initial context |
| Main startup/reset | One TRAP #12, two RESET callbacks, one RESET-handler RTE |
| Sub cancellation/reset | One synchronized cancellation and vector/prefetch sequence |
| Sub IRQ | 568 retained services/returns, 568 VPA acknowledges |
| Shared reads | 7,665,892/7,665,892 identical, no expected-read seeds |
| Shared writes | 1,194,565 computed, 12 supplied sub startup writes |
| Main PC coverage | 5,628/7,786 PCs; IRQ wrappers 108/113 |
| Sub PC coverage | 658/1,136 PCs |
| Remaining supplied sub execution | 101 phases at 20 PCs, 12 writes, three initial/startup contexts |

All rows have zero differences/skips. The other 2,158 main PCs and 478 sub
PCs, and branch directions not observed in the natural corpus, remain
unverified. The five unexercised IRQ-wrapper PCs are `$B836/$B83A/$B83E`,
`$146D8` and `$14702`. The replay lists all unexercised PCs explicitly. DIVU zero-divisor/
overflow exception paths are rejected rather than invented. Two native
continuations are parked beyond the final accepted grant; their later
completions are outside the corpus. Initial RAM/CPU state and boot-port
initialization are supplied fixture boundaries. Other hardware deadlines,
non-main boost request owners, sound execution and video/compositor
ownership remain outside this main CPU closure.

The focused frame-400..440 window matches 1,449,723 records and 3,762,992
native phases, zero differences/skips. Its 645,905 native main IPL polls
select all 204 main physical
entries/services/returns, the TRAP/RESET/RTE path and 40 retained sub
services. Two initial partial-instruction prefetch phases at `$14092` are
supplied; there are no later main context replacements. Its 101 supplied
sub phases are the same startup/initial boundary as the full corpus.
`make oracle-capture-joint-reset-window oracle-joint-reset-window` regenerates
and checks this window with physical main selection enabled.

Validation: `make -j2 test verify-stage1 oracle-test`, the affected flight/
companion/weapon/host caller checks, generated opcode/membership checks,
strict short/full main closure replays, game build and 180-frame headless
run pass. Nine mutations are rejected: physical main IPL, producer counter,
IRQ callee timing, remaining producer timing, TRAP stack SR, ADC head-timer
selection, RESET callback timing, cancellation register checkpoint and reset
E-clock origin. The final entry-selection audit also rechecks two rejected
mutations of VPA timing and the second handler prefetch. The ADC mutation specifically reproduces the late timer
competition that a scanline-only deadline test missed. A first callee
mutation at unexercised `$B840` was replaced by covered `$14A86`; the
unexercised path remains unverified. CMake registrations are updated but
CMake is unavailable here. Captures and ROM data remain ignored.

## Sub startup, sound handshakes and renderer bank feedback

This supersedes the supplied sub startup and video/sound boundaries in the
preceding checkpoint only to the extent stated here. The joint harness now
computes the 17 startup PCs `$402/$408/$40C/$40E/$414/$418/$41A/$41C`,
`$42A/$42E` and `$62E/$632/$634/$636/$63A/$63C/$63E`. PEA, synthetic
exception frame, RTE, TRAP #10, stack/register setup and byte/long clearing
follow the ROM instructions and MAME's `m68000-sdf.cpp` bus/internal/selection
phases. TRAP includes USP/SSP banking, stack word order, vector fetch and
prefetch; CLR.L preserves the low-word-first write order. User-mode RTE
privilege exceptions are rejected as unverified. No startup CPU context is
replaced after the initial fixture. `--require-sub-continuity` requires one
initial sub context and no later supplied phases; with `--require-reset`
it also requires all 17 startup PCs. Short captures without RESET do not
claim startup coverage.

The full live-gameplay replay requires RESET, main closure, sub continuity,
video frontiers and sound handshake. Its current results are:

| Assertion | 600-frame result |
| --- | --- |
| Instruction/IRQ records | 20,532,168/20,532,168 identical |
| Ordered native phases | 53,449,620/53,449,620 identical |
| Bounded grant completions | 2,655,852/2,655,852 identical |
| Native writes | 3,145,947 computed |
| Main selection/service/ack/RTE/resumption | 2,893 each, all retained; 9,012,714 native IPL polls |
| Sub service/return | 568 each; 11,518,883 native IPL polls |
| Initial contexts | One per CPU, no later replacement |
| Supplied main execution | Zero phases, PCs or writes |
| Supplied sub execution | 46 initial partial ISR phases at `$58A/$58E/$592`, zero writes; zero later supplied phases |
| Shared reads | 7,665,892/7,665,892 identical, zero expected-read seeds |
| Shared writes | 1,194,577 computed, zero supplied |
| Sub startup coverage | All 17 added PCs; generated sprite/road coverage remains 658/1,136 |
| Video bank swaps | 854 computed: 570 sprite, 284 road |
| Road controls | 278 computed |
| Hidden sprite scaler feedback | 568 physical-frame checks |
| Partial screen updates | 575 computed, exact time/clip/display assertions |
| Synchronized sound commands | 172 deliveries, including supplied pre-cutoff boot initialization |
| Sound latch reads | 1,278 computed values/NMI acknowledgement requests |
| YM/PCM transactions | 891,610 assertions at supplied Z80 clocks |

All comparisons have zero differences/skips. The focused frame-400..440
window matches 1,449,740 records and 3,763,047 native phases, zero differences/
skips: 204 main cycles, 40 sub services, 63 bank swaps, 19 road controls,
40 sprite feedback frames, 44 partial updates and 85 sound latch reads.
Its two initial main prefetch phases at `$14092` and the same 46 initial
sub ISR phases remain supplied, with zero writes. The short 120-frame
capture matches 3,160,862 records and 8,320,519 phases, zero differences/
skips, with 130 swaps, 88 sprite feedback/frontier checks and 172 latch
reads. It separately checks main closure, sub continuity and sound/video
assertions without requiring an unobserved reset startup; it observes no
new command deliveries and does not establish command delivery coverage.

### Sound synchronization and device census

The 315-5250 endpoint now optionally queues sound commands at the native
main bus transaction clock, aborting the grant, and delivers them at global
scheduler frontiers. This follows MAME's synchronized `write_to_sound`;
the prior immediate latch change occurred at a CPU-private clock. Default
standalone endpoint delivery remains immediate, and a ROM-free regression
checks deferred latch/NMI state and read acknowledgement. `AB2SND01` records
actual Z80 latch/YM I/O and PCM transactions. Replay checks grant containment
and computes latch values through `sound_read()` without expected-value
seeding. The read requests NMI clearing at the device endpoint; native Z80
interrupt entry/service and synchronized input-line delivery are not proved.
YM/PCM transactions and their CPU clocks remain supplied. The 172 commands
include boot initialization fixture transactions, not 172 native CPU writes.

`AB2_DEVICE_TRACE` records actual scheduler head owners and callback
expirations in an optional `.devices` trace. `report_device_ownership.py`
is a read-only inventory, not an acceptance test or deadline source. The
full trace observes 157,164 scanline callbacks, 2,310 ADC completions,
24,867 input queue callbacks, 600 first-scanline ticks, 600 VBLANK starts,
599 VBLANK ends, 172 sound deliveries, 600 tile latch callbacks, 503 sound
manager updates, 1,159 YM mode writes and 1,170 YM timer callbacks. Input
queue and YM mode callbacks show up to 1 microsecond delivery delay;
other observed owners show zero. YM timers own 7,199 head grants, tile
latches 11,289 and sound manager updates 2,717. These counts identify
remaining owners; they do not establish native YM/PCM execution, deadlines
or boost requests. The harness still receives other device scheduler
frontiers from the trace.

### Video bank feedback and physical partial updates

`AB2VBN03` captures the real hidden sprite and road banks at the fixture
boundary. Sprite draws compare the pre-handler CPU-visible bank against
the computed post-swap hidden bank. Road reads compare MAME's real hidden
buffer after the handler swap. Road control writes and frame-end hidden
sprite buffers are independent assertions. Earlier attempts to infer the
road buffer at a prefetch PC were wrong for operand-before-prefetch paths;
the final probe reads the actual buffer. The ignored isolated MAME build
adds save-item registration for the road pointer and observers for actual
screen updates. Original MAME source/binaries remain unchanged.

MAME's sprite renderer writes scaler address word 7 into the hidden bank,
including its initial address before hidden-sprite rejection and its final
address for each visible row. `HWSprites` now reproduces this hardware
feedback. `render_bank` loads the scheduler-owned big-endian bank and
exports the feedback to that bank; subsequent CPU swaps expose it naturally.
Its clipped renderer uses the actual partial update bounds. The joint
sprite bitmap is scratch output, not a proof of full RGB composition.

Native scanline zero resets the partial cursor; line 224 renders through
223. Display-enable changes and text register writes `$D0E80..$D0FFF`
request a partial update before the register changes, using the old display
state. Beam position follows MAME's nearest-pixel rounding on the 160 ns
pixel/64 microsecond line grid. Already rendered lines are skipped. The
initial capture fixture is after the frame notifier's visible rendering;
its partial cursor must therefore start at 224, avoiding duplicate feedback
before the next frame. This is an explicit initial fixture boundary, not a
claim about boot rendering.

`--require-video-frontiers` compares computed update time, clip and display
state with actual MAME `screen_update` calls. The `.bin.screen` assertions
do not drive timing or rendering. A separate fresh boot with the identical
gameplay sequence supplied this read-only metadata for the already recorded
full corpus; future Make capture targets produce it in the same capture
run. The independent existing video oracle remains 300/300 frames
pixel-identical, zero mismatched pixels; this does not close joint RGB,
tile/text latch or palette/compositor input ownership.

### Validation and remaining acceptance boundaries

Game build, ROM-free tests, Stage 1 integration, board/main IRQ/sub IRQ/sub
reset/sub progress oracles, generated main/sub opcode-membership checks and
180-frame headless execution pass. Make and CMake register the stricter
joint sidecar requirements; CMake execution remains unavailable here.
Five mutations fail differentially: TRAP saved SR, a hidden sprite bank
byte, delivered sound command, scaler word-7 feedback, and omission of the
text-register partial update. The last mutation produces a wrong time and
clip frontier rather than a final frame-only mismatch. Omitting the
display-enable partial update did not fail this natural corpus, because
observed toggles occur after the relevant rendering is complete; this case
is not registered as proven. Visible-area display toggles need directed
coverage and remain unverified.

The 2,158 unexercised main PCs, 478 unexercised generated sub PCs and
unobserved branch directions remain unverified. The five missing main IRQ
wrapper PCs are `$B836/$B83A/$B83E`, `$146D8`, `$14702`; rejected divide
exception paths also remain open. Initial RAM/CPU/boot port fixtures, the
46 initial partial sub ISR phases, and two continuations parked beyond the
last accepted grant remain explicit boundaries. Native Z80/YM/PCM execution,
remaining device head/boost owners, tile/text latches and full RGB compositor
inputs must still close before whole-host acceptance. Native Stage 1 remains
the game loop. ROM data, captures, sidecars and instrumented binaries remain
under ignored paths.

## Sound-reference continuation: separate native Z80 runner

`XBoardSoundCpu` separates cycle-stepped sound ROM execution from the Stage 1
audio player's 32,000-clock rendering batches, command queue and artificial
32-clock command gap. It uses the existing licensed `src/third_party/z80.h`
decoder, with one shared implementation for both audio runners. Its host
supplies 4 MHz grants and reset/NMI/IRQ lines. CPU state stays in the decoder
between ticks, interrupt entry and return; CPU reset retains board RAM.
The runner is not wired into the joint scheduler or game loop.

The bus follows MAME `segaxbd.cpp::sound_map/sound_portmap`: ROM below
`$F000`, PCM offsets `$00..$FF` mirrored across `$F000..$F7FF`, and 2 KB
work RAM at `$F800..$FFFF`. I/O masks the high address byte, YM aliases
`$00..$3F` preserve A0, and latch aliases `$40..$7F` call the supplied
315-5250 endpoint. Unmapped reads return `$FF`, ROM writes are ignored,
and an uninstalled mapped device callback fails. Side effects are delivered
once per bus request, rather than once per tick while request pins remain
asserted. Latch acknowledgement delegates to the verified compare/timer;
the host still owns synchronized NMI line clearing. `XBoardPcmStartup`
computes register reads/writes while all channels remain disabled. It
initializes registers to `$FF` as MAME does, and rejects any write that
enables a channel. Stream updates cannot change register state while every
channel is stopped; active stepping remains a separate acceptance task.

### Reference audit

Inspected `rossops/Arcade-SegaXBoard_MiSTer` at commit
`c48e62c06f3ffd56b05749488867040bb962666e`, fetched into a temporary checkout.
No upstream code, ROMs or golden captures were added to this repository.
Useful references are
[xb_soundsys.sv](https://github.com/rossops/Arcade-SegaXBoard_MiSTer/blob/c48e62c06f3ffd56b05749488867040bb962666e/rtl/audio/xb_soundsys.sv),
[PCM model](https://github.com/rossops/Arcade-SegaXBoard_MiSTer/blob/c48e62c06f3ffd56b05749488867040bb962666e/verif/models/segapcm.py),
and [PCM tests](https://github.com/rossops/Arcade-SegaXBoard_MiSTer/blob/c48e62c06f3ffd56b05749488867040bb962666e/verif/unit/chips/test_segapcm.py).
The sound RTL confirms the memory mirrors, reset input, latch read strobe
and YM IRQ wiring. The reset source is I/O chip 0 port C bit 0 in MAME;
the RTL header's chip numbering must not override the actual board source.

For future PCM acceptance, reuse the test dimensions (loop/end/stop,
fraction/address write-back, volumes and bank selection), not its fixture
as proof. The pinned test constructs a new `random.Random(5218)` for each
ROM byte, yielding a single repeated value rather than diverse sample data.
That can conceal address-selection errors. Use one persistent RNG or an
address-sensitive synthetic ROM, plus real MAME stepping captures. Its
sound gate compares audio envelopes, not exact samples or our joint native
ownership contract. FPGA cache delays and physical timing must also not be
substituted for the MAME scheduler semantics this project accepts.

### Bounded MAME evidence and exclusions

`oracle-capture-sound-boot` captures reset-entry execution without CPU state
setters. `oracle-sound-boot` starts the decoder once at reset and initializes
work RAM once from the capture; it never reseeds registers or later RAM.
All 556/556 pre-activation transactions match PC, direction, address and data,
zero differences/skips: 421 memory reads (134 computed PCM reads), one
computed reset-state latch read, and 134 computed PCM writes. The native
runner uses 2,880 input clocks through the terminal PCM activation request.
No captured read value is supplied to execution. ROM address-selection,
latch-value and PCM read-address mutations fail the comparison.

The capture can stop at a bounded transaction window, the first unsupported
YM read/active PCM channel, or a main command/I/O change. The current fixture
stops before enabling PCM channel 0. PCM stopped-register writes are computed;
other device writes are assertions, not proof of synthesis or stepping.
Captured transaction timestamps are ordered assertions and timing diagnostics,
never execution inputs. After aligning the first bus event, 278/556 relative
phase times equal MAME's, with a maximum difference of 500 ns. This exposes
the existing pin decoder's access-phase placement as remaining work, not
timing acceptance. Clock offsets, per-phase timing and native grant ownership
are unverified. The reset-state latch requires no main
command or NMI assertion in this fixture. ROM-free synthetic programs
separately check NMI/RETN and IM1 IRQ/RETI caller retention, reset hold/release,
second NMI delivery and mirrored device access without repeated side effects.
They do not constitute natural ROM interrupt oracle coverage.

Native Z80 progress in the joint stream, main-to-sound command/NMI queue
timing, YM busy/timer/status ownership, PCM stepping and register write-back,
audio output and further ROM paths remain open. The ordered sound milestone
and whole-host acceptance remain incomplete. Make and CMake register the
new CPU checks; the Make oracle suite includes the boot comparison and
rejected mutations. Captures remain in ignored `build/oracle/`.

Validation: `python3 tools/workflow.py run required` passes (`make test`,
`make verify-stage1`, complete `make oracle-test`), including the existing
joint and pixel regressions. The new boot comparison and all three mutations
also pass separately; a final `quick` run passes after the PCM boundary
regression was added. `make -j4` game build passes. CMake source/test
registration is updated, but CMake execution remains unavailable here.

## Active PCM continuation: continuous sample and write-back oracle

`hwaudio/xboard_pcm.hpp` implements the MAME `segapcm.cpp` BANK_512
algorithm, with one tick per 4 MHz/128 raw stereo sample. It retains all
2 KB device RAM and sixteen internal fractional address bytes. Channel
enable/stop, loop/end comparison (including the 8-bit end-register wrap),
24-bit address addition/wrap, bank bits 6:4, seven-bit panning volumes and
address/fraction write-back follow MAME directly. The host must advance
the stream before register accesses; the component does not invent stream
update deadlines. CPU reset does not reset this device state.

The MiSTer model and randomized channel tests suggested the coverage
dimensions. Two reference differences matter: its synthetic ROM generator
repeats one byte, and its per-tick model clamps the mixed PCM channels to
signed 16-bit. MAME accumulates raw sums before speaker routing and does
not clamp in `segapcm_device::sound_stream_update`. This component returns
32-bit raw sums, matching MAME even when they exceed signed 16-bit.
Original algorithm attribution and the BSD-3-Clause notice are retained
in `LICENSES/MAME-SegaPCM.txt`; no MiSTer GPL source was imported.

### Read-only MAME observer and replay

`build_pcm_probe.py` compiles an observer in a separate local MAME binary
from the existing build objects. The MAME source tree, installed binary
and original objects remain unchanged. Reference source is clean MAME
commit `6b511c0a858b8de0499d38cf382f0ef666ebf2cc`.

The `AB2PCM02` stream records one initial device state, every PCM register
write/read in execution order, each real MAME stream update's sample count,
the actual pre-routing outputs and its post-update RAM/fraction state.
Outputs are observed with `stream.get_output`, scaled by 32768 back to
exact raw integers; no alternate PCM algorithm generates the golden
samples. This scale is a power of two and the sixteen-channel raw sum fits
exactly in the stream's floating-point representation. Register accesses
retain MAME's update-before-read/write ordering.

`oracle_pcm.cpp` loads CRC-checked sample ROMs and fills the unpopulated
region with `$FF` as MAME's ROMREGION_ERASEFF requires. It loads device
state once, then computes every sample, read value and write-back state
continuously. Later snapshots are assertions only, never replacement RAM
or fractions. CPU register writes and stream batch lengths remain external
fixtures; this is device algorithm acceptance, not joint native scheduling.

### Natural and directed results

Each capture runs 600 physical frames with the established coin/start and
stick/button inputs. The directed variants also program stopped channels,
then bank/panning/delta/loop/end/control registers on frames 500..563.
All CPUs continue executing; their interleaved writes are captured and
replayed alongside these explicit device fixtures.

| Corpus | Raw stereo samples identical | Full RAM/fraction states identical | Writes / reads | Samples with raw sums outside signed 16-bit |
| --- | --- | --- | --- | --- |
| Natural gameplay, arcade ROM | 314,376/314,376 | 6,216/6,216 | 22,783 / 15,804 | 0 |
| Directed registers, arcade ROM | 314,376/314,376 | 6,277/6,277 | 33,003 / 15,814 | 26,008 |
| Directed registers, synthetic ROM | 314,376/314,376 | 6,277/6,277 | 33,003 / 15,814 | 26,369 |

All have zero differences/skips. Natural coverage observes all sixteen
channels, banks 0..4, four loops, 35 stops and 217,970 fraction carries.
Each directed run observes all sixteen channels/eight banks, 89,177 loops,
491 stops, 505,847 fraction carries, 21,460 full-address wraps, 134,719
active samples with wrapped end registers and 263,179 samples with high
volume bits masked. The directed control fixture also sets ignored control
bits 7/3/2: 702,637 active channel samples retain those bits without changing
the algorithm, and 172,034 active channel samples have zero delta. There
are 646 explicit channel disables retaining nonzero fractions in each
directed run. Both positive and negative output sums are observed. These
paths are required by the directed gate, along with CPU register read/write
interleaving and continuous fractional state.

The synthetic fixture replaces only the PCM sample region in the local
MAME process with an address-sensitive generated pattern. Native replay
generates the same explicit external ROM fixture; no commercial bytes or
captures are committed. This is a directed device test, separate from
arcade-ROM fidelity. It distinguishes banks 6/7, both erased `$FF` in the
arcade set: a bank-7-to-bank-6 alias passes both arcade-ROM captures but
fails the synthetic one. It therefore closes a hole that a constant test
ROM or the erased arcade banks cannot expose.

Eight independently compiled mutations fail actual comparisons: bank
shift, erased-bank alias, end increment, stop bit, loop address, volume
mask, fraction retention and output clipping. Compilation errors, crashes,
missing captures and coverage-only failures are not accepted as mutation
proof. `oracle-test` includes all three PCM captures and the mutation gate;
Make and CMake register the comparisons.

### Remaining ownership boundaries

The startup 556-transaction Z80/disabled-PCM oracle remains intact. The
active component is not wired into Stage 1 or the joint scheduler. The
500 ns Z80 access-phase mismatch, CPU command/reset/NMI/IRQ scheduling,
native PCM stream update sizes/deadlines, YM busy/timer/FM behavior,
speaker gains/mute, final mixed/resampled audio and independently owned
initial device state remain unverified. The Stage 1 ROM loader still
zero-fills its unused PCM tail; the new oracle explicitly supplies MAME's
erased `$FF` region, and a future native adapter must preserve that contract.
These results do not complete either ordered sound milestone or whole-host
acceptance. Captures, probe source/binaries and synthetic output remain in
ignored `build/oracle/`.

Validation: `oracle-pcm` and `oracle-pcm-mutations` pass all three corpora
and eight rejected mutations. `python3 tools/workflow.py run required`
passes ROM-free tests, Stage 1 integration and the complete oracle suite,
including the existing boot/CPU/joint/video checks and the PCM comparisons.
CMake registrations are updated; CMake execution remains unavailable here.

## MiSTer-guided joint tile/text latches and partial RGB (2026-10-08)

The reference remains MiSTer commit
`c48e62c06f3ffd56b05749488867040bb962666e`. Its
[`xb_tilemap_5197.sv`](https://github.com/rossops/Arcade-SegaXBoard_MiSTer/blob/c48e62c06f3ffd56b05749488867040bb962666e/rtl/video/xb_tilemap_5197.sv),
`xb_video_timing.sv`, `xb_mixer.sv` and `verif/models/tilemap16b.py` identify
useful acceptance dimensions: line-261 global latches, live row/column
scroll and alternate sets, text decoding, accumulated priority marks and
sprite effect pens. No MiSTer source was imported. MAME's BSD implementation
(`segaic16.cpp`, `segaxbd.cpp`, same source revision as the scheduler probe)
remains the behavioral oracle. This verifies MAME's partial-render model;
it does not adopt the FPGA's next-scanline buffers or pixel-clock pipeline.

`Screen::render_partial` composes an inclusive visible clip, resolves its
pixels using the palette at that update, and renders sprites against the
computed buffered bank while retaining scaler feedback. Display-disabled
clips produce black RGB and do not invoke the sprite engine. The existing
`render_frame` API delegates to the complete enabled clip. Road/tile work
uses scratch surfaces; only the requested output rows are published.

The joint main/sub replay now drives `HWTiles::latch_frame_registers` from
its physical line-261 callback. All four page, Y-scroll and X-scroll sets
are compared with the actual MAME latch callback at the exact timestamp.
One initial latch boundary is loaded from the last actual callback before
the initial CPU/RAM boundary; later observations are assertions, never
seeds. Text/tile RAM, palette RAM, road control/buffer and sprite buffer
come from the retained native main/sub memory and computed bank swaps.
Copying these into renderer caches does not supply later MAME snapshots.
Existing CPU bus, bank and scaler assertions remain active.

The isolated scheduler probe adds two read-only streams alongside the
existing `.screen` frontier assertions:

- `.latches`: decimal nanoseconds and twelve words in page/Y/X order.
- `.pixels`: big-endian nanoseconds (8 bytes), inclusive top/bottom
  (2 bytes each), display flag (1 byte), then each clipped pixel's palette
  index (2 bytes), priority mark (1 byte) and RGB (3 bytes), row-major.

Scope-exit pixel observation covers both enabled and disabled MAME returns.
Enabled clips compare all three values for every pixel. Disabled clips
compare black RGB; MAME's retained priority marks and special black palette
index are not treated as active mixer outputs. This avoids the frame
notifier's previous-image/current-palette lag. All captured bytes and the
instrumented executable remain ignored under `build/oracle/`.

The explicit `AB2_DISPLAY_FIXTURE` run additionally records `.fixture`
events: nanoseconds, display flag, word count, and offset/value pairs.
Offsets `$0000–$07FF` denote text words, `$1000–$17FF` buffered sprite
words, and `$2000–$3FFF` palette words. After one second, line 64 disables display; line 128 enables it and
writes global registers and live tables; line 260 writes them again before
the next latch. Each table write uses the actual MAME device handler. The
fixture selects different page quadrants, nonuniform row/column scroll,
alternate rows, and global changes which must wait for line 261. Native
replay applies these as **supplied fixture transactions**, preserving the
normal CPU execution and all assertions. They are test stimulus, not a
claim that the ROM generated these directed register values. A runtime ROM
scan selects a real sprite row containing pen `$A` and an end marker; 32
repeating-row sprite descriptors exercise all four priorities. Palette
bit 15 toggles while preserving each entry's normal color. The fixture
contains no embedded ROM bytes. Coverage gates require both shadow and
hilight output pixels as well as consumed fixture events, visible blanking, enabled renders with
pending globals and both row/column scroll modes.

Commands (reuse captures unless the probe/fixture format changes):

```bash
python3 tools/workflow.py run oracle-capture-joint-main-progress
python3 tools/workflow.py run oracle-capture-joint-main-progress-full
python3 tools/workflow.py run oracle-capture-joint-video-directed
python3 tools/workflow.py run oracle-joint-video
python3 tools/workflow.py run oracle-joint-video-directed
python3 tools/workflow.py run oracle-joint-video-mutations
python3 tools/workflow.py run required
```

The first 120-frame natural run compares 6,307,840/6,307,840 pixels and
88/88 computed latches, zero differences/skips. It has priority-mark
coverage bitset `$1111`, no effect pixels and no renders with pending
global registers. The stronger directed fixture and 600-frame run extend
this coverage; their final results are recorded below.

The final 120-frame directed replay compares 6,307,840/6,307,840 pixels
and 88/88 native latches, zero differences/skips. All 180 supplied fixture
events are consumed; 60 disabled visible clips match. It checks 60 enabled
clips with pending globals and 180 clips in each row/column scroll mode.
Priority-mark coverage bitset is `$1115`. There are 350,868 effect pixels:
176,198 hilights and 174,670 shadows. The CPU execution census still has
3,160,862 native records and 8,320,519 native phases, one initial context
per CPU, no supplied main phases, and only the initial sub ISR's 46 read
phases supplied. Fixture memory writes remain explicitly supplied.

CPU I/O writes and directed device calls share the same native display
transition policy. This permits an omitted visible partial update to fail
the actual MAME frontier assertion, rather than exercising a separate
fixture-only copy of that policy.

Coverage remains the recorded natural and directed corpora. The reported
priority-mark bitsets describe observed accumulated marks, not all sixteen
possible combinations or exhaustive graphics/page/quadrant contents.
Flip modes, non-After-Burner banking, unobserved game states and the FPGA's
scanline buffering/pixel pipeline are outside this acceptance. Initial CPU,
RAM, video-bank and latch boundaries remain supplied; Z80/YM/PCM scheduling
and other device head/boost owners retain their existing exclusions.
Stage 1 remains the game's reconstruction loop; this compositor ownership
is wired into the joint oracle, not a claim of whole-game acceptance.

All ten targeted mutations are rejected by actual MAME assertions: latch
line, alternate-set selection, row-table origin, column-table origin,
text color decode, strict sprite priority, effects-bank selection,
immediate global latching, omitted display partial frontier and nonblack
blanking. Comparison failures alone count; compile/runtime/coverage failures
do not. The mutation harness builds temporary overlays and leaves production
sources unchanged. These mutations are included in `oracle-test` alongside
the natural 600-frame and directed checks.
Normal CPU I/O stays active during the fixture: mux/output writes can
re-enable display before the line-128 fixture edge. The replay compares
those actual intervening partial updates as well, using the shared display
transition policy and the retained CPU bus timestamps.

The final 600-frame natural run compares 40,714,240/40,714,240 RGB pixels
and 568/568 line-261 latches, zero differences/skips. It includes 22
whole/partial disabled clips and two enabled clips with pending globals;
priority-mark coverage bitset is `$1515`. Its zero effect-pixel coverage is
explicitly supplemented by the directed run above. The original joint CPU,
RESET, bank-swap and scaler-feedback assertions remain enabled.

| Capture length and stimulus | Visible frames after initial boundary | RGB pixels identical | Computed latches identical |
| --- | ---: | ---: | ---: |
| 120, natural | 88 | 6,307,840 | 88 |
| 600, natural coin/start/flight | 568 | 40,714,240 | 568 |
| 120, directed device fixture with CPUs active | 88 | 6,307,840 | 88 |

Total: 53,329,920 RGB pixels and 744 computed latch observations, zero
differences/skips. Each capture has its own supplied initial boundary.

Final validation: `python3 tools/workflow.py run required -j2` passes
ROM-free tests, Stage 1 integration (audio RMS remains 903.949), and the
complete oracle suite, including all ten integrated video mutations and
all existing fixture coverage. Full local logs are under
`build/workflow/20261008T174422.903992Z/`. Existing routine-local race
exclusions remain documented; the new joint video comparisons skip zero
pixels/latches. CMake registrations are updated; CMake execution remains
unavailable in this environment.
The game build also passes (`python3 tools/workflow.py run all -j4`).

## Phase 3d continuation: cold boot and shared-bus edges

MiSTer's `docs/DESIGN.md` CPU-bus section at commit
`c48e62c06f3ffd56b05749488867040bb962666e` identifies repeated sub resets,
byte strobes and main ownership of the sub address space as useful boot
verification dimensions. The reference is
<https://github.com/rossops/Arcade-SegaXBoard_MiSTer>. Its RTL holds sub DTACK
while main owns that bus; that FPGA arbitration delay is not imported into
our MAME scheduler model. MiSTer's reported polling-loop resynchronizations
are likewise not exclusions in this oracle. No reference HDL is copied.

`capture_bus_arbiter.lua` now supports `AB2_ORACLE_START_FRAME=0`. It asserts
that scheduler base and grant index are still zero before observation starts,
then captures through frame 32 using fresh NVRAM/config. `AB2ARB04` retains
the prior `AB2ARB03` fields and adds one byte per phase: bit 0 marks reset
entry from the read-only CPU reset serial, and bit 1 marks a directed,
read-only RAM-mirror probe. The reader retains compatibility with existing
`AB2ARB03` captures. Consecutive reads of ROM addresses 0/2/4/6 alone are not
reset evidence: the boot ROM checksum also visits those addresses. Only the
CPU probe's reset-entry tag arms the four-vector-word coverage counter.

Cold shared RAM starts from the native memory object's zero initialization;
the captured initial bytes assert that initialization and never seed it.
The existing frame-32 corpus still supplies its one initial RAM boundary.
Each main shared access now uses `MainBus`, and each sub access uses its
actual `SubBus`, retaining one underlying RAM object. Byte transactions use
`read8`/`write8` with the observed lane rather than normalizing every access
through a sub word read/merge. CPU resets do not clear that retained RAM.
All subsequent read values are assertions, never replacements.

Natural boot uses the canonical `$29C000/$2A0000` and `$9C000/$A0000` views.
To exercise mirrors explicitly, the capture adds 16 debug reads per CPU per
RAM chip after real nonzero shared reads, cycling through all eight 16-KB
aliases twice. These 64 observations are labelled probes, not CPU bus cycles;
they change no CPU state, RAM, device state or scheduler deadline. Coverage
requires all eight aliases for both chips in both CPU maps. They do not
establish CPU access timing for those aliases. Natural CPU byte writes cover
both high and low lanes independently on both CPUs.

The fresh cold-boot corpus matches, with zero differences/skips:

| Assertion | Identical / total |
| --- | ---: |
| Scheduler grant groups | 33,234 / 33,234 |
| CPU runs | 89,422 / 89,422 |
| Ordered natural main/sub program phases | 3,100,956 / 3,100,956 |
| Natural shared reads | 418,054 / 418,054 |
| Additional labelled mirror reads | 64 / 64 |
| Explicit shared producer writes delivered | 176,846 / 176,846 |

There are 1,547 grant groups without captured program phases, 45 quantum
requests and 63 natural sub reads after a main write with a later CPU-local
timestamp. Tagged reset coverage is one main and six sub vector sequences;
this is a count of actual CPU reset entries, not a claim that every entry
originates in a main RESET instruction. Word/high/low shared writes are
16,699/73,310/77 for main and 85,860/872/28 for sub.

Eight mutations are rejected by actual comparison mismatches: clearing
shared RAM on sub reset, changing main address mapping, aliasing the two sub
RAM chips, corrupting main high-byte writes, exchanging sub byte lanes,
reversing CPU grants, adding a budget clock and shortening boost duration.
`check_bus_boot_mutations.py` builds temporary overlays under ignored
`build/oracle`; a build failure, crash or coverage-only failure is not a
successful rejection. `oracle-test` includes the cold positive and mutation
gates; CMake also registers the positive check.

```bash
python3 tools/workflow.py run oracle-capture-bus-boot
python3 tools/workflow.py run oracle-bus-boot-mutations
python3 tools/workflow.py run required -j2
```

This extends bounded bus-policy acceptance through initialization. CPU
instructions, shared producer values, completion frontiers, device deadlines,
boost requests and reset tags remain observed inputs in this bus-only test.
It does not close native main/sub cold-boot instruction execution, boot I/O,
render/bank initialization, the initial partial sub ISR or the final parked
continuations. Those remain task 5 in [historical PHASE3D PLAN](https://github.com/duduke/AfterBurner2-Recomp/blob/1f584d75aef22bf6e487a502235bc61065738e94/docs/PHASE3D_PLAN.md); whole-host acceptance
and the retained Stage 1 loop are unchanged. ROM/capture bytes remain ignored.

Validation passed `python3 tools/workflow.py run required -j2` (full logs in
`build/workflow/20261008T185916.490868Z/`): ROM-free tests, Stage 1 and the
complete oracle suite, including all eight new boot/shared mutations and all
ten integrated video mutations. The 600-frame joint replay retained its
20,532,168 instruction/IRQ records, 53,449,620 phases and 40,714,240 RGB pixels,
zero differences/skips. Legacy routine-local race exclusions are unchanged;
they are not new boot/joint exclusions. A fresh frame-32-to-34 `AB2ARB04`
capture also passed (32,131 shared reads), checking the default post-boot
format path without replacing the existing `AB2ARB03` corpus. The game build
and `git diff --check` pass. CMake registrations remain untested because
CMake is unavailable here. This checkpoint is included in the authorized
audio/video/boot checkpoint commit and merge.

## Tooling pilot: REA and 68000 boot analysis

The requested small REA pilot used upstream source commit
`84a17d55199b1b46493aea6bf4e41029a3b200fc` from
<https://github.com/morluto/rea>. The selected target was the sub CPU startup
at `$402`, stopping before `$420`, including exception-frame/RTE initialization.
Our existing disassembler produced eight instructions, zero unknown opcodes.
The existing MAME sub-reset oracle passed seven reset states and 42 ordered
vector/prefetch phases, zero differences/skips. That reset oracle checks
vector/prefetch execution; it is not an execution test of all eight startup
instructions merely because the listing decoded successfully.

`tools/rea_boot_pilot.mjs` runs upstream's unmodified executable-header parser
using Node's type stripping and a `.js`-to-`.ts` resolution hook. It requires
the exact clean source revision; no REA source or algorithms are imported into
the game. The pilot writes all ROM-derived inputs and results to ignored
`build/oracle/rea_pilot/`. It exercises three real ROM-based admission attempts
and one supported-format control:

| Input | REA admission result |
| --- | --- |
| Actual interleaved sub CPU ROM | Unsupported binary format |
| Actual interleaved main CPU ROM | Unsupported binary format |
| Sub ROM in ELF32 big-endian EM_68K, entry `$402`, load address zero | Unsupported ELF architecture |
| Existing built native game Mach-O | Accepted |

All four expected outcomes passed. The ELF wrapper's header, PT_LOAD mapping
and unchanged ROM payload were independently checked. Merely wrapping the ROM
in ELF does not solve the missing architecture admission. No architecture
spoofing was used. These results are parser acceptance/rejection evidence;
they are not successful REA decompilation or native-provider measurements.

The source target model admits x86/x86_64/arm/arm64, not 68000 or Z80. Ghidra
raw BinaryLoader configuration is currently specific to DOS COM/x86. A useful
68000 integration would need target/architecture admission, explicit
big-endian raw-ROM processor/load/entry configuration, and verified analysis
seeded from the actual reset/vector/call evidence. Installing REA alone would
not provide that. This host also has no detected Ghidra/Hopper installation or
JDK, and its active Node 24.3 is below REA's supported 24.11 minimum for that
major release. The isolated parser ran under Node 24.3; no supported full CLI,
MCP server or provider run is claimed.

**Decision:** defer REA integration. No decompiled startup routine or measured
analysis speedup was obtained. Its calls/xrefs/evidence interface remains a
possible later aid, but adapter and provider setup would exceed this small
pilot. Continue the existing instruction-by-instruction/MAME workflow; if
static navigation becomes the bottleneck, evaluate direct 68000-capable
Ghidra analysis before building a REA adapter. CPU/device timing and oracle
runtime would still require our existing checks. No acceptance task is closed
by this tooling experiment; game code and agent configuration are unchanged.

Reproduction (requires the pinned upstream checkout and existing game build):

```bash
mkdir -p build/oracle/rea_pilot
python3 tools/build_rom_image.py subcpu build/oracle/rea_pilot/subcpu.bin
python3 tools/build_rom_image.py maincpu build/oracle/rea_pilot/maincpu.bin
node --experimental-strip-types tools/rea_boot_pilot.mjs /path/to/rea-checkout
python3 tools/workflow.py run oracle-subreset
```

Pilot admission and ELF validation, script syntax, `oracle-subreset`, artifact ignore checks and
`git diff --check` passed. No native code was changed, no REA/provider package
was installed, and no agent registration/skill was applied. The earlier full
required-suite checkpoint remains applicable to the unchanged native code.


## Tooling pilot: trace-assisted Z80 sound-ROM analysis

Ran unmodified `maziac/z80dismblr` source at commit
`2ff52822f0d706f9f39762e079f300f45cfd451d` in a temporary checkout.
Locked dependencies were installed with lifecycle scripts disabled, then the
TypeScript source compiled successfully. No upstream code was imported into
the game. `tools/z80_analysis_pilot.mjs` checks the revision and clean source,
recompiles it, and runs both reset-only and MAME-trace-assisted analysis.
The input is the user's `epr-11112.17`, truncated to the board's ROM mapping
below `$F000`; PCM and RAM addresses are not presented as static ROM.

`tools/oracle/capture_z80_analysis.lua` records a read-only debugger trace,
with fresh NVRAM/config through the existing capture wrapper. There are no
register setters, synthetic sound commands or device-state replacements.
The wrapper uses `-sound none`, so this is specifically a sound-disabled
cold-boot analysis window, not an audio-enabled gameplay capture. The script
closes the trace and writes completion metadata before exiting. Its trace
format is directly compatible with the upstream four-digit-address reader.

The initial 30-frame trace recorded 184,177 instructions at 158 unique PCs.
Extending to 120 frames recorded 789,553 instructions at the same 158 PCs;
the final 300-frame capture recorded 2,009,361 instructions, still at 158 PCs.
The retained capture is the 300-frame one. Upstream single-instruction decoding
matches MAME's mnemonics and operands at **158/158 unique observed PCs,
zero differences/skips**, normalizing only whitespace, case and equivalent
numeric notation. A private in-memory reset `DI` to `EI` mutation is rejected.
This is decoder agreement, not instruction execution or access-phase proof.

Both analyses produce 1,423 candidate instruction starts and 39 call-graph
nodes, plus a reset flowchart. Both include all 158 observed PCs; trace import
adds **zero** instruction starts. The remaining static candidates are not
validated merely because they appear in the listing or graph. No indirect
`JP (HL)` or `RETN`/`RETI` was observed. Reset/startup and polling are covered;
command dispatch, natural NMI/IRQ service, runtime RAM code and indirect-target
recovery are not established by this corpus. Static labels/comments remain
heuristics (for example, a stack address can be labelled as code); use the
ROM and MAME evidence to interpret them.

Reproduce from the repository root, after fetching the pinned upstream
checkout into a temporary directory:

```sh
npm ci --ignore-scripts --no-audit --no-fund --prefix /tmp/ab2-z80dismblr-pilot
mkdir -p build/oracle/z80_pilot
AB2_ORACLE_CALLER_SNAPSHOTS=1 tools/oracle/capture.sh tools/oracle/capture_z80_analysis.lua build/oracle/z80_pilot/sound.tr 300 10
node tools/z80_analysis_pilot.mjs /tmp/ab2-z80dismblr-pilot
```

Listings, graph files, ROM mapping, trace, completion metadata and
`analysis.json` stay under ignored `build/oracle/z80_pilot/`. The pilot is
optional and adds no build/runtime dependency. Validation passed
`python3 tools/workflow.py run oracle-sound-boot-mutations` (556/556 existing
transactions identical, zero differences/skips; all three mutations rejected)
and `python3 tools/workflow.py run test-sound-cpu`, plus Node syntax and diff
whitespace checks. These are focused checks, not whole-host or full-suite
acceptance; no production runner or scheduling code changed.

**Decision:** retain this as an optional inspection aid. The graph output may
reduce manual navigation, but this pilot measured no additional code discovery
from the trace and no engineering speedup. Before adopting it for command/NMI
analysis, obtain a bounded audio-enabled capture with actual command delivery
and compare its observed paths. The current Z80 access-phase mismatch, native
interrupt scheduling, YM/PCM deadlines and game integration remain open.


## Native sound CPU continuation and joint execution (2026-10-08)

The trace-assisted disassembler pilot supplied no extra code starts, so this
continuation uses the existing decoder, ROM loader, MAME probes and bus oracle.
`XBoardSoundCpu::run` now computes each grant completion, including private
machine-cycle debt and synchronized callback aborts. The adapter previews at
most two private decoder ticks, stopping before any unknown read, to issue
memory and I/O callbacks at the MAME machine-cycle start. The result remains
latched until the real decoder pins request it; divergent previews fail.
The third-party decoder is unchanged. Boot now matches all 556 transactions
**and** their relative timestamps, with zero nanoseconds of phase difference.

MAME `z80_device::device_start/device_reset` supplies the reset contract:
power-on general/alternate registers are zero, Z is set, IX/IY are FFFF;
reset retains those banks, SP, IX/IY and IM while resetting PC/WZ/I/R/IFF and
pending NMI. Board RAM and YM/PCM are retained. The adapter suppresses the
pin decoder's discarded NMI M1 read because MAME performs five private clocks
before the two stack writes. IRQ acknowledge is observed at accepted entry,
followed by MAME's separate two- and five-clock latency groups. NMI edge
latching and synchronized assertion/clear ordering retain the caller.

The continuous oracle starts at sound power-on, asserts initial RAM rather
than loading it, and never loads sound registers, return addresses or device
read values from a capture. Standalone main I/O/command records are explicitly
external providers. The joint mode changes to computed main bus callbacks at
the existing main/sub initial-context boundary; it checks latch continuity
without reseeding the CPU, sound RAM, YM or PCM. Every sound bus record checks
grant, nanosecond time, kind, instruction PC, address and value. Grant budgets
and returned completion are computed by the production arbiter/runner in
joint mode; captured fields are assertions. The full sound sidecar closes at
machine stop so the scheduler's terminal padding remains covered.

Standalone MAME results:

| Corpus | Identical sound transactions and timestamps | Interrupt coverage |
| --- | --- | --- |
| 16-frame ROM startup | 267,407/267,407 | Reset and device initialization |
| 600-frame ROM boot/coin/start | 10,634,620/10,634,620 | 172 NMI entries and 172 RETN |
| Hand-authored YM timer/IM1 fixture | 44,767/44,767 | 2 IRQ entries and 2 RETI |

All have zero differences/skips. The directed program is original test code,
not ROM bytes. It arms the physical YM timer through Z80 instructions, saves
and restores AF/BC, stops/acknowledges the timer, and returns via RETI. A
read-only MAME probe separately asserts both IRQ timestamps, interrupted and
return PCs, SP, AF, BC, DE and HL. The natural ROM corpus accepts no maskable
IRQs. IM0/IM2, HALT interrupt timing, other unseen opcodes/flag combinations
and reset/NMI edge coincidences are not promoted by these captures.

`XBoardYmTimers` implements the OPM timer/status/busy subset from MAME/ymfm;
timer A and synchronized mode/status behavior are exercised here. Timer B
requires an actual FM sample-clock owner and fails if it is unbound; its
phase and FM audio synthesis remain unverified. PCM reads
advance the existing verified PCM engine to their bus sample index. Captured
global timer delivery frontiers still drive the oracle's device delivery;
independent head timers, YM synthesis and audio batch/deadline ownership are
the next acceptance task. This closes neither whole-host/game-loop acceptance
nor unobserved CPU/device behavior.

Reproduction (captures and ROMs remain ignored):

```sh
python3 tools/workflow.py run oracle-capture-sound-execution
python3 tools/workflow.py run oracle-capture-sound-execution-full
python3 tools/workflow.py run oracle-capture-sound-irq
python3 tools/workflow.py run oracle-capture-joint-sound-full
python3 tools/workflow.py run oracle-sound-execution-mutations
python3 tools/workflow.py run oracle-joint-sound-full
python3 tools/workflow.py run required
```

The mutation harness builds private copies and rejects wrong bus phases,
discarded-NMI bus accesses, latch values, disabled IRQs, reset register loss,
NMI clearing and YM timer-A periods. No production edit is
left behind. ROM-free tests and Stage 1 integration pass. The full 600-frame joint result
is **10,634,678/10,634,678 sound bus records and nanosecond timestamps
identical**, zero differences/skips; 819,024 computed sound grants, 172 NMI
entries and 172 RETN. All 3,049 native main sound host writes match;
45 pre-window main boot writes are labelled external providers. The shared
stream retains 20,532,168 native main/sub instruction records,
53,449,620 accepted phases, 40,714,240 exact RGB pixels and one initial context
per main/sub CPU. The only supplied sub execution remains the initial partial
ISR's 46 phases, with no writes or later supplied sub phases. The full sound
sidecar also covers scheduler padding outside the main/sub final accepted grant:
53,449,666 total ordered phases and 1,371,502 computed grant groups match.
There are zero supplied YM/PCM transaction clocks in the joint acceptance window.

The complete `oracle-test` run at `build/workflow/20261008T203251.615168Z/`
passed its MAME comparisons and sound/PCM mutations, then failed to compile a
temporary video mutation copy because its include path lacked `tests/`.
The harness now includes that shared fixture directory. Its repaired mutation
verification is recorded below; this build issue did not change any oracle
flags or remove fixtures. CMake targets were updated but could not be exercised
because this host has no CMake executable.


The repaired video mutation harness subsequently rejects all ten mutations
against the same natural/directed MAME fixtures (exit 0; observed result lines
recorded in ignored `build/oracle/joint_video_mutations_repaired.txt`). Thus
all comparison and mutation gates exercised by the required suite pass.
The complete make target was not repeated after this include-only harness fix;
its first run retains the compile-failure exit status described above.
The latest ROM-free/Stage 1 checks are in
`build/workflow/20261008T203932.219427Z/`, and the seven sound mutation checks
are in `build/workflow/20261008T203842.928748Z/`. No captures or ROM data are
tracked, and no game-loop wiring or whole-host acceptance is claimed.

### Oracle host synchronization optimization (2026-10-09)

Profiling identified mutex/condition-variable handoffs at each literal CPU
checkpoint as the dominant cost. `XBoardCpuContinuation` now exchanges one
atomic turn with release/acquire ordering. A single host and its worker
exchange exclusive ownership of the existing plain CPU/bus state; up to 2048
short spin iterations avoid kernel parking on hot handoffs, followed by
`atomic::wait` for long delays. Idle RESET leaves the worker parked; active
RESET still cancels the retained instruction. Grant ordering, private step
debt, RMW locals, VPA waits and abort clocks remain unchanged. The workflow
runner now defaults to at most eight independent make jobs, using more of
this host's ten CPU cores and 32 GiB RAM.

The ROM-free continuation regression adds 2000 repeated jobs with shared-state
publication checks, idle RESET/reuse, and delayed host/worker handoffs reaching
the sleep fallback. ThreadSanitizer passes the regression. A private copy
replacing acquire loads/waits with relaxed ordering is rejected by a TSan
data-race report (exit 66); evidence remains ignored at
`build/oracle/continuation_order_mutation.log`. The reproducible
`benchmark-cpu-continuation` target checks 100,000 handoffs; an isolated initial
before/after sample measured 0.319456/0.0213953 seconds (about 15x). This is
host wall time, load dependent, and never feeds emulated timing.

The complete `oracle-test` now passes in **407.91 seconds**, with eight jobs,
at `build/workflow/20261008T210826.889022Z/`. The previous successful full run
at `build/workflow/20261008T174422.903992Z/` took **1380.15 seconds** before
the new sound gates were added: the observed whole-suite reduction is about
3.4x despite those additional gates. No captures were regenerated, no fixtures
removed and no comparison flags weakened. All ten video mutations and seven
native sound mutations reject, alongside the existing boot/shared, sound-boot
and PCM mutations. This successful complete run supersedes the earlier video
mutation include-path failure described above.

The focused 600-frame native joint sound gate also passes in 105.74 seconds
at `build/workflow/20261008T210556.951208Z/`: 10,634,678 sound transactions and
timestamps, 53,449,666 total ordered phases, 53,449,620 accepted main/sub phases
and 40,714,240 RGB pixels retain the exact previously accepted result, with
zero differences/skips. ROM-free tests and Stage 1 verification pass at
`build/workflow/20261008T210805.188792Z/`. This is an oracle execution speed
improvement; it advances no additional emulation acceptance boundary.

### Independent audio/device head ownership (2026-10-09)

`oracle-joint-audio-full` extends the 600-frame native main/sub/sound contract
with native device heads and raw audio stream batches. The MAME observer now
records PCM/FM output immediately after each stream callback, before speaker
gains/resampling. Record times, first sample indexes, batch sizes and stereo
samples are assertions only: none determine a native batch or replace device
state. Its fresh capture is recorded at
`build/workflow/20261009T014251.102717Z/`.

`XBoardAudioDeadlines` uses the unmodified BSD-3-Clause YMFM OPM core from
MAME source commit `6b511c0a858b8de0499d38cf382f0ef666ebf2cc`; provenance and
license accompany the five source files. Its interface follows
`ymfm_mame.h`: mode writes enqueue synchronization and abort sound grants,
callbacks retain their posted expiry on late delivery, busy spans 64 chip
clocks, and YM timers schedule chip-clock durations. Timer B's free-running
phase comes from generated FM samples, including stream updates before CPU
register accesses. `sound.cpp` supplies the exact stream rule: skip time zero,
then generate through `floor(time*rate)+1`; skip already generated samples
when a CPU's private time leads a periodic global update. PCM clocks at
4 MHz/128, FM at 4 MHz/64. The existing verified PCM algorithm supplies raw
samples and fraction/register write-back continuously; there is no reseeding.

`XBoardDeviceDeadlines` owns reset-phased tile, first-scanline, VBLANK and
50 Hz sound-flush timers. The joint driver chooses its head from those timers,
board scanlines, ADC completion, main input queues, sub RESET, sound latch
publication, sound input queues and native YM callbacks. It determines outer
timer delivery from that head reaching the computed arbiter base. IRQ/RESET
callbacks produce the quantum boosts from `segaxbd.cpp`; captured requests
only assert their number, timestamps, duration and quantum. Sound device reads,
CPU grants/completions and all existing video assertions remain coupled.

The focused gate passes at `build/workflow/20261009T014901.251096Z/` in
113.07 seconds:

- **1,338,472/1,338,472 native head deadlines identical** after the initial
  joint context; outer delivery markers agree. The regular device owners
  execute 600 tile, 600 first-scanline, 600 VBLANK-begin, 599 VBLANK-end and
  503 sound-update callbacks over the cold capture.
- **628,796/628,796 FM and 314,376/314,376 PCM raw stereo samples identical**,
  with 423,102 FM and 6,216 PCM independently computed batch times/indexes.
  All expected batches are consumed; zero differences/skips. Natural coverage
  includes 171,085 nonzero FM and 113,153 nonzero PCM stereo samples, both
  signs and distinct left/right output.
- 1,159 mode callbacks (1,158 delivered late), 1,170 timer-A callbacks;
  these counts agree with the independent device inventory. The natural
  program does not run timer B. Within the owned window, 2,913 IRQ/RESET
  boosts are native; 45 earlier boot boosts retain their labelled provider.
- Native sound retains **10,634,678/10,634,678** bus records/timestamps,
  819,024 grants and 172 NMI/RETN. The shared CPU/video result retains
  20,532,168 records, 53,449,620 accepted phases and 40,714,240 RGB pixels,
  zero differences/skips. Total ordered phases, including padding, remain
  53,449,666 across 1,371,502 grant groups.

Original-program timer-A and timer-B diagnostics pass at
`build/workflow/20261009T015026.396810Z/`: respectively 44,767 and 44,759
checked sound transactions, two checked IRQ entries/RETI each, and retained
caller registers. Both compare 3,751 FM and 1,876 PCM stereo samples with
native stream indexes/batches. These diagnostic streams are silent; nonzero
synthesis evidence comes from the natural corpus. ROM-free checks cover
zero-time behavior, timer B phase, late mode delivery, status/busy and
periodic clocks. The latest ROM-free and Stage 1 passes are at
`build/workflow/20261009T014934.016528Z/`.

Acceptance remains bounded: the **33,030 pre-window boot grant groups** use
the existing supplied main/sub/frontier edges while the sound CPU runs from
reset with 45 external boot main writes. The live joint window supplies no
device head frontier or boost. One initial CPU context per main/sub CPU,
the initial partial sub ISR's 46 supplied phases, initial RAM/video state,
physical inputs and the final parked CPU edges retain their prior labels.
Unobserved chip/interrupt combinations (including CSM and additional
simultaneous/reset patterns), expanded gameplay, cold main/sub execution,
mixed/resampled speaker output and game-loop wiring remain unaccepted.
This closes observed deadline/raw-stream ownership, not whole-host startup
or audio-output integration. Old PCM and sound/video gates stay required;
no fixture, mutation, oracle flag or ROM/capture discipline is weakened.

Reproduction:

```sh
python3 tools/workflow.py run test-audio-deadlines
python3 tools/workflow.py run oracle-capture-joint-audio-full
python3 tools/workflow.py run oracle-capture-sound-audio-irq oracle-capture-sound-audio-timer-b
python3 tools/workflow.py run oracle-joint-audio-full oracle-sound-audio-irq oracle-sound-audio-timer-b
python3 tools/workflow.py run oracle-audio-deadline-mutations
python3 tools/workflow.py run required
```

The first complete suite at `build/workflow/20261009T015120.117584Z/`
failed (449.07 seconds) because shifting the tile deadline by one scanline
was not detected: both times coincided with an already due scanline timer,
so the composite head stayed unchanged. The gate now independently asserts
posted expiry and actual delivery time for each periodic owner and YM
mode/timer callback, consuming every matching read-only device trace record.
Those records never set a native deadline or callback time. The strengthened
periodic-owner gate passes at `build/workflow/20261009T020026.141210Z/` in
114.71 seconds, and the tile mutation is rejected. First-scanline and both
VBLANK owners have additional directed mutations for this same coincidence
risk. The final required run, including YM callback assertions and all
thirteen new mutations, **passes** at
`build/workflow/20261009T020341.060731Z/`: ROM-free tests 2.41 seconds,
Stage 1 0.38 seconds (unchanged RMS 903.949), and complete `oracle-test`
533.70 seconds. All existing sound/PCM/boot/video gates remain enabled and
pass. The thirteen new rejected mutations cover FM/PCM sample indexes,
YM timer periods, late mode timestamps, sound-update cadence, each of the
four periodic screen/tile owners, omitted PCM flushes, IRQ/RESET boosts and
timer B phase. Private copies are removed; rejection logs remain ignored at
`build/oracle/audio_mutation_*.log`. This final complete success supersedes
the earlier unrejected tile mutation. Task 2 is accepted for the joint window
with the exclusions above. CMake targets are updated; this host still has no
CMake executable, so validation uses the complete make workflow. No ROMs,
output audio, captures or generated logs are tracked.

## Mixed/resampled speaker and Stage 1 audio integration (2026-10-09)

The native raw stream contract now extends through the board speaker mixer.
`XBoardSpeaker` adapts MAME's BSD-3-Clause `resampler.cpp` LoFi implementation,
`sound.cpp` mixing/PCM conversion and `segaxbd.cpp` routes at source commit
`6b511c0a858b8de0499d38cf382f0ef666ebf2cc` (MAME 0.287). MAME defaults to
LoFi, not HQ: its polynomial interpolation table, source averaging when
reducing FM rate, history, initial zero padding and batch-start phase are
preserved. FM is 62,500 Hz at gain 0.15; PCM is 31,250 Hz at gain 0.35.
Both route channel 0 left/channel 1 right. Speaker output is 48 kHz stereo,
with float mixing followed by truncation toward zero and int16 saturation.
This is the speaker/recording stream before optional OS effects, with unity
host gain. LICENSES/MAME-Audio.txt retains attribution and license.

The isolated MAME observer records exact speaker float bits without altering
CPU/device state. The fresh 600-frame natural capture yields 482,881 stereo
frames in 503 batches: 347,379 nonzero channel samples, both signs and 170,000
asymmetric stereo frames, no clipping. All float bits and PCM conversion
match the independent resampler. `--require-speaker` in the native joint gate
feeds only native FM/PCM outputs into the same mixer and compares computed
speaker times, first indexes, counts and float bits: 482,881 frames/503 batches
identical, zero differences/skips. Existing 1,338,472 head assertions, raw
streams, CPU grants, main/sub phases and RGB checks stay intact.

The explicitly synthetic PCM test fills its local sample region with an
arithmetic square wave and programs all 16 channels. Left-only, right-only
and paired channels yield 50,881 frames/53 batches, 31,257 saturated channel
samples, 16,102 positive and 15,469 negative float samples, and 18,719
asymmetric stereo frames. These stimuli are device tests, not arcade data.
Ten private mutations reject: FM gain, PCM gain, channel swap, history delay,
initial phase, fixed-point step, interpolation coefficients, output index,
saturation endpoint and integer conversion scale. No ROM or capture is tracked.

`XBoardAudioRuntime` hosts the verified machine-cycle sound CPU, full YMFM
and native PCM independently of render calls. CPU bus accesses update the
appropriate native stream; synchronized mode/NMI acknowledgements end a grant;
posted timer deadlines advance separately. The real 315-5250 latch delivers
commands and acknowledges NMI. The Stage 1 producer API has an external FIFO
transport with no silent 256-command drop and no invented 32-clock delay.
This FIFO is not a claim that reconstructed gameplay commands have arcade
producer timing. Whole main/sub cold boot and remaining gameplay/exception
coverage remain the ordered whole-host work.

Speaker batches retain MAME's fixed 20 ms cadence. The playback FIFO begins
with 960 silent stereo frames (20 ms), allowing a game-tick render to stop at
its actual simulated time rather than executing into a future command tick.
Native stream histories are bounded after each mix. `ArcadeAudio` now uses
this path instead of the legacy per-T-state/125 Hz audio backend, FM channel
swap and reconstruction gains. ROM PCM regions now start ERASEFF, including
the unused tail, as in MAME. Legacy device fixtures remain available.

The SDL game loop advances the sound board even if audio-device creation
fails, recreates all chip/CPU/filter/playback state on restart, and waits for
queue space instead of discarding generated sound. The Stage 1 integration
check compares 800-frame renders against 1/13/127/659-frame chunks at identical
command times for four seconds, bit-for-bit, with RMS 1054.67. Reset after
prior music must reproduce fresh-board output. Both dummy-speaker and
unavailable-speaker 120-frame headless game runs finish successfully.

Reproduce with the mixed-speaker targets in WORKFLOW.md. Final required gate
results are recorded below.

The first final suite attempt passed speaker comparisons/mutations and the
existing owner checks through RESET, then exposed a harness-only failure:
adding YMFM to the game build creates `ymfm_opm.o` beside the vendor sources;
the timer-B mutation helper tried reading every directory entry as UTF-8.
The helper now copies only `.h`, `.cpp` and `.ipp` sources, retaining the full
timer-B phase mutation and its oracle rather than excluding that coverage.

Final required suite is green in `build/workflow/20261009T034358.283333Z/`:
`test` 0.35 s, `verify-stage1` 0.83 s (RMS 1054.67) and `oracle-test`
516.47 s, all exit 0. Timer-B phase rejection is included, as are all thirteen
audio/device and ten speaker mutations, existing video/CPU/PCM fixtures,
and their mutation gates. The game build passes in
`build/workflow/20261009T034214.187983Z/`; final dummy/unavailable audio
120-frame runs both exit 0. `git diff --check` is clean. The CMake targets
and CTest entries are synchronized, but CMake remains unavailable on this
host and was not run. The five YMFM source files remain byte-identical to
MAME; compiled objects, ROMs and all capture outputs remain ignored.


## Video timing owner and coupled directed acceptance (2026-10-09)

`XBoardVideoTiming` now owns the partial-render cursor, display transitions,
text-register write frontiers, line-261 latch dispatch and visible frame
completion outside the oracle harness. It translates the existing MAME
`screen_device::vpos/update_partial/first_scanline_tick/vblank_begin` and
`segaic16_video_device::set_display_enable/textram_w` policy: round to the
nearest 160 ns pixel, render the old display/register state before a write,
retain inclusive clips across grants, reset the cursor at line zero, and
complete the visible area at VBLANK. CPU text mirrors and directed fixture
writes use the same before-write endpoint. Graphics decoding, palette,
priority, effect pens, bank swaps and sprite feedback retain their existing
verified implementations. No ROM tables or captured data were added.

The natural 600-frame `joint_speaker_full` capture is reused, and the new
120-frame `joint_video_owned_directed` capture combines the existing video
fixture with continuous native sound, audio and speaker observation. Both
replays require native device heads and boosts; their screen/latch/pixel
streams are assertions. Directed video writes remain explicitly supplied
stimulus, applied at computed scanline callbacks. The old natural/directed
video and standalone pixel gates remain required.

| Coupled corpus | RGB pixels identical | Native latches | Native device head assertions |
| --- | ---: | ---: | ---: |
| 600, natural coin/start/flight | 40,714,240 | 568 | 1,338,472 |
| 120, directed with CPUs and sound active | 6,307,840 | 88 | 206,147 |

Total: 47,022,080 pixels and 656 latches, zero differences/skips. Directed
coverage retains 180 fixture events, 60 blank clips, 60 enabled clips with
pending globals, 180 clips in each row/column mode, 174,670 shadow and
176,198 hilight pixels. Its 130 bank swaps, 39 road controls and 88 hidden
sprite-bank writebacks also match. Native audio independently compares
62,727 raw PCM and 125,764 raw FM stereo samples and 96,001 mixed speaker
frames in 100 batches. Main/sub retain one initial context each, zero
supplied main phases and only the initial partial sub ISR's 46 supplied
read phases. Later CPU state and compositor inputs are never reseeded.

Reproduction (capture once, wait for completion, then reuse it):

```bash
python3 tools/workflow.py run oracle-capture-joint-video-owned-directed
python3 tools/workflow.py run oracle-joint-video-owned
python3 tools/workflow.py run oracle-joint-video-mutations
python3 tools/workflow.py run required -j2
```

The video mutation harness now runs the original ten cases with native
sound/device/speaker scheduling, and adds omitted text-register partial
updates, omitted line-zero cursor reset and omitted VBLANK completion.
All thirteen are rejected by actual MAME comparisons: latch-line by the
latch assertion, first-scanline by hidden sprite scaler-feedback bank equality,
text/display/VBLANK frontiers by clip/time assertions, and the remaining
cases by RGB equality. The original ten retain the latch/pixel rejection
criterion; only the new frame-cursor/completion cases additionally permit
bank comparison failure. Header overlays precede production include paths
so timing-owner mutations exercise the actual owner. Compile/runtime/coverage
failures do not count. The older `jointvideotextpartial` mutation also rejects
on the reset-focused corpus after updating its current compositor/sound
compile dependencies and shared test-header include path.

This completes video ownership for these observed post-boundary corpora.
It does not remove the supplied initial RAM/banks/latch/partial-render
boundary, pre-window boot scheduling and sound writes, physical inputs,
directed stimulus, or unobserved CPU/graphics/reset states. Cold-boot video
initialization still depends on tasks 4–5; whole-host acceptance and replacing
the Stage 1 reconstruction loop still depend on task 6. Stage 1 wiring is
unchanged by this video checkpoint.


Final validation: `python3 tools/workflow.py run test verify-stage1 all -j2`
passes; Stage 1 audio RMS is 1054.67. The complete
`python3 tools/workflow.py run oracle-test -j2` passes in 1031.62 seconds,
including all thirteen coupled video mutations, native sound/audio/speaker
mutations and the existing 300/300 pixel-identical standalone video frames.
Full logs are in `build/workflow/20261009T040014.106606Z/` and
`build/workflow/20261009T040106.835312Z/`. Routine-local race exclusions remain
unchanged; the coupled video gates skip zero pixels/latches. The focused
legacy text-frontier mutation also passes its rejection check. CMake test
registration is updated, but CMake is unavailable on this host.
`git diff --check` passes; captures, ROMs and probe binaries remain ignored.

## Expanded main/sub PC, branch and exception coverage (2026-10-09)

This closes task 4 for explicitly declared instruction fixtures. It does
not promote their independent initial contexts into a continuous whole-game
claim. The required `oracle-main-sub-coverage` target preserves every earlier
oracle and adds original-ROM instruction, nested-entry and longer gameplay
comparisons. Captures, runtime images, plans and successful census JSON live
only under ignored `build/oracle/`. The canonical plan uses the successful
natural speaker JSON census; the original passed-log importer remains available.

`plan_cpu_coverage.py` subtracts the successful natural speaker census from
`joint_main_schedule.lua` and `osubprogress_manifest.hpp`: 2,158 absent main
and 478 absent generated sub PCs. It also classifies runtime original ROM
Bcc/DBF instructions, generating true/false CCRs or loop counters 0/1 for
all 995 sites. The longer gameplay census additionally reaches 714 previously unported
main PCs. They translate through the existing instruction generator, with
one new memory ABCD form at `$CD26`. There are now 3,350 PC fixtures plus
1,990 branch fixtures, giving 5,340 cases. No instruction table or ROM image is committed. The coverage gate
rejects a stale membership plan, absent PC or missing branch direction.
Two conditional branches have their target equal to fall-through; their
outcomes are classified from the captured pre-CCR after exact phase/time
comparison, rather than incorrectly inferred from successor PC alone.

Each case executes a real RAM `MOVE.W #SR,SR; JMP.l original-PC` trampoline,
then the original ROM instruction. Operands, scratch RAM, USP/SSP and return
frames are explicit independent test stimuli. Only the first memory/context
of each independent case is supplied. The successor PC, all D/A registers,
SR, both stack banks, nanoseconds and every ordered program-bus address,
read/write value and byte mask must match the literal port. No returned
read values are seeded during the instruction; no mismatch is skipped.
The expanded capture passes **5,340/5,340 records and 12,735/12,735 phases,
zero differences/skips**. Its union with natural 600 reaches **8,500/8,500
main PCs and 1,136/1,136 generated sub PCs**. This union is not the unique-PC
count of any one continuous joint run.

A separate default 52-case capture verifies `$B836/$B83A/$B83E/$146D8/$14702`
with two CCRs, all six main DIVU sites with normal/overflow/zero stimuli as
applicable, negative/positive high-word zero-divide flags in both modes,
sub startup user RTE at `$040C/$063E` and main startup user RTE at `$14842`.
It passes **52/52 records, 218/218 phases, zero differences/skips**.
Supervisor RTE fixtures use a valid IPL7 return frame; user RTE faults
before reading that frame. Faults use the retained SSP and preserve USP.

The previously rejected exceptions now translate MAME's `divu_w_*_dd` and
`state_priviledge` microcode. DIVU overflow leaves Dn unchanged and sets N/V
with six internal clocks. Zero divide establishes the microcode CCR from
the dividend high word, spends eight internal clocks, stacks low return
PC/SR/high PC, fetches vector 5 and both handler words with the intervening
two clocks. User RTE spends four internal clocks, stacks the fault opcode
PC and original SR on SSP, and fetches vector 8 plus both handler words.
The new captures pin the stack-bank transitions, flags and clock positions.
The first 4,464-case capture remains required as `oracle-cpu-coverage-legacy`;
all 10,343 phases still match. The newer capture supplements its exact inputs
and does not discard them. `$CD26` memory ABCD translates MAME
`abcd_pais_paid_df`: two internal clocks, source predecrement/read,
destination predecrement/read, BCD flags, prefetch, IPL selection, then the
byte-lane destination write. Its changed clock is rejected by a separate
mutation. All added 714 cases are address-labelled generator outputs; no
runtime opcode interpreter or guessed callee timing is introduced.

These are exception entry checks; subsequent fault-handler execution and
unobserved fault/operand combinations remain outside this fixture contract.

`AB2_CPU_IRQ_FIXTURE` explicitly asserts IRQ2 for a real SR-writing RAM
instruction and asserts IRQ6 after the first VPA/vector lookup. This mode
only exists in the isolated probe; natural captures omit it. MAME's ordinary
microcode/pin logic supplies all execution, SR and stack changes. The nested
oracle initializes RAM/CPU once, then compares the second pre-state and RAM
against the computed first entry before continuing. **2/2 IRQ2 -> IRQ6
entries and 16/16 program/CPU-space phases match**, including VPA phase,
selection with the new handler SR and masked IRQ6 at the second selection.
No captured second context is installed. This proves the directed nested
entry, not natural board producer timing for a complete nested handler.

Nine temporary source overlays change divide return PC/clock/overflow
flags, main/sub privilege vectors, the newly covered `$9626` ADDQ operand,
`$19E2` DBF decrement, the new memory ABCD internal clock, and the nested entry mask. Every mutation fails an
actual MAME bus/state/selection comparison. Original source is never
modified by the mutation runner. Make and CMake register the new oracles;
Make's complete `oracle-test` requires their coverage/mutation/long replay.

The physical-input gameplay extension lasts 1,200 frames and retains main
and sub state after the same frame-32 boundary as natural 600. It requires
main closure, sub continuity, RESET/TRAP, native RGB frontiers and computed
sound latch handshakes. Its device deadline and Z80/audio execution ownership
remain those of this supplied-deadline gate; the earlier native speaker and
coupled video gates remain required. It is not a replacement for them.
The original probe prints low-64 attoseconds, wrapping at
18.446744073709551616 seconds. Its trace reader now reconstructs epochs from
captured source chronology with 128-bit times; it never uses native predicted
times to choose an epoch. Absolute retained CPU/bus nanoseconds are still
compared. Compile-time checks cover forward wrap, a lagging CPU just before
wrap and time zero. Adjacent source clocks must differ by less than half
an epoch; this is an explicit capture-format bound.

Unverified: every operand/CCR/memory combination, continuous gameplay
execution of all fixture PCs/branch directions, all later stages/credits,
all reset sequences, exception handler continuation, cold-boot CPU/video
initialization, the initial partial sub ISR's supplied phases and final
parked continuations. Task 5 must remove or bound these fixture edges before
whole-host acceptance. Native Stage 1 remains the game loop.


The fresh expanded 1,200-frame gate now passes **42,142,844/42,142,844
instruction/IRQ records and 109,932,641/109,932,641 native phases**, zero
differences/skips. All 109,932,687 captured bus phases are delivered in
computed CPU order across 2,795,644 exact grant groups; only the explicitly
bounded initial 46 sub phases remain supplied. Main supplies **zero phases,
zero writes and one initial context**; sub also has one initial context and
zero later replacements. All **5,949 main and 1,168 sub IRQ entries,
acknowledgements and returns** retain their callers and stack banks. Main
TRAP #12 and RESET-handler RTE pass. This window reaches **6,384/8,500 main
and 819/1,136 generated sub PCs** continuously; the independent fixtures
cover the remainder. It compares 15,985,121 shared reads without seeds,
computes 2,402,533 shared writes with zero supplied writes, and checks
**83,722,240 exact RGB pixels, 1,168 latches and 22 disabled clips**.
There are 778 synchronized sound command deliveries and 3,057 computed latch
reads/NMI acknowledgements; 1,582,571 YM/PCM transactions retain supplied
Z80 clocks in this longer gate. The earlier native sound/audio/speaker gates
remain required. The strict first 1,200-frame attempt exposed 714 absent
main PCs and was rejected; fresh snapshots and literal translations close
them instead of relaxing closure or installing replacement contexts.

Focused acceptance logs: `build/workflow/20261009T050338.511045Z` (5,340-case
coverage, coverage gate, nine mutations, ROM-free tests and Stage 1),
`build/workflow/20261009T050530.405373Z` (legacy 4,464 cases and generated
manifest checks), `build/workflow/20261009T051041.918418Z` (fresh nested
capture replay), and `build/workflow/20261009T051449.306245Z` (strict longer
joint replay, 205.11 s). All report zero differences/skips where applicable.

Final required validation: `python3 tools/workflow.py -j 2 run required`
passes `test` (0.20 s), `verify-stage1` (0.81 s, audio RMS 1054.67) and the
complete `oracle-test` (1,182.62 s). Full logs are in
`build/workflow/20261009T051450.457435Z`. The complete suite includes all
previous video/sound/deadline/speaker gates, original routine fixtures,
the legacy and expanded CPU captures, all nine new mutations and the strict
long joint replay. Earlier standalone sprite/scenery race exclusions remain
declared and unchanged; every new CPU fixture and the new joint gate have
zero differences/skips. `git diff --check` passes. No ROM images, captures,
plan data or probe binaries are tracked, and no commit was made. Task 5
(fixture edges and cold boot) is next; whole-game acceptance remains open.

## Cold native startup and retained capture boundaries (2026-10-09)

Task 5 now has a continuous cold gate instead of importing the frame-32
CPU/RAM/video boundary. `AB2ARB10` starts before either 68000 executes and
adds main reset-entry snapshots to the existing instruction/exception
assertions. `AB2PIN03` carries initial physical buttons/axes and the three
other IO1 input ports, including the boot-read DIP switches. Those are
external physical inputs, not read-value seeds. Fresh NVRAM/config and the
same ROM/gameplay input script remain required.

The additional main graph is 352 address-labelled instructions in
`omainbootschedule.inc`, generated from runtime ROM images and the checked-in
PC-only list `main_boot_pcs.txt`. It includes checksum/RAM-clear work and boot
I/O/video producers. PEA absolute-long now has its literal extension,
stack-write and prefetch phases. The five separate startup exception sites
are TRAP #10 at `$13820/$13884`, TRAP #12 at `$144F4`, and RTE at
`$144EA/$14828`. Main reset uses the same MAME `state_reset_df` vector/prefetch
microcode already verified for sub reset. Every added PC must occur in the
cold gate; the previous 8,500-main/1,136-sub independent coverage contract
and its fixtures remain unchanged.

Power-on CPU registers, stack banks, clocks, work/shared/video RAM, math
chips and hidden sprite/road banks are native initial values. Captured
initial values only assert them. There are zero supplied CPU contexts,
execution phases, producer writes, initial tile latches or boot sound host
writes. Scheduler clocks start at zero; main/sub and held-reset sound consume
native grants. Captured enabled masks are assertions, not grant controls in
cold mode. A suspend without clock consumption is outside this corpus and
fails the cold gate. All device heads and IRQ/RESET boosts are computed from
time zero. The first tile latch is 37 lines (2,368,000 ns) after power-on,
before the board's first line-0 callback at 2,432,000 ns; `XBoardVideoTiming`
owns that initialization as well as later scanline latches/render frontiers.

Calling `machine:exit()` from a CPU bus tap is unsuitable for checking final
continuations: MAME's `running_machine::schedule_exit()` calls
`eat_all_cycles()`, changing the remaining microcode clocks. The cold
capture therefore requests exit from the next frame device callback, with
no CPU executing, and keeps its taps active until the machine-stop notifier.
It closes the last grant there. The replay also delivers the final device
callback batch after the last CPU grant; those timer, bank and RGB assertions
are checked rather than dropped.

The focused capture requests 120 frames and stops at the natural frame-121
callback. It matches 4,279,315 instruction/IRQ records, 11,514,112 phases,
8,673,280 RGB pixels and 241,279 device heads, zero differences/skips.
The long capture requests 602 frames and stops at frame 603:

| Assertion | Identical / total |
| --- | ---: |
| CPU grant groups and native device heads | 1,378,296 / 1,378,296 |
| Main/sub instruction/IRQ records | 21,722,588 / 21,722,588 |
| Ordered main/sub bus phases | 56,831,338 / 56,831,338 |
| Native Z80 transactions and timestamps | 10,686,524 / 10,686,524 |
| Joint RGB pixels | 43,223,040 / 43,223,040 |
| Tile latches / physical hidden sprite writebacks | 603 / 603 each |
| Mixed speaker stereo frames / batches | 484,801 / 484,801; 505 / 505 |

The run includes 2,939 native I/O writes on both chips, 2,517 compare/timer
writes, 1,185 bank swaps, 569 road-control writes, 615 partial updates,
3,113 native sound host writes, and 175 NMI/RETN pairs. Continuous PCM/FM
matches 315,835/631,944 raw stereo samples and 6,236/424,660 batch
timestamps/indexes. Six computed main RESET deliveries cancel sub work;
seven sub vector/prefetch entries include initial power-on. No retained RAM
is cleared merely because the CPU resets.

`check_joint_cold_boundaries.py` independently matches the retained
`joint_speaker_full` capture's 46 initial partial-ISR phases and both CPUs'
last observed grant phases against the passed cold stream. Each CPU then
reaches a later instruction/IRQ snapshot without a reset replacement. The
old two parked continuations are inside the new native window. This is an
overlap/continuation assertion, not an independent state fixture.

The new finite machine-stop boundary remains explicit: both CPUs can have
pending instruction debt, and one new sub ISR is active after its verified
entry. The long run verifies 603 entries and 602 exact retained RTE
restorations; the last ISR's later completion is outside observation. This
supplies no future CPU completion or replacement state. Legacy joint
fixtures still require their original fully closed ISR windows. Every added
PC is exercised, but alternate boot checksums/DIP settings, other operands
and branch outcomes, hard-reset combinations, unobserved suspension states,
future execution after machine stop and a complete credit remain unverified.
Task 6 must establish its declared whole-host coverage and wire the verified
host into the game. Stage 1 remains the existing reconstruction loop.

Eight private mutations reject by actual MAME comparisons: nonzero power-on
RAM, nonzero hidden sprite bank, reset internal clocks, startup PEA stack
value, startup SR mask, TRAP #10 vector, omitted first tile latch and changed
physical DIP values. Build failures and coverage-only failures do not count.
All old boot/shared, CPU, audio and video fixtures/mutations remain required.
ROM images, captures, temporary mutation sources and logs remain ignored.

```sh
python3 tools/workflow.py run oracle-capture-joint-cold oracle-joint-cold
python3 tools/workflow.py run oracle-capture-joint-cold-full oracle-joint-cold-boundaries
python3 tools/workflow.py run oracle-joint-cold-mutations
python3 tools/workflow.py run required -j4
```

Capture completion must precede comparisons. The cold positive, full overlap
and eight mutations are part of `oracle-test`; CMake registers the positive
checks, overlap and mutation script. See the validation continuation below.

Validation: `python3 tools/workflow.py run required -j4` passed ROM-free
`test`, `verify-stage1` and the complete `oracle-test`, including the two cold
gates, overlap and all eight new mutations. Logs are in
`build/workflow/20261009T060235.220987Z/`; `oracle-test` took 798.40 s. Existing
routine-local race exclusions remain unchanged; cold/native windows have
zero differences/skips. The final scheduler assertion refinement (native
zero clocks and fixed consumed-grant masks rather than imported initial
clocks/enabled flags) was recompiled and passed the full cold/overlap gate
again in `build/workflow/20261009T061630.470418Z/` (160.27 s), with identical
counts. The game build, 120-frame dummy video/audio smoke run and
`git diff --check` pass. CMake registrations remain untested because CMake
is unavailable here. No ROM/capture files are tracked and no game-loop
integration is promoted by this checkpoint.

## Autonomous whole-host acceptance and game integration (2026-10-09)

Task 6 accepts the complete joint owner contract within the declared finite
cold and expanded physical-input captures. This is separate from universal
full-credit/operand/reset coverage. `XBoardHost` is now the application host,
through `XBoardGame`; the earlier `OEngine` reconstruction is retained only
in historical/regression tools and the original Stage 1 test.

### Autonomous dispatch and owners

The old joint replay used captured snapshots to initiate each instruction,
even when its retained state and all computed phases matched. Cold replay
now initiates successors from native PC/state and independently latched
IRQ/RESET signals. The reusable `src/main/engine/xboard_host.hpp` contains
that dispatch, private instruction debt and common board scheduling. Its
API receives ROM regions, video hardware, a sound owner and physical inputs;
it has no capture, instruction list, state-seed or completion-time provider.
The ports remain the literal address-labelled `OMainIrqService` and
`OSubIrq`/`OSubProgress` translations, including boot, TRAP/RTE and RESET.
An unknown instruction fails explicitly instead of entering a reconstruction.

One `MainBus::Memory` instance owns both CPUs' shared RAM and hidden banks.
The native arbiter selects main, sub and sound grants in board order, using
independently computed periodic, scanline, ADC, IRQ-line, sound synchronization
and YM/audio deadlines. Main IRQ polls and sub VBLANK polls select entries;
retained PC/SR/USP/SSP/registers and E-clock origins supply their context.
The last executing instruction PC supplies exception bus attribution, while
the retained successor supplies its return PC. The main RESET opcode owns
sub cancellation, queue draining, clock rephasing and vector/prefetch restart.
No later snapshot is used to repair a state or choose an exception sequence.

`XBoardSoundBoard` executes the original Z80 in those same arbiter grants,
with main I/O RESET and the actual compare/timer command latch. It owns
synchronized NMI delivery/acknowledgement, YM timers, FM/PCM stream batches
and fixed 20 ms speaker updates. The test runs this production sound owner,
not a separate playback scheduler. Native video timing owns initial and
line-261 tile latches, display/text-write partial frontiers and VBLANK frame
completion. The compositor consumes the current native RAM/banks and retains
sprite scaler feedback in the real hidden bank. The application presents
those already completed clips without invoking an additional render pass.

### Passive whole-host MAME comparison

`tests/oracle_whole_host.cpp` reads the task-5 cold capture format as assertions.
Expected snapshots are consumed only when the host emits its first bus phase;
they never initiate its job. Scheduler starts, targets, budgets, completions,
quantum requests and device heads are assertions on `host.step()`, whose
selection remains native. Initial CPU/RAM/math/video-bank/sound-RAM state,
physical bus data, stack banks, bank/control/frame bytes, render/latch clips,
pixel indices/priorities/RGB, Z80 transactions and host command/reset writes,
per-owner callbacks, raw stream sample/index/batch data and mixed float bits
are all compared. Source trace time decoding uses source chronology; it does
not unwrap timestamps against the native scheduler. External physical DIP,
button and ADC inputs are the only capture values delivered to the board.

All positive corpora have zero supplied execution phases, state replacements,
initial latches, CPU contexts or read seeds; zero differences/skips:

| Corpus | Scheduler groups/heads | CPU snapshots | Main/sub phases | RGB pixels | Mixed stereo frames/batches |
| --- | ---: | ---: | ---: | ---: | ---: |
| Cold 121 frames | 241,279 | 4,279,315 | 11,514,112 | 8,673,280 | 96,961 / 101 |
| Cold 603 frames | 1,378,296 | 21,722,588 | 56,831,338 | 43,223,040 | 484,801 / 505 |
| Expanded 781 frames | 1,801,247 | 28,128,435 | 73,593,282 | 55,982,080 | 627,841 / 654 |

The passive native-entry census observes 2,929 main/693 sub instruction PCs
in the focused window, 6,046/706 in the full window and 6,247/706 in the
expanded window. Main IRQ2/4/6 entry counts are 364/119/3, 2,322/601/18 and
3,046/779/23 respectively; sub IRQ entries are 121, 603 and 781. Those are
whole-host observations, not all-port coverage claims. The larger independent
8,500-main/1,136-sub instruction and 995 branch-direction fixtures remain
required, and their other combinations remain unverified in the whole host.

The expanded capture uses only the physical gameplay script's coin/start,
ADC endpoints and all four weapon-button combinations. It extends gameplay
past both previous cold/retained terminal boundaries. Its 4,671,054 native
CPU grants, 6,814 owner callbacks, 2,891 bank/control/frame assertions and
781 tile latches match. It also matches 13,765,117 Z80 transactions and
4,195 native sound-host writes, 818,431 FM samples/517,813 batches,
408,751 PCM samples/7,509 batches, six synchronized sub RESET deliveries
and seven native reset sequences including power-on. Capture logs count the
sound-host writes together with Z80 transactions; the table/report separates
these owners. No total is inferred from an incomplete capture.

### Directed owner rejection

`check_whole_host_mutations.py` builds isolated private overlays and requires
an actual passive MAME comparison failure. Compile failures, coverage-only
failures and unported-instruction exceptions are not successful rejections.
All fifteen mutations reject: main and sub reset clocks; successor bus-PC
dispatch; main and sub IRQ selection; DIP inputs; initial hidden bank;
initial renderer latch; arbiter quantum; periodic device deadline; Z80 bus
clock; YM and PCM writes; mixed speaker gain; and native publication re-arm
producer value. The speaker gain mutation runs against the 603-frame corpus:
the 121-frame cold speaker stream is entirely silent, so changing gain there
cannot establish rejection. The full stream has 351,511 nonzero channel
samples, with peak magnitude 0.1854326874. All earlier startup, CPU-coverage,
branch, video, audio, speaker and independent routine mutants remain required.

### Live game and integration

`XBoardGame` creates a fresh board without Stage 1 setup writes or native
object/HUD/audio producers. It advances until the next completed physical
frame and drains real speaker batches. The SDL loop uses the 16.768 ms
262-line board frame period instead of an assumed 60 Hz clock. Defaults are
the MAME `aburner2` input definition's FF/FF/DD switches (upright 1, throttle,
three lives, normal difficulty, demo sounds). Arrow keys feed the documented
raw ADC ranges (X 20..E0, reversed Y 40..C0); Z/X are active-low weapon pins,
Space/Left Shift feed throttle FF/00, 5 feeds Coin 1, Enter feeds Start 1,
F5 creates a fresh cold board and Escape exits. Inputs alter no gameplay RAM.

`whole_host_integration` executes 781 physical frames with coin/start,
endpoint/throttle and weapon combinations, checks nonzero native screen/audio
and retained CPU/reset activity, then destroys the active board and compares
a restarted board against an independent fresh boot. RAM, both hidden banks,
RGB and speaker samples match. Measured PCM output RMS is 742.315; that is a
local corpus observation, not a tuning constant. Worker destruction cancels
parked jobs before their ports and buses are destroyed. No old queues, chip,
renderer or private instruction-debt state is imported into the new board.
The original 7,200-tick reconstruction Stage 1 assertions and standalone
irregular-chunk sound checks remain in `verify-stage1`; this target also
requires the new native integration check. The application headless test
uses SDL dummy video/audio and completes 120 physical frames from power-on.

### Acceptance limits and evidence

The finite MAME machine-stop boundary can still cut active native instructions
or an ISR. Their observed phases are accepted; future completion is outside
that finite comparison. The host itself can keep executing without a capture.
Full-credit stage progression/refueling, all possible operands and branch
outcomes, other DIP/cabinet settings, arbitrary hardware-wide warm reset,
watchdog expiry, non-consuming CPU suspension and unobserved instructions
remain Phase 3e work. F5 recreation proves fresh cold restart, not arbitrary
MAME warm-reset semantics. Independent directed instruction fixtures do not
turn their unobserved whole-game combinations into accepted gameplay paths.
Real-time playback performance is not implied by exact emulated timing;
cooperative CPU handoffs remain a host performance cost.

Focused/full/expanded comparisons, fifteen owner mutations and every older
oracle are registered in Make; CMake guards oracle tests with `ORACLE_TESTS`
and native game integration with `STAGE1_ROM_TESTS`. ROMs, captures, images,
mutation binaries and full logs remain gitignored local artifacts. The
expanded capture completed in `build/workflow/20261009T062557.099604Z/`;
initial focused/full/expanded acceptance logs are respectively
`20261009T063024.969020Z`, `20261009T063104.597147Z` and
`20261009T063459.423240Z`. Native adapter acceptance is
`20261009T063500.548115Z`; the nonzero-speaker/publication mutations are
`20261009T063830.173490Z`. Final required-suite and headless validation are
recorded below after completion. CMake is unavailable on this host.

Final validation: `python3 tools/workflow.py run required -j4` passes in
`build/workflow/20261009T064134.111408Z/`: ROM-free `test` 0.20 s,
`verify-stage1` 41.67 s (both native whole-host and legacy reconstruction
integration), and complete `oracle-test` 906.64 s. All three autonomous-host
positive corpora and all fifteen owner mutations pass in this final suite.
Older isolated fixtures retain their documented race exclusions (including
two sprite passes and 263 scenery records); these are not skips in any new
whole-host corpus. The live game builds, and
`verify-whole-host-headless` passes in `20261009T064203.914065Z` (7.77 s,
SDL dummy video/audio, 120 frames). `git diff --check` passes. CMake test
registrations are source-reviewed but not executed because CMake is absent.
No ROM/capture data is tracked or committed.


## Reported live stops and CPU handoff cost (2026-10-09)

The reported decimal PCs 23786 and 50422 are `$5CEA` and `$C4F6`.
They were missing caller cases. The literal
recovery schedule now covers `$5CEA/$5CEE/$5CF4`, the `$C4F6..$C5A6`
continuations, and additional paths exposed by extended neutral attract play:
`$5D72/$5DE6`, `$7A8E..$7AD6`, `$1583A/$1583C`, `$AE50..$AE6C`,
`$E0EE/$E0F2`, the remaining `$A90E..$A9C0` branch-table continuations,
and the normal `$A6D6..$A712` branch table plus its `$A728..$A762/$A88C`
selections, and the `$68DA..$69EE` script-dispatch table with
`$6B00..$6B86` settings writers. The special `$A716..$A724` CHK sites are
not part of this set. Script object selectors `$6B88..$6CDC` and their
`$6D24/$6F5A..$7122` formation/allocation continuations are also covered.
The `$786C..$79B6` formation update and motion helpers are also covered.
Horizontal-motion/sprite-selection helpers `$7EAC..$7F3A` are also covered.
The `$A5F0` signed-motion negate branch is also covered.
Secondary-object allocation `$AD9C..$AE16`, initialization/update
`$8D44..$9044`, and release `$9556..$9574` are also covered.
The `$14FB6` hit-response clamp, `$CBD2/$CBD8/$CBDE` HUD formatting
continuations and `$068E..$096C` script setup/initialization/update were
subsequently exposed and ported as literal instruction sites. The
`$060E..$068C` script allocator/selectors are also covered.
The accepted recovery checkpoint covers 3,165 new instruction addresses, kept
in a separate declared manifest
rather than reducing or redefining the retained 8,500-PC acceptance corpus.
The generator's added `ST d16(An)` form follows MAME's `st_das_df`: extension
prefetch, byte read, program prefetch, IPL selection and byte write; CCR is
unchanged. Added register TAS and displacement memory ASL follow MAME
`tas_ds_df` and `asl_das_df`. TAS fixtures include old zero/negative bytes
and preserved upper register bytes; ASL fixtures cover sign-change overflow,
carry/X and zero result with explicit independent RAM operands.
The `$7EFA` indexed word-table fixture uses an even D1 offset: `$7ED8`
clears it and `$7EF0` advances it by two. The `$0926` longword table uses a multiple-of-four D1 offset, as produced
by the two preceding doubles at `$0922/$0924`. Generic odd offsets instead enter
MAME's address-error exception; those exception contexts remain outside this
normal-instruction acceptance set. The ROM remains an external runtime input.

`oracle-capture-recovery` executes independent original-ROM instructions in
MAME. It exercises all 32 CCR patterns at conditional branches, both DBF counter outcomes,
and nonzero data-register operands at the other sites. `oracle-recovery`
compares bus address/data/mask/time, post-registers, stack banks, SR, next PC
and completion time. All 14,352 fixtures and 33,540 bus phases match, with zero
differences/skips. The census requires every declared PC and both outcomes of
all 266 branches. `oracle-recovery-mutations` rejects a wrong `$5CEA` data
write and a wrong `$C504` ST result through the actual bus comparison, plus
wrong TAS output and ASL carry/X through the post-state comparison.
Captures and flat ROM images remain ignored under `build/oracle/`.

Later observed paths add stage-script transitions, palette/road setup, road
allocation/placement, sprite positioning, collision/effect continuations,
object motion and hit updates, and the `$D358/$D3E8` game-state transitions.
The PC-only manifest lists their exact bounded code ranges and excluded inline
data. Immediate signed MULS follows MAME `muls_w_imm16_dd_df`: word prefetch,
signed low-word product, Booth-transition-dependent internal clocks and final
NZVC with preserved X. Directed fixtures cover zero, both signs and signed word
limits; an unsigned-popcount clock mutation is rejected. Additional fixtures
exercise nonzero register shift counts, byte rotate carry/sign/high-byte
preservation and normal/overflow immediate DIVU. Indexed longword tables at
`$17B0/$53B4` use multiples of four, as produced by the preceding doubles.
The accepted checkpoint and all six recovery mutations pass in
`build/workflow/20261009T112127.810934Z/`, using the isolated ignored
`build/oracle/cpu_recovery_native.bin`. `RECOVERY_CAPTURE` selects a private
capture path without changing the default fixture path. This avoids concurrent
writers corrupting replay records. Ongoing work can extend the declared
manifest beyond this checkpoint; new addresses require their own MAME proof.

The CPU continuation now runs consecutive microsteps within the same granted
budget without returning to the host thread at every checkpoint. It yields
at the existing bus-debt, VPA wait and synchronized-abort boundaries. The live
host prepares each instruction without an initial worker round trip; its first
grant starts it. RESET can cancel such a prepared instruction without running
it. Sleeping waiters register and recheck the turn using sequentially
consistent atomics, allowing hot handoffs to omit unnecessary kernel wakeups.
The original release/acquire publication and instruction-tail debt contract
remain intact. Regression checks include deferred cancellation and bus effects,
retained RMW, overrun debt, VPA, aborts, idle sleep and error propagation.

An isolated 120-frame board benchmark, with SDL pacing excluded, measured
baseline 6.77/6.80 seconds and updated 3.45/3.71 seconds after the acceptance
suite finished: about 1.8–2.0 times faster. With the suite running, the same
comparison was 12.45/11.43 versus 7.30/6.01 seconds. This is not real-time
acceptance: 120 physical frames represent about 2.012 seconds. The SDL loop
additionally caps each catch-up batch at two frames and bounds wall-time backlog, so slower execution cannot
monopolize input polling and presentation. Every emulated frame still executes
all of its CPU and device work.

The rebuilt Apple Silicon host benchmark subsequently measures 120 unpaced
frames in 1.32 seconds (`build/game_speed_latest.txt`), versus the original
6.77/6.80 seconds. This initial-window throughput exceeds the corresponding
2.012-second arcade interval, but is not a full-credit peak-load guarantee.
The native 2,400-frame neutral attract smoke passes in
`build/workflow/20261009T112206.823409Z/` and is retained in `verify-stage1`.
It proves extended native execution without an unsupported-PC stop; it is
not a 2,400-frame independent pixel/audio oracle. Final `test`, `verify-stage1`
(including both CPU backends and retained integration/session fixtures) and
`oracle-whole-host` pass in `build/workflow/20261009T112457.520018Z/`.
The original full oracle suite also passed earlier in this session at
`build/workflow/20261009T091720.040685Z/`; later recovery additions have the
independent instruction proof above. CMake registrations remain unexecuted
because CMake is unavailable. No ROM/capture data is tracked.

These independent instruction checks extend finite acceptance. They do not
prove an arbitrary credit, all later stages, or every operand/input trajectory.
Use the retained whole-host pixel/audio/scheduler corpus and the full oracle
suite in addition to the recovery fixtures. Full-game coverage and further
CPU synchronization optimization remain necessary.

## Application extension boundary (2026-10-09)

The application now uses `GameSession` around the unchanged translated-game
adapter. `GameControls` supplies normalized device axes and named buttons;
the mapping preserves the native application's physical ADC endpoints and
active-low pins. Keyboard precedence is retained. No flight angles, object
positions, gameplay timers or shared RAM are supplied by the session.

Extensions receive completed read-only RGB frames, can process outgoing PCM
without altering chip state, and receive an explicit restart notification.
The session owns pause and pending output audio; pause performs no board
advance and restart discards that output and recreates the board. The SDL
application exposes P to pause/resume, pauses the playback device, clears
wall-time backlog and continues presenting the frozen frame while paused.
F5 clears playback and starts a fresh unpaused board. No translated routine
or CPU timing is changed by this boundary. See `ENHANCEMENT_ARCHITECTURE.md`
for lifetime contracts and the future typed subsystem interfaces needed for
content, asset replacement, widescreen, music routing and gameplay policies.

ROM-free controls checks pass for neutral/endpoints, saturation, all 16 button
combinations and nonfinite rejection. The new integration compares `GameSession`
against `XBoardGame` for the 781-frame endpoint/weapon fixture: exact RGB,
frame/time frontiers and identity audio match. It pauses/resumes mid-game,
mutes a nonzero audio section through the extension then resumes exact native
audio, and verifies restart notifications and fresh-board RGB/audio equality.
Both checks are included in the existing `test`/`verify-stage1` targets.
Focused validation passes in `build/workflow/20261009T092341.350121Z/`,
including the 120-frame headless application launch. The broader ROM-free and
Stage 1/native/session integration also pass in
`build/workflow/20261009T092409.518783Z/`. CMake registrations have not run
because CMake is unavailable. These checks establish an application boundary,
not additional full-credit MAME coverage or completion of Phase 5 features.
After keeping paused presentation active, `test` and the headless application
pass again in `build/workflow/20261009T092902.667511Z/`; `git diff --check`
passes. No new ROM port was added by this change, no full oracle-suite result
is claimed here, and no ROM/capture data is included.

## Direct Three.js scene prototype (2026-10-09)

`tools/threejs_bridge.cpp` runs the existing `XBoardGame` and observes native
completed partial clips. It reads published sprite/road banks, tile/text RAM,
latched tile registers and resolved palette pens at the rendering frontier.
`XBoardGame::video_registers()` is a read-only snapshot getter. No CPU/device
scheduling, producer writes or gameplay logic is replaced. Native internal
rendering remains necessary for scaler writeback feedback.

`ThreeScene::sprites` converts the published descriptors and runtime ROM into
indexed source images and screen-space quad transforms. It follows the existing
scaler's bank/address wrap, signed pitch, vertical address accumulator, marker,
nibble-order and direction rules, without modifying the published bank.
Three.js performs horizontal source sampling and writes per-object indexed
words into its sprite render target. Indexed road/tile/HUD scanline strips
are batched geometry in a separate render target. The GPU composition pass
implements the native strict priority comparison and palette effects selection;
it does not display a C++ composite framebuffer texture. Original ground/tile
sampling remains native in this first prototype. World coordinates, 3D models,
true widescreen and a standalone Wasm execution host are not claimed.

The local Python transport binds 127.0.0.1:8765, reads the user's ROMs in the
native process, and serves only explicit frontend/library files. Sources contain
no original assets. Runtime images, PCM, screenshots and logs remain local;
node_modules is ignored and Three.js is pinned to 0.186.1 in the npm lockfile.
Browser audio is optional and not a new audio-oracle acceptance claim.

`test-threejs-scene` passes 80 synthetic exact comparisons with `HWSprites`,
covering zoom, both directions, ROM nibble order, signed pitch, address wrapping
and clip bounds. A private row-step mutation is rejected by that native pixel
comparison; it is included in `test`. Focused evidence is in
`build/workflow/20261009T095114.607737Z/`. `test` and `verify-stage1` pass in
`build/workflow/20261009T094907.282157Z/`, including native/session and retained
reconstruction integration. JS syntax and Python compile checks pass. CMake is
unavailable; its new ROM-free test registration is not executed.

Live WebGL2 browser testing showed native RGB checksum MATCH during observed
attract/gameplay frames, including frame 1079, and no shader error/warning.
This is a rendered-frame FNV-1a diagnostic, not a full MAME coverage claim.
Extended play reached unported main PC $CBD8 (52184) and stopped explicitly.
Restart recreates the bridge process so the known failure is recoverable.
Simulation throughput is below arcade rate under the observed load. Full-credit
coverage, broader renderer comparisons and browser performance remain open.

Final browser recovery check recreated the stopped native process, executed
Boot to flight, then exercised resume, right-stick input, pause and object
outlines. The run from frame 600 through 730 reports 131 rendered-frame RGB
checksum checks and zero mismatches. The final view remains paused at frame
730 with 92 sprite meshes and native checksum MATCH. The local preview is
`build/threejs-prototype.jpg` (ignored); no capture/art is tracked. Boot to
flight now pauses at its completed scene so the user can resume explicitly.
Final ROM-free checks (including the mutation) and the native headless launch
pass in `build/workflow/20261009T095437.152637Z/`; `git diff --check` passes.

## Browser hit-response and object-motion stops (2026-10-09)

The browser stopped after frame 1340 at decimal PC 29402 (`$72DA`).
The literal recovery manifest adds 24 instruction sites in `$72DA..$7330`,
including the `$7E5A` call, ROM-table lookup, multiplier operands, velocity
writes, zero-velocity branch and return to `$5AF2`. Extended neutral gameplay
then exposed decimal PC 87008 (`$153E0`) after frame 1850. Its connected
`$153E0..$1574A` motion lifecycle and `$14E14..$14E1C` helper add another
223 missing sites. These are generated instruction translations from the
external ROM; existing schedules and fixtures remain retained.
The next extended run reached the collision-success branch `$15178` in the
existing `$150C0` helper. Four additional sites `$15178..$15184` set D6 and
write the original collision position/status. Its mutation clears D6 instead
of setting it, requiring a fixture mismatch.

The current recovery union contains 3,101 PCs and 259 conditional branches.
All 14,014 independent original-ROM fixtures match, with 32,726 bus phases,
zero differences and zero skips. Both outcomes occur at every declared branch.
Evidence is in `build/workflow/20261009T111410.777576Z/`. Separate mutations
target the `$7330` return and `$153E0` entry prefetch in addition to the retained
recovery mutations. This extends finite instruction coverage; it does not
establish full-credit gameplay acceptance.

The local server now retains physical bridge commands in memory and writes
ignored `build/threejs_failure_replay.txt` only when execution stops. The
error identifies the ROM PC and last completed response frame, excluding
diagnostics from older process instances. No graphics, audio or ROM data are
saved in that replay. Stop, broken-pipe and stale-log diagnostics were checked
with isolated fake process streams.

The collision extension matches 14,022 fixtures in
`build/oracle/browser_recovery.bin`; all eight then-declared recovery mutations
reject. Its aggregate coverage command detected a concurrent 60-PC manifest
expansion, rather than a CPU difference. The independent concurrent
`cpu_recovery_native.bin` capture covers that larger 3,165-PC union: 14,352
fixtures, 266 branches and 33,540 phases match. No declared PCs were removed.

The next live stop occurs after frame 1937 at `$19CC0` (105664), in the
stage-indicator selection/update path. Its 53 missing instruction sites through
`$19D86` are generated, excluding the data starting at `$19D90`. An incremental
MAME plan selects these sites from the same independent ROM-classified recovery
plan. All 286 fixtures and 612 phases match, and the `$19CC6` wrong branch-target
mutation rejects. Combined coverage from `cpu_recovery_native.bin` and
`browser_indicator.bin` includes all 3,218 declared PCs and both outcomes of
272 branches (14,638 fixtures). The coverage checker now accepts multiple
captures while retaining exact PC/branch census and both-outcome requirements;
negative checks reject an omitted PC and an omitted branch outcome. The
mutation tool's optional `--only` supports a named incremental test; default
whole-corpus runs still execute every retained mutation.

After resolving the indicator, the same first-crash transition reached
`$E662`. Its connected static control-flow graph contains 1,776 additional
instruction sites, including literal branch-table entries at `$E91E`, `$E99A`
and `$F374`, object callback addresses, debris, crash animation/recovery,
`$9576` allocation, `$10002..$10346` helpers and the `$188A8` caller. Inline
pointer data at `$E89E..$E8A4` is excluded. Both DBF loop bodies are included.
The fixture plan uses the ROM-produced aligned indices: `$E778/$E77A` shift
debris indices left six, `$F578/$F57A` and `$F630/$F632` double longword table
indices twice, and `$FA90` masks the ring-buffer word offset to `$E`.

The initial comparison exposed eight differences at four indexed immediate
BSET sites `$E782/$E788/$E78E/$E794`. The generator omitted their two internal
index clocks. MAME's `bset_imm8_dais_df` performs immediate prefetch, two index
clocks, extension prefetch, byte read, final prefetch/IPL and byte write. The
generator now emits that sequence; no previously generated graph contains
these indexed bit forms. The corrected source matches all 8,922 crash fixtures
and 22,148 phases, with zero differences/skips. The earlier failing comparison
remains local in `build/oracle/browser_crash_compare_pre_fix.log`.

The complete current recovery union is 4,994 PCs, 451 branches with both
outcomes, and 23,560 original-ROM fixtures across the retained native capture,
the indicator increment, and `browser_crash_aligned.bin`. Each capture passes
CPU state/bus comparison separately. The mutation runner can compare a single
mutated executable against multiple captures; every default mutation remains
required, and a non-fixture error is rejected as mutation evidence. Wrong crash
descriptor and dispatch-target mutations reject after the baseline passes.

Extended neutral play subsequently passed the first crash through frame 2350,
then reached `$DD7A`. Its recovery HUD countdown and `$E192` blinking descriptor
helper add 17 literal sites. All 94 independent MAME fixtures match; the current
4-capture union contains 5,011 PCs, 453 branches with both outcomes and 23,654
fixtures. The eleven pre-HUD recovery mutations all reject across the retained
native, indicator and crash captures (`build/oracle/browser_recovery_mutations.log`).
The new countdown mutation changes its decrement from one to two and is checked
against `browser_recovery_hud.bin`. Aggregate checks never omit older PCs or
fixture comparisons to accommodate an increment.

The HUD countdown mutation rejects after a passing baseline. The runner now
checks every supplied recovery baseline before compiling mutations; a negative
check with the earlier unaligned crash capture confirms that a failing baseline
cannot produce accepted mutation evidence.

The next respawn reaches main-CPU `$0402`, a different routine from the sub-CPU
reset at the same numeric address. Its auxiliary-object graph includes 82 new
sites and its `$043E/$044E/$054C` callbacks. All 374 MAME fixtures pass.
Combined coverage from the five retained/incremental captures includes 5,093
PCs and both outcomes of 460 branches, with 24,028 fixtures. The callback
mutation changes `$044E` to `$0450` in `$0428`.

The respawn callback mutation rejects. Extended play then reaches frame 2850
before stopping in the `$D89A` bonus/countdown continuation. Its 96 missing
sites and callees match all 402 MAME fixtures. The six-capture union contains
5,189 PCs, 467 branches with both outcomes and 24,430 fixtures. The new
`$D8CE` mutation changes the countdown decrement from one to two.

The bonus mutation rejects, and neutral gameplay completes through frame 3600.
A fresh run with flight/throttle/cannon/missile inputs reaches frame 1320 before
exposing `$5B5A`. Its combat-motion graph adds 229 sites, including memory
`TAS (A0)` at `$6426`. The generator adds only that supported memory addressing
form: byte read, two internal clocks, NZVC from the old byte with X retained,
byte write with bit 7 set, IPL selection, and final prefetch. This follows
MAME `tas_ais_df`; the X-Board driver defines no TAS write override. Directed
fixtures include `$80A5/$7FA5/$FFA5`, verifying the old sign, bit update and
neighbor-byte preservation. All 1,334 fixtures match. Aggregate coverage is
5,418 PCs, 496 branches with both outcomes and 25,764 fixtures across seven
captures. The new memory-TAS mutation sets bit 6 instead of bit 7.

The memory-TAS mutation rejects after its passing baseline. The active-controls
run subsequently reaches frame 1440, then encounters positive velocity clamp
`$5C28`. A literal branch/callback audit of the already verified motion family
`$5B5A..$679E` finds eleven missing sites, including both clamp directions and
their neighboring continuations. All 52 independent fixtures match, and changing
the `$5C28` subtraction to addition rejects. The eight-capture recovery union
contains all 5,429 declared PCs and both outcomes of 497 branches, with 25,816
matching original-ROM fixtures. Earlier captures and default mutations remain
retained.

Final optimized-bridge live smoke passes 5,160 physical frames: original boot
and 3,000 neutral gameplay frames (through frame 3600), then cold restart/boot
and 960 sustained flight/throttle/cannon/missile frames (through frame 1560).
The commands and result remain ignored in `build/threejs_extended_play.py` and
`build/threejs_extended_result.log`. This verifies the reported stop and the
observed first-crash/respawn/combat continuations together; it is not a full
credit or whole-host MAME comparison. All sixteen retained/new recovery
mutations reject across their explicit captures. Final `test`, `verify-stage1`
and optimized `verify-threejs-core` pass in
`build/workflow/20261009T122421.411456Z/`, retaining both the 603- and 781-frame
whole-host MAME gates. `git diff --check` passes. No ROM data/captures are tracked.

The live Three.js run subsequently verifies 3,790 rendered board frames with
zero native RGB checksum mismatches, including crash and recovery, before a
later descriptor update stops after frame 4372 at `$13BCC`. Its fifteen literal
sites through `$13BF8` select a ROM descriptor and copy four longwords to video
RAM. All thirty MAME fixtures match. The nine-capture recovery union includes
5,444 PCs, both outcomes of 497 branches and 25,846 matching fixtures. A new
pointer mutation selects `$30CF6` instead of `$30CF4` and rejects after its
baseline passes, bringing the retained recovery mutation total to seventeen.

The final rebuilt bridge passes 6,160 native physical frames: cold boot and
4,000 neutral gameplay frames through frame 4600, then restart/boot and 960
flight/throttle/cannon/missile frames through frame 1560. All nine retained
recovery captures recompare with zero differences or skips (25,846 fixtures),
and the aggregate coverage check passes all 5,444 PCs and both outcomes of
497 branches. Final `test`, `verify-stage1` and `verify-threejs-core` pass in
`build/workflow/20261009T123856.834272Z/`. This retains the 603/781-frame
whole-host MAME gates; the longer smoke does not establish full-credit
acceptance. The browser is returned to a fresh flight scene with diagnostic
pixel verification disabled for normal play.


### Verified playable-host merge snapshot (2026-10-09)

The merge freezes 5,011 recovery instruction sites, independently replayed as
23,654 original-ROM fixtures and 56,530 exact bus phases, with zero differences
or skips. All 453 conditional branches have both outcomes; all twelve recovery
mutations reject after the baseline passes. The capture is
`build/oracle/cpu_recovery_release_complete.bin`, generated with a 32-second
emulation limit so the complete declared set is captured.

Concurrent gameplay work continued in the shared checkout. Verification used
an isolated copy matching every staged Git blob, with separate build outputs
and retained local ROM/capture inputs. On that snapshot, `test` passed in
32.11 seconds, `verify-stage1` in 89.09 seconds, the complete `oracle-test` in
628.64 seconds, and `verify-threejs-core` in 104.34 seconds. The latter checks
the browser release compiler/options against the cold and expanded whole-host
corpora, including 781 frames, 55,982,080 exact RGB pixels and mixed audio.
Logs are retained locally in `build/workflow/release-snapshot-20261009/`.

Later uncommitted continuations remain outside this merge checkpoint. The
2,400-frame native attract smoke and finite MAME acceptance do not close
full-credit gameplay, arbitrary input trajectories, later stages or peak-load
real-time performance. CMake test registrations were added but CMake was
unavailable locally; these results use Make. No ROM data or captures are
committed.

## Windows performance portability (2026-10-15)

The user reported very slow Windows gameplay using an apparent GCC 16 / Ninja /
CMake build, without a saved profile. Code inspection found that Apple Silicon
used cooperative stacks while Windows used OS worker handoffs at CPU execution
checkpoints. Windows now uses `CreateFiberEx` / `SwitchToFiber` on the host
thread, with 1 MiB reserved stacks and `FIBER_FLAG_FLOAT_SWITCH`. A shared host
conversion survives CPU destruction in either order and restores a thread only
if this adapter converted it. Existing caller-owned fibers remain caller-owned.
Grant/debt, bus timing, cancellation, unwinding and exception propagation retain
the existing continuation protocol; no literal CPU port or board clock changes.
`AB2_FORCE_THREAD_CONTINUATION` retains the worker backend for comparisons.

CMake now checks IPO support and enables LTO for Release/RelWithDebInfo, with
`-O3` for GCC/Clang or `/O2` for MSVC-style drivers. Debug stays unoptimized.
`AB2_ENABLE_IPO=OFF` permits disabling LTO. No fast-math or build-host CPU ISA
requirement is introduced. This does not port the engine's existing
`unsigned __int128` to MSVC. GCC/MinGW x64 supports the existing engine requirement.

The GPU path already embeds Metal/MSL and Vulkan/SPIR-V; Vulkan is the Windows
compute option, subject to the installed SDL build and GPU driver. Direct3D 12
compute shader binaries remain absent. A failed GPU-device/renderer setup now
lets SDL choose an available renderer (including accelerated presentation) with
CPU filters instead of forcing software. `--profile` reports the selected
continuation, renderer/backend and CPU/GPU filter path. Frame-deadline waits use
`SDL_DelayPrecise` with a 2 ms polling cap and subtract already-spent host time;
the board's fixed tick and catch-up limits remain unchanged.

The existing continuation tests cover interleaved locals/FPU state, deferred
jobs, reset, cancellation, exceptions and destructor unwinding. Windows-only
cases additionally check shared conversion lifetime, non-LIFO destruction and
caller-owned fiber preservation. The optimized macOS CMake build and both
continuation tests pass; logs are in `build/cmake-windows-perf/` (`build-final.log`
and `ctest-final.log`). A 120-frame dummy-video smoke passed; a forced compute
failure used accelerated Metal presentation with CPU filters (`smoke-dummy.log`
and `smoke-fallback.log`). These are macOS results, not Windows performance or
fiber execution evidence. The Windows checks and representative/peak-load
Windows profiling remain required before platform acceptance.

The final game and both continuation regression executables also cross-build as
Windows x64 PE binaries using LLVM-MinGW 20261006 (Clang 23.1.3), SDL 3.4.16,
Release/LTO and optional OpenAL disabled. Toolchain/SDK downloads were checked
against their official release SHA-256 digests and extracted under the local
temporary directory, without system installation. Cross-build logs are
`build/cmake-windows-perf/cross-configure.log` and `cross-build-final.log`;
executables are in `build/windows-cross/`. This exposed missing `<string>`
includes in `omainhost.cpp` and `omainirqcallee.cpp`, now explicit. Cross-building
does not execute the Windows fiber tests or validate Vulkan output. The prior
`required` run at `build/workflow/20261015T201350.493071Z/` was deliberately
interrupted after those include fixes; it is not a complete acceptance pass.

Final-tree `test` and `verify-stage1` pass at
`build/workflow/20261015T202242.843626Z/`; the directed sweep covers all 23 stages.
Final `native-release` and `verify-gpu-native-menu` pass at
`build/workflow/20261015T203355.998841Z/` (4,128/4,128 native GPU cases identical,
zero skipped). The unchanged standalone GPU filters also pass 630/630 cases,
zero skipped, with six rejected controls at
`build/workflow/20261015T201706.542431Z/`. Final CMake GPU, accelerated-fallback
and dummy/software smokes are retained as `smoke-*-final.log` under
`build/cmake-windows-perf/`. The complete final-tree `required` profile passes
at `build/workflow/20261015T202242.843626Z/`: `test`, `verify-stage1` and
`oracle-test`, including all retained mutations. The four lifecycle replays
match 104,452/104,452 ten-column MAME observations with zero skips; individual
legacy oracle input-race exclusions remain reported in the full log. No
captures or ROM data were committed. This is macOS regression evidence and
Windows cross-build evidence; Windows fiber execution, Vulkan output and
sustained performance remain unverified.

### Preview 2 packaging (2026-10-16)

`v0.1.0-preview.2` includes the updated macOS arm64 package and a separate,
unsigned experimental Windows x64 package. README, release notes, roadmap and
dependency notices describe platform requirements and the Windows runtime gap.
The new `tools/package_windows.py` accepts the clean committed Release/LTO
cross-build with OpenAL disabled, checks pinned dependency/source archives,
copies x64 DLL bytes from the official archives and verifies PE architecture,
fiber imports and dependency closure. Both continuation test executables and a
ROM-free check launcher are included. Project, SDL, LLVM runtime, exact MinGW
runtime and toolchain build-script sources accompany the DLLs and notices.
Microsoft system DLLs, ROMs, captures and personal settings are excluded.

Preflight packages from implementation commit `bab6a4f` pass independent ZIP
and payload hash checks, complete source-tree comparison with Git, executable
launcher permissions, package-relative Mach-O dependencies/signatures and
Windows x64 DLL closure. The 240-frame extracted macOS launcher smoke uses
temporary settings/ROM links, Metal GPU filters, widescreen/effects and OpenAL
headphone HRTF; all temporary ROM links are removed afterward. Windows execution
is not attempted on macOS. Records: `build/releases/preview2-preflight-audit.json`
and `.log`; checksum/overwrite rejection controls are retained in
`build/releases/preview2-package-controls.json`.

The complete engine `required` pass at `build/workflow/20261015T202242.843626Z/`
is explicitly reused: engine sources, transitive dependencies, compiler/options,
fixtures and comparison/mutation harness remain unchanged. Supplemental
`oracle-joint-main-progress-full`, `oracle-joint-reset-window` and `native-release`
pass freshly at `build/workflow/20261016T034347.883905Z/` (37.49/7.90/0.34 s).
Subsequent changes are release packaging and documentation. Package smoke and
cross-build checks do not establish Windows execution, sustained performance or
full-game/physical-platform acceptance. Final ZIP manifests identify the release
commit and all file hashes; the GitHub release carries the ZIP checksums.

### Windows runtime, OpenAL Soft and widescreen/GPU pacing validation (2026-10-16)

Native testing on Windows 11 x64 (`w64devkit` GCC 16.2.0 `-O3`, SDL3 3.4.18,
OpenAL Soft 1.25.2) identified and resolved three runtime issues and several
cross-platform performance bottlenecks:

1. **Vulkan fullscreen and presentation pacing (`src/main/main.cpp`, `src/main/frontend/native_menu.hpp`):**
   - `SDL_RENDERER_VSYNC_ADAPTIVE` (`-1`, `VK_PRESENT_MODE_FIFO_RELAXED_KHR`) on
     the Windows Vulkan swapchain stalled fullscreen mode transitions and
     conflicted with DWM desktop composition and software deadline waits.
     Presentation now uses `SDL_SetRenderVSync(renderer, 0)` on the GPU renderer
     and calls `SDL_SyncWindow(window)` after `SDL_SetWindowFullscreen`.
   - Frame-deadline waiting now runs immediately after `SDL_RenderPresent`
     (before 1-second FPS window sampling), with input polled immediately before
     `advance(input)` for lower latency and 1:1 `simulation_fps == display_fps`.
   - The 2-frame catch-up loop (`while (... && catchup_frames++ < 2)`) was
     replaced with a single simulation step per display frame and a hybrid wait
     (`SDL_Delay(1)` only when `remaining > 4.5 ms`, spin-waiting to the exact
     `16.768 ms` deadline below `4.5 ms`, bounded by `4.0 * tick_seconds`) so
     Windows timer-resolution jitter cannot trigger double-tick spikes.
2. **OpenAL Soft 1.25.2 enablement and `AL_STOPPED` stream-restart fix (`src/main/frontend/audio_output.hpp`, `src/main/frontend/native_menu.hpp`, `src/main/frontend/native_music.hpp`):**
   - Under OpenAL 1.1, `alSourceStopv` followed by `alSourceUnqueueBuffers`
     leaves sources in `AL_STOPPED` (`0x1014`). Because `queue_al()` only starts
     sources when all active sources are in `AL_INITIAL` (`0x1011`), any
     subsequent audio queued after `clear_al()` (such as returning from the
     pause menu with `ESC` or switching music track previews) remained silently
     stuck in `AL_STOPPED`. Calling `alSourceRewindv` in `AudioOutput::clear_al()`
     and rewinding exhausted `AL_STOPPED` sources in `AudioOutput::reap()`
     restores `AL_INITIAL` and fixes both music preview switching and mid-game
     pause/resume audio across Windows and macOS.
   - `NativeMenu` and `NativeMusic` now switch directly when `SPACE` is pressed
     on a different track row while previewing, prioritize the latest track
     request at the front of the background decode queue (`jobs.push_front`), and
     reflect pending track selections immediately in `NativeMusic::selection()`.
3. **Cross-platform CPU, widescreen, horizon and GPU-filter optimizations:**
   - `XBoardHost::observe()` (`src/main/engine/xboard_host.hpp`) checks a
     `presentation_phase` fast-path mask before constructing 88-byte `Entry`
     structs and invoking `std::function`, avoiding ~200,000 unnecessary struct
     copies and indirect calls per frame during normal gameplay.
   - `XBoardHost` tracks `tile_gen` and `palette_gen` dirty counters via
     `trace.before_access` (`src/main/engine/xboard_host.hpp`,
     `src/main/engine/native_stage_skip.hpp`, `src/main/hwvideo/scene_snapshot.hpp`,
     `src/main/hwvideo/wide_scene.hpp`), skipping redundant 32,768-tile and
     8,192-palette reloads across partial scanline clips in both `XBoardHost::render()`
     and `WideScene::draw()`.
   - `GameSession` (`src/main/engine/game_session.hpp`) pools `wide_clips`
     capacity via `wide_clip_count` so 94 KB `SceneClip` buffers and
     `extended_sprites` vectors are reused across partial clips instead of
     reallocated/zeroed every frame.
   - `WideVisibility::recover()` (`src/main/hwvideo/wide_visibility.hpp`) syncs
     only the 64-byte object and `0x29c080..0x29c180` camera block instead of
     copying 64 KB (`sync_scratch`) per rejected scenery object.
   - `GroundHorizon` (`src/main/hwvideo/ground_horizon.hpp`) caches computed
     horizon profiles across partial scanline clips within the same frame and
     reuses its `pens` scratch vector.
   - `HWSprites` (`src/main/hwvideo/hwsprites.cpp`) and `Presentation::decode`
     (`src/main/hwvideo/sprite_scene.hpp`) clip vertical row loops directly to
     `[clip_top, clip_bottom]` and skip all-transparent 32-bit sprite words
     (`!word`). `HWTiles::draw_layer()` (`src/main/hwvideo/hwtiles.cpp`) hoists
     per-row scroll/page calculations out of the inner pixel loop.
   - `GPUSpriteScaler` (`src/main/frontend/gpu_sprite_filter.hpp`) allocates
     `layer_capacity` with a 2 MB minimum and 2x growth headroom to prevent
     mid-flight GPU buffer reallocations during dense explosions/clouds.

**Windows x64 verification results:**
- Both ROM-free continuation tests (`cpu_continuation_regression.exe` with
  `windows-fiber` and `cpu_continuation_thread_regression.exe` with
  `worker-thread`) pass on Windows 11 x64.
- Stage 1 (420 frames, 16:9 widescreen, Corrected ground, ScaleFX 3x, all four
  translucency toggles, Integer scaling, OpenAL Soft 1.25.2 headphone HRTF):
  locked `simulation_fps=59.6374 display_fps=59.6374` across all seven 1-second
  windows (`board_ms=9.67–10.24 ms`, `wide_scene_ms=2.46–2.84 ms`,
  `filter_submit_ms=0.24–0.35 ms`).
- Stage 5 (420 frames, 16:9 widescreen, Corrected ground, ScaleFX 3x, all four
  translucency toggles, Pixel-Art scaling, OpenAL Soft 1.25.2 headphone HRTF):
  locked `simulation_fps=59.6374 display_fps=59.6374` (`board_ms=9.15–10.15 ms`,
  `wide_scene_ms=1.27–3.94 ms`, `filter_submit_ms=0.22–0.45 ms`).
- `tools/package_windows.py` now supports `--native-build build/win`, packaging
  the verified Windows x64 executables alongside `SDL3.dll` (3.4.18),
  `OpenAL32.dll` (1.25.2), upstream sources and notices.
- **macOS handoff requirement:** full MAME oracle and regression suites were
  intentionally skipped on the Windows machine per user instruction. The next
  macOS agent must run `python3 tools/workflow.py run required -j 4`,
  `verify-gpu-native-menu`, `verify-wide-gpu`, `test-spatial-audio`,
  `verify-spatial-mutations` and `native-release` on macOS to re-validate the
  modified engine/video/audio headers against the retained MAME captures.

## Local 16:9 and 16:10 viewport selection (2026-10-17)

The local source now exposes Original, 16:9 Preview (398×224, 39 side columns)
and 16:10 Preview (358×224, 19 side columns). Both enhanced modes retain 224
logical rows and square-pixel proportions; the even widths approximate their
named display ratios. This is additional scene rendering, not stretching or
cropping the native frame. The published preview 2 packages predate this change.

`Frontend::Viewport` replaces the Boolean runtime setting. Preferences retain
`widescreen=0|1` for older readers and add `viewport=original|16:9|16:10`;
the explicit recognized viewport overrides the legacy setting in either file
order. Existing `widescreen=1` files select 16:9. The menu cycles in both
directions; logical presentation, window sizing and the centered 320×224
HUD/menu safe area follow the selected width.

`GameSession::capture_wide_scene(enabled, logical_width)` retains the 398 default
for existing callers, validates dimensions and supplies `(width-320)/2` to the
detached visibility observer. Width changes recreate that observer, clear ready
clips and rebuild the frontend wide renderer/textures without advancing native
time. The next completed board frame publishes fresh clips. Native CPU ports,
bus timing, spawning, collision, targeting and hardware sprite limits are unchanged.

The independent geometry tests retain 398-wide coverage and additionally check
358-wide tile/column wrapping, partial palettes, both sprite edges, original-road
center alignment, non-flight correction and immutable feedback. All nine retained
geometry mutations are applied independently at both widths; three anchors were
updated to match the earlier optimization's whitespace/signature changes.
Directed stage tests accept `--width 398|358`, retain all 23 existing stage slots,
and add live width switching, native RGB/PCM/time comparisons, legacy native-image
reconstruction, both roll-input directions, terrain protection, pause and restart.
The new `verify-wide-stages-16-10` is included in `verify-stage1`.
`verify-wide-gpu-16-10` consumes the Stage 1/5 fixtures from that sweep and compares
all six filters and both opacity policies with the CPU renderer.

Validation records and remaining scope are recorded in the widescreen plan.
These finite checks do not close every controller/trajectory, sustained load,
physical Windows/Linux 16:10 or continuous whole-credit fidelity acceptance.

## Native rendering and playable release continuation (2026-10-11)

`make play-native` builds `build/afterburner2-release` with the same `-O3 -flto`
options as the verified bridge (ordinary floating-point semantics). It runs
600 original cold board frames with the bridge's physical coin/start pulses,
drains startup audio, and hands flight to the existing keyboard controls.
`--profile` reports completed simulation/display frames per wall-clock second,
board cost, SDL presentation call time and selected renderer. P/F5 clear timing
statistics; F5 retains the original fresh cold restart. No browser/server,
capture-driven execution, direct game-state seed or gameplay rule is introduced.

The first unpaced release profile over 181 flight/weapon frames measured
10.9555 ms average board work (11.326 ms p95), of which rendering and video
synchronization cost 1.04048 ms. The remaining board work was 9.91499 ms;
rendering was not the dominant cost in that window. An optional passive
`rendering_started` observer brackets existing render completion without owning
board time. `benchmark-native` measures this window; `benchmark-native-extended`
replays the retained eight 120-frame flight/throttle/weapon segments, sampling
960 flight frames after the same boot. Benchmarks exclude window presentation.

Road, tile and text draws now accept inclusive clip bounds. Screen clears only
the current priority/sprite rows and draws only those road/tile/text rows.
Sprite clipping and scaler writeback are unchanged. Only the supplied clip's
rows belong to the passive layer-observer frontier. Tile/text categories are
disjoint opaque pixels: a combined traversal writes each pixel once with the
original category-dependent priority mark instead of recomputing coordinates
twice. Palette pens retain the original resistor-DAC conversion; unchanged
words reuse both resolved pens, with an explicit initial sentinel.

The ROM-free clipping check compares 384 layer/road clips against full rasters,
including single rows, tile boundaries, row/column/alternate scroll state, all
eight road controls and untouched outside rows. Three further comparisons
check combined categories against the original two-pass ordering. Two private
mutations (missing last road row and wrong category priority) reject. The
existing MAME RGB, video timing, sprite feedback and audio checks remain required.

The first real macOS-window run after physical boot completed 180 flight frames
at 59.6287–59.9087 simulation/display FPS, with 11.3219–11.4128 ms average board
cost per reporting window. The final release-core 603/781-frame comparisons
match, including 55,982,080 expanded RGB pixels and 627,841 mixed stereo frames,
with zero differences/skips. ROM-free and Stage 1/native integration checks
pass. These finite observations do not close full-credit progression,
arbitrary input trajectories, every-stage real-time performance or unported
instructions. No scheduler approximation or CPU timing shortcut was needed.

Final unloaded-machine measurements are in
`build/workflow/20261011T083052.670135Z/`: the same 181-frame profile averages
10.7994 ms (11.3276 p95, 12.064 maximum), with rendering/synchronization at
0.875154 ms, about 16% less than the initial profile. Whole-board improvement
is only about 1.4%, consistent with the non-rendering work dominating. The
960-frame sustained flight/throttle/cannon/missile profile averages 10.8291 ms
(11.4404 p95, 13.9927 maximum), about 92.34 unpaced frames/second. Every measured
frame fits the 16.768 ms board budget in that finite run. The bridge's 60-frame
simulation/export/transport mean is 12.8878 ms, excluding browser GPU work;
there is no material bridge throughput gain claimed from these observations.

A separate native Metal window (`accelerated=1`, `vsync=1`) completed 960
neutral flight frames after physical boot; all sixteen one-second reporting
windows measured 59.5917–59.9072 simulation/display FPS. The ignored log is
`build/native_play_extended_profile.log`. These observations are on the current
Mac, with default arcade resolution, and do not predict other hardware, high
resolution enhancements or every stage. CMake is unavailable locally; its test
registrations are added but not executed.

The first required suite exposed an existing recovery fixture/manifest mismatch:
`cpu_recovery.bin` covered 3,101 PCs while the uncommitted current manifest
declared 5,444. Existing local increments cover the manifest, but the default
gate consumed only the old fixture. A fresh original-ROM capture under the
stable ignored name `cpu_recovery_complete.bin` contains 25,846 fixtures; all
25,846 match, with 61,760 exact bus phases and zero differences/skips. The old
14,014-fixture baseline (32,726 phases) stays required, replayed separately;
the combined census covers 5,444 PCs and both outcomes of all 497 branches.
Capture, coverage and mutation targets now consistently use both fixtures.
`oracle-capture-recovery` preserves an existing baseline and independently
captures one only when absent. The release compiler independently passes both
corpora through `verify-native-release-recovery`, recorded with ROM-free and
native/Stage 1 integration checks in `build/workflow/20261011T083322.349970Z/`.
No fixture coverage is removed; captures and ROM data remain ignored.

Final required checks in `build/workflow/20261011T083322.349970Z/` all pass:
release recovery 26.70 s, `test` 12.09 s, `verify-stage1` 43.48 s and complete
`oracle-test` 985.29 s. All fifteen whole-host owner mutations, all seventeen
recovery mutations and the two new renderer mutations reject. Earlier fixture
race exclusions remain reported by their own checks (including the legacy
sprite/scenery captures); the zero-difference/skip claim applies only to the
declared autonomous/recovery corpora, not every older fixture. No existing
fixture is dropped to obtain this pass.

### Space-input stop at `$E514` (2026-10-11)

The native stop at decimal PC 58644 is the missing `MOVEQ #127,D0`
at `$E514`, between the already ported `$E510` BLE and `$E516` table
lookup. `$E504` reads `$29C0AA`, shifts the signed word right twice and
compares it with 127; this instruction clamps larger indices before the
ROM lookup at `$2B790`. Neither local `main` nor `origin/main` contains
the missing case. The recovery generator now translates it literally,
including CCR preservation and its final prefetch, with no scheduling change.

Two independent original-ROM fixtures (initial CCR 0 and 31) compare exactly:
2/2 states and 2 bus phases, zero differences or skips. Changing 127 to 126
is rejected by the independent state comparison. The retained recovery plan
includes the new PC and mutation; no prior coverage is removed.

`benchmark-native-full-throttle` adds 960 sustained Space/neutral-steering
physical flight frames, enabling both weapons after 120 frames. This is a
finite input smoke check, not proof of all full-throttle gameplay: the first
960 frames do not naturally execute `$E514`, so the directed oracle is the
regression for the exact reported instruction. A separate longer exploratory
run stops at the still-unported `$096E` object callback; complete-credit
acceptance remains open. ROM data, logs and captures stay ignored.

The refreshed canonical capture contains 25,848/25,848 identical fixtures
(61,762 bus phases), zero differences or skips, including both new clamp
cases. Together with the retained baseline, coverage is 5,445 PCs, 497
branches with both outcomes and 39,862 fixtures. The optimized release
replay passes both corpora. `test`, `verify-stage1` and the 960-frame
Space-input smoke pass; both native executable variants were rebuilt.
Logs: `build/workflow/20261011T090321.043030Z/`,
`build/workflow/20261011T090456.449828Z/` and
`build/workflow/20261011T090705.855785Z/`.
The optimized `verify-threejs-core` also passes the retained cold and
781-frame active-gameplay comparisons, with zero supplied execution/state
seeds and zero differences/skips within those finite captures
(`build/workflow/20261011T090656.666779Z/`, 95.06 seconds).
All eighteen retained recovery mutations reject against the two corpora
(`oracle-recovery-mutations`, 369.15 seconds). The full unrelated
`oracle-test` umbrella was not rerun for this one-instruction change; the
required full suite remains a pre-commit gate. No commit was made.

### Left/right roll stop at `$7784` (2026-10-11)

Decimal PC 30596 is `BSET #4,(A5)` at `$7784`, a missing sprite-
orientation bit update in the otherwise translated `$7650..$77AC` handler.
A static instruction census finds no other missing instruction within that
handler after this addition. The adjacent `$77F8..$786A` orientation helper
also lacked `$7802`, which selects the ROM table at `$34BE0`; it is now
translated. Neither change invents an object or changes CPU scheduling.

Independent original-ROM fixtures cover both initial CCR extremes and the
orientation bit initially clear/set, while preserving unrelated byte bits
and the neighbouring byte. The two instructions match 8/8 fixtures and
30 bus phases with zero differences or skips. Wrong orientation-bit and
table-address mutations both fail.

A fresh physical replay alternating full left/right every 60 flight frames
exposed `$6598` before reaching the reported orientation site. The literal
effect-animation initializer, update and restart graph `$6598..$66EC` has
80 additional instruction sites, with six conditional branches. Its ROM
tables are read at runtime. The combined directed increment matches
348/348 original-ROM fixtures, 932 bus phases, zero differences or skips.
Wrong effect-table and countdown mutations both fail. All four new mutations
are retained in the recovery gate.

`benchmark-native-rolls` runs 960 physical flight frames with alternating
full left/right, Space held for the first 480 frames and neutral throttle
for the remaining 480, plus both weapons. Passive entry counts distinguish
naturally executed orientation/effect instructions from directed fixture
coverage. The first neutral-throttle replay executes the effect initializer
once but not `$7784`; it completes after the new graph is ported. Finite
input smoke coverage is not complete-credit MAME acceptance.

The final full/neutral-throttle roll replay completes 960 flight frames;
it executes `$6598` once and does not naturally execute `$7784`. The
reported stop is therefore pinned by its independent directed fixtures,
not inferred from this input smoke. Logs are retained locally under
`build/workflow/20261011T100618.336477Z/`. Both native executable variants
were rebuilt. No claim of full-credit or every-roll-sequence acceptance is
made, and the previously discovered later `$096E` gap remains open.

Final checks: refreshed recovery replay matches 26,196/26,196 fixtures
and 62,694 bus phases with zero differences/skips; the unchanged baseline
still matches 14,014/14,014 fixtures. Their union covers 5,527 PCs and 503
conditional branches with both outcomes, 40,210 original-ROM fixtures.
Both normal and optimized compilers pass this recovery census
(`build/workflow/20261011T101031.501598Z/`). `test`, `verify-stage1` and
`verify-threejs-core` also pass (`build/workflow/20261011T100523.258427Z/`).
All four new mutations were individually rejected against the independent
increment before live integration. The full unrelated `oracle-test` umbrella
was not rerun; it remains required before committing. No commit was made,
and the prior Space fix and all earlier fixture coverage are preserved.

### Missile-input stop at `$19C78` (2026-10-11)

Decimal PC 105592 is `SUBQ.B #1,($89EA).W` in the indicator handler
`$19C4E..$19D8E`. It decrements a byte, preserves the neighbouring byte,
and branches at `$19C7C` to the already translated `$19D80`. The report
identifies the triggering input; the instruction itself is a countdown
update, not a new missile trajectory or rendering rule.

An instruction census of the handler found nine missing sites: `$19C78`,
`$19C7C`, `$19C80`, `$19C88`, `$19C8C`, `$19C94`, `$19CB4`, `$19CBA` and
`$19CBE`. They are now translated literally, closing the two mode-check
branches and the secondary word countdown/indicator-call/return path as
well as the reported byte countdown. No missing static instruction remains
within this handler. No CPU scheduling or ROM-derived data is replaced.

Independent original-ROM fixtures cover byte/word countdown zero, one,
signed-boundary and wrapping values; the byte's neighbour is retained. Mode
compares have operands around `$10` and `$16`; both conditional branch
outcomes are exercised independently over all 32 CCR patterns. All
114/114 fixtures and 360 bus phases match, zero differences or skips.
Wrong byte decrement, terminal branch target and word decrement mutations
are independently rejected and retained in the recovery gate.

`benchmark-native-missiles` adds 960 flight frames with 32 six-frame missile
press windows separated by 24 release frames, neutral steering, no cannon,
neutral throttle for the first 480 frames and Space for the remainder. The
count is physical press windows, not claimed missile launches; ammunition
and launch gates remain ROM-owned. A passive entry counter reports whether
this finite input sequence actually executes `$19C78`. Directed fixtures
pin the exact reported instruction independently of that live reach.

The missile smoke completes 960 flight frames and naturally executes
`$19C78` fifteen times, covering the exact reported stop. The smoke now
requires at least one such instruction entry, so a run that misses the
regression path cannot silently pass. Physical input and counters remain
passive; no gameplay RAM or CPU context is seeded. The three new mutations
were rejected before native integration.

Final physical missile regression, including its required exact-PC reach,
passes in `build/workflow/20261011T101624.782374Z/`: 960 flight frames,
32 input press windows and 15 `$19C78` entries. The optimized instruction
replay also matches all 114 fixtures. `test`, `verify-stage1` and
`verify-threejs-core` pass in `build/workflow/20261011T101452.943818Z/`;
this includes the retained cold and 781-frame whole-host comparisons.
Both native executable variants are rebuilt. No complete-credit acceptance
is inferred from these finite captures.
The refreshed recovery gate passes with 26,310/26,310 fixtures and 63,054
bus phases, zero differences or skips; the retained baseline still passes
14,014/14,014 fixtures. Combined coverage is 5,536 PCs, all 505 conditional
branches with both outcomes and 40,324 original-ROM fixtures. Normal and
optimized replays pass (`build/workflow/20261011T101937.000267Z/`).
The full unrelated `oracle-test` umbrella was not rerun; it remains a
pre-commit gate. No commit was made, and previous patches/captures remain
covered. ROM data and captures stay ignored.

## Initial directed native stage/input discovery (2026-10-11)

This is the initial discovery checkpoint. The follow-up instruction closure
below supersedes its open-frontier list; its logs remain historical evidence.

The requested starting scope is ten seconds **total per stage**, split into
eight 75-frame windows: full up/down, full bank left/right, opposite-endpoint
roll gestures left/right, held Vulcan, and four-frame missile presses with
eleven-frame releases. The native physical period makes 600 frames about
10.06 seconds. Throttle stays neutral. This is an instruction-discovery smoke,
not a rendering/FPS measurement or a MAME comparison.

`tools/stage_input_sweep.cpp` reads the ordered stage-slot count from the ROM
at runtime (23 in this set). Each case cold-boots a fresh board with physical
coin/start pulses, then waits for active-flight mode `$10`; frame 600 itself
is still the carrier sequence. Slot 1 starts naturally. Later slots use
directed operands at a completed frame boundary: the original event command
12 (`$DEC0 -> $DFC4`) increments the selected stage/index, resets distance,
and runs `$13C92`. The initializer reads that slot's own descriptors and
publishes its event pointer at `$142D0`; entry is observed at `$13CE8`.
The test does not patch CPU instructions, copy ROM tables into source, or
force roll/weapon behavior. These seeded later-stage entries do not establish
natural progression, prior-stage accumulated state or full-credit acceptance.
All observations use backing memory or passive instruction-entry callbacks;
using diagnostic bus reads here would alter CPU continuation timing.

All 23 slots were attempted. Slots **1, 2, 4, 6 and 7** complete all 600 input
frames. The other **18** stop during directed stage entry, before any of the
eight input windows can run:

| First missing main PC | Decimal diagnostic | Stage slots blocked |
| --- | ---: | --- |
| `$D914` | 55572 | 3, 9, 11, 16, 19, 21 |
| `$DC8E` | 56462 | 5, 13 |
| `$DD38` | 56632 | 8, 17 |
| `$E314` | 58132 | 10, 12, 14, 15, 18, 20, 22, 23 |

Original-ROM disassembly identifies these first sites as, respectively,
`MOVE.B ($8B5B).W,D0`, `BMI $DC94`, `MOVE.W D1,D0` and `CLR.B D1`.
No new instruction was translated in this discovery task. Each frontier
and its reachable continuation still need independent MAME evidence and
mutation checks before integration; additional frontiers may lie behind them.

The five completed slots were replayed with passive coverage counters. Every
input gets all 75 completed frames in each replay. The shared roll angle
`$29C0AE` is nonzero during both gesture windows on all five; this confirms
rotation was active, not its direction or a completed full roll. Weapon
routine entries are reported separately from physical button delivery:

| Slot | Left/right gesture frames with active roll angle | Vulcan allocator `$9ED8` entries | Missile initializer `$9F7C` entries |
| ---: | ---: | ---: | ---: |
| 1 | 57 / 55 | 19 | 5 |
| 2 | 55 / 57 | 18 | 5 |
| 4 | 55 / 42 | 0 | 0 |
| 6 | 55 / 44 | 0 | 0 |
| 7 | 55 / 57 | 18 | 5 |

No weapon-execution coverage is claimed for slots 4 and 6 in this sequence.
The allocator count is a routine-entry count, not guaranteed projectile
allocation; the ROM retains ammunition, object-pool and gameplay gates.

The complete discovery log is
`build/workflow/20261011T102851.638324Z/01-stage-input-sweep.log`.
Coverage replay logs are under workflow runs `103307.838837Z`,
`103327.171828Z`, `103346.041877Z`, `103405.089575Z` and `103423.696227Z`
on the same UTC date. The ignored aggregate
`build/oracle/stage_input_sweep_report.json` retains all 23 results and those
coverage records, with source-log paths. The discovery make target returns
success when all requested cases have been examined; individual `status=stop`
lines remain failures to exercise gameplay. This tool adds no whole-host
oracle acceptance. Existing game-core and recovery patches are unchanged.
Focused coverage replays of slots 3, 5, 8 and 10 reproduce each of the four
first missing PCs with zero completed input frames (workflow runs
`103538.270691Z`, `103550.287061Z`, `103602.253085Z`, `103614.269717Z`).
`git diff --check` passes, and ROM images/results remain ignored. No new port,
oracle acceptance or commit is included in this discovery task.

## Native debug stage selection (2026-10-11)

`make play-native STAGE=7` passes `--stage 7` to the release SDL executable.
Direct `--stage N` implies physical coin/start boot even without
`--boot-to-flight`; N is validated as a positive integer against the runtime
ROM slot count before boot. F5 creates a fresh board and repeats the selected
startup. Without the option, existing startup and restart behavior remain.

`native_stage_skip.hpp` shares the sweep's directed operands, active-flight
wait and original stage initialization with `GameSession`'s explicit
`debug_skip_to_stage` method. The temporary passive phase observer is restored
on success or failure. Diagnostics issue no bus transactions. Normal gameplay
does not invoke this helper. This is debug stage selection, with fresh
first-stage accumulated state, not natural progression or a new gameplay port;
the four discovered missing-PC stops were open at this checkpoint; see
the follow-up closure below.

Release build, `test` and `verify-stage1` pass in
`build/workflow/20261011T103859.724306Z/`; normal session behavior retains
781 exact native RGB frames, processed audio, pause/resume and restart.
The final release rebuild and focused session gate pass in
`build/workflow/20261011T103954.949735Z/`. The shared helper's stage 2 sweep
retains all 600 input frames and the previous passive coverage counts
(`build/workflow/20261011T103956.113498Z/`). No new ROM instructions were
ported and no commit was made.
Native CLI smoke with SDL dummy video/audio passes normal two-frame startup
and selected-stage 1, 2 and 7 startup plus two displayed frames. Stage 3
retains the expected PC 55572 stop; zero, 24, negative, nonnumeric and missing
stage arguments are rejected. Logs remain ignored under
`build/oracle/native_stage_cli/`. Interactive F5 was not separately exercised;
it calls the same tested startup path after the existing restart operation.
`git diff --check` passes.

## Stage and gameplay instruction closure (2026-10-11)

The initial stage-entry stops and the subsequently discovered enemy, scenery,
weapon, carrier and final-stage continuations now have literal instruction
ports. The recovery manifest requires **10,525 PCs and both outcomes at
1,060 branches**. All 68,294 retained/new original-ROM instruction fixtures
match, with 158,088 compared bus phases, zero differences and zero skips.
The earlier discovery sections record historical frontiers; they are not the
current missing-instruction list. The Space, roll and missile fixes are retained.

| Graph increment | New sites | Exact MAME fixtures | Bus phases |
| --- | ---: | ---: | ---: |
| `$D914/$DC8E/$DD38/$E314` stage-entry continuations | 40 | 334 / 334 | 760 |
| `$096E` object callback and recovery graph | 189 | 1,568 / 1,568 | 3,610 |
| `$8352/$8098/$2D52/$161B8/$7C56` successors and indexed dispatch | 1,090 | 6,414 / 6,414 | 14,212 |
| Scenery, weapon, enemy and scripted-flight successor graphs | 2,222 | 11,044 / 11,044 | 24,866 |
| Additional condition-byte and spawn-word boundaries | 0 | 42 / 42 | 154 |
| Nine later script/object continuations and final-stage dispatch | 463 | 2,096 / 2,096 | 4,664 |
| Carrier scripts, final-stage callback and enemy motion helpers | 472 | 3,098 / 3,098 | 6,502 |
| Observed enemy-program callees and seven-state carrier variant | 314 | 2,156 / 2,156 | 4,660 |
| Alternate carrier constructor and enemy/weapon effects | 165 | 1,080 / 1,080 | 2,514 |
| Spawn-speed threshold boundaries | 0 | 16 / 16 | 80 |
| Stage 5 carrier callback and animation continuation | 34 | 128 / 128 | 308 |

These increments add 4,989 sites. The 42-case boundary capture repeats six
ordinary fixtures and adds 36 operand cases. The 16-case speed-boundary
capture repeats two ordinary fixtures and adds 14 operand cases. Every generated case is labelled
with its original ROM address. ROM tables are read at runtime; no ROM image,
raw table or instruction capture is committed. All increments receive MAME
comparisons and deliberate mutation rejection before their native stage reruns.

The original bounded dispatch tables at `$7D14/$11246/$16264/$16D72`, the
five-entry final-stage table at `$11C04`, four-entry carrier table at `$12F7A`
and seven-entry variant at `$127EA` are followed as instruction entries.
The final-stage callbacks assigned at `80(A5)` and the object callback stored
through A6 are included. Passive script observations at `$30A4/$315E` expose
original six-byte duration/callee records. Their ROM program continuations
are inspected with the interpreter's negative-count loop rules and actual
held-duration instructions; unrelated data after hold records is not ported
as code. This analysis does not implement replacement gameplay or script rules.

Additional instruction forms follow MAME's microcode: PC-relative PEA keeps
prefetch/stack-write order and IPL latching; `DIVS.W #3,D0` keeps the fixed
positive-divisor restoring-division timing, early/late overflow and signed
quotient/remainder; DBNE keeps the condition-true path, decrement and terminal
fetches. Displacement TAS keeps extension fetch, byte read, two internal
clocks, byte write and final prefetch. Register/displacement MULS reuses the
signed Booth-transition timing. Fixtures cover signed extrema, alternating
bits, zero, register highs, byte neighbours and all initial CCRs where
specified. The activation threshold and held-duration fixtures include
zero, neighbouring thresholds, sign limits and wrap. Zero/negative DIVS
divisors and indexed address-error exceptions remain outside these literal
normal-instruction fixtures. Table-load fixtures use aligned normal offsets.

The orientation-mask mutation exposed a capture setup defect: A5's default
memory aliases SSP, and stack setup overwrote a seeded operand. Operand seeds
now apply after stack setup. The corrected callback corpus was recaptured,
replayed and tested against the wrong-mask mutation. Baseline failures cannot
count as rejected mutations. Concurrent mutation jobs use separate source
overlays and binaries. The complete recovery gate retains **85 mutations**,
including all earlier checks; none of the older fixture coverage is removed.

The complete corpus has 54,280 exact fixtures and 125,362 bus phases; its
14,014-fixture baseline adds 32,726 phases. Original independently replayed
captures are concatenated without altering plan entries, state, timestamps
or events. `merge_cpu_coverage.py` requires newer passing input censuses,
writes atomically, records input SHA-256 provenance in ignored `.merge.json`,
and removes any stale output census. The merged corpus is independently
replayed and checked against the complete manifest and branch census. The
9,077-, 9,540- and 10,012-site corpora are preserved locally. The plan safety
limit rises from 50,000 to 100,000 because the verified complete file now has
54,280 records; state/bus equality and coverage requirements are unchanged.
A fresh full capture uses the original-ROM plan with a 64-emulated-second bound.

Focused accepted capture runs include UTC workflow directories
`20261011T104634.540529Z`, `105516.069644Z`, `111212.737790Z`,
`112650.638549Z`, and `112945.446436Z`. The 9,077-site complete capture is
`build/workflow/20261011T113003.502713Z/`. Later accepted captures and replay/
mutation logs are under ignored `build/oracle/`: `stage_late_all_aligned.bin`,
`next_scripts_complete_verified.bin`, `script_program_closure_verified.bin`,
`remaining_live_gaps.bin`, `spawn_speed_boundary.bin`, and `carrier_last_gap.bin`.
Earlier odd-index discovery captures are excluded from normal instruction
acceptance. An early full-suite attempt encountered the complete capture
while it was still being written; this is not accepted oracle evidence.
Further exploratory suite runs were interrupted when new live gaps appeared.
Only the final completed suite establishes this checkpoint's required checks.

The final required suite completes successfully in
`build/workflow/20261011T125042.300165Z/`, using
`python3 tools/workflow.py run test verify-stage1 oracle-test verify-threejs-core -j3`.
All 85 recovery mutations and all 15 whole-host owner mutations reject;
the earlier nine CPU mutations also remain required and pass. Retained
legacy fixtures keep their explicitly reported inter-CPU race skips; the
expanded recovery fixtures have zero differences/skips.

| Check | Result | Elapsed seconds |
| --- | --- | ---: |
| `test` | PASS | 31.75 |
| `verify-stage1` | PASS | 71.93 |
| `oracle-test` | PASS | 2100.85 |
| `verify-threejs-core` | PASS | 107.49 |

`verify-native-release-recovery` also passes in 28.11 seconds with the
native `-O3 -flto` settings: all 68,294 instruction fixtures and 158,088 bus
phases match, with zero differences/skips and the complete PC/branch census.
Its log is `build/workflow/20261011T132936.239250Z/`.

The 781-frame session checks exact native RGB/audio, pause/resume and cold
restart equality. The optimized core checks both retained whole-host corpora
with zero supplied execution/state/read seeds and zero differences/skips.

The final native sweep passes **23 / 23 ordered ROM stage slots**, each
with 600 completed frames (~10.06 seconds) and all eight inputs delivered
for 75 frames. The ignored log is `build/oracle/stage_input_sweep_final.log`;
its `RESULT`/`COVERAGE` lines and local JSON report preserve the observations.
The board starts fresh for each selected debug entry. Roll counts mean a
nonzero roll angle during that gesture segment, not a completed roll or
verified direction. Weapon counts are original allocator/initializer entries;
zero means the input did not activate that routine in this finite window.

| Stage slot | Roll-angle frames left / right | Vulcan allocator entries | Missile initializer entries |
| ---: | ---: | ---: | ---: |
| 1 | 57 / 55 | 19 | 5 |
| 2 | 55 / 57 | 18 | 5 |
| 3 | 0 / 0 | 6 | 5 |
| 4 | 55 / 42 | 0 | 0 |
| 5 | 0 / 0 | 18 | 2 |
| 6 | 55 / 44 | 0 | 0 |
| 7 | 55 / 57 | 18 | 5 |
| 8 | 0 / 0 | 0 | 0 |
| 9 | 0 / 0 | 6 | 5 |
| 10 | 55 / 40 | 0 | 0 |
| 11 | 0 / 0 | 6 | 5 |
| 12 | 55 / 57 | 18 | 3 |
| 13 | 0 / 0 | 18 | 2 |
| 14 | 55 / 57 | 13 | 0 |
| 15 | 55 / 57 | 18 | 1 |
| 16 | 0 / 0 | 6 | 5 |
| 17 | 0 / 0 | 0 | 0 |
| 18 | 55 / 57 | 18 | 2 |
| 19 | 0 / 0 | 6 | 5 |
| 20 | 55 / 40 | 0 | 0 |
| 21 | 0 / 0 | 6 | 3 |
| 22 | 55 / 40 | 0 | 0 |
| 23 | 0 / 0 | 18 | 5 |

Native selected-stage CLI startup also passes slots 8, 19 and 23 with two
displayed frames using SDL dummy video/audio, supplementing the earlier
argument validation and normal startup checks. Logs are local under
`build/oracle/native_stage_cli_final/`. F5 calls the shared tested startup
helper; interactive keyboard F5 was not separately exercised.

These instruction fixtures and directed stage inputs do not establish natural
stage progression, accumulated prior-stage state, complete-credit MAME RGB/
audio equality, alternate DIP settings, arbitrary reset/suspend states or
60 FPS. The independently accepted finite whole-host corpora and their owner
mutations remain required; full-credit expansion remains Phase 3e work.


## SDL3 native platform migration (2026-10-11)

The native entry point and Make/CMake dependency discovery now use SDL3
(minimum 3.2). Local verification used SDL 3.4.16. The migration follows the
[official SDL3 guide](https://wiki.libsdl.org/SDL3/README-migration) and
[playback stream API](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream):
boolean success returns, `SDL_EVENT_KEY_DOWN`/`event.key.key`, boolean keyboard
state, renderer selection/vsync, letterboxed logical presentation, nearest
texture filtering and an owned playback `SDL_AudioStream`. Source PCM remains
signed 16-bit stereo at `GameSession::sample_rate`; SDL handles conversion to
the physical output device. Device pause/resume, input-side queue limits,
restart clearing and no-speaker fallback remain application responsibilities.
No gameplay instructions, emulated clocks, native RGB or PCM production change.

Both Make builds (`native-release`, `all`) pass in workflow directory
`build/workflow/20261011T133452.996557Z/`. The executable links `libSDL3.0.dylib`.
`test` passes in 34.50 s and `verify-stage1` in 43.04 s under
`build/workflow/20261011T133720.508729Z/`, including 781 exact native RGB/audio
frames, pause/resume and restart equality, and the 2,400-frame SDL3 dummy-driver
attract run. A clean Release CMake configure/build also passes under ignored
`build/sdl3-cmake/`; the verification-only CMake tool is installed under
`build/sdl3-tools/`, without modifying system packages.

Additional ignored logs under `build/oracle/sdl3_smoke/` record stage 7 plus
90 frames with dummy audio, 120 frames with an unavailable audio driver, and
120 frames on the native macOS Metal/audio backend. The original application
entry point also passes a scripted SDL3 event smoke: P pause/resume, repeated
P/F5 suppression, F5 selected-stage restart, real stream pause/resume and PCM
queue clearing, key-up handling and Escape exit. Its ignored harness supplies
events and observes SDL calls while invoking the original application code.
This is finite platform validation, not later-stage/full-credit MAME acceptance
or an across-stage 60 FPS claim. The earlier full oracle suite remains retained;
it is not rerun for this platform-only migration, and the repository's required
checks still apply before committing.

### Blue horizon wedge during barrel rolls (2026-10-11)

Investigated the reported blue triangle above the horizon when reversing
horizontal input into a roll. A fresh original-ROM MAME run reproduces a
dark blue wedge beside the tilted horizon (for example, captured frame
1155). It is part of the arcade's rendered image, not SDL frame presentation
tearing. No horizon smoothing or renderer correction is applied.

`AB2_ORACLE_ROLLS=1` selects alternating full horizontal input every 60
frames in the existing physical-input script. Coin/start timing and the
vertical, throttle and weapon phases remain scripted physical controls;
this mode does not patch RAM or video state. Reproduce with:

```
python3 tools/workflow.py run capture-video-rolls oracle-video-rolls
```

The capture takes 160 frames at stride 3 after warm-up frame 960, including
tilted and near-vertical horizon frames. All 160 are pixel-identical through
the native video layer, zero differences and zero skipped. Workflow logs:
`build/workflow/20261011T135331.823981Z/`. A separate ignored mutant changing
the tilemap scroll origin from `$C0` to `$C1` is rejected by the same corpus.
Native full/neutral-throttle roll captures also complete without a host stop;
they are a visual smoke, not an identical-input whole-host comparison.
Captures and diagnostic images stay under ignored `build/oracle/`.
This is focused raster evidence; it does not extend whole-host or full-credit
acceptance. Existing renderer/gameplay code remains unchanged.

### Stage 8 black canyon ground after debug skipping (2026-10-11)

Reproduced with the shared selected-stage startup used by `--stage 8` and
the stage/input sweep. The road's fourteen alternate-gradient words at
`$122F02..$122F1D` remained zero after a fresh stage 1 start. The canyon road
later selects these solid-fill colors, producing black ground below the
yellow sky. Applying the old skip operands in original MAME reproduces the
same black ground: 160/160 captured raster frames match, zero skipped.
That isolates missing debug-startup state rather than a renderer/SDL defect.

The ordered ROM descriptor for the preceding stage supplies its event
program at offset 16 (`$142D0`). Before the stage-advance command 12, stage 7
has command 18 (`$E086`): temporarily select the next stage and prepare its
alternate colors through `$13DF0/$142DC`. Normal IRQs then blend/upload the
gradient. Jumping directly to command 12 omitted that preparation. A directed
MAME replay that executes and settles it restores the canyon ground; its
120/120 captured raster frames also match the native video layer exactly.

`NativeStageSkip::enter` now resolves the preceding stage's actual event
program through the live ordered selectors and runtime ROM descriptor table.
When command 18 precedes command 12, it executes that original event and
waits for the observed `$29C54C` blend flag to set and clear before advancing.
No RGB/color constant is substituted and no CPU instruction or raster
implementation changes. Only explicit debug startup is seeded, as before.

`capture_stage_skip_palette.lua` captures the settled fourteen-word gradient
from independently executing original MAME. `oracle_native_stage_palette`
cold-boots the native board with physical coin/start input, performs selected
startup and compares those exact words. All four relevant entries (5, 8, 13,
17) match: **56/56 words identical, zero skipped**. Workflow logs:
`build/workflow/20261011T140700.255070Z/`. A private mutant bypassing command
18 is rejected with all fourteen stage 8 words different. ROM data, captures,
mutant sources/binaries and diagnostic images remain ignored under `build/`.
The new captures/comparison are retained by `oracle-capture`/`oracle-test`.

The native release rebuild passes. `test` (4.84 s) and `verify-stage1`
(44.99 s), including the 781-frame session RGB/audio comparison and dummy
SDL3 attract run, pass under `build/workflow/20261011T140511.670201Z/`.
The final stage/input sweep passes all 23 entries with 600 input frames each
(13,800 total, zero stops), under `build/workflow/20261011T140418.158837Z/`.
Its `COVERAGE` lines retain actual roll/weapon activation counts after the
changed debug preparation; completed input frames do not imply active weapons
or a completed roll in every slot. The Release CMake build also passes, and
the rebuilt SDL3 app completes `--stage 8 --frames 360` with dummy drivers.
This directed palette test does not establish natural stage progression,
the previous stage's complete accumulated state or whole-credit RGB/audio
acceptance. Full required checks still apply before committing.

### `play-native` publication validation (2026-10-11)

The SDL3 platform changes, roll-capture tooling and selected-stage road
palette correction are retained on `play-native`, without merging their
implementation into `main`. Required `test` and `verify-stage1` pass under
`build/workflow/20261011T140511.670201Z/`; the complete `oracle-test` passes
in **1700.13 s** under `build/workflow/20261011T141334.295380Z/`.
This latest run includes all four new selected-stage gradient comparisons
(56/56 original-MAME words identical, zero skipped), all 85 recovery
mutations and every earlier fixture. Historical input-race skips remain
explicit in their respective routine reports; none is counted as a match.
All 23 directed stage/input cases complete 600 frames each without a stop.
These checks retain the documented finite acceptance scope; they do not
establish natural full-credit progression or an across-stage FPS guarantee.


### Natural physical recordings and transition recovery (2026-10-11)

`--record` records normal SDL play from cold boot as `AB2PHYS1`: physical
board-frame indices, active-low buttons and all eight ADC bytes. Pause
produces no rows; F5 creates a separate cold `.restartN` recording. Stage
skips are refused when recording. The frame count is flushed after each
attempt, retaining a failing frame rather than silently losing it. JSON
sidecars retain switch bytes, fresh persistent-state declaration and loaded
ROM-region length/FNV-1a identifiers. The discovery input generator also
retains original ROM-file SHA-256 identifiers and request hashes.

`natural_credit` cold-boots `XBoardGame` and supplies only physical pins and
DIP bytes. It records every completed frame's time, game mode `$29C09C`,
ordered stage index `$29C060`, lives `$FFFF88E0`, damage-controller state
`$FFFF9124`, raw score longword `$FFFF8884` and packed credit byte
`$FFFF8052`, carrier selector `$FFFF90B0` and refuel selector `$FFFF90E0`.
`$103F2/$1624E` dispatch on those selectors in the original ROM. The mode/stage/lives producers are the existing ROM game-flow
controllers; `$17610` reads the score for ranking and `$E1BA` reads credits
for scaled starting lives. These observations read backing RAM, never timed
bus transactions. A passive phase observer separately retains the first
carrier `$10348`, refuel `$16236`, shared helper `$E224`, stage advance `$DFC4`, lives `$EC78`
and weapon `$9ED8/$9F7C` producer entry per stage. Entry is not proof of a
completed action or sequence. A stop records the exact unported PC, last
completed frame, attempted input frame and last main/sub instruction PCs;
after each fix the entire affected prefix is replayed from cold boot.

The MAME script uses fresh NVRAM/config via `capture.sh`, applies physical
controls and reads the same observation words. MAME input-field updates have
latency: preserve requested controls separately and replay its dense actual
port recording (`.mame.csv.inputs`) in native. This is an input observation,
not a game-state or execution seed. Unsupported service/ADC inputs are
rejected. `check_natural_credit.py` requires equal timestamps and all nine
state/frame fields on **every** declared frame, including unchanged frames;
missing, extra, shifted or changed intervals fail. Five deliberate mutations
reject. `oracle-natural-ending` additionally requires ordered flight slots
0–22, ending modes 17–22, ranking 25/26 and return 0/1. Five semantic mutations
remove a stage, ending mode, attract return or terminal carrier/refuel
selector while preserving equal CSVs;
the ending-path gate rejects each. Commands and formats are in WORKFLOW.

Natural attempts exposed literal missing game-over `$133A8..$13628`, ending
mode/display controllers, credit-scaled lives/start/display/graphics edges,
attract continuations, exhausted object-pool return, scripted object setup
`$F54/$FB0` and callback `$1020`, scripted aircraft `$1882` and callback
`$18EA`, and attract screen/motion controllers
`$D11A..$D12E/$D4BA..$D54E/$BA38/$C41A/$BC1E`, plus stage object
initialization `$4392` and its literal `$4470` callback, stage-9 motion
selectors `$34FA/$3502/$350A/$3512`, enemy turn `$338C/$33B4`, and
later `$68D2/$7334/$121D0` event/flight/object edges. Only missing PCs and their
verified ROM successors/callbacks were appended. `$FB0` literally publishes the
`$1020` callback; ROM data tables remain runtime reads. The generator now
accepts ST with absolute-short addressing for `$D6D2`; existing instruction
bodies remain unchanged. Every new group receives an independent MAME
instruction corpus and rejected constants/branch/callback mutations before
native replay. All older fixture sources are retained when merging corpora.

The `$120A` longword-table fixture initially used the generic synthetic odd
D1=17 and entered a separate address-error exception. `$1204/$1206/$1208`
shift and double this index twice on the real path; normal fixtures now use
aligned D1=16, retaining both CCR extremes and the same fixture count. The
failed odd-address discovery capture is preserved locally. Address-error
execution for that synthetic operand remains outside this normal port's
fixture contract; it is not counted as a passing fixture or skipped interval.

The recovery union currently checks **12,382 PCs, both outcomes at 1,252
branches and 77,786 original-ROM fixtures**, all identical with zero skips.
The 10,525 earlier instruction bodies are byte-identical; 1,857 PCs were
appended. The complete recovery mutation plan has 120 retained mutations;
the 35 additions have each rejected against their independently passing
incremental corpora. The original 14,014-case baseline and 54,280-case
pre-natural complete capture are retained, along with every passing increment;
the expanded complete capture contains 63,772 unmodified fixture records.
Recovery logs: `build/workflow/20261011T184312.125153Z/`.
The `$BC22` mask selects exactly four `$BC2A` branch-table
offsets; all four are captured independently alongside the retained generic
fixtures. Literal record-handler callbacks beyond RTS are included; data
tables are still runtime ROM reads. The CMake target is
provided beside the existing ROM-dependent tools; CMake is unavailable in
this shell, so its new definition has not been compiled here.

Passive PC discovery uses a new optional bitmap in the isolated MAME
scheduler probe, enabled only by `AB2_PC_CENSUS`. At each original 68000
instruction-entry hook it marks the physical even PC in an observer-owned
32-bit-word array. It does not log every instruction, change any CPU state,
issue emulated bus reads or alter virtual time. Lua exports `.main.pcs` and
`.sub.pcs` at the declared endpoint. 32-bit words avoid Lua's unsigned-64-bit
representation limit; incomplete/failed captures never count as evidence.
The short smoke includes the actual main/sub reset PCs and all three main
interrupt-vector entries, and leaves every original observation unchanged.

The 12,000-frame neutral census observes **14,229 main / 900 sub sites**;
the 74,452-frame ending census observes **16,985 main / 1,031 sub sites**.
Their complete physical pins and all ten frame-observation columns match the
ordinary MAME references exactly. The census closes discovery gaps beyond
native's first stop; both relevant direct branch outcomes and literal record
callbacks are translated/tested independently before replay. All observed
sites now have existing or new mapped code, including the earlier manually
ported RTE/TRAP handlers. This is an executed-PC census, not comparison of
instruction state, bus phases, scheduling, devices, pixels or audio.
Census logs: `build/workflow/20261011T182942.763340Z/` and
`build/workflow/20261011T183105.744844Z/`; smoke:
`build/workflow/20261011T182740.850574Z/`.

Default-switch (`FF/FF/DD`) continue and weapons/control recordings match
**10,000/10,000** and **8,000/8,000** frames respectively, zero differences or
skips. The continue recording observes zero lives at frame 4691, damage state
11 at 4815, restored three lives at 4847, continued stage progression, game
over 8117, ranking 8495 and return to attract 8814. The weapons/control run
observes changing raw score, death/lives transitions, stage progression,
game over 5013, ranking 5391 and return to attract 5710. It does not establish
collision causes or every requested weapon action. Both complete streams
reject all five observation mutations. Replay logs are under
`build/workflow/20261011T172459.286194Z/` and
`build/workflow/20261011T172459.298695Z/`; comparison/mutation logs are under
`build/workflow/20261011T172934.042736Z/` and
`build/workflow/20261011T172934.936724Z/`.

The default-switch neutral failure recording now passes **12,000/12,000**
frames with all ten columns identical and zero skipped. It covers three
lives exhausted at 4691, game-over modes 23/24 at 5373/5375, ranking
25/26 at 5749/5751, return to attract 6068/6070, subsequent attract modes
2–11, reset to mode 0 at 10932 and the next demo start at 10934. No failed
prefix is counted as complete acceptance. Its final replay log is
`build/workflow/20261011T184425.302039Z/`; comparison/mutations:
`build/workflow/20261011T184718.531179Z/`.
The continue and weapons streams also pass after adding both sequence
selectors: `build/workflow/20261011T181351.982436Z/` and
`build/workflow/20261011T181352.797006Z/`. All earlier named observations
and physical pins are unchanged by the expanded MAME capture.

An uninterrupted MAME `credit-stock` run, with Easy / 3× Credits switches
`FF/FF/8D`, receives 100 physical coin pulses before Start, accumulates the
capped 99 credits and begins with 297 lives. It naturally visits all 23
ordered flight slots, reaches ending mode 17 at
frame 71347, progresses through modes 18–22, ranking 26 at 72813 and returns
to attract at 74452 with 221 lives. No RAM patches, invulnerability, stage
skips or replacement CPU state are used. The full 90,000-frame discovery
request/reference is preserved. Its declared ending window is the complete
74,452-frame prefix, exported with source/request/reference hashes and ROM
metadata. **This unusual multi-credit setting does not establish standard
three-life, single-credit completion.** The complete native window now passes
**74,452/74,452** frames, all ten columns identical, zero differences/skips.
The ending-path assertion and all ten observation/semantic mutations reject
wrong/missing data. Replay: `build/workflow/20261011T184425.280719Z/`
(923.72 s); comparison/mutations: `build/workflow/20261011T190037.588224Z/`.
The native path observes all 22 ordinary stage-advance entries, carrier
initialization at completed frame 2831 and refuel entry in ordered slots
2, 8, 10, 15, 18 and 20. Each refuel slot samples the complete ordered 0–7
selector progression, matching MAME frame for frame. The initial carrier
progresses through 0–12, with 12 first visible at return to flight. There
are 76 life decrements during flight, leaving 221 lives at the endpoint.
Carrier states 13–15 remain directed fixture coverage; they were not
observed in these natural recordings.

The SDL dummy video/audio desktop recorder smoke completes 120 cold-board
frames. Its declared count, dense frame indices, seven loaded ROM-region
fingerprints and fresh-state metadata are valid; its physical pins match
the MAME actual-input stream. Native replay and ordinary MAME match all ten
observation columns for 120/120 frames, zero skipped, and all five
observation mutations reject. Local artifacts use the
`build/oracle/native_recorder_final` prefix.
The final native release rebuild passes in 42.44 s:
`build/workflow/20261011T193032.556916Z/`.

The first full required run passes `test` (34.89 s) and `verify-stage1`
(79.10 s), then stops in the recovery mutation harness after 116 successful
rejections: the old `attract-st` substring now matches five literal ST
instructions. Its selector is anchored to the unchanged original `$C504`
body, retaining the same mutation and fixture coverage. All mutation
selectors are now checked before expensive builds. The final four retained
mutations reject in a focused rerun. The clean complete `oracle-test` rerun
passes in **2270.84 s**, including all **120 recovery mutations** and all
**15 whole-host ownership mutations**:
`build/workflow/20261011T193215.016295Z/`.
The first run is retained at `build/workflow/20261011T184425.284224Z/`;
the focused completion is `build/oracle/natural_mutation_selector_check.log`.

These finite observation comparisons do not establish continuous full-credit
CPU/bus/device/RGB/audio identity. High-score name entry, NVRAM persistence,
additional DIP trajectories and reset/suspend acceptance remain open. The
retained 121/603/781-frame whole-host and earlier routine oracles still apply;
older documented input-race skips are not erased by zero-skip recovery or
observation results. The required `test`, `verify-stage1` and complete
`oracle-test` results above are all passing. No ROM bytes or capture files
are added to the tracked changes.

### Multi-credit continue display stop at `$C618` (2026-10-11)

Live play reported `unported caller instruction at PC 50712` at a stage-4
continue screen. Decimal 50712 is `$C618`, the ROM's `ADDQ.L #2,A0` in
the `$C600` credit display controller. `$C60E/$C614` select it when the
credit byte is at least two; the earlier one-credit continue recording
never reached this path. `$C624/$C62A` then select the plural-credit text
descriptor at `$C63C`, with `$C642` calling the existing `$137D2` text
renderer. The fix appends exactly these three literal instructions.
All 12,382 existing recovery instruction bodies remain byte-identical.

A cold-board physical-input reproduction adds two coin pulses to the earlier
continue sequence. The old native build stops at the same `$C618` after
7,436 completed frames, ordered slot 2, with two remaining credits. This
reproduces the instruction failure, not the user's exact stage-4 inputs.
The retained discovery artifacts use `build/oracle/continue_multi_before`
and `build/oracle/continue_multi_requested.inputs`.
The reusable `continue-multi-credit` scenario supplies three coin pulses
at requested frames 4800–4811, 4824–4835 and 4848–4859, with the existing
Start pulse at 4860–4871. It uses the default `FF/FF/DD` switches and
10,000 frames, with no game-state injection. Native replay uses MAME's
actual observed physical pins, preserving input-update latency.

Independent original-ROM fixtures at `$C618/$C63C/$C642` exercise both CCR
extremes: **6/6 identical, zero different/skipped, 16 bus phases**.
All three new pointer/text/call-return mutations reject after a passing
baseline; logs are `build/oracle/continue_credit.verify.log` and
`build/oracle/continue_credit.mutations.log`. The original complete corpus
is preserved as `cpu_recovery_before_multi_credit.bin` before merging
the six unmodified records. Complete recovery now passes **12,385 PCs,
1,252 branches with both outcomes and 77,792 MAME fixtures**, zero
differences/skips (`build/workflow/20261011T202618.505240Z/01-oracle-recovery.log`).
All 120 earlier mutation definitions remain, alongside the three new ones;
their last full run is the preceding checkpoint, not a new 123-mutation run.

Focused `test` (48.02 s) and `verify-stage1` (78.07 s) pass at
`build/workflow/20261011T202613.753296Z/`. The native release rebuild passes
in 40.99 s and headless replay rebuild in 30.95 s at
`build/workflow/20261011T202719.311646Z/`; the 120-frame SDL dummy smoke
also exits successfully. The affected natural replay completes all
10,000 frames in 111.74 s (`build/workflow/20261011T202840.460216Z/`).
All ten timestamp/RAM columns match ordinary MAME frame for frame, zero
differences/skips, and all five observation mutations reject
(`build/workflow/20261011T203052.179492Z/`). The former `$C618` stop is
crossed with two remaining credits; this window also completes game-over,
ranking and the return to the ROM's credit/start modes. The complete directed
stage sweep passes all 23 ordered slots, 600 input-window frames each,
**13,800 frames with no stops** (459.04 s,
`build/workflow/20261011T202719.311646Z/03-stage-input-sweep.log`). This
retains the sweep's debug-entry/discovery limits. Full merge/milestone gates
remain governed by the validation cadence; unrelated long ending recordings
are not repeated for this local literal-port fix.

### Native presentation settings

The SDL frontend now provides a ROM-font Start/Options/Quit menu and presentation
settings for SDL scaling, fullscreen and statistics. This is separate from the
translated board and its native raster. Menu pause restores prior pause state,
clears wall-clock debt on transitions and suppresses held menu controls until
released. Preferences are frontend-owned and versioned; no ROM data is stored
in source. Focused validation uses `test-native-settings` and `verify-native-menu`
(persistence, SDL option application, navigation and board frame/time isolation).
Commands and controls are in WORKFLOW's Native settings menu section.

Validation: `build/workflow/20261011T210054.925466Z/` records native release,
menu integration, `test` and `verify-stage1` passes (including 781 exact session
RGB frames and identity/processed audio). Final presentation-only cursor changes
were rebuilt and checked by `verify-native-menu` in
`build/workflow/20261011T210217.486274Z/`. A 120-frame dummy-video, unavailable-audio
smoke completed with measured simulation around 59.6 FPS; the Options preview is
`build/oracle/native_options.png`. A macOS desktop launch succeeded outside the
sandbox, but computer-use inventory could not bind the standalone executable,
so desktop keyboard/fullscreen interaction remains a manual check. CMake target
wiring was inspected; CMake is unavailable here. No whole-host oracle acceptance
is claimed for these frontend checks; the retained continue-screen port evidence
above is separate.

### Stage-1 full-throttle/cannon report at `$14D62` (2026-10-12)

Live play reported decimal PC 85346 while holding Space and Z (full throttle
and cannon in the SDL frontend). `$14D62` is `MOVEQ #10,D0`, the fallback
following `$14D60`'s signed-positive branch in the weapon allocation/launch
path. When the computed word is nonpositive, the original ROM replaces the
whole D0 register with ten, clears N/Z/V/C, retains X and continues at `$14D64`.
Exactly one literal recovery instruction is added; all preceding 12,385 bodies
are unchanged, including the earlier continue-screen fix.

Independent MAME fixtures exercise both CCR extremes with a nonzero high D0
word: **2/2 identical, zero different/skipped, two bus phases**.
Changing ten to eleven or retaining old condition flags is rejected after a
passing baseline (`build/oracle/stage1_weapon.verify.log` and
`stage1_weapon.mutations.log`). The previous complete corpus is preserved as
`cpu_recovery_before_stage1_weapon.bin`, and the unmodified new fixtures are
merged into the retained corpus. Complete recovery passes **12,386 PCs,
1,252 branches with both outcomes and 77,794 fixtures**, zero differences/skips.
The capture log is `build/workflow/20261011T210707.406664Z/`; complete replay
and coverage are in `build/workflow/20261011T210837.464019Z/01-oracle-recovery.log`.
All previous mutations remain; this focused repair does not claim a new full
125-mutation run or whole-host acceptance.

The reusable `cannon-boost` discovery scenario holds cannon and physical
throttle 255 from frame 480, with default `FF/FF/DD` switches and the existing
cold-boot coin/Start pulses. A 3,600-frame pre-fix run completes; it does not
reproduce the user's exact trajectory or demonstrate natural execution of the
faulting fallback. The missing instruction itself is independently verified
above. The matching ordinary MAME capture uses fresh persistent state and
emits actual physical pins for native replay, preserving input update latency.
Artifacts are under ignored `build/oracle/stage1_cannon_boost*`.

Focused `test` (37.82 s) and `verify-stage1` (79.36 s) pass at
`build/workflow/20261011T210837.464019Z/`. Native release (41.78 s) and replay
build (28.51 s) pass at `build/workflow/20261011T210910.539930Z/`; the rebuilt
release also completes a 120-frame dummy-video/unavailable-audio smoke.
Replay of MAME's actual pins completes all 3,600 frames in 40.20 s at
`build/workflow/20261011T211025.138111Z/`. All ten timestamp/RAM columns match
ordinary MAME frame for frame, **3,600/3,600 identical, zero skipped**; all five
observation mutations reject at `build/workflow/20261011T211115.728986Z/`.
This validates the bounded scenario observations, not full CPU/device/RGB/audio
identity or recreation of the user's fault trajectory. The native executable
contains the new port and is ready for manual retry; see the daily main
checkpoint below for commit validation.

### Ground-target award queue stop at `$E570` (2026-10-12)

Live play reported decimal PC 58736 after shooting the first ground object in
the first canyon. `$E570` is `SUBQ.B #1,($904E).W`, reached from `$E568/$E56C`
when the award queue at `$904F` is nonempty. Earlier input coverage had verified
the empty-queue return but not this consumer. The connected `$E570..$E5AE`
path adds exactly 18 literal instructions: countdown/borrow dispatch, queue pop
and compaction, signed entry/count extension, table index, score publication
through `$CD12`, sound command through `$B84C`, queue length decrement and
selection of the existing display rebuild/clear path. Original ROM tables are
read at runtime. All preceding 12,386 recovery bodies remain unchanged.

The independent fixture increment covers all 18 PCs, both outcomes of its two
conditional branches and DBF, plus countdown/queue-count byte values 0, 1,
$7F, $80 and $FF with both CCR extremes and adjacent-byte sentinels.
**146/146 fixtures identical, zero different/skipped, 356 bus phases**
(`build/oracle/ground_award.verify.log`). The capture and build log is
`build/workflow/20261011T211806.777457Z/`. The previous complete corpus is
retained as `cpu_recovery_before_ground_award.bin`; the new records are merged
without modification. Complete recovery passes **12,404 PCs, 1,255 branches
with both outcomes and 77,940 fixtures**, zero differences/skips at
`build/workflow/20261011T211945.740666Z/01-oracle-recovery.log`.

The `ground-awards` physical-input discovery scenario uses the existing Easy /
99-credit stock setup, starts through physical coin/Start pulses, then varies
stick position while holding full throttle/cannon and pulsing missiles. It
runs 12,000 frames with fresh persistent state and no game-state injection.
It attempts ground targets; the scenario name is not proof of a successful hit.
Ordinary MAME records actual physical pins for native replay. Captures,
observations and replay logs remain under ignored `build/oracle/ground_awards*`.

Focused `test` (50.01 s) and `verify-stage1` (93.75 s) pass at
`build/workflow/20261011T211945.740666Z/`; the latter retains 781 exact session
RGB frames and identity/processed audio. Native release (48.29 s) and replay
build (36.32 s) pass at `build/workflow/20261011T212146.758307Z/`, followed by
a successful 120-frame dummy-video/unavailable-audio smoke.

The pre-fix 12,000-frame discovery window completes without reproducing the
reported stop. Its passive MAME census (`build/workflow/20261011T212103.991541Z/`)
contains 11,040 main/758 sub instruction sites and none of the new consumer
PCs (nor `$E542`'s producer). Thus the run does not establish a ground-target
award or reproduce the user's canyon trajectory. Independent instruction
fixtures above verify the nonempty consumer. For a manual retry,
`./build/afterburner2-release --record build/oracle/manual_ground.inputs`
retains physical input rows, including a failing attempted frame, if another
path is exposed. ROMs and recordings remain ignored.

All **18 new instruction mutations reject** after the passing MAME baseline
(`build/oracle/ground_award.mutations.log`), covering countdown, branch, queue
addresses/increments/copy/loop, signed extension, table indexing, score
publication/call return, sound command/call return and display/empty-queue
selection. The previous 125 recovery mutation definitions remain, for 143 total;
this focused repair does not claim a fresh full mutation or whole-host run.

Final native replay of ordinary MAME's actual pins completes all 12,000 frames
in 138.42 s (`build/workflow/20261011T212331.787151Z/`). All ten timestamp/RAM
columns match, **12,000/12,000 identical, zero skipped**, and all five observation
mutations reject (`build/workflow/20261011T212602.105326Z/`). These bounded
observations preserve the census limits above; they do not establish natural
award-consumer execution or full CPU/device/RGB/audio identity. The rebuilt
native executable includes the connected consumer and is ready for manual
retry. See the daily main checkpoint below for commit validation.


## Sprite-only native presentation filters (2026-10-12)

The native Options menu now has a separate Sprites row: Off, EPX 2x, Scale3x,
2xSaI, Super2xSaI and ScaleFX 3x. Off remains the default, with no layer capture.
GameSession's opt-in observers copy the existing pre-sprite background and
priority-resolved visible sprite plane; they read the native hardware raster
without rerendering its scaler or writing feedback. Native frame RGB and audio
remain the reference. Filtered sprite coverage is composited over nearest-scaled
backgrounds; native foreground tile/text coverage and palette shadows are
protected. Sprite-based scenery also participates. Final SDL window scaling
remains independent; Linear can intentionally soften the completed image.

EPX/Scale3x implement published ScaleNx rules. SaI preserves Snes9x's rule order,
with clamped borders and premultiplied ARGB8888 interpolation; its noncommercial
license/attribution and unchanged RGB565 reference are retained. ScaleFX adapts
all five MIT-licensed upstream GLSL passes with float intermediate planes,
default thresholds and a documented opaque/transparent alpha distance. This is
not an approximate EPX mode labelled ScaleFX. xBRZ remains unavailable because
its published GPL implementation's named exceptions do not include this project;
no incompatible source or project relicensing was introduced.

Validation on the final filter/session tree:

- `test` (4.69 s), `verify-stage1` (45.67 s), `verify-native-menu` (32.54 s), and
  native optimized build (33.03 s): `build/workflow/20261011T214857.371883Z/`.
  The session check compares all 781 native RGB frames, reconstructs each from
  presentation planes, compares original/processed audio, and exercises late
  capture enable/disable while paused plus restart observer rebinding.
- ROM-free filter checks cover transparent/solid borders, ScaleNx corners,
  premultiplied alpha, protected foregrounds, invalid planes and input isolation.
  Both SaI modes match unchanged upstream RGB565 routines on 12 synthetic inputs.
  ScaleFX fingerprints retain six independently rendered synthetic GLSL outputs.
- The offscreen macOS OpenGL reference check compares 6,912 output pixels on
  each of six synthetic inputs: **41,472/41,472 identical, zero skipped**. Four
  use unmodified upstream GLSL; two apply only the same documented alpha metric.
  The optional `verify-scalefx-reference` target reproduces this comparison.
- Cold and expanded whole-host MAME checks pass (45.39 s and 46.28 s):
  `build/workflow/20261011T215028.942578Z/`. They retain 603/781 native frames,
  43,223,040/55,982,080 RGB pixels, CPU/bus/device/audio and bank/control checks,
  with zero differences/skips. This is focused validation, not a fresh complete
  oracle/mutation or full-credit acceptance run.
- The SDL application renders every filter using dummy video/audio after the
  normal 600-frame physical boot and two subsequent frames. All six runs exit
  successfully; every filtered image differs from Off, while all 100,800 pixels
  of the top HUD test region are identical to Off. Captures and configuration
  files are ignored under `build/oracle/sprite-*`. This does not claim every HUD
  layout or full-game presentation has been inspected.

Synthetic full-frame CPU scaling/composition measured approximately 0.75 ms
EPX, 2 ms Scale3x, 1.2 ms 2xSaI, 1.4 ms Super2xSaI and 12 ms ScaleFX here;
these finite timings exclude gameplay and do not guarantee 60 fps. Detailed
stats now report filter/upload time. The app reuses the filtered texture across
unchanged physical frames, including pause. Source/license provenance is in
`src/main/frontend/scalers/README.md`. Captures are not committed.

The final rebuilt executable also completes a 120-frame native desktop smoke
using SDL 3.4.16's Metal renderer with ScaleFX and vsync active (log:
`build/oracle/sprite-scalefx-metal.log`). Its two one-second windows measure
55.19/59.52 simulation FPS and 27.60/29.76 display FPS, with approximately
10.7 ms/core frame. This exposes a current presentation cost: ScaleFX can retain
near-native simulation cadence while displaying fewer frames. EPX is the lighter
starting option; these measurements are not sustained full-game benchmarks.
The final cleanup-only optimized rebuild passes in 31.88 s at
`build/workflow/20261011T215153.459580Z/`.


## Daily main checkpoint (2026-10-12)

This checkpoint combines the three reported host-stop repairs (22 new literal
instructions), the ROM-font settings menu, persisted SDL display/statistics
preferences, and the optional sprite-only presentation path with five filters.
The README, roadmap, workflow and enhancement contracts now describe the launch
controls, completed work, filter licenses/performance and remaining acceptance
limits. No ROM assets, captures, screenshots or local validation logs are included
in the commit. xBRZ remains pending compatible licensing; CPU ScaleFX's measured
presentation cost remains a documented limitation.

The complete `required` profile passes on this checkpoint: `test` 4.74 s,
`verify-stage1` 45.51 s and `oracle-test` 2653.29 s, at
`build/workflow/20261011T220049.037304Z/`. All **143 retained recovery mutations**
and **15 whole-host ownership mutations** reject, alongside the other retained
oracle mutation checks. The recovery corpus retains 12,404 PCs, both outcomes
at 1,255 branches and 77,940 fixtures. Existing routine-level input-race skips
remain documented (for example, 1,198/1,200 sub-sprite passes identical with two
input-change skips, and 937/1,200 scenery records identical with 263 input-change
skips in one older corpus); they are not erased by zero-skip whole-host/recovery
checks. This main-push gate does not close standard single-credit completion,
whole-credit CPU/device/RGB/audio acceptance, high-score entry or NVRAM coverage.

Whitespace-only normalization of the imported/adapted scaler sources is followed
by `test-sprite-filters` (3.32 s), `verify-native-menu` (0.48 s), and optimized
native build (36.46 s), all passing at
`build/workflow/20261011T220319.186745Z/`. Shader reference and native Metal
smoke evidence are recorded in the preceding filter section. The engine/oracle
sources and captures are unchanged by this formatting cleanup.

The complete ROM-free `test` target is rerun after the scaler formatting cleanup
and passes in 4.31 s at `build/workflow/20261011T224559.697101Z/`. The final staged
patch passes `git diff --check`; publication excludes ROM-derived outputs.

## SDL GPU sprite filters and branch-only Three.js (2026-10-12)

The native app now creates an SDL GPU device and SDL 3.4 GPU 2D renderer.
All five sprite filters (EPX, Scale3x, 2xSaI, Super2xSaI, ScaleFX) run in GPU
compute passes, including protected background/premultiplied-alpha composition.
The app uploads native background/sprite/protection planes; intermediate and
scaled output data stay on the GPU. SDL wraps the output texture for presentation
and existing menu/scaling controls. Ordinary playback performs no GPU readback.
Original CPU board rendering, sprite feedback, game scheduling and audio owners
remain authoritative. Unsupported/dummy-video systems report an explicit
CPU/software fallback; retained CPU/upstream filters and fixtures remain intact.

Shader sources and embedded SPIR-V/Metal source are regenerated with
`make shader-build` (`glslangValidator`, `spirv-cross`). Normal builds need only
SDL 3.4 and the embedded header. Metal is physically validated here; Vulkan
SPIR-V is supplied but untested on a physical Vulkan device, and DXIL/D3D12
shaders are not included. These limits are not whole-host acceptance claims.

The real native-plane check exposed a ScaleFX float boundary missed by the
synthetic cases. CPU contraction at optimized build settings changed edge
choices; Metal's approximate division could also alter them. ScaleFX now
explicitly disables CPU contraction locally, Metal disables contraction and
reassociation and uses precise sqrt, and shaders refine division through an
explicit fused residual. This leaves board arithmetic/compiler options alone.
All four corrected GPU intermediate planes matched the CPU reference on the
retained failing frame; upstream GLSL comparisons and earlier fingerprints pass.
The original native-plane fixture stays ignored at
`build/oracle/gpu-native-planes.bin`; the native GPU test recreates it if absent
and retains separately named future failures without replacing old captures.
Its layout is two 320x224 ARGB8888 planes followed by 320x224 protection bytes.

Focused validation:

- Final `test` (4.49 s) and software `verify-native-menu` (0.31 s) pass at
  `build/workflow/20261012T111352.419948Z/`. `verify-stage1` (45.64 s) passes at
  `build/workflow/20261012T105053.305451Z/`; its engine/session sources and
  transitive dependencies are unchanged by the subsequent presentation repair.
  The Stage 1 result is reused evidence, not a fresh full oracle run.
- Final CPU sprite references/fingerprints pass at
  `build/workflow/20261012T110906.881707Z/`; optimized native build and software
  menu checks pass there too. Independent upstream GLSL verification passes at
  `build/workflow/20261012T111002.954970Z/`.
- GPU synthetic comparison: 250 of 250 identical, 21,755,400 pixels, zero
  skips; wrong filter selection and missing priority protection both reject.
  Native cold boot with physical coin/start, then stick/cannon inputs, advances
  1,081 frames and compares all five filters at 17 sampled frames: 85 of 85
  presentations identical, zero skips. GPU menu checks cover scaling, integer
  presentation, fullscreen, pause/resume and frame/time isolation. Logs:
  `build/workflow/20261012T111208.446310Z/` (final retention/replay run). Full-credit output and every native frame are not covered by this
  sampled presentation check.
- Final generated-source whitespace cleanup also passes the GPU comparison at
  `build/workflow/20261012T111809.480440Z/`.
- Make and CMake builds pass; the CMake log is
  `build/oracle/gpu-cmake-final.log`. Shader regeneration is deterministic.
  Headless ScaleFX fallback passes at `build/oracle/gpu-headless-fallback.log`.

A finite 600-frame post-boot native ScaleFX smoke is retained at
`build/oracle/gpu-scalefx.log`, with a final screenshot under the same ignored
prefix. After startup, its one-second windows report 59.45–59.70 display FPS
and roughly 0.08–0.15 ms filter/upload submission time, versus about 12–14 ms
CPU filter/upload time in the headless fallback check. These are different
presentation environments, not a controlled GPU-kernel speedup measurement. GPU submission/upload statistics measure CPU submission work, not
isolated GPU kernel duration; sustained peak-load and other backend performance
remain unproved. ROMs, fixtures, screenshots and build logs remain excluded
from Git. The complete oracle suite has not been rerun for this presentation
change; the preceding full checkpoint remains the full-game regression evidence.

Three.js source, npm metadata, bridge, browser-only tests/mutations and build
commands have been removed from main at the user's request. All are retained
on `codex/threejs-game-engine`; the local branch was advanced to pre-removal
main (`72910ee`) to retain its latest prototype documentation too. Existing
ignored browser dependencies/caches remain local and root-ignored. Native
release targets now use `NATIVE_CXXFLAGS` and `build/native_obj/` with the same
`-O3 -flto` options instead of borrowing browser-named objects. Original CPU,
ROM and MAME fixture/mutation coverage remains on main; browser-only coverage
belongs to the preserved branch.

## GPU composition with optional cloud/trail opacity (2026-10-12)

GPU composition now runs at native size with Sprites Off as well as through
all five scalers. The app skips the unused composite-frame texture upload when
presenting GPU planes. Options adds independent persisted Clouds and Trails
rows: Opaque is the default; Translucent uses 160/255 and 112/255 opacity,
respectively. These are optional presentation choices, not ROM gameplay rules.
Original board RGB, clocks, CPU/device state, sprite feedback and audio remain
owned by the translated host.

`hwvideo/sprite_scene.hpp` is a passive descriptor decoder, following the
verified hardware renderer's row stepping, zoom, nibble direction, wrapping,
clipping and ordering. GameSession captures per-row fragment chains at the
existing indexed-layer/palette frontier, retaining covered sprites across
partial clips. It issues no bus transactions and never writes word-7 feedback.
Scene views invalidate on advance, capture changes and restart. A GPU compute
pass builds the premultiplied foreground from ordered layers; the existing
filter/background/protection pass finishes composition. The CPU implementation
is the software fallback and independent presentation reference.

Blocked tiles and hardware shadows terminate the stack as background actions;
their colors never enter the foreground scaler's neighborhood. Shadows use the
native effects pen and preserve coverage, including underneath translucent
sprites. Full-opacity scene output matches both native RGB and the preceding
flat-plane scaler output, so activation does not change unrelated opaque edges.

Asset identification is deliberately bounded. ROM frame families `$16500` and
`$16550` were observed in native world objects and visually inspected as clouds.
Player trail allocation reads its frame pointer at main `$28C64`; enemy `$14EC8`
selects the same `$164B0` art. Trail classification additionally requires the
verified player `$E9..$F0` or enemy `$F5..$FC` palette range. All five LOD offsets
come from sub `$9E4`; bank/address extents and pitch come from runtime frame
records. No art, ROM bytes or capture data are embedded. Other cloud/smoke art
families are not assumed to be covered. Original silhouettes/dithering remain;
soft RGBA replacements and age-based trail fading are future work.

Focused validation on Metal:

- `test` (4.72 s), `verify-stage1` (46.07 s), default Make build (0.16 s) and
  software menu (0.30 s) pass at `build/workflow/20261012T115530.716389Z/`.
  The paired session test now enables individual-layer capture and compares all
  781 native RGB/audio frames, capture toggling, pause and restart against an
  independent host. No differences or skipped frames.
- GPU synthetic comparisons: 450 of 450 identical, 33,720,870 pixels, zero
  skips, including zero/partial/full opacity, covered sprites, blocked tiles,
  shadows and all six scaler choices. Four deliberate mutations (filter,
  protection, cloud opacity and trail opacity) reject. Retained native-plane
  fixture replay passes. Logs: `build/workflow/20261012T115529.528144Z/`.
- Native GPU comparisons: 888 of 888 presentations identical, zero skips.
  Across 1,081 physical boot/attract/coin/start/gameplay frames, 37 sampled
  frames exercise all four option combinations and six scaler choices.
  Actual fragment observations include 497,565 cloud and 57,407 trail pixels;
  absence of either asset class fails the check. Original-mode outputs also
  match the previous scalers. GPU menu/lifecycle checks pass at
  `build/workflow/20261012T115307.132335Z/`.
- Make optimized build and CMake build pass. CMake output is retained at
  `build/oracle/gpu-effects-cmake.log`; the final native release build is at
  `build/workflow/20261012T115304.266088Z/`.

A 360-frame attract smoke with both effects and Sprites Off completes on Metal
at roughly 59.3–59.7 displayed FPS after startup, with about 0.38–0.43 ms CPU
filter/upload submission work in the later windows. The passive scene decoder
and packing still run on CPU when effects are enabled. A separate dummy-video
CPU/software smoke with both effects also passes. Logs/screenshots are under
ignored `build/oracle/gpu-effects*`; menu and render screenshots were inspected.
These finite checks do not establish whole-credit visuals, every stage/asset,
peak-load performance or a physical Vulkan backend. The full oracle suite was
not rerun for this presentation extension; earlier ROM/device fixtures and
mutations are retained, and the preceding complete checkpoint remains the last
full-game regression gate. Three.js remains branch-only as requested.

## SDL3 native input remapping (2026-10-12)

`frontend/native_input.hpp` owns SDL3 gamepad/joystick discovery, hotplug and
mapping-change events, keyboard scancodes, neutral-gated axis capture and
GUID/type profiles. `Options → Controls` offers eleven actions (four flight
directions, boost/brake, cannon/missile, coin/start and Menu), clear/reset,
deadzones (0–50%), linear sensitivity (50–200%), polarity and full-axis raw
joystick throttle. Full throttle replaces that device's boost/brake pair and
uses the complete signed axis range; keyboard remains available. Devices without
left-stick/trigger axes receive D-pad/shoulder defaults. Other raw sticks get
explicit generic axis/button defaults, editable for their physical layout.

`AB2INPUT3` preferences are stored atomically beside the native settings file.
Malformed records retain defaults. Identical GUID/type devices share mappings;
lifecycle events preserve the selected connected instance, cancel capture and
require held controls to release. Focus loss returns neutral inputs. Capturing
input cannot swallow the window Quit event. The Menu binding detects press edges;
Guide and reserved keyboard shortcuts remain available. No board logic, arcade
ADC mapping, simulation cadence or recording format changed.

SDL_GameControllerDB commit `c1d5289a1f713b30a2c121e9fe6529d39360be0f`
is included with its zlib license and provenance. The embedded raw-string
wrapper matches the unmodified source text (SHA256
`e606134678e6b3fdbdec9081289a1f0ba3e25d53b59b2aa3cdfe69bf491ad18f`).
SDL loads it offline before frontend device handles are opened.

Focused validation:

- `test`, including SDL virtual analog gamepad, digital-only gamepad and raw
  flightstick: passes (4.28 s), including real live SDL mapping changes,
  axis/button/hat capture, deadzone rescaling/sensitivity, trigger and full-axis
  throttle, Menu edges, focus, reconnect, persistence, malformed config and
  capture/quit handling. Native menu capture/clear/scroll/navigation and
  `native-release` pass (31.83/32.59 s). Logs:
  `build/workflow/20261012T130132.783595Z/`.
- `verify-stage1` passes (52.13 s), including 781 exact native RGB frames and
  audio/game-session lifecycle checks. Logs:
  `build/workflow/20261012T130000.338471Z/`; its engine/session sources are
  unchanged by subsequent frontend capability defaults.
- Final SDL Metal `verify-gpu-native-menu` passes (18.59 s), with the Controls
  panel plus 888/888 native GPU presentation comparisons, zero skips. Logs:
  `build/workflow/20261012T130229.422053Z/`.
- Finite 120-frame native startup passes on Metal with SDL 3.4.16; the final
  interval reports 59.64 simulation/display FPS. Log:
  `build/oracle/input-metal-smoke.log`. The software/dummy startup also passed
  earlier at `build/oracle/input-smoke.log`. These are finite startup checks.
- CMake `native_input_regression`/CTest passes (0.50 s); logs:
  `build/oracle/input-cmake-build.log`. Controls panel screenshots were visually
  inspected; SDL names are capitalized for the ROM font. Captures remain ignored.

Physical Xbox/PlayStation/Switch controllers and specific HOTAS units were not
attached for validation. Their recognition depends on SDL drivers and applicable
platform mappings; device-specific calibration/response curves remain future
work. These are frontend focused checks, not a new full-game MAME acceptance run.

## Native music presentation adapter (2026-10-13)

Replacement mixing correction: decoded files previously entered the speaker
sum at full digital gain, overwhelming the quieter arcade effects and clipping
overlaps with mastered files. The initial 1/16 (-24 dB) adjustment proved too
quiet in user playback. A frontend-only 1/4 (-12 dB) baseline now applies
to replacement playback and preview before user gains/native fades. Original
unity PCM, effects gain, file samples, routing and board clocks are unchanged.
Retained generated full-scale stereo tests check unclipped additive effects,
independent mutes and global/track gains; loop/fade tests include the baseline.
This fixed balance preserves dynamics; it does not normalize arbitrary files.
The preceding 1/16 revision passed `test` and `verify-stage1` at
`build/workflow/20261013T143648.631824Z/`, including 781 exact native RGB/PCM
frames and 627,841 stem frames. The earlier full checkpoint below retains its
original scope; this correction does not claim whole-credit acceptance.
That revision's focused routing/mutation checks passed at
`build/workflow/20261013T143442.451800Z/`: both retained MAME corpora match
42,148/42,148 and 119,374/119,374 observations, zero skips; all five music
mutations reject, including bypass of the new replacement gain.
That revision's native release rebuild and 480-frame software launch with generated
full-scale replacements and both user volumes at 100% pass (ignored log:
`build/oracle/replacement-balance-smoke.log`). Actual user recordings were not
available for listening comparison; quieter mastering may need track adjustment.

Current-song menu mappings now switch on the next frontend mix after successful
asynchronous decoding; menu pause therefore applies them on resume. Replacement
files start at sample zero with the current fade; Original returns to the live
native stem. Inactive mappings and stopped cues do not start/retrigger music.
No sound commands or board execution changes are used. Retained tests cover
Original/file/file switches, inactive edits, All Original, fades and stopped
music. Final 1/4-gain and live-selection `test` / `verify-stage1` pass at
`build/workflow/20261013T145024.004633Z/`, including 781 exact RGB/PCM frames and
627,841 stem frames; preceding revision logs above remain historical evidence.
The rebuilt release passes at `build/workflow/20261013T145512.368422Z/` and
a 480-frame software launch with full-scale generated replacements and both
user gains at 100% passes (`build/oracle/replacement-live-balance-smoke.log`).
Mutation unit playback uses SDL's dummy backend, matching the normal unit
target, so preview assertions do not depend on physical device availability.
The clean final routing/mutation rerun passes at
`build/workflow/20261013T145459.602242Z/`: both retained MAME corpora match
with zero skips and all six music mutations reject, including deferred live
selection and bypassed replacement attenuation.

See [music implementation](MUSIC_REPLACEMENT_PLAN.md) for ROM addresses, routing,
menus and coverage. Passive MAME routing compares 42,148/42,148 transactions in
781 frames and 119,374/119,374 in 1,800 frames, zero skips. The longer physical
input corpus includes 472 actual FM effects channel writes and supports the FM
ownership negative control. Descriptor roots and all sixteen music records are
read from ROM; no captured state supplies native execution. PCM music includes
channels 0–7, including percussion; effects use 8–15. YM projections read the
current chip output without generating a second clock. Optional speaker taps
preserve the original mixed stream and deadline indices.

Natural coverage is Maximum Power/Final Take Off with shooting and overlapping
effects. Other song descriptors are inspected at runtime but their natural
contexts are unverified; no natural fade updates occur in these captures. Fade
and replacement lifecycle have frontend unit coverage. All-Original unity PCM
and RGB are exact across 781 native frames with taps enabled/disabled. Fixtures
and generated audio are retained under ignored `build/oracle/`.

Final validation: `python3 tools/workflow.py run required` passed on 2026-10-13
(`test`, `verify-stage1`, complete `oracle-test`; logs under ignored
`build/workflow/20261013T075232.029672Z/`). All 143 retained recovery mutations
and four new music mutations were rejected. Existing historical per-routine
skip policies remain unchanged; the new music routing and presentation checks
have zero skips. Native speaker comparison retains 484,801 identical stereo
frames, and joint audio/whole-host baselines and their mutations pass.
CMake product/audio/menu builds and four targeted CTests pass. Native release
smokes with saved generated replacements and both gains muted pass on software
and Metal; menu rendering was inspected. Generated tones, preferences, images,
ROM symlink and routing captures remain ignored.

## Explosion and player-flame presentation transparency (2026-10-13)

Optional persisted Explosions and Afterburner rows use 176/255 and 192/255
opacity, respectively; all effects default to Opaque. The Options viewport
scrolls eight rows with stable mouse hit testing. Main ROM $347BE selects ten
explosion animation lists ($90AC/$9306), including gate explosions. Each frame's
palette and sub-ROM descriptor/LOD extent are read at runtime. Aircraft frames
come from five $28C94 pitch tables and banks -5..5. The $14062 palette writer
animates F2 pens 1, 2 and 4; only those pixels in matching aircraft assets get
flame opacity, preserving the airframe. A rendered ROM mask was inspected.
No animation tables, pixel data or captures are embedded in source.

Shader and CPU paths share explicit effect kinds; background and shadow remain
terminators rather than absorbing new effect IDs. GPU uniform data includes
independent opacities. All opaque settings preserve original composition;
original board RGB, sprite feedback and audio remain unchanged. Synthetic Metal
readback checks pass 570/570 cases (42,713,102 pixels, zero skips), covering each
scaler and opaque/zero/translucent overlaps. All six retained GPU mutations are
rejected, including explosion and flame opacity. The 3,601-frame physical-input
native scene corpus counts every frame to catch brief effects; first explosion
and first flame frames also force readback, alongside regular 90-frame samples.
All sixteen transparency policies and six scalers are compared on each sampled
frame. Natural fragment counts include 38,064 explosions and 173,929 flames.

The full core `required` run immediately preceding this presentation change
remains in `build/workflow/20261013T075232.029672Z/`; native execution, device
logic, ROM fixtures and core mutation selectors are unchanged by these visual
options. Final focused regression/integration, retained GPU mutations, native
readback, CMake build and desktop/software smokes are recorded below. This
finite scene check does not establish whole-credit visuals or peak-load FPS.

Final transparency checks passed: `test`, `verify-stage1`, `verify-native-menu`,
`native-release` (`build/workflow/20261013T102805.986233Z/`), synthetic GPU filters
and all six mutations (`20261013T101824.556086Z/`), then 4,128/4,128 native GPU
readbacks with zero skips (`20261013T102941.774317Z/`). CMake product builds and
four targeted CTests pass. Ninety-frame software and Metal launch smokes with
all effects enabled and audio muted exit successfully; these finite launches
are not whole-game performance acceptance. README, workflow, roadmap and
architecture documentation now describe GPU presentation, input profiles,
music/volume settings and all four original-art transparency controls.

Staged source/document whitespace and links were inspected. The unmodified
upstream controller database license and dr_flac header retain their original
whitespace; decoder/database hashes match their pinned provenance. No ROMs,
captures, generated audio, screenshots or personal settings are staged.


## Native DIP-switch menu (2026-10-13)

Options includes **Video**, Controls, Audio and **DIP Switches**. Video owns
upscaling, sprite filters, cloud/trail/explosion/afterburner opacity, screen mode
and stats. DIP Switches offers **Lives/Credit** (3 or 4), **Ship Increase**
(No or Yes) and Difficulty (Easy, Normal, Hard and Hardest). Ship Increase is
SWB:5; the 3/4 ships choice is SWB:4. The two rows edit independent bits of the
existing combined `lives` preference index, preserving all previously saved
3/4/3x Credits/4x Credits selections without a format migration.
The mapping comes from MAME's `src/mame/sega/segaxbd.cpp`,
`INPUT_PORTS_START(aburner2)`, IO1PORTD: lives mask `0x18` maps to
`0x18/0x10/0x08/0x00`, and difficulty mask `0xc0` maps to
`0x80/0xc0/0x40/0x00`. The default remains physical pins
`255,255,221` (three lives, Normal). All other DIP bits are preserved.
The owner's manual calls SWB:5 **Ship Increase**. This is the credit-multiplied
lives setting that MAME combines with SWB:4; there is no separate score-based
Bonus Life DIP. The initial placeholder row has been replaced with the actual
Ship Increase switch.

Selections persist in the existing versioned frontend preferences. They remain
pending for the active board until **Apply & Restart**, F5 or the next launch;
restarting ends the current game. `GameSession::set_dip_switches` configures
physical pins only before the first cold-board frame. No ROM translation,
CPU/device timing or gameplay RAM is changed. Recording headers and ROM
metadata now carry the actual selected pins, including restart recordings.
Use a separate `--settings-file` with default/missing preferences when recording
the original three-life/Normal reference.

Initial implementation validation: `test-native-settings` covers all 16 settings combinations,
physical masks, persistence, old-file defaults and invalid input. The initial
`verify-native-menu` covered keyboard wrapping, the then-unsupported Bonus Life row,
pending-board isolation, Apply & Restart, cold-only configuration, recording
header/metadata and pointer/Escape return, alongside retained Controls/Audio
checks. Its ROM-font screenshot was visually inspected locally. Logs:
`build/workflow/20261013T111908.505604Z/` (settings) and
`build/workflow/20261013T112033.353650Z/` (final menu/build).
`test` and `verify-stage1` pass at
`build/workflow/20261013T112003.972186Z/`; the latter retains 781 exact native
RGB frames with unchanged default DIP values. `oracle-caller-input` also
passes its retained short/directed/full fixtures at the same log directory:
4,236/4,236, 29,614/29,614 and 2,727,071/2,727,071 caller instruction/IRQ
entries identical, zero skipped. That gate retains supplied shared-state inputs,
explicit graph exits and directed fixture cuts; it is not alternate-DIP
whole-host acceptance. A four-life/Easy 90-frame
software launch and physical recording pass at `build/oracle/dip-smoke.log`.
These focused checks do not establish complete gameplay/MAME equivalence for
all alternate DIP choices or constitute a new full-suite acceptance gate.


The subsequent Ship Increase/Video UI revision passes `test-native-settings`
and `verify-native-menu` at `build/workflow/20261013T113139.289268Z/`.
`test` passes at `build/workflow/20261013T113223.552951Z/`. The final layout
(menu/build) run is `build/workflow/20261013T113259.418120Z/`. Tests retain all
previous settings combinations and independently toggle SWB:4/SWB:5, reject
failed application, check pending-board isolation and recording pins, and
exercise keyboard/pointer navigation for every Video setting and all four
Controls/Audio availability combinations. DIP, Video and Options screenshots
were inspected locally. The earlier Stage 1/input-oracle results above are
reused only for unchanged session, engine and recording sources; no new
alternate-DIP full-game oracle acceptance is claimed by this UI change.

The final release binary also passes a 90-frame software launch with four
ships/credit, Ship Increase Yes and Easy (`255,255,133`); its physical header
and JSON metadata agree. Log: `build/oracle/ship-increase-smoke.log`.


## Explosion visibility and full-throttle-only flame (2026-10-13)

The reported release behavior exposed a classification gap: $347BE target/gate
explosion lists were covered, but player-crash effects use independent streams.
$E7BA resolves two variants through $E89E, with five stream pointers each;
$F796..$F7EE reads sixteen-byte entries, two-byte $EEEE holds and $FFFF ends.
The presentation cache now reads these streams at runtime and classifies their
$A2/$75/$2C fire/smoke palettes as explosions. ROM art inspection confirmed
that $15 is the aircraft/debris image and stays opaque. No crash animation or
pixel data is embedded in source. The Translucent explosion setting now uses
128/255 opacity (previously 176/255) for clearer background visibility.

The optional player-flame mask formerly applied at ordinary throttle. Passive
per-scanline RAM observations now gate it to active flight ($29C09C == $10) and
the ROM's maximum processed throttle at $29C0D2. $B262..$B2B0 clamps the ADC
value to $B0, subtracts $40, and computes ($70 << 8) / $71 = $FD. Only that
maximum range enables 192/255 opacity for the established F2 pens 1, 2 and 4;
idle, braking, attract and other modes remain opaque. This observes board RAM
without bus transactions or replacement state and does not alter ROM execution,
RGB production, descriptor feedback or audio. Video shows a full-throttle-only
hint on the Afterburner row.

`verify-effect-assets` independently checks 363 crash fire/smoke frames,
21 opaque aircraft/debris frames and 82 retained target frames at all 64 LOD
entries (29,824 descriptor checks), plus throttle/mode boundaries. Five retained
`verify-effect-asset-mutations` reject ordinary-throttle translucency, a wrong
maximum endpoint, attract translucency, missing crash fire and translucent
aircraft debris. The mutation target is included in `oracle-test`; native menu
checks also retain the asset gate. The synthetic GPU corpus keeps all nineteen
previous patterns and adds the new explosion opacity, across six scalers and
five sizes. The retained six GPU mutations remain required.

The retained 3,601-frame native steering/weapons corpus still compares all
sixteen effect policies and six scalers with original RGB and GPU/CPU output.
It now checks visible explosion/crash pixel changes and verifies ordinary
throttle produces no translucent flame. An additional 3,600-frame physical
throttle sequence checks full, ordinary and braking frames through takeoff and
flight; ordinary/braking flame-only compositions must be identical to opaque,
and full-throttle compositions must visibly differ. Comparison images remain
under ignored `build/oracle/`. This is finite presentation coverage, not
whole-credit or every-stage acceptance.


Final focused execution/asset checks pass at
`build/workflow/20261013T115958.326128Z/`: `test`, `verify-stage1` (781 original
RGB frames, zero differences), and all five asset/policy mutations. Final
menu/release and synthetic Metal checks pass at
`build/workflow/20261013T120004.611990Z/`: 600/600 synthetic readbacks,
44,961,160 pixels, zero skips; all six retained GPU mutations reject.
The 3,600-frame throttle sequence observes 28,022 full-throttle flame fragments,
104,374 opaque ordinary-throttle candidates and 9,295 changed flame pixels.
Both 90-frame release launch smokes pass with effects enabled (software after
normal boot-to-flight and Metal cold boot); logs are
`build/oracle/effects-fix-software-smoke.log` and
`build/oracle/effects-fix-metal-smoke.log`. The final native Metal scene replay,
including the earlier forced ordinary-flame sample and all crash-fire palettes,
is recorded at `build/workflow/20261013T120504.368031Z/`. These focused runs
retain earlier fixtures and mutations; they do not claim a new complete
`required` or alternate-setting whole-game pass.

The final native Metal replay passes 4,128/4,128 readbacks, zero skipped,
preserving the earlier forced ordinary-flame sample. Both explosion comparison
images were visually inspected: the opaque crash fire hides the scenery,
while the 128/255 composition reveals the rocks and sea through it; aircraft
parts and HUD remain opaque. Asset/capture data and screenshots remain ignored.

The final sampled native corpus changes 10,407 visible explosion pixels, all
from player-crash effects; target explosion descriptors are covered by the
82-frame/64-LOD asset check and synthetic compositing, but this run does not
claim a separately observed natural target-explosion visibility comparison.


## Captured afterburner white-core report (2026-10-13)

The user's screenshot exposed a semantic gap in the earlier flame test. The
saved Afterburner setting was already enabled. The previous F2 pen 1/2/4 mask
covered only the aircraft's small nozzle glow, which is animated by $14062.
It did not cover the large paired flames seen during full throttle. The old
readbacks proved the compositor changed the declared mask, rather than proving
that mask selected the actual afterburner. An attempted expansion of F2 colours
was rejected because those colours also occur on the aircraft; it is absent
from the final implementation.

The actual flame producer is $AF3C/$AFCC. $AE8C/$AEE4 initialize its separate
sprites with palette $80, and $AF88/$B018 choose frames from two phases at
$29018/$2939C, five pitch tables, banks -5..5 and left/right pointers per
sixteen-byte entry. The presentation cache now reads all 220 exhaust frame
selections and their runtime LOD descriptors. Every drawn pixel in those known
flame sprites, including their bright white cores, receives the optional
128/255 opacity. The F2 aircraft and nozzle glow remain entirely opaque.
The previous active-flight/max-throttle predicate is unchanged. ROM execution,
producer feedback, hardware RGB and audio are unchanged; no animation tables,
artwork or captures are embedded in source.

`verify-effect-assets` checks all 220 exhaust selections and 55 opaque aircraft
selections at 64 LOD entries, in addition to all earlier target/crash asset
coverage. A real exhaust descriptor is decoded and every drawn pen must retain
the effect kind. Seven retained mutations reject omitted exhaust pixels, wrong
exhaust palette, ordinary-throttle opacity, wrong throttle endpoint, attract
opacity, missing crash fire and translucent debris. Native replay requires
white core pixels to blend visibly while F2 aircraft/nozzle pixels stay exactly
opaque, and compares the full-throttle scene on GPU with all six filters,
including ScaleFX. The earlier 4,128-readback native corpus and all prior
fixtures/policies remain. Synthetic GPU coverage retains all twenty preceding
patterns and adds the 128/255 afterburner policy.

The focused `test` and `verify-stage1` checks pass at
`build/workflow/20261013T140424.259568Z/`, retaining 781 exact original RGB frames.
The seven asset/policy mutations pass at
`build/workflow/20261013T141232.012651Z/`. Final native GPU/release/synthetic
checks are recorded at `build/workflow/20261013T141235.508677Z/`.
ROM-derived audits, captures and before/after images remain under ignored
`build/oracle/`. These checks cover the declared scenes and poses, rather than
whole-credit or every-stage visual acceptance.

Final results: 5,147 white-core pixels blend, 14,346 F2 aircraft white pixels
remain opaque, and all six native full-throttle GPU filter checks match the CPU
reference. The retained native corpus passes 4,128/4,128 readbacks, zero skips;
630/630 synthetic GPU cases (47,209,218 pixels) and all six retained GPU
mutations also pass. The release is rebuilt. Ninety-frame ScaleFX/Afterburner
software and Metal launch smokes pass; logs are
`build/oracle/actual-afterburner-software-smoke.log` and
`build/oracle/actual-afterburner-metal-smoke.log`. A separate normal cold-boot
stage-1 boost replay supplies before/after pictures under `build/oracle/`.

## Optional roll horizon correction (2026-10-13)

**Superseded visual implementation:** the first correction described below
passed identity/count tests but reversed sky/ground and could remove valid sea.
Its screenshots did not prove wedge removal. See the subsequent both-direction
correction for current behavior and stronger visual acceptance. These earlier
logs retain only their historical scope.

Options → Video → Ground now offers Original (default) and Corrected. The native
wedge remains MAME-identical in Original. The normal road generator `$F02` uses
bank-history-dependent scroll ramps; the fifteen horizon sprites use the main
CPU's `$10ED8/$10FBC` projection/art selection. Their boundaries can leave plain
road ground fill on the sky-facing side of the visible strip.

`Presentation::GroundHorizon` reads the 64-entry main `$11000` frame table, sub
LOD records at `$9E4`, and signed main projection values at `$33E98`. At each
native partial-layer frontier it copies the published sprite bank, extracts the
horizon extent and replaces only eligible road-0 ground pens on the sky side
with that row's native exterior/sky pen. Foreground priority, palette frontier,
source textures below the strip, sprites and shadow resolution are retained.
No bus transactions, road/descriptor writes or native RGB/audio changes occur.
The correction is gated to active flight and normal direct road control 4;
solid-fill rows, alternate roads and special modes keep their original output.
This is a conservative seam correction, not continuous 3D terrain/projection.

Focused validation at `build/workflow/20261013T190817.472125Z/` passes `test`,
`verify-stage1`, `verify-native-menu`, `oracle-video-rolls`,
`verify-ground-mutations` and `native-release`. The retained MAME roll corpus
remains **160/160 pixel-identical, zero skipped**. Five new negative controls
reject wrong sky side, foreground overwrite, solid-fill overwrite, lost clip
frontier and lost row palette. Existing fixtures/mutations remain intact.

The strengthened composed-image check passes at
`build/workflow/20261013T191129.867809Z/`: **1,560 native RGB/PCM/time frames
identical, zero skipped**, 4,096 ROM asset/LOD selections checked; 1,414,679
background pixels corrected in 394 frames, with 101,398 visibly exposed pixels.
Left/right input phases cover 215/179 corrected frames. Disable/restart/cold
boot identity passes. Synthetic tests cover opposite and inverted planes,
priority, pattern boundaries, solid fills, palette choice and partial clips;
full natural inverted/altitude/pitch coverage is not established.

Metal checks at `build/workflow/20261013T191413.658777Z/` pass all **630/630**
retained synthetic GPU cases (47,209,218 pixels), then repeat against a corrected
native plane fixture, including all six filter modes, zero skips. The native
1560-frame check passes again in that gate. These finite Metal scenes do not
establish every-stage/backend performance. CMake product/settings/ground builds
and two focused CTests pass; logs are under `build/oracle/horizon/`.

Ignored `ground-before.png` / `ground-after.png` show the largest visibly exposed
sample, scripted frame 1338. ROM-derived images, fixture and diagnostic files
remain excluded from Git. Additional stage backgrounds, special-road enhanced
projection, full inverted flight and sustained peak-load performance remain
open. This focused increment does not claim a fresh complete `required` run or
whole-credit acceptance.

Final retained GPU mutations also reject (all six) at
`build/workflow/20261013T191712.646395Z/`. Two 180-frame boot-to-flight launches
with Ground Corrected, ScaleFX and all opacity settings enabled pass on Metal
and the explicitly forced software fallback. Logs: `build/oracle/horizon/metal-smoke.log`
and `software-smoke.log`. The finite Metal smoke reports about 59.64 simulation/
display FPS after warm-up; software ScaleFX reports about 26 displayed FPS.
These are launch observations, not controlled before/after benchmarks or
sustained peak-load acceptance.

## Horizon correction both roll directions (2026-10-13)

The user correctly rejected the initial correction: its comparison removed valid
sea, while the wedge remained. Original RGB identity and changed-pixel counts
were insufficient visual acceptance. The ROM quarter-shifted sine is negative
cosine, so the initial normal was reversed; one fitted plane also missed the
published strip during interpolation. Global endpoint centroids were biased by
viewport clipping, particularly during inverted rolls. Those approaches are
superseded, not accepted fixes.

The current correction identifies only the ROM horizon families with palette
$E8 (`$10E6E` initializes `$007F00E8`). It resolves the published horizon raster
before measuring each row/column. Strictly ordered sky pen 1 and terrain pen 14
establish that line's direction. A pixel changes only when every available local
silhouette agrees it is outside the strip on the sky side, and its native layer
is an eligible plain road pen. Ambiguous/absent endpoint lines retain Original.
There is no global angle fit, recoloring of the strip/sprites or extrapolation
into the ground side. Normal-road/flight/priority/solid-fill guards remain.

New `verify-ground-fixture` replays the retained MAME roll corpus: **160/160
original RGB frames identical, zero skipped**. Enhanced witnesses cover 159
frames; one lacks the required horizon endpoints. Actual visible bank sides
(not requested input labels) cover 29/30 changed frames. **58,471 visible wedge
pixels are corrected; 2,643,597 ground-side pixels are preserved.** The checks
assert sky-side fill, terrain preservation, foreground protection and native
baseline identity. Normal ROM-backed integration does not depend on the MAME
capture; this new witness gate belongs to `oracle-test` and focused validation.

Final focused checks pass at `build/workflow/20261013T200558.745478Z/`: `test`,
`verify-stage1`, `verify-ground-fixture`, `verify-ground-mutations`,
`verify-native-menu`, `native-release`. Native integration retains **1,560 exact
RGB/PCM/time frames**, zero skips, and checks all 4,096 asset/LOD selections,
disable/restart and cold boot. Eight retained unit mutations reject, plus three
recorded-scene mutations for sea overwrite, sky-side reversal and fitted-plane
shortcuts. CMake product builds and two focused CTests also pass; logs are in
`build/oracle/horizon/final-cmake-*.log`.

Ignored `verified-left-before/after.png` and `verified-right-before/after.png`
show upright opposite-bank witnesses (frames 1275 and 1287). Sea below the strip
is unchanged. Mutations run with `--no-images` so failing controls cannot replace
the passing visual evidence. Older comparisons and validation logs retain only
their historical scope. Further stage/background and sustained-performance
coverage remains open; this is not full-credit acceptance or a fresh complete
`required` run.

Final Metal composition and all six retained GPU mutations pass at
`build/workflow/20261013T202429.623225Z/`: 630/630 cases, 47,209,218 pixels,
zero skips, including the current corrected native-plane fixture. Native
identity checks pass again in that gate. Final 90-frame Metal and explicit
software-fallback launches with Ground Corrected, ScaleFX and opacity settings
also pass (`build/oracle/horizon/revised-metal-smoke.log` and
`revised-software-smoke.log`). These finite checks do not establish sustained
peak-load acceptance.

## SDL controller feedback (2026-10-14)

Basic rumble is implemented in the native frontend with Controls → Feedback,
versioned `.haptics` preferences, explicit output-device selection, independent
body/trigger gains, four cue toggles, trigger swap, finite motor tests and stops.
Default Off leaves the passive board tap detached. Enabled capture observes only
completed main-CPU handler assignments: `$9EF6 → $A030` cannon, `$9EBC → $9F7C`
missile and `$9C20 → $E662` player damage. Continuous afterburner uses flight
selector `$29C09C == $10` and signed processed throttle `$29C0D2 >= $FD`.
No control requests or sound amplitudes stand in for successful gameplay actions.

The SDL adapter borrows input handles, stops before close/remap, and owns any
simple-haptic fallback handle. The mixer uses maximum per channel and independent
gains, monotonic envelopes, once-only event IDs, stale suppression and finite
watchdogs. Pause/menu/focus loss/restart/disable/device changes/unplug/shutdown
stop output. Tests are finite even with gameplay feedback Off. Unsupported
capabilities and errors leave input and other output channels available.

Independent `capture-feedback` MAME witnesses cover **4,200 frames**, with
**301 cannon allocations, 17 missile allocations, 3 player hits and 3,106 boost
frames**, plus **7 attract/suppressed assignments and 25,914 unrelated writes**.
The retained expanded-host fixture covers **73,593,282 original bus phases**
and confirms no feedback from its attract allocations. No captures/ROM data
were added to Git. `verify-feedback-mutations` rejects eleven retained mutations:
wrong producers, attract output, early boost, overlap cancellation, duplicate
frames, missing explicit stops, trigger crosswire, lost re-enable events and missing watchdog.

Native capture-on/off comparison covers **4,200 identical RGB/PCM/time frames**,
with **377 cannon, 20 missile and 2 player-hit events**, **3,106 boost frames**,
pause/once-only consumption/reset checks. The MAME and native runs use different
physical input schedules; these counts are separate witnesses, not a claim of
trajectory equivalence. `test`, `verify-stage1`, `verify-native-menu` and native
release build passed in `build/workflow/20261013T205330.258503Z/`.
Mutations passed in `build/workflow/20261013T205716.580954Z/`; Make and CMake
virtual-device/fixture checks both pass. The final idempotent polling, Off-mode tap detachment and sequence-reset changes
pass `test`, the 4,200-frame comparison, menu checks and native build in
`build/workflow/20261013T210002.022614Z/`; all eleven mutations pass in
`build/workflow/20261013T210209.601136Z/`.

The user's Bluetooth Xbox controller, named **Xbox One S Controller** by SDL
**3.4.16**, reports body and trigger capabilities. The user physically confirmed
body rumble and **both trigger motors** through SDL. The host reports macOS
**26.7.1**. Controller-only initialization accepted a test without felt output;
adding the SDL video window/event loop worked. The game already owns that loop.
Firmware and output latency were not measured. Windows/Linux physical hardware,
USB, simple-haptic fallback hardware, full gameplay/lifecycle feel and advanced
FFB remain open. This finite enhancement check does not replace whole-credit
or whole-host acceptance. See [HAPTICS_PLAN](HAPTICS_PLAN.md).

Final Make executable smoke: 600-frame physical boot plus 660 live frames with
feedback enabled in temporary settings and the attached controller, Metal output,
and dummy audio completed successfully (`build/haptics/native-final-smoke.log`).
The finite run reports roughly 57–59 simulation FPS; this is no peak-load or
latency claim. Final CMake motor/positive-fixture/attract-fixture checks pass
3/3 (`build/haptics/cmake-final-test.log`).


## End-of-day enhancement checkpoint (2026-10-14)

This checkpoint collects the DIP/Video menu work, original-art effect fixes,
external-music balance and live mapping changes, ground correction for rolls in
both directions, and SDL rumble. README now lists implemented enhancements,
configuration and defaults; the documentation guide separates current coverage
from haptics/widescreen plans and retained historical checkpoints. The rumble
notes include explicit left/right body and trigger motor assignments.

Validation retained for this commit: final `test`, `verify-feedback`,
`verify-native-menu` and `native-release` pass in
`build/workflow/20261013T210002.022614Z/`; `verify-stage1` passes in
`build/workflow/20261013T205330.258503Z/`; all eleven feedback mutations pass in
`build/workflow/20261013T210209.601136Z/`. Ground fixtures/mutations pass in
`build/workflow/20261013T200558.745478Z/`, ground GPU and GPU mutations in
`build/workflow/20261013T202429.623225Z/`, and music mutations in
`build/workflow/20261013T145459.602242Z/`. Final CMake feedback checks pass 3/3;
the rebuilt native executable completes the finite attached-controller smoke.
Documentation-only finishing edits pass diff/link/command inspection.

No ROM data, captures, recordings, personal soundtracks or build artifacts are
included. All retained fixtures and mutations remain local. The last full
`required` checkpoint predates these enhancements; this commit records focused
validation, not a fresh full-suite or whole-credit acceptance claim. Remaining
acceptance tasks continue in ROADMAP; Windows/Linux physical rumble, advanced
FFB, incoming warnings and true widescreen remain open as documented.

## Native menu scrollbars (2026-10-14)

Video, Controls and Feedback now draw a slim scrollbar at the right edge when
items extend beyond the visible rows. The blue thumb reflects the visible
fraction and viewport position, including short final pages, and reaches both
ends. Track and thumb use SDL rectangles and existing menu colors; native-pixel
rounding keeps the indicator crisp. Short menus hide it. Existing keyboard,
controller and mouse navigation still selects and scrolls the same rows.

`test`, `verify-native-menu` and `native-release` pass in
`build/workflow/20261014T035635.596389Z/`. Menu readbacks check top/middle/bottom
positions, both endpoints, hidden indicators on short menus and board-frame
isolation. Controls/Feedback screenshots were visually inspected; a finite
native menu launch passes (`build/oracle/scrollbar-native-launch.log`). Images
remain ignored local captures. These are frontend checks, not new whole-host
acceptance evidence.

## Restore normal arcade attract startup (2026-10-14)

The original attract sequence was already implemented, but choosing Start Game
ran 600 board frames in the background, supplying coin/start pulses and discarding
their presentation/audio. The default `play-native` target requested the same
shortcut. Normal launch now offers Enter Arcade and resumes the cold board at
frame zero, displaying its boot and attract sequence. Credits and Start come
from the existing player controls (keyboard 5, then Enter; gamepad Back/Start).
The explicit `--boot-to-flight` and `--stage` debug paths retain their shortcuts.

`test`, `verify-native-menu` and `native-release` pass in
`build/workflow/20261014T041917.051744Z/`. A finite native cold-boot smoke records
720 contiguous frames with all button pins released, renders the original attract
demo with INSERT COIN / CREDIT 0, and exits successfully. Its image and recording
stay local under `build/oracle/arcade-attract-720.*`. The standalone SDL process
was unavailable to the UI automation app selector, so visual verification uses
the bounded native launch; the menu action path was inspected directly. No new
literal ROM port or whole-credit acceptance is claimed.


## Widescreen plan refresh (2026-10-14)

The draft in WIDESCREEN_PLAN now proposes a 16:9-first, render-only delivery:
scene/culling audit and immutable partial-frame snapshots, CPU background proof,
sprites/visibility, existing effects and corrected ground, frontend integration,
then acceptance. Current square-pixel proportions imply 398×224 with 39 columns
added per side; native simulation and combat stay unchanged initially. Unlimited
sprites/custom stages are separate work. Menu scrollbars, restored arcade attract,
both roll directions and sea/terrain preservation are included in the gates.
The plan was followed by the experimental preview described below; full widescreen acceptance remains open.


## Experimental 16:9 presentation (2026-10-14)

Video → Viewport offers Original (default) and 16:9 Preview. The preview renders
398×224 square pixels with a 39-column margin around the native safe area. SDL
logical coordinates, window aspect, pointer conversion, menu/HUD viewport and
captures follow that width. Native 320×224 simulation and sprite feedback remain
unchanged. Existing filters, opacity settings and corrected ground work through
the wide scene planes; the software path uses the same composition inputs.

The passive GameSession tap snapshots owned tile/text/palette words, buffered
road/sprites, latched tile registers, road control and presentation mode/throttle/
roll before each partial render. WideScene uses separate road/tile/palette adapters,
signed sprite clipping and per-row fragment chains. It resolves each clip with its
own palette. Text remains centered. Road origin is applied after the native 12-bit
phase mask; moving the constructor offset instead changes the center at wraps.
The default native road/tile call paths retain their original behavior.

The 1,560-frame comparison preserves native RGB/PCM/time and reconstructs the
unfiltered opaque center exactly. It covers 1,573 clips, both roll directions
(240 frames each), 11,039,984 non-repeated side-background pixels and 8,167,910
side-sprite pixels. Corrected wide ground changes 343,729 eligible sky pixels
while preserving terrain indices, sprite planes and priority protection.
Natural column-scroll scenes were not encountered; an independent planar-ROM
reference exercises signed column/source wrapping with synthetic registers.
Geometry fixtures also cover road phase, distinct partial-clip palettes,
left/right sprite edges and immutable input descriptors. Retained wide mutations
exercise origin, column wrap, palette ownership, sprite bounds and feedback writes.
Real Metal GPU readback passes six filters × two opacity policies: 12 cases,
5,527,424 compared pixels, no skips. Local captures/fixtures stay in build/oracle.

Original producer bounds remain: Main scenery `$8B94..$8BB8`, Sub sprite rejection
`$852..$890`, and the 192-request limit `$B628..$B654`. Missing outer descriptors
can cause edge popping; finite published horizon coverage also limits correction
in the added bands.
No extrapolated horizon or extra gameplay objects are invented. This is an
experimental preview, not full-width visibility or full widescreen acceptance.
Every-stage/peak-load performance, other GPU/platform paths, 16:10 and ultrawide
remain open. WIDESCREEN_PLAN tracks the remaining gates.

Finite macOS Metal smokes produce 1194×672 gameplay/menu captures in both GPU
and forced CPU/software modes. The saved viewport width is used when creating
the window; windowed mode switches refresh the resized swapchain before drawing
the new menu. This avoids stale first-frame separator bounds after resizing.
The `--profile` output includes `wide_scene_ms` (CPU time averaged per display).
In an 180-frame Off/opaque Metal run without an audio test device, the last two
samples report 59.60/59.64 simulation and display FPS, about 11.3 ms board time
and 1.8 ms wide-scene time. Dummy-audio smokes instead show about 29 display FPS
in both Original and Wide, so those runs cannot isolate renderer pacing.
These are short controlled checks, not sustained gameplay/audio acceptance.

Validation logs: `build/workflow/20261014T105953.967349Z` passes `test` and
`verify-stage1`, including the wide integration/geometry checks. Every oracle
prerequisite and all 143 retained recovery mutations complete; the root legacy
recipe then fails because local `build/oracle/main.bin` is a raw 262,144-byte ROM
image rather than an `AB2MAIN1` capture. The image is retained separately and
`capture_main.lua` independently regenerates the intended 600-sample boot capture:
600/600 identical, zero skipped, all six ADC slots and 100 wraps covered.
The unchanged complete root recipe is replayed via an ignored local Make include
(`build/workflow/20261014T121522.778424Z`): passing boot/gameplay ADC, sprite/road,
video and scenery corpora. Existing skips remain explicit: 2/1,200 gameplay sprite
passes have mid-pass input changes; 263/1,200 gameplay scenery cases have input
writes by the other CPU. No corpus or mutation coverage is removed. This is a
completed regression graph with a focused fixture repair, not a new clean
`required` invocation; run the clean gate before a merge/release milestone.
Use distinct `*_image.bin` names for raw ROM extraction to avoid fixture collisions.

Real-GPU menu checks pass in `20261014T111107.704531Z` (4,128/4,128 native sprite
planes, zero skipped), wide GPU readback in `build/oracle/widescreen/gpu-final.log`,
and the final release build in `20261014T112150.351789Z`. CMake builds both wide
integration/geometry targets and the GPU check; its geometry CTest passes.
Documentation links and `git diff --check` pass. Full widescreen visibility and
whole-credit acceptance remain open.

## OpenAL Soft cockpit audio (2026-10-14)

Options → Audio → Output adds Original SDL (default), Headphones HRTF, 2.1 and
5.1 profiles, live device/resampler choices, actual output status and a finite
four-speaker test. Output preferences use `.output`; existing music mappings,
-12 dB replacement balance and music/effects/track gains remain intact. Preview
uses the selected playback owner. One thread owns device/stream/context/source
operations; SDL subsystem initialization/teardown stays on the main thread.
Pause, clear/restart, profile/device changes, menu exit, focus loss, disconnect
fallback and shutdown are handled without sound-CPU commands or game restart.

Native FM/PCM taps retain 62,500/31,250 Hz, before the MAME LoFi resampler. Four
virtual positions use separate mono S16 FM/PCM sources with equal-power pair
normalization and native left/right gains. Native music has separate chip-rate
direct stereo sources; decoded external music retains its existing 48 kHz path.
Unclassified effects use neutral direct stereo. A separate SDL WAV conversion
helper supplies mono S16 effects or stereo S16 music for future assets. No
native effect is loaded/replayed as a WAV.

Ownership follows the completed Z80 `$0C76` descriptor initializer, `$17A2`
direct-sample allocator and actual PCM/YM writes, using IX/DE/BC observations.
Main producers include cannon `$9ED8`, reload `$16CEC`, missiles `$A002/$A716`,
explosion `$9122/$91B8`, crash `$E70E/$EA86` and debris `$F72E`; command tables
are read from ROM at runtime. Engine variants initialize `$FBA7` through the
sound `$0A65` handler. The lightweight ownership observer remains active while
raw export is off, so switching into HRTF preserves currently playing voices.
Capture toggle tests caught and rejected the earlier reset-on-enable approach.
The listener/layout is the user's requested virtual arrangement, not measured
Deluxe cabinet wiring. Remaining voice categories/cabinet revisions stay open.

The follow-up classification audit confirms passing commands `$6E/$6F` and the
refueling/departure sequence command `$EF` remain unclassified. In the retained
1,800-frame routing capture, descriptor commands `$7E/$9A/$D8` and direct PCM
sample IDs `$06/$26/$28` also remain unclassified. Their audible identities and
speaker assignments have not been established. Commands and sample IDs are
different namespaces and may refer to related effects; this is not a complete
sound catalog. This historical gap list is superseded by the
[2026-10-22 routing audit](#spatial-audio-routing-and-playback-acceptance-2026-10-22).

Independent `capture-spatial-routing` records **117,509/117,509 exact sound
transactions/IX/DE/BC, zero skips**, through 1,800 frames, with 13 descriptor
initializations, 135 direct allocations and 15 observed owners. Recombining
raw music/effect groups preserves **943,466 exact PCM sums**. Export-on/off
integration preserves **1,800 native RGB/PCM frames** and checks continuous raw
frontiers, nonzero front/rear buses, pause/restart and live raw-capture toggles.
The retained BC corruption and detached-observer controls reject. A forced
missing-direct-channel capability also proves truthful SDL fallback: disabling
positioning alone still virtualizes stereo in HRTF, so it cannot guarantee dry
music. The fallback clears stale HRTF status. Preview completion accounts for
in-flight submissions before declaring the playback queue drained.

Soft loopback verifies WAV conversion, actual HRTF, all four directional images,
direct stereo BGM, 62,500 Hz conversion, pause/clear and live 2.1/5.1 requests.
Four playback mutations reject swapped sides, disabled HRTF, stereo positional
buffers and spatialized BGM. End-to-end native-to-Soft playback renders
**1,448,641 stereo frames**, zero clipped samples, while retaining unity native
PCM; pause/clear/restart also pass. Logs:
`build/workflow/20261014T141457.913725Z/` and
`build/workflow/20261014T141935.771674Z/`. The latter keeps two native chunks of
lookahead during normal loopback rendering; deliberate immediate draining after
restart can log expected source starvation. This is finite signal/lifecycle
coverage, not sustained real-device acceptance.

Make and CMake build both the optional backend and SDL-only paths. Soft 1.25.2
is pinned/checksummed and dynamically linked, with embedded HRTF data and
retained upstream notices. A Clang 21/CoreAudio annotation conflict required
disabling upstream's optional function-effects feature probe on Darwin; game
compiler flags and upstream source remain unchanged. Apple's legacy OpenAL
framework is not used. Windows CMake copies the Soft DLL beside the executable.

Finite Metal launches on this Mac report actual headphone HRTF (status 1,
Built-In HRTF), stereo/HRTF-off for 2.1, and internal 5.1/HRTF-off for the surround
request. Warm samples report about 59.6 simulation/display FPS. Each initial
launch logged one 13-source underrun/restart during startup; no sustained or
underrun-free performance claim is made. Actual physical speaker wiring/channel
mapping and headphone localization/balance were not heard/confirmed by the user.
Logs: `build/oracle/spatial-audio/{headphones,speakers21,speakers51}-smoke.log`.
Sandboxed display launch failed; the finite checks used approved native display/
audio access. Headless SDL-only fallback also launches successfully.

The complete `required` profile passes at
`build/workflow/20261014T142055.116214Z/`: `test` **4.92 s**, `verify-stage1`
**148.77 s**, `oracle-test` **3763.25 s**, including all retained controls and the
new spatial ownership/playback gates. Legacy comparisons retain their declared
races/skips; no fixture or comparison was weakened. Shared engine/device/tap
sources remained unchanged during this completed run. Final frontend-only dry-
stereo fallback and preview-drain fixes are additionally checked at
`build/workflow/20261014T144830.917124Z/` (playback controls/fallback and release)
and `20261014T145258.232576Z/` (test/menu). CMake product/unit/game loopback checks
pass in `build/oracle/spatial-audio/cmake-*-final.log`; fresh ROM-test-disabled
CMake builds also pass. Standard Make is rebuilt with Soft after the SDL-only
check (`build/workflow/20261014T150551.469056Z/`). Earlier interrupted profiles
are superseded, not claimed as complete passes. This is finite regression scope,
not fresh whole-credit acceptance.

Remaining acceptance: physical headphones/2.1/5.1 on Windows/Linux/macOS,
all native voice classifications/six music contexts, cabinet wiring/revision,
sustained latency/peak load/device-loss coverage. Discrete LFE synthesis,
replacement-effect selection and original-rate external music are future work.
See [the implementation and acceptance plan](SPATIAL_AUDIO_PLAN.md).

Final rebuilt 120-frame Metal/headphone launch also reports active Built-In HRTF
and about 59.6 warm simulation/display FPS:
`build/oracle/spatial-audio/headphones-final-smoke.log`. The initial 13-source
restart remains explicitly logged. The reproducible dependency builder finishes
with a matching archive checksum and copied notices in
`build/oracle/spatial-audio/dependency-build-final.log`.

## Reported enemy-coordinate overflow stop at `$15C10` (2026-10-14)

Live play reported `unported caller instruction at PC 89104`, possibly during
stage 18. Decimal 89104 is `$15C10`, `SUB.W D0,34(A5)`. The enemy movement
controller adds velocity to its coordinate at `$15C0A`; `$15C0E` skips the
subtraction when signed overflow did not occur. The subtraction restores the
previous coordinate on overflow. Normal-path stage checks did not establish
coverage of this fallback. The adjacent `$15C1E` rollback was already translated.

The fix appends one literal instruction using the existing word-subtraction
flags and bus-phase helpers. All 12,404 retained recovery instruction bodies
remain byte-identical; no generator, scheduler, device or gameplay policy is
changed. Independent original-ROM fixtures exercise both CCR extremes and
signed overflow, unsigned borrow, zero and boundary operands: **14/14 identical,
zero differences/skips, 56 bus phases**. Three deliberate subtraction,
destination and prefetch mutations reject after a passing baseline. All 143
earlier recovery mutation definitions remain unchanged. Capture and
mutation logs remain under ignored `build/oracle/enemy_overflow.*`.

The previous complete corpus is preserved as
`build/oracle/cpu_recovery_before_enemy_overflow.bin`; the 14 verified records
are merged without modifying existing records. Complete recovery passes
**12,405 PCs, 1,255 branches with both outcomes and 77,954 MAME fixtures**, zero
differences/skips, at `build/workflow/20261014T195913.455584Z/`. An earlier replay
started before the atomic corpus merge finished and failed the missing-PC gate;
the final replay above uses the completed merge and passes.

`test` (42.42 s) and `verify-stage1` (186.86 s) pass at
`build/workflow/20261014T195710.518285Z/`; `native-release` passes at
`build/workflow/20261014T195808.858780Z/` and the standard build at
`build/workflow/20261014T200035.100811Z/`.
The directed stage-18 sweep completes **600/600 input-window frames** at
`build/workflow/20261014T195841.067358Z/`. This is supplementary stage coverage,
not a reproduction of the user's exact physical inputs or proof that the sweep
executed `$15C10`. The rebuilt release passes a 120-frame SDL dummy smoke
(`build/oracle/enemy_overflow_smoke.log`). Older unchanged mutation results remain
the preceding checkpoint's evidence; these focused checks do not claim a fresh
full-suite or whole-credit CPU/device/RGB/audio acceptance pass.

The retained Easy / 99-credit physical recording completes all **74,452 frames**
in 832.01 s at `build/workflow/20261014T195951.728689Z/`. All ten named transition
observation columns match the unchanged MAME reference **74,452/74,452**, zero
differences/skips; the ending/state gate and all nine observation/ending mutations pass
at `build/workflow/20261014T201350.458806Z/`. The recording crosses ordered stage
18 and reaches the ending, ranking and return to attract. It is the existing
declared trajectory, not the user's unrecorded session, and does not extend
acceptance to continuous whole-credit CPU/bus/device/RGB/audio comparison.

## Wider object visibility and horizon coverage (2026-10-15)

The 398×224 preview now has a detached visibility policy for the identified
native-edge limits. `WideVisibility` observes completed native entry/bus phases;
its private copies never supply CPU execution or write native RAM, math chips,
request counts, feedback, collision/targeting state or sound commands.

`WideSpriteProjection` copies the verified Sub `$7B8..$9E2` transform. Only its
horizontal visibility/clip bounds extend by 39 pixels; unwrapped X anchors avoid
nine-bit coordinate wrap at the added right edge. Native request depth order,
flip/zoom/pitch, palette metadata and `$726` interpolation are preserved. The
no-sort path retains the previous full projection and updates its half-frame
positions. Dynamic presentation descriptor banks can include extra render-only
objects; the native producer's 192-request cap remains unchanged.

The Main `$8B94..$8BB8` scenery cull retains a private 64-byte object
and owned size record. Its shared world pair is read from the current camera
state, rather than retained as object-owned data. The verified `$8AF2..$8C14` update advances that copy with
current camera inputs until it leaves the wider bounds or passes the native
near-depth limit. Native slot reuse cannot overwrite it. Stage/mode changes and
restart clear the retained objects. Capture disable clears the observer; enabling
mid-session starts with subsequent publications, without reconstructing earlier
already-freed objects. Other object/despawn controllers and capacity rejection
still need coverage before promising complete visibility in every scene.

For the horizon, the observer captures `$10FBA` after `$10ED8` has stepped its
fifteen parts. Extra endpoints use the actual 16.16 X/Y increments, current ROM
frame/quadrant flips and previous-frame positions. This extends the procedural
strip instead of repeating border pixels or fitting a guessed line. Corrected
ground measures the extended sprites across the full width and still replaces
only plain `$1700..$1707` fill on the sky side with the row's `$1720..$172F` sky
index. Terrain/tile indices, aircraft, shadows and layer protection stay intact.
Original ground and the legacy snapshot renderer remain available as references;
extended visibility can add sprites overlapping the original center.

Independent retained MAME checks pin the copied math before widening:

- Sub directed: **240/240 passes identical**, zero skips; **7,200** detached
  descriptors identical. Boot: **1,199/1,200**, one input-race skip; **103,617**
  detached descriptors identical. Gameplay: **1,198/1,200**, two input-race skips;
  **120,320** detached descriptors identical. Original comparison regions,
  write-set checks and race gates remain intact.
- Scenery: **377** accepted natural and **18** accepted directed updates match
  all 64 object bytes from MAME. The same windows include **18/38** native culls;
  skipped records remain **12/8**, respectively, due to other-CPU input writes.
  The final Main replay also checks kept/culled status independently against
  MAME's `$9556` free-list writes (`20261014T211702.624331Z/`); the height mutation
  still rejects with that stronger gate in
  `build/oracle/widescreen/visibility-mutations/mame-scenery-height-final-run.log`.
- Five new negative controls reject early native-bound retirement, a short
  horizon, a native-memory alias, wrong Sub pitch and wrong Main vertical
  projection. The last two fail the independent MAME comparisons. All six
  earlier wide controls remain, plus the new narrow-bound and wrapped-X controls.

Final focused `test` (4.41 s) and `verify-stage1` (146.51 s) pass at
`build/workflow/20261014T205411.814658Z/`. Wide capture preserves **1,560 native
RGB/PCM/time frames**, and the legacy scene reconstructs their exact centers.
The extended view records **5,967 recovered descriptor observations** (including
**4,122 retained scenery** and **616 added horizon** observations), **2,556,982
side-pixel differences**, and **424,392 eligible sky-fill corrections**. Outer
correction counts for the two ROM-measured roll-normal signs are
**101,668/64,908**; both are required by the test. Allocator-reuse/retirement,
immutable native memory/time, pause/reset/disable and shared observers pass.

Additional directed ordered stage slots **6, 12 and 18** each pass 360-frame
left/right input windows, with native RGB/PCM/time unchanged and terrain-safe
correction. Including their boots, **2,880 counted frames** match. These are
stage-skip fixtures, not natural progression; their 180/180 direction windows
are delivered inputs, not a claim of full rolls. Toggling feedback capture keeps
the wide observer active. They add **1,044,369 eligible sky-fill corrections**
and **8,303 retained scenery observations**. Sea and land captures for both
roll directions were inspected locally; assets/captures remain ignored.

MAME/geometry/mutation logs are
`build/workflow/20261014T204143.211006Z/`,
`20261014T204142.087532Z/` and the final
`20261014T210448.031149Z/`. A first MAME mutation correctly detected bad pitch
but aborted through an uncaught test exception; the fixture executable now
reports that semantic failure with exit 1, and all five controls pass in the
final run. No passing baseline or historical coverage was removed.

The updated real-Metal fixture matches CPU output **12/12** across all six
filters and both opacity policies, **5,527,424 pixels**, zero skips:
`build/oracle/widescreen/visibility-gpu-final.log`. Make release and standard
builds pass at `20261014T204920.816704Z/` and `20261014T210354.145699Z/`.
CMake product/geometry/scene/stage builds and geometry CTest pass in
`build/oracle/widescreen/cmake-visibility*.log`. A finite 240-frame Metal wide
launch with Corrected ground passes; warmed samples are about **59.3–59.7 fps**,
**13.6 ms board** and **1.8–1.9 ms wide scene**. Its initial display rate is lower
while warming up. This is a finite smoke, not sustained/peak-stage acceptance:
`build/oracle/widescreen/visibility-metal-smoke.log`.

These are focused presentation checks, not a fresh complete `required` run or
whole-credit CPU/device/RGB/audio acceptance. The prior full core/device profile
and the `$15C10` recovery results above retain their declared scopes. Complete
object/scene/lifecycle coverage, Windows/Linux hardware, sustained performance,
16:10/ultrawide and removal of the native request cap remain open.

## User-reported macOS completion (2026-10-15)

The user reported finishing the game from start to finish on their Mac with
the current saved preferences. The files were read from
`~/Library/Application Support/AfterBurner2-Recomp/AfterBurner2/` after that report;
they describe the reported configuration, rather than a captured runtime trace.

| Area | Saved configuration |
| --- | --- |
| DIP switches | Normal difficulty; three ships per credit; Ship Increase off |
| Video | 16:9 preview; Corrected ground; pixel-art scaling; ScaleFX 3X sprites; windowed; FPS display |
| Translucency | Clouds, trails, explosions and full-throttle afterburner enabled |
| Music | All six replacement tracks mapped; music/effects and per-track gains at 100% (replacement music retains the -12 dB baseline) |
| Audio output | Headphones HRTF requested; MacBook Pro Speakers selected; default resampler |
| Rumble | Enabled; body and trigger strengths 50%; cannon, missile, impact and boost enabled; motor swap off; saved Xbox controller selected |

This is successful manual macOS playthrough evidence with three starting ships
and Normal difficulty. The report does not specify continues, executable/version,
frame timings or observed audio/HRTF status. No input recording or independent
MAME comparison accompanies this run, so reproducible whole-credit acceptance
and other platform/configuration coverage remain open.

## Local capture deduplication (2026-10-15)

Audited all seventeen `cpu_recovery*.bin` files against the current complete
corpus. Eleven historical files had exact plan and payload prefixes in
`cpu_recovery_complete.bin`: the 9,077-, 9,540-, 10,012-, 10,326- and 10,491-PC
snapshots, `pre_natural`, `natural_verified`, and the four `before_*` snapshots.
Removed those duplicate physical files, reclaiming **99,828,453,034 bytes
(92.97 GiB)**. All their original fixture bytes remain in the retained corpus;
no fixture or mutation coverage was reduced. Different-plan baseline, native
and release corpora remain, as do coverage reports, merge provenance, original
increment captures, logs and unrelated whole-host recordings.

The local audit records each original SHA-256 and reconstruction ranges, with
a restoration helper described in [the workflow](WORKFLOW.md#capture-discipline).
The recovery manifest/branch gate passes before and after deletion: **12,405
PCs, 1,255 branches with both outcomes, 77,954 fixtures across two active
captures**. This is a storage-only audit; no new engine replay or acceptance
claim is implied.

An additional exact-file audit of captures/traces larger than 64 MiB found
36 duplicate copies across 16 groups. On this Mac's filesystem, replaced each
duplicate with an APFS copy-on-write clone of its identical peer, preserving
every filename, byte and modification timestamp. Independent-write behavior was
tested first; subsequent writes to one clone do not modify its peer. This freed
another **18,774,145,037 bytes (17.48 GiB)**, confirmed by filesystem available
space, without changing active fixture paths. Local hashes and sharing records
are in `build/cleanup/exact-duplicates.json` and `shared-exact-files.json`.
The 6.81 GiB `cpu_recovery_9077.bin` historical snapshot was also restored and
matched its original SHA-256 before removing that redundant physical copy again.
Total capture storage reclaimed is **118,602,598,071 bytes (110.46 GiB)**.

## Widescreen capacity and stage/lifecycle checks (2026-10-15)

The opt-in wider observer now samples Main `$B628` before the native signed
`$C0` request rejection. `wide_request.hpp` copies the verified producer format:
object bytes 0–15, with request byte 12 replaced by object byte 20. It records
both accepted and rejected submissions, keyed to the native publication/request
buffer. Native counters, allocation, combat rules, bus phases and hardware
sprite banks are unchanged.

At the matching Sub pass, submissions travel with the actual sorted bank. At
latch, the enhanced bank preserves native descriptors/metadata, removes stale
repeated request offsets from a full depth bucket and recovers requests missing
from the native bank. Capacity rejects and bucket drops have separate provenance
kinds (4 and 5). Extra descriptors use the already verified wider projection,
interpolation and stable depth insertion. Dynamic decoder storage also removes
the former presentation-only 64 KiB descriptor-bank guard; native hardware banks
remain bounded. This expands visible rendering capacity without generating new
gameplay objects or changing the original object pools.

Publication replacement and reset clear detached submissions. Enabling during
a Sub pass preserves uncaptured native slots, including the no-sort/interpolated
pass, until their projection is available. Previously default-filled leading
slots could masquerade as request zero; validity now gates both replacement and
duplicate detection.

Independent retained MAME producer checks pass 1,201/1,201 records with zero
skips (`build/workflow/20261015T080048.451839Z/`). The directed set includes
the 191/192/193 boundary and reports 291 exact copies and 290 rejected calls.
The request helper and original append port are unchanged after that pass.

Capacity fixtures retain 512 rejected requests while the native counter stays
192, recover 34 missing requests from the real Sub sorter's final 30-entry depth
bucket without duplicates, and render the last entry of a 5,001-descriptor bank.
They also check publication lifetime, reset, mid-pass enabling and immutable
native RAM/time. Existing scenery, horizon and MAME arithmetic controls remain;
new controls discard capacity/bucket recovery, retain stale duplicates, replace
uncaptured slots, reinstate the decoder bound, corrupt producer metadata and move
the signed capacity boundary.

`wide_stage_visibility` covers all 23 ordered ROM slots, 360 input-window frames
per slot, both bank-input directions, pitch and weapons, then pause, three
Original/Wide cycles and cold restart. Directed stage setup/wait intervals are
not counted as natural progression or compared by that test. The compared
windows require native RGB/PCM/time identity, complete wider rows and safe ground
correction. Their legacy 320-wide reconstruction must also match native pixels.

`wide_lifecycle_visibility` uses the retained ending, neutral, continue and
weapons physical recordings from cold boot. Each frame compares all ten retained
MAME observation columns, reconstructs the original image exactly and renders
both wider ground policies, preserving terrain, sprites and protection. The
ending gate requires all 23 natural flight indices, all 13 observed carrier
selectors, all eight refuel selectors and the ending/ranking/attract modes.
A changed reference mode is a deliberate failing comparison control. CMake
registers all four recordings as separate tests.

These are finite directed windows and retained natural paths, not every possible
input trajectory, alternate lifecycle or hardware configuration. MAME transition
column comparison is not continuous whole-credit CPU/device/RGB/audio acceptance.
Remaining pre-publication object controllers, custom stages/objects, other
aspects, physical Windows/Linux GPU checks and sustained performance remain
separate work. All generated screenshots, fixtures and logs stay ignored.

Focused final-code checks: the capacity/mid-pass fixtures and 1,560-frame wide
scene comparison pass in the final integration run. It preserves native
RGB/PCM/time and exact legacy reconstruction, with 6,497 recovered/projected
descriptor observations and unchanged eligible ground-correction counts
(424,392; outer roll-sign witnesses 101,668/64,908). The updated scene fixture
passes GPU readback **12/12**, all six filters and both opacity policies,
**5,527,424 pixels**, zero skips:
`build/oracle/widescreen/capacity-metal-gpu.log`. Display-service access was
required outside the macOS sandbox; the first sandboxed launch had no displays.
The rebuilt release completes a bounded 240-frame wider/Corrected/all-effects
launch in `capacity-metal-smoke.log`. This is a smoke, not sustained performance.
Make release build passes at `build/workflow/20261015T083151.243226Z/`.
CMake builds both new replay/sweep executables; its focused stage-12 run passes
978 counted native frames, all 360 legacy image reconstructions and the
pause/viewport/restart checks (`capacity-cmake-build.log`,
`capacity-cmake-stage12.log`). The altered-reference control rejects as expected.

Visual inspection of the first all-stage sweep found an additional canyon issue
in slots 8/17: isolated gradient/ground blocks appeared in the outer bands.
A controlled stage-8 replay removed each extension class in turn; only removing
added horizon descriptors removed the blocks (`capacity-canyon-diagnostic.png`).
The controller could update its fifteen positions without actually submitting
the strip for drawing. The observer now checks submissions from those native
object slots before extending that strip. A dedicated inactive-publication
fixture and rejected mutation cover that distinction. The focused stage-8
replay still matches native RGB/PCM/time and legacy pixels, and its capture is
clean (`capacity-canyon-fixed.png`, `capacity-stage8-final.log`). No native
controller behavior or terrain data was changed; the affected stage/integration
and oracle checks are repeated after this presentation-only fix.

After the canyon fix, final `test` passes (5.44 s) at
`build/workflow/20261015T090006.500451Z/`, and the complete `verify-stage1`
target passes (885.08 s) at `build/workflow/20261015T090012.142240Z/`.
The repeated **23-slot sweep** preserves **22,494 counted native RGB/PCM/time
frames**, including **8,280 pixel-exact legacy reconstructions**, and passes
pause, repeated viewport changes and cold restart in every slot. It records
**17,115,517** eligible sky-fill corrections with protected terrain/sprite layers
unchanged and **49,613** retained-scenery descriptor observations. These are
observations across frames, not unique extra objects. The setup/skip intervals
remain excluded from the counted comparison. The surrounding 781-frame session,
1,800-frame music, 1,560-frame ground and 4,200-frame feedback checks also pass.

Final release rebuild passes (45.90 s) at
`build/workflow/20261015T090109.896831Z/`. Updated GPU replay again matches
**12/12**, **5,527,424 pixels**, zero skips (`capacity-metal-gpu-final.log`), and
the bounded release launch passes (`capacity-metal-smoke-final.log`). Final
CMake builds pass; its stage-17 canyon check also passes all 978 counted native
frames and 360 legacy reconstructions (`capacity-cmake-stage17-final.log`).
The stage screenshots and final contact sheet remain local under
`build/oracle/widescreen/`.

The first full lifecycle pass completes the 74,452-frame ending path, including
all 23 flight indices, 13 carrier/eight refuel selectors, ending/ranking and
return to attract, with zero ten-column differences/skips and exact legacy
pixels. It then correctly stops at neutral frame 372: the harness had paired
requested input with a capture of MAME's actual input pins. The retained notes
already require replaying observed pins to preserve MAME input-update latency.
No reference/recording was changed, shifted or regenerated. The harness now
explicitly pairs ending with `natural_ending_sequences.inputs` (byte-identical
to the ending input used in the earlier pass), and the other three captures
with their `natural_*_sequences.mame.csv.inputs` companions. The corrected
12,000-frame neutral replay passes, including death/game-over/ranking/attract.
The complete lifecycle set and comparison mutation are repeated after this
fixture-pairing repair. Requested and observed pin recordings are both retained.

The earlier parallel audio test timeout exposed independent Make targets
regenerating the same deterministic WAV/FLAC files while consumers decoded
them. `audio-test-fixtures` is now one shared phony prerequisite for menu,
GPU-menu, music and spatial-audio tests. It runs once per Make invocation before
any consumer; no fixture content, decoder or test assertion changed, and no
result cache was added. The parallel dry-run graph contains exactly one
fixture-generation command; `test -j4` passes (4.60 s) at
`build/workflow/20261015T093548.567263Z/`. The complete regression is repeated
with the corrected dependency graph and unchanged compiler settings.

Focused observed-pin replays also pass **10,000/10,000 continue** and
**8,000/8,000 weapons** frames, zero ten-column differences/skips and exact
legacy reconstruction. Their final logs are
`capacity-lifecycle-continue-final.log` and `capacity-lifecycle-weapons-final.log`;
neutral is `capacity-lifecycle-neutral-final.log`. Together with the completed
ending pass, the four declared paths cover **104,452 physical frames**. A fresh
complete profile repeats those paths with the repaired harness/build graph before
claiming final full-suite acceptance.

The fresh run at `build/workflow/20261015T093902.465525Z/` repeats `test`
(4.49 s) and the complete `verify-stage1` (886.83 s) successfully. Its four
widescreen lifecycle replays all pass on the final observed-pin pairing:
**74,452 ending + 12,000 neutral + 10,000 continue + 8,000 weapons = 104,452
frames**, every retained MAME observation column identical, zero skips, with
pixel-exact original image reconstruction on every frame. The altered-reference
mode is rejected.

That clean run completes successfully: `oracle-test` **3,937.16 s**,
`oracle-joint-main-progress-full` **37.66 s**,
`oracle-joint-reset-window` **7.97 s**, and `native-release` **0.32 s**.
All retained mutation controls, including the widened visibility/capacity and
lifecycle controls, remain enabled. Existing documented race skips in older
routine-level oracles remain reported in their logs; the new lifecycle paths
skip no frames. The supplemental long joint comparison matches **20,532,168
instruction/IRQ records**, **53,449,620 ordered phases** and **40,714,240 RGB
pixels**, zero differences/skips. The reset window matches **1,449,740 records**
and **3,763,047 ordered phases**, zero differences/skips. These are their retained
bounded windows, not a whole-credit CPU/device/RGB/audio comparison. Full logs
and target timings are in that run directory and its `results.json`.

### Attract/takeoff outer-strip omission (2026-10-15)

The user subsequently identified missing sprites specifically in the added side
bands during attract and initial takeoff. The earlier lifecycle gates established
native fidelity and complete wider rows, not visual completeness of every
extended scene. One concrete omission was the Main publication observer clearing
fresh horizon-extension requests whenever the game mode was not normal flight
(`$10`), including attract (`$01`) and takeoff (`$0F`). The native controller had
actually submitted those strips.

The observer now preserves each freshly generated strip through publication,
still requires a matching actual native submission, and consumes it immediately
afterward. Scenery ghost lifetime remains restricted as before. Stage changes
invalidate interpolation history before generating the next strip. This changes
presentation copies only; native controller execution and assets are untouched.
Fixtures cover attract/takeoff/flight publication, inactive canyon strips and
stale-strip reuse. New deliberate controls restore the flight-only gate and
retain a stale strip. Lifecycle replays now require explicit horizon-extension
witnesses in attract and takeoff, rather than inferring coverage from progression.

A focused 1,560-frame comparison passes with native RGB/PCM/time and the original
image reconstruction unchanged, zero skips; the wider view now observes 3,070
extra horizon descriptors across the window (previously 616). The real neutral
replay independently observes extra strips in modes 1, 3 and 15. Diagnostic
captures/logs stay in `build/oracle/widescreen/startup-*`. This addresses the
identified strip omission; it is not an exhaustive audit of all scene-controller
bounds or of artwork authored only for the original viewport.

Final focused results: `test` passes (5.17 s) and `verify-wide-scene` passes at
`build/workflow/20261015T112656.449588Z/`. All **15 visibility mutation controls**
reject (426.81 s), followed by a release rebuild (36.86 s), at
`build/workflow/20261015T112821.203015Z/`. The updated neutral lifecycle replay
passes **12,000/12,000** ten-column MAME observations, zero skips, and exact
original image reconstruction; its horizon witnesses explicitly include modes
1 and 15 (`attract-takeoff-lifecycle-final.log`). Its altered-reference control
also rejects (`attract-takeoff-reference-mutation.log`). The complete integration
stage sweep is repeated separately. These are focused follow-up results, not
a new complete oracle-suite run after this presentation fix.

The complete `verify-stage1` follow-up passes **975.49 s** at
`build/workflow/20261015T112907.169886Z/`, including all 23 ordered stage slots,
both bank directions, pause, repeated viewport changes and restart. Its native
RGB/PCM/time comparisons and legacy image reconstruction remain exact. The
rebuilt release also completes a 240-frame macOS Metal attract launch using a
local copy of the user's settings (`attract-takeoff-metal.log/.png`); the saved
settings are untouched. The capture shows the extended horizon in the attract
scene. This bounded smoke is not a sustained-performance measurement.

### Non-flight fill blocks and horizon joins (2026-10-15)

The user still saw blocks at mid-right/lower-left and gaps between horizon
pieces in attract and carrier takeoff. Restoring strip submission alone did not
close those visual defects. Two further flight-only policies were responsible:
`WideScene` skipped Corrected ground outside mode `$10`, and `WideVisibility`
discarded previous strip coordinates on every non-flight publication. The first
left the original road-fill wedges exposed in the added bands; the second made
extra segments use current coordinates alongside interpolated native segments.

Corrected wide rendering now uses the existing ROM-asset/silhouette/road-index
guards in every mode. It still changes only eligible plain road-0 sky fill,
preserving terrain, sprites, tile priority and shadows. Strip history now follows
the last controller update within the same mode/stage; a scene change resets
history before generation. The original halfway arithmetic therefore applies to
the extra segments in attract and takeoff as well. Native simulation, buffers,
RGB, audio and timing are untouched.

Before changing production code, two added tests failed against the previous
build: `non-flight outer sky-fill missing` and `non-flight horizon interpolation
differs` (local `nonflight-ground-before.log` and
`nonflight-interpolation-before.log`). Both pass after the fix. Geometry uses
actual ROM horizon assets to require safe correction on both outer bands in
modes 1/15/16. Visibility fixtures require an eight-pixel full-step to become a
four-pixel halfway step, and prohibit interpolation across scene changes. The
1,560-frame real integration also requires corrections in attract and takeoff;
native RGB/PCM/time and the legacy image remain exact, zero skips. New retained
fault controls reintroduce flight-only correction/history or cross-scene history.
The user's tearing description was gaps/joins in the horizon, rather than a
whole-window presentation split.

A controlled takeoff replay first applied the existing correction to the same
immutable snapshots outside flight; this removed the right-hand fill rectangle.
The final paired diagnostic then renders the previous and corrected strip/fill
policies alongside one native run. All **1,200/1,200** MAME observation rows and
legacy images match. Local comparison animations are
`attract-before-after.gif` (frames 200–260) and `takeoff-before-after.gif`
(560–640), previous policy on the left and corrected policy on the right, slowed
to 30 displayed frames/s. They visibly reproduce/remove the lower-left block and
the right-hand horizon notch. These are finite rendered sequences, not a claim
about every possible transition or display platform.

The final neutral lifecycle replay passes **12,000/12,000** ten-column MAME
observations and exact legacy image reconstructions, zero skips, with terrain
and protected layers preserved (`nonflight-correction-lifecycle-final.log`).
The altered-reference control rejects. Final release build passes (43.44 s) at
`build/workflow/20261015T115723.719815Z/`; its 240-frame Metal attract smoke and
capture complete (`nonflight-metal-final.log/.png`). Native VSync is requested;
the reported horizon gaps are tested as geometry/interpolation, rather than
treated as evidence of a whole-window synchronization failure.

All **17 visibility controls** reject (507.08 s), including lost non-flight
history and cross-scene history; all **nine geometry controls** reject (48.54 s),
including the flight-only correction. Logs are at
`build/workflow/20261015T115839.735614Z/`. The final integration run also confirms
2,662,172 eligible sky-fill corrections in its 1,560-frame window, retaining the
native comparison and terrain/protection gates. No fixtures or earlier controls
were removed. The new comparisons/animations remain ignored local evidence.

Final `test` passes **5.39 s** and complete `verify-stage1` passes **1,008.38 s**
at `build/workflow/20261015T115700.469482Z/`. This repeats all 23 directed slots,
both roll-input directions, pause, viewport cycles and restart, with 22,494
counted native RGB/PCM/time frames and 8,280 exact legacy reconstructions.
The surrounding game-session, music, ground and feedback integrations also pass.
This is the focused follow-up integration/visibility/geometry/lifecycle validation;
the earlier complete oracle-suite pass remains separately scoped to its earlier
tree, rather than being presented as a new full run after these presentation fixes.

### Screenshot-driven outer background coverage (2026-10-15)

Three further user screenshots expose gaps the preceding finite samples missed:
translucent clouds reveal a hard original-viewport background boundary at left,
the initial carrier sea has a cyan rectangular exterior, and departure shows a
dark-blue vertical band at right. The same defects reproduce in cold-boot attract
and retained neutral-input snapshots. Opaque sprite-only captures hid part of the
cloud-background problem; inspect the composed translucent result as well.

The blue column occurs when the strip is near the bottom: the measured profile
can see its sky marker but its ground marker is below the viewport. Only in the
new side bands, an actual silhouette edge can now use the ROM-derived normal
when that local marker pair is clipped. The original 320-wide correction policy
is unchanged. The cloud/sea gaps need the reverse operation too: beyond the
ground-facing silhouette, the enhanced wide plane may replace plain road
exterior sky with an eligible ground index observed on the same native row.
This preserves priority/tiles, uses only native road-0 indices, and does nothing
without an actual native-row source. It does not stretch clouds/waves, invent
asset data or change native controllers, framebuffers, audio or timing.

The comparison contract permits that reverse change only in the enhanced wide
plane, with the destination present in the original row. All previous terrain,
sprite and protection checks remain. New fixtures test clipped-marker fallback,
center preservation, source absence, exact native color and priority protection.
The prior policy (with only a signature compatibility shim) rejects at
`bottom-clipped outer sky column survived`; local before/prototype captures
also reproduce/remove the three reported classes. The new 840-frame no-input
attract replay compares raw native RGB/PCM/time and original reconstruction, then
checks the cloud-background witness. Neutral lifecycle frames 500 and 960 assert
the carrier-side fill and absence of the departure column. These complement the
existing 1,560-frame roll and 23-slot checks; they are not exhaustive scene
acceptance. Views are still unfilled when their required silhouette/source is
unavailable.

The first new attract witness incorrectly compared two post-sprite background
pixels that could be shaded differently. It now checks pre-sprite ground indices
and native-row provenance, with the existing protected-layer gates retained. Its
840-frame replay and the 1,560-frame side-band comparison pass at
`build/workflow/20261015T124613.391907Z/` (107.15 s). The new side-band mutation
controls belong to the wide unit gate; they are not assigned to the original
320-wide recorded corpus, where they correctly have no effect. The repaired
complete ground mutation target and unchanged recorded fixture pass at
`build/workflow/20261015T124536.458085Z/` (34.31/5.95 s). The recorded gate retains
160/160 original MAME RGB frames, zero skips, 58,471 exposed wedge pixels removed
and 2,643,597 preserved terrain pixels.

Separate `edge-cloud.bin`, `edge-carrier.bin` and `edge-departure.bin` fixtures
retain the three new actual scenes for GPU replay. `wide_gpu_regression` accepts
an optional fixture path, preserving its original default and all comparisons.
macOS Metal passes **36/36** scene/filter/opacity cases, **16,582,272 pixels**,
zero skips, including ScaleFX and translucent composition. Logs are
`build/oracle/widescreen/edge-{cloud,carrier,departure}-metal.log`; captures,
fixture bytes and contact sheets remain ignored local evidence.

The enlarged ScaleFX cloud preview then exposed a narrower seam: the first two
columns inside the original viewport also contain exterior sky below the strip.
Ground-plane extension now includes confirmed ground-side holes there, rather
than stopping exactly at the old viewport boundary. The original 320-wide
presentation still does not use this extension. Unit and real-attract witnesses
check those two columns, and an additional rejected mutation restores the seam.
Side-only prototypes/fixtures and their logs are retained separately; final
acceptance repeats the affected integration, lifecycle, GPU and mutation gates.

The final ground mutation target passes **18 controls** (15 unit + three retained
MAME-fixture controls, 42.30 s); the original recorded gate again passes all
160 frames (6.30 s) at `build/workflow/20261015T125618.849113Z/`. Its release
build passes (48.19 s). All three regenerated complete-plane GPU fixtures again
pass **36/36 cases**, **16,582,272 pixels**, zero skips
(`edge-*-plane-complete-metal.log`). The actual release completes the bounded
240-frame Metal smoke with VSync reported enabled
(`edge-plane-complete-release-metal.log`). This is not a sustained-performance
measurement. The six-panel ScaleFX/translucent comparison is
`reported-edge-cases-before-after.png`, previous policy left and corrected
policy right. Final images include removal of the two-column native-edge seam.

The final complete-plane neutral replay passes **12,000/12,000** ten-column
MAME observations and exact original image reconstructions, zero skips, with
the carrier/departure assertions and terrain/protected-layer gates enabled
(`edge-plane-complete-lifecycle-final.log`). The altered-reference control also
rejects. Ground-fill changes include confirmed reverse fills in the enhanced
wide plane; raw native output and the Original viewport policy remain unchanged.

Final widened-visibility and geometry mutation targets pass at
`build/workflow/20261015T125757.010024Z/` (**515.26/44.85 s**): all 17 visibility
and nine geometry controls remain enabled and reject. Alongside the 18 ground
controls above, this retains 44 relevant negative controls. Final CPU/GPU and
release results refer to the complete wide plane including the native-edge
seam fix, not just the earlier side-only prototype.

Final clean `test` and complete `verify-stage1` pass at
`build/workflow/20261015T125755.871942Z/` (**5.35/992.82 s**). All 23 directed
slots preserve **22,494** counted native RGB/PCM/time frames and **8,280** exact
legacy reconstructions, with pause, viewport cycles and restart. The additional
840-frame attract replay and 1,560-frame roll comparison pass. The latter
records 5,234,269 fill changes; the stage windows record 23,677,511. These counters
include confirmed ground-side extension as well as sky correction, despite their
retained `sky-fill` log label. Native and protected terrain/layer checks remain
enabled. The surrounding game-session, ground, music and feedback checks pass.
This is final focused follow-up validation; it does not claim a new complete
oracle-suite or exhaustive widescreen acceptance on this tree.

### Rolling scenery, takeoff waves and banked cloud corners (2026-10-15)

Three follow-up fixes remain confined to the detached widescreen presentation:

- Retained scenery now reads its **live shared rotation/position pair**. Keeping
  that pair frozen at native deletion could place ground objects in the sky as
  the camera rolled. Private object and size records still survive allocator
  reuse. Retention also runs during attract and takeoff, with stage/mode changes
  clearing the preceding scene.
- Takeoff extends the water field with adjacent world cells. The original
  `$884E..$8860` distribution has sixteen columns at `3*64` world units; the
  enhancement repeats that period alongside the native field. Wave frames come
  from the runtime `$3F918` animation list, with native depth, projection and
  animation phase. Previous positions support interpolation. This adds actual
  wave artwork, rather than only filling the surrounding sea color. It applies
  to takeoff (`$0F`) only; native spawning and gameplay remain unchanged.
- Corrected ground projects the observed horizon envelope onto the ROM normal
  in the wider viewport. This classifies corners where neither the pixel's row
  nor its column intersects the strip, removing the reproduced banked attract
  color blocks. Clipped-marker sky correction also covers the enhanced center;
  otherwise inversion can leave a short road-color strip at the top of the sky.
  It still requires an observed native-row ground color, respects
  protected layers and leaves the original 320-wide correction policy intact.

Fixtures cover live camera changes after zoom settles, attract/takeoff/flight
retention, stage retirement, both water edges and takeoff-only scope, and the
recorded attract frame-140 corner. Synthetic corner checks include upright and
inverted orientations. Captures and comparisons remain under ignored
`build/oracle/widescreen/`; no ROM-derived assets are added to the repository.

The focused scene gate passes at `build/workflow/20261015T155456.349103Z/`
(126.62 s): 840 attract frames, the existing 1,560-frame comparison and a new
720-frame directed inversion tail preserve native RGB/PCM/time and legacy
images. The combined flight windows include 12/36 near-full-inversion frames
by actual negative/positive angle change. Full-stick reversals are held for
120 frames, respecting the native `$C0A` trigger and offset decay; alternating
inputs alone did not prove completed rolls in both directions. A specific
inverted top-center road-fill witness now passes. Ground mutations and the
160-frame retained MAME roll fixture also pass at
`build/workflow/20261015T154620.561166Z/` (zero original RGB differences/skips).

Final visibility/MAME fault controls (21) and geometry controls (9) pass at
`build/workflow/20261015T154824.191378Z/` (594.26/57.67 s); all 19 ground controls
remain enabled. The 12,000-frame neutral lifecycle replay matches all ten
retained MAME observation columns with zero skips and exact legacy images;
its altered-reference control rejects. Logs are `roll-water-cloud-neutral-final.log`
and `roll-water-cloud-reference-control.log` under `build/oracle/widescreen/`.
Seven GPU readback fixtures cover the banked cloud corner, carrier waves,
both actual inversion directions and the earlier scene/cloud/departure cases:
84/84 filter/opacity cases match the CPU compositor, 38,691,968 pixels, zero
skips (`*-followup-metal.log`). A sampled 1,200-frame native takeoff replay also
retains the wave-field and departure comparisons locally. These finite checks
are presentation follow-up evidence, not exhaustive widescreen acceptance.


Final `test`, complete `verify-stage1` and `native-release` pass at
`build/workflow/20261015T155816.097755Z/` (4.65/973.41/37.97 s). All 23 ordered
slots preserve 22,494 counted native RGB/PCM/time frames and 8,280 legacy
reconstructions; pause, viewport cycles and restart remain covered. The 840-frame
attract check, 1,560-frame scene comparison and 720-frame actual-direction
inversion tail also pass. The rebuilt release completes a 240-frame Metal /
VSync smoke with a local copy of the saved widescreen/Corrected/ScaleFX/effect
settings (`roll-water-cloud-release.log` and `.png`). The real settings file is
unchanged. GPU fixture hashes remain unchanged after the final capture replays.
This is a finite launch/performance smoke and focused integration follow-up,
not a new full oracle-suite or exhaustive widescreen milestone.

### macOS preview packaging (2026-10-15)

Published as [v0.1.0-preview.1](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.1)
from `2ce8446`; both uploaded asset digests match the verified local ZIP/checksum.

`tools/package_macos.py` packages a clean committed tree and the optimized
native executable into a ROM-free Apple Silicon ZIP. The current executable,
SDL3 3.4.16 and OpenAL Soft 1.25.2 declare macOS 26.0 deployment targets.
The package bundles the two replaceable shared libraries, removes development
library search paths, rewrites dependencies to package-relative paths and
ad-hoc signs/verifies each binary. It is not Developer ID signed or notarized.
An allowlist includes setup documentation, notices, complete project source
and checksum-verified upstream dependency source archives. Neither local ROMs
nor settings, soundtracks or captures are copied. A manifest records the source
commit and payload hashes; the ZIP has a separate SHA-256 file.

The preflight package from `4cb5bd2` was extracted into a separate temporary
directory and launched from a different working directory using temporary
settings and a temporary link to user-provided ROMs. All manifest hashes,
executable permissions, signatures and package-relative dependencies passed.
The 240-frame launch reported Metal/VSync and OpenAL Soft headphone HRTF enabled
with the built-in dataset (`build/releases/preflight-smoke.log`). A startup
stream restart was logged; this finite smoke does not establish sustained
latency or performance. The temporary ROM link was removed; the original ZIP
contains only ROM setup instructions/listing. See the
[release workflow](WORKFLOW.md#macos-preview-release) for reproduction.


The fresh macOS release `required` gate passes at
`build/workflow/20261015T162805.304852Z/`: `test` **4.86 s**, complete
`verify-stage1` **917.01 s**, complete `oracle-test` **3,870.46 s**. All retained
mutation categories execute, including all **146** recovery controls and the
three `$15C10` controls. Existing fixture race exclusions remain explicit in
the individual logs; they are not omitted failures. Recovery covers 12,405 PCs
and 77,954 identical fixtures. Integration includes all 23 directed slots,
22,494 counted native RGB/PCM/time frames, 8,280 exact legacy reconstructions,
840 attract frames and both actual inversion directions. Four natural lifecycle
replays preserve all ten MAME observation columns, original image reconstruction
and terrain/protected layers across **104,452 frames**, zero skips; the altered
MAME-reference control rejects.

The supplemental `full` targets and `native-release` pass at
`build/workflow/20261015T174807.542965Z/` (**37.41/7.92/0.30 s**). The 600-frame
joint window compares 40,714,240 RGB pixels and 53,449,620 ordered phases exactly;
its retained initial contexts and reset-window scope remain as documented in
the logs. The earlier seven-fixture Metal readback evidence is **reused**, not
reported as a fresh run: 84 cases / 38,691,968 pixels, zero skips, from
`*-followup-metal.log`. Rendering sources, compiler/options and comparison
harness are unchanged, and all seven hashes in
`roll-water-cloud-gpu-fixtures.json` remain identical after the fresh integration
and lifecycle replays. Subsequent release-note/README changes are documentation
only. These gates establish the declared finite release coverage, not exhaustive
widescreen, physical-platform or continuous whole-credit acceptance.

## Input calibration and macOS spatial-audio recheck (2026-10-18)

Options → Controls → Calibration is enabled only after selecting a gamepad or
joystick. It displays bound stick X, stick Y and throttle values before and
after dead zone/sensitivity. The live
display uses the same bindings and precedence as gameplay, including full-axis
throttle; disconnecting the selected device shows a notice and returns Controls
to Keyboard. Rendering
does not advance the paused board. Virtual-device regression checks exercise
dead-zone rescaling, full-axis throttle and unplug fallback. Software and Metal
menu checks pass on the final controller-only Calibration UI at
`build/workflow/20261018T171320.949284Z/` and
`build/workflow/20261018T171432.708761Z/` (4,128/4,128 GPU sprite-plane
cases, zero skips). The optimized native release builds at
`build/workflow/20261018T171320.949284Z/`.
The user confirmed on the final rebuilt UI that Keyboard leaves Calibration
closed with a Select Device hint, while selecting their controller opens the
display and moving it changes the values. This is physical macOS controller
acceptance for the displayed actions, not a full HOTAS or platform matrix.

The current macOS OpenAL Soft loopback and four retained playback mutations
pass at `build/workflow/20261018T165843.993427Z/`. The 1,800-frame game-level
check renders 1,448,641 stereo output frames with no clipping and unchanged
native PCM. Independent MAME ownership matches 117,509/117,509 observations,
zero skips, with the retained detached-observer mutation rejected; logs are at
`build/workflow/20261018T165921.689316Z/`. The user reported correct Speaker
Test direction on headphones and the Mac's built-in stereo speakers. This
confirms headphone listening and stereo left/right, not physical rear-channel
output. Unclassified voices, cabinet wiring, 2.1/5.1 hardware, Windows/Linux
acceptance and sustained real-device latency remain open.

A 3,600-frame Stage 5 debug-skip performance window on the Mac uses Metal,
ScaleFX, 16:10, CoreAudio/OpenAL headphone HRTF and VSync. All 25 reported
intervals hold `simulation_fps=display_fps=59.6374`; the log contains no
stream-underrun or playback-failure report. This is a bounded one-minute
window, not a long-session, device-loss or physical 5.1 acceptance test.
Record: `build/oracle/spatial-audio/stress-stage5.log`.

The Sega After Burner Deluxe Type owner's manual 420-5812 (first printing)
labels one `REAR SP` and `FRONT SP x2` on its wiring diagram (pp. 63–64). Its
parts list includes a 10 cm, 8 Ω, 15 W cabinet-roof speaker, a lamp-box speaker
of the same rating, and a front-cabinet phone-jack board. This supports a
raised rear sound image, but the scan does not establish a reliable conductor
trace from board pins to each speaker, exact listener angles, or After Burner II
cabinet revision applicability. The headphone HRTF presentation now raises rear
sources above ear level while 2.1/5.1 rear sources sit at ear level; four
virtual directions remain a presentation choice, not a claim of four cabinet
speakers. The sound CPU, chip output, native stereo reference and direct music
path are unchanged. Spatial loopback, retained playback mutations, 1,800-frame
game integration, independent MAME ownership (117,509/117,509, zero skips), and
native release build pass at `build/workflow/20261018T220405.403751Z/`.
At that checkpoint, physical listening of the revised height and exact cabinet
electrical wiring remained open; the headphone check below followed.
The high-resolution schematic audit on 2026-10-22, recorded below, supersedes
the earlier interpretation of a single rear speaker and unreadable wiring.

The user then reported that Headphones HRTF gunfire, missiles and explosions
were quieter than Original SDL and lacked positional feel. The frontend had
applied an extra equal-power factor of 0.707 to each classified native stereo
channel before HRTF. For headphones only, those independent channels now enter
OpenAL at full level; the 2.1/5.1 speaker profiles retain their earlier gain.
In the 1,800-frame loopback test, headphone rendered squared-sample energy rose
from 3,428.69 to 3,895.41 versus 4,933.92 in the native stereo reference,
with no clipping. The comparison is not a perceptual loudness or physical
localization measurement. `test`, `verify-spatial-game`, `oracle-spatial-routing`
and `native-release` pass at `build/workflow/20261019T035751.967100Z/`.
With Headphones HRTF and External Headphones selected, the user confirmed in
the revised build that gunfire and missiles were louder and more clearly in
front, and explosions/crashes more clearly behind. This verifies the reported
Mac headphone issue on that output; exact cabinet electrical mapping and other
physical outputs remain open.

The macOS full gate initially exposed a stale retained `extra-budget-clock`
mutation anchor after the earlier bus-arbiter budget optimization. Only the
mutation probe was retargeted to the current `span` expression; its added
cycle still changes the CPU budget. The unchanged cold MAME baseline matches
33,234 grant groups, 3,101,020 bus phases and 418,118 shared reads; all eight
bus-boot mutations reject. Focused log:
`build/workflow/20261018T172445.653914Z/`.

## Integrated macOS acceptance gate (2026-10-19)

The final 16:10 viewport, controller calibration and revised headphone HRTF
source passes the complete `required` profile:
`test` (5.13 s), `verify-stage1` (400.62 s), and `oracle-test` (3,406.69 s),
including all retained mutations. Log directory:
`build/workflow/20261019T042949.062480Z/`. The supplemental `full` profile
passes both joint-main-progress and reset-window comparisons at
`build/workflow/20261019T050739.004809Z/`. The former matches 20,532,168
instruction/IRQ records, 53,449,620 ordered phases and 40,714,240 RGB pixels,
with zero differences/skips; the reset window matches 1,449,740 instruction/IRQ
records and 3,763,047 ordered phases, also with zero differences/skips.

The complete gate includes the 16:9 and 16:10 directed stage sweeps, native
RGB/PCM/time preservation, MAME sound routing and spatial playback mutations.
The current mutation probes for the optimized bus budget, YMFM Timer B,
translucent afterburner and wide visibility were retargeted to the present
source while retaining their semantic rejection checks. No ROM data or captures
are part of the source changes. This finite acceptance does not replace
physical Windows/Linux device checks, sustained load or continuous whole-credit
comparison.

## Spatial audio routing and playback acceptance (2026-10-22)

The completed Z80 allocations now route passing commands `$6E/$6F` behind the
listener. Main `$156F8..$1571C` waits until object depth crosses the player and
selects the original stereo side from signed X. Enemy weapon allocation `$14D96`
emits `$D8`, assigned to the front by explicit presentation policy. Credit
`$7E` (`$C79C`), flag/countdown `$9A` (`$DC5E`), stage-entry `$A5→$06`
(`$D804`), startup `$4C/$4E→$26/$28` (`$BF56/$BF60`) and carrier `$EF→$46`
(`$104EE`) retain neutral stereo. Service sound-test tables identify `$7E` as
INSERTED COIN, `$A5→$06` as READY, and `$4C/$4E→$26/$28` as FLIGHT 1 R2/L2.
The timer/carrier audible identities and directional assignments remain open.
Missing direct allocator handlers
`$AA6/$AAE/$B02/$B22/$B44` now participate in metadata ownership.

The retained independent 1,800-frame MAME fixture matches 117,509/117,509
transactions/IX/DE/BC, zero skips, and 943,466 exact PCM voice sums. The new
semantic audit checks nine descriptor roots, seven direct IDs, 2,889 PCM enables
and 37,137 FM assignments, rejecting unreviewed allocations. Passing commands
are absent from this natural fixture and have separate directed descriptor
checks. Focused log: `build/workflow/20261022T153539.729550Z/`.

Playback fixes cover queued paused-loopback deadlock, busy spinning at a full
backend queue, stale OpenAL/HRTF failure status and uncounted source exhaustion.
OpenAL playback failure or injected disconnect falls back to SDL while preserving
the requested profile for retry. Diagnostic status and `--profile` report
software queue times, stopped-source counts and recoveries. These timings
exclude the device mix buffer, DAC and physical end-to-end latency.

An actual CoreAudio run exposed repeated immediate restarts after producer
starvation. An isolated 13-source producer reproduces it: one 100 ms stall
caused 148 stopped-source instances over about six seconds. The fix re-primes
all exhausted streams synchronously to 50 ms, retaining the original 25 ms
startup and immediate single-source restart. A starvation cohort spans polls,
so twelve sources finishing before the final source cannot evade recovery.
The same injected stall then produces thirteen stopped sources, one re-prime
and no later underruns. Steady software buffering is about 37–48 ms, peak
65.55 ms. Evidence: `build/oracle/spatial-audio/realtime-before-reprime.log`
and `realtime-after-reprime.log`. These were muted CoreAudio checks, not
subjective listening or physical end-to-end latency measurements.

The final 600 sample-second loopback run uses all 13 sources at
48,000/62,500/31,250 Hz, renders 28,800,000 frames with no new underruns or
clipping, and bounds steady block energy at 3.10887–3.10952. The offline
two-block prefill peaks at 209.373 ms. Simultaneous and staggered starvation
checks pass; all four retained and five new playback mutations reject. Missing
direct-channel capability and injected disconnect recover truthfully to SDL.
Log: `build/workflow/20261022T155153.353263Z/`.

All four retained physical-input recordings pass the final HRTF lifecycle
integration at `build/workflow/20261022T155553.106603Z/`:

| Recording | Native RGB/PCM/time frames identical | Stereo output frames | Native fade updates |
| --- | ---: | ---: | ---: |
| Ending, all 23 stages | 74,452 | 59,923,201 | 525 |
| Neutral/game-over | 12,000 | 9,657,601 | 36 |
| Continue | 10,000 | 8,048,641 | 36 |
| Weapons | 8,000 | 6,438,721 | 36 |

Total: **104,452 exact native frames, 84,068,164 output frames, zero skips**,
no clipped output samples, no steady-state underruns and no backend recovery.
The ending visits all six music slots with starts `1,3,2,2,2,4`, fourteen stops
and 525 fade updates. Peak offline software buffering is 79.5159 ms, pending
20.032 ms. Each replay also passes pause/clear/restart. Every recorded ADC and
button byte is round-tripped through `GameSession` input before execution.
The reference is an untapped native board, not independent whole-credit MAME
audio. Replacement-file natural fade/listening acceptance remains open.

The final muted, actual CoreAudio Stage 5 run completes 18,000 frames (about
five minutes) with HRTF active while the shared regression runs. One load stall
exhausts thirteen sources; one synchronized re-prime recovers, with no later
underruns or backend fallback. Software backend buffering peaks at 120 ms and
pending native audio at 40 ms. Per-report simulation rate spans 55.082–60.3983
fps. Log: `build/oracle/spatial-audio/coreaudio-20261022-final.log`. This uses
the dummy video driver and muted gains: it establishes device streaming and
bounded recovery under this concurrent load, not visible-renderer performance,
subjective localization or physical output latency. The SDL-only compile/run
also passes with no OpenAL linkage at
`build/workflow/20261022T155833.973257Z/`.

The final shared macOS tree passes the complete `required` gate and supplemental
`full` profile. The unchanged-source `test` (48.90 s), `verify-stage1` (500.03 s),
`native-release` and software/Metal menu results are reused from
`build/workflow/20261022T160051.739352Z/`. The clean complete `oracle-test`
passes in 3,737.13 s at `build/workflow/20261022T162126.082489Z/`, including
all nine playback and six ownership/reference controls, both fallback checks,
all retained fixtures and all 146 recovery plus nine baseline CPU controls.
Existing legacy concurrent-input skips retain their stated reasons; the
spatial ownership and native lifecycle comparisons skip nothing. Supplemental
`full` passes at `build/workflow/20261022T161927.407826Z/` (36.44 / 7.80 s).
Source/harness hashes remained unchanged during the final gate; the retained
manifest is `build/oracle/spatial-audio/gate-source-snapshot-20261022.json`.

The first oracle attempt found a stale widescreen mutation signature after the
shared HUD change. Updating that probe preserves the same native-memory
corruption and rejection witness; all eighteen width/control combinations pass
at `build/workflow/20261022T161923.955307Z/` and in the clean complete rerun.
No fixture or prior mutation was removed. This closes local software validation,
not physical surround/localization, other-platform device acceptance or
continuous whole-credit MAME audio equality. The user could not listen to the
revised passing/recovery changes today.

The high-resolution 420-5812 diagram, printed pp. 63–64, shows two rear and two
front 8 Ω / 15 W speakers on duplicated stereo amplifier nets. The connector
trace is in [the spatial plan](SPATIAL_AUDIO_PLAN.md#virtual-cockpit-routing).
This corrects the earlier single-rear-speaker interpretation. It establishes
the original Deluxe wiring, while unchanged applicability to an After Burner II
cabinet revision and exact listener angles remain unverified. Independent
front/rear sound categories are an enhancement rather than separate original
electrical channels.

The diagnostic version 2.00 in service manual 420-5842-01 pp. 1–4 matches the
ABII main program at `$32D63..$32DA2`; the sound-test title starts at `$33106`.
Shared lookup `$1BCD2..$1BCEE` bounds the selection below 17 and enqueues its
table byte through `$B840`. Category dispatchers and matching display rows
establish the neutral identities above: EFECT A table `$330C6`, indices 4/6,
display rows `$33836/$3388A`; EFECT B table `$330D8`, index 6, row `$33A22`;
VOICE table `$330EC`, index 5, row `$33B90`. `$9A/$D8/$EF/$F2/$F8` are absent
from these named tables. Service labels for `$6E/$6F` are respectively ENM MIS1 R
and L1 under ENM MIS0; they are different variants, while their live producer
independently establishes the passing-depth rule. No matching-pair identity is
assumed from those labels.

## Offline sprite extraction and mapping (2026-10-22)

`tools/assets/extract_sprites.py` reconstructs the Sub CPU and sprite regions
with the same two-/four-way chip interleaving as `roms.cpp`. Its structural scan
of aligned records in Sub `$10000..$1FFFF` finds 2,931 candidates in the user's
ROM. Width, last-row, bank, word address, pitch and vertical-flip adjustment
follow the frame format consumed by Sub `$7B8..$9C8`. Export uses forward,
unscaled pens, eighth-pixel-inclusive end-word handling and 16-bit address wrap.
No execution, bus, renderer or gameplay logic changes.

`make extract-sprites` writes indexed PNGs and `build/sprites/mapping.json`,
with stable Sub-address IDs, source hashes, dimensions, original addressing,
pen hashes, editable label/replacement fields and the signed ROM LOD offsets
from Sub `$9E4`. It refuses existing output directories and restricts CLI output
to ignored `build/`. PNG palettes are grayscale previews; dynamic game palette
assignments, object names, animation membership, runtime replacement loading
and reachability/completeness of the candidate scan remain unverified/open.
Arithmetic LOD links are explicitly candidates. See
[format and usage](SPRITE_EXTRACTION.md).

Focused validation (not whole-host acceptance):

- `test` and `verify-sprite-extraction` pass on the final executable sources:
  `build/workflow/20261022T154646.936627Z/`. Five ROM-free format/export checks
  pass, including wrap, marker placement, PNG CRC/index/transparency roundtrip,
  mapping serialization and overwrite refusal. All **2,931 of 2,931** candidate
  pen images match the unchanged `HWSprites` reference, **0 skipped**; four
  retained nibble-order/end-marker/pitch/bank mutations are rejected.
- `oracle-video-rolls` passes against the retained independent MAME capture:
  **160 of 160 RGB frames identical**, zero mismatched pixels and zero skips;
  `build/workflow/20261022T154522.873050Z/`. This validates the reference renderer
  in that captured scene scope, not independent MAME coverage of every exported
  record. No new CPU port or oracle instrumentation was introduced.
- Actual export passes in `build/workflow/20261022T154442.423434Z/`; outputs stay
  under ignored `build/sprites/`. A cloud PNG was inspected visually; diff and
  output-ignore checks pass. Original fixtures and mutation coverage are retained.

## High-DPI HUD text presentation (2026-10-22)

The local Video menu now offers Original (default), Sharp 3x and, when a valid
atlas is loaded, Custom. `--hud-atlas path` selects a user indexed RGBA PNG;
the atlas path/mode persist with the normal Video settings. The SDL window
requests high pixel density. Glyphs draw directly at the output resolution,
inside the centered native safe area for widths 320/358/398. Details and art
format: [HUD presentation](HUD_PRESENTATION.md).

This uses verified hardware text ownership rather than `OHud`/`HudState`, which
belong to the historical reconstruction. The existing scene capture at native
width 320 supplies immutable partial frontiers. `WideScene` can omit text from
its detached underlay and publish `HudClip` entries/palette pens plus a native
sprite occlusion mask. The frontend resolves pen indices from the live palette,
including hardware effects colours, and respects clip boundaries through a
glyph. Category-1 text remains above sprites; category-0 text is hidden or
shadowed/highlighted by the native top priority-3 sprite. Display-disable clears
replacement coverage. Existing Original raster rendering is the default path.
No CPU routine, bus, timing or native hardware compositor changed.

Sharp 3x uses the existing Scale3x algorithm on each ROM glyph's categorical
pens, followed by linear output sampling. Custom accepts 16..128-pixel tiles;
empty tiles fall back to nearest native art. Palette-keyed textures use
premultiplied alpha and a 1,024-entry / 32-MiB bound. Malformed atlas reloads
preserve the previous atlas. Native-width HUD presentation disables detached
visibility extension and redundant native sprite-plane captures; wider modes
retain the existing wider scenery. The menu skips unavailable Custom so both
directions and mouse/Enter cycling can always return to Original.

Initial focused evidence:

- `verify-hud-presentation` passes at
  `build/workflow/20261022T155412.685907Z/`: **789 of 789 native RGB/PCM/time
  frames identical, 0 skipped** (781 physical-control frames and eight after
  restart). Independent planar-ROM reconstruction restores exactly the native
  text raster over the text-free underlay at all three widths, including
  capture disable/re-enable while paused and restart readiness. Across three
  aspects this observes 2,217 clips and 3,482,709 opaque glyph pixels. This
  natural bridge contains category-1 text only; category-0 hidden/effects
  branches are covered by explicit synthetic fixtures, not natural evidence.
- `test-hud-scene` passes at `build/workflow/20261022T155540.086863Z/`.
  Ten retained ownership/underlay negative controls reject category, priority,
  shadow, column, effects-palette, frontier, baked-text, cache, detached-bank
  and display-disable changes, **10 of 10 rejected, 0 skipped**. No fixture or
  prior mutation was removed.
- `test-native-hud` and actual Metal `verify-gpu-hud` pass at
  `build/workflow/20261022T160011.584726Z/` and
  `build/workflow/20261022T160026.421834Z/`: 3840×2160 letterbox and Integer
  layouts at all three widths retain subnative custom-atlas detail. Indexed
  BMP/PNG, alpha edges, palette/frontier changes, hidden/effects masks,
  fallback, transactional reload, cache churn and caller renderer state pass.
- `oracle-video-rolls` passes at `build/workflow/20261022T155737.749510Z/`:
  **160 of 160 independent MAME RGB frames identical**, 0 skipped, no mismatched
  pixels. It checks the unchanged native renderer in this capture scope.
- The extended `test-hud-mutations` passes at
  `build/workflow/20261022T160256.586038Z/`: all ten scene controls plus the
  retained straight-alpha blending control reject, **11 of 11, 0 skipped**.
  CMake configures/builds both ROM-free HUD tests and CTest passes **2 of 2** in
  `build/hud-cmake/` with SDL3 and OpenAL disabled for that independent build.
- Complete local `verify-stage1` passes at
  `build/workflow/20261022T160051.739352Z/02-verify-stage1.log`, including the
  new HUD integration and all retained directed stages at both wide aspects.
- Final-tree `test` passes, including both retained HUD mutation scripts, at
  `build/workflow/20261022T160903.410240Z/01-test.log`. Native release builds at
  `build/workflow/20261022T160051.739352Z/03-native-release.log`.
- Final software `verify-native-menu` and Metal `verify-gpu-native-menu` pass
  in the same `20261022T160051.739352Z/` run (logs 04 and 05). They include HUD
  navigation/wrap and unavailable-atlas cycling; GPU sprite comparison retains
  **4,128 of 4,128 identical planes, 0 skipped**. The initial sandbox GPU attempt
  exposed no displays; the successful rerun used normal macOS display access.
- The shared complete oracle attempt at
  `build/workflow/20261022T160345.216180Z/01-oracle-test.log` stopped on the old
  `native-feedback` textual mutation anchor after `WideScene::draw` gained
  `original_hud`. The anchor/replacement signature now includes that argument;
  the deliberate native sprite-byte corruption and rejection witness are
  unchanged. Focused `verify-wide-mutations` passes at
  `build/workflow/20261022T161542.909355Z/01-verify-wide-mutations.log`:
  both independent geometry baselines pass and **18 of 18 retained controls
  reject, 0 skipped**. This repair does not turn the stopped complete oracle
  attempt into a pass; its complete rerun remains a separate gate.
- The subsequent clean complete `oracle-test` passes at
  `build/workflow/20261022T162126.082489Z/01-oracle-test.log` (3,737.13 s).
  Combined with final-tree `test` at `20261022T160903.410240Z/` and the retained
  `verify-stage1` pass above, the integrated tree passes the complete required
  gate. Supplemental `full` also passes at
  `build/workflow/20261022T161927.407826Z/`. The 365-file gate source/harness
  manifest at `build/oracle/spatial-audio/gate-source-snapshot-20261022.json`
  was rechecked after completion: **365 of 365 SHA-256 hashes unchanged**,
  including the HUD renderer, main integration, tests and both new mutation
  scripts. Existing oracle concurrent-input skips retain their stated reasons;
  the HUD's 789-frame comparison and retained negative controls skip nothing.
- Actual O2 Metal gameplay smoke runs with Sharp HUD, ScaleFX, corrected ground
  and all four opacity options pass at native width (180 frames) and 16:9
  (420 frames), using isolated settings and dummy audio. Captures at
  `build/oracle/hud-{sharp,wide}-smoke.png` were visually inspected; output is
  1920×1344 and 2388×1344 respectively, confirming Retina density. Profile logs
  are alongside them. These concurrent-load smoke runs report roughly
  58.9–59.6 Hz at native width and 55.4–59.6 Hz wide; they are layout/functional
  evidence, not sustained performance acceptance or a 4K gameplay benchmark.

This is a local text-plane preview. Sprite-based instruments/target markers,
semantic instrument replacements, new anchoring/layouts, production replacement
art, exhaustive scenes, sustained 4K performance and other-platform high-DPI
acceptance remain open. These focused passes do not declare whole-host or
whole-credit acceptance or refresh a published package.

## Optional CRT display presentation (2026-10-23)

The native Video menu now offers CRT Mode Off / Subtle / Arcade and independent
Scanlines / RGB Mask strengths. Off is the default. Subtle starts at 25/15%;
Arcade starts at 50/35%. Presets restore both starting values, Off retains them,
and strength edits clamp to 0–100% in 5% steps. Validated percentages and mode
persist in `AB2SETTINGS1`; older files remain Off. See
[CRT display](CRT_DISPLAY.md) for controls, rendering policy and limits.

`frontend/native_crt.hpp` modulates the final SDL game/HUD image before menus
and statistics. The scanline envelope follows 224 native rows; its phase uses
integer output rows so exact 2× scaling produces separate beam/gap rows.
Contrast fades below 2× to reduce aliasing. The RGB mask follows physical output
columns. Neither effect reads/writes board state or changes the source image.
The cached output-resolution MOD texture needs no framebuffer readback, game
copy, new shaders or runtime dependencies. Geometry/strength changes replace
one accepted texture transactionally; its logical pixel data is capped at
64 MiB. Off/zero release it. Failed allocation/draw switches CRT Off for the
session and reports the SDL error without saving that fallback over preferences.

Validation and measured scope:

- ROM-free software pixel/state checks pass at
  `build/workflow/20261023T110145.851830Z/01-test-native-crt.log`.
  Retained controls reject **6 of 6 mutations, 0 skipped**: bypassed pass,
  missing scanlines, missing mask, wrong raster phase, lost viewport and
  enabled-while-Off. Logs and independent baseline are under
  `build/oracle/crt-mutations/20261023T110151.405878Z-67b12fe4/`.
- Metal readbacks pass at
  `build/workflow/20261023T110216.672106Z/01-verify-gpu-crt.log`: all three
  viewport widths, letterbox/integer and fractional layouts through 4K,
  explicit 2×/3× white-field contrast, exact Off/zero/invalid pixel identity,
  renderer state/UI/target preservation, cache reuse and oversized rejection.
  The real high-density resize reaches 1600×1200. The initial hidden-window
  fullscreen API round trip did not assert the actual fullscreen flag and is
  not evidence that the window manager applied the request.
  Tests pump resize events and present once before readback so the swapchain
  acquires the resized drawable.
- CMake configure, CRT/settings targets and both CTest cases pass. Logs are
  `build/crt-cmake/crt-validation-{1,2,3}.log`.
- With 36 same-source samples per mode and vsync requested/reported Off,
  Metal source+present wall time is 8.186 → 8.308 ms at 3840×2160 and
  8.245 → 8.340 ms at 1920×1080 (Off → cached Arcade). GPU idle is awaited
  at aggregate boundaries. The roughly 8.3 ms display/swapchain pacing remains,
  so **0.122/0.095 ms incremental wall time is not an unpaced GPU-cost claim**.
  First-pass/present times are 29.91/9.85 ms, including cache creation.
  Software same-source samples measure 2.006 → 18.764 ms at 4K and
  0.360 → 5.996 ms at 1080p: the software fallback has substantial cost,
  especially before adding emulation work.

Additional local integration and application evidence:

- `test` (54.62 s), `verify-stage1` (448.36 s), `verify-native-menu` (29.88 s)
  and `native-release` (42.31 s) pass at
  `build/workflow/20261023T110249.624684Z/`. These are the targets evaluated
  during that run. Concurrent Practice/aircraft edits appeared in the shared
  checkout during validation; the logs are not a blanket final-tree gate for
  those separate features. CRT and UI integration were checked again below.
- Final software rendering, **6 of 6 rejected controls, 0 skipped**, and
  visible-menu regression pass at `build/workflow/20261023T112840.465224Z/`.
  CRT menu replay preserves **781 of 781 RGB/PCM/time frames, 0 skipped**.
  Final Metal renderer checks pass at `20261023T113048.259801Z/`, including
  **1 of 1 observed hidden-window fullscreen flag round trips, 0 skipped**.
  This does not substitute for the visible-menu fullscreen gate below.
  Final CMake rebuild and both CTest cases pass at
  `build/crt-cmake/crt-final-{1,2}.log`.
- An isolated CMake application build avoids concurrent native Make outputs.
  Three 420-frame Metal gameplay runs use isolated settings with 16:9,
  Sharp HUD, ScaleFX, corrected ground and all four opacity options. At
  `build/oracle/crt-display-20261023T110339Z/`, each Subtle/Arcade capture
  matches **3,209,472 of 3,209,472 independently predicted output pixels,
  0 different, 0 skipped**, with zero channel error against the Off capture
  and the specified modulation. The 2388×1344 PNGs were visually inspected.
  Source/binary hashes, configs, profiles and comparison JSON remain local.
  Each active mode builds exactly one 12,837,888-byte overlay across the run;
  cached submission is roughly 0.004–0.005 ms. These locked-desktop/dummy-audio
  smokes range roughly 55–59.6 Hz in all three modes and are not sustained-load
  or 4K gameplay acceptance.
  After the concurrent scenery changes, a fresh CMake build and all three
  120-frame captures under the same folder's `current/` also pass. Subtle and
  Arcade again match every one of 3,209,472 predicted pixels, zero channel error.
  The current capture was visually inspected and has its own binary/source manifest.
- The later real Metal menu gate fails at its existing fullscreen assertion,
  before any CRT drawing: requested=true but observed fullscreen=false.
  The harness now mirrors the game's resizable visible window, pumps/presents
  before requesting fullscreen, waits up to three seconds and retains actual
  fullscreen/windowed assertions and diagnostic state. It still fails while
  the desktop is locked; it does not replace observation with requested state.
  CoreGraphics `CGSessionCopyCurrentDictionary` independently reports
  `CGSSessionScreenIsLocked=true`. The observed fullscreen check remains
  pending an unlocked desktop. Failed logs are retained at
  `20261023T111525.436567Z`, `20261023T111747.452842Z`,
  `20261023T112032.602834Z` and `20261023T112225.314484Z` under `build/workflow/`.
  The synthetic CRT test now reports unavailable fullscreen observations as
  skipped instead of treating an accepted API request as a successful transition.

This is optional display treatment. Glow, curvature, persistence, general
pacing work, sustained-load acceptance and Windows/Linux runtime checks remain
open. The 2026-10-22 complete hardware/oracle checkpoint retains its original
source and scope; this frontend addition does not claim a new full required
gate, whole-credit fidelity result or refreshed release package.

## Stage Select / Practice first increment (2026-10-23)

The native launcher and pause menu now expose [Practice](PRACTICE_MODE.md):
select a runtime ROM stage, start a fresh session, retry that starting stage
with F5, or return to a cold arcade board. The mode and retry target are
ephemeral frontend state. Browsing and cancelling a different stage preserves
the active target. Normal native lives, damage, score and later progression
continue; no stage loop, invulnerability or Time Attack rules are added.
`--practice-stage N` launches the same mode; natural recordings reject both
selected-stage CLI paths and disable the Practice menu.

`StageCatalog` names each one-based ordinal, ordered selector, descriptor and
event-program address. It reads `$32480/$32482/$3249A` and descriptor byte
offset 16 at runtime, following the already ported `$18918/$13CEE/$142D0`
ownership. It contains no embedded ROM tables. `GameSession` exposes this
catalog and the restricted `enter_practice_stage` operation; the existing
`debug_skip_to_stage` delegates to the same operation. No translated gameplay,
CPU, bus, scheduler or device implementation changes.

`frontend/native_practice.hpp` cold-restarts with current DIP pins, supplies
the established physical coin/start sequence and calls the existing directed
ROM-event setup. The application clears pacing, input holds, haptics,
presentation history and playback queues. Hidden startup updates replacement
music state with playback detached, while draining PCM/music/feedback each
frame. Menu entry/back and the persistent Practice retry label only draw on
the frontend image, preserving the native frame/time and prior pause state.

Independent metadata evidence: `capture_stage_catalog.lua` observes the copied
selector list in original MAME after physical boot/start and records its
descriptor/event fields. **23 of 23 entries identical, 0 skipped**. Four
retained controls (ordinal, selector offset, descriptor pointer and event-field
offset) are rejected by field comparisons against that capture. The final
catalog and mutation checks pass under
`build/workflow/20261023T111336.040619Z/`. Synthetic malformed ROM/count/pointer
and ordinal bounds checks also pass. Capture generation joins `oracle-capture`;
comparison and mutations join `oracle-test`.

The unchanged selected-stage palette oracle passes **56/56 MAME words identical,
0 skipped**, at entries 5, 8, 13 and 17 under
`build/workflow/20261023T110636.269626Z/03-verify-native-stage-palette.log`.
Its earlier command-18 bypass control remains retained. These metadata/palette
observations do not establish complete directed-entry CPU/device/RGB/audio
equivalence or the previous stages' accumulated state.

The new `verify-practice-mode` (included in `verify-stage1`) passes 375 exact
native RGB/PCM/time lifecycle comparisons at entries 1, 8 and 23 plus cold
arcade return, with 2,280 supplied hidden-frame callbacks. Bounds rejection,
DIPs, pause, default/supplied output drains, extension restart and retry pass.
Its reference is the existing debug startup, not independent MAME. A deliberate
changed reference pixel is rejected (`build/oracle/practice-mode-bad-reference.log`).
SDL menu checks cover all stage selections, wrapping, keyboard/pointer actions,
recording guards, candidate versus active retry target and frame/PCM/time/DIP
isolation. The 320×224 menu readback was visually inspected; captures remain
ignored under `build/oracle/`.

Final `verify-native-menu` and release-backed `verify-native-practice` pass in
`build/workflow/20261023T112514.014116Z/`. The CLI rejects zero/out-of-range
stages, conflicting stage modes and natural recording before creating a
recording; Stage 8 launches and presents eight counted frames with dummy SDL.
The final gameplay readback places the Practice starting-stage label below
the score strip, preserving the bottom HUD. The menu and final gameplay image
were visually inspected. CMake configuration and `git diff --check` pass.

`test` passes on the settled combined source in
`build/workflow/20261023T112711.644483Z/01-test.log`. A later CRT regression-test
edit is separately covered by clean `test-native-crt` / `test-crt-mutations`
in `build/workflow/20261023T112949.720457Z/`; runtime sources stayed unchanged.

The complete `verify-stage1` passes (423.86 s) in
`build/workflow/20261023T112711.644483Z/02-verify-stage1.log`, including all
23 directed stage slots at each aspect: 22,540 native RGB/PCM/time frames
unchanged at 398×224 and another 22,540 at 358×224. The source/harness manifest
at `build/workflow/practice-settled-source-manifest.json` confirms that runtime
sources remained unchanged throughout this final run. Earlier concurrent
aircraft build/text-ownership failures were repaired before this clean pass;
their fixture coverage is retained.

Complete entry-state fidelity, broader physical-controller/platform acceptance,
Time Attack and natural whole-credit coverage remain open. Published packages
are unchanged. This local feature is not a new full required acceptance gate.

## HD player-aircraft presentation prototype (2026-10-23)

`--player-aircraft image.png` enables the first transparent HD aircraft pose.
Main `$99BE..$9A48` supplies the bank/pitch selection through `$28C94`; pitch-2,
bank-0 selects Sub `$100A0`. `PlayerAircraftAssets` reads the actual frame and
Sub `$9E4` LOD offsets from the user's ROM. Palette `$F2`, bank, pitch, exact
forward/reverse source start and native draw directions identify this family;
palette alone never establishes ownership. Offscreen-adjusted starts, duplicate
matches and unmatched poses retain native art. All five neutral records have
width equal to pitch times eight; other layouts are deliberately unsupported.

The opt-in `WideScene` branch omits only the accepted descriptor from its passive
underlay and publishes a rectangle plus native-coordinate per-clip visibility.
Native tile/text priority and later raw sprite coverage clip the authored art.
Later shadow or enabled translucent-effect overlap retains the native aircraft
for that clip, since fullcolor art has no categorical effect palette and a
post-composition HD draw cannot supply the underlay for earlier alpha blending.
Detached HUD keeps priority-3 aircraft native as well: authored aircraft holes
and glyph coverage can differ from the original HUD's native ownership mask,
including a custom replacement of an originally blank glyph. The observed
active-flight aircraft uses priority 1 and remains supported.
The default branch, board buffers, sprite feedback, CPU code, bus and timing
are unchanged. `NativePlayerAircraft` loads transactionally, trims transparent
canvas padding, premultiplies alpha and draws HD samples before CRT presentation.
No preferences/menu row or release package changes are required for this CLI
prototype. The authored PNG, provenance and exact built-in ImageGen prompt live
under `assets/player-aircraft/`; extracted references and screenshots stay ignored.

Focused evidence:

- `test` passes at `build/workflow/20261023T112502.766319Z/`, including **ten
  identity/ownership** and **six frontend sampling** mutation rejections, zero
  skips. Retained sources/logs are under the run's referenced
  `build/oracle/player-aircraft-mutations/` and
  `build/oracle/native-player-aircraft-mutations/` folders.
- `preview-player-aircraft` passes at
  `build/workflow/20261023T112502.766319Z/`: **1,804 of 1,804 native RGB/PCM/time
  frames identical, zero skipped**, with independent hardware-renderer/native
  sprite restoration at 320/358/398. The 1,800 physical-input frames exercise
  198 accepted draws across three aspects, including 174 in mode `$10`; first
  active-flight input frame 1098. Modes `$01` and `$10` reuse the same pose.
  Only base `$100A0` is naturally observed here; four smaller LODs are generated
  matcher fixtures. Eighty generated LOD/direction cases, fractional zoom,
  source holes, later ownership/shadow/translucency, duplicate fallback,
  native/custom HUD priority, two palette/row frontiers, toggles and reset pass.
  The saved in-game HD preview was visually inspected.
- `verify-gpu-player-aircraft` passes with actual Metal at
  `build/workflow/20261023T111821.929802Z/`. The same assertions exercise native
  and 3840×2160 outputs, all viewport origins, premultiplied blending, flips,
  native/caller clips, malformed scene inputs and detail within a native pixel.
  The first sandboxed attempt could not enumerate displays; rerunning with
  desktop access passed without changing comparisons.
- `oracle-video-rolls` in that same run preserves **160 of 160 independent MAME
  RGB frames**, zero mismatched pixels and zero skips. This rechecks the original
  renderer reference used by restoration, not production acceptance of new art.
- The broader `verify-stage1` pass at
  `build/workflow/20261023T111215.472189Z/` predates the last detached-HUD
  priority-3 fallback and the extended replay fixture; it is not a final-tree
  full-gate claim. After that presentation-only change, `native-release`,
  `verify-hud-presentation`, `verify-wide-scene`, `verify-wide-geometry` and
  `verify-native-menu` and all 18 retained widescreen geometry mutations pass
  at `build/workflow/20261023T112416.461549Z/`.
  The final `test` and 1,804-frame replay above include the new fallback and
  rejected tenth ownership mutation. Core/CPU sources and oracle instrumentation
  were not changed for this prototype.

The art is a first neutral-pose prototype. Other pitch/bank/crash/carrier assets
retain native artwork unless they reuse that exact neutral descriptor. Nozzle
registration, consistent remaining animation art, a versioned registered asset
pack, effect-palette materials, natural smaller-LOD coverage and sustained
Windows/Linux/backend acceptance remain open. This is development validation;
it does not close full-credit fidelity or replace the complete merge/release gate.
See [usage and scope](SPRITE_REPLACEMENT.md).

## Registered aircraft pack and gentle bank (2026-10-23)

The follow-up accepts `--player-aircraft pack.aircraft` and supplies
`assets/player-aircraft/neutral-gentle-v1.aircraft`. It includes the accepted
neutral image plus `gentle-bank-hd-v2.png`, generated with built-in ImageGen
against the neutral design and a local native pose. Art/provenance remain in
`assets/`; no extracted ROM pixels or captures are distributed there.

`AircraftPack` defines versioned, bounded artist source rectangles, reference
dimensions, corresponding art/native pivots, scales, nozzle review points and
optional LOD registration. It validates finite scales in 1/4096..4096, duplicate
identities, bounds and confined image paths. `NativePlayerAircraft` stages all
images, registrations and admitted roots transactionally. A bad reload retains
the previous complete pack. A LOD override incompatible with actual ROM source
dimensions uses the valid base registration. The legacy PNG/BMP path remains
neutral-only and retains its previous alpha-bound fit.

The matcher still reads runtime ROMs. Pitch-2's main table at `$28E58` points to
neutral Sub `$100A0` and uses `$10230` for both signed banks -1/+1. Main
`$99BE..$99E4` supplies request bit 13 and signed quotient; Sub `$8FE..$904`
chooses the reversed source. Base bank +1 has its left wing high/right wing low;
bank -1 reflects it through the original hardware flags. Each family contributes
five runtime LOD records. Only loaded, verified root families may be detached;
unknown roots, source aliases and unsupported descriptors retain Original.
All 55 normal-flight table selections reuse 22 base records; this increment
authors two of those records. Three additional sources complete this pitch row.

Nozzle annotations are visual guide points, not new flame-position rules.
Connected F2 glow pens 1/2/4 identify native image-space centers at neutral
(43,32)/(78,32) and gentle (45,31)/(78,36), counting pixel centers at n+0.5.
The main aircraft table's four words after its frame pointer are collision
bounds used at `$1513A..$15170`, not nozzle attachment offsets. The actual
flame-table ownership remains `$AF88..$AFB0` / `$B018..$B040`; no CPU code,
collision bounds, flame placement, bus or timing was modified.

The longer physical replay exposed a real widescreen bug: suppressing the
whole player descriptor removed its overhanging native pixels, while the HD
mask covers only 320 columns. A safe-area fallback now preserves Original
whenever the full candidate rectangle crosses either native horizontal edge.
Generated tests cover both families/edges at 320/358/398; the failing physical
sequence is retained. Existing shadow, enabled-translucency, detached priority-3
HUD, duplicate and adjusted-source fallbacks remain intact.

Visual review also exposed SDL software reflection reducing high-frequency
authored detail to native-cell averages. A controlled 240-stripe/120-cell probe
measured flipped contrast collapsing to zero at 3x and 4K width. The software
path now caches H/V/HV premultiplied textures at load time and samples them
through ordinary texture draws; GPU rendering retains its existing path.
There are no per-frame allocations or pixel copies. Pack texture budgets count
all cached orientations (32 Mi texels total); the existing 4096-square legacy
image allowance implies at most 64 Mi cached texels on software.

A retained fractional-size test then exposed an older software-only seam:
SDL truncated each one-native-row destination independently at noninteger
output scales, leaving blank physical rows. Software draws now quantize shared
destination endpoints by physical pixel-center ownership. SDL's software queue
casts source crop coordinates to integers, so the authored source intervals
remain unchanged to retain their existing sample phase. The transform accounts
for logical presentation, user scale and the actual viewport offset without
modifying renderer state. Adjacent rows meet exactly; transparent art is blended
once, and unowned cells stay untouched. GPU geometry is unchanged.

Validation records:

- Final `test` at `build/workflow/20261023T155902.949407Z/` passes, including
  **21/21 frontend and 14/14 matcher/scene negative controls**, zero skips.
  Existing strict 4K sample checks are retained. Added cases cover all four
  reflection combinations at exact 3x and 3840x2160 for legacy images and
  registered packs at 320/358/398. Fractional 960x672 windows at 358/398 scan
  owned pixel centers, uniform half-alpha seams, a deliberate mask hole and all
  half-open outside edges. Actual Metal passes `verify-gpu-player-aircraft` at
  `build/workflow/20261023T155949.048164Z/`.
- Final `preview-player-aircraft` and `native-release` pass in that final
  `20261023T155902.949407Z` run. The replay retains the original 1,800-input
  prefix and extends it to **3,000 physical inputs plus four restart frames**,
  exact native RGB/PCM/time and zero skips. Across all three viewports it
  observes 1,260 replacement draws, including 1,206 in active flight: 978
  neutral, 42 gentle unflipped and 186 gentle mirrored. Independent original
  sprite restoration checks 7,649,706 visible ownership pixels, 6,151,425 source
  holes and 1,495,263 restored pixels. Another 7,740 views retain Original.
  The production pack generates visually reviewed gameplay previews at input
  frames 1098/1928/2552 under ignored `build/oracle/`; no board state or aircraft
  descriptors are seeded. Smaller LODs use generated coverage, not a claim of
  natural-flight observation.
  A bounded dummy-video/audio release smoke exits successfully with the real
  `.aircraft` CLI option and reports two loaded pose families; its complete log
  is `build/oracle/player-aircraft-pack/final-release-smoke.log`.
- `oracle-video-rolls` at `build/workflow/20261023T153556.902170Z/` preserves
  **160/160 independent MAME RGB frames**, zero mismatched pixels and zero
  skips. This checks the unchanged original renderer used for reconstruction;
  the capture does not naturally exercise the two new aircraft identities.
- The broader `verify-stage1` at
  `build/workflow/20261023T154445.417557Z/` passes, including both retained
  23-stage sweeps (22,540 native RGB/PCM/time frames per viewport), the aircraft
  replay and other presentation/lifecycle checks. The same run passes
  `native-release` and all 18 widescreen geometry mutations. It predates the
  final fractional software endpoint fix. A source hash audit also records
  concurrent unrelated GameSession/XBoard/frontend/build edits, so this is
  evidence for that recorded checkpoint, not a final-tree whole-host gate.

This is a two-pose development increment. Complete pitch/bank animation,
production nozzle/silhouette registration, dedicated smaller-LOD art, palette
animation/effect materials, broader natural/platform coverage and an Original/HD
menu setting remain open. It does not close whole-credit fidelity acceptance.

## RetroArch Slang presentation (2026-10-23)

The optional `NativeSlang` frontend loads user `.slangp` presets through
librashader 0.12.0 (C API 5, ABI 2). Its pinned Apple Silicon runtime is fetched
and verified under ignored `build/deps/`; no runtime or shader pack is committed.
The upstream MIT C header and MPL license/provenance are retained under
`src/third_party/librashader/`. See [setup and scope](CRT_DISPLAY.md).

SDL's GPU renderer exposes no public native Metal handles suitable for this
runtime. Shader mode therefore uses SDL's native Metal renderer and existing
CPU sprite filters; Off/Subtle/Arcade retain the normal GPU path. A native
320/358/398×224 source and physical-output texture share IOSurface storage
between SDL and Metal. An explicit fence, command submission and completion
waits order their use without CPU image readback. Game/HUD are rasterized to
224 rows, including HD replacements; frontend UI draws after the shader.
Switching backends recreates renderer resources, retains the GameSession/menu
and decoded HUD, and reloads selected aircraft art. The main-thread settings
transaction accepts/persists a preset only after successful loading; failed
loads restore the previous renderer/settings. File-dialog callbacks queue
owned strings through a mutex and never touch renderer/menu state.

Paused output is cached by source frame and presentation geometry; visual
changes, menu close, restart and frame rollback invalidate it. NativeSlang
restores target/logical/scale/viewport/clip and draws the captured original on a
runtime failure. The pinned Metal runtime emits an invalid render pass when
clearing an empty history list. History resets recreate a chain and reapply
parameters, covering history and feedback without that broken API path. The
C header's milliseconds comment is also incorrect: tagged runtime binding
forwards `frametime_delta` unchanged to Slang `FrameTimeDelta`, whose unit is
microseconds. The host passes microseconds and tests this independently.

Validation and retained local evidence:

- `test` / complete `verify-stage1` pass in
  `build/workflow/20261023T154054.724187Z/` (87.94 s / 468.09 s). These cover the
  applicable engine/presentation integration, with the menu/settings headers
  frozen before the run; the real-main shader path is covered separately below.
- Final settings/menu checks include persisted shader paths, rejection of
  injected line/NUL values, picker activation, disabled built-in strengths,
  mode wrapping, renderer detach/rebind and pixel-identical font layout.
  Logs: `build/workflow/20261023T153731.406930Z/` and
  `build/workflow/20261023T153805.994146Z/`.
- Both normal Make and native release builds pass; final focused settings and
  fetcher checks also pass in `build/workflow/20261023T155847.178387Z/`. Independent
  CMake Release builds and actual shader/main integration targets pass, with
  `STAGE1_ROM_TESTS=ON`, logged in `build/slang-cmake-verify.log` (OpenAL disabled
  in this separate CMake check; the Make native build retains OpenAL).
- `verify-native-shader-main` passes in
  `build/workflow/20261023T155425.134099Z/`: the real main loop accepts a
  worker-thread chooser result, switches GPU→Metal→GPU, attempts an invalid
  preset and restores GPU plus accepted settings. Paused native RGB/PCM/time
  remain identical, zero skips; no new PCM buffers/samples are consumed.
- The same run's `verify-native-slang` passes every authored reference pixel,
  actual renderer state, raw microseconds, resize through 4K, exact original
  output on forced reset failure, recovery, live previous-frame history and
  history reset. The temporal reference observes red `(128,0,0)`, then blue
  with previous red `(128,0,128)`, cached identity, then reset `(0,0,128)`,
  within one RGB level.
- `verify-native-slang-mutations` passes in
  `build/workflow/20261023T155635.512469Z/`: both millisecond conversion and
  omitted temporal reset are rejected at their intended assertions, **2/2,
  zero skipped**. Staged sources, fixtures, binaries and complete logs remain
  under `build/oracle/slang-mutations/20261023T155636.694251Z-526430ce/`.
- The ten offline fetcher tests pass, including pinned checksum/integrity,
  extraction path/link/size checks, installation reuse, failure cleanup and
  preservation of an existing invalid installation. The real installation
  also validates against its pinned manifest. Logs:
  `build/workflow/20261023T154532.107963Z/`.
- Real installed CRT Easymode, CRT Geom and 12-pass CRT Royale presets render
  successfully. Three 180-frame Stage 1 game smokes at 2388×1344 with 16:9 and
  Sharp HUD retain images/logs under `build/oracle/retroarch-game-20261023/`.
  Profiles observe `shader=active` and 17/20/46 parameters. Completed shader
  submission/waits cost approximately 2.7–3.1 / 4.4–4.7 / 5.1–5.4 ms respectively;
  finite displayed-rate samples are 55.3–59.6374 Hz with dummy audio and
  concurrent validation. These are smoke measurements, not sustained load
  acceptance. Main startup also succeeds with dummy video and a requested
  shader by falling back to the normal renderer, recorded in
  `build/oracle/slang-startup-fallback.log`.
- Built-in CRT GPU and visible native-menu checks pass in
  `build/workflow/20261023T155425.134099Z/`. Actual fullscreen/windowed transitions
  now pass, resolving the earlier locked-console observation. Native replay
  preserves **781/781 RGB/PCM/time frames**, zero skipped, and GPU sprite planes
  preserve **4,128/4,128** pixels, zero skipped. All six built-in CRT negative
  controls remain retained and pass their rejection gate.

The original/native board remains the comparison reference. This frontend
increment does not extend the earlier complete hardware gate to every later
concurrent enhancement or replace a required merge/release gate. macOS Metal
SDR is the implemented shader backend; Windows/Linux, legacy GLSL/Cg, HDR,
first-input mipmaps, parameter UI, universal preset compatibility and sustained
scene/preset performance remain open. Published packages are unchanged.

## Shuffle mode (2026-10-23)

### Inherited music correction (2026-10-24, Windows)

Seed `12345` opens on source stage 12. Before the correction, its 180-frame
post-startup music stem had zero energy: only the boot song initialized.
Main `$DF4E..$DF5E` reads the stage's `$2B604` command; the sound handler table
at `$08B3` maps the `$80` continue command to `$0C91` (`RET`). Moving that
row alone loses its original predecessor's song. The owned Shuffle view now
resolves preceding original commands for continue rows when the new adjacency
does not supply the same history. Explicit controls and same-history continue
rows are retained. Canonical ROMs and literal CPU/device code are unchanged.

The new `shuffle_session_integration --music` check observes Final Take Off
initialization and music energy **192,479,584** over 180 post-startup frames
with that seed. Its pre-fix probe measured **0**; the new ROM-free inherited
command check also failed before the fix. The fixed synthetic policy passes,
including identity order, continue-only openings and explicit controls. All
eight ROM-free Shuffle mutation controls reject their intended faults. The
full focused session run passes 660 observer-neutral RGB/PCM/time frames,
16 canonical-host restoration frames and a 7,118-frame three-position run.
A rebuilt Windows Release executable completes a 360-frame Vulkan Shuffle
smoke test with no reported audio underruns. Logs are local under
`build/shuffle-music-*`, `build/shuffle-session-fixed.log` and
`build/smoke-shuffle-fixed.log`.

The nine existing focused CTest checks pass (the settings path test required
an unsandboxed rerun). Real-ROM data checks reached the retained-palette gate,
but the required MAME captures are absent on this host, so `--roms` as a whole
did not pass. No new independent MAME equivalence, complete ending run or
whole-host acceptance is claimed for this local correction.

The root and pause menus now offer [Shuffle](SHUFFLE_MODE.md) alongside
Practice. A fresh Shuffle permutes gameplay stages 1–22 exactly once, keeping
the native stage-23 finale last as requested by the user.
F5 restarts the same seed/order; New Shuffle draws another seed, while Return
to Arcade and Practice restore canonical ROM storage. `--shuffle` and
`--shuffle-seed N` use the same startup. Natural recording rejects these
options before creating files, and disables the enhanced menu entries.

`Shuffle::RomView` deep-copies all ROM regions and binds them for the entire
host lifetime. It remaps the setup/road words at `$32196/$32324/$32452/$30D54`,
four `$40`-stride DIP banks at `$32352/$32224`, music `$2B604`, text `$2B6AC`,
inline palette `$2EA16`, the five pointer tables `$140DA..$1424E`, and the
ordered selector/descriptor/event graph. Copied palettes and event programs
occupy the already mapped upper main-ROM data area. Startup sources
`$2BC60/$2CFF4/$2DBF4/$2BF60` are replaced only after indexed palette payloads
are cloned, preserving the original first stage's colors wherever it appears.
Instruction bytes and canonical ROM regions remain unchanged.

The literal CPUs retain native progress, lives, score and update timing. The
hardcoded position-based collision branches at `$E6C4/$E6D0`, animation threshold
at `$11844`, and carrier variant at `$12540/$13090` retain their original run
positions. This is explicit enhanced policy, not a claim that every behavior
travels with a source stage. Stage 23's scripted aircraft owns an early-ending
flag as well as the native final-position check; keeping this finale last
preserves the original ending sequence.
Stage transitions do not use Practice skips, board resets or hidden frames.

Original event command 18 prepares the original adjacent destination and is
omitted from copied event programs. The typed policy instead installs the
selected destination's settled alternate palette at the original `$138FE`
first/new-credit or `$13CE8` later-stage initialization completion. Which source
stages require preparation is discovered from the original predecessor events.
This replaces the original 128-update preblend; other source palette animation
remains native. The helper writes exactly 25 words: one at `$122F00`, two at
`$122E60`, eight at `$122E10`, and fourteen at `$122F02`. It preserves the
fourteen-word sky tail `$122E64..$122E7F`. Synthetic policy writes do not enter
CPU transaction observations; palette generation is explicitly invalidated.
The host's existing filtered presentation observer adds only `$138FE`.

Independent original-MAME palette fixtures at entries 5, 8, 13 and 17 compare
**156/156 observed words identical, 0 skipped**, including all 100 written
words and preserved sky tails. Four retained mutations reject gradient,
auxiliary-source, missing-write and sky-tail corruption. An initial incorrect
repeated sky upload failed this independent comparison and was corrected;
failed logs remain in `build/oracle/shuffle_palette_initial_failures/`.
The real-ROM remapping check and seven semantic controls cover all setup/DIP/
selector/event/palette mappings, canonical immutability, startup aliases,
permutation validity and instruction preservation. These pass in
`build/workflow/20261023T155851.333164Z/` and
`build/oracle/shuffle-rom-mutations/`. The final fixed-finale policy and seven
controls pass in `build/workflow/20261023T162455.546971Z/`; existing valid
arranged images remain byte-identical. The original selected-stage palette
reference remains **56/56 words identical**.

The arranged-run oracle exports only a declared private ROM view, seed/order
and physical requests. Original MAME independently executes that data view,
with its own settled-palette hook; native expected states, CPU registers and
RAM are never supplied. Its actual port observations are replayed, preserving
the established digital-port update latency; every request-to-observed relation
and DIP byte is checked. The clean run compares **12,000/12,000 frame rows
identically, 0 skipped**, with mode, run position, lives, raw score and exact
nanosecond time. It reaches five flight positions, seven modes and source
8 → 5 → 17 at the first three positions. Log:
`build/workflow/20261023T160914.464696Z/01-verify-shuffle-run.log`.
The earlier requested-versus-cached-port comparison failures and both capture
attempts remain retained; their observation CSVs were identical. Full CPU,
device, RGB and PCM equivalence is not inferred from these named observations.

The session lifecycle gate preserves 660 native RGB/PCM/time frames with
presentation/music/cockpit/feedback captures enabled versus disabled, plus
16 original-board restoration frames. It checks DIP state, pause, output
queues, extension resets, invalid plan rejection, deterministic retries,
canonical restoration and a continuous 8 → 5 → 17 prefix. That prefix starts
with 297 ships from 99 stocked credits under the declared Easy/three-ship DIP
profile; this is not a standard single-credit completion claim. Its clean log
is `build/workflow/20261023T160325.580086Z/01-verify-shuffle-session.log`.

SDL menu checks and release-backed CLI launches pass under
`build/workflow/20261023T155851.333164Z/` and
`build/workflow/20261023T160431.628552Z/`. Seven CLI guards reject recording,
conflicting modes and invalid seeds. Seed zero reproduces its complete order
and presented image; seed one differs. Practice's four guards and directed
launch also pass. Root/Shuffle menus and the gameplay label were visually
inspected from ignored readbacks. CMake configures with oracle tests enabled
and Stage 1 integration disabled, and retains separate ROM-free/ROM/oracle
registrations.

An exploratory first-source-23 test exposed the scripted finale dependency:
`$11D4A` sets `$89E2.bit0`, and `$DD10/$DD16` consumes that flag and branches
to `$E04E` without the ordinary stage-count check. The aircraft callback also
updates `$910C`. At the first run position, `$E04E` decrements the ordinal to
`$FFFF` and requests mode 17. Independent MAME and native both observed flight
at frame 3,495 and ending at frame 4,039, followed by ranking and attract.
Those failed endpoint captures are retained as
`build/oracle/shuffle_complete_early_end.*`; the interrupted native and required
runs remain logged. The user then specified keeping stage 23 as the ending,
since it has no real gameplay. `validate` now rejects a moved finale, and seeded
Fisher–Yates shuffles only the first 22 entries. No terminal flag, aircraft
callback, CPU state or instruction bytes are changed to bypass the finale.

The longer shuffled run also exposed the original paired-selector cleanup
continuation `$67B0..$6836`. Twenty absent sites are now literal generated
translations: selector upper-bound addition/comparison, inclusive BLE,
paired object depth/mask cleanup, two queue-byte increments/indexed writes,
owner callback restoration and flag update. Independent original MAME supplies
**180/180 instruction fixtures identical, 0 different, 0 skipped**, with 564
bus phases, all 32 incoming branch CCRs, both BLE outcomes (8/24), ADD/CMP
boundaries, nonzero masks, queue-byte wrap and neighboring sentinels, and
zero/max indexed writes. All **20/20** site-specific controls reject by state
or bus comparison. Independent review checked instruction order, flags,
operands and operand coverage. Logs:
`build/workflow/20261023T161243.614662Z/` and
`build/oracle/shuffle_paired_release_mutations.log`.

The unchanged earlier recovery corpus is retained, including a backup before
merging the new independent fixtures. The complete replay passes
**14,014/14,014 baseline + 64,120/64,120 complete = 78,134/78,134 identical,
0 skipped**, with 12,425 manifest PCs and all 1,256 conditional branches
observed both ways. Log:
`build/workflow/20261023T161753.054763Z/01-oracle-recovery.log`.
Forced queue wrapping is pinned instruction by instruction; the continuous
recording does not claim a naturally forced wrap of that paired release.

The complete recovery capture timeout is now 96 emulated seconds. With 64,656
cases after the later continuations at sixteen per frame after forty boot
frames, regeneration needs about 68 seconds; the previous 64-second default
was too short even for the previous
63,940-case plan. This changes capture capacity only, preserving all fixtures
and comparison rules. The focused increment still uses its 32-second limit.

Final fixed-finale lifecycle checks pass under
`build/workflow/20261023T162519.473313Z/`. The three-position window repeats
all 660 capture-neutral comparisons, sixteen original-host restoration frames
and 7,118 continuous physical frames, including misplaced-finale rejection
and direct Practice entry rejection while Shuffle owns the ROM view. The
explicit complete run visits all 23 initialized positions and observes modes
17–22, ranking 25/26 and return to attract across **61,478 physical frames**,
with no directed skips, extra restarts or frame/time resets. It begins with
297 ships from 99 stocked credits and records 5,664 cannon, 300 missile and
47 impact allocations. These are native lifecycle observations under declared
physical requests, not a standard-credit completion or MAME field comparison.

The independent fixed-finale MAME capture completes **100,000 physical frames**,
including all 22 gameplay positions, stage-23 finale, ordered ending 17–22,
ranking 25/26 and attract return. Its input echo, exact frame/time continuity
and path-coverage baseline pass; removing finale coverage is rejected by the
same gate. Capture log:
`build/workflow/20261023T162515.497880Z/01-capture-shuffle-ending.log`.
The full native comparison uses MAME's actual port observations, rather than
the requested controls used by the native-only 61,478-frame run.

The complete `required` profile passes in
`build/workflow/20261023T162746.531591Z/`: ROM-free `test` (84.39 s), complete
`verify-stage1` (477.41 s), and `oracle-test` (3,285.22 s), including every
retained mutation. Both widescreen stage sweeps preserve 22,540 native
RGB/PCM/time frames each at 398×224 and 358×224. The complete recovery gate
passes all 78,134 fixtures, 12,425 PCs, 1,256 branches both ways and all 166
recovery controls. New palette, seven ROM-policy, changed-reference and
missing-finale controls all reject. Existing race-qualified skips in older
fixture families retain their documented reasons; the new Shuffle checks
skip zero records.

The final native menu and release-backed Shuffle CLI pass in
`build/workflow/20261023T163056.588440Z/`, including the readable
"22 STAGES THEN FINALE" summary, recording guards, stable seed-zero output and
fixed stage 23. Practice's unchanged CLI check also passes in
`build/workflow/20261023T162601.196957Z/`. Runtime and harness hashes are retained
in `build/workflow/shuffle-settled-source-manifest.json`. Relative to the earlier
required-profile snapshot, only the UI wording, capture timeout and ending
control's diagnostic matcher changed: ROM-free test inputs remained unchanged,
and the final oracle pass includes the updated control. The final UI is
separately exercised above; a fresh complete Stage 1 gate covers its final build.

The extended actual-pin replay subsequently reached an additional unported
instruction at observation **63,533**, after the ending and return to attract.
The failure is `$D27E` in shared credit/continue-prompt blinking. Previous state
was mode 1/position 11/lives 249/score 0; the partial failed frame had already
reached the independently expected mode 1/position 12/lives 249/score 0. The
retained diagnostic is
`build/workflow/20261023T175135.837058Z/01-verify-shuffle-ending.log`.
The comparator now includes passive frame/state context around an advance
failure; comparison semantics and the full 100,000-row window remain intact.

Fourteen new generated bodies at `$D27E..$D2B0` close that blink continuation,
joining the already verified `$D2D2` path. They handle the byte counter and
bit-3 transition, preserving register neighbors and X/NZ flags, and select
original show/blank strings through `$137E2`. Independent MAME checks pass
**186/186 fixtures identical, 0 different, 0 skipped, 354 bus phases**. The two
conditional branches each exercise sixteen taken and sixteen untaken cases.
Operand cases cover byte/word preservation, borrow/overflow, counter neighbors,
X preservation and both text-call return addresses. All **14/14** new controls
reject; independent review found no instruction or coverage defect. Logs:
`build/workflow/20261023T175419.805306Z/` and
`build/oracle/shuffle_credit_blink_mutations.log`.

The atomic merged corpus preserves the original baseline and immutable prior
64,120-case complete capture. Its new replay passes **14,014 + 64,306 =
78,320/78,320 identical fixtures**, zero differences/skips, comparing
32,726 + 149,934 bus phases. The manifest covers **12,439 PCs** and all
**1,258 conditional branches both ways**. Log:
`build/workflow/20261023T175702.174990Z/01-oracle-recovery.log`.
The earlier full `required` pass predates these fourteen bodies. This later
increment changes generated instruction bodies and their focused fixtures/
controls; it does not change the generator, shared CPU/flags, bus, scheduler,
compiler options or capture instrumentation. It follows the literal-port
validation cadence with the complete retained corpus and affected continuous
recording, rather than claiming the prior complete gate covers new bodies.

The repaired replay then passed observation 63,533 and exposed the sibling
credit-clear path at **63,973**, `$D2B4`. The diagnostic is retained in
`build/workflow/20261023T175931.302695Z/01-verify-shuffle-ending.log`.
The final static audit covers the entire `$D216..$D2EC` routine: 52 instruction
entries, with `$D2EC` returning rather than falling into the separate `$D2EE`
function. Both direct helpers are complete: `$E35E..$E3C8` (27 entries) and
`$137D2..$1381A` (30). Five missing sibling entries at
`$D2B4/$D2BC/$D2C0/$D2C8/$D2CE` handle the request bit, conditional flag clear and original blank
string call. All reachable local alternatives are now translated; the audit
is retained in `build/oracle/shuffle_credit_graph_audit.txt`.

Independent original MAME verifies the five additional sites with **64/64
fixtures identical, 0 different, 0 skipped, 232 bus phases**, including both
BNE outcomes (16/16), even/odd byte lanes, bit-0 values, neighbor and flag
preservation and the formatter return address. Capture/build log:
`build/workflow/20261023T181000.546319Z/`. The atomic retained union passes
**14,014 + 64,370 = 78,384/78,384 identical fixtures**, zero differences/skips,
32,726 + 150,166 bus phases, **12,444 PCs** and all **1,259 branches both ways**.
Log: `build/workflow/20261023T181224.614091Z/01-oracle-recovery.log`.
The original baseline and all earlier complete corpora remain preserved.
An optimized-recovery run during the fixture update correctly rejected the
then-incomplete coverage union for these five newly declared PCs; its failed
log is retained under `build/workflow/20261023T175929.906747Z/`. No comparison
or coverage condition was relaxed.

The five-site tree then passed `test` (95.39 s), complete `verify-stage1`
(481.13 s) and optimized-release recovery replay (32.73 s) in
`build/workflow/20261023T181305.314474Z/`. Both recovery corpora match all
78,384 fixtures, zero skipped. The unchanged full arranged comparison passed
the credit-prompt paths and matched through 78,000 frames before reaching an
additional untranslated attract-mode helper at observation **78,031**,
`$15774`. Its retained failure is
`build/workflow/20261023T181306.257682Z/01-verify-shuffle-ending.log`.

The attract failure came from the original missile-controller helper's
nonlocal stack exit. The expanded static audit follows all six controller
entries, direct callees and deferred child callbacks: **686 reachable
instruction entries**, no missing translations or unresolved control-flow
sites after the increment. Its reproducible census is retained as
`build/oracle/shuffle_missile_cfg_audit.py` and `.json`. This is a conservative
static closure, not an execution-coverage claim.

Twenty-two new generated bodies close the helper and release tail at
`$1576E..$15788/$15292..$152A6`, the RNG zero-seed fallback at `$C5B0`, and
the bit-6 target-offset branch at `$14DD4..$14DE2`. In particular, the
`ADDQ.W #4,A7` exits update the full address register and preserve all CCR
bits before jumping to the existing continuation. Original MAME independently
verifies **194/194 helper/tail fixtures** and **92/92 callee fixtures**,
**286/286 identical, 0 different, 0 skipped**, comparing 626 bus phases.
Capture/build logs are retained in
`build/workflow/20261023T182620.323005Z/` and
`build/workflow/20261023T182836.774414Z/`.
The three new conditional branches each cover both outcomes (16/16).
Operands cover byte lanes, modulo-eight bit clearing, signed address addition,
ordinary full-width stack pointers, signed targeting, arithmetic flags and
the RNG fallback. Independent review found no translation or fixture defect.
Forced stack low-word overflow remains outside these fixtures.

The retained union passes **14,014 + 64,656 = 78,670/78,670 identical
fixtures**, zero differences/skips, comparing 32,726 + 150,792 bus phases.
It covers **12,466 PCs** and all **1,262 branches both ways**. Log:
`build/workflow/20261023T183137.636676Z/01-oracle-recovery.log`.
All **22/22** relevant new mutation controls reject, bringing the retained
recovery controls to **207**. Log:
`build/oracle/shuffle_attract_exit_mutations.log`. The source remains limited
to generated bodies and their manifest/fixture/control declarations; shared
CPU, bus, scheduler and generator implementations are unchanged.

The complete actual-pin replay now passes **100,000/100,000 independent
arranged-MAME observations identical, 0 skipped** in
`build/workflow/20261023T183024.653870Z/01-verify-shuffle-ending.log`
(560.75 s, including the optimized rebuild). All 22 gameplay positions and
the native finale appear, followed by the ordered ending, ranking and attract sequence;
the longer post-ending attract window also completes. Physical pin and DIP
echoes match exactly. The compared fields are frame number, nanosecond time,
mode, position, lives and raw score. This does not extend those observations
to full CPU/device/RGB/audio equivalence or every stage order and DIP profile.
The frozen source/harness snapshot is retained in
`build/workflow/shuffle-final-source-manifest.json` (410 files).

Final checks on that frozen tree pass in
`build/workflow/20261023T183906.434922Z/`:

- `test`: **82.40 s**; complete `verify-stage1`: **469.50 s**.
- `verify-native-release-recovery`: **32.29 s**, all 78,670 retained fixtures
  identical in the optimized build, zero differences/skips.
- `verify-shuffle-run-mutation`: **57.91 s**, all 12,000 independent frame
  observations identical and the changed timestamp rejected.
- `verify-shuffle-ending-mutation`: **0.96 s**, complete coverage baseline
  passes and missing finale is rejected.
- `verify-native-shuffle`: **43.68 s**, all seven guards, stable seed-zero
  order/image, different seed-one order and stage 23 fixed last.
- `verify-native-practice`: **6.33 s**, all four guards and the directed SDL
  launch pass.

The separate final `verify-native-menu` passes in **68.82 s** at
`build/workflow/20261023T183946.284122Z/`, including Shuffle/Practice actions,
keyboard/pointer navigation, recording guards and native RGB/PCM/time/DIP
preservation. Source/harness hashes remain unchanged across the complete
100,000-frame comparison and these checks. These focused results cover the
literal continuations added after the earlier complete `required` snapshot;
they do not relabel that earlier snapshot as a fresh whole-host gate.

## Preview 3 release (2026-10-23)

The requested `v0.1.0-preview.3` refresh targets Apple Silicon / macOS 26+.
It includes the integrated Practice/Shuffle, 16:10, calibration, spatial-audio,
HUD/CRT and registered aircraft changes. Windows remains on the existing
Preview 2 download; its old executable is not relabeled as a new build.
README, release notes, roadmap, enhancement status, documentation index and
third-party notices describe this platform split and the new mode controls.

The macOS packager adds four explicit aircraft files under
`assets/player-aircraft/`, requiring committed regular files. It includes the
complete MIT C-header notice and MPL/redistribution notes for librashader.
Its runtime dependencies remain SDL3 3.4.16 and OpenAL Soft 1.25.2; optional
librashader runtime binaries and user shader packs remain external. Setup
instructions cover Practice, fixed-finale Shuffle, widescreen and CRT choices.
ROMs, captures, preferences and soundtracks remain excluded.

The complete release `required` run is retained at
`build/workflow/20261023T185259.944984Z/`. The separate `native-release` build
passes in `build/workflow/20261023T185938.980796Z/`. The earlier 100,000-frame
Shuffle comparison and final focused menu/CLI results above retain their
unchanged application sources; only documentation and package content change
for this release step. The package manifest and GitHub release notes record
the exact committed source and independent extracted-package verification.

The package-only policy test retains its script and passing output at
`build/releases/check_macos_package_policy.py` and
`build/releases/macos-package-policy.log`: **15/15 checks pass**. It covers
the exact four assets and three notices, image/manifest closure, regular
committed files, rejected missing/uncommitted/symlink/directory sources,
dirty-tree rejection before writes, and preserved ROM/build/soundtrack
source-archive exclusions.

Release staging contains 109 source/documentation/asset paths, with no ROM,
capture, dependency-runtime or personal-setting payloads. Project-owned changes
pass the staged whitespace check. The retained MPL license text keeps its
Markdown hard breaks and final blank line; the otherwise clean check excludes
only `src/third_party/librashader/LICENSE.MPL-2.0` for that formatting.

The final-source release `required` profile **passes completely** at
`build/workflow/20261023T185259.944984Z/`: `test` **77.92 s**,
`verify-stage1` **402.07 s**, and `oracle-test` **3,938.59 s**. All **207/207**
retained recovery controls reject; all 78,670 recovery fixtures and 1,262
branches retain their passing baseline. Existing race-qualified skips in
older fixture families keep their recorded reasons; new recovery and Shuffle
comparisons skip zero records. The 410-file release execution/harness snapshot
is retained at `build/workflow/preview3-source-manifest.json` and remains
unchanged through the completed gate. This is a fresh complete release gate,
separate from the earlier focused Shuffle checks and historical `full` runs.

Independent package QA caught an existing notice-path defect before upload:
`pkg-config --variable=libdir sdl3` returned `/opt/homebrew/lib`, so deriving
the license from its parent selected Homebrew's 1,334-byte BSD notice instead
of SDL's 884-byte zlib notice. The packager now reads `LICENSE.txt` directly
from the checksum-verified SDL source archive. The first failed artifact and
audit remain under `build/releases/`; no ROM links or launches occurred in
that attempt. Exact evidence is retained in
`build/releases/preview3-sdl-license-mismatch.json`. This package-only repair
changes no application, compiler, fixture or comparison dependencies of the
completed release gate.

## Windows LTO, Vulkan shaders and feature parity (2026-10-24)

The installed w64devkit GCC 16.2.0 reports `--disable-lto` and fails CMake's
compile/link probe. An isolated Windows Release build now uses checksum-verified
LLVM-MinGW 20261020 UCRT x64 / Clang 23.1.4 / LLD with `-O3 -flto=thin`.
`AB2_REQUIRE_IPO=ON` enforces a passing probe; a negative configuration with
the old GCC is rejected. Two presentation headers now include `<cstdlib>`
explicitly for `std::abs`, fixing standalone regression compilation with libc++.
CMake discovers Python rather than relying on the nonfunctional Windows
`python3` App Execution Alias, and copies required runtime DLLs beside the game.

Windows uses an optional native SDL Vulkan Slang host with librashader 0.12.0
ABI 2 / API 5. It borrows SDL's Vulkan device, queue and target images, submits
source work on unbind and transitions the output around the external pass.
The completion fence now waits before command/chain/target reuse rather than
blocking immediately after every submission; output sampling is ordered on
the shared graphics queue. SDR output, transactional replacement, parameter
validation, state restoration, frame caching and in-place history reset match
the frontend contract. Shader mode uses CPU sprite filters; normal rendering
retains SDL GPU Vulkan compute filters. Windows Vulkan uses the existing
VSync-off pacing choice for both renderers. Runtime/shader downloads remain
external and untracked; Windows runtime archive/member hashes are checked by
the dependency fetcher. Runtime shader-cache files are ignored.

Compared the changes since `v0.1.0-preview.2` and platform guards in the frontend,
audio and main loop: Practice/Shuffle, 358×224 and 398×224 viewports, live
calibration, spatial audio revisions, HUD/CRT and aircraft presentation all use
the shared source. The missing platform implementation was Slang; Windows
packaging also lacked the four allowlisted aircraft assets and librashader
notices. Both are now supplied. The packager identifies the actual native
compiler/LTO flags and includes available LLVM-MinGW runtime DLLs. No package
has been published; the clean-tree release gates remain required.

Local evidence under ignored `build/` (not whole-host acceptance):

- `windows-lto-build.log`, `windows-regression-build.log`: successful ThinLTO
  game and selected regression builds; `windows-lto-negative.log` rejects GCC
  without LTO support.
- `windows-regression-tests.log`: 23/24 selected CTest checks pass, including
  controls, Shuffle ROM policy, settings/input/haptics/music, HUD/CRT, buses,
  engine and Stage 1. The aircraft file-escape fixture fails at `create_symlink`
  because this Windows account lacks the privilege; an unsandboxed rerun in
  `windows-aircraft-recheck.log` fails for the same reason. Coverage is retained.
- `windows-spatial-tests.log`: generated audio fixture and spatial regression
  pass. The fetcher suite passes 10 tests, with one existing symlink test skipped
  for the same host privilege limit (`windows-fetch-tests.log`).
- `windows-vulkan-shader-test.log`, `windows-vulkan-crt-easymode.log` and
  `windows-vulkan-crt-pack.log`: authored channel/timing/history references and
  CRT Easymode, Geom and Royale pass live pipeline, renderer state, cached
  redraw, failed-preset retention, parameter and widescreen checks. References
  additionally cover resize through 4K and genuine previous-frame reset.
- `windows-vulkan-mutations.log`: independent passing baseline and 3/3 rejected
  timing/history/renderer-state controls, zero skipped. Sources, binaries,
  fixtures and observations are retained under `build/oracle/slang-vulkan-mutations/`.
- `windows-vulkan-main-final.log`: real menu-driven SDL GPU → native Vulkan →
  SDL GPU switch and invalid-preset rollback preserve paused native RGB/PCM/time.
- `windows-gpu-filters.log`: 630/630 comparisons, 47,209,218 pixels identical,
  zero skipped. `windows-gpu-hud.log` covers all three viewport widths and 4K.
  The supplemental built-in CRT GPU test stops at SDL 3.4.18's
  `SDL_SetGPUViewport_REAL` assertion, "Viewport size exceeds current render
  target dimensions" (`windows-gpu-crt.log`, repeated in
  `windows-gpu-crt-abort.log`). It was terminated at the assertion dialog;
  this GPU gate has not passed. The software CRT regression passes. No
  comparison, fixture or library assertion was disabled to obtain a pass.
- `windows-vulkan-practice.log`: the supplied two-pose HD pack loads in the real
  16:10 Practice game. Combining CPU ScaleFX with the external preset is costly
  (roughly 10–11 fps in this bounded run); this is not a sustained-performance
  acceptance pass. Shader-only and normal Shuffle smokes have separate logs.
- `windows-vulkan-fast-final.log`: final VSync-off shader smoke with the HD
  aircraft pack measures approximately 59.6 fps, no reported audio underruns;
  `windows-lto-shuffle.log` records a normal SDL GPU Vulkan Shuffle smoke near
  59.6 fps. These are bounded observations, not sustained acceptance.
- `windows-vulkan-startup-fallback.log`: a missing startup preset reports its
  error and restores SDL GPU Vulkan/compute rendering. The local playable
  folder was updated with the ThinLTO game and runtime DLLs; the previous EXE
  is retained under `build/previous-windows-build/`. The resulting executable
  SHA-256 is `ed9a8ba54261f68da263d816981195622d8dd9c31090cb9c16ff90ee5d0a6b6d`.
  `windows-playable-shader.log` records a bounded launch of this copied binary.

The Make-based `required` attempt is retained at
`build/workflow/20261023T214701.334638Z/`; it stops in `test` because this host's
Make/pkg-config setup resolves SDL headers to the macOS fallback. Native CMake
checks above do not replace that complete gate. Retained MAME captures are absent
in this checkout, so full oracle/mutation/natural-recording acceptance is open.
No gameplay instruction port or capture coverage was changed by this work.

### NTSC preset performance follow-up (2026-10-24)

Reproduced `ntsc/ntsc-320px-composite-scanline.slangp` from the external shader
pack in `build/playable/shaders/`, with Sprite Filter Off. The Vulkan host was
waiting on the external pass immediately after submission. It now keeps that
fence pending until resource reuse; SDL samples the output on the same graphics
queue after the output barrier. Preset replacement, resize, parameter edits and
command reuse wait before touching resources; destruction still waits idle.
The CPU SpriteScaler Off path also bypasses scaling scratch storage and
coordinate divisions while retaining the same blend/protection rules.

Finite local Practice stage 1 observations (windowed, no HD aircraft):

- `build/ntsc-before.log`: 360 frames, 16:10 / sharp HUD / ground enabled;
  shader-stage CPU time 2.01–2.16 ms and sprite composition 1.69–1.81 ms.
- `build/ntsc-after.log`: 600 frames after deferred fencing, shader-stage CPU
  time 0.53–0.95 ms. These CPU timings do not measure GPU execution alone.
- `build/ntsc-final.log`: 1,200 frames with both changes; sprite composition
  0.91–1.26 ms and shader-stage CPU time 0.54–1.14 ms. Early intervals reach
  59.64 fps, but later busy scenes reach 5.5 ms of scene reconstruction and
  fall to approximately 53–55 fps. Sustained 16:10 performance remains open.
- `build/ntsc-original.log`: 1,200 frames, Original viewport/HUD, ground off;
  first interval 57.55 fps, all remaining intervals 59.64 fps. This is a bounded
  fallback measurement, not whole-game or fullscreen acceptance.

`build/ntsc-final-build.log` records the ThinLTO build;
`build/ntsc-final-checks.log` records authored shader references, consecutive
frames without intermediate readbacks, real renderer switching/rollback with
unchanged paused RGB/PCM/time, and 3/3 rejected timing/history/state mutations,
zero skipped. `build/ntsc-final-regressions.log` passes 21/21 selected CTest
checks including the sprite regression (all 256 alpha values with/without pixel
protection compared against unchanged uniform EPX composition) and Stage 1.
The complete Make/oracle gates described above remain open.

The playable EXE was updated; its SHA-256 is
`2351637b299f4cc7d5e41ec10fb924cf6e1d383b9b6a011d8ae90891486818df`.
The preceding EXE remains at `build/ntsc-before-afterburner2.exe`.

## Integrated primary renderer: first underlay slice (2026-10-24)

The measured preview cost motivates a shared primary-renderer path, rather than
changing translated gameplay or simply widening the hardware sprite scaler.
`Screen::render_integrated_partial` now rasterizes road/BG/FG layers once at a
bounded even width, then copies the centered 320 columns into native text and
sprite composition. `Screen::ViewportLayers` owns reusable indexed/priority
planes at 224 rows; its initial 320–640 width limit matches the preview policy.
The API supplies the underlay before text, corrected ground, detached visibility
or sprite effects. Disabled-display clips clear the corresponding underlay
storage and black the native output without running sprite feedback. Native
layer observers still see the original 320-wide pre-sprite data.

The ordinary `render_partial` entry point uses the same implementation with no
extended viewport. The scheduler, bus and translated code are unchanged. Native
sprite rasterization and word-7 feedback still run at 320×224 in both paths.
The new entry point is exercised by tests only: no frontend migration, new menu
ratio, playable EXE update or performance improvement is claimed by this slice.

Evidence retained under ignored `build/`:

- `workflow/20261024T063517.025760Z/`: focused `test-integrated-renderer`
  passes using the Windows LLVM-MinGW compiler.
- `integrated-renderer-test.log` records the initial 80/80 synthetic cases.
  The final fixture primes all rows before its partial/disabled clips so stale
  prior-frame content must be cleared; `integrated-renderer-regressions.log`
  passes 22/22 selected CTest checks, including the strengthened fixture,
  engine and Stage 1. Widths 320/358/398/522/640, all eight road controls,
  scroll/category policies, native RGB, observer planes, sprite raster, feedback,
  wide indexed/priority planes, single rows and untouched rows are compared.
  Final fixture counts: 80/80 cases identical, zero skipped, 240 feedback
  witnesses and 6,758 distinct side-pixel witnesses.
- `integrated-renderer-mutations-final.log`: passing baseline and 4/4 rejected
  center-shift, reversed-origin, lost disabled-row clear and feedback-corruption
  controls, zero skipped. All sources/logs/binaries remain in
  `oracle/integrated-renderer-mutations/`. The initial failed control is retained
  too: it exposed the need to populate rows before testing their erasure.
- `integrated-renderer-native-build.log`: native ThinLTO game, engine, Stage 1
  and new regression targets build. `integrated-renderer-native-smoke.log`:
  120-frame Original/NTSC Practice smoke passes with shader active and no reported
  audio underruns. This exercises the ordinary rendering path, not a deployed
  integrated frontend or a controlled performance comparison.
- `workflow/20261024T063529.753285Z/`: the complete Make gate attempt stops in
  `test` because `SDL3/SDL.h` is unavailable through this host's Make/pkg-config
  fallback. It does not reach `verify-stage1` or `oracle-test`. Independent MAME
  captures remain absent in this checkout; CMake Stage 1 and synthetic comparisons
  do not close that acceptance gate. No fixture or mutation was removed.

Focused commands:

```bash
python3 tools/workflow.py run test-integrated-renderer test-integrated-renderer-mutations
```

```powershell
cmake --build build/windows-lto --target integrated_renderer_regression -j4
ctest --test-dir build/windows-lto -R '^integrated_renderer_(regression|mutations)$' --output-on-failure
```

The follow-up below supplies these primary underlays at live partial-render
frontiers and compares the enhanced consumer against the retained snapshot
renderer. Independent MAME native-output/feedback and full required gates precede
frontend promotion. Variable-width geometry includes nominal 21:9 (522 rounded
even); ultrawide scene/visibility/HUD and sustained-load acceptance remain open.
Custom-stage data/object contracts are a separate extension workstream.

### Live primary underlay consumer and compact capture (2026-10-24)

`XBoardHost::integrated_viewport` now selects the shared underlay pass at the
existing partial-render frontier. `GameSession::capture_wide_scene` accepts an
optional synchronous primary consumer. Metadata, including the original sprite
bank, is captured before native scaler feedback; the consumer receives that clip
and the borrowed underlay after native rendering at the same frontier. Native
text/sprite composition and frame/audio publication remain authoritative.

`WideScene::render_primary_clip` consumes those indexed/priority planes instead
of rasterizing road/BG/FG again or reloading road/tile devices. Existing text,
palette, ground correction, descriptor visibility, shadows, fragment ordering and
aircraft/HUD ownership still use the same policies. `finish_primary_frame`, called
after `advance()`, flattens and publishes the completed enhanced frame. Partials
cannot publish readiness early; frame/options/width/order mismatches are rejected.

Default capture still retains snapshots and the default frontend still uses the
preview. With a primary consumer and `retain_snapshots=false`, compact capture
reuses one metadata slot and omits the tile array (`tile_gen=0`); `wide_scene()`
returns no ready snapshots in this mode. Text/palette/road/sprite/visibility inputs
needed by the consumer remain available. Restart restores the optional host
viewport, and disabling capture restores ordinary native rendering. Consumers
must process borrowed data synchronously and finish frames after advancement.

New local evidence, with original ROM assets external and untracked:

- `build/integrated-live-final-checks.log`: 27/27 selected CTest checks pass,
  including native engine/Stage 1/session checks and the new live/clip fixtures.
  `build/integrated-live-final-details.log` retains the detailed CTest output
  and counts at this checkpoint.
- `integrated_scene_358`, `_398` and `_522` each compare 881/881 paired native
  RGB/PCM/time and enhanced-frame observations, zero skipped: 2,643/2,643 total.
  Each run includes 1,209 partial callbacks, 67 disabled-display clips, seven
  multi-clip compared frames, 806 frames with sprite fragments, 210 detached-HUD
  frames and 12 actual aircraft-draw frames. The one deliberate capture-disabled
  frame per run still checks native RGB/PCM/time, but has no enhanced comparison.
  Practice helper startup callbacks run too; their intermediate outputs are not
  counted as paired frame comparisons. The fixture covers a bounded boot/input
  trajectory, directed stage-1 Practice, retained-to-compact switching, pause,
  disable/re-enable, compact restart and coexistence with native sprite capture.
- Native capture frontiers and pre-feedback metadata are compared separately.
  Enhanced comparisons include background/sprite/protection/ground planes,
  fragment heads/chains, HUD entries/palettes/occlusion and aircraft geometry/masks.
  These are local differential checks, not independent MAME CPU/device coverage.
- `integrated_scene_clips`: 30/30 synthetic-state/external-ROM-graphics cases
  identical at widths 320/358/398/522/640, zero skipped. Partial palette changes,
  enabled/disabled clips and native/detached HUD policies pass. Deliberately
  poisoned tile metadata on the live side proves it uses the primary underlay
  rather than silently reconstructing the old path.
- `build/integrated-live-focused-workflow.log` and
  `build/workflow/20261024T065935.160152Z/`: focused Make targets
  `test-integrated-renderer` and `verify-integrated-scene-mutations` pass. The
  latter retains a passing baseline and 3/3 rejected bypassed-underlay,
  lost-priority and early-publication controls, zero skipped. Inputs, mutants,
  binaries and logs remain under `build/oracle/integrated-scene-mutations/`.
  This consumer/hardware fixture does not depend on GameSession's later compact
  capture change; its relevant sources/dependencies are unchanged by that change.
- `build/integrated-live-compact-build.log`: final native game and both live/native
  session integration targets build with the existing ThinLTO configuration.

Commands for this increment:

```bash
python3 tools/workflow.py run verify-integrated-scene verify-integrated-scene-mutations
```

```powershell
cmake --build build/windows-lto --target integrated_scene_integration integrated_scene_clip_regression -j4
ctest --test-dir build/windows-lto -R '^integrated_scene_(358|398|522|clips|mutations)$' --output-on-failure
```

No playable-folder update, default frontend rollout, new aspect menu option or
FPS improvement is claimed. The previously recorded Make SDL/pkg-config failure
and absent independent MAME corpus still leave complete required acceptance open;
these local comparisons do not replace it. Next is the frontend adapter and
matched busy-scene measurements, followed by full/MAME/GPU/lifecycle acceptance
before promotion. Snapshot reconstruction remains available as the comparison
reference. Custom art/backend combinations and full-stage ultrawide coverage
remain unverified.

### Frontend adapter, matched NTSC timing and independent MAME RGB (2026-10-24)

The native frontend now accepts `--scene-renderer snapshot|integrated`. Snapshot
remains the default while complete acceptance is open. Integrated mode installs
the compact primary consumer for widened/detached presentation, using a stable
settings copy for the partial frame. A new optional GameSession completion hook
runs after the native board tick, before frame/audio extensions; it also finishes
hidden Practice/Shuffle startup frames. Changing renderer resources retains the
same menu/settings path. Native Original output and sprite feedback stay intact.

Profiling labels the selected path. Integrated enhanced-consumer time is removed
from `board_ms` and reported in `wide_scene_ms`; shared background work still
belongs to the primary pass. Compare their sum when comparing modes.

`tools/video/benchmark_integrated_frontend.py` ran eight matched 1,200-frame
windowed Practice-stage-1 tests: Off sprite filter, sharp HUD, corrected ground,
the user's four-pass NTSC scanline preset, explicit runtime and neutral controller
policy. Each aspect used snapshot/integrated/integrated/snapshot order. The capture
process tree was temporarily suspended during timing and resumed afterward;
MAME's virtual-time recordings and all outputs were retained. First intervals
are excluded from summaries only; raw logs retain them.

| Viewport | Snapshot board + scene | Integrated board + scene | Reduction | Median FPS in each run |
| --- | --- | --- | --- | --- |
| 398×224 (16:9) | 13.827 ms | 13.183 ms | 0.644 ms / 4.7% | 59.637 |
| 358×224 (16:10) | 13.373 ms | 12.897 ms | 0.476 ms / 3.6% | 59.637 |

These are means of the reported per-interval CPU timings, averaged across both
runs of each mode. Busy integrated intervals still reach 53.37–55.72 fps; the
change does not establish sustained 59.6 fps. One snapshot interval measured
19.416 fps despite ordinary reported render-section costs; it is retained as an
unexplained outlier, not removed or assigned an unsupported cause. All four final
BMPs within each aspect are byte-identical. Eight raw runs, commands, all
intervals, images and `results.json` remain under
`build/oracle/integrated-frontend-perf-verified/`. The measured EXE is retained as
`afterburner2-measured.exe` there, SHA-256
`1bea93a54863aafd2276d2ca111314f04129411ff4e0fdeb87dee205ff39c519`.
The runtime SHA-256 is
`1890f647c7fbe52d4cc591526db24367caca284996855c4565c6003c7e46f8cc`.
The earlier `integrated-frontend-perf-final/` attempt rejected an inactive shader
after runtime discovery failed; it is retained and excluded from accepted timing.
The final BMP/drawable dimensions are 1194×672 at 16:9 and 1074×672 at 16:10.

Independent MAME evidence:

- Downloaded [official MAME 0.287](https://github.com/mamedev/mame/releases/tag/mame0287)
  to ignored `build/deps/mame-0.287/`; verified the publisher's SHA256SUMS value
  `68cdaf6d48213c6f3d0f7fa7f2733db46f74e400ad66db2d8a8d777430a42fb9`.
  The existing ROMs were packaged locally under ignored `build/oracle/mame-roms/`
  for MAME's directory layout; its audit passes. No ROM data is committed.
- Fresh-state stock MAME captures contain 300 attract, 100 no-sprite and 300
  gameplay frames. `oracle_integrated_video` reuses the existing unmodified RGB
  comparator, replacing only Screen's rendering entry point and fixing tolerance
  at zero. All 700 frames match at each of 320/358/398/522/640: **3,500/3,500
  frame comparisons identical, zero differences/skips**. This validates the native
  center, not the outer bands or CPU/device chronology. Record:
  `build/integrated-frontend-mame-all.log`.
- `build/integrated-frontend-mame-controls.log`: passing independent baseline and
  2/2 rejected primary-center-shift/native-text-loss mutants, zero skipped. Each
  variant retains its sources, binary, logs, local ROM inputs and diff images in
  `build/oracle/integrated-mame-mutations/`.
- `build/integrated-frontend-focused-final.log`: 32/32 selected CTest checks pass,
  including the five independent MAME width checks, live scene/clip matrices and
  existing engine/Stage 1/session regressions. Detailed output is retained in
  `build/integrated-frontend-focused-final-details.log`.
- `build/integrated-frontend-shader-main.log`: real integrated 16:10/sharp-HUD
  GPU → native Vulkan → GPU switching and failed-preset rollback preserve paused
  native RGB/PCM/time. This is not a complete frontend lifecycle/GPU matrix.

Full acceptance is not declared:

- The Make test setup now uses the installed Windows SDL pkg-config directory
  and LLVM-MinGW ThinLTO rather than falling back to macOS include paths.
  The first complete test attempt exposed a Windows fixture stack overflow;
  moving large renderer fixtures to heap ownership preserves all cases. The
  retained 14/14 aircraft controls pass afterward. An earlier retry stopped at
  the existing file-symlink escape fixture because this account lacked privilege,
  including outside the sandbox. Logs: `build/integrated-frontend-full-test-retry.log`
  and `build/integrated-frontend-aircraft-privilege-check.error.log`. The fixture
  and its comparisons remain intact. The user subsequently enabled Windows
  Developer Mode; the unchanged escape fixture now passes, recorded in
  `build/integrated-frontend-aircraft-devmode.log`. A later complete run hit
  `independent effect persistence failed`; the unchanged settings fixture passed
  twice in isolation and in the next complete run. Its cause is unproven; failed
  logs remain retained. That next run exposed a stale HUD mutation anchor because
  its target expression now appears in both rendering paths. The anchor now
  includes the snapshot-only same-frame return, preserving the original deliberate
  cache-invalidation fault and expected failure rather than dropping the control.
  The focused HUD gate passes 10/10 scene controls plus 1/1 native alpha control.
  The subsequent complete `test` passes in **204.88 s** at
  `build/workflow/20261024T085930.786742Z/02-test.log`; summary:
  `build/integrated-frontend-full-test-hudfix.log`. All retained controls execute;
  the dependency-fetch suite also passes all 11 tests with no symlink skip.
- The full Stage 1 attempt exposed the same stack limit in the retained geometry
  fixture; its focused check now passes at both widths. Large scene/host objects
  in related retained integration fixtures are heap-owned without dropping cases,
  recordings, assertions or mutation controls. The complete rerun is recorded at
  `build/integrated-frontend-full-stage1-final.log`: **PASS (1410.72 s)**,
  workflow `build/workflow/20261024T082107.230663Z/`. This includes both retained
  23-stage sweeps, Practice/Shuffle lifecycle and all three integrated widths.
- `build/integrated-frontend-full-oracle.log`: the complete oracle-test attempt
  fails on unavailable/invalid CPU visibility and IRQ4 operand-read corpora.
  Stock MAME does not provide the scheduler/CPU probe needed for these gates.
  The existing probe builder expects an instrumented source/build-object setup;
  the downloaded binary lacks it (`build/integrated-frontend-scheduler-probe.log`).
  Previously validated complete captures or a compatible instrumented build are
  needed; the passing independent RGB gate is not a substitute. The supplemental
  `full` attempt also stops on an unavailable/invalid arbiter capture
  (`build/integrated-frontend-supplemental-full.log`). The user explicitly deferred
  these instrumented CPU/device and supplemental gates to feature testing on
  their Mac, where the instrumented build and retained corpus exist.
- A full stock-MAME capture attempt is retained under
  `build/workflow/20261024T071836.581456Z/`; its full 100,000-frame Shuffle
  observation recording completed before the remaining routine captures.
  After that deferral, the broad capture job was stopped after its active MAME
  recording finished; all controllers exited and all completed/partial files
  remain retained. Record: `build/integrated-frontend-capture-deferred-final.log`.
  These ignored captures are not promoted as a complete corpus.

No default promotion, release or playable EXE replacement is claimed while the
full gates remain open. The integrated frontend is available explicitly for
testing. Further sprite/fragment work remains the main performance workstream.

From the repository root, the experimental frontend can be launched with
`build/windows-lto/afterburner2.exe --scene-renderer integrated`; choose the
existing 16:9 or 16:10 viewport in Options. The playable package has not been
replaced. For shader testing with this build directory, pass an absolute
`--shader-runtime` path to `build/playable/librashader.dll`.


## Integrated primary renderer macOS acceptance (2026-10-24)

The Windows source at `3587cdb` is integrated on macOS. Source builds now select
the live primary road/tile path by default for widened or detached HUD/aircraft
presentation; `--scene-renderer snapshot` retains the previous reconstruction
reference. Native Original rendering, CPU/device clocks, gameplay object pools
and sprite feedback retain their owners. No published package is replaced.

Acceptance now exercises the actual primary host, rather than inferring its
CPU/device identity from a passing snapshot-host oracle. `oracle_whole_host`
accepts an optional validated logical width and otherwise retains every existing
CPU state, bus, device, native index/priority/RGB and audio assertion. At 358 and
398 pixels, each of the retained cold (121 frames), cold-full (603 frames) and
expanded (781 frames) corpora passes: **3,010/3,010 frame comparisons identical,
zero differences/skips**, zero supplied execution/state/read seeds. These are
finite, overlapping capture windows, not 3,010 distinct physical frames.
`build/workflow/20261024T091627.987335Z/` also records a passing independent
baseline and **3/3 rejected** center-shift, missing-native-text and corrupted
sprite-feedback controls. Sources, binaries and logs remain under ignored
`build/oracle/integrated-host-mutations/20261024T091936.717437Z/`.
The new host gate is retained in `oracle-test` and both Make/CMake inventories.

Independent RGB coverage now includes attract, no-sprite, gameplay and roll
captures at 320/358/398/522/640. This expands the retained gate rather than
replacing any prior fixture. The live-scene fixture still compares every
background, sprite, protection, ground, fragment-chain, HUD and aircraft plane;
its exact comparison helper is shared with the full natural-lifecycle test.
`verify-integrated-lifecycle` replays all four retained natural recordings at
both supported wide widths, comparing the live primary path with snapshot
reconstruction for ground correction Off and On at every completed frame.
The original independent MAME ten-column, native-image, terrain, protection,
visibility and lifecycle checks remain in place. Width-specific edge fixtures
remain required at their original 398-pixel geometry.

Frontend GPU evidence:

- `build/workflow/20261024T091857.175775Z/`: Metal shader switching/failed-preset
  rollback; both 358-pixel Stage 1/5 GPU fixtures (24/24 filter/opacity cases);
  GPU HUD/custom art and registered aircraft through 4K; CRT pixels/state,
  high-DPI resize and an observed fullscreen round trip; 4,128/4,128 menu GPU
  sprite cases; 781/781 CRT native RGB/PCM/time frames; both retained Slang
  mutations rejected. All pass with zero skips.
- `build/workflow/20261024T092229.767142Z/`: real GPU -> Metal -> GPU switching,
  failed-preset rollback, sharp HUD, corrected ground and enabled opacity options
  at Original, 16:9 and 16:10. Paused native RGB/PCM/time remains identical in
  all three runs. Missing-preset diagnostics are the expected negative case.
- The supplemental `full` profile passes at
  `build/workflow/20261024T091828.214166Z/`: 20,532,168/20,532,168 full-window
  instruction/IRQ comparisons and 53,449,620/53,449,620 ordered phases match;
  the retained reset-window checks also pass. This supplements the required
  gate, not a replacement for it.

The completed frontend follow-ups are retained separately:

- `build/workflow/20261024T094533.053205Z/`: the three shader-main runs assert
  that an ordinary launch installs the compact primary scene consumer, and cycle
  all three viewport choices through the real menu/resource adapter after shader
  rollback, with paused RGB/PCM/time unchanged. The preceding explicit-mode
  runs also pass at `build/workflow/20261024T093127.673958Z/`. The test waits for the
  persisted settings transaction after the intermediate swapchain-priming
  present. The earlier observer failure is retained at
  `build/workflow/20261024T092926.437642Z/`; it sampled that intermediate present.
- `build/integrated-macos-frontend-final.log` and
  `build/oracle/integrated-macos-frontend-final/results.json`: **12/12** real
  Stage 5 frontend runs, 360 frames each, cover GPU and dummy/software at
  Original/16:9/16:10 with ScaleFX, sharp HUD, corrected ground, all opacity
  options and the registered aircraft pack loaded. All six snapshot/default
  final BMP pairs are byte-identical. Logs assert the requested renderer/filter
  path and the loaded two-family pack. The earlier temporary harness incorrectly
  expected three root families; its output remains retained under
  `build/oracle/integrated-macos-frontend/`.
- `build/integrated-macos-shuffle-frontend.log` and
  `build/oracle/integrated-macos-shuffle-frontend/results.json`: **4/4** real
  Shuffle startup runs (seed 42, 360 visible frames each) cover GPU at 16:9 and
  16:10 with the same enhancement/art settings. Both snapshot/default final BMP
  pairs are byte-identical. Logs confirm Shuffle startup and active live primary
  composition after the hidden startup; these contended smoke timings are not
  used for the performance comparison below.

Isolated Mac timings use the installed four-pass
`ntsc/ntsc-320px-gauss-scanline.slangp` (a different preset from the Windows
measurement), sprite filter Off, sharp HUD and corrected ground. At each of
Practice stages 1 and 5, eight 1,200-frame windowed runs use the same executable,
runtime, neutral controller policy and snapshot/integrated/integrated/snapshot
ordering at each aspect. The first interval is excluded from summaries only;
all intervals remain recorded. Existing validation groups were suspended at a
point with no timed subprocess harness active and resumed automatically after
446.10 s; `build/oracle/integrated-macos-timing/pause.json` records this. Their
workflow elapsed times include the suspension. No emulated clocks or captures
were changed.

| Practice stage / viewport | Snapshot board + scene | Integrated board + scene | CPU reduction |
| --- | --- | --- | --- |
| 1 / 398×224 | 7.151 ms | 6.833 ms | 4.44% |
| 1 / 358×224 | 6.969 ms | 6.654 ms | 4.51% |
| 5 / 398×224 | 7.619 ms | 7.328 ms | 3.82% |
| 5 / 358×224 | 7.421 ms | 7.147 ms | 3.70% |

All **16/16** timing runs pass, and each stage/aspect's four final images are
byte-identical. Every run's median is 59.637 fps; integrated minimum intervals
range from 55.983 to 56.282 fps. This does not establish sustained 59.6 fps or
attribute slower intervals to a particular subsystem. Drawables are
2388×1344 (16:9) and 2148×1344 (16:10) on this high-DPI Mac. Commands, logs,
all timing intervals, final BMPs and `results.json` remain under
`build/oracle/integrated-macos-timing/stage-1/` and `stage-5/`.
The measured executable is retained there as `../afterburner2-measured`, SHA-256
`ed4bcb792eef2228419f4a46b12b79ca3a89fbda49c3b56fd571330ad2b6e4c2`;
runtime SHA-256 is
`1dbecea0c165fd0fddc2407ed4b8872f9f73f4fd2c3689a80ffc24c87e3fda2a`.

Complete natural-lifecycle acceptance passes at
`build/workflow/20261024T092208.080290Z/` (**2261.09 s**, including the timing
suspension): **208,904/208,904** frame comparisons across four recordings and
both supported wide widths, zero differences/skips. Each width retains all
74,452 ending, 12,000 neutral/failure, 10,000 continue and 8,000 weapons frames.
Both ending runs visit all 23 stages, 13 carrier states and eight refuel states.
The live and snapshot enhanced planes match with ground correction Off and On;
this retains the independent MAME observations and native image checks rather
than claiming a new whole-credit CPU/device/RGB/audio capture.

The first complete required attempt at
`build/workflow/20261024T092256.239946Z/` passes `test` (94.34 s) and
`verify-stage1` (484.32 s), but `oracle-test` exposes a stale retained
`native-feedback` mutation anchor: `WideScene::draw` gained its primary-layer
parameter. The control now targets that exact current signature and still
injects the same illegal sprite-byte write, with the same required semantic
rejection. All nine controls remain and run at both widths. The geometry
mutator now validates every anchor up front and retains baseline/mutated
sources, binaries, commands and logs in unique ignored run directories instead
of deleting temporary variants. The focused repair passes two positive geometry
baselines and **18/18** rejected faults, zero skipped, at
`build/workflow/20261024T095913.204595Z/` and
`build/oracle/widescreen/mutations/20261024T095913.943979Z/`.
The failed run and its partial work remain retained. The clean rerun at
`build/workflow/20261024T100051.688495Z/` passes `test` (79.45 s) and complete
`verify-stage1` (424.60 s), then exposes an out-of-date local Shuffle declaration:
the Windows source preserves inherited music across new stage adjacency, while
the previous Mac fixtures declare the older arranged ROM. The exact-ROM guard
correctly refuses those fixtures; it is unchanged.

The prior short/complete ROM declarations, requested and echoed inputs, MAME
observations and negative-control logs are preserved under
`build/oracle/shuffle-pre-windows-music/20261024T102123.183764Z/`. Fresh-state
independent MAME recordings use the current declared arrangement and unchanged
physical-input/palette-policy script at
`build/workflow/20261024T102123.311398Z/`: 12,000 short frames (49.84 s) and
100,000 ending/ranking/attract frames (381.98 s) complete. No native expected
state, CPU execution or gameplay RAM is exported. The local MAME version is
`0.287 (mame0287-163-g6b511c0a858-dirty)`; binary/source/fixture hashes and the old
fixture manifest are recorded in `build/oracle/shuffle-macos-refresh-20261024.json`.
The fresh short native replay and changed-reference control pass at
`build/workflow/20261024T102352.477623Z/`: **12,000/12,000** independent
observations identical, zero skipped; the original timestamp mutation is rejected.

The full fresh native Shuffle replay passes at
`build/workflow/20261024T102947.314949Z/` (**474.37 s**): **100,000/100,000**
independent MAME observations identical, zero skipped, exact physical input/DIP
echoes, all 23 flight positions and ending/ranking/attract. CPU/device/RGB/audio
equivalence is not inferred from these observation columns.

The parallel oracle attempt at `build/workflow/20261024T102947.322986Z/` fails
on `build/native_obj/config.tmp`: launching two separate Make wrappers together
raced their fixed temporary configuration-stamp path. This is a host build
invocation error, not a failed state comparison; flags and compiled sources did
not change. Separate Make wrappers are now run serially (parallel targets within
one Make remain supported). The failed log is retained.

**The complete required gates now pass on the final renderer source:**

| Gate | Result / record |
| --- | --- |
| `test` | PASS, 79.45 s; `build/workflow/20261024T100051.688495Z/01-test.log` |
| `verify-stage1` | PASS, 424.60 s; `build/workflow/20261024T100051.688495Z/02-verify-stage1.log` |
| Complete `oracle-test` | PASS, 3926.11 s; `build/workflow/20261024T103838.020631Z/01-oracle-test.log` |
| Supplemental `full` | PASS; `build/workflow/20261024T091828.214166Z/` |

The `test` and `verify-stage1` passes remain applicable: their sources,
compiler/options, canonical ROMs and transitive fixtures are unchanged; they do
not consume the refreshed Shuffle observation files. The clean complete oracle
rerun uses the refreshed corpus and executes all retained controls, including
**207/207** recovery faults, **18/18** geometry faults, **3/3** integrated-host
faults and **2/2** independent integrated-RGB faults. None were removed or
weakened. Its independent integrated RGB matrix matches **4,300/4,300** frames
across four corpora and five widths, zero differences/skips. The six direct
integrated-host runs again match all **3,010/3,010** counted frames with zero
supplied execution/state/read seeds or differences/skips.

Existing routine-level race skips remain explicitly reported in the full log:
for example, the gameplay Sub sprite fixture is **1,198/1,200 identical, two
skipped** for inputs changed mid-pass, and the later scenery fixture is
**937/1,200 identical, 263 skipped** because the other CPU wrote an input.
The zero-skip new renderer/whole-host/lifecycle scopes above do not erase these
older finite-coverage limits.

This closes the declared Mac/source primary-renderer rollout gate and promotes
the integrated frontend default. The snapshot path and every prior fixture and
control remain available. The optimized build still has the measured SHA-256
listed above. Published preview packages have not been replaced.

Additional ratios, all possible controller/scene combinations, custom stage
semantics and exhaustive platform/sustained-preset performance remain separate.


## Preview 4 release (2026-10-24)

The macOS-only `v0.1.0-preview.4` refresh packages the default integrated primary
renderer together with the inherited Windows/source updates and all Preview 3
features. The existing Windows Preview 2 release remains unchanged. Viewport
labels retain their Preview suffix for finite scene/platform acceptance; release
notes, README, startup guidance and widescreen docs distinguish that label from
the integrated renderer selection and the Original default viewport.

Application sources, compiler/options and the validated executable remain
unchanged from the [Mac renderer acceptance](#integrated-primary-renderer-macos-acceptance-2026-10-24).
The complete required gates use the passing `test` / `verify-stage1` records at
`build/workflow/20261024T100051.688495Z/` and the clean complete `oracle-test` at
`build/workflow/20261024T103838.020631Z/`, including all 207 recovery controls.
Supplemental full, independent integrated-host/RGB, 208,904 lifecycle comparisons,
fresh 100,000-frame Shuffle replay, GPU/frontend and matched timing records keep
their stated unchanged dependency scope. Only documentation and package startup
text change for this release preparation; no fixture or comparison is weakened.

The packager requires committed source, verifies dependency archives, copies
allowlisted generated aircraft art and notices, removes development library
paths, ad-hoc signs the executable/libraries and emits payload/source hashes.
The package manifest and GitHub release identify the exact source commit.
Independent verification checks a separately extracted copy, clean ROM/media/
preferences exclusions, archive/source/notice closure, signatures, dependency
paths and executable permissions. Bounded Metal/OpenAL launches from another
working directory cover Arcade, Practice and Shuffle with the default integrated
path; temporary user-ROM links are removed before publishing the original ZIP
and checksum. Verification outputs remain under ignored `build/releases/` and
the published release records their results.

## macOS source-build shader runtime discovery (2026-10-25)

Launching `build/afterburner2-release` outside the checkout exposed a missing
runtime search candidate. The pinned optional library already existed at
`build/deps/librashader/librashader.dylib` and loaded successfully by absolute
path. The executable-relative candidate incorrectly looked under
`build/build/deps`; the relative fallback depended on the working directory.
The Metal loader now also searches `deps/librashader/librashader.dylib` beside
the executable, preserving explicit-path and environment precedence.

The retained Metal integration test clears the SDL runtime override, changes
to an unrelated scratch directory and opens an absolute authored preset. It
failed before the fix at
`build/workflow/20261025T072859.280361Z/01-verify-native-slang.log`.
The final focused gates at `build/workflow/20261025T072925.418838Z/`
pass the Metal baseline and all three retained mutations, including removal
of this lookup candidate. Each staged binary receives its own runtime copy;
timing and temporal-history controls remain intact. The integrated shader-main
gate also passes all three initial viewports and live viewport switches,
queued preset selection, failed-preset rollback and identical paused native
RGB/PCM/time, zero skipped. This changes frontend library discovery only;
it does not expand gameplay or whole-host acceptance.

The rebuilt optimized executable also completes a 180-frame launch from
`build/oracle/slang-launch-cwd/` using the user's
`/Users/dakiva/Downloads/shaders_slang/crt/crt-easymode.slangp`, without
`--shader-runtime` or `AB2_LIBRASHADER`. Its profile reports `shader=active`
with 17 parameters; the image, log and result are retained in that scratch
directory. Only a local ROM-directory link is supplied there, with no runtime
under the working directory. This is a finite shader-loading smoke, not a
sustained-performance measurement.

The same focused workflow passes `native-release` and the complete `test`
target (107.18 seconds). `git diff --check` passes. No ROMs, captures or runtime
binaries are added to source control.

## Native Metal sprite filters with Slang (2026-10-25)

The user's `ntsc/ntsc-320px-composite.slangp` slowdown was reproduced with
their ScaleFX setting. Shader mode previously switched from SDL GPU to SDL
Metal and its CPU sprite-filter fallback. At Practice stage 1, the initial
240-frame probes measured approximately 43.4 fps in Original and 36.3 fps in
16:9: ScaleFX/composition cost 12.8 and 15.9 ms respectively, while NTSC cost
4.3 and 5.2 ms. Keeping NTSC but disabling ScaleFX restored approximately
59.64 fps in both modes. CRT Easymode with CPU ScaleFX also fell to 47.1 fps,
so the regression was not specific to NTSC decoding. Logs, settings, commands
and the old optimized executable are retained under
`build/oracle/ntsc-composite-perf/`.

The native Metal renderer now runs the same six generated sprite/scene
compute kernels as the existing SDL GPU path. ScaleFX's four intermediate
passes, final scaling/composition, priority protection and all scene opacities
are retained. Shared input/layer buffers and an IOSurface-backed output avoid
framebuffer readback. SDL submits prior output reads before a cross-queue
fence wait; compute completes before SDL samples the new output. Resource
resize follows completion of prior reads. Backend replacement releases this
scaler before destroying its renderer. Shader input stays at the selected
native raster, with the original preset and selected filter unchanged.
The CPU reference's six header-defined helpers are marked inline so they can
be included in multiple translation units; their calculations are unchanged.
Windows retains its existing Vulkan/CPU sprite-filter route.

The independent CPU-reference matrix passes 882/882 comparisons and
157,452,162 pixels, zero skipped, across six filters, 21 patterns and seven
sizes including all three viewport widths. It covers transparency,
premultiplied alpha, protection, clouds/trails, background/shadow fragments,
explosions and afterburners. Six retained deliberate filter/protection/opacity
mutations are rejected by exact pixel comparison at
`build/oracle/native-metal-sprite-mutations/20261025T083857.525670Z-c83ddcf3/`.
The Make and CMake builds expose `verify-native-metal-sprite-filters` and
`verify-native-metal-sprite-mutations`; GPU gates require a usable desktop.

`build/workflow/20261025T083549.296350Z/` passes all three integrated
shader-main viewport runs, live backend/viewport changes, failed-preset
rollback, unchanged paused RGB/PCM/time, `native-release`, `test` and complete
`verify-stage1` (448.09 seconds). These are frontend checks; unchanged core/oracle fixtures retain their prior
acceptance scope.

The CMake-built matrix also passes with Metal API validation enabled. A queued
three-frame red/blue/green test draws successive outputs into distinct target
regions without an intervening readback: both same-size texture reuse and a
resource resize preserve earlier queued draws. Outputs and build/run logs are
under `build/oracle/ntsc-composite-perf/cmake-*`. The first 180-frame full-game
comparison with NTSC Composite and ScaleFX also has byte-identical old/new
final images under `functional-pair/` in that directory.

After the integration gates finished, matched 360-frame windowed runs use the
user's NTSC Composite preset and ScaleFX at Practice stages 1 and 5. All six
old/new final BMP pairs are byte-identical. The directory retains commands and
settings; `comparison.json` records binary hashes and every profile sample.
The benchmark excludes its
first interval and retains the final image-capture overhead. Mean simulation
rates and sprite-filter submission/completion costs are:

| Stage | Viewport | Old fps | Metal fps | Old filter ms | Metal filter ms |
| --- | --- | ---: | ---: | ---: | ---: |
| 1 | Original | 41.91 | 59.03 | 13.06 | 1.63 |
| 1 | 16:9 | 36.58 | 58.92 | 15.87 | 1.55 |
| 1 | 16:10 | 39.39 | 58.98 | 14.46 | 1.54 |
| 5 | Original | 41.00 | 59.05 | 13.21 | 1.60 |
| 5 | 16:9 | 35.00 | 58.91 | 16.33 | 1.62 |
| 5 | 16:10 | 37.91 | 58.99 | 14.64 | 1.57 |

Each new run has a 59.6374 fps median interval. The reduced final interval
includes the requested framebuffer readback/BMP save; every sample remains
in the report. The old/new executable SHA-256 values are respectively
`be1a2f9911e2ddfe42ed31927d3bb9b05c569a8ce24f33adb03d0e36415d40e2`
and `d33e55b5196deb4f26df9979581c8d3e45276543efc88b1a96981f45088fc1aa`.
These finite windowed runs establish the reported combination's improvement
on this Mac; exhaustive preset, fullscreen, peak-load and sustained whole-credit
performance remain outside this check. This is a local source-build update,
not a replacement of the published Preview 4 archive.

Final Make gates at `build/workflow/20261025T085003.380094Z/` pass the updated
native Metal matrix plus queued-read/resize check and the unchanged SDL GPU
matrix (630/630 cases, 47,209,218 pixels), zero skipped. `git diff --check`
passes; runtime libraries, ROMs, binaries and captured images remain ignored.


## Shuffle gameplay openings (2026-10-25)

At the user's request, `Shuffle::generate` rejects orders starting with source
stage 5, 13 or 23 and repeats the existing Fisher–Yates pass with the same RNG
stream until the opener is eligible. Every source remains present exactly once;
stage 23 remains fixed last. Already eligible seeded orders retain their original
output; seeds with a previously scripted opener now produce a revised deterministic
order. New Shuffle and CLI startup share this generator; F5 retains its stored
plan. This is an opt-in arrangement policy change, not a literal CPU port.
Explicit directed plans remain available to retained ROM/palette fixtures.

The policy regression checks 4,096 seeds, complete permutations, reproducibility,
all twenty eligible openings, and the retained known seeded order. The native CLI
also asserts that its displayed order starts with an eligible stage. The new
`scripted-opening-allowed` negative control disables the retry and must be rejected;
all eight earlier ROM-policy mutations remain retained.

Focused `test-shuffle-rom`, `verify-shuffle-rom` and
`verify-shuffle-rom-mutations` pass at
`build/workflow/20261025T090033.745356Z/`: nine of nine controls rejected,
56/56 retained independent MAME gradient words identical, zero skipped.
General `test` passes at `build/workflow/20261025T090042.152157Z/`.
The same run also passes complete `verify-stage1`, `verify-native-shuffle` and
`verify-shuffle-palette`: Shuffle lifecycle preserves 660 native RGB/PCM/time
frames plus 16 original-host restoration frames, and the natural three-position
window completes 7,118 physical frames. Seven CLI guards and three eight-frame
SDL launches pass, including same-seed order/image identity and a different-seed
order. Retained independent MAME palette fixtures match 156/156 observed words,
zero skipped. `git diff --check` passes. These are local focused/integration
checks; no complete `oracle-test` or new whole-credit acceptance is claimed.

## Native menu organization (2026-10-25)

The source frontend now launches with Arcade / Practice / Shuffle / Settings /
Quit. The running-board menu offers Resume / Restart Run / Change Mode /
Settings / Quit. Change Mode groups Arcade, Practice and Shuffle and explains
that a launch ends the current run. Restart Run delegates to the existing
restart action, retaining the active Practice stage or Shuffle order.

Settings separates Display, Graphics, CRT and Shaders, Controls, Audio, Arcade
Settings and Diagnostics. Graphics groups the four translucency switches in
Transparency. Controls puts device selection first and splits Button Bindings,
Analog Settings, Input Monitor and Feedback. The former Calibration page is
named Input Monitor because it displays raw/mapped input rather than performing
calibration. Feedback has separate cue and finite motor-test pages. The full
Settings overview fits without scrolling; longer binding/music pages retain
scrollbars. A page history restores parent selection and scroll position for
keyboard, pointer, Escape/F1 and Back navigation, including renderer rebinds.
Existing preference keys, setting adjustments, board pause ownership, audio
preview cleanup, finite feedback stopping and recording restrictions remain.
`--menu-preview` now selects Settings at its new root position.

The menu integration fixtures follow the new visible labels and pointer
coordinates. They retain setting wrap/clamp/persistence/rejection checks,
Practice/Shuffle guards and independent retry targets, controller remapping and
live monitor coverage, optional-service combinations, scrollbar endpoints,
renderer rebind identity, full/windowed transitions and native RGB/PCM/time
isolation. Added checks cover startup/pause action order, Change Mode Arcade
recording restrictions, Restart Run dispatch, analog adjustments and restoring
the parent row. No literal CPU port, oracle fixture or comparison gate changed.

Validation: `test` passes in `build/workflow/20261025T145702.452998Z/` (86.24 s).
Its sources/dependencies were unchanged by the subsequent menu-caption edit.
Final frontend validation passes in
`build/workflow/20261025T150031.612773Z/`: `verify-native-menu` (65.97 s),
`native-release` (39.03 s) and `verify-gpu-native-menu` (68.50 s). The menu replay retains 781/781 identical native
RGB/PCM/time frames, zero skips; real-GPU composition retains 4,128/4,128
identical native sprite-plane cases, zero skips. Software and Metal menu images
were inspected locally. The release executable's isolated `--menu-preview`
smoke also succeeds (`build/oracle/menu-reorganized-preview.log`). Captures
remain ignored under `build/oracle/`.

The separate real-main-loop shader fixture now navigates Settings / CRT &
Shaders and Display / Aspect Ratio. Its retained valid-preset, asynchronous
chooser, failed-preset rollback, settings persistence and paused native output
assertions are unchanged. `verify-integrated-shader-main` passes (52.87 s) in
`build/workflow/20261025T150633.024308Z/`: all three initial viewports complete
GPU → Metal → GPU switching, failed-preset recovery and three live viewport
changes each; native RGB/PCM/time stays identical, zero skips.

The first sandboxed GPU attempt could not enumerate desktop displays; desktop
access was required for the real Metal/fullscreen checks. This is focused local
frontend validation, not a complete host/release acceptance gate. Published
packages have not been refreshed, and physical controller navigation and other
platforms have not been reaccepted by this change.


## ZIP ROM loading and startup picker (2026-10-25)

Source startup supports `--roms directory|set.zip`, with `roms/aburner2.zip`
as the fallback for missing loose chips in the default `roms/` directory.
Explicit sources are isolated: a wrong explicit ZIP does not silently borrow
chips from the default directory. ZIP member basenames may have a directory
prefix. Duplicate names, unsupported/encrypted members, wrong lengths,
corrupt ZIP data and wrong expected ROM CRCs fail before destination writes.
Stored and Deflate entries decode into memory, bounded to the expected chip
size. Region bounds and interleave stride are checked before writes.
The MIT-licensed miniz 3.1.0 reader is embedded in `romloader.cpp`, keeping
standalone oracle tools free of new link requirements; both package scripts
include its license. Snapshot mutation runners retain the reader dependency.

The frontend initializes SDL before loading, tries default ROMs then a saved
ZIP location, and offers an asynchronous native ZIP picker on failure.
A valid selection is remembered in `<settings-file>.roms`; the ZIP stays
where selected. Cancel quits successfully; an invalid set shows an error and
allows another choice. Picker callbacks own their request and use weak startup
state, allowing quit while a late callback remains outstanding. Bounded
`--frames` launches fail without a picker, preserving automation behavior.
Published previews have not been repackaged.

Validation already completed:

- `test-romloader`: 48/48 ROM-free checks across directory, stored/Deflate,
  nested/Unicode paths, default ZIP/loose precedence, missing/corrupt/truncated,
  duplicate/unsupported, size/expected-CRC and region bounds/interleave cases.
  Log: `build/workflow/20261025T154754.199016Z/01-test-romloader.log`.
- `test-rom-picker`: selection, cancel, error, quit and late callback checks
  pass. `verify-native-roms` builds the release and compares four 240-frame
  final software images (directory, explicit ZIP, saved ZIP and default ZIP),
  all byte-identical; an explicit missing source returns failure promptly.
  Logs: `build/workflow/20261025T155333.550478Z/01-test-rom-picker.log` and
  `02-verify-native-roms.log`. Generated fixtures/images/process logs are in
  `build/oracle/rom-zip-startup/` and remain ignored.
- CMake builds both new test targets; both CTest entries pass in
  `build/cmake-rom-zip`. Configure/build logs are under `build/rom-zip-smoke/`.
- Complete ROM-free `test` passes at
  `build/workflow/20261025T154811.368220Z/01-test.log`. The initial
  `verify-stage1` attempt caught a missing miniz dependency in the isolated
  integrated-scene mutation build; the source-copy layout was repaired with
  all controls retained. A clean ROM-backed rerun is recorded below.

The clean complete `verify-stage1` rerun passes at
`build/workflow/20261025T155333.550478Z/03-verify-stage1.log`, including the
three retained integrated-scene rejected faults and both full 23-stage aspect
sweeps (22,540 native RGB/PCM/time frames each). Original integration, audio,
practice/shuffle, feedback and enhanced presentation checks remain passing.

Affected oracle checks also pass:

- `oracle-integrated-video-mutations`: all five widths across four retained
  independent MAME video corpora pass, with 2/2 renderer faults rejected and
  zero skips. Log: `build/workflow/20261025T155333.550478Z/04-oracle-integrated-video-mutations.log`.
- `verify-wide-mutations`: both geometry baselines pass and 18/18 retained
  controls are rejected, zero skips. Log:
  `build/workflow/20261025T155333.550478Z/05-verify-wide-mutations.log`.
- `git diff --check` and Python syntax checks pass. No ROMs, archives containing
  ROMs, screenshots or captures were added to tracked source.

The actual macOS picker was launched for a desktop smoke check, but the
computer-use service failed with ScreenCaptureKit error -3811 before visual
inspection. Its test processes were stopped. Callback tests and the native
software startup checks pass; physical selection/retry/cancel, Windows/Linux
native dialogs and packaging remain unverified. This is a focused frontend/
loader validation, not a new whole-host or release acceptance milestone.

### Menu font label correction (2026-10-25)

The frontend font addresses ROM tiles directly using ASCII character codes.
The tile at `&` is not an ampersand in this font, so the new category and
restart labels visibly used the wrong glyph. The menu now spells out
`CRT AND SHADERS` and `APPLY AND RESTART`, using the existing letter tiles.
Visible-label navigation fixtures and current instructions use the same names.
No font/ROM data or gameplay behavior changed.

Label correction validation passes in
`build/workflow/20261025T161020.709069Z/`: `test` (93.52 s),
`verify-native-menu` (66.88 s, including 781/781 identical native RGB/PCM/time
frames, zero skips) and `native-release` (39.34 s). The CRT and Arcade Settings
menu readbacks were inspected to confirm the spelled-out labels fit. This is a
focused text/UI check; no new full-host or platform acceptance is claimed.


## Wide world-explosion retention (2026-10-25)

The audit found a second native horizontal lifetime boundary: world explosions
at `$91C0` use `$92A4..$92B6`, rather than the ordinary scenery width-table
cull already observed by `WideVisibility`. The native radius is `D7 ASR.W #4`
plus 160. Taken `$92AE/$92B6` branches now retain a private 64-byte object,
finish that tick at `$92BA`, and advance detached `$91C0..$9364` updates with
only the horizontal margin extended. New descriptor kind 7 identifies these
visual copies. They survive native slot reuse and use current shared camera
state, the ROM animation payload and the original two sway operations.

The update distinguishes visible, above-screen hidden and retired states.
Hidden frames do not publish a sprite or advance the animation timer, but keep
the native age/depth/position updates and can reappear. Pause uses the native
previous-position copy and append behavior. Near-depth, below-screen and wide
horizontal retirement remain final; existing stage/mode/restart resets clear
the copies. Native execution, free-list/size slots, damage, sound, targeting,
request banks/counts and hardware sprite feedback are unchanged.

Independent ROM evidence precedes wiring: retained world calls include
**64/64 identical `$91C0` comparisons**, zero skips. The supplemental opt-in
`AB2_ORACLE_WIDE_EXPLOSIONS=1` operands execute the original ROM using its runtime
animation table, without changing the older capture profiles. They supply
**128/128 identical comparisons**, zero skips: **56 visible, 16 hidden,
56 retired**, including 16 left-edge, 16 right-edge, 16 below-screen and eight
near-depth retirements, 16 paused calls and 25 animation-index advances.
Both older world corpora remain in `verify-wide-world`; their unrelated
cross-CPU races retain **12/8 skips**, respectively. The first older-corpus
hidden-state mutant survived because that corpus had no genuinely hidden
explosion calls; the new supplemental corpus closes that gap. Animation
coverage counts index advances because consecutive ROM entries can reuse art.

`verify-wide-world` now enforces these branch categories and all 64 object
bytes for surviving/hidden updates, plus independently observed native free-list
retirement and accepted publication. Full-queue visible calls are distinguished
from hidden calls; the native append can reject a visible object.
Five MAME controls reject pause bypass, age drift, missing first sway,
hidden-to-retired conversion and timer drift. Baseline and controls pass at
`build/workflow/20261025T164121.771287Z/`; initial supplemental capture is
`build/workflow/20261025T163050.965201Z/01-oracle-capture-wide-explosions.log`.

Explicit detached fixtures exercise both sides at margins 19 and 39, native
slot reuse, pause, hide/reappear and edge/depth/bottom/stage retirement while
checking native memory/time immutability. Three added observer controls cover
missing recovery hooks, native-width retirement and premature retirement of
hidden copies; all earlier visibility/geometry/lifecycle controls remain retained.
This closes the identified world-explosion controller gap, not every object
controller or whole-game/platform acceptance.

The adjacent controller audit also distinguishes two follow-ups: ground/air
hit-effect updates `$8E18/$8ECE` have a separate horizontal retirement path
and are not retained by this increment. Rotated gate explosions `$93A0/$94B8`
do not have that horizontal deletion in the inspected port; their depth and
animation lifetime does not justify adding the same recovery hook. Other
controller families still need their own ROM/producer audit.

Focused final-source results: `test` (90.47 s) and complete `verify-stage1`
(470.74 s) pass at `build/workflow/20261025T163503.082665Z/`. Both 23-slot
stage sweeps retain **22,540 native RGB/PCM/time frames per width**, and
integrated/snapshot scene comparisons pass. `native-release` passes (41.92 s)
at `build/workflow/20261025T164216.819663Z/`; the local release executable
includes this source change, while published packages are unchanged.

The complete retained weapons recording passes integrated/snapshot rendering
at 358 and 398 pixels: **8,000/8,000 ten-column MAME observations per width**,
zero differences/skips, at `build/workflow/20261025T164730.728478Z/` (296.91 s).
Both runs have **zero retained world-explosion descriptor observations**:
this is integration/lifecycle evidence, not natural execution coverage for the
new recovery path. The retained ground-target instruction census also lacks
`$91C0`, so it was not counted as additional coverage. Natural recovery of this
controller remains unobserved in these recordings. Independent directed MAME
calls and detached both-edge/lifetime fixtures cover the new behavior.

An attempted hidden-publication fault survived because the downstream sprite
projection independently clips entirely above-screen art. The new observer
control instead rejects premature hidden-object retirement on reappearance;
this is additional to the ROM hidden/retired control. The initial failed
mutation run remains in `build/workflow/20261025T163503.082665Z/`. An overlapping
build-config temporary-file collision stopped the first natural replay build
at `build/workflow/20261025T164659.708141Z/`; the clean replay rerun above passes.
These are focused source checks, not a fresh complete `required`/`full` release
or whole-host/platform acceptance declaration.

The clean complete focused visibility rerun passes **24/24 rejected controls**
(20 observer/presentation and four independent MAME faults), zero skipped, in
`build/workflow/20261025T164659.695259Z/` (778.66 s). Together with the five
new explosion-MAME controls, all **eight newly added faults** are rejected;
all earlier controls remain. Final diff/command/link inspection and
`git diff --check` pass. No ROM data, captures or generated art are committed.


## Wide ground and air hit-effect retention (2026-10-25)

This implements the follow-up identified in the preceding world-explosion
record. Ground `$8E18` and air `$8ECE` updates share horizontal retirement at
`$8FA2/$8FAA`. A taken branch retains a private 64-byte record and resumes that
tick at `$8FAE`; the object handler pointer identifies its family before native
`$9556` frees the slot. Detached updates extend only the horizontal margin.
Descriptor kinds 8/9 identify ground/air copies; kind 7 remains world explosions.

`HitEffectProjection` translates the ROM update instruction by instruction.
Age is incremented as a word and capped with a signed comparison at four for
scale growth, including word-sized product shifts. Ground special drift reads
`$29C33C/$29C33E` and bypasses sway; air always uses sway. Visible animation
advances on even ages until the byte frame limit matches. Palette selection
uses the base animation frame before applying `$29C0A4` to the art pointer.
Shared flips, pause, hidden-top behavior, below-screen and near-depth retirement
are retained. Above-screen frames keep age/depth/movement but do not animate or
publish; they can reappear. Slot reuse cannot overwrite detached state. Existing
stage/mode/restart clearing remains. Original's zero-margin policy adds neither
family. No native allocation, size slots, CPU/bus clocks, feedback, targeting,
collision, damage or sound commands are changed.

The helper was verified before observer wiring. `verify-wide-hit-effects`
retains the older `effects_directed.bin`: **308/320 handler calls identical,
12 skipped** because the other CPU wrote an input; the affected effect entries
are ground **64/64**, air **52/64 with 12 input-race skips**. The supplemental
opt-in `AB2_ORACLE_WIDE_HIT_EFFECTS=1` profile executes both original ROM entries
using runtime animation tables. Old capture profiles/instrumentation are
unchanged; all fixtures remain. New comparisons are **256/256 identical,
zero skipped**, ground/air **128/128 each**. Visible/hidden object comparisons
cover all 64 object bytes; retirement and accepted publication use independently
observed native free-list/count writes. Native full-queue rejection is handled
separately from hidden state.

Per family, the supplemental corpus covers **48 visible, 16 hidden, 16 left,
16 right, 16 below-screen and 16 near-depth retirements**. It includes 16 paused
calls, 12 animation-index advances, 20 held frames, four capped-age and 28
uncapped-age visible updates and 20 direction-offset updates. Ground supplies
16 special-drift and 16 sway cases; air supplies 32 sway cases, including an
active ground-drift flag that must not alter air movement. The gate requires
these categories for each family, including ground-specific drift, with zero
races. Logs: `build/workflow/20261025T171026.679265Z/` (capture 22.70 s,
comparison 10.72 s).

All **seven new MAME faults are rejected** at
`build/workflow/20261025T171107.128907Z/` (73.22 s): ignored pause, incorrect
age cap, ground drift leaking into air, omitted sway, reversed animation parity,
palette read after direction offset and hidden-to-retired conversion. They do
not replace older explosion/scenery controls. Detached policy fixtures cover
both families, both sides, margins 0/19/39, taken/untaken branches, pause, native
slot reuse, hide/reappear, ground/air drift ownership and edge/depth/bottom/stage
retirement, checking native memory/time immutability throughout. Four new
observer controls target missing hooks, native-width retirement, swapped drift
ownership and premature hidden retirement.

The native cannon producer `$AD9C..$AE0E` selects `$8D44/$8D58` initializers
for impact/splash emission according to the environment flag. Ground/air here
are controller-family labels; this work extends the existing effects rather
than inventing additional hit effects. The older note's separate follow-up is
now implemented for these two controllers; other object families still need
their own audit. Published packages remain unchanged.

The final helper gate additionally requires an even-age final-frame hold,
observed **four times per family**. An eighth MAME control forces animation
past its frame limit; it is rejected alongside the original seven. This avoids
satisfying the held-frame category solely with odd-age parity holds. Final
baseline/control logs are `build/workflow/20261025T172200.701411Z/`.

Final-source focused `test` (89.66 s) and complete `verify-stage1` (484.55 s)
pass at `build/workflow/20261025T171412.644537Z/`. Both 23-slot sweeps preserve
**22,540 native RGB/PCM/time frames per width**. Integrated/snapshot scene,
pause/restart, feedback/music, HUD/aircraft, Practice and Shuffle checks remain
passing. The local `native-release` rebuild passes (43.28 s) in the same run.
The preceding world-explosion helper/comparison controls also pass on the final
harness at `build/workflow/20261025T172426.906932Z/` (`verify-wide-world`,
`verify-wide-explosion-mutations`, 0.83/57.18 s). Existing old-corpus input-race
skips retain their documented scope; no new corpus skips are accepted.

The complete retained weapons recording passes at both widths in
`build/workflow/20261025T172617.691072Z/` (259.74 s): **8,000/8,000 ten-column
MAME observations per width**, zero differences/skips, with matching full-width
integrated/snapshot output and terrain/protection invariants. It supplies
**18 retained air-effect descriptor observations at 358 pixels and 62 at 398**;
these are observations across frame/clip publications, not unique objects.
Both runs supply **zero retained ground-effect observations**. Ground retention
therefore has independent directed MAME and policy-fixture coverage, but no
natural recovery evidence from this recording. The existing ground-target
instruction census also contains `$8ECE/$8FA2/$8FAA` but lacks `$8E18`, so its
name was not treated as ground-controller evidence. World-explosion observation
counts remain zero on these same recordings, as in the preceding increment.

The final focused visibility mutation run passes **28/28 rejected controls**,
zero skipped, at `build/workflow/20261025T172625.411672Z/` (850.80 s): 24
observer/presentation faults and four independent MAME projection/request
faults. This retains all prior controls and adds four hit-effect observer
faults. Together with the eight hit-effect MAME faults, all **12 newly added
faults** are rejected. No earlier fixtures or mutations were removed.

Coverage limits: the new supplemental corpus selects animation-table entries
0..3 out of the eight selected by the native `$345C8`/RNG interface. Entries
4..7 are loaded unchanged at runtime but are not directly selected by this
profile. It uses direction offsets 0/4/8, shared flip values 0/$10/$20/$30 and
the listed age/depth operands; this is not exhaustive operand/art coverage.
Natural ground retention, additional trajectories and platform/sustained-load
acceptance remain open. Final diff, command/link inspection, Python syntax
check and `git diff --check` pass. These are focused source results, not a new
complete `required`/`full` release or whole-game acceptance declaration. No ROM
data, captures or generated assets are committed.

## Stage 6 fast-roll sky triangles (2026-10-25)

The user reported white triangles in the blue sky with 16:9 and Corrected
ground. A directed Stage 6 startup followed by a held left bank and an opposite
right input reproduces them. At local flight frames 256 and 286 the road fill
extends onto the sky side of the displayed horizon. The old correction removes
only part of each triangle: current roll `$29C09E` has crossed an axis while
the published/interpolated horizon raster still has the previous quadrant.
For example, frame 256 has current angle 7 (positive X normal), but the observed
horizon row directions sum to -90, establishing the opposite sky/terrain
ordering. The corresponding inverted witness at frame 286 has angle 133 and
a row-direction sum of +42.

`GroundHorizon::correct` now takes fallback component signs from the sum of
ordered sky-pen-1/ground-pen-14 row and column directions when available in the
wide presentation. Magnitudes still come from the runtime ROM normal; local
ordered silhouettes still take precedence. This changes the clipped-row/column
fallback and its conservative projected envelope, without fitting a replacement
horizon line. Unmeasured components retain the supplied normal. Native rendering,
320-wide correction policy, foreground priority, solid-fill guards, palette and
partial-frame ownership remain unchanged.

The ROM-free regression mirrors the clipped witness across both axes, transposes
it to exercise both normal components, and checks 358/398 widths, sky fill,
terrain preservation and foreground protection. Two retained negative controls
restore stale X/Y signs and must fail. The ROM-backed integration adds 420 Stage 6
frames, checks native RGB/PCM/time identity, and asserts both triangle witnesses
at both render widths. Its visibility observation uses a 398-wide capture; the
358-wide render checks do not claim an independent 358-wide lifecycle recording.
Directed startup and this finite trajectory do not establish natural Stage 6
progression or all rolls/stages. The retained independent MAME roll corpus
continues to be the native video baseline; no new instruction port or MAME
instrumentation is introduced. Diagnostic snapshots/images remain ignored under
`build/oracle/stage6-sky/`.

Validation at `build/workflow/20261025T175901.542160Z/` passes `test`,
complete `verify-stage1`, `verify-ground-mutations` and `verify-ground-fixture`.
The expanded horizon integration preserves **420/420 Stage 6 native
RGB/PCM/time frames**, zero skipped, covers six roll-quadrant transitions and
passes all four named triangle witnesses across 358/398 render widths. Its
previous **1,560/1,560 native frames** and 4,096 runtime asset/LOD checks also
remain intact. Both widths pass all 23 directed stage visibility slots, with
native RGB/PCM/time and terrain-protection checks. The 18 synthetic and three
recorded-scene controls are all rejected (**21/21**, zero skipped), including
both new stale-sign faults. The retained independent MAME replay preserves
**160/160 original RGB frames**, zero skipped, with 58,471 visible wedge pixels
corrected and 2,643,597 terrain-side pixels preserved; 159 frames have measurable
horizon endpoints and one does not. These are focused source checks, not a new
complete `required`/`full` release or whole-credit acceptance declaration.

The same final-source workflow passes `native-release` and `all`; both local
executables are rebuilt. A bounded 30-frame Stage 6 Practice launch with
`viewport=16:9`, `ground=1`, dummy video/audio and the software renderer exits
successfully and saves `build/oracle/stage6-sky/smoke.bmp`. Its settings and log
are adjacent. This is a software launch check; new physical GPU/shader or
sustained-load acceptance is not claimed. Final diff, policy/command/link
inspection and `git diff --check` pass. Published packages remain unchanged.

## Stage 6 two-sided horizon correction (2026-10-25)

The subsequent user replay exposed both blue cuts through clouds and more white
cuts through sky. The preceding quadrant fix covered two named white-triangle
pixels but did not close the whole sky/ground boundary. Reusing the retained
Stage 6 clip snapshots reveals two additional causes:

- A measured ordering on one axis could be vetoed by an inferred component on
  the other. The independent full-image marker witness rejects seven of the
  205 retained frames (259, 272, 273, 288, 291, 304 and 305) with white remaining
  on the measured sky side. `Profile::sky` now uses fallback components only
  when neither local axis has an ordered pair of sky/ground markers.
- At some bank angles no native ground sample exists anywhere on a particular
  row. The previous same-row copy policy therefore leaves road exterior/blue
  behind the white horizon/cloud edge. The enhanced renderer now supplies the
  clip's palette to `GroundHorizon`. A missing-row fill may borrow an actually
  observed native ground pen from the same partial clip only when its full road
  color-control word matches and every observed eligible ground pen for that
  control has the identical palette word. Stage 6's different ground indices
  all resolve to `$7FFF`. Mixed colors, missing observations, different road
  controls, solid rows, other clips and protected samples do not authorize this
  fallback. Existing same-row sampling remains available for patterned fields.

Both projected corner extensions also defer to an opposing measured side, so a
corner fallback cannot repaint known sky as ground or known terrain as sky.
These are optional wide-presentation changes; native hardware rendering, CPU
execution and the 320-wide correction policy are unchanged. Runtime ROM data
and the actual published horizon raster still supply geometry and colors.

The Stage 6 integration now independently decodes the horizon markers and
checks all eligible measured sky/ground pixels on every frame at both widths,
in addition to retaining the earlier triangle samples and adding blue-cloud
samples. It does not call `Profile::sky/ground` or use the current roll/plane
fit for its side classification. Ground-side assertions require an independent
uniform-color donor witness; observations without one are counted separately.
The donor comparison keeps the old same-row index check and adds explicit
same-clip/color-control/palette/observed-pen evidence for the new fallback.
A ROM-free reference regression rejects incompatible donors. Existing fixtures
and faults are retained; new faults cover both corner sides, measured-axis
precedence, missing/color-mismatched donors and the comparison gate itself.

The standalone 398-wide retained-frame audit passes **205/205** frames after the fix,
zero skipped, compared with the seven explicit pre-fix rejections above. Its
snapshots, images, audit source/binary and JSON results are ignored under
`build/oracle/stage6-sky/`. Directed startup and this finite roll trajectory do
not establish whole-credit progression or every scene/trajectory. The native
MAME baseline remains the retained roll corpus; no CPU port or capture probe
was changed.

The full-image witness is also tested against two deliberately broken runtime
variants under `build/oracle/stage6-sky/side-mutations/`: restoring the inferred
axis veto fails the measured-sky assertion at frame 259, and disabling the
flat-field donor fails the measured-ground assertion at frame 264. Both controls
are retained in the regular unit fault dictionary as well; their recorded-frame
sources, build commands and rejection logs remain local. The focused final-source
horizon run at `build/workflow/20261025T182925.235697Z/` passes its two ROM-free
baselines, all **33/33** retained ground/reference faults (24 synthetic runtime,
three MAME scene, six donor-comparison controls), and **160/160** independent
native MAME RGB roll frames, zero skipped.

The expanded ROM-backed horizon replay passes at
`build/workflow/20261025T183722.410412Z/`: **420/420** Stage 6 native RGB/PCM/time
frames are identical, zero skipped; independent measured-side checks cover
**44,271,042 sky pixels and 5,808,532 ground pixels** across 358/398 render
widths. There are **zero ground-side observations without a validated uniform
color donor** on this finite trajectory. The four earlier white-triangle samples
and the added two blue-cloud sample frames pass at both widths. The original
1,560-frame horizon integration and 4,096 asset/LOD checks remain intact.

The first broader run at `build/workflow/20261025T183055.663166Z/` passed `test`
(91.00 s), then stopped `verify-stage1` at a new blue-cloud spot check that
incorrectly used a 398-wide coordinate outside the 358-wide frame. The sample
was moved to the same tear at an in-bounds coordinate and given an explicit
bounds assertion; the full-image side checks were retained. The focused clean
replay above verifies that repair. The earlier `test` result covers unchanged
ROM-free sources and is reused with that scope; it is not a passing result for
the failed integration gate. The complete integration gate is rerun separately.

The clean complete `verify-stage1` rerun passes at
`build/workflow/20261025T183909.915634Z/` (487.78 s), including all 23 directed
stage visibility slots at both supported wide widths, the expanded Stage 6
side census, 881 native/plane comparisons at each integrated width and the
retained native RGB/PCM/time, terrain, layer protection, pause/reset/disable
checks. The final source retains the 33/33 ground/reference fault rejections and
160/160 independent MAME roll-frame result cited above. The failed spot-check
run is not substituted for this clean complete gate.

The same clean workflow passes `verify-wide-mutations` (**18/18** rejected,
zero skipped; 59.05 s), `native-release` (37.27 s) and `all` (0.65 s). Both local
executables are rebuilt. Separate bounded 30-frame Stage 6 Practice launches
with 16:9 / Corrected ground pass for `afterburner2` and
`build/afterburner2-release` using dummy video/audio and the software renderer;
settings, logs and saved BMPs are retained as `final-default.*` and
`final-release.*` under `build/oracle/stage6-sky/`. The matching before/after
comparison is `two-sided-comparison.png` in that directory. These are local
software launch and focused presentation results, not new physical GPU/shader,
sustained-load, full `required`/`full` or release-package acceptance. Published
packages remain unchanged. Final Python syntax, diff, command/link and policy
inspection and `git diff --check` pass; no ROM data/captures are added to source.


## Wide missile trails and player damage emissions (2026-10-25)

The three identified follow-ups now have detached horizontal retention:
player missile smoke `$AAEE` at `$ABCA/$ABD2`, enemy missile smoke `$14EE2`
at `$14FA8`, and the world-projected player damage/death emissions
`$FCB4/$FDFA` at `$FF64/$FF6C`. Descriptor kinds 10/11/12 identify their
visual copies. Screen-following damage animations without this horizontal
retirement remain natively published; this increment does not reimplement
those sequences or the player aircraft/debris controllers.

`TrailEffectProjection` keeps the ROM update order and operand widths:

- Player smoke uses `$C0AC` depth movement, the first-eight-update width ramp,
  curvature and reused perspective divisor. It has no native enable/draw-only
  gate; top clipping hides and bottom/depth clipping retires. Resuming at the
  cull reinstalls captured D2 into the private divider before the Y tail.
- Enemy smoke preserves its enable/draw-only gates, unsigned depth bounds,
  signed growth/taper comparisons, 36-tick age limit and symmetric inclusive
  horizontal boundary. Existing verified `$15842/$15200` pure projection tails
  run only against the private bus. There is no new vertical retirement rule.
  Native ring unlink/replacement, allocation and ownership are never replayed;
  retention starts only at the horizontal-removal branch, not ring eviction.
- Player damage emissions preserve the damage-state-dependent depth rate,
  growth, projection and hidden/bottom rules. `$FCB4` can transition into the
  runtime `$2BC34` animation through the visual portion of `$FDA6`; `$FDFA`
  keeps signed-overflow SUBQ/BGT timing, palette/duration/scale records and
  scaling. Copies do not replay the alternating sound counter or sound enqueue.
  Native `$FF08` can free a record and return to further caller instructions;
  the detached horizontal alternative instead finishes its Y/animation tail
  without freeing or modifying the native record.

The observer retains each family before native cleanup, finishes that tick once
and advances from the following publication at the normal 30 Hz cadence.
Private records survive slot reuse; native math, request banks, scaler feedback,
missile counters, collision/targeting, damage, sound and respawn remain unchanged.
`$10310` clears copies in the precise ROM-defined pool range (`$1C5D0/$1C5D6`),
so a horizontal copy cannot outlive that non-horizontal lifecycle reset. Existing
stage/mode/restart resets still apply. Zero-margin Original adds no copies.

Independent helper verification preceded observer wiring. The retained missile
corpus passes **375/448 handlers**, 73 input-race skips in other missile/init
entries; all **64/64 `$AAEE` calls match**, zero skips. Enemy effects pass
**480/480**, including **373/373 `$14EE2` calls**. Companion directed and natural
routine captures pass **1,408/1,408** and **2,400/2,400**, zero skips, including
128 directed and 1,894 natural `$FCB4/$FDFA` calls. No earlier comparisons,
fixtures or controls were removed.

The supplemental profile supplies 128 calls per entry. The immediate-frontier
capture passes **387/512**, with 125 actual other-CPU input races: 37 `$FCB4`
windows on shared camera sine/cosine bytes (snapshot offsets `$8090..$8093`)
and 88 `$14EE2` windows on the roll byte (`$809F`). It remains retained as
`wide_trail_effects_frontier.bin`. The 2 ms delayed profile passes **512/512**,
zero skips: **128/128 per entry**, with visible/hidden/depth/bottom/left/right,
enable/draw-only, width ramp, enemy taper/age limit, damage animation transition
and signed timer-overflow coverage. Enemy horizontal retirement is categorized
after Y projection; a Y write is not evidence of bottom retirement for that
family. The gate requires applicable accepted categories separately per entry.
For surviving records, all 64 object bytes match MAME; retired status uses the
independent native free-list writes, and publication is checked against the
native request count. A separate invariant rejects any private RAM change
outside the copied object, including accidental ring or sound-state changes.

Capture/baseline records are `build/workflow/20261025T192733.951651Z/`,
`build/workflow/20261025T192923.867455Z/` and the complete accepted focused replay
`build/workflow/20261025T193126.605248Z/` (12.47 s). Early gate failures above
are preserved: the first exposed raced branch coverage, the second corrected
an enemy-Y-write coverage label; neither was a port mismatch.

All **13 new independent MAME faults are rejected** at
`build/workflow/20261025T193249.058823Z/` (133.90 s): wrong player speed source,
fabricated player pause, wrong reused divider, uncapped ramp, enemy enable/age/
taper errors, private ring writes, wrong damage depth rate, private sound-state
writes, timer overflow, animation scaling and hidden-to-retired conversion.
The divider fault is rejected by the older missile corpus's nonzero-Y operands;
retaining those fixtures is essential, because the new profile's logical Y is
zero. These comparisons and mutations are included in `oracle-test` and CMake
registers both supplemental captures.

Directed observer fixtures cover all three families and both damage modes,
both edges at margins 0/19/39, taken/untaken branches, native slot reuse,
hide/reappear, depth/stage retirement and the ROM-defined pool reset. They
explicitly reject applying damage/player vertical retirement to enemy smoke,
verify the exact 36-tick enemy lifetime, and reject fabricating an enable gate
for player smoke. Every observed event checks host memory/time immutability.
Six new observer mutations cover each missing family hook, native-width
retirement, hidden retirement and an omitted pool-reset hook.

Focused final-source results at `build/workflow/20261025T194020.628581Z/`:
`verify-wide-world` (0.84 s), `verify-wide-hit-effects` (0.70 s), all five
world-explosion faults (52.35 s), all eight hit-effect faults (83.04 s),
`test` (89.35 s) and complete `verify-stage1` (497.09 s) pass. Both 23-slot
stage sweeps retain **22,540 native RGB/PCM/time frames per width**. The wide
scene retains **1,560 native RGB/PCM/time frames** and exact native-center
reconstruction, with both roll directions and terrain protection checked.
The reported enhanced pixels can change because additional retained visuals
are intentional; the original board framebuffer remains the regression reference.

The local release executable rebuild passes (41.63 s), and the lifecycle replay
binary rebuild passes (35.74 s), in the same final-source run. Published
packages are unchanged. Complete weapons (8,000 frames) and continue (10,000
frames) recordings pass at both 358 and 398 pixels in
`build/workflow/20261025T195436.900576Z/` (611.08 s): **36,000/36,000 ten-column
MAME observations**, zero differences/skips, plus matching full-width integrated
and snapshot output and unchanged terrain/protection invariants.

| Recording / width | Player trail observations | Enemy trail observations | Damage emission observations | Native pool-reset entry observations |
| --- | ---: | ---: | ---: | ---: |
| Weapons / 358 | 402 | 12 | 140 | 3 |
| Weapons / 398 | 766 | 78 | 250 | 3 |
| Continue / 358 | 218 | 8 | 118 | 1 |
| Continue / 398 | 402 | 74 | 216 | 1 |

Every run actually exercises all three retained families. These are descriptor
observations across frame/clip publications, not counts of unique objects;
pool-reset counts are observed entry boundaries. The finite recordings do not
establish every trajectory, all animation operands, whole-credit CPU/device/
RGB/audio equivalence or broader platform/sustained-load acceptance. Original
remains the regression reference; these are focused source checks, not a new
complete `required`/`full` release milestone.

The complete focused visibility fault suite passes **34/34 rejected controls**,
zero skipped, at `build/workflow/20261025T194732.731408Z/` (1104.63 s): 30
observer/presentation and four independent MAME projection/request faults.
All six new observer faults and all 13 new helper faults are rejected:
**19 newly added faults**, with every earlier control retained. Final diff,
Python syntax and command/link inspection pass, including `git diff --check`.
No ROM data, captures or generated art are committed.


## Preview 5 release (2026-10-26)

The final application sources pass the complete `required` profile at
`build/workflow/20261025T201833.443522Z/`: `test` (88.80 s),
`verify-stage1` (436.71 s) and the entire `oracle-test` (4068.84 s),
including all retained fixtures and mutation controls. No fixtures or controls
were removed. `native-release` passes at
`build/workflow/20261026T040111.408102Z/` (0.89 s; existing current objects).
Documentation and package instructions were finalized afterwards without
changing the application sources. `git diff --check` and package-script syntax
inspection pass. Focused baselines, rejected faults and the complete weapons/
continue recordings for the new visual families are recorded above; this
release does not declare a new whole-game or platform acceptance milestone.

Preview 5 packages the ROM ZIP picker, reorganized menus, native Metal sprite
filters under Slang, Shuffle opening policy, corrected rolling horizons and
retained world explosions/hit effects/player and enemy missile smoke/player
damage emissions. The source tag is `v0.1.0-preview.5`. Apple Silicon/macOS 26+
remains the target, with ad-hoc signing, SDL3 3.4.16 and OpenAL Soft 1.25.2;
Windows remains on its existing Preview 2 download. The clean-source package
manifest identifies its commit and file hashes. The GitHub release records
the ZIP checksum and independent extracted-package signature, dependency,
Metal and OpenAL smoke results. ROMs, captures, preferences, personal music,
external shader packs and runtimes are excluded. Fresh promotional captures
are kept only under ignored `build/itch-preview5/`.

The existing finite-coverage limits remain: standard-credit recording, continuous
whole-credit CPU/device/RGB/audio comparison, broader scene/controller coverage
and sustained-load/platform acceptance are open. The earlier renderer
acceptance and supplemental `full` results remain historical evidence at their
recorded source checkpoint, not a claim that `full` was rerun for Preview 5.

Preview 5 publication completed at source commit
`e3056cdf2f4c7f3c928215b70416dfbf46a95d0b`. The post-whitespace-normalization
release rebuild passes (34.03 s) at
`build/workflow/20261026T040243.652566Z/`; miniz changes after the required
run normalize line endings/trailing whitespace only. The independently extracted
ZIP matches all 58 manifest hashes and its archived executable modes. Strict
code signatures and bundled/system dependency paths pass. A 180-frame native
launch from `/private/tmp` passes with Metal GPU filters, integrated 16:9,
Corrected ground and OpenAL stereo HRTF, with zero reported underruns or recovery
events. A sandboxed attempt could not enumerate displays; the native launch
passes outside that sandbox. Smoke logs are under
`build/releases/preview5-independent-smoke/`; no temporary ROM links remain.
The uploaded ZIP was downloaded again and matches SHA-256
`b6090a36a9cfd11ae87c65f926a852352780bdd4f666099a144a23c438bfd6e6`.
The source archive contains ROM instructions/list only, with no ROM binaries
or captures. The GitHub prerelease has the ZIP/checksum and three fresh
promotional images; repository and itch.io access settings remain unchanged.


### License inventory and packaging notices (2026-10-26)

`LICENSES/README.md` now inventories the included components, optional runtime,
platform toolchains and notice provenance. Central verbatim copies cover SDL3,
Z80, dr_flac, miniz, controller mappings, ScaleFX/Snes9x, librashader's header
and MPL option, and OpenAL Soft's fmt/GSL/pffft notices. Added the complete
Cannonball, CannonBall-SE and historical MAME notices for retained audio code,
plus Vulkan-Headers 1.4.341's Apache terms and GCC's GPLv3/Runtime Library
Exception texts. The original-code license no longer denies the retained
Cannonball-derived code, and legacy sound files are no longer labeled BSD
solely by association with modern MAME. Original source attribution remains.
Upstream license whitespace is preserved through `.gitattributes`.

Both packagers preflight the required central notices before producing an
artifact and already copy the complete directory into `Notices/project/`.
Native Windows packaging now requires a nonempty regular compiler-specific
MinGW runtime COPYING input via `--mingw-notice`; missing, empty and symlinked
inputs fail. The cross-build path retains its pinned archive extraction.
The Windows workflow documents the required argument. LLVM and MinGW notices
remain tied to the actual compiler distribution, and SDL/OpenAL packaging
still retains the bundled versions' notices and source archives.

Validation: `python3 tools/workflow.py run test` passes at
`build/workflow/20261026T090525.628147Z/01-test.log`. The final focused
`test-license-notices` passes all five tests at
`build/workflow/20261026T090812.842174Z/01-test-license-notices.log`, including
invalid-input rejection, central notice availability, local inventory links
and exact agreement with vendored texts/header blocks. Independent comparison
confirms all six SDL/OpenAL central license texts match members of the
SHA-256-pinned source archives. New Cannonball/SE texts match fixed upstream
revisions recorded in the inventory; historical MAME and Vulkan texts retain
their upstream tags. Python compilation and `git diff --check` pass.
No gameplay code, ROMs or captures changed. No artifact was rebuilt or
published: packaging requires a clean committed source tree and release gates;
existing published previews retain their original notices.

## Standalone interactive stages prototype (2026-10-26)

The user selected standalone practice before normal-run integration.
`frontend/interactive_stages.hpp` contains authored, deterministic 60 Hz
enhancement rules for tanker rendezvous/docking and carrier approach/flare.
`tools/interactive_stages.cpp` is a separate SDL executable with original
geometric placeholder art, live alignment/speed/height/transfer guidance,
briefings, success/failure/bolter outcomes, keyboard/gamepad control, pause,
retry and switching. It reuses the existing input adapter and controller
database. It neither constructs a GameSession nor accesses the translated
board, ROM data, native score/lives, audio or recordings. Its constants are
prototype choices, not inferred arcade behavior. See
[usage and exact rules](INTERACTIVE_STAGES.md).

Focused final model and executable checks pass at
`build/workflow/20261026T092323.717947Z/01-verify-interactive-stages.log`.
Input-only analog and quantized digital controllers complete both challenges.
Other retained cases cover contact loss/reacquisition, overshoot, timeout,
unflared/off-deck touchdown, bolter, deterministic replay, reset and invalid
axis rejection. The actual SDL executable completes two 1,500-frame explicit
demo runs, writes both 1152x720 BMPs, leaves an unstarted briefing frozen and
rejects seven invalid CLI cases. No skips. Headless bounded runs ignore focus
loss so SDL's dummy driver cannot pause them before execution; ordinary live
practice pauses on focus loss and requires explicit resume.

The complete ROM-free `test` profile passes on the final executable/model/test
sources at `build/workflow/20261026T092432.184329Z/01-test.log` (91.98 seconds),
including the retained regression/mutation coverage. No shared CPU, generator, scheduler,
device, bus, game-session or native-sequence implementation changed, so no
new literal-port oracle or Stage 1 integration gate is claimed or required
for this standalone experiment.

An actual macOS desktop-driver run also completes 500 bounded landing demo
frames at prototype time 8.33333 seconds. Its approach image
`build/oracle/interactive-stages/landing-approach.png` was inspected for layout,
carrier/aircraft geometry, guidance and HUD readability. The ordinary sandbox
cannot enumerate macOS displays; this bounded window check was run with the
approved desktop permission. A subsequent unbounded interactive launch opens
the practice briefing. Computer Use cannot bind this unbundled executable,
so UI key-event automation and physical controller feel remain unverified;
model input checks do not substitute for those checks. No ROM assets or
generated images are tracked. Audio, main-menu integration, native transition
observation and automatic normal-run interruption remain future work.

Pitch-control follow-up (2026-10-26): the user requested reversed controls.
The prototype now negates only physical stick Y at its input boundary:
Down/pull back raises the nose and Up/push forward lowers it. Bank, throttle,
the shared NativeInput adapter, native gameplay controls and model-positive
nose-up semantics retain their previous behavior. The briefing, footer and
usage table describe the new directions. Regression assertions exercise the
actual boundary mapping and resulting opposite height/pitch changes, including
preservation of horizontal/throttle/button inputs. The complete `test` pass
is at `build/workflow/20261026T093907.789632Z/01-test.log` (87.20 seconds).
The rebuilt executable's model/demo/CLI/screenshot checks also pass at
`build/workflow/20261026T093907.789632Z/02-verify-interactive-stages.log`
(34.53 seconds). The updated live prototype was relaunched; physical-device
feel remains for the user to assess.

Carrier-forgiveness follow-up (2026-10-26): the user requested easier landing.
The authored carrier-only touchdown envelope expands from 6 to 12 units either
side of centerline, 70–105 to 60–120 U/S, bank 0.25 to 0.5, descent 3 to 5 U/S
and minimum nose-up pitch 0.15 to 0.05. The landing area extends from range
0..-180 to +25..-260, including a small early-touchdown grace zone. The
low-altitude slow-speed failure threshold changes from 65 to 58 U/S.
Below height 12 with positive pitch, flare assist bounds descent to at least
2.5 U/S: holding back can settle onto the deck rather than inadvertently
climbing into a bolter. The normal pitch/speed dynamics outside that assist,
reversed physical pitch mapping and refueling rules are retained.

Briefing/guidance and the exact-rule documentation describe the new behavior.
The model regression retains earlier analog/digital and negative cases and
adds an input-only neutral-approach/full-back flare success. Directed authored
policy cases accept combined offset/high-speed/moderate-bank/early touchdown
and later-deck touchdown, while rejecting clear lateral misses, excessive
speed and excessive bank. These are enhancement-policy checks, not MAME
fixtures or an arcade fidelity claim.
The complete ROM-free `test` suite passes on the final model/test sources at
`build/workflow/20261026T094800.131769Z/01-test.log` (89.26 seconds), retaining
all existing regression and mutation checks.
The rebuilt model/demo/CLI/screenshot gate also passes at
`build/workflow/20261026T094800.131769Z/02-verify-interactive-stages.log`
(34.23 seconds). The updated live executable was launched directly into the
carrier briefing for user testing; physical handling acceptance stays open.

Original-sprite presentation follow-up (2026-10-26): the user requested the
game's sprites in the standalone prototype. `InteractiveStageSprites` now
loads the user's existing set through `Roms`/`RomLoader` and caches five
transparent SDL textures using the unchanged `HWSprites` renderer and
`HWPalette` DAC. Player neutral/gentle-bank roots come from main `$28C94`
(neutral pitch index 2) and the initialized `$F2` colors from `$13F42/$2BF60`.
Carrier hull `$19C70`/palette `$6F` is identified by `$108AC`. Tanker fuselage
`$1D770`/palette `$46` and its mirrored wing pair `$1D780`/palette `$70` are
identified by `$16892..$16910`. Static colors come from `$13B1C/$2EDF4`.
No ROM bytes or exported pictures are added to source. Only the existing ROM
loader, palette and sprite-renderer implementation files are additionally
linked into the prototype; their sources are unchanged.

Local `roms/` or its ZIP is detected automatically; `--roms` accepts an explicit
directory/ZIP, and `--no-sprites` selects the retained ROM-free geometry.
Detected/explicit invalid sets fail clearly. The full set is currently loaded
although only main/sub/sprite regions are consumed. Texture ownership is
scoped inside renderer lifetime, and decoded art remains valid after temporary
ROM storage is released. Nearest filtering preserves the sprite pixels.
Challenge input/rules remain independent of all art. The small player bank
rotation, tanker assembly spacing, positions and scale are authored scene
choices; native pitch/exhaust animation, dynamic palette/shadow behavior and
camera/projection are not reproduced. The carrier uses its native hull view;
reticle/hose guide, background and HUD are still prototype geometry.

The complete ROM-free `test` suite passes at
`build/workflow/20261026T095647.905397Z/01-test.log` (87.98 seconds).
`verify-sprite-extraction` passes **2,931/2,931** unscaled pen comparisons,
zero skipped, with all four retained nibble/end-marker/pitch/bank mutations
rejected, at `02-verify-sprite-extraction.log` in the same workflow directory.
This independently checks the reused decode path, not native sequence fidelity.

Both refuel (frame 450, contact) and landing (frame 500, approach) ROM-sprite
images were visually inspected under ignored `build/oracle/interactive-stages/`.
The real macOS desktop driver also completes the 500-frame landing demo at
prototype time 8.33333 seconds; its inspected image is
`landing-sprites-desktop.png`. This requires the existing approved desktop
permission because the ordinary sandbox cannot enumerate macOS displays.
No new literal ROM port, board mutation or native sequence hook is introduced.
The final `verify-interactive-stage-sprites` gate passes at
`build/workflow/20261026T095647.905397Z/03-verify-interactive-stage-sprites.log`
(68.09 seconds). It retains both complete 1,500-frame ROM-free demo successes,
frozen briefing and seven invalid CLI cases, then completes both sprite demos,
checks changes in the aircraft scene area rather than the header alone and
rejects missing-ROM/conflicting-option inputs. Generated pictures remain
ignored. The sprite-backed carrier briefing was relaunched for user testing.

Runway/continuation/tanker follow-up (2026-10-26): the user's four notes are
implemented in the standalone prototype. Tab/Y now cycles refuel, carrier,
Stage 5 autumn runway and Stage 13 jungle runway; CLI names are `refuel`,
`landing`, `runway` and `runway-jungle`. Successful touchdown enters rollout
rather than a success modal. The revised `InteractiveStage` model retains a
ROM-free rollout/rearm/full-throttle/takeoff fallback, with its previous
approach tolerances and reversed pitch mapping intact.

`InteractiveSequencePlayer` is a narrow, engine-owned isolated `XBoardGame`
adapter. It boots through existing physical coin/start inputs; runways use the
existing directed Practice stage entry. A passive scan finds the active
`$1122E` owner at its state-4 ground pose (`$11428`), without assuming a pool
slot or changing sequence RAM. After interactive touchdown it executes the
original instructions for rollout, service vehicles, reload and departure.
It witnesses `$9100` pending ammunition and waits for both pending and `$90FF`
active to clear, then holds the board until throttle reaches 95%. This is an
explicit enhancement pause; native departure resumes on full throttle. The
native 16.768 ms frame interval is accumulated separately from the 60 Hz
approach model, and native audio is drained without playback. A short scene
fade introduces native RGB; native HUD bands are cropped for the standalone
arranged missile count and power prompt. Reset/switch discard the isolated
owner; main-game progression and the existing board/CPU/device implementations
are unchanged.

Carrier preparation stops its original startup just before deck arrival.
That ROM sequence has no carrier servicing animation, so this variant adds
an explicitly authored four-native-frame-per-missile service wait using the
ROM's ammunition maximum before the prompt and original carrier takeoff.
The arranged carrier count is UI state; it does not patch native ammunition.
This does not claim an original carrier reload scene. Without ROMs, the
fallback's 1.5-second rollout, 15-missile/s rearm and three-second takeoff are
authored placeholders. Directed startup retains fresh first-stage history,
not complete natural-credit state.

Tanker composition was wrong in three ways: horizontal spacing, vertical
spacing and which side was mirrored. Main `$167CE..$16846` projects the wings
from +/-`$208` in X and +`$50` world Y; `$1690C` flips the left part only.
Normalized placement is approximately +/-130 source pixels and +20 Y rather
than +/-90/+5. Read-only Stage 3 discovery observes body flags `$812F` at
(-5,-64), right wing `$812F` at (302,-16) and left `$A12F` at (-312,-16),
confirming sign, separation and vertical offset. The corrected composite was
visually compared with the original board's Stage 3 tanker image. The retained
discovery tool `interactive_sequence_probe` writes metadata and PPMs beneath
`build/oracle/`; it is not an independent MAME oracle.

The new native-wrapper gate passes at
`build/workflow/20261026T102122.296339Z/01-verify-interactive-sequences.log`
(86.92 seconds): **2,396/2,396 native RGB frames identical, zero skipped**
against separate direct-board executions. All three surfaces witness rollout,
rearm, ready and takeoff, hold their exact frame/image identity through 120
partial-throttle calls and complete departure after full throttle. This is a
wrapper differential, not fresh independent MAME evidence. Existing model
cases are retained, with both new runway variants and explicit post-rearm
power-gate checks added.

Complete local `test` passes at `02-test.log` in that directory (84.19 seconds).
Complete `verify-stage1` passes at `03-verify-stage1.log` (554.20 seconds),
including its retained integration, wide-stage, presentation/audio/lifecycle
checks; the dummy-display shader check reports its expected unavailable Metal
backend and uses the normal display fallback. The unchanged-decoder gate at
`04-verify-sprite-extraction.log` passes **2,931/2,931**, zero skipped, retaining
all four rejected nibble/end-marker/pitch/bank controls. No full `oracle-test`,
release, whole-credit or physical-controller acceptance is claimed for this
local enhancement increment.

The first four-choice executable gate passes at `05-verify-interactive-stage-sprites.log`
in that workflow directory (223.21 seconds): four ROM-free demo completions,
four ROM-backed completions, four scene-area image changes, all previous CLI
negative cases, missing-ROM/conflicting-option rejection and a held, fully
rearmed runway prompt. The final HUD crop was then adjusted to native rows
28..187 and retested separately; that earlier executable log does not cover
the final crop. Core/model/native-wrapper sources are unchanged across that
presentation adjustment, so their scoped passes above remain applicable.

An actual macOS desktop-driver run of the rebuilt final source reaches the
runway's ready phase after 1,500 prototype frames with 100/100 missiles and
holds there. `build/oracle/interactive-stages/runway-prompt-desktop.png` was
inspected for the full-throttle message, native animation scene and removal of
duplicate native HUD bands. The earlier review image
`tanker-fixed-review.png` confirms the corrected wing joins/orientation against
`probe3/150.png`. Physical button/controller feel remains for the user to
assess; Computer Use cannot bind this unbundled SDL executable.
The final-source executable gate passes at
`build/workflow/20261026T104016.349538Z/01-verify-interactive-stage-sprites.log`
(224.05 seconds), retaining all four choices and the held takeoff-prompt case.
The rebuilt discovery tool is recorded at `02-build_interactive_sequence_probe.log`
(29.87 seconds); direct CLI checks reject malformed stages and destinations
outside `build/oracle` before generating images. Python compilation, new-file
whitespace/newline checks and `git diff --check` pass. The final runway briefing
was opened for user testing. No ROM/capture files are tracked or committed.

Pre-burner prompt and continuous ROM scenery follow-up (2026-10-26): the user
correctly reported that the prior takeoff prompt appeared after automatic
afterburner had already started. The ROM overlaps its last reload ticks with
that flame animation, so waiting only for `$9100/$90FF` was too late visually.
The new passive descriptor observation checks active, published palette-$80
exhaust entries (assigned by `$122BA`) at the actual native layer frontier,
before composition. An initial instruction-only `$122E6` probe missed the
register-only producer phase; the new test rejected that attempt at
`build/workflow/20261026T124954.671431Z/01-verify-interactive-sequences.log`.
The published-descriptor observation replaces it; no CPU probe or shared
execution implementation is changed.

On the first burner frame the presentation retains its last unlit native RGB
image, then queues every subsequent original frame while the real native reload
finishes. The ready prompt therefore shows an unlit aircraft at full ammunition.
Neutral/partial throttle cannot expose the queued burner frames. Full throttle
replays the queue in order at the native frame interval, then continues the
live original board. This retimes presentation only: native instructions,
ammunition, source frame pixels and bus/device state are not patched. The queue
is bounded to 600 RGB frames and is not a save state. Carrier practice keeps
its existing pre-departure service wait; its source startup already waits
before its departure flames.

`InteractiveSceneCapture` now supplies native approach scenery and a detached
aircraft plane from the same partial-render frontiers. A separate `HWSprites`
instance renders copied descriptors, preserving original road/tile priority,
palette and scenery sprites without native player/exhaust duplication. It
admits the ROM-selected player pitch/bank roots from both `$28C94` (16-byte
bank records) and `$16B9E` (four-byte bank records), plus native Sub-ROM LODs.
Tanker/hose roots come from `$16892..$16910` and `$166CE`'s nineteen records;
the refueling backdrop removes that compound while retaining original wingmen.
This also removes the duplicate rolled native player/hose visible during the
first scenery review. No ROM pictures/tables are embedded in source.

All four challenges capture their ROM scenery at preparation. Up to 256
approach snapshots are retained in memory. Authored distance selects the
native scene; adjacent background images blend and aircraft bounds interpolate
with control offsets/rotation. The camera crop, sprite scale and palette
sources remain consistent across approach, touchdown and departure. Scenery
capture stops after preparation, while the lightweight exhaust observation
remains active. SDL uploads immutable frames only when source identity changes.
The geometric version remains the explicit ROM-free fallback. These authored
presentation choices are not a new arcade flight/camera fidelity claim.

The initial engine-wrapper/scenery gate, before the parked-camera refinement below, passes at
`build/workflow/20261026T130851.719907Z/01-verify-interactive-sequences.log`
(98.67 seconds): **2,396/2,396 underlying native RGB frames identical**, plus
**2,753/2,753 continuation image buffers identical** to their recorded direct-
board source frames before frontend crop/fade, zero skipped. All three landing surfaces complete rollout,
reload, a 120-call partial-throttle hold and departure; both runway variants
require a nonempty burner queue and reject any burner presentation before full
power. The scenery check preserves over 80% of the original scene pixels while
detaching a nonempty aircraft plane. This is a direct-board differential,
not independent MAME evidence. The companion prototype build passes at
`02-build_interactive-stages.log` (37.53 seconds).

Fresh complete ROM-free `test` passes at
`build/workflow/20261026T131434.036235Z/01-test.log` (98.14 seconds).
Fresh unchanged-decoder validation at `02-verify-sprite-extraction.log` passes
**2,931/2,931** frames, zero skipped, with all four retained controls rejected.
The first end-to-end four-choice executable run passes at
`03-verify-interactive-stage-sprites.log` (145.07 seconds), including four
ROM-free completions, four 2,400-frame ROM-backed completions, scene-image
changes and a 100/100 ready prompt with `burner_visible=0`. The final on-screen header
scene-label cleanup is rechecked separately below. Native-wrapper/model sources
are unchanged by that final presentation adjustment.

Original-board Stage 1 evidence is reused from
`build/workflow/20261026T102122.296339Z/03-verify-stage1.log` (554.20 seconds):
its test sources, transitive board/device/decoder sources, fixtures, compiler
options and capture instrumentation are unchanged. The new isolated scene and
presentation consumer is covered by the fresh native-wrapper and executable
checks above; this is not a new whole-host or full-oracle acceptance pass.
No shared port, scheduler, device, bus or original main application changed.

Final visual review exposed an additional handoff problem: state 4's gear pose
still uses the aerial approach camera, even when its object Y reaches `$F0`.
The preparation boundary now uses `$114B2..$114C4`'s explicit state-8 parked/
service countdown. The approach tape therefore includes the native descent
and ground arrival, so its endpoint joins the service/departure scene without
the previous aerial-to-ground change after the prototype declared touchdown.
Snapshots are sampled every two native frames, with the exact final pose also
retained; the 256-shot cap and frontend interpolation remain in place.

The final-source native-wrapper gate passes at
`build/workflow/20261026T133246.814435Z/01-verify-interactive-sequences.log`
(88.69 seconds): **1,808/1,808 underlying native frames** and **2,165/2,165
continuation image buffers** identical to direct-board source frames, zero
skipped. These comparison windows begin at the later parked handoff; the
earlier aerial/landing interval is now part of the ROM-rendered approach tape.
No retained oracle fixture, mutation or earlier log was removed. This is not
a claim of unchanged comparison-window coverage or fresh MAME acceptance.

The final executable gate passes at `02-verify-interactive-stage-sprites.log`
in that directory (168.44 seconds), retaining all four ROM-free and ROM-backed
completions and the fully rearmed, unlit prompt hold. The earlier on-screen
header-label cleanup also passed at
`build/workflow/20261026T132347.783015Z/01-verify-interactive-stage-sprites.log`
(180.52 seconds). Fresh `test`/decoder results and the explicitly scoped reuse
of unchanged Stage 1 dependencies above remain applicable.
Final-source native-window review at 600 approach frames is retained as
`build/oracle/interactive-stages/runway-seamless-approach-desktop.png`: the
runway/scenery and detached native aircraft now reach the ground-view endpoint
before the handoff. The final executable gate's fully rearmed, visibly unlit
prompt is `runway-full-throttle-prompt-final.png` in the same directory. The
earlier aerial-camera and pre-burner review images are retained as historical
diagnostics. Final source/object timestamps were checked, Python compilation
and new-file whitespace/newline checks pass, and `git diff --check` passes.
Runway practice was reopened with the final build. No ROM/capture is tracked,
no release/commit was made, and physical-controller feel remains unverified.

## Authored custom-stage framework (2026-10-26)

The user's revised direction supersedes native-film continuation as the primary
prototype. `tools/custom_stages.cpp` now builds `build/interactive-stages`;
`tools/interactive_stages.cpp` remains `build/interactive-stages-legacy` with its
original CLI, ROM-art and native-continuation checks. No fixture or mutation was
removed. The main arcade executable's stage transitions and flight rules are
unchanged.

`frontend/custom_stage.hpp` owns a fixed-60-Hz authored model and strict stage
configuration reader. Seven presets/examples cover carrier/autumn/jungle
landings, their independent departures and tanker refueling. Landing continues
through braking to a full stop and never starts a departure. Takeoff starts cold,
waits for full power, accelerates and needs pullback to rotate. The ready prompt
changes to the build-speed message in the same tick that exhaust becomes enabled.
Pitch inversion remains at the input boundary. Carrier landing allows 14 meters
of lateral error, five meters/second of descent, and low-height flare assistance.
Defaults are editable prototype proportions, not assertions about real aircraft.

`StageCamera` projects common metric world coordinates. An 18-meter fighter
span at 55 meters behind the camera and focal length 480 produces approximately
158 logical pixels; a 50-meter tanker assembly scales by its depth instead of
clamping to the previous oversized billboard. Runways are 2200 by 45 meters;
the carrier deck is 300 by 70. Deck/pavement meshes, runway markings, HUD,
camera and kinematics are authored, while original F14/carrier/tanker/crew,
exhaust, apparatus and road materials are decoded from the user's local ROMs.
This remains 2.5D; the carrier hull uses the original single rear-view image.

`InteractiveStageSprites` opts into additional custom-stage art explicitly;
legacy callers retain their previous constructor behavior. Main `$1686A`
selects the deployed apparatus from nineteen eight-byte records at `$166CE`:
entry 9's Sub frame is `$1D820`, palette `$8C`. It includes the actual hose and
nozzle. The apparatus, fuselage and both mirrored wings use one depth scale.
Its lower tip is aligned to the model's basket contact point. Exhaust uses the
first five `$12416` records and palette `$80`, with the ROM's attachment offsets.
Ground crew use the runway object's `$12A1A` frame source at their own human scale.

A temporary original board prepares each environment once. The new const
`XBoardGame::terrain_material()` renders only HWRoad background/foreground and
resolves its palette into a separate image: no sprite, tilemap or HUD pixels and
no board advancement. The board is discarded before custom simulation. Aerial
sky and parked runway ground are sampled separately because the airborne road
view can contain only sky. Stage 5/13 ground colors and Stage 1 sea are retained;
custom perspective rows use those materials. Early experiments sampling the
composited layer accidentally repeated HUD text; early aerial-only sampling also
produced a sky-colored runway landscape. Both were corrected before final review.
No native approach films or continuation frame queues drive custom gameplay.

Approach assistance is optional (H / gamepad X): a cross at the desired glide
angle, lateral/height correction box, winged flight-path deviation circle and
height-error ladder. Tanker contact also has a projected basket marker. The HUD
uses prototype graphics and does not copy another game's art.

Custom authoring uses strict `key=value` `.stage` files under
`assets/custom_stages/`, with finite/bounded numbers, duplicate/unknown rejection,
objective/environment checks and R/F5 reload. Fixed-frame CLI rendering draws the
final requested frame only; simulation still advances exactly one 1/60-second
tick per requested frame. Live rendering/pacing is unchanged. Local screenshots
and logs remain in ignored `build/oracle/custom-stages/`; no ROM data is packaged.

Validation: `build/workflow/20261026T145346.147949Z/02-test.log` passes the complete
regression/mutation suite (90.18 s). `03-verify-interactive-sequences.log` passes
three retained surfaces (90.18 s), including 1808/1808 raw RGB and 2165/2165
buffered presentations, zero skips. The direct-board comparison now also checks
that material export is stable, advances no frames, leaves native RGB unchanged
and excludes native actors/HUD. This is direct-board evidence, not independent
MAME. The initial custom executable gate passed at `01-verify-custom-stages.log`
(156.38 s); final presentation refinements are rechecked in
`build/oracle/custom-stages/final-cli.log`. These cover seven ROM-free completed
demos, seven ROM-versus-geometric in-flight image comparisons, a cold carrier
full-power hold, custom-file completion, eight invalid stage files, CLI rejection
and missing ROM handling. Final integration result is recorded below.

Fresh integration validation passes at
`build/workflow/20261026T145346.147949Z/04-verify-stage1.log` (511.74 s), including
all retained directed-stage/ratio, terrain, input, audio, attract, feedback and
native center-frame comparisons and their existing mutation gates. The unchanged
sprite decoder's earlier independent MAME extraction baseline remains
2931/2931 matching, zero skips (`20261026T131434.036235Z/02-verify-sprite-extraction.log`);
this change does not alter that decoder, DAC, ROM inputs or extraction tooling.
No new full-oracle, release, whole-credit or acceptance-milestone claim is made.

Manual final scene review covers runway flare, carrier approach, deployed tanker
apparatus/nozzle and takeoff exhaust. Representative final captures are
`build/oracle/custom-stages/runway-near-final.png`, `carrier-final.png` and
`refuel-final.png`. The updated interactive executable was opened on runway
landing. Stage files and usage are documented in `docs/INTERACTIVE_STAGES.md`.

Final command gates pass at `build/workflow/20261026T150831.079515Z/`:
`01-verify-custom-stages.log` (54.67 s) checks the final executable, all seven
objectives/art comparisons, cold departure and retained invalid-input fixtures;
`02-verify-interactive-stages.log` (97.44 s) rebuilds the renamed legacy binary
and retains its four completed SDL demos, screenshots, frozen briefing and seven
CLI rejections, plus its full analog/digital model suite. `git diff --check`
and stage/example/legacy command-link inspection also pass. Full native RGB,
regression/mutation and stage-one evidence above applies to the unchanged final
shared core; subsequent refinements affected only custom rendering and docs.

## Original custom-stage runway and scenery restoration (2026-10-26)

The user identified missing scenery and the generated runway covering the
carrier. Both were presentation omissions in the new custom-stage renderer.
ROM mode now samples the original parked HWRoad pavement, edges and stripe
pixels into `CustomRunwayArt`; no replacement markings or pavement colors are
painted. Each source row is deprojected between the native runway edges into a
reusable strip. The custom metric camera projects it by scanline. An initial
quad implementation bent stripes along affine triangle diagonals; scanline
projection removes that distortion. ROM-free fallback geometry remains explicit.

Carrier rendering has a separate branch: only the complete original `$19C70`
image supplies deck, markings, bridge, parked jets and hull. No land-runway
texture, pavement polygon or added runway markings run in this branch. The
close camera follows the original image's stern-to-bow deck rows so landing
rollout and independent takeoff keep the F14 over the native deck rather than
in front of its hull. This is authored 2.5D camera positioning, not a new port.

During each environment's temporary native startup, Main `$8AF2` scenery objects
supply their actual Sub-frame pointers, palette selectors and flip flags. Up to
24 distinct frame/palette pairs per environment are decoded by the existing
hardware decoder and palette DAC. Original rocks, trees, structures, towers and
vehicles are placed alongside the runway with distance scaling and depth order.
Positions and repetition are authored custom-stage scenery; native event/CPU
behavior is unchanged. Main `$7650`'s observed `$1E7E0`/palette `$CC` flyby
artwork supplies airborne sea scenery; land objects are excluded from water.
`--no-scenery` supports paired visual comparisons without changing HUD or physics.
The discovery tool now additionally records original frame/palette/handler
metadata and sprite-free terrain PPMs in its ignored output folder.

Local discovery evidence: `build/oracle/custom-scenery-probe-5.log`,
`custom-scenery-probe-13.log` and their matching directories. These are directed
original-board observations, not independent MAME captures. Original source
frame previews and final scene captures remain under
`build/oracle/custom-stages/`. No ROM-derived imagery or ROM bytes are checked in.

Validation adds original-row/color provenance and blank-frontier rejection to
`test-custom-stages`. The executable gate retains every previous case and adds
seven scenery-on/off pixel comparisons, actual scenery draw counts, original
runway-use checks, carrier/runway separation and rejection of the old generated
gray pavement in ROM scenes. Its scenery comparison includes the upper world
viewport: the first lower-only region missed takeoff flyby aircraft above it,
which correctly triggered a failing gate before the region was extended. The
original lower-world artwork comparisons are retained unchanged.

`build/workflow/20261026T164246.429378Z/01-test.log` passes the complete regression
and retained mutation suite (91.89 s). Native CPU/bus/device, HWRoad/HWSprites/
HWPalette, game-session execution, ROM inputs, build options and original-game
rendering are unchanged. The complete stage-one pass at
`build/workflow/20261026T145346.147949Z/04-verify-stage1.log` (511.74 s), and the
independent 2931/2931 sprite-pen extraction baseline recorded above, are reused
only for those unchanged dependencies. They are not presented as new runs or
whole-game/release acceptance. Final custom/legacy presentation results follow.

Final presentation gates pass at `build/workflow/20261026T164618.446452Z/`:
`01-verify-custom-stages.log` (104.43 s) retains seven completions and all
previous authoring/input cases, and passes original-runway/carrier isolation
plus seven visible scenery-on/off comparisons. `02-verify-interactive-stage-sprites.log`
(171.73 s) rebuilds and checks the retained legacy asset path: all four original
SDL demos, all four ROM-backed demos/artwork changes, cold reload-complete hold,
missing-ROM rejection and option rejection pass. No cases were removed.

`build/oracle/custom-stages/carrier-overlay-rejection.log` proves the added
pavement detector rejects the saved bad carrier image (7567 generated-gray
pixels) and accepts the corrected scene (zero). Final manual captures include
`runway-original-final.png`, `carrier-deck-final.png` (cold departure, F14 on
the original deck), and `landing-original-final.png` (rollout; no runway overlay).
The corrected native executable was opened on runway practice. Current guide
links and `git diff --check` pass. Later carrier camera refinements changed no
shared-core or model dependencies of the recorded full regression pass.

## Widescreen look and custom scenery coverage (2026-10-26)

Compared the custom stages visually with retained widescreen Stage 5/13 landing
and Stage 3 refueling PPMs (`stage-5-210.ppm`, `stage-13-210.ppm`,
`stage-3-210-358.ppm`) and `widescreen/edge-carrier-final.png`. The references have
orange broadleaf/palm corridors reaching both edges, packed foam patches around
the hull, and a black refueling backdrop. Copies for inspection and new local
captures are under ignored `build/oracle/custom-stage-wide-look/`.

`CustomSceneryField` now creates world-anchored cells over the whole visible
frustum, including the wide edges. It uses native `$1DAA0`/palette `$12` orange
trees and `$140F0`/`$14140`, palette `$D6`, palm clusters. The former 24-entry
custom catalog cap is removed. Native object, descriptor and depth-bucket limits
do not constrain these cached SDL instances. There are no land trees on water
or across the runway clearance corridor. Frustum/subpixel culling remains.

Water reuses the exact seven-frame Main `$3F918/$3F91C` table already consumed
by `WideVisibility::extend_water`, with the observed native `$23` palette. Patches
are smaller than the first trial, compressed with ground perspective, animated
and drawn behind the complete original carrier. An early upright-billboard trial
looked like high surf; flattening and resizing restored surface foam. Native
water frames are excluded from generic land props, removing leftover blue water
patches from the runway landscape. A representative frame draws about 4000 trees
or 2500 wave sprites, rather than exhausting the arcade's sprite capacity.

Runway material preparation now retains the original parked sunset/green sky,
not the earlier transition's cyan sky. Refueling loads Stage 3 and waits for its
original state-3 palette frontier. Its actor colors are read through the existing
read-only layer callback and decoded with the native distance-palette table at
`$16B8A`; the refuel pose comes from `$16B9E`. Small native fighter wingmen replace
the sea flyby formation there. No native RAM, palette or timing is modified.

Background and original-runway rendering now advance in three logical rows,
from the existing 392-to-160 scene scaling, instead of generating one-pixel road
bands. The original artwork/pavement/markings and independent custom stage rules
are retained. Raster scaling follows the SDL logical viewport at higher output
resolutions. Carrier deck/runway isolation remains covered.

New layout checks cover beyond-256 instance fields, left/right coverage, runway
clearance and stable world cells during camera movement. Existing executable
cases now require more than 256 actual tree/wave draws for each applicable stage.
`test_custom_stage_bands.py` independently examines saved background pixels;
`band-pixels.log` records a minimum 38-pixel color run in the sampled corrected
sea image and rejects the retained thin-band image (minimum 1 pixel). This check
is part of future `verify-custom-stages` runs. All previous fixtures/cases remain.

Final executable/model checks pass at
`build/workflow/20261026T181847.659427Z/01-verify-custom-stages.log` (107.67 s).
The complete regression/mutation suite passes at `02-test.log` (88.73 s).
The legacy executable rebuild passes at `03-build_interactive-stages-legacy.log`
(33.87 s). The added band-pixel check is separately recorded in
`build/oracle/custom-stage-wide-look/band-pixels.log`; it was added to the target
recipe after that executable run and run independently against its final image.
Native CPU/bus/device, original-game rendering, HWRoad/HWSprites/HWPalette,
compiler settings, ROMs and independent comparison fixtures are unchanged. The
previous complete stage-one and independent sprite-pen baselines retain only
that unchanged-core scope; no fresh full-oracle or release acceptance is claimed.

Focused legacy compatibility also passes: the rebuilt legacy refueling demo's
2400-frame BMP is byte-identical to the retained prior ROM-demo image. See
`build/oracle/custom-stage-wide-look/legacy-pixel-comparison.log` and
`legacy-refuel-default.log`. This pins the shared decoder/draw helper's default
presentation while custom-only vertical wave scaling and live refueling colors
are enabled separately. The updated prototype was reopened on autumn runway.
Documentation links, Python syntax and `git diff --check` pass.

## Connected custom landing/service/departure and guidance revision (2026-10-26)

The user's latest twelve-point revision changes the earlier independent-stage
plan: landing entries now continue through rollout, ground service and takeoff.
Standalone departure entries remain for practice. `seamless_takeoff=false` retains
the earlier landing-only behavior and its regression case; default/examples use
true. The custom scene/model owns the whole cycle without starting or cutting
to a native cinematic. A stable `surface_offset` prevents the former potential
180/55-meter scene jump when switching from landing to departure coordinates.
Carrier service is followed by a smooth deck-crew return to the catapult position,
keeping enough deck for an authored takeoff based on the arcade startup pose.

Ground service uses the ROM vehicle/crew art selected by `$12540/$1262E` and
`$12A2A`: the fuel truck/jeep arrive, a hose connects, fuel and missiles increase,
and the vehicles clear before full power is accepted. The service interval and
vehicle paths are authored for this prototype, not a new literal ROM port.
The chaser reads all three original frame/palette records at `$130C0` (autumn
motorcycle) or `$130D8` (jungle car), selected by native `$13090`. It follows the
landing/departure ground run. Carrier ground service shows deck crew and its
fuel connection. Full throttle held through service cannot trigger early exhaust:
release once, then reapply after the ready prompt. All three landing entries
have a corresponding connected departure; the three standalone presets remain.

Tanker apparatus remains the original `$166CE` deployed frame, palette `$8C`.
The upper flap is now composited after the tanker body, over the aft-right
fuselage rather than hidden behind it or placed over an engine pod. Its last
opaque source pixel defines the nozzle tip. The physical player probe extends
12 meters forward, 1.8 right and 0.3 up from the authored aircraft datum. Both
rendering and capture use that exact world point; capture requires distance
<=0.45 meters, matched speed and modest attitude. Coupling aligns/holds the
connection for four seconds; large inputs disconnect it. Fuel transfer cannot
complete at the old 14–24-meter floating proximity window. A visible metal probe
joins the F14 to the nozzle. Final transfer screenshots report zero projected
probe/nozzle error, with the upper hatch visible on the rear-right fuselage.

Afterburner images still use the five native `$12416` frames. The previous
per-frame Y offsets (1/7/12/17/25) were incorrectly treated as free center offsets,
which made the flame bob. The loader now records each image's opaque attachment
point and the aircraft engine hot spots. `anchored_stage_image` transforms both
through the same scale, mirror and bank rotation, fixing the flame root at each
engine for every frame. Ground/initial-departure poses use original `$10140`.

`CustomStageGuidance` separates ILS guidance from aircraft attitude: the moving
cross gives angular localizer/glideslope correction; the winged circle projects
actual lateral/vertical velocity. Approach gates project the glideslope corridor,
with a flare reference near the surface. Refueling instead has the requested
large tanker diamond and an inner diamond growing with closure/alignment. The
provided thumbnail reference was viewed in the browser; Reddit blocked the
landing image with a humanity challenge, so that aid follows the user's explicit
ILS/velocity-vector description. No challenge bypass or borrowed HUD bitmap was
used.

Carrier sea now has a pale cyan-to-white horizon band and horizontal wave
patches with no random mirror or rotation. Vertical compression and a small
raster-height floor prevent the earlier tall/rotating-looking wave figures.
Jungle fields use the upright palm family only; tilted native palm props are
excluded from the sparse layout. Ground environments no longer draw any
sky aircraft; refueling retains its right-side fighter wingmen. Original static
non-tree props come from the parked native `$8AF2` object snapshot, preserving
signed X/depth coordinates under one common 16-units-per-meter conversion.
The invented side-forcing and three repeated prop copies are removed. Extra
forest/wave field placement remains an authored custom-stage addition.

Validation adds complete three-environment service/departure timelines, smooth
surface coordinates, refueled cold holds, held-throttle release gating, physical
docking/rejection of the old distant window, ILS error signs/velocity alignment,
diamond closure and transformed flame-root invariance. Existing cases remain,
including the optional landing-only case. The executable gate additionally checks
all three serviced ready poses (100% fuel, 100 missiles, no burner), a mid-service
ROM image, successful physically connected transfer, five exhaust frame images,
no landing sky traffic, carrier/runway isolation and original scenery/artwork.

The complete regression/mutation suite passes at
`build/workflow/20261026T190632.884279Z/02-test.log` (84.90 s). Final extended
executable checks pass at `build/workflow/20261026T191708.860910Z/01-verify-custom-stages.log`
(196.21 s), followed by the legacy rebuild at `02-build_interactive-stages-legacy.log`
(32.26 s). The rebuilt legacy refuel image is byte-identical to the retained
reference; see `custom-stage-wide-look/revised-legacy-pixels.log`.

The final aft-right flap placement refinement was rebuilt separately at
`custom-stage-wide-look/final-port-build.log` and replayed in
`final-physical-refuel.log`/`.png`: coupled=1, 100% transfer and projected
probe/nozzle error=0. It changes only custom tanker attachment coordinates.
`revision-band-gradient-check.log` independently verifies 36 distinct horizon
colors and rejects the retained one-pixel-band fault image. Manual review also
covers ground-service vehicles/hose, cold carrier launch pose and changing
exhaust frames in `attached-flame-130.png`/`attached-flame-146.png`.

All native CPU/bus/device code, HWRoad/HWSprites/HWPalette, normal arcade stage
logic, ROM inputs and compiler options remain unchanged. Earlier full stage-one
and independent sprite-extraction evidence is reused only for those unchanged
core dependencies, not as fresh whole-game/release acceptance. The prototype
uses authored service timing, camera and docking rules with native artwork.


## Custom-stage independent ILS bars (2026-10-26)

The attached Ace Combat landing screenshot clarifies the custom HUD: a vertical
localizer bar moves sideways and a horizontal glideslope bar moves vertically,
with long capped arms around a fixed center ring. Replaced the short moving
cross and approach rectangles accordingly. Both bars center on the ring at
zero approach error; camera pitch does not move this datum. The separate
winged flight-path marker still follows actual velocity. This is authored
prototype presentation, with no change to arcade execution or ROM assets.

Regression cases cover independent axes, both error directions, centered
approach, off-scale bounds and camera-pitch independence. The focused suite
and native build pass at `build/workflow/20261026T195655.262924Z/`. ROM-backed
carrier/runway and high-approach captures were inspected under
`build/oracle/custom-stage-wide-look/ils-independent-*.png` and
`ils-high-approach.png`; these ignored captures remain local.

The complete `test` regression/mutation target passes at
`build/workflow/20261026T195736.657361Z/01-test.log` (95.95 s), including
the additional opposite-direction and saturation assertions. `git diff --check`
also passes. Earlier arcade/MAME evidence applies only to unchanged core
dependencies; no new whole-game acceptance is claimed for this HUD change.


## Custom stages: missiles-only service, smoke, audio and native backdrop (2026-10-26)

Ground service keeps crew/rearming transport and the full-throttle departure,
but removes fuel transfer, the fuel hose and the fuel-truck instance. Tanker
contact also reloads missiles rather than fuel, consistent with the original
RELOAD WEAPONS presentation. Fuel stays constant. The existing physical
probe/nozzle and capture tolerances remain in use. Ground reloads still take
the authored six-second loading interval inside the ten-second crew sequence;
tanker loading takes four connected seconds and retains partial ammunition.

Native discovery replays of Stages 9 and 18 were retained under
`build/oracle/custom-stage-revision/native-stage{9,18}/`, with discovery logs
in `custom-stage-wide-look/native-stage{9,18}.log`. Stage 9 provides the daytime
tanker, pale sky, snowy ground and forest reference. The custom stage now
extracts its settled palette and scenery instead of Stage 3's black night sky.
Additional ground instances fill the wide view; the backdrop horizon sits
below the aircraft and docking area. These placements remain authored.

The road-only material excludes the original horizon SPRITE gradient. Main
$10E6E and GroundHorizon identify palette E8 (pen 1 sky, pen 14 ground).
The loader retains that live palette at the carrier snapshot; its brightest
native gradient color drives the smooth blend ABOVE the horizon. A native
pre-sprite column at x=16, outside HUD text, supplies the sky fill. The tanker
uses the native E8 ground color for its snow surface, avoiding road-only seam
artifacts. Road-band thickness and horizontal carrier waves are retained.

Wheel contact resets the rollout clock. Two sets of alpha-faded radial smoke
puffs expand and drift aft during braking; they are authored procedural
presentation and do not change native sprite decoding or arcade execution.

`CustomStageAudio` uses the existing standalone ArcadeAudio runtime for native
ROM music ($91, MusicRouting), missile reload ($9B, Main $16CF4) and launch
($EF, Main $104D2). The engine bed and tire friction are explicitly authored
procedural audio. Playback is 48 kHz stereo, freezes/clears on pause, resets on
retry, and supports M/--mute. Offline --frames checks skip playback;
--audio-wav records deterministic PCM for tests. Captures remain ignored.

Validation: complete regression/mutation target `test` passes at
`build/workflow/20261026T201513.040464Z/01-test.log` (90.04 s). The final
native build passes at `build/workflow/20261026T202157.022325Z/` (36.01 s),
and the full custom-stage gate passes at
`build/workflow/20261026T202324.520007Z/01-verify-custom-stages.log`
(189.16 s). It retains all seven completions and prior cases and adds
missiles-only service assertions, visible touchdown smoke, native forest
coverage, decoded ROM audio energy, 50 ground reload cues, one touchdown and
one launch cue, plus exact stereo WAV duration.

Inspected images include `custom-stage-wide-look/rearm-smoke-final.png`,
`rearm-stage9-accepted.png` and `rearm-gradient-accepted.png`. Pixel checks
verify 25 gradient colors, monotonic shading with no raster gaps and immediate
sea below the gradient. They reject the retained detached/striped gradient
(`rearm-gradient-negative.log`) and original one-pixel-band fixture
(`rearm-gradient-check.log`). Earlier verification runs were interrupted for
the gradient repair; the final gate above completes cleanly. Audio capture
`rearm-audio.wav` has 1,840,000 stereo frames at 48 kHz, peak 5333 and RMS
653.3; the native audio device opened in the live prototype. Physical listening
quality remains a user check. `git diff --check` passes.

Native CPU/bus/device, sprite/road/tile compositors, sound-board runtime and
ROMs were not changed. Earlier stage-one evidence
`build/workflow/20261026T145346.147949Z/04-verify-stage1.log` and sprite
extraction evidence `build/workflow/20261026T131434.036235Z/02-verify-sprite-extraction.log`
are reused only for those unchanged core dependencies, not as fresh acceptance
of the custom authored scenes.

The retained legacy executable also rebuilds cleanly at
`build/workflow/20261026T202650.053053Z/01-build_interactive-stages-legacy.log`
(35.19 s). A fresh 2400-frame native-art refuel demo is byte-identical to the
previous retained BMP; see `custom-stage-wide-look/rearm-legacy-comparison.log`.


## JSON custom stages and modern scene compositor (2026-10-26)

Supersedes the prototype's earlier key/value authoring format. All seven
bundled presets are version-1 JSON documents and are now the source used by
--stage/default startup, not just examples beside C++ defaults. --stage-file
loads arbitrary JSON; R/F5 rereads it. The strict standalone parser rejects
duplicate keys, bad types, nonfinite/invalid numbers, trailing data, malformed
UTF-8 and invalid Unicode escapes. stage.schema.json describes the format;
layered-runway.json demonstrates screen/world layers over the existing scene.
Historical .stage inputs remain represented as a rejected-format test.

CustomStageScene submits direct SDL geometry and full RGBA32 textures. Its
layer/element vectors and repeated tile grids do not use Main/Sub object
pools, native priority bits, indexed palette entries, tile codes, ROM LODs
or the ROM frame decoder's image-size limits. Native artwork is an optional
asset provider, not the custom scene's allocation/format contract. There is
no fixed scene layer/sprite/tile count; memory, frame time, numeric precision
and the host GPU's capabilities remain practical limits. Color is 8-bit per
RGBA channel with alpha blending, not an indexed arcade palette.

Layers have stable numeric ordering, screen/world positions, tint, alpha and
rotation. PNG (SDL 3.4+) / BMP assets resolve relative to the JSON file.
Element repeat counts describe tile grids, with visible-cell/subpixel culling
before iteration. Built-in environment, guidance, aircraft/effects and HUD
are composition landmarks; fractional numeric orders also work between them.
presentation.rom_environment=false replaces the stock background and scenery
with custom layers while retaining the practice objective and player. Flight
physics validation ranges are independent of rendering capacity.

No arcade execution, native decoder, compositor, sound runtime or ROM data
was changed. Existing fixture and mutation coverage is retained, with the
authoring rejection cases migrated to JSON and extended for syntax/version
and rendering-resource validation. The new direct-render check uses 40
layers, 600 explicit sprites, 4800 image tiles and a 1024-pixel true-color PNG,
and independently checks alpha/order, color diversity and HUD-boundary
placement in captured RGB. Generated images remain under build/oracle.

Validation: `test` passes at
`build/workflow/20261026T203709.532611Z/01-test.log` (93.95 s). The final
`verify-custom-stages` passes at the same run's
`02-verify-custom-stages.log` (225.06 s), including the native build, all
retained CLI/ROM cases, JSON rejection cases and modern scene RGB checks.
The stock carrier's JSON-driven image is byte-identical to its pre-migration
reference; see `build/oracle/custom-stage-json/carrier-preserved-comparison.log`.
The extra layered example also completes via the JSON CLI. `git diff --check`
passes. C++ preset helpers remain only as model-test fixtures; the executable
loads its active presets from disk. The JSON Schema is provided for editors;
the C++ loader performs the runtime validation without an external dependency.

Earlier full native stage-one and sprite-decoder evidence remains scoped to
unchanged arcade dependencies; this renderer/parser work is isolated to the
standalone custom-stage target and does not claim new whole-game acceptance.


## Custom landing polish: ROM smoke, pitch poses and ocean haze (2026-10-26)

Read-only ROM inspection is retained in build/oracle/landing-polish/main.bin
and the two landing disassembly text files. Main $119A2 creates the paired
wheel clouds; $11A1E selects Sub frame $164B0 / palette $EF and $11A38 grows
the cloud with its zoom table. The custom renderer now uses that original
silhouette at both main wheels for carrier, autumn and jungle landings,
neutralizes its source tint to gray, and composites expanding/fading alpha
instances. This supersedes the procedural radial smoke geometry. Placement,
color treatment and duration are authored custom-stage presentation.

The inspected landing path emits command $EE at $112D6/$112DA before the
touchdown cloud sequence and $EF at $11500/$11502 for departure. No dedicated
tire-squeal command was identified in the wheel-cloud routine. Rather than
labeling an unverified command as a brake effect, CustomStageTireSound adds an
authored, attack-smoothed rubber spin-up chirp with descending resonance and
speed-dependent filtered friction through the existing audio mixer.

Main $99E4/$9A0A selects pitch/bank frames through $28C94. All five pitch
rows and six bank magnitudes are cached from the ROM; normalized authored
pitch maps to those five rows. The old gear-proximity branch had overridden
all airborne pitch, which explains the static landing pose. Gear proximity
now leaves the airborne selection active; only a grounded aircraft retains
its ground pose. Each selected image supplies its own exhaust attachment
anchors, preserving the prior afterburner fix. Legacy player drawing is
unchanged.

The carrier no longer forces the ROM's white/electric-blue palette boundary.
CustomStageOcean interpolates a desaturated sky through one shared horizon
haze color into deeper foreground water; presentation.ocean in the carrier
JSON files exposes sky/haze/water RGB triples. Native wave sprites retain
horizontal animation, with deep-blue fill keyed transparent and distance-
faded/tinted foam. This modern RGBA treatment is authored and has no effect
on the normal arcade render or optional external JSON layers.

The new ROM-backed verify-custom-stage-art gate independently compares wheel
regions with/without smoke on all three surfaces, verifies neutral translucent
cloud output, checks pitch response near gear-down height, and captures five
distinct ROM pitch images. A pure model/audio check covers silent reset,
non-clipping tire attack/decay and sky/sea continuity. The background pixel
gate retains runway road-band thickness and the old thin-band fault fixture,
while checking a continuous ocean gradient instead of imposing road bands
on this smooth modern water surface. All earlier CLI cases remain retained.

Validation: the complete `test` target passes at
`build/workflow/20261026T210250.369675Z/01-test.log` (89.42 s), followed by
the final `verify-custom-stages` at `02-verify-custom-stages.log` (203.28 s).
That gate includes the new ROM-art checks, all seven completions, all three
touchdown smoke captures, audio/WAV checks, five attached exhaust frames,
strict JSON colors and the existing modern 40-layer/600-sprite/4800-tile checks.
The original-focused art gate also passes independently at
`build/workflow/20261026T205952.916131Z/01-verify-custom-stage-art.log`.

Reviewed approach and touchdown images are retained in
`build/oracle/landing-polish/carrier-approach-final.png`,
`carrier-braking-final.png`, `runway-braking-final.png`, and the five pitch
images from the art gate. The tire waveform unit check verifies attack/decay
and safe amplitude; listening quality remains a user check. The new horizon
has 24 distinct sky-gradient colors and 195 ocean-gradient colors in the
measured column, with shared sky/sea haze and retained runway band checks.
`atmosphere-pixels.log` also rejects the retained thin-band control, while
`old-gradient-rejected.log` rejects the earlier striped/detached horizon.
All captures/ROM data remain ignored. `git diff --check` passes.

The normal arcade CPU/bus/device, native renderer and sound runtime are
unchanged. Earlier native stage-one and extraction evidence applies only
to those unchanged dependencies; the new presentation is an authored optional
prototype, not new MAME/whole-game acceptance.

The retained legacy prototype rebuilds at
`build/workflow/20261026T210810.480396Z/01-build_interactive-stages-legacy.log`
(35.39 s). Its fresh 2400-frame ROM-art refuel BMP remains byte-identical to
the prior reference; see `landing-polish/legacy-comparison.log`.


## Custom guidance feedback and tanker latch (2026-10-26)

ILS localizer and glideslope bars now independently thicken from one to three
logical pixels, including their end caps, inside a three-pixel centering
window. Position/velocity guidance geometry remains unchanged. The header
explains the bold state. Matching one axis does not bold the other axis.

The tanker diamond aid formerly looked coincident before the physical probe
reached the nozzle, and strong inputs could immediately break captured contact.
The inner diamond now retains an eight-pixel visible gap until the existing
0.45-meter physical capture, matched speed and attitude conditions succeed.
Capture snaps the probe into its established contact geometry, locks both
diamond centers/radii into one bold green outline and automatically rearms
for four seconds. Stick/throttle commands are ignored during the latch;
R/retry resets it. Fuel remains unchanged. Near-contact speed/attitude errors
now prompt speed matching and level wings; the briefing states the correct
12-meter probe spacing instead of the obsolete 14–24-meter proximity window.

New regression cases retain the earlier no-distant-transfer check and add
full-control disturbance through a complete latched reload, fixed physical
contact, monotonic ammunition, reset and pre-capture visual-gap checks.
The ROM-art gate compares bar thickness in captured pixels for both-centered
and one-axis-only cases. It also compares the entire tanker world rectangle
before/after 30 ticks of strong controls during captured rearming with the
environment disabled, requiring identical foreground pixels so background
motion cannot mask diamond/aircraft drift under the latch.

This supersedes the earlier custom-stage break-contact behavior only. The
normal arcade stage/sound/physics code and ROM assets are unchanged.

Validation: focused model checks pass at
`build/workflow/20261026T211307.683275Z/01-test-custom-stages.log`; final
UI build and independent pixel/art checks pass at
`build/workflow/20261026T211645.783259Z/` (36.27 s / 4.02 s). The complete
`test` target passes at `build/workflow/20261026T211811.349799Z/01-test.log`
(93.35 s), and its final `02-verify-custom-stages.log` passes (205.09 s),
retaining every existing case and the new axis/lock checks.

Native-art captures `landing-polish/ils-bold-rom.png` and
`tanker-lock-rom.png` were inspected. The latter is in contact phase at tick
1250 with coupled=1, probe/nozzle pixel error=0 and 64/100 missiles; fuel remains unchanged. Bare-background captures `ils-weight-0/1/2.bmp`
and `tanker-locked/held.bmp` retain the independent pixel comparisons.
`git diff --check` passes. Earlier MAME/native evidence remains limited to
unchanged arcade dependencies; no new whole-game acceptance is claimed.


## Restore classic carrier look with connected horizon blending (2026-10-26)

The user prefers the earlier arcade presentation. Both carrier JSON presets
again use bright cyan sky and saturated blue foreground water. The sky stays
flat through its upper 60%, then transitions to a pale horizon; the sea
transitions from that same color through its first 17%, then stays at the
classic foreground blue. This retains the older broad color fields while
fixing the disconnected/hard sky-water boundary and raster gaps.

Native wave frames again decode with their original RGB/opaque blue fill,
without the muted tint or foreground alpha reduction. Only their distant
edge fades through a short band, avoiding blue sprite patches cutting across
the horizon blend. Wave geometry/orientation, original carrier/deck art,
landing controls, pitch poses, smoke, audio and ILS/tanker feedback are retained.

The model check pins the flat upper sky and classic foreground alongside the
shared horizon color. Pixel checks verify exact stock sky/sea colors, a
smooth 46-color transition and existing runway-band coverage. The retained
one-pixel-band fixture still fails its negative control. The restored look
was reviewed in `build/oracle/landing-polish/classic-carrier.png` and the
background-only `classic-horizon.png`. This is optional authored presentation;
normal arcade rendering and ROM/CPU/device logic are unchanged.

Validation: focused model/build passes at
`build/workflow/20261026T212504.217547Z/` (2.84 s / 37.18 s), followed by
the complete `test` target at
`build/workflow/20261026T212627.386963Z/01-test.log` (93.16 s) and final
`02-verify-custom-stages.log` (205.25 s). All retained objective, ROM-art,
smoke/pitch/audio, ILS weight/lock, strict JSON and modern-layer cases pass.
`git diff --check` passes. Earlier MAME evidence remains scoped to unchanged
arcade dependencies, not fresh whole-game acceptance.


## Projected player shadow for custom landings (2026-10-26)

The custom renderer now projects a soft translucent F-14 silhouette onto the
runway or carrier surface. The mask is a separate linear-filtered RGBA copy
of Sub $10140 / player palette $F2, so the visible aircraft retains its native
nearest-neighbor art. The small native helper frame initially investigated
was unsuitable for an aircraft-shaped projection and is not used. No shadow
behavior is ported or inferred into the normal arcade simulation.

CustomStageShadow defines authored sunlight offsets in world X/depth,
perspective footprint reduction, height-dependent opacity/softness and slight
bank foreshortening. A normalized nine-tap kernel softens the black alpha
mask. Its textured polygon is clipped against the perspective runway/deck
receiver with interpolated UVs, before drawing crew/player/landing smoke.
This prevents the footprint from spilling outside the modeled surface or
appearing in the sky/sea before the aircraft reaches it. Air tanker stages
skip it. presentation.player_shadow in JSON defaults true and can disable
the effect; bundled non-tanker presets explicitly enable it.

The model check covers contact versus airborne footprint/opacity/softness and
receiver clipping with preserved UVs. ROM-art tests compare on/off captures
for autumn, jungle and carrier surfaces, require only darkening in the
receiver region, and verify absence before the carrier surface and during
air refueling. Existing CLI smoke checks also require the shadow on all
three landing surfaces, and strict JSON rejects a nonboolean shadow setting.
Reviewed native-art captures are retained as
`build/oracle/landing-polish/runway-shadow-final.png` and
`carrier-shadow-final.png`, with on/off art fixtures `1/2/3-shadow-*.bmp`.
Normal arcade CPU/device/rendering, ROMs and stage physics remain unchanged.

Validation: focused model/build/art checks pass at
`build/workflow/20261026T214245.879969Z/` (2.98 s / 35.76 s / 4.06 s).
The full `test` target passes at
`build/workflow/20261026T214613.461336Z/01-test.log` (94.71 s), followed by
the final `02-verify-custom-stages.log` (204.40 s). All retained stage,
smoke/pitch/audio, classic horizon, ILS/tanker latch and modern JSON/layer
cases remain passing, along with the new shadow geometry and pixel cases.
`git diff --check` passes. Earlier MAME evidence remains restricted to
unchanged arcade dependencies; this is not new whole-game acceptance.

## Random native tanker backdrops for custom stages (2026-10-27)

The custom tanker scene now selects complete native sky/ground/scenery sets
from ROM stage slots 3, 9, 11, 16, 19 and 21. The catalog is discovered at
runtime through StageCatalog metadata and event programs: Main $DEE4/$DEFC/$DF0A
handles command 0, subselector 4 by setting the actual refueling flag. No ROM
bytes or captures are included in source. A shuffled bag visits each eligible
variant before starting another cycle and avoids consecutive repeats across
cycles. The choice remains fixed throughout an attempt; retry/completion Enter
and a new tanker entry select again. presentation.tanker_variant accepts random
or a fixed original tanker stage number, and --seed reproduces random choices.

Each choice boots the corresponding native sequence to settled refuel state 3,
collecting its scenery frames, native placements, sky/ground palette and player
palette together. The previous material and scenery cache are invalidated as a
unit, preventing scenery/palette mixtures when switching. The authored camera,
dense scenery extension, physical nozzle docking and missile rearming remain.
Normal arcade execution/rendering and the legacy prototype are unchanged.

Native reference replays and rendered variants remain under ignored
build/oracle/tanker-variants, with the existing native-stage9 reference reused.
Reviewed variants include night, lavender/blue-green and bright autumn scenes.
The stage 11 probe obtained the settled tanker reference, then encountered the
existing unported Main $14D38 caller during later combat; this does not affect
asset loading, which stops at settled refueling, or constitute combat acceptance.

Model checks cover five complete shuffled cycles, no adjacent repeats,
same-seed replay and fixed selection/rejection. Art checks verify scenery-cache
replacement. The native CLI fixture checks all six distinct sky, ground/scenery
and scene images, successful physical docking with unchanged fuel, identical
same-seed pixels and rejection of a non-tanker slot. Existing CLI comparison
fixtures use a fixed seed so paired images use the same backdrop. All previous
fixtures and negative controls are retained.

Validation: focused model/build and ROM-art checks pass at
build/workflow/20261026T215836.456971Z and 20261026T220102.420582Z.
The full test target passes at
build/workflow/20261026T220246.313875Z/01-test.log (92.29 s), followed by
02-verify-custom-stages.log (255.07 s), including all six native variants.
Python compilation and git diff --check pass. Prior MAME captures remain scoped
to unchanged arcade dependencies; no fresh whole-game acceptance is claimed.

## Tanker contact/pitch corrections and custom sky clouds (2026-10-27)

Compared the original Stage 9 settled refueling capture
build/oracle/custom-stage-revision/native-stage9/300.png with the custom scene.
Main $16848..$1688C derives apparatus X from $16766 and Y from entry 9 of
$166CE, with four multiplier units per source pixel. The deployed hose now
reads those offsets at runtime instead of the authored (20,58) placement that
left its flap/hose too low. Body/wing assembly zoom remains shared. The hose
is composited in front of the player after capture, matching the connected
original look; no extra long metal probe or contact ring is drawn.

The custom metric probe datum remains at the same physical capture range.
Its projection is calibrated to the selected ROM aircraft's first opaque
centerline canopy/nose pixel, with one source pixel of overlap. Averaging the
two highest tail-fin pixels was investigated and rejected: their midpoint is
empty sky, which reproduced a visible gap despite a zero coordinate error.
Art checks now independently require the contact point to cover a rendered
opaque aircraft pixel. Diamond acquisition shares the model's aligned predicate,
with the approaching inner diamond growing nearly to the outer outline instead
of retaining an eight-pixel gap. Capture continues to hold the aircraft and
connection throughout four-second missile-only servicing.

Removed the custom escort-aircraft instances from tanker skies. The player now
uses all five runtime $28C94 pitch rows and their bank records during tanker
flight; the old refueling override and the incorrect grounded test on a negative
basket-height error no longer freeze its pose. Tanker pitch textures have a
separate cache with the settled sequence's native player palette, leaving
landing palettes independent when cycling between stages.

CustomStageClouds generates a soft full-RGBA cloud texture and composites four
large translucent masses behind aircraft and scenery in every custom sequence.
Camera position/pitch affects their placement; dark tanker skies dim them.
They remain stable through an automatic servicing hold. presentation.clouds is
a schema-checked JSON boolean defaulting true; --no-scenery also hides clouds
for independent terrain comparisons. This is authored optional presentation,
without native layer/sprite/palette restrictions or changes to arcade execution.

Focused art tests cover all seven clouded skies, independent translucent pixel
compositing, tanker pitch selection with negative basket-height error, contact
on actual opaque aircraft art, hatch offsets and connected nozzle error. All
previous ILS, smoke, shadow, five-pitch and held-input capture fixtures remain.
The six native variant checks now also require zero extra sky aircraft and
four cloud masses. Captures remain ignored under build/oracle/landing-polish.

Both connection endpoints are now actual opaque source-pixel centers: the hose
uses an opaque pixel on its final visible row, rather than an averaged edge
outside that row. Independent rendered-pixel tests cover the aircraft anchor
and nozzle endpoint. The endpoint fixture samples the containing framebuffer
pixel (floor), avoiding rounding across its lower edge. A separate palette
replacement fixture confirms tanker live colors do not change landing poses.

Validation: the full test target passes at
build/workflow/20261026T222437.772853Z/02-test.log (92.11 s), including all
retained HUD/player mutations. Final verify-custom-stages passes at
build/workflow/20261026T222839.713779Z/01-verify-custom-stages.log (284.09 s),
covering the rebuilt renderer, opaque contact endpoints, palette isolation,
all seven objectives, audio, horizon/band checks, JSON layers and six native
backdrops with deterministic seeded replay. Focused final art checks also pass
at build/workflow/20261026T222821.268659Z/01-verify-custom-stage-art.log.
Reviewed final native scene: build/oracle/landing-polish/tanker-corrected-native.png.
Python compilation and git diff --check pass. Core CPU/device/arcade renderer
sources are unchanged by this work; prior MAME evidence is reused only for those
unchanged dependencies, not presented as fresh whole-game acceptance.
The updated native tanker prototype was reopened for interactive review.

## Stable tanker attitude and takeoff exhaust (2026-10-27)

The tanker renderer previously calibrated the docking frame to the current
pitch/bank sprite's canopy pixel on every draw. Crossing a discrete ROM frame
boundary therefore shifted the entire tanker/hose assembly. The player also
moved down with positive pitch, opposite to the landing renderer's smooth
screen displacement. The metric docking frame now uses the neutral aircraft
datum, independently of the displayed pose; the player uses the same -12-pixel
pitch displacement as landing. Visible nozzle error still measures the current
aircraft pose, and capture still levels and locks it to the nozzle. Model
physics, timing, tolerances and landing ILS behavior are unchanged.

Gentle left banks had a second threshold mismatch: native bank pose 1 begins
near magnitude 0.1, but mirroring waited until 0.18. The selected native bank
pose now determines mirroring for both the fighter and its canopy anchor.
Flame orientation mirrors with the aircraft too, as do its engine anchors.

The prior custom exhaust code cycled the first five $12416 records every four
ticks, causing large plume-shape changes despite a held aircraft pose. The
original takeoff setup at Main $11318 writes a fixed byte offset (20) into the
exhaust selector; $12308/$1230C adds that selector to $12416. Existing native
runway probes also retain Sub $1E010 under handler $122DA during takeoff
(build/oracle/custom-scenery-probe-5.log and custom-scenery-probe-13.log).
The custom renderer now reads that native takeoff selector at runtime and
retains the chosen plume geometry. A restrained authored brightness pulse
provides motion without translating the plume or changing its footprint.
Per-pose opaque engine anchors, common scale and bank transforms are retained.
This is an optional custom-stage presentation fix, not a new arcade port.

New independent pixel checks first rejected the prior tanker pose coupling
(build/workflow/20261027T054350.996877Z/01-verify-custom-stage-art.log), then
rejected the old time-cycled exhaust after the tanker repair
(20261027T054403.967787Z/01-verify-custom-stage-art.log). The passing checks cover
tiny attitude changes across pose/left-bank boundaries, twenty fixed-pose ground
exhaust frames, and both airborne bank directions. All previous five pitch,
nozzle-contact, ILS/lock, clouds, smoke and shadow fixtures remain. Generated
before/after pictures and flame snapshots stay under ignored build/oracle.

Validation: focused art/build passed at
build/workflow/20261027T054438.254562Z/ (4.72 s / 34.24 s). Final checks at
build/workflow/20261027T054551.195556Z/ pass: 01-verify-custom-stage-art.log
(4.86 s), 02-test.log (91.82 s), and 03-verify-custom-stages.log (284.67 s),
including the final rebuilt renderer and retained ROM-art/CLI/scene/variant gates.
Reviewed fixed-pose plume image: build/oracle/landing-polish/steady-flame-0.png.
The updated tanker prototype was reopened for interactive review.
git diff --check passes. Arcade CPU/device/rendering sources are unchanged by
this repair; earlier native/MAME references are reused only for those unchanged
dependencies, without claiming fresh whole-game acceptance.

## Original HUD widgets and ROM font inside custom playfields (2026-10-27)

CustomStageHud now decodes the original 512 planar text tiles, caches a font
atlas and a full-color eight-palette widget atlas, and places them freely in
the authored scene. Font pen 7 retains the black outline/counters; other ink
pens accept the prompt color, with nearest-neighbor sampling. The glyph test
independently decodes the ROM's A tile and checks all 64 rendered cell centers,
including lowercase-to-uppercase handling. The ROM slash and minus tiles have
zero foreground pixels, so metric labels use MPS and ABOVE/BELOW or CLIMB/DESCENT
instead of losing units or sign information. No font bytes are embedded in code.

At the settled asset-loading frontier, the existing temporary native board
supplies visible 40x28 entries from Main text RAM $0D0000 (native column offset
24), plus the live $1C00 text palette colors already delivered to the layer
callback. SCORE/HIT surrounds, speed ruler, STAGE lettering and missile icons
therefore retain the original tile entries and palettes. Constructor defaults
read ROM palettes for isolated art previews without native execution. The
native board is still destroyed after asset loading; no arcade state or HUD
sequencer controls the custom simulation.

The authored layout now puts all text and widgets inside the existing
(40,78)-(920,470) playfield. The outside debug title, metric panel and control
footer are removed. Briefing, paused/completion feedback, control hints and live
instructions use the game font. ILS/diamond geometry and camera/world coordinates
are retained, with concise guidance above and phase prompts below the aircraft.
Score/hits stay zero in these combat-free challenges; preset numbering, missile
symbols/count, speed fill and metric readings bind to the custom model. A
continuous fill replaces repeated pointer fragments under the original speed
ruler. presentation.hud is a strict schema-checked boolean, default true; custom
layers at order 1000 and above retain their above-HUD drawing contract.

ROM-art checks cover original font pixels, all seven in-playfield HUD layouts,
empty outer borders and missile-icon changes during rearming. Guidance-only
held-input checks explicitly hide the changing HUD so world motion cannot be
confused with live counter/progress updates. All prior smoke/shadow, cloud,
nozzle, pitch/bank/exhaust and palette-isolation fixtures remain. CLI cases
require game-font/native-widget diagnostics for every ROM-backed preset and
reject a nonboolean HUD setting. Captures remain ignored under build/oracle.

Validation: final focused art/build/full checks pass at
build/workflow/20261027T060539.167078Z/: 01-verify-custom-stage-art.log
(4.98 s), 02-build_interactive-stages.log (39.05 s), 03-test.log (101.32 s),
and 04-verify-custom-stages.log (256.61 s). The retained HUD/player mutation
checks, complete custom objectives, native art, horizon/bands, modern JSON layers
and all six seeded tanker variants remain passing. Reviewed final captures:
build/oracle/landing-polish/hud-runway-final.png and hud-carrier-final.png.
The updated runway prototype was opened for interactive review. Python syntax
checks and git diff --check pass. No arcade CPU/device/renderer source is
changed by this HUD work; prior MAME evidence remains scoped to unchanged
arcade dependencies rather than fresh whole-game acceptance.
