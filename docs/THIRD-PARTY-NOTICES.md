# Third-party code

Most of this project is original code written specifically for After
Burner II's X-Board hardware, following algorithms reimplemented from
MAME's driver (see each file's header comment and `docs/HARDWARE.md` for
citations — algorithm only, no MAME code copied, in each of those cases).

Two source files are a direct exception, carried over near-verbatim rather
than reimplemented, because they emulate generic Sega/Yamaha sound chips
that are the literal same hardware across many Sega arcade boards of this
era (OutRun, X-Board, Y-board, System 24, ...) — there is nothing
X-Board-specific to reimplement:

- `src/main/hwaudio/ym2151.cpp` / `.hpp` — Yamaha YM2151 FM synthesis chip
  emulator. Original author: Jarek Burczynski, 1997-2002, distributed as
  part of MAME (BSD-3-Clause). Carried into this project via Chris White's
  Cannonball, which itself adapted it from MAME with only minor
  integration changes.
- `src/main/hwaudio/segapcm.cpp` / `.hpp` — Sega 8-bit PCM sample-playback
  chip emulator. Also MAME-derived (BSD-3-Clause), carried via Cannonball.
- `src/main/hwaudio/soundchip.cpp` / `.hpp` — a small abstract base class
  (buffer management shared by the above two), original to Chris White's
  Cannonball, not MAME-derived.

See each file's own header comment for its specific attribution.
