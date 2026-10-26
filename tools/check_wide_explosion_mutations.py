#!/usr/bin/env python3
"""Reject detached world-explosion faults against retained independent MAME calls."""
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LOGS = ROOT / 'build/oracle/widescreen/explosion-mutations'
MUTATIONS = [
    ('pause', 'if(b.read8(0xffff91d0)&1)', 'if(false)'),
    ('age', 'b.read16(p+38)+1', 'b.read16(p+38)+2'),
    ('double-sway', 'sway(b,r); r.a[0]', 'r.a[0]'),
    ('hidden-top', 'return State::hidden;', 'return State::retired;'),
    ('timer', 'b.write16(p+48,old-1)', 'b.write16(p+48,old-2)'),
]

LOGS.mkdir(parents=True, exist_ok=True)
subprocess.run(['make', 'build/oracle_world_visibility'], cwd=ROOT, check=True, capture_output=True)
capture = 'build/oracle/wide_explosions_directed.bin'
baseline = subprocess.run(['./build/oracle_world_visibility', capture], cwd=ROOT, capture_output=True, text=True)
(LOGS / 'baseline.log').write_text(baseline.stdout + baseline.stderr)
assert baseline.returncode == 0, baseline.stdout + baseline.stderr
dry = subprocess.check_output(['make', '-n', '-B', 'build/oracle_world_visibility'], cwd=ROOT, text=True)
command = next(shlex.split(line) for line in dry.splitlines() if ' -o build/oracle_world_visibility' in line)
for name, old, new in MUTATIONS:
    with tempfile.TemporaryDirectory(prefix='wide-explosion-', dir=ROOT / 'build/oracle') as tmp:
        overlay = Path(tmp)
        (overlay / 'hwvideo').mkdir()
        source = (ROOT / 'src/main/hwvideo/wide_explosion.hpp').read_text()
        assert source.count(old) == 1, name
        (overlay / 'hwvideo/wide_explosion.hpp').write_text(source.replace(old, new))
        build = list(command)
        build[1:1] = ['-I' + str(overlay)]
        binary = overlay / name
        build[build.index('-o') + 1] = str(binary)
        with (LOGS / (name + '-build.log')).open('w') as log:
            subprocess.run(build, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        result = subprocess.run([str(binary), capture], cwd=ROOT, capture_output=True, text=True)
        output = result.stdout + result.stderr
        (LOGS / (name + '-run.log')).write_text(output)
        assert result.returncode == 1 and ('detached explosion lifetime differs from MAME' in output or
                                        'detached explosion evolution differs from MAME' in output), (name, output[-2000:])
        print('Wide explosion MAME mutation ' + name + ': rejected', flush=True)
