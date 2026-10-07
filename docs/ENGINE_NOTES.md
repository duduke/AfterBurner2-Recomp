# Engine reverse-engineering notes

Unlike the hardware layer (`docs/HARDWARE.md`, distilled from MAME's
driver), there's no existing reference for the actual 68000 *game code* —
this file tracks what's been figured out directly from disassembling the
ROM, and is expected to grow incrementally as more of it gets mapped out.
Treat everything here as "best current understanding," not verified fact,
until it's actually been exercised by a running port.

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
in [PHASE3C_PLAN.md](PHASE3C_PLAN.md); next work is [PHASE3D_PLAN.md](PHASE3D_PLAN.md).

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
No ROM data or captures are committed. Next: [PHASE3D_PLAN.md](PHASE3D_PLAN.md).


## Phase 3d — verified foundations; phase remains incomplete

`OMainObjects` ports `$01488E` initialization, `$014594..$0145B8`
dispatch, `$00C5A8` RNG, and sequence compound setup/initialization/hiding
(`$016976`, `$016892`, `$0169B2`). `OMainSound` ports the command-ring
producer entries `$00B840/$00B84C` and timer-interrupt consumer `$00B7F8`.
They are built and independently verified, but **not wired into OEngine**:
untranslated live handlers still prevent replacement of `OLevelObjs` and
its sprite adapter. No new gameplay/flight or sound-cadence equivalence is
claimed. See [PHASE3D_PLAN.md](PHASE3D_PLAN.md).

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
below. The remaining 3d work is tracked in `PHASE3D_PLAN.md`; Phase 3e
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
in `PHASE3D_PLAN.md`. These ports remain unwired.

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

1. Read the roadmap's working rules and `PHASE3D_PLAN.md` before editing. Port
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
