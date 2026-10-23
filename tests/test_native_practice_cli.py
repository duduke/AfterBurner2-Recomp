#!/usr/bin/env python3
"""Exercise selected-stage CLI guards and an SDL dummy Practice launch."""
import argparse
import os
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def verify(binary):
    binary = binary.resolve()
    require(binary.is_file(), f'Native binary unavailable: {binary}')
    output = ROOT / 'build/oracle'
    output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, SDL_VIDEODRIVER='dummy',
                       SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')

    def run(arguments):
        result = subprocess.run([str(binary), *map(str, arguments)], cwd=ROOT,
                                env=environment, capture_output=True, text=True,
                                timeout=120)
        return result, result.stdout + result.stderr

    with tempfile.TemporaryDirectory(prefix='practice-cli-', dir=output) as directory:
        work = Path(directory)
        recording = work / 'rejected-recording.phys'
        guards = (
            ('zero stage', ['--practice-stage', '0'],
             'Stage must be a positive integer'),
            # The runtime ROM count is bounded below this valid unsigned value.
            ('stage outside ROM catalog', ['--practice-stage', '4294967295'],
             'Stage number out of range'),
            ('conflicting stage modes', ['--practice-stage', '8', '--stage', '2'],
             'Choose either --stage or --practice-stage'),
            ('natural recording guard', ['--practice-stage', '8', '--record', recording],
             'Natural recording requires normal cold boot'),
        )
        for name, arguments, message in guards:
            result, log = run(arguments)
            require(result.returncode == 1,
                    f'{name}: expected exit 1, got {result.returncode}\n{log}')
            require(message in log, f'{name}: expected diagnostic absent\n{log}')
            require(not list(work.glob(recording.name + '*')),
                    f'{name}: rejected startup created a recording or metadata')
            print(f'Practice CLI guard passed: {name}')

        preview = output / 'practice-stage8.bmp'
        preview.unlink(missing_ok=True)
        result, log = run(['--practice-stage', '8', '--frames', '8',
                           '--settings-file', work / 'settings.cfg',
                           '--frame-preview', preview])
        require(result.returncode == 0,
                f'Practice Stage 8 smoke failed with exit {result.returncode}\n{log}')
        require(preview.is_file() and preview.stat().st_size > 54,
                f'Practice Stage 8 preview missing or empty\n{log}')
        header = preview.read_bytes()[:26]
        require(header[:2] == b'BM', 'Practice Stage 8 preview is not a BMP')
        width, height = struct.unpack_from('<ii', header, 18)
        require(width > 0 and height != 0, 'Practice Stage 8 BMP has invalid dimensions')
        require(not list(work.glob(recording.name + '*')),
                'Practice smoke created a rejected natural recording')
        print(f'Practice CLI: 4 guards passed; Stage 8 SDL dummy launch and '
              f'8 counted frames passed ({preview.relative_to(ROOT)}).')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path, nargs='?',
                        default=ROOT / 'build/afterburner2-release')
    verify(parser.parse_args().binary)
