#!/usr/bin/env python3
"""Reconstruct a flat, byte-addressable CPU program image from the
interleaved ROM chip dumps in roms/, for feeding to m68k_disasm.py (or any
other offline analysis tool). This mirrors RomLoader's INTERLEAVE2 logic
(src/main/romloader.cpp) but standalone, with no build step required.

Usage: build_rom_image.py {maincpu|subcpu} <output_file>
"""
import sys
import pathlib

ROMS_DIR = pathlib.Path(__file__).resolve().parent.parent / 'roms'

# (even-byte file, odd-byte file) — even file supplies each 16-bit word's
# high byte, matching MAME's ROM_LOAD16_BYTE offset 0x00000/0x00001
# convention (see docs/HARDWARE.md section 6).
IMAGES = {
    'maincpu': ('epr-11107.58', 'epr-11108.63'),
    'subcpu': ('epr-11109.20', 'epr-11110.29'),
}


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in IMAGES:
        print(f'Usage: {sys.argv[0]} {{{"|".join(IMAGES)}}} <output_file>', file=sys.stderr)
        return 1

    hi_name, lo_name = IMAGES[sys.argv[1]]
    hi = (ROMS_DIR / hi_name).read_bytes()
    lo = (ROMS_DIR / lo_name).read_bytes()
    if len(hi) != len(lo):
        print(f'{hi_name} and {lo_name} are different lengths', file=sys.stderr)
        return 1

    out = bytearray(len(hi) * 2)
    out[0::2] = hi
    out[1::2] = lo

    pathlib.Path(sys.argv[2]).write_bytes(out)
    print(f'wrote {len(out)} bytes to {sys.argv[2]}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
