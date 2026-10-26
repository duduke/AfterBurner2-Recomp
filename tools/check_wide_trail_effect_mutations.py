#!/usr/bin/env python3
"""Reject detached player/enemy trails and damage-emission faults against retained independent MAME calls."""
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
LOGS = ROOT / 'build/oracle/widescreen/trail-effect-mutations'
MUTATIONS = [
    ('player-speed', 'b.read16(0x29c0ac)', 'b.read16(0x29c0aa)'),
    ('player-false-pause', 'b.write32(p+8,b.read32(p+4));w(d[1],b.read16(0x29c0ac));', 'b.write32(p+8,b.read32(p+4));if(b.read8(0xffff91d0))return State::visible;w(d[1],b.read16(0x29c0ac));'),
    ('player-divisor', 'b.write16(m+4,d[2]);\n    b.write16(m-0x4000', 'b.write16(m+4,d[2]+32);\n    b.write16(m-0x4000'),
    ('ramp-cap', 'if(s(b.read16(p+50))<=7)', 'if(true)'),
    ('enemy-enable', 'if(!(b.read8(0x29c200)&2))', 'if(false)'),
    ('enemy-age', '>=0x24)return State::retired', '>0x24)return State::retired'),
    ('enemy-taper', 'if(s(d[1])>=30)', 'if(s(d[1])>=31)'),
    ('enemy-ring-write', 'w(d[0],b.read16(p+44));if(s(d[0])>15)', 'b.write8(0xffff9162,b.read8(0xffff9162)-1);w(d[0],b.read16(p+44));if(s(d[0])>15)'),
    ('damage-depth-rate', '==4?1:3', '==4?3:1'),
    ('damage-sound-state', 'b.write16(p+30,0);r.a[0]=0x2bc34;', 'b.write8(0xffff9118,b.read8(0xffff9118)+1);b.write16(p+30,0);r.a[0]=0x2bc34;'),
    ('damage-timer-overflow', '(old==0x8000)', 'false'),
    ('damage-frame-scale', 'd[0]=uint16_t(d[0])*b.read16(p+34);asrl(d[0],7);', 'd[0]=uint16_t(d[0])*b.read16(p+34);asrl(d[0],6);'),
    ('hidden-retirement', 'return State::hidden;', 'return State::retired;'),
]

LOGS.mkdir(parents=True, exist_ok=True)
subprocess.run(['make', 'build/oracle_world_visibility'], cwd=ROOT, check=True, capture_output=True)
capture = 'build/oracle/wide_trail_effects_directed.bin'
baseline = subprocess.run(['./build/oracle_world_visibility', capture], cwd=ROOT, capture_output=True, text=True)
(LOGS / 'baseline.log').write_text(baseline.stdout + baseline.stderr)
assert baseline.returncode == 0, baseline.stdout + baseline.stderr
dry = subprocess.check_output(['make', '-n', '-B', 'build/oracle_world_visibility'], cwd=ROOT, text=True)
command = next(shlex.split(line) for line in dry.splitlines() if ' -o build/oracle_world_visibility' in line)
for name, old, new in MUTATIONS:
    with tempfile.TemporaryDirectory(prefix='wide-trail-effect-', dir=ROOT / 'build/oracle') as tmp:
        overlay = Path(tmp)
        (overlay / 'hwvideo').mkdir()
        source = (ROOT / 'src/main/hwvideo/wide_trail_effect.hpp').read_text()
        assert source.count(old) > 0, name
        (overlay / 'hwvideo/wide_trail_effect.hpp').write_text(source.replace(old, new))
        build = list(command)
        build[1:1] = ['-I' + str(overlay)]
        binary = overlay / name
        build[build.index('-o') + 1] = str(binary)
        with (LOGS / (name + '-build.log')).open('w') as log:
            subprocess.run(build, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, check=True)
        rejected = False
        for fixture in [capture, 'build/oracle/missiles_directed.bin', 'build/oracle/enemy_effects.bin', 'build/oracle/companion_directed.bin', 'build/oracle/companion_gameplay.bin']:
            result = subprocess.run([str(binary), fixture], cwd=ROOT, capture_output=True, text=True)
            output = result.stdout + result.stderr
            (LOGS / (name + '-' + Path(fixture).stem + '-run.log')).write_text(output)
            if result.returncode == 1 and any(witness in output for witness in [
                'detached trail-effect lifetime differs from MAME',
                'detached trail-effect evolution differs from MAME',
                'detached trail effect wrote outside its object']):
                rejected = True
                break
            assert result.returncode == 0, (name, output[-2000:])
        assert rejected, (name, 'survived all retained independent captures')
        print('Wide trail-effect MAME mutation ' + name + ': rejected (' + fixture + ')', flush=True)
