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
(`$55555555`) at offset `$70`** before proceeding — the sub CPU's own
version of the main CPU's NVRAM-signature check, but implemented as a
*wait for the main CPU to write it* rather than a self-contained check.
Strongly suggests **the main CPU writes this signature into shared RAM
once its own init is done, and the sub CPU's reset routine blocks until
that handshake completes** — i.e. main CPU boots first and the sub CPU
waits on it, not the other way around. Worth confirming from the main
CPU's side (grep its code for a write of `$55555555`).

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

### VBLANK handler (`$55E`)

1. Saves all registers, sets `A6` to the shared-RAM base again.
2. **Spin-waits for bit 0 of `subram0` offset 0 to clear.** The main
   CPU's own VBLANK handler sets that exact bit at its very start
   (`BSET #0,$29C0D6` — `$29C0D6` is another mirror of `subram0`, real
   offset `$D6`... **correction while writing this up**: offset-0 and
   offset-`$D6` are different bytes, so this specific bit is not
   obviously the same one main sets — needs a closer look before treating
   this as confirmed cross-CPU handshake, but the *shape* (sub spins on a
   flag near the top of its VBLANK, main sets/clears a flag at the
   top/bottom of its own) strongly suggests some kind of "don't let sub
   read shared state mid-update" mutex either way.
3. A small countdown/backoff mechanism (byte at offset `$49`), then a
   fixed-count delay loop (`DBF` from `$39` — **the exact same constant
   the main CPU's VBLANK handler uses** for its own delay loop; another
   sign of shared low-level conventions between the two programs).
4. The main body (from `$640`): reads a count `D7` and a "current" value
   `D6` from two pointer-variables in shared RAM, and if in range
   (0 < D7 < 255), runs what reads as a **counting sort**: for each of
   up to 256 source entries (a byte value 0-255 read via a pointer that
   advances by 16 bytes per entry — i.e. a 16-byte-stride source table,
   plausibly per-object distance/depth), bucket it into a 32-byte-stride
   array of small linked lists by that byte value, then walk the buckets
   in order (near-to-far or far-to-near) to produce a **depth-sorted
   index list**, finally using each sorted entry to index into a region
   nudged by the hardware multiplier chip's base (`$0E4000`) and store a
   value into a 4th pointer-variable's target (`+14` field of a struct).
   Reads exactly like "sort game objects/road segments by distance, then
   drive the scaling-math hardware for each one in sorted order" — the
   single strongest piece of evidence yet for "sub CPU = road/sprite-scale
   perspective coprocessor," though the exact data (road segments? sprite
   Z-order? both?) isn't pinned down yet.

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

## Open threads to pick up next

- Examine the object/task dispatch table at `$9FC0` in detail — pick one
  active entry, follow its function pointer, and see if it matches a
  recognizable per-object update pattern (position, velocity, sprite RAM
  write) — this is the highest-value next target since it's the likely
  entry point for individual game object logic. **Still open** — the
  actual road/ground-scroll and sprite-object update subsystems haven't
  been located yet either, now that `$014A86`/`$013F92` turned out to be
  palette animation instead.
- Find what sets `$0CDD07`'s trigger bit (the palette fade effect's
  activation) and what reads/sets `$8C2A`/`$8D30` (the palette-animation
  frame counter and palette-set selector) — would confirm which in-game
  visual events these two subsystems are actually tied to.
- Confirm or refute the "4x double-buffer swap = tilemap scroll registers"
  hypothesis in step 3 above by tracing where `$2A3C04/0C/14/1C` are
  written from and read into.
- Confirm the main↔sub NVRAM-signature handshake: search the main CPU's
  code for a write of `$55555555` (the value the sub CPU's reset routine
  spin-waits for at `subram0` offset `$70`).
- Pin down whether main CPU's `$29C0D6` (bit 0) and the sub CPU's own
  `subram0` offset-0 bit 0 (which its VBLANK handler spins on) are really
  the same shared flag — they're at different real offsets (`$D6` vs `$00`)
  within the same chip, so this needs tracing rather than assuming.
- Follow the sub CPU's VBLANK counting-sort routine (`$640`) further —
  figure out what the 16-byte-stride source table and the "sorted index
  drives the multiplier chip" step actually operate on (road segments?
  sprite depth order? both?).
