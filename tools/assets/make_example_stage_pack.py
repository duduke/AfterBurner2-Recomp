#!/usr/bin/env python3
"""Generate the example custom stage pack's art and music.

Everything is drawn procedurally by this script, so the pack contains no ROM
data and can be redistributed with the source. Standard library only.

    python3 tools/assets/make_example_stage_pack.py
"""
import argparse
import math
import random
import struct
import wave
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / 'assets/custom_stages/packs/sunset-strike'


def write_png(path, width, height, pixels):
    """pixels: flat iterable of (r, g, b, a) rows top to bottom."""
    def chunk(tag, data):
        body = tag + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body))
    raw = bytearray()
    stride = width * 4
    flat = bytes(channel for pixel in pixels for channel in pixel)
    for y in range(height):
        raw.append(0)
        raw += flat[y * stride:(y + 1) * stride]
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b'\x89PNG\r\n\x1a\n'
                     + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 6, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(bytes(raw), 9))
                     + chunk(b'IEND', b''))


def mix(a, b, t):
    t = max(0.0, min(1.0, t))
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


class Noise:
    """Value noise that wraps horizontally, so the backdrop scrolls without a seam."""

    def __init__(self, seed, period):
        rng = random.Random(seed)
        self.period = period
        self.table = {}
        self.rng = rng

    def lattice(self, x, y):
        key = (x % self.period, y)
        if key not in self.table:
            self.table[key] = self.rng.random()
        return self.table[key]

    def sample(self, x, y):
        x0, y0 = math.floor(x), math.floor(y)
        fx, fy = x - x0, y - y0
        fx, fy = fx * fx * (3 - 2 * fx), fy * fy * (3 - 2 * fy)
        top = self.lattice(x0, y0) * (1 - fx) + self.lattice(x0 + 1, y0) * fx
        bottom = self.lattice(x0, y0 + 1) * (1 - fx) + self.lattice(x0 + 1, y0 + 1) * fx
        return top * (1 - fy) + bottom * fy


def sunset_sky(width=768, height=320):
    stops = [(0.00, (22, 18, 58)), (0.35, (88, 36, 104)), (0.62, (214, 74, 92)),
             (0.82, (255, 150, 72)), (1.00, (255, 214, 130))]
    noise = Noise(7, 32)
    sun_x, sun_y, sun_r = width * 0.5, height * 0.74, height * 0.13
    pixels = []
    for y in range(height):
        v = y / (height - 1)
        for i in range(len(stops) - 1):
            if v <= stops[i + 1][0]:
                t = (v - stops[i][0]) / (stops[i + 1][0] - stops[i][0])
                base = mix(stops[i][1], stops[i + 1][1], t)
                break
        for x in range(width):
            color = base
            # Wrapped distance keeps the seam invisible at the image edges.
            dx = min(abs(x - sun_x), width - abs(x - sun_x))
            distance = math.hypot(dx, y - sun_y)
            if distance < sun_r:
                color = (255, 236, 170)
            else:
                color = mix(color, (255, 196, 120), 0.45 * math.exp(-(distance - sun_r) / (height * 0.14)))
            # Long thin cloud bands: many noise cells per image height, few across.
            band = noise.sample(x / width * 16, y / height * 26)
            band += 0.5 * noise.sample(x / width * 32 % 32, y / height * 52 + 40)
            streak = max(0.0, band / 1.5 - 0.5) * 3.0 * math.sin(math.pi * min(1.0, v * 1.15))
            color = mix(color, (255, 176, 150), min(0.7, streak))
            pixels.append((*(int(c) for c in color), 255))
    return width, height, pixels


def crystal(width=128, height=86):
    """A cluster of angular rock crystals, to replace a desert bush frame."""
    spires = [(0.5, 0.05, 0.16, (250, 150, 210)), (0.27, 0.3, 0.13, (150, 110, 240)),
              (0.72, 0.24, 0.14, (120, 200, 250)), (0.12, 0.55, 0.09, (240, 120, 170)),
              (0.88, 0.58, 0.09, (110, 150, 240))]
    pixels = []
    for y in range(height):
        for x in range(width):
            u, v = x / width, y / height
            color = None
            for cx, top, half, tint in spires:
                span = (v - top) / (0.98 - top)
                if 0 <= span <= 1 and abs(u - cx) < half * min(1.0, span * 2.2):
                    side = 1.0 if u > cx else 0.62
                    color = tuple(int(c * side * (1.05 - 0.45 * span)) for c in tint)
            pixels.append((*[min(255, c) for c in color], 255) if color else (0, 0, 0, 0))
    return width, height, pixels


def drone(width=120, height=40):
    """A flying-wing silhouette seen from behind, to replace an aircraft frame."""
    cx, cy = width / 2, height * 0.52
    pixels = []
    for y in range(height):
        for x in range(width):
            dx, dy = abs(x - cx) / (width / 2), (y - cy) / (height / 2)
            wing = dy > -0.1 - 0.5 * dx and dy < 0.26 - 0.3 * dx and dx < 0.98
            body = dx < 0.2 and -0.62 < dy < 0.52
            fin = dx < 0.05 and -0.92 < dy < -0.3
            engine = abs(dx - 0.42) < 0.1 and abs(dy - 0.08) < 0.14
            if not (wing or body or fin or engine):
                pixels.append((0, 0, 0, 0))
            elif engine:
                pixels.append((255, 150, 50, 255))
            elif abs(dy - (0.08 - 0.4 * dx)) < 0.05:
                pixels.append((250, 240, 200, 255))
            else:
                shade = 150 - int(90 * max(0.0, dy))
                pixels.append((shade // 2, shade // 3, shade, 255))
    return width, height, pixels


FONT = {
    'S': ['01111', '10000', '10000', '01110', '00001', '00001', '11110'],
    'U': ['10001', '10001', '10001', '10001', '10001', '10001', '01110'],
    'N': ['10001', '11001', '10101', '10101', '10011', '10001', '10001'],
    'E': ['11111', '10000', '10000', '11110', '10000', '10000', '11111'],
    'T': ['11111', '00100', '00100', '00100', '00100', '00100', '00100'],
    'R': ['11110', '10001', '10001', '11110', '10100', '10010', '10001'],
    'I': ['11111', '00100', '00100', '00100', '00100', '00100', '11111'],
    'K': ['10001', '10010', '10100', '11000', '10100', '10010', '10001'],
    ' ': ['00000'] * 7,
}


def banner(width=640, height=72, text='SUNSET STRIKE', scale=4):
    pixels = []
    text_width = len(text) * 6 * scale - scale
    left, top = (width - text_width) // 2, (height - 7 * scale) // 2
    for y in range(height):
        edge = min(y, height - 1 - y)
        for x in range(width):
            fade = min(1.0, min(x, width - 1 - x) / 80)
            alpha = int(190 * fade * min(1.0, edge / 8))
            line = edge < 3
            color = (255, 200, 110) if line else (28, 16, 48)
            value = (*color, 255 if line and fade > 0.6 else alpha)
            gx, gy = (x - left) // scale, (y - top) // scale
            if 0 <= gy < 7 and 0 <= gx < len(text) * 6 and gx % 6 < 5:
                if FONT[text[gx // 6]][gy][gx % 6] == '1':
                    value = (255, 224, 170, 255)
            pixels.append(value)
    return width, height, pixels


def glow(size=96):
    pixels = []
    center = (size - 1) / 2
    for y in range(size):
        for x in range(size):
            d = math.hypot(x - center, y - center) / center
            alpha = int(255 * max(0.0, 1 - d) ** 2)
            pixels.append((255, 220, 150, alpha))
    return size, size, pixels


def theme(path, seconds=8, rate=22050):
    """A short looping synth phrase, so the pack demonstrates stage music."""
    notes = [146.83, 146.83, 174.61, 196.00, 220.00, 196.00, 174.61, 130.81]
    step = seconds / (len(notes) * 2)
    frames = bytearray()
    for n in range(int(seconds * rate)):
        t = n / rate
        index = int(t / step)
        root = notes[(index // 2) % len(notes)]
        local = (t % step) / step
        envelope = (1 - local) ** 1.5
        lead = math.sin(2 * math.pi * root * 2 * t) + 0.4 * math.sin(2 * math.pi * root * 4.01 * t)
        bass = 0.9 if math.sin(2 * math.pi * root * 0.5 * t) > 0 else -0.9
        sample = 0.2 * lead * envelope + 0.16 * bass
        frames += struct.pack('<h', int(max(-1.0, min(1.0, sample)) * 30000))
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(frames))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    out = args.output
    write_png(out / 'art/sunset-sky.png', *sunset_sky())
    write_png(out / 'art/sprites/flying-wing.png', *drone())
    write_png(out / 'art/sprites/crystals.png', *crystal())
    write_png(out / 'art/title-banner.png', *banner())
    write_png(out / 'art/sun-glow.png', *glow())
    theme(out / 'audio/sunset-theme.wav')
    print('Wrote example stage assets to', out)


if __name__ == '__main__':
    main()
