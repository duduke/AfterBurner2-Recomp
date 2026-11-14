#!/usr/bin/env python3
"""Generate the "Iron Trench" example stage: a high-speed run down a metal trench.

Everything is drawn and composed here, so the pack contains no ROM data and no art
or music from elsewhere. The walls are the arcade's own canyon wall objects, laid out
by scenery patterns and reskinned. Standard library only.

    python3 tools/assets/make_trench_stage_pack.py
"""
import argparse
import json
import math
import random
import struct
import wave
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OUTPUT = ROOT / 'assets/custom_stages/packs/iron-trench'

WALL_LEFT, WALL_RIGHT = '2', '4'   # Canyon wall objects $3B and $3C.
OBJECTS = {'2': '0x3b', '4': '0x3c', '1': '0xcd', '3': '0xc0'}
STAGE_TICKS = 24576                # Stage 8's terminal event.
GRID_TICKS = 992                   # About how long one 16x16 pattern lasts.


def write_png(path, width, height, pixels):
    def chunk(tag, data):
        body = tag + data
        return struct.pack('>I', len(data)) + body + struct.pack('>I', zlib.crc32(body))
    stride = width * 4
    flat = bytes(max(0, min(255, int(channel))) for pixel in pixels for channel in pixel)
    raw = bytearray()
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


def starfield(width=1024, height=360):
    """Black space with stars and a faint nebula band. Wraps horizontally."""
    rng = random.Random(1977 * 3)
    buffer = [(4, 5, 14)] * (width * height)
    # Nebula from a few wrapped sine bands, so it has no seam.
    nebula = []
    for y in range(height):
        v = y / height
        for x in range(width):
            u = x / width * math.tau
            cloud = (math.sin(u * 2 + 1.3) + math.sin(u * 3 + v * 7) * 0.7 + math.sin(u * 5 - v * 11) * 0.4)
            band = math.exp(-((v - 0.34 - 0.08 * math.sin(u * 2)) / 0.16) ** 2)
            nebula.append(max(0.0, cloud * 0.3 + 0.45) * band)
    buffer = [tuple(int(c) for c in mix((4, 5, 14), (58, 30, 92), n)) for n in nebula]
    for _ in range(1500):
        x, y = rng.randrange(width), rng.randrange(height)
        level = rng.choice((90, 120, 160, 210, 255))
        tint = rng.choice(((1, 1, 1), (0.8, 0.88, 1), (1, 0.9, 0.78)))
        buffer[y * width + x] = tuple(int(level * c) for c in tint)
    for _ in range(46):
        x, y = rng.randrange(width), rng.randrange(height)
        for dy in range(-1, 2):
            for dx in range(-1, 2):
                if dx == 0 or dy == 0:
                    buffer[((y + dy) % height) * width + (x + dx) % width] = (255, 255, 245)
    # A distant ringed gas giant, in the open sky between the HUD readouts.
    cx, cy, radius = 412.0, 108.0, height * 0.075
    for y in range(int(cy - radius * 2.4), int(cy + radius * 2.4)):
        for x in range(int(cx - radius * 2.4), int(cx + radius * 2.4)):
            dx, dy = x - cx, y - cy
            distance = math.hypot(dx, dy)
            tilted = math.hypot(dx * 0.96 + dy * 0.28, (-dx * 0.28 + dy * 0.96) * 3.6)
            ring = radius * 1.35 < tilted < radius * 2.15
            behind = (-dx * 0.28 + dy * 0.96) < 0
            index = (y % height) * width + x % width
            if distance < radius and not (ring and not behind):
                light = max(0.12, (dx * -0.55 + dy * -0.55) / radius + 0.55)
                stripe = 0.82 + 0.18 * math.sin(dy * 0.55 + dx * 0.05)
                buffer[index] = tuple(int(c * light * stripe) for c in (214, 170, 128))
            elif ring and distance >= radius * (1.0 if not behind else 0.0):
                shade = 0.55 + 0.25 * math.sin(tilted * 0.35)
                buffer[index] = tuple(int(c * shade) for c in (196, 178, 150))
    return width, height, [(*p, 255) for p in buffer]


def wall_panel(width=88, height=178):
    """A solid slab of hull plating with conduits and lit slots. Dark, for only 14 colors."""
    rng = random.Random(42)
    rows = [0, 22, 46, 64, 92, 118, 138, 160, height]
    pixels = [[(70, 76, 90)] * width for _ in range(height)]
    for top, bottom in zip(rows, rows[1:]):
        base = rng.choice(((84, 90, 104), (66, 72, 86), (96, 102, 116), (58, 62, 76)))
        split = rng.randrange(18, width - 18)
        for y in range(top, bottom):
            for x in range(width):
                shade = base if x < split else tuple(int(c * 0.82) for c in base)
                if y in (top, top + 1) or x in (split, split + 1):
                    shade = (26, 28, 38)
                elif y == top + 2:
                    shade = (150, 156, 170)
                pixels[y][x] = shade
        kind = rng.random()
        if kind < 0.45:          # Row of lit slots.
            light = rng.choice(((250, 186, 80), (120, 214, 250)))
            for x in range(6, width - 8, 12):
                for y in range(top + 8, min(bottom - 3, top + 13)):
                    for dx in range(6):
                        pixels[y][x + dx] = light
        elif kind < 0.8:         # Pipe bundle.
            for y in range(top + 6, bottom - 3):
                for x in range(10, width - 10, 18):
                    pixels[y][x] = (40, 42, 52)
                    pixels[y][x + 1] = (40, 42, 52)
                    pixels[y][x + 2] = (138, 144, 158)
    return width, height, [(*p, 255) for row in pixels for p in row]


def gun_block(width=128, height=88):
    """A squat armored gun emplacement, an obstacle to steer around."""
    pixels = []
    for y in range(height):
        for x in range(width):
            u, v = x / width, y / height
            taper = 0.12 * (1 - v)
            if not (taper < u < 1 - taper) or v < 0.28:
                inside_turret = abs(u - 0.5) < 0.17 and 0.06 < v <= 0.28
                barrel = abs(v - 0.14) < 0.03 and 0.2 < u < 0.8
                if inside_turret:
                    pixels.append((92, 96, 110, 255))
                elif barrel:
                    pixels.append((46, 48, 58, 255))
                else:
                    pixels.append((0, 0, 0, 0))
                continue
            if abs(v - 0.62) < 0.05 and int(u * 12) % 2:
                pixels.append((255, 92, 60, 255))
            else:
                shade = 112 - int(58 * v)
                pixels.append((shade, shade + 4, shade + 16, 255))
    return width, height, pixels


FONT = {
    'A': '01110 10001 10001 11111 10001 10001 10001', 'C': '01111 10000 10000 10000 10000 10000 01111',
    'D': '11110 10001 10001 10001 10001 10001 11110', 'E': '11111 10000 10000 11110 10000 10000 11111',
    'G': '01111 10000 10000 10011 10001 10001 01111', 'H': '10001 10001 10001 11111 10001 10001 10001',
    'I': '11111 00100 00100 00100 00100 00100 11111', 'K': '10001 10010 10100 11000 10100 10010 10001',
    'L': '10000 10000 10000 10000 10000 10000 11111', 'N': '10001 11001 10101 10101 10011 10001 10001',
    'O': '01110 10001 10001 10001 10001 10001 01110', 'R': '11110 10001 10001 11110 10100 10010 10001',
    'T': '11111 00100 00100 00100 00100 00100 00100', 'U': '10001 10001 10001 10001 10001 10001 01110',
    ' ': '00000 00000 00000 00000 00000 00000 00000',
}


def text_card(text, width, height, scale, background, ink, rule=None):
    pixels = [background] * (width * height)
    text_width = len(text) * 6 * scale - scale
    left, top = (width - text_width) // 2, (height - 7 * scale) // 2
    for index, char in enumerate(text):
        rows = FONT[char].split()
        for gy, row in enumerate(rows):
            for gx, cell in enumerate(row):
                if cell == '1':
                    for y in range(scale):
                        for x in range(scale):
                            pixels[(top + gy * scale + y) * width + left + (index * 6 + gx) * scale + x] = ink
    if rule:
        for x in range(width // 5, width * 4 // 5):
            for y in (top - scale * 2, top + 7 * scale + scale * 2 - 1):
                pixels[y * width + x] = rule
    return width, height, pixels


def reticle(size=180):
    """Corner brackets and a center ring for the final approach."""
    pixels = [(0, 0, 0, 0)] * (size * size)
    color = (250, 190, 70, 235)
    arm, thick = size // 4, 4
    for i in range(arm):
        for t in range(thick):
            for x, y in ((i, t), (t, i), (size - 1 - i, t), (size - 1 - t, i),
                         (i, size - 1 - t), (t, size - 1 - i), (size - 1 - i, size - 1 - t), (size - 1 - t, size - 1 - i)):
                pixels[y * size + x] = color
    center = (size - 1) / 2
    for y in range(size):
        for x in range(size):
            if abs(math.hypot(x - center, y - center) - size * 0.14) < 1.6:
                pixels[y * size + x] = color
    return size, size, pixels


def theme(path, rate=22050):
    """An original driving minor-key theme: an ostinato with a rising brass-like lead."""
    beat = 60 / 138 / 2
    ostinato = [73.42, 73.42, 87.31, 73.42, 98.0, 73.42, 87.31, 65.41]
    lead = [(293.66, 4), (349.23, 2), (392.0, 2), (440.0, 6), (392.0, 2),
            (349.23, 4), (293.66, 2), (261.63, 2), (293.66, 8),
            (293.66, 4), (349.23, 2), (440.0, 2), (523.25, 6), (466.16, 2),
            (440.0, 4), (392.0, 2), (349.23, 2), (293.66, 8)]
    steps = sum(length for _, length in lead)
    total = int(steps * beat * rate)
    frames = bytearray()
    timeline = []
    position = 0
    for frequency, length in lead:
        timeline.append((position, position + length, frequency))
        position += length
    for n in range(total):
        t = n / rate
        step = t / beat
        local = step % 1
        root = ostinato[int(step) % len(ostinato)]
        bass = (1 if (t * root * 2) % 1 < 0.5 else -1) * (1 - local) ** 1.2
        drum = 0.0
        if int(step) % 4 == 0:
            drum = math.sin(math.tau * 70 * (t % beat) * (1 - local * 0.5)) * (1 - local) ** 3
        voice = 0.0
        for begin, end, frequency in timeline:
            if begin <= step < end:
                held = (step - begin) / (end - begin)
                attack = min(1.0, (step - begin) * 6)
                release = min(1.0, (end - step) * 4)
                wave_shape = sum(math.sin(math.tau * frequency * h * t) / h for h in (1, 2, 3, 4, 5))
                voice = wave_shape * attack * release * (0.8 + 0.2 * math.sin(held * 9))
                break
        sample = 0.17 * bass + 0.14 * voice + 0.24 * drum
        frames += struct.pack('<h', int(max(-1.0, min(1.0, sample)) * 30000))
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), 'wb') as out:
        out.setnchannels(1)
        out.setsampwidth(2)
        out.setframerate(rate)
        out.writeframes(bytes(frames))


def row(left, right, extras=None):
    cells = ['.'] * 16
    cells[left] = WALL_LEFT
    cells[right] = WALL_RIGHT
    for column, cell in (extras or {}).items():
        cells[column] = cell
    return ''.join(cells)


def patterns():
    """A straight trench that tightens, with guns to steer around further in."""
    straight = [row(4, 11)] * 16
    guns_left = [row(4, 11, {5: '1'}) if i in (3, 11) else row(4, 11) for i in range(16)]
    guns_right = [row(4, 11, {10: '1'}) if i in (7, 15) else row(4, 11) for i in range(16)]
    weave = [row(4, 11, {5: '1'}) if i == 2 else row(4, 11, {10: '1'}) if i == 8 else
             row(4, 11, {5: '1'}) if i == 14 else row(4, 11) for i in range(16)]
    entry_narrowing = [row(2, 13), row(2, 13), row(2, 13), row(3, 13), row(3, 12), row(3, 12), row(3, 12), row(3, 11),
                       row(4, 11), row(4, 11), row(4, 11), row(4, 11), row(4, 11), row(4, 11), row(4, 11), row(4, 11)]
    sequence = [entry_narrowing, straight, straight, guns_left, straight, guns_right, straight, weave]
    result = []
    tick = 1184
    index = 0
    while tick + GRID_TICKS < STAGE_TICKS - GRID_TICKS * 2:
        grid = sequence[index] if index < len(sequence) else sequence[1 + (index - 1) % (len(sequence) - 1)]
        result.append({'tick': tick, 'rows': grid, 'objects': OBJECTS})
        tick += GRID_TICKS
        index += 1
    return result


def stage():
    final = STAGE_TICKS - 3200
    return {
        '$schema': '../../stage.schema.json',
        'version': 1,
        'name': 'IRON TRENCH',
        'objective': 'combat',
        'environment': 'canyon',
        'slot': 8,
        'source_stage': 8,
        # Named scenery_stage in the format, but this stream is the air enemy waves.
        'scenery_stage': 2,
        'music': 'super_stripe',
        'music_file': 'audio/trench-theme.wav',
        # Stage 8 switches air enemies off with sub-code 12 of this event; 0 turns them on.
        'events': [{'tick': 0, 'command': 'music_transition', 'params': [0, 0, 0, 0, 0]}],
        'replace_enemy_waves': True,
        # Stage 8's own opening and closing stream records, without its canyon layouts.
        'enemy_waves': [
            {'tick': 0, 'command': 'dual_param', 'formation': 0, 'params': [0x30, 0xc2, 0x18, 0xc0]},
            {'tick': 23808, 'command': 'dual_param', 'formation': 0, 'params': [0x10, 0xc0, 0x20, 0xc3]},
            {'tick': 26784, 'command': 'dual_param', 'formation': 0, 'params': [0x10, 0xc0, 0x20, 0xc3]},
        ],
        'scenery_patterns': patterns(),
        'presentation': {
            'backdrop': {'asset': 'art/starfield.png', 'parallax': [0.2, 0.3], 'drift': [0, 0], 'follow_roll': True},
            'ocean': {'water': [44, 46, 58], 'water_band': [58, 60, 74], 'haze': [60, 62, 78]},
        },
        'sprites': [
            {'frame': '0x1d270', 'asset': 'art/sprites/wall-panel.png', 'own_colors': True, 'fit': 'stretch'},
            {'frame': '0x1d310', 'asset': 'art/sprites/gun-block.png', 'own_colors': True},
        ],
        'layers': [
            {
                'name': 'title card, covers the fly-in',
                'order': 1000,
                'space': 'screen',
                'elements': [
                    {'type': 'image', 'asset': 'art/title.png', 'x': 480, 'y': 300, 'width': 1800, 'height': 600,
                     'filter': 'nearest', 'ticks': [0, 1200]},
                ],
            },
            {
                'name': 'final approach',
                'order': 900,
                'space': 'screen',
                'elements': [
                    {'type': 'image', 'asset': 'art/reticle.png', 'x': 480, 'y': 300, 'width': 240, 'height': 240,
                     'ticks': [final, STAGE_TICKS]},
                    {'type': 'image', 'asset': 'art/target-text.png', 'x': 480, 'y': 170, 'width': 480, 'height': 48,
                     'filter': 'nearest', 'ticks': [final, STAGE_TICKS]},
                ],
            },
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT)
    args = parser.parse_args()
    out = args.output
    write_png(out / 'art/starfield.png', *starfield())
    write_png(out / 'art/sprites/wall-panel.png', *wall_panel())
    write_png(out / 'art/sprites/gun-block.png', *gun_block())
    write_png(out / 'art/title.png', *text_card('IRON TRENCH', 900, 300, 6, (3, 3, 10, 255), (250, 190, 70, 255), (250, 190, 70, 255)))
    write_png(out / 'art/target-text.png', *text_card('TARGET AHEAD', 480, 48, 4, (0, 0, 0, 0), (250, 190, 70, 255)))
    write_png(out / 'art/reticle.png', *reticle())
    theme(out / 'audio/trench-theme.wav')
    (out / 'stage.json').write_text(json.dumps(stage(), indent=2) + '\n', encoding='utf-8')
    print('Wrote trench stage to', out)


if __name__ == '__main__':
    main()
