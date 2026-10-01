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

The object table spans `$9FC0` to `$DF40` in `backup1` mirror RAM, comprising **254 slots of 64 bytes each** (`$00FE` loaded from `$0001C176` into `$8806` at `$014594`).

### 64-Byte Object Entry Layout
- `+0`: Bit 7: `0` = Active, `1` = Free/Inactive. Bit 5: Horizontal flip. Bits 0-9: Screen Y position.
- `+2`: Palette / bank / priority attribute.
- `+3`: Current sprite frame index.
- `+4`: Longword: current screen X, Y coordinates.
- `+8`: Longword: previous screen X, Y coordinates (used for interpolation / deltas).
- `+12`: Longword: animation script / state sequence pointer.
- `+16`: Longword: **Per-object tick / update function pointer** (called by dispatch loop via `MOVEA.L 16(A5),A0; JSR (A0)`).
- `+20`: Extra sprite attribute byte (patched into hardware sprite descriptor at `append_sprite`).
- `+36`: Object Z-distance / depth (used by Sub CPU counting sort) or bank angle.
- `+38`, `+40`: Velocity vectors (X, Y).
- `+42`: World 3D X coordinate.
- `+54`: Trajectory table / motion script pointer.
- `+60`, `+62`: Attachment offsets for multi-part objects.

### Core Object Types Mapped

1. **Player Aircraft (F-14 Tomcat) — Multi-Part Compound Object (Slots 0..3)**:
   - Initialized at `$016892` and activated together at `$0169B2` via `BCLR #7,(A0)`:
     - **Slot 0 (`$A000`)**: Airframe / fuselage (`update = $01653A`). Computes roll and flight stick banking at `$01678E` using the hardware multiplier (`$0E4000`), selects frame from `$00016B8A`, and calls `append_sprite(A5)`.
     - **Slot 1 (`$A040`)**: Left afterburner exhaust flame (`update = $01656A`, anim script `$01D780`).
     - **Slot 2 (`$A080`)**: Right afterburner exhaust flame (`update = $016572`, anim script `$01D780`, `BSET #5` horizontal flip).
     - **Slot 3 (`$A0C0`)**: Vulcan cannon / missile pod (`update = $01657A`).
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
