#!/usr/bin/env python3
"""Exercise Shuffle recording guards, seeded launches and reproducible output."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def verify(binary):
    binary = binary.resolve()
    directory = ROOT / 'build/oracle'
    directory.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, SDL_VIDEODRIVER='dummy',
                       SDL_AUDIODRIVER='dummy', SDL_RENDER_DRIVER='software')

    def run(arguments):
        result = subprocess.run([str(binary), *map(str, arguments)], cwd=ROOT,
                                env=environment, capture_output=True, text=True, timeout=120)
        return result.returncode, result.stdout + result.stderr

    with tempfile.TemporaryDirectory(prefix='shuffle-cli-', dir=directory) as temporary:
        work = Path(temporary)
        recording = work / 'forbidden.inputs'
        guards = [
            (['--shuffle', '--record', recording], 'Natural recording requires normal cold boot'),
            (['--shuffle-seed', '0', '--record', recording], 'Natural recording requires normal cold boot'),
            (['--shuffle', '--stage', '8'], 'Shuffle cannot be combined'),
            (['--shuffle', '--practice-stage', '8'], 'Shuffle cannot be combined'),
        ]
        guards.extend((['--shuffle-seed', value], 'Shuffle seed must be')
                      for value in ('-1', 'invalid', '4294967296'))
        for arguments, diagnostic in guards:
            status, output = run(arguments)
            assert status == 1 and diagnostic in output, (arguments, status, output)
            assert not list(work.glob(recording.name + '*')), 'Rejected Shuffle created a recording'

        def launch(seed, name):
            preview = directory / f'{name}.bmp'
            preview.unlink(missing_ok=True)
            status, output = run(['--shuffle-seed', seed, '--frames', '8',
                                  '--settings-file', work / 'settings.cfg',
                                  '--frame-preview', preview])
            assert status == 0, output
            match = re.search(r'^Shuffle seed (\d+): ([\d,]+)$', output, re.MULTILINE)
            assert match and int(match[1]) == seed, output
            order = [int(value) for value in match[2].split(',')]
            assert sorted(order) == list(range(1, 24)), order
            assert order[-1] == 23, 'Shuffle moved the native finale'
            pixels = preview.read_bytes()
            assert pixels[:2] == b'BM' and len(pixels) > 54, 'Missing Shuffle frame preview'
            return order, pixels

        first = launch(0, 'shuffle-seed0')
        repeated = launch(0, 'shuffle-seed0-repeat')
        different = launch(1, 'shuffle-seed1')
        assert first == repeated, 'Same seed changed stage order or presented output'
        assert first[0] != different[0], 'Different seeds did not change the stage order'
        print('Shuffle CLI: 7 guards passed; stages1..22 shuffled with finale23 fixed, seed0 retry identity, '
              'seed1 different order and three 8-frame SDL launches passed.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path, nargs='?', default=ROOT / 'build/afterburner2-release')
    verify(parser.parse_args().binary)
