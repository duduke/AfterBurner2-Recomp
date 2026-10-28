#!/usr/bin/env python3
"""Retained negative controls for host pacing, independent of native clocks."""
from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
destination = ROOT/'build/oracle/frame-pacing-mutations'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
source = (ROOT/'src/main/frontend/frame_pacing.hpp').read_text()
regression = (ROOT/'tests/frame_pacing_regression.cpp').read_text()
mutations = {
    'discard-ordinary-stalls': ('period*16.0', 'period*4.0', 'frame pacing discarded ordinary deadline debt'),
    'unbounded-suspension': ('std::min(period*16.0,accumulated+seconds)', 'accumulated+seconds', 'frame pacing suspension catch-up is unbounded'),
    'forget-wait-time': ('void waited(double seconds){accumulated+=seconds;}', 'void waited(double){ }', 'frame pacing discarded ordinary deadline debt'),
}
for name, change in [('baseline', None), *mutations.items()]:
    folder = destination/name
    (folder/'frontend').mkdir(parents=True)
    body = source
    if change:
        before, after, _ = change
        assert body.count(before) == 1, name
        body = body.replace(before, after)
    (folder/'frontend/frame_pacing.hpp').write_text(body)
    (folder/'regression.cpp').write_text(regression)
    binary = folder/('check.exe' if os.name == 'nt' else 'check')
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-I'+str(folder), str(folder/'regression.cpp'), '-o', str(binary)]
    with (folder/'build.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    assert result.returncode == 0, folder/'build.log'
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
    (folder/'run.log').write_text(result.stdout+result.stderr)
    assert (result.returncode == 0 if change is None else result.returncode == 1 and change[2] in result.stderr), folder/'run.log'
    print(f'PASS {name}: {folder}', flush=True)
