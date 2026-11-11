#!/usr/bin/env python3
"""Generate the "Leviathan" example stage: a boss fight against a flying carrier.

LEVIATHAN is a huge airborne aircraft carrier that takes station ahead of the
player while the stage's own fighters keep coming. Shoot out its four engine
pods and two deck turrets, then the exposed bridge. All art here is drawn by
this script; the pack contains no ROM data and nothing from other games.
Standard library only.

    python3 tools/assets/make_boss_stage_pack.py
"""
import argparse
import json
import math
import random
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / 'assets/custom_stages/packs/leviathan'
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
    """Supersampled painter: shade(u, v) -> (r, g, b, a) with u, v in 0..1."""
    px = []
    for y in range(h):
        for x in range(w):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    c = shade((x + (sx + 0.5) / ss) / w, (y + (sy + 0.5) / ss) / h)
                    a = c[3] / 255.0
                    acc[0] += c[0] * a
                    acc[1] += c[1] * a
                    acc[2] += c[2] * a
                    acc[3] += c[3]
            alpha = acc[3] / (ss * ss)
            k = 255.0 / max(1e-6, acc[3])
            px.append((acc[0] * k, acc[1] * k, acc[2] * k, alpha) if alpha > 0 else CLEAR)
    return w, h, px


def noise(x, y, seed=0):
    n = (int(x * 913 + y * 6271 + seed * 113) * 2654435761) & 0xffffffff
    return ((n >> 13) & 1023) / 1023.0


def crack(u, v):
    """Thin glowing fissures across a wreck."""
    a = math.sin(u * 23 + math.cos(v * 17) * 3) * math.sin(v * 19 + u * 7)
    return abs(a) < 0.07


def metal(base, light, u, v, seed=0):
    """Plated metal: a lit base color with panel seams and fine grain."""
    seam = (abs((u * 46) % 1 - 0.5) < 0.035) or (abs((v * 18) % 1 - 0.5) < 0.05)
    grain = noise(u * 400, v * 400, seed) * 10 - 5
    k = light * (0.82 if seam else 1.0)
    return (base[0] * k + grain, base[1] * k + grain, base[2] * k + grain, 255)


def hull(w=800, h=300):
    """LEVIATHAN from astern: a flying-wing carrier with its flight deck on top."""
    pods = [(-0.77, 0.857), (-0.44, 0.768), (0.44, 0.768), (0.77, 0.857)]
    steel, deck = (112, 120, 138), (52, 56, 64)

    def shade(u, v):
        x = u * 2 - 1
        ax = abs(x)
        # Engine housings: dark recesses where the pods hang.
        for px_, pv in pods:
            d = ((x - px_) / 0.15) ** 2 + ((v - pv) / 0.11) ** 2
            if d < 1:
                return metal((70, 74, 86), 0.6 + 0.4 * d, u, v, 3)
        # Twin canted tail fins rising from the deck's rear corners.
        for side in (-1, 1):
            fx = x * side
            if 0.30 < fx < 0.42 and v < 0.20 and (fx - 0.30) < (0.20 - v) * 0.5 + 0.02:
                return metal(steel, 1.15 - v * 1.2, u, v, 5) if fx < 0.40 else (220, 60, 50, 255)
        # Flight deck on top, a lit trapezoid.
        deck_half = 0.40 + (v - 0.16) * 0.55
        if 0.16 <= v <= 0.30 and ax < deck_half:
            if ax < 0.006 or (abs(ax - deck_half * 0.55) < 0.004 and int(v * 120) % 3):
                return (230, 200, 60, 255)
            if abs(ax - deck_half) < 0.012:
                return (255, 220, 120, 255) if int(x * 90) % 4 == 0 else (150, 156, 170, 255)
            g = noise(u * 300, v * 300) * 8
            return (deck[0] + g, deck[1] + g, deck[2] + g + 4, 255)
        # Deck edge / hull side below it.
        if 0.30 < v <= 0.36 and ax < 0.40 + 0.14 * 0.55 + 0.02:
            return metal(steel, 0.65, u, v, 7)
        # Central body, with the hangar bay glowing at the stern.
        if 0.30 < v < 0.92 and ax < 0.30 - (v - 0.62) * 0.4 * (v > 0.62):
            if ax < 0.12 and 0.62 < v < 0.80:
                glow = 1 - abs(v - 0.71) / 0.09
                return (120 + 135 * glow, 60 + 110 * glow, 30 + 40 * glow, 255)
            return metal(steel, 1.05 - (v - 0.30) * 0.7 - ax * 0.4, u, v, 1)
        # Wings: swept leading edge, notched trailing edge, striped tips.
        lead = 0.30 + ax * 0.32
        trail = 0.82 - ax * 0.10 + 0.04 * abs(math.sin(ax * 9))
        if ax <= 1.0 and lead <= v <= trail:
            if ax > 0.93:
                return (230, 230, 230, 255) if int(v * 40) % 2 else (210, 50, 44, 255)
            t = (v - lead) / max(0.01, trail - lead)
            light = 1.45 if t < 0.06 else 1.25 - t * 0.75 - ax * 0.15
            return metal(steel, light, u, v, 2)
        return CLEAR

    return render(w, h, shade)


def pod(broken=False, w=120, h=90):
    def shade(u, v):
        x, y = u * 2 - 1, (v * 2 - 1) * 1.15
        r = math.hypot(x, y)
        if broken:
            # Torn, charred nacelle with glowing cracks.
            edge = 0.92 + 0.08 * math.sin(math.atan2(y, x) * 7)
            if r > edge:
                return CLEAR
            if crack(u, v) or r < 0.18:
                return (255, 150, 50, 255)
            v0 = 34 + 26 * (1 - r)
            return (v0 + 8, v0, v0 - 2, 255)
        if r > 1:
            return CLEAR
        if r > 0.80:                                  # nacelle lip, lit from above
            return metal((150, 156, 172), 0.9 - 0.25 * y, u, v, 4)
        if r > 0.66:
            return (40, 42, 50, 255)
        if r > 0.30:                                  # turbine blades
            a = math.atan2(y, x)
            c = 70 if math.sin(a * 11 + r * 6) > 0.2 else 46
            return (c, c + 2, c + 8, 255)
        glow = 1 - r / 0.30                           # hot core
        return (255, 140 + 115 * glow, 60 + 190 * glow * glow, 255)
    return render(w, h, shade)


def turret(broken=False, w=80, h=64):
    def shade(u, v):
        x, y = u * 2 - 1, v
        if broken:
            if y > 0.55 + 0.08 * math.sin(x * 9) and abs(x) < 0.8:
                return (255, 150, 40, 255) if crack(u, v) else (46, 42, 44, 255)
            return CLEAR
        for bx in (-0.22, 0.22):                      # twin barrels, pointing up
            if abs(x - bx) < 0.07 and 0.02 < y < 0.55:
                return (56 + 40 * (abs(x - bx) < 0.025), 60, 70, 255)
        if (x / 0.62) ** 2 + ((y - 0.62) / 0.36) ** 2 < 1:
            return metal((140, 146, 160), 1.3 - (y - 0.3) * 0.9 - x * 0.2, u, v, 6)
        if 0.86 < y < 1.0 and abs(x) < 0.85:          # base ring
            return metal((90, 94, 106), 0.9, u, v, 6)
        return CLEAR
    return render(w, h, shade)


def bridge(broken=False, w=150, h=110):
    def shade(u, v):
        x = u * 2 - 1
        ax = abs(x)
        if broken:
            if v > 0.45 + 0.1 * math.sin(x * 8) and ax < 0.75 - (1 - v) * 0.3:
                return (255, 140, 40, 255) if crack(u, v) else (52, 46, 48, 255)
            return CLEAR
        if ax < 0.025 and v < 0.30:                    # mast
            return (80, 84, 96, 255)
        if abs(v - 0.10) < 0.025 and ax < 0.30:        # radar bar
            return (190, 196, 208, 255)
        if 0.22 < v < 0.46 and ax < 0.42:              # upper bridge
            if 0.30 < v < 0.38 and int(u * 40) % 3:
                return (140, 220, 255, 255)
            return metal((140, 146, 162), 1.15 - v * 0.6 - x * 0.15, u, v, 8)
        if 0.46 <= v and ax < 0.48 + (v - 0.46) * 0.6:  # main block, widening
            if 0.56 < v < 0.64 and int(u * 52) % 3 and ax < 0.6:
                return (120, 200, 240, 255)
            if 0.78 < v < 0.82:
                return (220, 60, 50, 255)
            return metal((128, 134, 150), 1.05 - (v - 0.46) * 0.6 - x * 0.15, u, v, 8)
        return CLEAR
    return render(w, h, shade)


def storm_sky(w=1024, h=512):
    """A dark storm sky for the battle."""
    rng = random.Random(5)
    px = []
    for y in range(h):
        for x in range(w):
            t = y / h
            n = (math.sin(x * 0.013 + y * 0.02) + math.sin(x * 0.031 - y * 0.011) * 0.6 + math.sin(x * 0.0071 + 2.0) * 0.8)
            v = 34 + 40 * t + 9 * n
            px.append((v * 0.85, v * 0.9, v * 1.15 + 14, 255))
    for _ in range(3):
        x = rng.randrange(w)
        y = 0
        while y < h * 0.7:
            for dx in range(-1, 2):
                if 0 <= x + dx < w:
                    px[y * w + x + dx] = (220, 225, 255, 255)
            x += rng.choice((-2, -1, 0, 1, 2))
            y += 1
    return w, h, px


PARTS = [
    # name, kind, image, broken image, position from hull center, size, hp
    ('PORT OUTER ENGINE', 'pod', -116, 40, 44, 34, 60),
    ('PORT INNER ENGINE', 'pod', -66, 30, 44, 34, 60),
    ('STBD INNER ENGINE', 'pod', 66, 30, 44, 34, 60),
    ('STBD OUTER ENGINE', 'pod', 116, 40, 44, 34, 60),
    ('PORT TURRET', 'turret', -40, -32, 32, 24, 50),
    ('STBD TURRET', 'turret', 40, -32, 32, 24, 50),
    ('BRIDGE', 'core', 0, -44, 58, 42, 160),
]


def stage():
    parts = []
    for name, kind, x, y, w, h, hp in PARTS:
        base = {'pod': 'engine', 'turret': 'turret', 'core': 'bridge'}[kind]
        parts.append({'name': name, 'kind': kind, 'asset': f'art/boss/{base}.png',
                      'broken': f'art/boss/{base}-wrecked.png', 'at': [x, y], 'size': [w, h], 'hp': hp})
    return {
        '$schema': '../../stage.schema.json',
        'version': 1,
        'name': 'LEVIATHAN',
        'objective': 'combat',
        'slot': 7,
        'source_stage': 7,
        'music': 'red_out',
        'replace_events': True,
        'events': [
            {'tick': 0, 'command': 'music_transition', 'params': [0, 0, 0, 0, 0]},
            {'tick': 6400, 'command': 'stage_advance', 'params': [0, 0, 0, 0, 0]},
        ],
        'presentation': {
            'backdrop': {'asset': 'art/storm-sky.png', 'parallax': [0.25, 0.3], 'drift': [6, 0], 'follow_roll': True},
        },
        'boss': {
            'name': 'LEVIATHAN',
            'hull': 'art/boss/hull.png',
            'size': [300, 112],
            'appear_tick': 3600,
            'hold_tick': 6000,
            'time_limit': 120,
            'bonus': 200000,
            'parts': parts,
        },
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    out = args.output
    write_png(out / 'art/boss/hull.png', *hull())
    write_png(out / 'art/boss/engine.png', *pod())
    write_png(out / 'art/boss/engine-wrecked.png', *pod(True))
    write_png(out / 'art/boss/turret.png', *turret())
    write_png(out / 'art/boss/turret-wrecked.png', *turret(True))
    write_png(out / 'art/boss/bridge.png', *bridge())
    write_png(out / 'art/boss/bridge-wrecked.png', *bridge(True))
    write_png(out / 'art/storm-sky.png', *storm_sky())
    (out / 'stage.json').write_text(json.dumps(stage(), indent=2) + '\n', encoding='utf-8')
    print(f'Wrote {out}')


if __name__ == '__main__':
    main()
