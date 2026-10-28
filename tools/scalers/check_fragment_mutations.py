#!/usr/bin/env python3
"""Retain negative controls for terminal sprite-fragment compaction."""
from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
destination = ROOT / 'build/oracle/fragment-mutations' / datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
header = (ROOT / 'src/main/hwvideo/sprite_scene.hpp').read_text()
regression = (ROOT / 'tests/sprite_filter_regression.cpp').read_text()
mutations = {
    'discard translucent predecessors': ('fragment.kind==0||fragment.kind==3||fragment.kind==4', 'true'),
    'lose shadow background': ('fragment.previous=UINT32_MAX;row[head]=fragment;', 'fragment.previous=UINT32_MAX;fragment.kind=0;row[head]=fragment;'),
    'overwrite another pixel': ('row[head]=fragment;', 'row[0]=fragment;'),
}
for name, change in [('baseline', None), *mutations.items()]:
    folder = destination / name
    (folder / 'hwvideo').mkdir(parents=True)
    body = header
    if change:
        before, after = change
        assert body.count(before) == 1, name
        body = body.replace(before, after)
    (folder / 'hwvideo/sprite_scene.hpp').write_text(body)
    (folder / 'regression.cpp').write_text(regression)
    binary = folder / ('check.exe' if os.name == 'nt' else 'check')
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2',
               '-I'+str(folder), '-I'+str(ROOT/'src/main'), '-I'+str(ROOT/'tests/scalers_reference'),
               str(folder/'regression.cpp'), '-o', str(binary)]
    with (folder/'build.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    assert result.returncode == 0, folder/'build.log'
    result = subprocess.run([str(binary), '--fragments-only'], capture_output=True, text=True, timeout=60)
    (folder/'run.log').write_text(result.stdout+result.stderr)
    assert (result.returncode == 0 if change is None else
            result.returncode == 1 and 'terminal fragment compaction' in result.stderr), folder/'run.log'
    print(f'PASS {name}: {folder}', flush=True)
