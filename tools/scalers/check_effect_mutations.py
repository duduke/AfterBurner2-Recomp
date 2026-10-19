#!/usr/bin/env python3
"""Retained negative checks for passive effect classification and throttle policy."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
header = (ROOT / 'src/main/hwvideo/sprite_scene.hpp').read_text()
mutations = {
    'opaque exhaust pixels': ('emit(x,unsigned(y),raw_pixel,kind);', 'emit(x,unsigned(y),raw_pixel,kind==Effect::afterburner?Effect::opaque:kind);'),
    'wrong exhaust palette': ('Effect::afterburner,0x80', 'Effect::afterburner,0xf2'),
    'ordinary-throttle flame': ('mode==0x10 && int16_t(throttle)>=0xfd', 'mode==0x10'),
    'throttle endpoint': ('int16_t(throttle)>=0xfd', 'int16_t(throttle)>=0xfc'),
    'attract flame': ('mode==0x10 && int16_t(throttle)>=0xfd', 'int16_t(throttle)>=0xfd'),
    'missing crash fire': ('color==0xa2 || color==0x75 || color==0x2c', 'color==0xa2 || color==0x2c'),
    'translucent aircraft debris': ('color==0xa2 || color==0x75 || color==0x2c', 'color==0xa2 || color==0x75 || color==0x2c || color==0x15'),
}
for name, (before, after) in mutations.items():
    if header.count(before) != 1:
        raise RuntimeError(f'Ambiguous mutation: {name}')
    with tempfile.TemporaryDirectory(prefix='ab2-effect-mutation-') as temporary:
        path = Path(temporary)
        (path / 'hwvideo').mkdir()
        (path / 'hwvideo/sprite_scene.hpp').write_text(header.replace(before, after))
        exe = path / 'check'
        subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2',
                        '-I' + str(path), '-I' + str(ROOT / 'src/main'),
                        str(ROOT / 'tests/effect_assets_integration.cpp'),
                        str(ROOT / 'src/main/roms.cpp'), str(ROOT / 'src/main/romloader.cpp'),
                        '-o', str(exe)], check=True)
        result = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
        if result.returncode == 0 or not any(message in result.stderr for message in (
                'ROM effect descriptor classification differs', 'full-throttle flame predicate differs', 'exhaust pixel classification differs')):
            raise RuntimeError(f'{name} was not rejected by comparison: {result.stdout}\n{result.stderr}')
        print(f'Effect mutation passed: {name} rejected by comparison')
