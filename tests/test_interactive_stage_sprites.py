"""ROM-backed presentation checks; baseline is verify-interactive-stages."""
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
binary = (ROOT / sys.argv[1]).resolve()
out = ROOT / 'build/oracle/interactive-stages'
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')


def run(args, valid=True):
    result = subprocess.run([str(binary), *args], cwd=ROOT, env=env,
                            capture_output=True, text=True, timeout=180)
    if (result.returncode == 0) != valid:
        raise RuntimeError(f'{args}: unexpected exit {result.returncode}\n{result.stdout}{result.stderr}')
    return result.stdout + result.stderr


def aircraft_region(path):
    data = path.read_bytes()
    offset = struct.unpack_from('<I', data, 10)[0]
    width, height = struct.unpack_from('<ii', data, 18)
    bits = struct.unpack_from('<H', data, 28)[0]
    if width != 1152 or abs(height) != 720 or bits not in (24, 32):
        raise RuntimeError(f'Unexpected BMP format: {path}')
    stride = ((width * bits + 31) // 32) * 4
    return b''.join(data[offset + (719-y if height > 0 else y)*stride + 450*(bits//8):
                         offset + (719-y if height > 0 else y)*stride + 700*(bits//8)]
                    for y in range(480, 550))


for kind in ('refuel', 'landing', 'runway', 'runway-jungle'):
    image = out / f'{kind}-sprite-demo.bmp'
    image.unlink(missing_ok=True)
    result = run(['--roms', 'roms', '--stage', kind, '--demo', '--frames', '2400',
                  '--screenshot', str(image)])
    if 'Practice artwork: original ROM sprites' not in result or 'phase=3' not in result:
        raise RuntimeError(f'{kind} ROM presentation did not complete: {result}')
    if 'approach_shots=0' in result:
        raise RuntimeError(f'{kind}: no ROM scenery was captured')
    # Compare the aircraft's scene area, excluding the header and modal text.
    # A changed label alone cannot satisfy this visual-change check.
    original = aircraft_region(out / f'{kind}-demo.bmp')
    dressed = aircraft_region(image)
    if sum(a != b for a, b in zip(original, dressed)) < 100:
        raise RuntimeError(f'{kind}: native aircraft artwork did not change the scene')
    print(result.split('Interactive stage:')[-1].strip())
ready_image = out / 'runway-full-throttle-prompt.bmp'
ready = run(['--roms', 'roms', '--stage', 'runway', '--demo-hold', '--frames', '1500',
             '--screenshot', str(ready_image)])
if 'phase=8' not in ready or 'missiles=100/100' not in ready or 'burner_visible=0' not in ready or not ready_image.is_file():
    raise RuntimeError(f'Runway did not hold at the fully rearmed takeoff prompt: {ready}')
run(['--roms', 'build/oracle/interactive-stages/no-such-rom-source', '--frames', '1'], False)
run(['--roms', 'roms', '--no-sprites', '--frames', '1'], False)
print('Interactive ROM sprites: four completed demos, four aircraft image changes, rearmed takeoff-prompt hold, missing-ROM and conflicting-option rejection passed')
