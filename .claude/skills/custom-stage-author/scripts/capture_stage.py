#!/usr/bin/env python3
"""Run a custom stage headless and capture frames as PNGs plus a contact sheet.

Each frame is its own deterministic run (--frames N --frame-preview), so the
runs go in parallel. Use it to see a stage without playing it.

    python3 capture_stage.py assets/custom_stages/packs/night-raid --frames 300 900 1500
    python3 capture_stage.py my-pack --frames 600 --mutators storm --out build/captures/mine

Needs the built game (build/<dir>/afterburner2[.exe]) and the user's ROMs
(roms/aburner2.zip or a folder). Prints the sheet path. Standard library only.
"""
import argparse
import concurrent.futures
import os
import struct
import subprocess
import sys
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]


def find_exe():
    for name in ('afterburner2.exe', 'afterburner2'):
        for d in sorted((REPO / 'build').glob('*')):
            if (d / name).is_file():
                return d / name
    return None


def bmp_rows(path):
    data = path.read_bytes()
    off = struct.unpack_from('<I', data, 10)[0]
    w, h = struct.unpack_from('<ii', data, 18)
    bpp = struct.unpack_from('<H', data, 28)[0]
    stride = ((w * bpp + 31) // 32) * 4
    top_down, h = h < 0, abs(h)
    rows = []
    for y in range(h):
        row = data[off + (y if top_down else h - 1 - y) * stride:][:stride]
        step = bpp // 8
        rows.append(bytes(c for x in range(w) for c in (row[x * step + 2], row[x * step + 1], row[x * step])))
    return w, h, rows


def write_png(path, w, h, rows):
    def chunk(t, b):
        return struct.pack('>I', len(b)) + t + b + struct.pack('>I', zlib.crc32(t + b))
    raw = b''.join(b'\x00' + r for r in rows)
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', w, h, 8, 2, 0, 0, 0))
                     + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('stage', help='pack folder, stage.json, .zip or preset name')
    ap.add_argument('--frames', type=int, nargs='+', default=[300, 900, 1500])
    ap.add_argument('--roms', default=str(REPO / 'roms/aburner2.zip'))
    ap.add_argument('--assets', action='append', default=[])
    ap.add_argument('--mutators', default='')
    ap.add_argument('--out', type=Path, default=REPO / 'build/captures')
    ap.add_argument('--columns', type=int, default=3)
    args = ap.parse_args()
    exe = find_exe()
    if not exe:
        print('No built game found under build/. Build it first (see README).')
        return 1
    args.out.mkdir(parents=True, exist_ok=True)
    name = Path(args.stage).stem if Path(args.stage).suffix else Path(args.stage).name

    def run(frame):
        bmp = args.out / f'{name}_{frame}.bmp'
        cmd = [str(exe), '--roms', args.roms, '--custom-stage', args.stage, '--frames', str(frame), '--frame-preview', str(bmp)]
        for a in args.assets:
            cmd += ['--stage-assets', a]
        if args.mutators:
            cmd += ['--stage-mutators', args.mutators]
        proc = subprocess.run(cmd, cwd=REPO, capture_output=True, text=True, timeout=600)
        log = proc.stdout + proc.stderr
        if not bmp.is_file():
            return frame, None, log
        w, h, rows = bmp_rows(bmp)
        png = bmp.with_suffix('.png')
        write_png(png, w, h, rows)
        bmp.unlink()
        return frame, (w, h, rows, png), log

    with concurrent.futures.ThreadPoolExecutor(max_workers=min(6, len(args.frames))) as pool:
        results = sorted(pool.map(run, args.frames))
    shots = []
    for frame, shot, log in results:
        errors = [l for l in log.splitlines() if 'rror' in l or 'stopped' in l or 'not found' in l]
        if shot:
            print(f'frame {frame}: {shot[3]}')
            shots.append(shot)
        else:
            print(f'frame {frame}: no image')
        for e in errors[:3]:
            print('   ', e)
    if not shots:
        return 1
    # Half-size contact sheet.
    w, h = shots[0][0] // 2, shots[0][1] // 2
    cols = min(args.columns, len(shots))
    grid = (len(shots) + cols - 1) // cols
    sheet = []
    for gr in range(grid):
        group = shots[gr * cols:(gr + 1) * cols]
        for y in range(h):
            line = bytearray()
            for sw, sh, rows, _ in group:
                src = rows[min(sh - 1, y * 2)]
                for x in range(w):
                    line += src[x * 6:x * 6 + 3]
            line += bytes(3 * w * (cols - len(group)))
            sheet.append(bytes(line))
    out = args.out / f'{name}_sheet.png'
    write_png(out, w * cols, h * grid, sheet)
    print(f'sheet: {out}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
