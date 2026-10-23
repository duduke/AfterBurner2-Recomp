#!/usr/bin/env python3
"""ROM-free format checks, plus optional full ROM renderer differential/mutations."""
import argparse
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zlib

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'tools/assets/extract_sprites.py'
spec = importlib.util.spec_from_file_location('extract_sprites', SOURCE)
extract = importlib.util.module_from_spec(spec)
spec.loader.exec_module(extract)


class FormatTests(unittest.TestCase):
    def test_end_marker_stride_and_wrap(self):
        rom = bytearray(0x200000)
        # Seventh pen F ends the row AFTER drawing the eighth pen.
        struct.pack_into('<I', rom, 0x40000 + 0xffff * 4, 0x123456f8)
        struct.pack_into('<I', rom, 0x40000 + 4, 0xabcdef12)
        frame = dict(width=16, height=2, bank=1, offset_words=0xffff, pitch_words=2)
        pixels = extract.decode(rom, frame)
        self.assertEqual(pixels[:16], bytes([1, 2, 3, 4, 5, 6, 0, 8] + [0] * 8))
        self.assertEqual(pixels[16:24], bytes([10, 11, 12, 13, 14, 0, 1, 2]))

    def test_png_indices_and_transparency(self):
        pixels = bytes(range(16))
        data = extract.png(8, 2, pixels)
        self.assertEqual(data[:8], b'\x89PNG\r\n\x1a\n')
        pos, chunks = 8, {}
        while pos < len(data):
            length = struct.unpack_from('>I', data, pos)[0]
            kind, payload = data[pos+4:pos+8], data[pos+8:pos+8+length]
            self.assertEqual(zlib.crc32(kind + payload), struct.unpack_from('>I', data, pos+8+length)[0])
            chunks[kind] = payload
            pos += length + 12
        self.assertEqual(chunks[b'tRNS'], bytes([0] + [255]*14 + [0]))
        self.assertEqual(zlib.decompress(chunks[b'IDAT']), b'\0' + pixels[:8] + b'\0' + pixels[8:])

    def test_discovery_rejects_fill_and_preserves_special_stride(self):
        sub = bytearray(b'\xff' * 0x40000)
        struct.pack_into('>8H', sub, 0x10000, 16, 3, 0x600, 0xffff, 8, 24, 0, 0)
        frames = extract.frame_records(sub)
        self.assertEqual(len(frames), 1)
        self.assertEqual(frames[0]['id'], 'sub_010000')
        self.assertEqual(frames[0]['height'], 4)
        self.assertEqual(frames[0]['pitch_words'], 8)

    def test_bad_rom_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path / 'epr-11109.20').write_bytes(b'bad')
            with self.assertRaisesRegex(ValueError, 'expected'):
                extract.load_roms(path)

    def test_export_mapping_and_refuse_overwrite(self):
        sub = bytearray(b'\xff' * 0x40000)
        struct.pack_into('>8H', sub, 0x10000, 8, 0, 0, 0, 1, 0, 0, 0)
        for z in range(64):
            struct.pack_into('>h', sub, 0x9e8 + z * 8, 0)
        rom = bytearray(0x200000)
        struct.pack_into('<I', rom, 0, 0x123456f8)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / 'sprites'
            with patch.object(extract, 'load_roms', return_value=(sub, rom, [])):
                mapping = extract.extract(Path(directory), output)
                self.assertEqual(json.loads((output / 'mapping.json').read_text()), mapping)
                frame = mapping['frames'][0]
                self.assertEqual(frame['lod_candidates'], {'0': 'sub_010000'})
                self.assertIsNone(frame['replacement'])
                self.assertEqual((output / frame['image']).read_bytes(),
                                 extract.png(8, 1, bytes([1, 2, 3, 4, 5, 6, 0, 8])))
                with self.assertRaises(FileExistsError):
                    extract.extract(Path(directory), output)


def verify(reference):
    sub, rom, _ = extract.load_roms(ROOT / 'roms')
    frames = extract.frame_records(sub)
    directory = ROOT / 'build/oracle/sprite-extraction'
    directory.mkdir(parents=True, exist_ok=True)
    source, result = directory / 'reference-input.bin', directory / 'reference-pens.bin'
    with source.open('wb') as output:
        output.write(rom)
        output.write(struct.pack('>H', len(frames)))
        for frame in frames:
            output.write(struct.pack('>5H', *(frame[key] for key in
                         ('bank', 'offset_words', 'pitch_words', 'width', 'height'))))
    subprocess.run([str(reference.resolve()), str(source), str(result)], check=True)
    expected = result.read_bytes()

    def compare(decoder):
        position = 0
        for frame in frames:
            actual = decoder(rom, frame)
            if actual != expected[position:position+len(actual)]:
                return False
            position += len(actual)
        return position == len(expected)

    if not compare(extract.decode):
        raise AssertionError('extraction differs from HWSprites')
    print(f"Sprite extraction: {len(frames)} of {len(frames)} identical, 0 skipped (unscaled pens).")
    mutations = (
        ('nibble-order', '28 - 4 * k', '4 * k'),
        ('early-end-marker', '(word >> 4) & 15 == 15', '(word >> 28) & 15 == 15'),
        ('row-pitch', "y * frame['pitch_words']", "y * (frame['pitch_words'] + 1)"),
        ('bank', "frame['bank'] * 0x40000", "((frame['bank'] + 1) % 8) * 0x40000"),
    )
    code = SOURCE.read_text()
    for name, old, new in mutations:
        if code.count(old) != 1:
            raise AssertionError(f'{name}: mutation site changed')
        namespace = dict(__file__=str(SOURCE), __name__='sprite_extraction_mutant')
        exec(compile(code.replace(old, new), str(SOURCE), 'exec'), namespace)
        if compare(namespace['decode']):
            raise AssertionError(f'{name}: mutation survived')
        print(f'Sprite extraction mutation rejected: {name}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--reference', type=Path)
    args = parser.parse_args()
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(FormatTests)
    if not unittest.TextTestRunner(verbosity=2).run(suite).wasSuccessful():
        raise SystemExit(1)
    if args.reference:
        verify(args.reference)
