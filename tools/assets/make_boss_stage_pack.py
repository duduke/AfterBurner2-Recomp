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


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [CLEAR] * (w * h)

    def put(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h:
            self.px[y * self.w + x] = c

    def rect(self, x0, y0, x1, y1, c):
        for y in range(max(0, int(y0)), min(self.h, int(y1))):
            for x in range(max(0, int(x0)), min(self.w, int(x1))):
                self.px[y * self.w + x] = c

    def poly(self, pts, shade):
        """Fill a polygon; shade(x, y) returns the color."""
        ys = [p[1] for p in pts]
        for y in range(max(0, int(min(ys))), min(self.h, int(max(ys)) + 1)):
            xs = []
            for (x0, y0), (x1, y1) in zip(pts, pts[1:] + pts[:1]):
                if (y0 <= y + 0.5 < y1) or (y1 <= y + 0.5 < y0):
                    xs.append(x0 + (y + 0.5 - y0) * (x1 - x0) / (y1 - y0))
            xs.sort()
            for a, b in zip(xs[0::2], xs[1::2]):
                for x in range(max(0, int(a)), min(self.w, int(b) + 1)):
                    self.px[y * self.w + x] = shade(x, y)

    def disk(self, cx, cy, r, shade):
        for y in range(int(cy - r), int(cy + r) + 1):
            for x in range(int(cx - r), int(cx + r) + 1):
                d = math.hypot(x + 0.5 - cx, y + 0.5 - cy) / r
                if d <= 1:
                    self.put(x, y, shade(d))

    def out(self):
        return self.w, self.h, self.px


def steel(v, warm=0):
    v = max(0, min(255, int(v)))
    return (v, v + 4, v + 14 - warm, 255)


def hull(w=640, h=240):
    """The carrier from astern: swept wings, a flat deck slab, a keel and tail fins."""
    c = Canvas(w, h)
    cx = w / 2
    # Wings: thick swept surfaces, darker underneath.
    wing = [(cx - 40, 92), (8, 150), (20, 172), (cx - 30, 150), (cx + 30, 150), (w - 20, 172), (w - 8, 150), (cx + 40, 92)]
    c.poly(wing, lambda x, y: steel(118 - (y - 92) * 0.9 - abs(x - cx) * 0.03))
    # Panel lines on the wing undersides.
    for k in range(1, 9):
        for side in (-1, 1):
            x0 = cx + side * (40 + k * 30)
            for t in range(40):
                c.put(int(x0 + side * t * 0.35), int(100 + t * 1.2), (60, 64, 76, 255))
    # Central fuselage body.
    body = [(cx - 70, 60), (cx + 70, 60), (cx + 92, 120), (cx + 62, 182), (cx - 62, 182), (cx - 92, 120)]
    c.poly(body, lambda x, y: steel(132 - (y - 60) * 0.55))
    # Flight deck slab on top, with deck edge lights and a center stripe.
    deck = [(cx - 150, 52), (cx + 150, 52), (cx + 128, 70), (cx - 128, 70)]
    c.poly(deck, lambda x, y: steel(96 + (70 - y) * 0.8))
    c.rect(cx - 150, 50, cx + 150, 53, (176, 182, 196, 255))
    for x in range(int(cx - 140), int(cx + 140), 14):
        c.rect(x, 50, x + 3, 53, (255, 210, 80, 255))
    c.rect(cx - 2, 53, cx + 2, 70, (230, 230, 230, 255))
    # Twin tail fins rising behind the deck.
    for side in (-1, 1):
        fin = [(cx + side * 96, 54), (cx + side * 112, 54), (cx + side * 132, 6), (cx + side * 124, 6)]
        c.poly(fin, lambda x, y: steel(124 - (54 - y) * 0.6))
        c.disk(cx + side * 128, 10, 3, lambda d: (255, 60, 50, 255))
    # Keel and the hangar mouth glowing at the stern.
    c.rect(cx - 46, 120, cx + 46, 150, (34, 36, 46, 255))
    for y in range(124, 146):
        glow = int(150 * (1 - abs(y - 135) / 12))
        c.rect(cx - 42, y, cx + 42, y + 1, (glow + 40, glow // 2 + 30, 30, 255))
    c.rect(cx - 6, 182, cx + 6, 230, (70, 74, 86, 255))
    # Navigation lights at the wingtips.
    c.disk(16, 160, 4, lambda d: (255, 70, 60, 255))
    c.disk(w - 16, 160, 4, lambda d: (80, 255, 120, 255))
    return c.out()


def pod(broken=False, w=96, h=72):
    c = Canvas(w, h)
    cx, cy = w / 2, h / 2
    if broken:
        c.disk(cx, cy, 30, lambda d: (40 + 30 * d, 34 + 20 * d, 34, 255))
        rng = random.Random(3)
        for _ in range(60):
            a, r = rng.random() * 6.28, rng.random() * 24
            c.disk(cx + math.cos(a) * r, cy + math.sin(a) * r, 2 + rng.random() * 3,
                   lambda d: (255, 120 + int(100 * (1 - d)), 40, 255))
        return c.out()
    c.disk(cx, cy, 34, lambda d: steel(150 - d * 60))
    c.disk(cx, cy, 24, lambda d: (40, 40, 50, 255))
    c.disk(cx, cy, 18, lambda d: (255, int(120 + 120 * (1 - d)), int(40 + 180 * (1 - d) ** 2), 255))
    for k in range(8):
        a = k * math.pi / 4
        for t in range(18, 25):
            c.put(int(cx + math.cos(a) * t), int(cy + math.sin(a) * t), (70, 70, 80, 255))
    return c.out()


def turret(broken=False, w=64, h=48):
    c = Canvas(w, h)
    cx = w / 2
    if broken:
        c.rect(10, 26, w - 10, h, (46, 44, 46, 255))
        for k in range(5):
            c.disk(18 + k * 7, 26, 4, lambda d: (255, 150, 40, 255))
        return c.out()
    c.rect(8, 30, w - 8, h, steel(88))
    c.disk(cx, 30, 18, lambda d: steel(170 - d * 70))
    for side in (-1, 1):
        c.rect(cx + side * 6 - 2, 2, cx + side * 6 + 2, 22, (60, 62, 72, 255))
    c.disk(cx, 28, 4, lambda d: (255, 70, 60, 255))
    return c.out()


def bridge(broken=False, w=120, h=84):
    c = Canvas(w, h)
    if broken:
        c.poly([(14, h), (24, 40), (60, 28), (96, 44), (106, h)], lambda x, y: (52, 48, 50, 255))
        rng = random.Random(9)
        for _ in range(40):
            c.disk(20 + rng.random() * 80, 40 + rng.random() * 40, 2 + rng.random() * 4, lambda d: (255, 140, 40, 255))
        return c.out()
    c.poly([(10, h), (22, 26), (98, 26), (110, h)], lambda x, y: steel(150 - (y - 26) * 0.8))
    c.rect(56, 0, 64, 26, (70, 74, 86, 255))
    c.rect(36, 8, 84, 12, (90, 94, 106, 255))
    for x in range(28, 92, 10):
        c.rect(x, 36, x + 7, 46, (120, 220, 255, 255))
    c.rect(20, 56, 100, 60, (255, 80, 60, 255))
    return c.out()


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
    ('PORT OUTER ENGINE', 'pod', -116, 40, 48, 36, 60),
    ('PORT INNER ENGINE', 'pod', -66, 30, 48, 36, 60),
    ('STBD INNER ENGINE', 'pod', 66, 30, 48, 36, 60),
    ('STBD OUTER ENGINE', 'pod', 116, 40, 48, 36, 60),
    ('PORT TURRET', 'turret', -40, -32, 32, 24, 50),
    ('STBD TURRET', 'turret', 40, -32, 32, 24, 50),
    ('BRIDGE', 'core', 0, -42, 60, 42, 160),
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
