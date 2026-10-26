#!/usr/bin/env python3
"""ROM-backed source, remembered-path and bounded-failure startup checks.

All ROM-derived fixtures/images and process logs stay in ignored build/oracle/.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def verify(binary):
    binary = binary.resolve()
    work = ROOT / 'build/oracle/rom-zip-startup'
    work.mkdir(parents=True, exist_ok=True)
    chips = re.findall(r'\("([^\"]+)"', (ROOT / 'src/main/roms.cpp').read_text())
    archive = work / 'aburner2.zip'
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as out:
        for chip in chips:
            out.write(ROOT / 'roms' / chip, 'aburner2/' + chip)
    environment = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
                       SDL_RENDER_DRIVER='software')
    settings = work / 'settings.cfg'

    def run(label, args, success=True, cwd=work):
        image = work / (label + '.bmp')
        result = subprocess.run([str(binary), '--frames', '240', '--settings-file', str(settings),
                                 '--frame-preview', str(image), *map(str, args)],
                                cwd=cwd, env=environment, capture_output=True, text=True, timeout=60)
        output = result.stdout + result.stderr
        (work / (label + '.log')).write_text(output)
        if result.returncode != (0 if success else 1):
            raise AssertionError(output)
        return image.read_bytes() if success else output

    reference = run('loose', ['--roms', ROOT / 'roms'])
    assert run('zip', ['--roms', archive]) == reference
    Path(str(settings) + '.roms').write_text(str(archive) + '\n')
    assert run('remembered', []) == reference
    # An explicit source overrides even a valid remembered location.
    assert 'Failed to load ROM set' in run('missing', ['--roms', work / 'absent.zip'], False)
    default = work / 'default'
    (default / 'roms').mkdir(parents=True, exist_ok=True)
    (default / 'roms/aburner2.zip').write_bytes(archive.read_bytes())
    Path(str(settings) + '.roms').write_text(str(work / 'absent.zip') + '\n')
    assert run('default', [], cwd=default) == reference
    print('Native ROM startup: directory, explicit ZIP, remembered ZIP and default ZIP have identical '
          '240-frame final images; explicit missing source fails without picker')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path, nargs='?', default=ROOT / 'build/afterburner2-release')
    verify(parser.parse_args().binary)
