#!/usr/bin/env python3
"""Generate the "Carrier Recovery" example stage: an interactive carrier landing.

It reuses the arcade's airbase landing (stage 5) and dresses it as a carrier at sea:
palette patches turn the ground into ocean, the sky and horizon into open sea air
and the runway into a gray deck with yellow edge lines, and scenery patterns
replace the trees with scattered swell. With Interactive Rearm on, the landing is
flown by the player under carrier rules (tight deck, wire window, bolters).

The pack holds only a stage file: no ROM data and no art from elsewhere.

    python3 tools/assets/make_carrier_landing_pack.py
"""
import argparse
import json
import random
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / 'assets/custom_stages/packs/carrier-recovery'

SWELL = '1'
SWELL_OBJECT = '0x13'          # The arcade's sea swell.
GRID_TICKS = 992               # About how long one 16x16 pattern lasts.

SKY = [150, 200, 240]
HORIZON = [176, 214, 240]
SEA, SEA_BAND = [20, 64, 150], [26, 76, 166]
DECK_DARK, DECK_LIGHT = [96, 100, 108], [108, 112, 120]
DECK_PAINT, DECK_EDGE = [240, 240, 240], [236, 200, 40]


def swell_pattern(rng, tick):
    """Scattered swell either side of the deck; the deck lane stays clear."""
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
        'replace_enemy_waves': True,
        'enemy_waves': [],
        'scenery_patterns': [swell_pattern(rng, 400 + k * GRID_TICKS) for k in range(8)],
        # Descriptor blocks: primary is sky, sky, ground, ground; alternate repeats
        # them and then holds the eight runway colors (4..11). The gradient is the
        # thin horizon strip.
        'palette_patches': [
            {'table': 'primary', 'words': [0, 1], 'color': SKY},
            {'table': 'primary', 'words': [2, 3], 'color': SEA},
            {'table': 'alternate', 'words': [0, 1], 'color': SKY},
            {'table': 'alternate', 'words': [2], 'color': SEA},
            {'table': 'alternate', 'words': [3], 'color': SEA_BAND},
            {'table': 'alternate', 'words': [4, 6, 10], 'color': DECK_DARK},
            {'table': 'alternate', 'words': [5, 9, 11], 'color': DECK_LIGHT},
            {'table': 'alternate', 'words': [7], 'color': DECK_PAINT},
            {'table': 'alternate', 'words': [8], 'color': DECK_EDGE},
            {'table': 'gradient', 'words': list(range(1, 15)) + list(range(17, 31)), 'color': HORIZON},
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'stage.json').write_text(json.dumps(stage(random.Random(41)), indent=2) + '\n', encoding='utf-8')
    print(f'Wrote {args.output}')


if __name__ == '__main__':
    main()
