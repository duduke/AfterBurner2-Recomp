#!/usr/bin/env python3
"""Generate the "Carrier Recovery" example stage: an interactive carrier landing.

It reuses the arcade's airbase landing (stage 5) at sea. Palette patches turn the
ground, sky and horizon into open ocean and paint the arcade runway the color of
the sea, so it disappears; the frontend then draws the carrier from this pack's
art in perspective (deck, wires, wake and island) while you fly the approach
under carrier rules: touch down short of the stern and it is a ramp strike, in
the wires a trap, past them a bolter.

Everything is drawn here: the pack contains no ROM data and no art from
elsewhere. Standard library only.

    python3 tools/assets/make_carrier_landing_pack.py
"""
import argparse
import json
import math
import random
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / 'assets/custom_stages/packs/carrier-recovery'

SWELL = '1'
SWELL_OBJECT = '0x13'          # The arcade's sea swell.
GRID_TICKS = 992               # About how long one 16x16 pattern lasts.

SKY = [150, 200, 240]
HORIZON = [176, 214, 240]
SEA, SEA_BAND = [20, 64, 150], [24, 70, 158]

# Deck geometry must match Rearm::Controller (lateral units): the texture spans
# 2 * 1.25 * half-width across and the full deck length from stern (bottom) to bow.
DECK_LENGTH, DECK_HALF_WIDTH = 320.0, 28.0
WIRE_FIRST, WIRE_SPACING = 34.0, 8.0
TEX_W, TEX_H = 192, 960
CLEAR = (0, 0, 0, 0)


def write_png(path, width, height, pixels):
    def chunk(tag, data):
        body = tag + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body))
    flat = bytes(max(0, min(255, int(c))) for p in pixels for c in p)
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw += flat[y * width * 4:(y + 1) * width * 4]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(bytes(raw), 9)) + chunk(b'IEND', b''))


def deck_plan():
    """Top-down flight deck, stern at the bottom. Lateral units map across the width."""
    span = 2 * 1.25 * DECK_HALF_WIDTH
    px = [CLEAR] * (TEX_W * TEX_H)
    rng = random.Random(77)

    def to_x(units):
        return (units + span / 2) / span * TEX_W

    def to_y(along):
        return (1 - along / DECK_LENGTH) * TEX_H

    def inside(ux, along):
        hw = DECK_HALF_WIDTH
        if along > DECK_LENGTH * 0.86:                     # tapered bow
            hw *= 1 - (along - DECK_LENGTH * 0.86) / (DECK_LENGTH * 0.14) * 0.55
        if -hw <= ux <= hw:
            return True
        # Angled landing deck overhang to port, over the stern half.
        if along < DECK_LENGTH * 0.5 and -hw * 1.24 <= ux < -hw:
            return True
        # Starboard sponson where the island stands.
        return DECK_LENGTH * 0.44 < along < DECK_LENGTH * 0.62 and hw < ux <= hw * 1.12

    for y in range(TEX_H):
        along = (1 - (y + 0.5) / TEX_H) * DECK_LENGTH
        for x in range(TEX_W):
            ux = (x + 0.5) / TEX_W * span - span / 2
            if not inside(ux, along):
                continue
            plate = 62 + (int(along / 6) % 2) * 3 + rng.randrange(4)
            px[y * TEX_W + x] = (plate, plate + 4, plate + 12, 255)

    def paint(ux0, ux1, a0, a1, color):
        for y in range(int(to_y(a1)), int(to_y(a0)) + 1):
            for x in range(int(to_x(ux0)), int(to_x(ux1)) + 1):
                if 0 <= x < TEX_W and 0 <= y < TEX_H and px[y * TEX_W + x][3]:
                    px[y * TEX_W + x] = color

    white, yellow, red = (232, 234, 236, 255), (240, 200, 40, 255), (220, 60, 50, 255)
    # Deck edge lines.
    for side in (-1, 1):
        x = side * DECK_HALF_WIDTH * 0.96
        for a in range(0, int(DECK_LENGTH * 0.86), 10):
            paint(x - 0.4, x + 0.4, a, a + 6, white)
    # Landing area: dashed yellow centerline and white edge lines from the stern.
    for a in range(2, int(DECK_LENGTH * 0.45), 9):
        paint(-0.7, 0.7, a, a + 5, yellow)
    for side in (-1, 1):
        paint(side * 13 - 0.4, side * 13 + 0.4, 0, DECK_LENGTH * 0.45, white)
    # Ramp stripes at the stern edge.
    for k in range(-14, 14, 2):
        paint(k * 2.0, k * 2.0 + 2.0, 0, 3, white if k % 4 else red)
    # Arresting wires: four cables across the landing area.
    for i in range(4):
        a = WIRE_FIRST + i * WIRE_SPACING
        paint(-14, 14, a - 0.18, a + 0.18, (150, 150, 140, 255))
        for side in (-1, 1):
            paint(side * 15, side * 15 + side * 1.2, a - 1, a + 1, (60, 60, 64, 255))
    # Foul line in red and white.
    for k in range(0, 14):
        paint(-DECK_HALF_WIDTH * 0.95 + k * 4, -DECK_HALF_WIDTH * 0.95 + k * 4 + 2, DECK_LENGTH * 0.45, DECK_LENGTH * 0.45 + 1.2,
              red if k % 2 else white)
    # Elevators outlined on the deck edges.
    for a0, side in ((DECK_LENGTH * 0.30, 1), (DECK_LENGTH * 0.68, 1), (DECK_LENGTH * 0.58, -1)):
        x0 = side * DECK_HALF_WIDTH * 0.62
        x1 = side * DECK_HALF_WIDTH * 0.95
        lo, hi = min(x0, x1), max(x0, x1)
        paint(lo, hi, a0, a0 + 0.7, yellow)
        paint(lo, hi, a0 + 22, a0 + 22.7, yellow)
        paint(lo, lo + 0.6, a0, a0 + 22.7, yellow)
    # Bow catapult tracks.
    for x in (-8.0, 8.0):
        paint(x - 0.5, x + 0.5, DECK_LENGTH * 0.62, DECK_LENGTH * 0.97, (150, 150, 150, 255))
        for a in range(int(DECK_LENGTH * 0.62), int(DECK_LENGTH * 0.95), 14):
            paint(x - 2.5, x + 2.5, a, a + 0.6, yellow)
    # Hull number on the bow.
    digits = {'7': '111 001 010 010 010'}
    for i, ch in enumerate('77'):
        for gy, row in enumerate(digits[ch].split()):
            for gx, cell in enumerate(row):
                if cell == '1':
                    ux = -9 + i * 10 + gx * 2.6
                    a = DECK_LENGTH * 0.93 - gy * 3.2
                    paint(ux, ux + 2.6, a - 3.2, a, white)
    return TEX_W, TEX_H, px


def island(w=96, h=176):
    """The island from astern: mast and radar, bridge windows, funnel, hull number."""
    px = [CLEAR] * (w * h)

    def rect(x0, y0, x1, y1, c):
        for y in range(max(0, int(y0)), min(h, int(y1))):
            for x in range(max(0, int(x0)), min(w, int(x1))):
                px[y * w + x] = c

    steel, dark, light = (128, 134, 146, 255), (86, 90, 102, 255), (168, 174, 186, 255)
    glass, mast = (36, 66, 92, 255), (64, 68, 78, 255)
    rect(44, 0, 50, 64, mast)
    rect(30, 12, 64, 15, mast)
    rect(34, 28, 60, 30, mast)
    rect(30, 34, 64, 40, light)
    rect(44, 40, 50, 46, dark)
    rect(54, 50, 74, 74, dark)           # funnel
    rect(22, 60, 70, 84, steel)          # upper bridge
    rect(66, 60, 70, 84, dark)
    for x in range(24, 66, 6):
        rect(x, 66, x + 4, 72, glass)
    rect(14, 84, 80, 124, steel)         # main block
    rect(74, 84, 80, 124, dark)
    for x in range(16, 74, 6):
        rect(x, 92, x + 4, 98, glass)
    rect(10, 124, 86, h, steel)
    rect(80, 124, 86, h, dark)
    digits = {'7': '111 001 010 010 010'}
    for i, ch in enumerate('77'):
        for gy, row in enumerate(digits[ch].split()):
            for gx, cell in enumerate(row):
                if cell == '1':
                    rect(26 + i * 24 + gx * 6, 132 + gy * 6, 32 + i * 24 + gx * 6, 138 + gy * 6, (236, 236, 236, 255))
    rect(18, 166, 28, h, dark)
    rect(66, 166, 74, h, dark)
    return w, h, px


def swell_pattern(rng, tick):
    """Scattered swell far either side; the ship's lane stays clear."""
    rows = []
    for _ in range(16):
        row = ['.'] * 16
        for column in (0, 1, 14, 15):
            if rng.random() < 0.08:
                row[column] = SWELL
        rows.append(''.join(row))
    return {'tick': tick, 'rows': rows, 'objects': {SWELL: SWELL_OBJECT}}


def stage(rng):
    return {
        '$schema': '../../stage.schema.json',
        'version': 1,
        'name': 'CARRIER RECOVERY',
        # The landing is flown through Interactive Rearm; 'combat' keeps the older
        # prototype controller and its guidance overlay out of the way.
        'objective': 'combat',
        'presentation': {'hud': False},
        'slot': 5,
        'source_stage': 5,
        'carrier_landing': True,
        'carrier_art': {'deck': 'art/carrier/deck.png', 'island': 'art/carrier/island.png'},
        'replace_enemy_waves': True,
        'enemy_waves': [],
        'scenery_patterns': [swell_pattern(rng, 400 + k * GRID_TICKS) for k in range(8)],
        # Descriptor blocks: primary is sky, sky, ground, ground; alternate repeats
        # them and then holds the eight runway colors (4..11), painted as sea here
        # so the arcade runway disappears under the drawn carrier.
        'palette_patches': [
            {'table': 'primary', 'words': [0, 1], 'color': SKY},
            {'table': 'primary', 'words': [2], 'color': SEA},
            {'table': 'primary', 'words': [3], 'color': SEA_BAND},
            {'table': 'alternate', 'words': [0, 1], 'color': SKY},
            {'table': 'alternate', 'words': [2, 4, 6, 8, 10], 'color': SEA},
            {'table': 'alternate', 'words': [3, 5, 7, 9, 11], 'color': SEA_BAND},
            {'table': 'gradient', 'words': list(range(1, 15)) + list(range(17, 31)), 'color': HORIZON},
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    out = args.output
    write_png(out / 'art/carrier/deck.png', *deck_plan())
    write_png(out / 'art/carrier/island.png', *island())
    (out / 'stage.json').write_text(json.dumps(stage(random.Random(41)), indent=2) + '\n', encoding='utf-8')
    print(f'Wrote {out}')


if __name__ == '__main__':
    main()
