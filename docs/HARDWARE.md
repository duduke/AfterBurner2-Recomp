# Hardware reference — Sega X-Board / After Burner II

Distilled from MAME's `src/mame/sega/segaxbd.cpp` (4911 lines) and
`segaxbd.h`, which is a shared driver covering the whole X-Board game
family (After Burner, After Burner II, Thunder Blade, Line of Fire, Super
Monaco GP, GP Rider, AB Cop, Racing Hero, Royal Ascot). This document keeps
only what's relevant to `aburner2` and notes where behavior is inherited
from the shared base config. Line numbers refer to `segaxbd.cpp` unless a
`.h`/other file is named.

Of the whole X-Board family, **aburner2 exercises the smallest slice of
this driver's complexity**: no MCU, no FD1094 program encryption, no
second sound CPU, no dual-PCB "double" setup, no network/motor board — all
of those exist for other games in the family (`segaxbd_state` is the
shared base device; games bolt on extras via subclasses in `segaxbd.h`
151-243). That makes it a good first native-port target.

## 1. CPU complement

| CPU | Type | Clock | Role |
|---|---|---|---|
| Main CPU | M68000 | 12.5 MHz (`MASTER_CLOCK/4`, 50MHz/4) | Game logic, inputs/ADC, sprite RAM, palette RAM, tile/text RAM |
| Sub CPU | M68000 | 12.5 MHz | Shares work with main; owns/updates road (ground/sky) control RAM |
| Sound CPU | Z80 | 4 MHz (`SOUND_CLOCK/4`, 16MHz/4) | Drives YM2151 + SegaPCM |

No MCU (i8751), no 2nd sound Z80, no comm board for aburner2 — those are
other X-Board games only.

**Inter-CPU communication:**
- Two shared dual-port RAM windows visible to both 68000s: `subram0`
  (main `0x280000-0x283FFF` / sub `0x080000-0x083FFF`) and `subram1`
  (main `0x2A0000-0x2A3FFF` / sub `0x0A0000-0x0A3FFF`).
- Shared road/ground control RAM (`0x2EC000` main / `0x0EC000` sub) — this
  is how the sub CPU's per-scanline road table reaches the renderer that
  runs off the main CPU's `screen_update`.
- A `sega_315_5250` compare/timer chip drives IRQ2 on the main CPU and NMI
  on the sound Z80. A per-scanline timer fires VBLANK (level 4) on **both**
  68000s independently near scanline 223/224; only the main CPU also gets
  the timer IRQ2.
- Resetting the main 68000 also resets the sub CPU.
- Two SEGA 315-5248 hardware multipliers and two 315-5249 hardware
  dividers (one pair per CPU) exist for the scaling/perspective math the
  sprite scaler leans on heavily — real hardware offloaded this; a native
  port can just do the math in software.
- Sound "command" delivery to the Z80 rides on the compare/timer chip's
  Z80-side port (`sound_portmap` `0x40`), not a discrete soundlatch device
  — the exact command-byte protocol lives in `segaic16_m.cpp`
  (`sega_315_5250_compare_timer_device`), not read yet.

## 2. Main CPU memory map (`segaxbd_state::main_map`)

22-bit address bus (`global_mask(0x3fffff)`), unmapped reads return high
byte value.

| Range | Target |
|---|---|
| `000000-07FFFF` | Program ROM |
| `080000-083FFF` (mirror `0x1C000`) | `backup1` battery-backed work RAM (NVRAM) |
| `0A0000-0A3FFF` (mirror `0x1C000`) | `backup2` battery-backed work RAM (NVRAM) |
| `0C0000-0CFFFF` | Tile RAM (`segaic16vid`) |
| `0D0000-0D0FFF` (mirror `0xF000`) | Text RAM (page/scroll registers, `segaic16vid`) |
| `0E0000-0E0007` (mirror `0x3FF8`) | Main-side hardware multiplier (315-5248) |
| `0E4000-0E401F` (mirror `0x3FE0`) | Main-side hardware divider (315-5249) |
| `0E8000-0E801F` (mirror `0x3FE0`) | Main-side compare/timer chip (315-5250) |
| `100000-100FFF` (mirror `0xF000`) | Sprite RAM (512 entries × 8 words) |
| `110000-11FFFF` | Sprite draw trigger (write starts the scanline-scaler draw) |
| `120000-123FFF` (mirror `0xC000`) | Palette RAM |
| `130001` (mirror `0xFFFE`) | ADC0804 analog input converter |
| `140000-14000F` (mirror `0xFFF0`) | I/O chip 0 (CXD1095), low byte |
| `150000-15000F` (mirror `0xFFF0`) | I/O chip 1 (CXD1095), low byte |
| `160000-16FFFF` | I/O "force input" control latch |
| `200000-27FFFF` | Sub CPU program ROM, read-only mirror into main space |
| `280000-283FFF` (mirror `0x1C000`) | `subram0`, shared with sub CPU |
| `2A0000-2A3FFF` (mirror `0x1C000`) | `subram1`, shared with sub CPU |
| `2E0000-2E0007` (mirror `0x3FF8`) | Sub-side hardware multiplier |
| `2E4000-2E401F` (mirror `0x3FE0`) | Sub-side hardware divider |
| `2E8000-2E800F` (mirror `0x3FF0`) | Sub-side compare/timer chip |
| `2EC000-2ECFFF` (mirror `0x1000`) | Road/ground control RAM (`segaic16road`), shared with sub CPU |
| `2EE000-2EFFFF` | Road control register (bank-swap trigger, see §3.2) |
| `3F8000-3FBFFF` | Mirror of `backup1` |
| `3FC000-3FFFFF` | Mirror of `backup2` |

### Sub CPU memory map (`segaxbd_state::sub_map`)

**Notably narrower than the main map**: 20-bit address bus
(`global_mask(0xfffff)`, a 1MB space, vs. the main CPU's 22-bit/4MB), and
**no access at all** to tilemap RAM, text RAM, sprite RAM, palette RAM, the
ADC, or either I/O chip — every one of those is exclusively main-CPU
territory. What the sub CPU *does* have:

| Range | Target |
|---|---|
| `000000-07FFFF` | Sub CPU program ROM |
| `080000-083FFF` (mirror `0x1C000`) | `subram0` — shared with main CPU (main sees the same chip at `280000-283FFF`) |
| `0A0000-0A3FFF` (mirror `0x1C000`) | `subram1` — shared with main CPU (main: `2A0000-2A3FFF`) |
| `0E0000-0E0007` (mirror `0x3FF8`) | Sub's own hardware multiplier (315-5248) — same chip main can also reach at `2E0000` |
| `0E4000-0E401F` (mirror `0x3FE0`) | Sub's own hardware divider (315-5249) — main: `2E4000` |
| `0E8000-0E800F` (mirror `0x3FF0`) | Sub's own compare/timer chip (315-5250, a **separate physical instance** from the main CPU's own `cmptimer_main`) — main: `2E8000` |
| `0EC000-0ECFFF` (mirror `0x1000`) | Road/ground control RAM — shared with main (main: `2EC000`) |
| `0EE000-0EFFFF` | Road control register (bank-swap trigger) — main: `2EE000` |

Both CPUs have an identical relative layout for their *own* multiplier/
divider/compare-timer chips, and can each additionally peek at the
*other* CPU's instances via a fixed `+0x200000` offset from the sub CPU's
own addresses (i.e. main sees sub's chips at `sub_addr + 0x200000`).

**This significantly narrows the sub CPU's likely role**: with no path to
any rendering hardware except the road controller, and its own dedicated
multiplier/divider, it reads as a math coprocessor dedicated to the
road/ground perspective-scaling calculation (and/or precomputing values
the main CPU's sprite-scaler math needs) — not a general "second half of
the game logic" CPU. Communication with main CPU is only through
`subram0`/`subram1` and whatever handshake convention the game uses
within them (not yet identified).

## 3. Video hardware

### 3.1 Sprites — hardware scaling sprite engine

Device: `sega_xboard_sprite_device` (a variant of the OutRun sprite
device, `sega16sp.h`/`.cpp`). This is a genuine **hardware line-buffer
scaler**: for every scanline it walks each active sprite RAM entry and
reads pixel data directly out of the sprite ROM with a Bresenham-style
zoom accumulator. Scaling happens at draw time from ROM data — there are
no pre-scaled tile sets.

Sprite RAM: **256 entries**, 8 words (16 bytes) each (0x1000 bytes total —
corrected from an earlier draft of this doc, which miscalculated 512 from
the byte range without checking the real entry stride; verified directly
against `sega16sp.cpp`'s `data += 8` per-entry loop), at main-map
`100000-100FFF`. A write to `110000-11FFFF` triggers the draw.

Per-entry format (X-Board variant fields called out where they differ
from the OutRun-only packing):

```
+0   e------- --------  End-of-list marker
+0   -h-h---- --------  Hide flags
+0   ----bbb- --------  Sprite ROM bank (0-7)
+0   -------t tttttttt  Top scanline + 256
+2   oooooooo oooooooo  Start offset within selected ROM bank
+4   ppppppp- --------  Signed pitch (per-scanline horizontal shift)
+4   -------x xxxxxxxx  X position
+6   -s------ --------  Shadow enable
+6   --pp---- --------  Priority vs. tilemaps (2 bits)
+6   ------vv vvvvvvvv  Vertical zoom (0x200 = 1x, 0x100 = 0.5x, 0x300 = 2x)
+8   y------- --------  Draw top-to-bottom vs bottom-to-top
+8   -f------ --------  Horizontal flip
+8   --x----- --------  Draw left-to-right vs right-to-left
+8   ------hh hhhhhhhh  Horizontal zoom
+A   ----hhhh hhhhhhhh  Height in scanlines - 1        (X-Board packing)
+C   -------- cccccccc  Color palette                  (X-Board packing)
+E   dddddddd dddddddd  Scratch: current ROM read address
```

Sprite graphics ROM region is 32-bit-wide (`ROM_REGION32_LE`), loaded as
4 interleaved byte-planes per 0x20000 group (see §6 for exact files).

### 3.2 Background — road/ground+sky generator

Device: `segaic16_road_device`, mode `ROAD_XBOARD`, init params
`(colorbase1=0x1700, colorbase2=0x1720, colorbase3=0x1780, xoffset=-166)`.
`ROAD_XBOARD` shares its decode/draw routines with `ROAD_OUTRUN` — **this
is the exact mechanism Cannonball already reimplemented for OutRun's
road**, so its approach can largely be reused/adapted rather than
reverse-engineered from scratch.

- Road gfx ROM (`epr-10922.40`) is unpacked at init into 2 bitplanes → a
  2-bit road-edge code per pixel, 512×512.
- Shared `roadram` holds, per scanline: two control/position words
  (offsets `0x000+y`, `0x100+y`), per-line horizontal-position tables
  (`0x200`, `0x400`), and a color-select table (`0x600`).
- Reading the road control register swaps "live" vs. "buffered" halves of
  road RAM — this double-buffering is the actual vblank-sync mechanism
  for the road layer (CPU builds next frame's table while current one
  draws).
- Writing the control register latches a 3-bit value selecting which of
  two independently-scrolling road strips is foreground/background per
  scanline — this is how curves/forks and the sky/ground split work.
- Draw order: solid background/sky fill first (where a road entry marks
  "no road here"), then the actual textured road-surface pass with
  per-line color/priority blend between the two strips.
- `segaxbd_state::screen_update` draws road-background, then tilemaps,
  then road-foreground either before or after the foreground tilemap
  depending on `m_road_priority`. **`install_aburner2()` explicitly sets
  `m_road_priority = 0`** — i.e. for After Burner II the road/ground
  foreground layer is drawn *before* tilemaps and sprites (low priority),
  which differs from this driver's default of 1. Get this backwards and
  the ground will incorrectly draw over HUD/sprites.

### 3.3 Tilemap / text layer

Device: `segaic16_video_device`, init as "System 16B"-style tilemaps
(`tilemap_init(0, TILEMAP_16B, 0x1c00, 0, 2)`: colorbase `0x1C00`,
`numbanks=2`). Bit 15 of a tile entry = priority-vs-sprite category (0/1).
Tile graphics are 8×8, 3-bitplane planar, region `gfx1`, split into 3 equal
contiguous thirds (one per bitplane); for tile N, row y (0-7): the byte at
`third_base + N*8 + y` supplies one bitplane of that row (8 pixels, MSB
first = leftmost). **Plane-to-bit assignment (verified directly against
`src/emu/drawgfx.cpp`, not just inferred from the gfx_layout declaration):**
first third → pixel bit 2 (MSB), middle third → bit 1, last third → bit 0
(LSB) — i.e. decreasing bit significance as you move earlier through the
ROM, which is easy to get backwards. Pixel value 0 is transparent; final
palette index = `0x1c00 + color*8 + pixel`.

**Tile RAM** (`0C0000-0CFFFF`, 0x10000 bytes): 16 independent pages of
64×32 tiles each (0x1000 bytes/page). Tile entry: `color = (data>>6)&0x7f`,
raw `code = data&0x1fff`, bank-remapped via `bank[code/banksize]` — but
**no X-Board game ever calls the bank-remap setter**, so for After Burner
II this is always the identity mapping; `code` can be used directly.

**Text RAM** (`0D0000`, mirrored every `0x1000`): a fixed (non-paged)
64×28 text tilemap at word offsets `0x000-0x37F` (`color=(data>>9)&7`,
`code=data&0x1ff`, always through tile bank 0), plus registers from
`0xE80`:

| Word offset (÷2 of byte offset) | FG | BG | ALT-FG | ALT-BG |
|---|---|---|---|---|
| page-select | `0x740` | `0x741` | `0x742` | `0x743` |
| y-scroll (9-bit) | `0x748` | `0x749` | `0x74A` | `0x74B` |
| x-scroll (10-bit) | `0x74C` | `0x74D` | `0x74E` | `0x74F` |

Each *layer* (FG, BG) presents a scrolling **1024×512px virtual surface**
built from up to 4 of the 16 physical pages in a 2×2 arrangement (so
scrolling wraps seamlessly across page boundaries) — the page-select word's
4 nibbles give the page index for each fixed quadrant: nibble 0 = quadrant
(col0,row0) "upper-left", nibble 1 = (col1,row0) "upper-right", nibble 2 =
(col0,row1) "lower-left", nibble 3 = (col1,row1) "lower-right" of that
virtual surface — *not* 4 nibbles for "whichever quadrant is currently on
screen"; which physical page ends up visually on the left/right or top/
bottom depends on the separate x/y scroll value. `y-scroll`/`x-scroll`
bit 15 is a column-scroll/row-scroll *enable* flag; when set, per-16px-
column (`0xF16-0xF3F` FG, `0xF56-0xF7F` BG) or per-8px-row (`0xF80-0xFB7`
FG, `0xFC0-0xFF7` BG, whose top bit also selects the ALT-FG/ALT-BG tilemap
for that 8px-tall band instead) fine-scroll tables apply instead of the
flat register value — used for raster effects. **Not yet implemented in
our port** (see `src/main/hwvideo/hwtiles.hpp`) since there's no ported
game logic yet to exercise/verify it against; only the flat per-layer
scroll is implemented so far.

The 6 registers at `0xE80-0xE9E` (page-select + flat scroll, for all 4 of
FG/BG/ALT-FG/ALT-BG) are **latched once per frame** (real hardware: at
scanline 261) rather than taking effect immediately on write — a
mid-frame write to one of those doesn't visibly change anything until the
next frame. The fine per-column/per-row tables, by contrast, are read live
at draw time and do support true per-scanline effects.

### Final screen compositing (`segaxbd_state::screen_update`)

Exact draw order for After Burner II (`m_road_priority=0`, forced by
`install_aburner2()`, overriding this driver's own default of 1):

1. road background
2. road foreground *(drawn here specifically because `m_road_priority==0`;
   with the default of 1 this step instead happens after step 6)*
3. BG tilemap, category 0 → OR priority-buffer mark `0x01`
4. BG tilemap, category 1 → OR mark `0x02`
5. FG tilemap, category 0 → OR mark `0x02`
6. FG tilemap, category 1 → OR mark `0x04`
7. text layer, category 0 → OR mark `0x04`
8. text layer, category 1 → OR mark `0x08`
9. sprites, composited against the accumulated priority buffer

The priority buffer accumulates via bitwise-OR (never overwritten) —
category marks from different layers deliberately share values (e.g. BG
cat1 and FG cat0 both mark `0x02`) to produce a single ladder:
`BG-normal(1) < {BG-priority, FG-normal}(2) < {FG-priority, text-normal}(4)
< text-priority(8)`. The road layers never touch the priority buffer at
all — they draw unconditionally, and simply get overdrawn by any later,
opaque (non-transparent-pen) layer.

Per sprite pixel (composited word from §3.1: 4-bit pixel, 8-bit color at
bits 4-11, 2-bit priority at bits 12-13, shadow-enable at bit 14):
```
priority = (pix >> 12) & 3
if (1 << priority) > accumulated_priority_at_this_pixel:
    if (pix & 0x400f) == 0x400a:      // shadow-enable set AND raw pixel == 0xa
        pixel_index += num_palette_entries   // resolve through the "effects" pen bank instead
    else:
        pixel_index = pix & 0xfff             // sprite's own color, drawn directly
# else: this sprite pixel is lower-priority than what's already there — leave it alone
```
Sprites need **no separate colorbase** — `pix & 0xfff` is already a
directly-usable master-palette pen index (0-4095), a different sub-range
of the same palette space tiles (`0x1c00+`) and road (`0x1700+`) occupy,
kept from colliding only by the game's own palette data layout, not by
hardware enforcement.

### 3.4 Palette

Palette RAM at `120000-123FFF`. Per-word format: `sBGR BBBB GGGG RRRR` —
bit 15 selects shadow/hilight, then 5 bits each of B/G/R via a weighted
resistor-ladder DAC (4 weighted bits + 1 tristate bit, not simple linear
5-bit color). Each palette RAM entry produces two actual color values: a
normal-brightness pen at `pen[offset]` and a shadow/hilight pen at
`pen[offset + num_entries]` (shadow if that entry's own bit 15 is clear,
hilight if set — this is a property of the *color*, decided once at write
time, not of whatever's drawing over it). Total 8192×2 palette entries.
The resistor weights (3.9k/2k/1k/1k÷2/1k÷4 ohms for the 5 real bits, plus
a 6th 470Ω resistor switched in only for the shadow/hilight pen) combine
via a simple conductance-ratio weighted sum in this no-pulldown/no-pullup
case — see `src/main/hwvideo/hwpalette.cpp` for the derivation.

### 3.4 Palette

Palette RAM at `120000-123FFF`. Per-word format: `sBGR BBBB GGGG RRRR` —
bit 15 selects shadow/hilight, then 5 bits each of B/G/R via a weighted
resistor-ladder DAC (4 weighted bits + 1 tristate bit, not simple linear
5-bit color). Each palette RAM entry produces two actual color values: a
normal-brightness pen and a shadow/hilight pen. Total 8192×2 palette
entries.

## 4. Sound hardware

- Z80 sound CPU memory map: `0000-EFFF` program ROM, `F000-F0FF`
  (mirrored ×8) SegaPCM chip, `F800-FFFF` work RAM.
- Z80 I/O ports: `00-01` (mirrored) → YM2151, `40` (mirrored) →
  compare/timer chip's Z80-side read port (NMI-acknowledge / sound
  command source).
- YM2151 @ 4 MHz; its IRQ feeds the Z80's IRQ0.
- SegaPCM @ 4 MHz, 512-byte bank mode, reads the `pcm` ROM region
  directly.
- Stereo output: YM2151 at 0.15 gain/side, PCM at 0.35 gain/side.
- Main CPU can hold the sound Z80 in reset via an I/O-chip-0 port C output
  bit; a separate port D output bit mutes/unmutes the whole sound system.
- No second sound CPU/PCM chip for aburner2 (that's Super Monaco GP's
  cabinet-specific hardware only).

## 5. Inputs / outputs (aburner2)

- **Digital**: service switch, service1, start1, coin1, coin2, plus two
  game buttons — `BUTTON1` "Vulcan" (cannon), `BUTTON2` "Missile".
- **Analog** (via ADC0804, 8 channels muxed through an I/O-chip port):
  - ADC0 — stick X
  - ADC1 — stick Y (reversed)
  - ADC2 — throttle lever
  - ADC3/ADC4 — "motor Y"/"motor X": fixed mid-value, feed the
    (unemulated) moving-cockpit motor, not a player control
- **Motor I/O**: aburner2-specific read/write handlers exist but are
  explicit TODO stubs in MAME — the moving-deluxe-cabinet motor board is
  not emulated upstream; read just forces bits high, write does nothing.
  No reference behavior to port here beyond that.
- **DIP switches**: coinage A/B, cabinet type (Upright 1/2, Moving
  Standard, Moving Deluxe), throttle lever present Y/N, lives
  (3/4/"3x credits"/"4x credits"), allow continue, difficulty
  (Easy/Normal/Hard/Hardest).
- **Lamps** (aburner2-specific bit assignment, documented inline in
  MAME): d1 altitude warning, d2 start, d5 lock-on, d6 danger; d4 = coin
  counter; d7 = audio mute (handled separately). The original 1987
  `aburner` (not II) has buggier lamp behavior — works "only in
  testmode" per a MAME comment — so don't use `aburner` as a lamp-timing
  reference, only `aburner2`.
- **Watchdog**: an MB3773 device is present, but a MAME comment notes
  aburner2 (like most games on this board) doesn't actually clear it via
  the documented port-C bit as the schematics imply — in the driver it's
  actually kept alive by the VBLANK-driven path instead. Easiest choice
  for a native port: don't emulate the watchdog at all.

## 6. `ROM_START( aburner2 )` — exact region map

Verified against the CRC32 of every file in the provided `aburner2.zip`.

### Main CPU program (`0x40000` populated of a `0x80000` region), word-interleaved

| File | Offset | Stride | Size | CRC32 |
|---|---|---|---|---|
| epr-11107.58 | 0x00000 | 2 (even/high byte) | 0x20000 | `6d87bab7` |
| epr-11108.63 | 0x00001 | 2 (odd/low byte) | 0x20000 | `202a3e1d` |

### Sub CPU program (`0x40000` populated of a `0x80000` region), word-interleaved

| File | Offset | Stride | Size | CRC32 |
|---|---|---|---|---|
| epr-11109.20 | 0x00000 | 2 | 0x20000 | `85a0fe07` |
| epr-11110.29 | 0x00001 | 2 | 0x20000 | `f3d6797c` |

### Tile/text graphics (`0x30000` total), sequential (not interleaved) — 3 bitplanes

| File | Offset | Size | CRC32 |
|---|---|---|---|
| epr-11115.154 | 0x00000 | 0x10000 | `e8e32921` |
| epr-11114.153 | 0x10000 | 0x10000 | `2e97f633` |
| epr-11113.152 | 0x20000 | 0x10000 | `36058c8c` |

### Sprite graphics (`0x200000` total, 32-bit LE), 4-way byte-interleaved per 0x20000 group

| Group offset | Byte 0 | Byte 1 | Byte 2 | Byte 3 |
|---|---|---|---|---|
| 0x000000 | mpr-10932.90 (`cc0821d6`) | mpr-10934.94 (`4a51b1fa`) | mpr-10936.98 (`ada70d64`) | mpr-10938.102 (`e7675baf`) |
| 0x080000 | mpr-10933.91 (`c8efb2c3`) | mpr-10935.95 (`c1e23521`) | mpr-10937.99 (`f0199658`) | mpr-10939.103 (`a0d49480`) |
| 0x100000 | epr-11103.92 (`bdd60da2`) | epr-11104.96 (`06a35fce`) | epr-11105.100 (`027b0689`) | epr-11106.104 (`9e1fec09`) |
| 0x180000 | epr-11116.93 (`49b4c1ba`) | epr-11117.97 (`821fbb71`) | epr-11118.101 (`8f38540b`) | epr-11119.105 (`d0343a8e`) |

Each file is 0x20000 bytes. Note the first two groups (`mpr-1093x`,
0x000000-0x0FFFFF) are shared verbatim with the original 1987 `aburner`
set; only the upper half (0x100000-0x1FFFFF) is After Burner II-specific.

### Road/ground graphics (`0x10000` total)

| File | Offset | Size | CRC32 |
|---|---|---|---|
| epr-10922.40 | 0x00000 | 0x10000 | `b49183d4` |

Shared verbatim with the original `aburner` set.

### Sound CPU program (`0x10000` total)

| File | Offset | Size | CRC32 |
|---|---|---|---|
| epr-11112.17 | 0x00000 | 0x10000 | `d777fc6d` |

### SegaPCM sample data (`0x60000` populated of a `0x80000` region, rest erased 0xFF)

| File | Offset | Size | CRC32 |
|---|---|---|---|
| mpr-10931.11 | 0x00000 | 0x20000 | `9209068f` |
| mpr-10930.12 | 0x20000 | 0x20000 | `6493368b` |
| epr-11102.13 | 0x40000 | 0x20000 | `6c07c78d` |

## 7. aburner2-specific quirks worth knowing

- `install_aburner2()` is a one-line driver-init hook: it only sets
  `m_road_priority = 0` (§3.2). Everything else about aburner2 comes from
  the shared X-Board base config — this is one of the "cleanest"
  (least game-specific-hackery) configs in the whole driver family.
- Watchdog behavior described in the driver doesn't match the schematics
  (§5) — safe to just not emulate it.
- Motor/moving-cabinet hardware is unemulated upstream in MAME too — no
  reference behavior exists to port for the Moving Deluxe cabinet motor.
- Real inter-CPU timing is tighter than a naive port might assume: MAME
  forces a 6000Hz minimum scheduler quantum and adds extra sync points
  around shared-RAM writes and IRQ delivery between the two 68000s. A
  native port won't have MAME's cooperative scheduler, so ordering of
  main/sub CPU steps needs deliberate attention wherever they touch
  shared RAM (`subram0`/`subram1`/road control RAM).

## Open questions to resolve before/while porting the relevant subsystem

- Exact sound "command" protocol between main CPU and Z80 rides through
  the `sega_315_5250_compare_timer_device` (`segaic16_m.h`/`.cpp`, not
  yet read) — needed before porting sound-trigger logic.
- ADC0804 conversion timing (`machine/adc0804.*`, not yet read) — only
  matters if analog stick/throttle timing needs to be cycle-accurate.
- CXD1095 I/O chip semantics (`machine/cxd1095.*`, not yet read) — needed
  for exact input-port bit mapping beyond what's summarized in §5.
- Sub CPU memory map (`segaxbd_state::sub_map`) hasn't been fully
  transcribed here yet — read it before starting the sub-CPU logic port.
