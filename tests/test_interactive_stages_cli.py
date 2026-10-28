"""Bounded ROM-free SDL executable checks; artifacts remain under build/."""
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
binary = (ROOT / (sys.argv[1] if len(sys.argv) > 1 else 'build/interactive-stages')).resolve()
out = ROOT / 'build/oracle/interactive-stages'
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')


def run(args, valid=True):
    result = subprocess.run([str(binary), '--no-sprites', *args], cwd=ROOT, env=env,
                            capture_output=True, text=True, timeout=90)
    if (result.returncode == 0) != valid:
        raise RuntimeError(f'{args}: unexpected exit {result.returncode}\n{result.stdout}{result.stderr}')
    return result.stdout + result.stderr


for args in (['--stage', 'bad'], ['--frames', '0'], ['--frames', '-1'],
             ['--frames', '1x'], ['--frames'], ['--screenshot', 'never.bmp'],
             ['--record', 'never.bin']):
    run(args, False)
for kind in ('refuel', 'landing', 'runway', 'runway-jungle'):
    screenshot = out / f'{kind}-demo.bmp'
    screenshot.unlink(missing_ok=True)
    result = run(['--stage', kind, '--demo', '--frames', '1500', '--screenshot', str(screenshot)])
    if 'phase=3' not in result:
        raise RuntimeError(f'{kind} demo did not complete successfully: {result}')
    data = screenshot.read_bytes()
    if data[:2] != b'BM' or struct.unpack_from('<ii', data, 18) != (1152, 720) or len(data) < 100000:
        raise RuntimeError(f'{kind} screenshot missing or malformed')
    print(result.strip())
briefing = run(['--frames', '1'])
if 'phase=0' not in briefing or 'time=0,' not in briefing:
    raise RuntimeError(f'Briefing advanced without start: {briefing}')
print('Interactive SDL: four demo successes, screenshots, frozen briefing and 7 rejected CLI cases passed')
