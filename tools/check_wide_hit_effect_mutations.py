#!/usr/bin/env python3
"""Reject detached ground/air hit-effect faults against retained independent MAME calls."""
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LOGS = ROOT / 'build/oracle/widescreen/hit-effect-mutations'
MUTATIONS = [
    ('pause', 'if(b.read8(0xffff91d0)&1)', 'if(false)'),
    ('age-cap', 'if(s(d[2])>4)w(d[2],4)', 'if(s(d[2])>3)w(d[2],3)'),
    ('air-ground-drift', 'if(ground&&b.read16(0x29c33c))', 'if(b.read16(0x29c33c))'),
    ('missing-sway', 'else sway(b,r);', 'else (void)0;'),
    ('animation-parity', 'if(!uint16_t(d[0]))', 'if(uint16_t(d[0]))'),
    ('frame-limit', 'if(uint8_t(d[0])!=b.read8(p+47))', 'if(true)'),
    ('palette-direction-order', 'b.read8(r.a[0]+s(d[0])+12)', 'b.read8(r.a[0]+s(d[0])+b.read16(0x29c0a4)+12)'),
    ('hidden-retirement', 'return State::hidden;', 'return State::retired;'),
]

LOGS.mkdir(parents=True, exist_ok=True)
subprocess.run(['make', 'build/oracle_world_visibility'], cwd=ROOT, check=True, capture_output=True)
capture = 'build/oracle/wide_hit_effects_directed.bin'
baseline = subprocess.run(['./build/oracle_world_visibility', capture], cwd=ROOT, capture_output=True, text=True)
(LOGS / 'baseline.log').write_text(baseline.stdout + baseline.stderr)
assert baseline.returncode == 0, baseline.stdout + baseline.stderr
dry = subprocess.check_output(['make', '-n', '-B', 'build/oracle_world_visibility'], cwd=ROOT, text=True)
command = next(shlex.split(line) for line in dry.splitlines() if ' -o build/oracle_world_visibility' in line)
for name, old, new in MUTATIONS:
    with tempfile.TemporaryDirectory(prefix='wide-hit-effect-', dir=ROOT / 'build/oracle') as tmp:
        overlay = Path(tmp)
        (overlay / 'hwvideo').mkdir()
        source = (ROOT / 'src/main/hwvideo/wide_hit_effect.hpp').read_text()
        assert source.count(old) == 1, name
        (overlay / 'hwvideo/wide_hit_effect.hpp').write_text(source.replace(old, new))
        build = list(command)
        build[1:1] = ['-I' + str(overlay)]
        binary = overlay / name
        build[build.index('-o') + 1] = str(binary)
        with (LOGS / (name + '-build.log')).open('w') as log:
            subprocess.run(build, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        result = subprocess.run([str(binary), capture], cwd=ROOT, capture_output=True, text=True)
        output = result.stdout + result.stderr
        (LOGS / (name + '-run.log')).write_text(output)
        assert result.returncode == 1 and ('detached hit-effect lifetime differs from MAME' in output or
                                        'detached hit-effect evolution differs from MAME' in output), (name, output[-2000:])
        print('Wide hit-effect MAME mutation ' + name + ': rejected', flush=True)
