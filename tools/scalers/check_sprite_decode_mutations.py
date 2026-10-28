#!/usr/bin/env python3
"""Retained faults for the passive decoder's unit-horizontal-zoom path."""
from datetime import datetime, timezone
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
destination = ROOT/'build/oracle/sprite-decode-mutations'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
source = (ROOT/'src/main/hwvideo/sprite_scene.hpp').read_text()
start = source.index('                if(hz==512){')
end = source.index('                if(!word){while(acc<4096)', start)
shortcut = source[start:end]
mutations = {
    'neighbor-zoom': ('if(hz==512)', 'if(hz==512||hz==511)'),
    'empty-word-step': ('x+=xd*8', 'x+=xd*7'),
    'pen-direction': ('flip?k*4:(7-k)*4', 'flip?(7-k)*4:k*4'),
    'terminal-position': ('if(k==6)', 'if(k==7)'),
    'lose-eighth-pen': ('k<8', 'k<7'),
    'transparent-pen': ('pen!=0&&pen!=15', 'pen!=15'),
    'left-clip': ('x>=clip_left', 'x>clip_left'),
    'right-clip': ('x<=clip_right', 'x<clip_right'),
}
for name, change in [('baseline', None), *mutations.items()]:
    folder = destination/name
    (folder/'hwvideo').mkdir(parents=True)
    body = source
    if change:
        before, after = change
        assert shortcut.count(before) == 1, name
        body = source[:start]+shortcut.replace(before, after)+source[end:]
    (folder/'hwvideo/sprite_scene.hpp').write_text(body)
    binary = folder/('check.exe' if os.name == 'nt' else 'check')
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-I'+str(folder),
               '-I'+str(ROOT/'src/main'), '-I'+str(ROOT/'tests'),
               str(ROOT/'tests/sprite_decode_regression.cpp'), '-o', str(binary)]
    with (folder/'build.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    assert result.returncode == 0, folder/'build.log'
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
    (folder/'run.log').write_text(result.stdout+result.stderr)
    assert (result.returncode == 0 if change is None else
            result.returncode == 1 and 'sprite decoder output/order/source changed' in result.stderr), folder/'run.log'
    print(f'PASS {name}: {folder}', flush=True)
