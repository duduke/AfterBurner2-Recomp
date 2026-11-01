#!/usr/bin/env python3
"""Retained negative checks for the Android input/storage adapter."""
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
cases = [
    ('vertical-sign', 'native_touch.hpp', 'out.stick_y=y*gain;', 'out.stick_y=-y*gain;'),
    ('background-stuck-input', 'native_touch.hpp', 'e.type==SDL_EVENT_WILL_ENTER_BACKGROUND', 'false'),
    ('weapon-drop', 'native_touch.hpp', 'case cannon:out.cannon=true;', 'case cannon:out.cannon=false;'),
    ('throttle-sign', 'native_touch.hpp', '(targets[throttle].y-f.y)/throttle_half', '(f.y-targets[throttle].y)/throttle_half'),
    ('slider-cancel-stuck', 'native_touch.hpp', 'e.type==SDL_EVENT_FINGER_CANCELED', 'false'),
    ('square-buttons', 'native_touch.hpp', 'std::hypot(dx,dy)<=targets[c].radius', 'std::max(std::abs(dx),std::abs(dy))<=targets[c].radius'),
    ('hide-held-input', 'native_touch.hpp', 'if(c==visibility){clear();', 'if(c==visibility){'),
    ('hidden-input', 'native_touch.hpp', 'c==none || !show_controls', 'c==none'),
    ('hidden-draw', 'native_touch.hpp', 'if(!show_controls && c!=visibility)continue;', ''),
    ('unbounded-import', 'native_rom_picker.hpp', 'total>64*1024*1024', 'false'),
]
flags = shlex.split(subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl3'], text=True))
compiler = shlex.split(os.environ.get('CXX', 'c++'))
directory = Path(tempfile.mkdtemp(prefix='touch-mutants-', dir=ROOT/'build'))
for name, header, before, after in [('baseline', None, '', ''), *cases]:
    includes = directory/name/'frontend'; includes.mkdir(parents=True)
    if header:
        source = (ROOT/'src/main/frontend'/header).read_text()
        if source.count(before) != 1: raise RuntimeError(f'{name}: mutation anchor drifted')
        (includes/header).write_text(source.replace(before, after))
    executable = directory/name/'test'
    subprocess.run([*compiler, '-std=c++20', '-O2', '-I'+str(includes.parent), '-I'+str(ROOT/'src/main'),
                    str(ROOT/'tests/native_touch_regression.cpp'), '-o', str(executable), *flags], check=True)
    result = subprocess.run([str(executable)], env=dict(os.environ, SDL_VIDEODRIVER='dummy'), capture_output=True, text=True)
    (directory/name/'run.log').write_text(result.stdout+result.stderr+f'\nexit_code={result.returncode}\n')
    if (result.returncode == 0) != (name == 'baseline'):
        raise RuntimeError(f'{name}: unexpected result\n{result.stdout}{result.stderr}')
    print(f'{name}: {"passed" if name == "baseline" else "rejected"}')
print(f'Touch mutation sources/binaries/logs retained at {directory}')
