#!/usr/bin/env python3
"""Prove the scene comparison rejects a changed ROM row-address step."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
(root / 'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='threejs-mutation-', dir=root / 'build') as directory:
    overlay = Path(directory)
    (overlay / 'tools').mkdir()
    (overlay / 'tests').mkdir()
    source = (root / 'tools/threejs_scene.hpp').read_text()
    needle = '(row*vz)>>9'
    if source.count(needle) != 1:
        raise SystemExit('Scene mutation site changed')
    (overlay / 'tools/threejs_scene.hpp').write_text(source.replace(needle, '(row*vz)>>8'))
    shutil.copy(root / 'tests/threejs_scene_regression.cpp', overlay / 'tests/test.cpp')
    binary = overlay / 'mutant'
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-I' + str(root / 'src/main'),
                    str(overlay / 'tests/test.cpp'), str(root / 'src/main/hwvideo/hwsprites.cpp'), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True)
    if result.returncode == 0 or 'scene source/transform differs from native hardware sprite scaler' not in result.stderr:
        raise SystemExit('Scene mutation did not fail the native comparison: ' + result.stderr)
    print('Three.js scene mutation passed: wrong vertical ROM row step rejected by native sprite comparison.')
