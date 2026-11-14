#!/usr/bin/env python3
"""Generate a procedural custom stage pack from a theme and a seed.

Themes combine the stage format's three interfaces:
  mission     what the run asks of the player (destroy N, or survive the clock)
  conditions  art and behavior: night, searchlights, flak, rain, lightning,
              fog, turbulence, wind and animated sea
  formations  native squadrons laid out from the seed: V / line / echelon /
              diamond, entering from the front, from behind, from either side
              or diving in; naval targets for sea assaults
plus an entry maneuver and optional run mutators.

    night-raid      a darkened city, sweeping searchlights that call in flak
    naval-assault   sea-skimming strike on a surface group at low level
    urban-trench    a night run down a street canyon between towers
    storm-front     severe weather: rain, lightning, turbulence and fog

    python3 tools/assets/make_procedural_stage.py night-raid --seed 7
    python3 tools/assets/make_procedural_stage.py storm-front --seed 3 --mutators swarm,ace_pilots
    python3 tools/assets/make_procedural_stage.py all

All art is drawn here: packs contain no ROM data and nothing from other games.
Standard library only.
"""
import argparse
import json
import math
import random
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKS = ROOT / 'assets/custom_stages/packs'
THEMES = ('night-raid', 'naval-assault', 'urban-trench', 'storm-front')
MUTATORS = ('night', 'fog', 'storm', 'turbulence', 'flak_alley', 'ace_pilots', 'swarm', 'afterburner')
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


def render(w, h, shade, ss=2):
    px = []
    for y in range(h):
        for x in range(w):
            acc = [0.0] * 4
            for sy in range(ss):
                for sx in range(ss):
                    c = shade((x + (sx + 0.5) / ss) / w, (y + (sy + 0.5) / ss) / h)
                    a = c[3] / 255.0
                    acc[0] += c[0] * a; acc[1] += c[1] * a; acc[2] += c[2] * a; acc[3] += c[3]
            alpha = acc[3] / (ss * ss)
            k = 255.0 / max(1e-6, acc[3])
            px.append((acc[0] * k, acc[1] * k, acc[2] * k, alpha) if alpha > 0 else CLEAR)
    return w, h, px


# --------------------------------------------------------------------- art
def fighter(w=96, h=56):
    """An enemy fighter from astern: dark swept wings, twin tails, hot nozzles."""
    def shade(u, v):
        x, y = u * 2 - 1, v
        ax = abs(x)
        if 0.2 < y < 0.95 and ax < 0.11 - (y - 0.95) * 0.05:          # fuselage
            return (70 + 30 * (1 - y), 74 + 30 * (1 - y), 84 + 26 * (1 - y), 255)
        if 0.46 < y < 0.70 and ax < 0.95 - (y - 0.46) * 0.6 and ax > 0.08:  # wings
            t = (y - 0.46) / 0.24
            return (92 - 40 * t, 96 - 40 * t, 108 - 36 * t, 255) if ax < 0.88 else (200, 50, 44, 255)
        for side in (-1, 1):                                            # canted tails
            if 0.05 < y < 0.5 and abs(x - side * (0.16 + (0.5 - y) * 0.22)) < 0.035:
                return (60, 62, 72, 255)
        if 0.82 < y < 0.98:                                             # nozzles
            for side in (-1, 1):
                d = math.hypot((x - side * 0.06) / 0.06, (y - 0.9) / 0.07)
                if d < 1:
                    return (255, 150 + 100 * (1 - d), 60, 255)
        if 0.24 < y < 0.36 and ax < 0.05:                               # canopy
            return (60, 120, 160, 255)
        return CLEAR
    return render(w, h, shade)


def ship(w=160, h=72):
    """A destroyer from astern: gray hull, superstructure, guns, a funnel."""
    def shade(u, v):
        x, y = u * 2 - 1, v
        ax = abs(x)
        if 0.62 < y < 0.9 and ax < 0.9 - (y - 0.62) * 0.6:              # hull
            return (96 - 30 * (y - 0.62) * 3, 100 - 30 * (y - 0.62) * 3, 110 - 20 * (y - 0.62) * 3, 255) if y < 0.86 else (150, 40, 36, 255)
        if 0.40 < y <= 0.62 and ax < 0.42:                               # superstructure
            if 0.47 < y < 0.52 and int(u * 40) % 3:
                return (40, 60, 80, 255)
            return (130, 136, 148, 255)
        if 0.18 < y <= 0.40 and abs(x - 0.12) < 0.07:                    # funnel
            return (60, 62, 70, 255)
        if 0.05 < y <= 0.40 and abs(x + 0.10) < 0.015:                   # mast
            return (70, 72, 80, 255)
        for gx in (-0.62, 0.62):                                         # gun mounts
            if 0.52 < y <= 0.62 and abs(x - gx) < 0.1:
                return (110, 114, 124, 255)
        if 0.9 <= y < 0.97 and ax < 0.95:                                # bow wave
            return (220, 236, 246, 200)
        return CLEAR
    return render(w, h, shade)


def water(w=128, h=64):
    """Tileable swell in the arcade's sea palette."""
    deep, mid, light, cap = (18, 56, 158), (40, 96, 196), (110, 170, 236), (236, 246, 255)
    px = [deep] * (w * h)
    rng = random.Random(12)
    for band in range(10):
        y0 = band * h / 10 + rng.random() * 3
        k = rng.choice((1, 2, 3))
        phase = rng.random() * 6.283
        for x in range(w):
            if math.sin(2 * math.pi * x / w * (k + 2) + phase * 2) < -0.35:
                continue
            y = y0 + 2.2 * math.sin(2 * math.pi * x / w * k + phase)
            for dy, c in ((0, cap), (1, light), (2, mid), (-1, mid)):
                yy = int(math.floor(y + dy)) % h
                if c is cap and math.sin(2 * math.pi * x / w * 5 + phase) < 0.55:
                    c = light
                px[yy * w + x] = c
    return w, h, [(r, g, b, 255) for r, g, b in px]


def facade(w=88, h=178, seed=1):
    """A tower front at night: concrete, rows of lit and dark windows, a roof light."""
    rng = random.Random(seed)
    lit = [[rng.random() < 0.35 for _ in range(6)] for _ in range(24)]
    def shade(u, v):
        row, col = int(v * 24), int(u * 6)
        if v < 0.03:
            return (255, 60, 50, 255) if abs(u - 0.5) < 0.06 else (50, 52, 60, 255)
        wx, wy = (u * 6) % 1, (v * 24) % 1
        if 0.2 < wx < 0.8 and 0.25 < wy < 0.75:
            return (255, 214, 130, 255) if lit[row][col] else (24, 30, 44, 255)
        g = 64 + 14 * math.sin(u * 40)
        return (g, g + 2, g + 8, 255)
    return render(w, h, shade, ss=1)


# --------------------------------------------------------------- layouts
def street(rng, ticks=24576, grid=992):
    """A street canyon between towers, narrowing and widening, for the urban run."""
    out, tick, left, right = [], 1184, 3, 12
    while tick + grid < ticks - grid * 2:
        rows = []
        for i in range(16):
            if rng.random() < 0.12:
                left = max(2, min(5, left + rng.choice((-1, 1))))
            if rng.random() < 0.12:
                right = max(10, min(13, right + rng.choice((-1, 1))))
            cells = ['.'] * 16
            cells[left], cells[right] = '2', '4'
            rows.append(''.join(cells))
        out.append({'tick': tick, 'rows': rows, 'objects': {'2': '0x3b', '4': '0x3c'}})
        tick += grid
    return out


def blank():
    return [{'tick': 400, 'rows': ['.' * 16] * 16, 'objects': {}}]


# ---------------------------------------------------------------- themes
def palette(sky, ground, horizon, runway=None):
    patches = [
        {'table': 'primary', 'words': [0, 1], 'color': sky},
        {'table': 'primary', 'words': [2, 3], 'color': ground},
        {'table': 'alternate', 'words': [0, 1], 'color': sky},
        {'table': 'alternate', 'words': [2, 3], 'color': ground},
        {'table': 'gradient', 'words': list(range(1, 15)) + list(range(17, 31)), 'color': horizon},
    ]
    if runway:
        patches.append({'table': 'alternate', 'words': list(range(4, 12)), 'color': runway})
    return patches


def stage_for(theme, seed, mutators, entry):
    rng = random.Random(seed)
    art = {'fighter': 'art/fighter.png', 'ship': 'art/ship.png', 'water': 'art/water.png'}
    pick = lambda pool, n: rng.sample(pool, n)
    base = {'$schema': '../../stage.schema.json', 'version': 1, 'objective': 'combat'}
    if theme == 'night-raid':
        stage = dict(base, name='NIGHT RAID', slot=2, source_stage=2, music='city_202',
                     palette_patches=palette([10, 12, 34], [40, 36, 30], [30, 32, 70]),
                     mission={'type': 'destroy', 'briefing': 'NIGHT RAID', 'detail': 'BREAK THE CITY DEFENSES',
                              'target': 10 + rng.randrange(6), 'time_limit': 100, 'bonus': 80000, 'hold_tick': 20000},
                     conditions={'night': 0.8, 'searchlights': 2 + rng.randrange(3), 'flak': 0.4 + 0.3 * rng.random(), 'art': art},
                     formations={'seed': seed, 'waves': 12, 'start_tick': 1600, 'spacing': 6.0,
                                 'patterns': pick(['v', 'line', 'echelon', 'diamond'], 3),
                                 'entries': pick(['front', 'left', 'right', 'dive', 'rear'], 3),
                                 'size': [2, 4], 'aggression': 0.45})
    elif theme == 'naval-assault':
        sea = [20, 64, 150]
        stage = dict(base, name='NAVAL ASSAULT', slot=4, source_stage=4, music='after_burner',
                     palette_patches=palette([150, 196, 236], sea, [176, 212, 240], runway=sea),
                     replace_enemy_waves=True, enemy_waves=[], scenery_patterns=blank(),
                     mission={'type': 'destroy', 'briefing': 'NAVAL ASSAULT', 'detail': 'SKIM THE WAVES - SINK THE GROUP',
                              'target': 12 + rng.randrange(6), 'time_limit': 110, 'bonus': 90000, 'hold_tick': 20000},
                     conditions={'sea': True, 'flak': 0.35, 'wind': round(rng.uniform(-0.3, 0.3), 2), 'art': art},
                     formations={'seed': seed, 'waves': 12, 'start_tick': 1500, 'spacing': 6.5,
                                 'patterns': ['line', 'echelon', 'v'], 'entries': pick(['front', 'rear', 'left', 'right'], 3),
                                 'size': [2, 3], 'aggression': 0.4, 'ships': 2})
    elif theme == 'urban-trench':
        stage = dict(base, name='URBAN TRENCH', slot=8, source_stage=8, scenery_stage=2, music='super_stripe',
                     events=[{'tick': 0, 'command': 'music_transition', 'params': [0, 0, 0, 0, 0]}],
                     replace_enemy_waves=True,
                     enemy_waves=[{'tick': 0, 'command': 'dual_param', 'formation': 0, 'params': [0x30, 0xc2, 0x18, 0xc0]},
                                  {'tick': 23808, 'command': 'dual_param', 'formation': 0, 'params': [0x10, 0xc0, 0x20, 0xc3]}],
                     scenery_patterns=street(rng),
                     sprites=[{'frame': '0x1d270', 'asset': 'art/facade.png', 'own_colors': True, 'fit': 'stretch'}],
                     palette_patches=palette([8, 10, 26], [34, 34, 40], [26, 28, 56]),
                     mission={'type': 'survive', 'briefing': 'URBAN TRENCH', 'detail': 'STAY BETWEEN THE TOWERS',
                              'time_limit': 75, 'bonus': 70000},
                     conditions={'night': 0.55, 'searchlights': 2, 'flak': 0.3, 'art': art},
                     formations={'seed': seed, 'waves': 9, 'start_tick': 2200, 'spacing': 7.0,
                                 'patterns': ['line', 'echelon'], 'entries': ['rear', 'dive', 'front'],
                                 'size': [2, 3], 'aggression': 0.35})
    elif theme == 'storm-front':
        stage = dict(base, name='STORM FRONT', slot=10, source_stage=10, music='red_out',
                     palette_patches=palette([60, 66, 80], [52, 58, 50], [86, 92, 104]),
                     mission={'type': 'survive', 'briefing': 'STORM FRONT', 'detail': 'HOLD TOGETHER THROUGH THE CELL',
                              'time_limit': 80, 'bonus': 60000},
                     conditions={'rain': 0.85, 'lightning': 0.75, 'fog': 0.35, 'turbulence': 0.6,
                                 'wind': round(rng.uniform(-0.4, 0.4), 2), 'flak': 0.15, 'art': art},
                     formations={'seed': seed, 'waves': 8, 'start_tick': 1800, 'spacing': 8.0,
                                 'patterns': pick(['v', 'line', 'echelon', 'diamond'], 2), 'entries': ['rear', 'front', 'left', 'right'],
                                 'size': [2, 4], 'aggression': 0.4})
    else:
        raise SystemExit(f'Unknown theme {theme} ({", ".join(THEMES)})')
    stage['entry'] = entry or {'night-raid': 'dive', 'naval-assault': 'dive', 'urban-trench': 'roll', 'storm-front': 'climb'}[theme]
    if mutators:
        stage['mutators'] = mutators
    return stage


def build(theme, seed, mutators, entry, out):
    stage = stage_for(theme, seed, mutators, entry)
    write_png(out / 'art/fighter.png', *fighter())
    write_png(out / 'art/ship.png', *ship())
    write_png(out / 'art/water.png', *water())
    if theme == 'urban-trench':
        write_png(out / 'art/facade.png', *facade(seed=seed))
    out.mkdir(parents=True, exist_ok=True)
    (out / 'stage.json').write_text(json.dumps(stage, indent=2) + '\n', encoding='utf-8')
    print(f'Wrote {out} ({stage["name"]}, seed {seed})')


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('theme', choices=THEMES + ('all',))
    parser.add_argument('--seed', type=int, default=1)
    parser.add_argument('--mutators', default='', help='comma list: ' + ', '.join(MUTATORS))
    parser.add_argument('--entry', choices=('level', 'dive', 'climb', 'roll'))
    parser.add_argument('--output', type=Path, help='pack folder (default assets/custom_stages/packs/<theme>)')
    args = parser.parse_args()
    mutators = [m for m in args.mutators.split(',') if m]
    for m in mutators:
        if m not in MUTATORS:
            raise SystemExit(f'Unknown mutator {m} ({", ".join(MUTATORS)})')
    for theme in (THEMES if args.theme == 'all' else (args.theme,)):
        build(theme, args.seed, mutators, args.entry, args.output if args.output and args.theme != 'all' else PACKS / theme)


if __name__ == '__main__':
    main()
