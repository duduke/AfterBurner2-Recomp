#!/usr/bin/env python3
"""Prove GPU/reference comparison rejects wrong filter and priority protection."""
from pathlib import Path
import os
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
header=(ROOT/'src/main/frontend/gpu_sprite_filter.hpp').read_text()
mutations={
    'priority protection':('protection.empty()?0:protection[i]','0'),
    'filter selection':('int32_t(filter),int32_t(scale)','int32_t(SpriteFilter::epx),int32_t(scale)'),
    'cloud opacity':('int32_t(cloud_alpha),int32_t(trail_alpha)','255,int32_t(trail_alpha)'),
    'explosion opacity':('int32_t(explosion_alpha),int32_t(afterburner_alpha)','255,int32_t(afterburner_alpha)'),
    'afterburner opacity':('int32_t(explosion_alpha),int32_t(afterburner_alpha)','int32_t(explosion_alpha),255'),
    'trail opacity':('int32_t(cloud_alpha),int32_t(trail_alpha)','int32_t(cloud_alpha),255'),
}
flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','sdl3'],text=True))
for name,(before,after) in mutations.items():
    if header.count(before)!=1:raise RuntimeError(f'Ambiguous mutation: {name}')
    with tempfile.TemporaryDirectory(prefix='ab2-gpu-mutation-') as temporary:
        path=Path(temporary);(path/'frontend').mkdir()
        (path/'frontend/gpu_sprite_filter.hpp').write_text(header.replace(before,after))
        exe=path/'check'
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(path),'-I'+str(ROOT/'src/main'),
                        str(ROOT/'tests/gpu_sprite_filter_regression.cpp'),'-o',str(exe),*flags],check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        if result.returncode==0 or 'GPU scaler differs from CPU reference' not in result.stderr:
            raise RuntimeError(f'{name} was not rejected by pixel comparison: {result.stdout}\n{result.stderr}')
        print(f'GPU mutation passed: {name} rejected by pixel comparison')
