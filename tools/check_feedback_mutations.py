#!/usr/bin/env python3
"""Retained negative controls for the passive feedback tap and SDL mixer."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MUTATIONS = [
    ('wrong-cannon-producer', 'engine/feedback_cues.hpp', 'p.pc==0x9ef6', 'p.pc==0x9ef8', 'fixture'),
    ('wrong-missile-producer', 'engine/feedback_cues.hpp', 'p.pc==0x9ebc', 'p.pc==0x9eba', 'fixture'),
    ('wrong-player-impact', 'engine/feedback_cues.hpp', 'p.pc==0x9c20', 'p.pc==0x9c22', 'fixture'),
    ('attract-rumble', 'engine/feedback_cues.hpp', '!flight || p.cpu', 'p.cpu', 'unit'),
    ('early-afterburner', 'engine/feedback_cues.hpp', '>=0xfd', '>=0xfc', 'unit'),
    ('overlap-cancellation', 'frontend/native_haptics.hpp', 'std::max(v[c],strength[i][c]*envelope)', 'strength[i][c]*envelope', 'unit'),
    ('repeated-frame-pulse', 'frontend/native_haptics.hpp', 'f.number<=last_frame', 'f.number<last_frame', 'unit'),
    ('missing-explicit-stop', 'frontend/native_haptics.hpp', 'send({},0);', '/* missing stop */', 'unit'),
    ('trigger-crosswire', 'frontend/native_haptics.hpp', 'SDL_RumbleGamepadTriggers(pad,v[2],v[3],ms)', 'SDL_RumbleGamepadTriggers(pad,v[3],v[2],ms)', 'unit'),
    ('lost-reenable-events', 'frontend/native_haptics.hpp', 'settings.enabled=!settings.enabled;mixer.restart();', 'settings.enabled=!settings.enabled;', 'unit'),
    ('missing-watchdog', 'frontend/native_haptics.hpp', 'now-fresh>=100', 'false', 'unit'),
]

def main():
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl3'], text=True).split()
    logs = ROOT / 'build/haptics/mutations'
    logs.mkdir(parents=True, exist_ok=True)
    for name, header, before, after, suite in MUTATIONS:
        source = (ROOT / 'src/main' / header).read_text()
        if source.count(before) != 1:
            raise RuntimeError(f'{name}: mutation anchor is not unique')
        with tempfile.TemporaryDirectory(prefix='ab2-feedback-') as tmp:
            overlay = Path(tmp)
            target = overlay / header
            target.parent.mkdir(parents=True)
            target.write_text(source.replace(before, after))
            test = 'tests/feedback_fixture.cpp' if suite == 'fixture' else 'tests/native_haptics_regression.cpp'
            command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-I'+str(overlay), '-Isrc/main', test]
            if suite == 'fixture':
                command += ['src/main/engine/mainbus.cpp', 'src/main/engine/subbus.cpp']
            executable = overlay / 'check'
            command += ['-o', str(executable), *flags]
            with (logs / (name+'.log')).open('w') as log:
                subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
                run = [str(executable)] + (['build/oracle/feedback.bin'] if suite == 'fixture' else [])
                result = subprocess.run(run, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            if result.returncode == 0:
                raise RuntimeError(f'{name}: mutation survived')
            print(f'PASS rejected {name}', flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())
