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

## RAM layout (deduced, not yet cross-checked against `sub_map`)

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
- The larger structures referenced with **full 32-bit absolute** addresses
  (`$0029C0xx`, `$002A3Cxx`) are further into the *un-mirrored* backup1/2
  ranges themselves (`$080000-$083FFF` / `$0A0000-$0A3FFF`, both only
  16KB) — e.g. `$29C0D6` is `backup1`'s real address (not its high mirror),
  `$2A3C00`-ish is `backup2`'s. Not yet clear why the code mixes short
  (mirrored, `$8000+`) and full 32-bit (unmirrored, `$0029xxxx`/`$002Axxxx`)
  forms for what appears to be the same underlying 16KB regions — possibly
  just compiler/assembler register-relative-vs-absolute choices, possibly
  a real distinction (e.g. different halves of each chip). Worth resolving
  before relying on address arithmetic between the two forms.

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

## Open threads to pick up next

- **Engine C++ Implementation**: We now have complete specifications for the object dispatch loop, 64-byte object structure, player jet composite parts, sprite pipeline, and road scanlines. Ready to scaffold `src/main/engine/` (`oentry.hpp`, `oplayer.cpp`, `osprites.cpp`).
- Find what sets `$0CDD07`'s trigger bit (the palette fade effect's
  activation) and what reads/sets `$8C2A`/`$8D30` (the palette-animation
  frame counter and palette-set selector).
- **RESOLVED**: Confirm VBLANK subsystem hooks: `$016C8A` is missile reload / HUD sequencer; `$018444` is stage event / background scenery scroll engine.
- **RESOLVED**: Map object dispatch table and structure: 254 entries × 64 bytes (`$9FC0`–`$DF40`), active player jet compound slots 0..3, 3D enemy world-to-screen pipeline mapped.
- **RESOLVED**: Sprite pipeline & Road / Ground-scroll generation mapped end-to-end.
- **RESOLVED**: Main ↔ Sub boot handshake and mutex mechanisms.
