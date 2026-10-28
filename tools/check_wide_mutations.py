#!/usr/bin/env python3
"""Retained negative controls for the render-only wide geometry contract."""
from pathlib import Path
import os
import shutil
import subprocess
from datetime import datetime, timezone
import shlex

ROOT = Path(__file__).resolve().parents[1]
MUTATIONS = [
    ('flight-only-ground', 'wide_scene.hpp', 'if(corrected)ground.apply', 'if(corrected && c.mode==0x10)ground.apply',
     'non-flight outer sky-fill missing', 1),
    ('narrow-projection', 'wide_projection.hpp', 'if (s(d6) >= 0x200+margin)', 'if (s(d6) >= 0x200)',
     'native-hidden sprite projection did not recover both wide edges', 1),
    ('wrapped-anchor', 'sprite_scene.hpp', 'xs=anchors[slot];', 'xs=xs;',
     'native-hidden sprite projection did not recover both wide edges', 1),
    ('sprite-edge', 'sprite_scene.hpp', 'x>=clip_left&&x<=clip_right', 'x>=0&&x<=319',
     'sprite disappeared at a native edge instead of a wide edge', 2),  # unit and scaled decoder paths
    ('road-origin', 'hwroad.cpp', ') & 0xfff) + native_left;', ') & 0xfff);',
     'wide road origin changed the native center', 2),
    ('tile-origin', 'hwtiles.cpp', 'static_cast<uint32_t>(x + native_left)', 'static_cast<uint32_t>(x)',
     'wide side column/source wrap differs', 1),
    ('column-wrap', 'hwtiles.cpp', '((x + native_left + 8) & 511)', '(std::max(0,x+native_left+8))',
     'wide side column/source wrap differs', 1),
    ('clip-palette', 'wide_scene.hpp', 'for(unsigned i=c.top*w;i<(c.bottom+1)*w;++i){\n            background[i]',
     'for(unsigned i=0;i<(c.bottom+1)*w;++i){\n            background[i]',
     'wide renderer lost partial-frontier palette/row ownership', 1),
    ('native-feedback', 'wide_scene.hpp', 'void draw(const SceneClip& c,bool corrected,bool extend_visibility,bool original_hud,const SceneClip* prev=nullptr,const Screen::ViewportLayers* primary=nullptr){',
     'void draw(const SceneClip& c,bool corrected,bool extend_visibility,bool original_hud,const SceneClip* prev=nullptr,const Screen::ViewportLayers* primary=nullptr){const_cast<SceneClip&>(c).sprites[15]^=1;',
     'wide renderer wrote native sprite feedback', 1),
]


def main():
    flags = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'sdl3'], text=True).split()
    logs = ROOT / 'build/oracle/widescreen/mutations' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    inputs = logs / 'inputs'
    shutil.copytree(ROOT / 'src/main', inputs / 'src/main', ignore=shutil.ignore_patterns('*.o', '*.d'))
    # Preserve the embedded ZIP reader alongside the frozen loader source.
    shutil.copytree(ROOT / 'src/third_party/miniz', inputs / 'src/third_party/miniz')
    (inputs / 'tests').mkdir()
    for name in ('wide_scene_geometry.cpp', 'wide_ground_reference.hpp'):
        shutil.copy2(ROOT / 'tests' / name, inputs / 'tests' / name)
    # Validate every anchor before compiling; never silently omit a control.
    for name, file, before, after, witness, count in MUTATIONS:
        if (inputs / 'src/main/hwvideo' / file).read_text().count(before) != count:
            raise RuntimeError(f'{name}: mutation anchor changed')

    for variant in [None, *MUTATIONS]:
        name = variant[0] if variant else 'baseline'
        overlay = logs / name
        shutil.copytree(inputs / 'src/main/hwvideo', overlay / 'hwvideo')
        if variant:
            _, file, before, after, witness, count = variant
            target = overlay / 'hwvideo' / file
            target.write_text(target.read_text().replace(before, after))
        binary = overlay / 'geometry'
        cmd = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-I'+str(overlay), '-I'+str(inputs / 'src/main'),
               str(inputs / 'tests/wide_scene_geometry.cpp')]
        cmd += [str(overlay / 'hwvideo' / p) for p in ['hwroad.cpp', 'hwtiles.cpp', 'hwpalette.cpp']]
        cmd += [str(inputs / 'src/main' / p) for p in ('engine/subbus.cpp', 'roms.cpp', 'romloader.cpp')]
        cmd += ['-o', str(binary)] + flags
        with (overlay / 'build.log').open('w') as log:
            log.write(shlex.join(cmd)+'\n');log.flush()
            subprocess.run(cmd, cwd=ROOT, check=True, stdout=log, stderr=subprocess.STDOUT)
        for width in (398, 358):
            result = subprocess.run([str(binary), '--width', str(width)], cwd=ROOT, capture_output=True, text=True)
            output = result.stdout + result.stderr
            (overlay / f'run-{width}.log').write_text(output)
            if not variant:
                if result.returncode or f'Wide geometry {width}x224:' not in output or 'immutable feedback passed' not in output:
                    raise RuntimeError(f'Original wide geometry baseline ({width}) failed: {overlay}')
                print(f'Wide geometry baseline ({width}): passed; retained at {overlay}', flush=True)
            else:
                if result.returncode != 1 or witness not in output:
                    raise RuntimeError(f'{name} ({width}): expected semantic rejection missing; {overlay}')
                print(f'Wide mutation {name} ({width}): rejected by independent geometry witness', flush=True)
    print(f'Wide geometry mutations: {2*len(MUTATIONS)} of {2*len(MUTATIONS)} rejected, 0 skipped; sources/binaries/logs retained at {logs}')


if __name__ == '__main__':
    main()
