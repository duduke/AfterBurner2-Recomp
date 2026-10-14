#!/usr/bin/env python3
"""Retained negative controls for the render-only wide geometry contract."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
MUTATIONS = [
    ('sprite-edge', 'sprite_scene.hpp', 'x>=clip_left&&x<=clip_right', 'x>=0&&x<=319',
     'sprite disappeared at a native edge instead of a wide edge', 1),
    ('road-origin', 'hwroad.cpp', ') & 0xfff) + native_left;', ') & 0xfff);',
     'wide road origin changed the native center', 2),
    ('tile-origin', 'hwtiles.cpp', 'static_cast<uint32_t>(x+native_left)', 'static_cast<uint32_t>(x)',
     'wide side column/source wrap differs', 1),
    ('column-wrap', 'hwtiles.cpp', '((x+native_left+8)&511)', '(std::max(0,x+native_left+8))',
     'wide side column/source wrap differs', 1),
    ('clip-palette', 'wide_scene.hpp', 'for(unsigned i=c.top*w;i<(c.bottom+1)*w;++i){\n            background[i]',
     'for(unsigned i=0;i<(c.bottom+1)*w;++i){\n            background[i]',
     'wide renderer lost partial-frontier palette/row ownership', 1),
    ('native-feedback', 'wide_scene.hpp', 'void draw(const SceneClip& c,bool corrected){',
     'void draw(const SceneClip& c,bool corrected){const_cast<SceneClip&>(c).sprites[15]^=1;',
     'wide renderer wrote native sprite feedback', 1),
]


def main():
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl3'], text=True).split()
    logs = ROOT / 'build/oracle/widescreen/mutations'
    logs.mkdir(parents=True, exist_ok=True)
    for name, file, before, after, witness, count in MUTATIONS:
        with tempfile.TemporaryDirectory(prefix='ab2-wide-') as tmp:
            overlay = Path(tmp)
            shutil.copytree(ROOT / 'src/main/hwvideo', overlay / 'hwvideo')
            target = overlay / 'hwvideo' / file
            source = target.read_text()
            if source.count(before) != count:
                raise RuntimeError(f'{name}: mutation anchor changed')
            target.write_text(source.replace(before, after))
            binary = overlay / 'geometry'
            cmd = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-I'+str(overlay), '-Isrc/main',
                   'tests/wide_scene_geometry.cpp']
            cmd += [str(overlay / 'hwvideo' / p) for p in ['hwroad.cpp', 'hwtiles.cpp', 'hwpalette.cpp']]
            cmd += ['src/main/roms.cpp', 'src/main/romloader.cpp', '-o', str(binary)] + flags
            with (logs / f'{name}-build.log').open('w') as log:
                subprocess.run(cmd, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
            result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True)
            output = result.stdout + result.stderr
            (logs / f'{name}-run.log').write_text(output)
            if result.returncode != 1 or witness not in output:
                raise RuntimeError(f'{name}: expected semantic rejection missing')
            print(f'Wide mutation {name}: rejected by independent geometry witness', flush=True)


if __name__ == '__main__':
    main()
