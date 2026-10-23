#!/usr/bin/env python3
"""Export AB2 frame-record candidates as indexed PNGs and mapping.json (stdlib only)."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[2]
SPRITE_FILES = (
    ('mpr-10932.90', 'mpr-10934.94', 'mpr-10936.98', 'mpr-10938.102'),
    ('mpr-10933.91', 'mpr-10935.95', 'mpr-10937.99', 'mpr-10939.103'),
    ('epr-11103.92', 'epr-11104.96', 'epr-11105.100', 'epr-11106.104'),
    ('epr-11116.93', 'epr-11117.97', 'epr-11118.101', 'epr-11119.105'),
)


def load_roms(directory):
    """Match src/main/roms.cpp's INTERLEAVE2/4; record input provenance."""
    sources = []

    def read(name):
        data = (directory / name).read_bytes()
        if len(data) != 0x20000:
            raise ValueError(f'{name}: expected 0x20000 bytes, got {len(data):#x}')
        sources.append(dict(file=name, size=len(data), sha256=hashlib.sha256(data).hexdigest()))
        return data

    sub = bytearray(0x40000)
    sub[0::2] = read('epr-11109.20')
    sub[1::2] = read('epr-11110.29')
    sprites = bytearray(0x200000)
    for group, names in enumerate(SPRITE_FILES):
        for lane, name in enumerate(names):
            sprites[group * 0x80000 + lane:(group + 1) * 0x80000:4] = read(name)
    return sub, sprites, sources


def frame_records(sub):
    """Structural scan, NOT a claim that every candidate is used by the game.

    Sub $7B8..$9C8 consumes records addressed by main-CPU requests. Scan the
    Sub ROM data area on its 16-byte alignment; reject fill and nonrecords.
    Do not infer object names, animations or palette assignments from pixels.
    """
    result = []
    for address in range(0x10000, 0x20000, 16):
        width, last_row, bank_word, offset, pitch, adjust, r0, r1 = struct.unpack_from('>8H', sub, address)
        if (not 0 < width <= 1024 or last_row >= 4096 or bank_word & ~0xe00
                or not 0 < pitch <= 127 or width > pitch * 8 or r0 or r1):
            continue
        result.append(dict(id=f'sub_{address:06x}', frame_address=f'0x{address:06x}',
                           bank=bank_word >> 9, offset_words=offset, pitch_words=pitch,
                           width=width, height=last_row + 1, vertical_flip_offset_words=adjust))
    return result


def decode(sprites, frame):
    """Unscaled, forward, top-to-bottom pens, matching HWSprites::render.

    The seventh nibble marks the last word, but all eight pixels are still
    processed. Zero/15 are transparent; pen 14 is retained as an effect pen.
    The 16-bit read address wraps inside its 0x40000-byte bank.
    """
    width, height = frame['width'], frame['height']
    pixels = bytearray(width * height)
    bank = frame['bank'] * 0x40000
    for y in range(height):
        start = frame['offset_words'] + y * frame['pitch_words']
        for x in range(0, width, 8):
            offset = bank + ((start + x // 8) & 0xffff) * 4
            word = struct.unpack_from('<I', sprites, offset)[0]
            for k in range(min(8, width - x)):
                pen = (word >> (28 - 4 * k)) & 15
                pixels[y * width + x + k] = 0 if pen == 15 else pen
            if (word >> 4) & 15 == 15:
                break
    return pixels


def png(width, height, pixels):
    """8-bit indexed PNG: pixel indices preserve ROM pens, grayscale preview."""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

    rows = b''.join(b'\0' + pixels[y * width:(y + 1) * width] for y in range(height))
    palette = bytes(c for pen in range(16) for c in (pen * 17,) * 3)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', width, height, 8, 3, 0, 0, 0))
            + chunk(b'PLTE', palette) + chunk(b'tRNS', bytes([0] + [255] * 14 + [0]))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def extract(rom_dir, output):
    sub, sprites, sources = load_roms(rom_dir)
    frames = frame_records(sub)
    if not frames:
        raise ValueError('no frame-record candidates found')
    # Refuse overwrites, including an existing artist-edited mapping.
    output.mkdir(parents=True, exist_ok=False)
    (output / 'sprites').mkdir()
    for frame in frames:
        frame['image'] = f"sprites/{frame['id']}.png"
        frame['label'] = ''
        frame['replacement'] = None
        pixels = decode(sprites, frame)
        frame['pens_sha256'] = hashlib.sha256(pixels).hexdigest()
        (output / frame['image']).write_bytes(png(frame['width'], frame['height'], pixels))
    lods = [struct.unpack_from('>h', sub, 0x9e8 + z * 8)[0] for z in range(64)]
    by_address = {int(frame['frame_address'], 16): frame['id'] for frame in frames}
    for frame in frames:
        address = int(frame['frame_address'], 16)
        frame['lod_candidates'] = {str(delta): by_address[address + delta]
                                   for delta in sorted(set(lods)) if address + delta in by_address}
    mapping = dict(schema='ab2-sprite-extraction-v1', sources=sources,
                   discovery=dict(method='structural candidates', cpu='subcpu', start='0x010000',
                                  end_exclusive='0x020000', stride_bytes=16,
                                  warning='Usage, names, palette assignments and animation families are unverified. '
                                          'LOD links are address candidates, not confirmed family membership.'),
                   pixels=dict(format='indexed PNG, ROM pen indices', preview_palette='grayscale, not game colors',
                               transparent_pens=[0, 15], effect_pen=14,
                               orientation='forward, top-to-bottom, unscaled',
                               extent='frame width and last-row + 1; no trimming'),
                   addressing=dict(bank_bytes=0x40000, word_bytes=4, address_mask=0xffff,
                                   match_fields=['bank', 'offset_words', 'pitch_words']),
                   lod_table=dict(cpu='subcpu', address='0x0009e4', record_bytes=8,
                                  frame_delta_byte_offset=4, signed_frame_deltas=lods),
                   frames=frames)
    (output / 'mapping.json').write_text(json.dumps(mapping, indent=2) + '\n')
    return mapping


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rom-dir', type=Path, default=ROOT / 'roms')
    parser.add_argument('--output', type=Path, default=ROOT / 'build' / 'sprites')
    args = parser.parse_args()
    # All derived art/metadata stays in the ignored build tree, even with a
    # custom output directory. Resolve symlinks before checking containment.
    output = args.output.resolve()
    if not output.is_relative_to((ROOT / 'build').resolve()):
        parser.error('--output must be inside the repository build/ directory (ROM-derived assets)')
    try:
        mapping = extract(args.rom_dir, output)
    except (OSError, ValueError, struct.error) as error:
        parser.exit(1, f'extract_sprites: {error}\n')
    print(f"Extracted {len(mapping['frames'])} frame candidates; mapping: {output / 'mapping.json'}")
    return 0


if __name__ == '__main__':
    sys.exit(main())
