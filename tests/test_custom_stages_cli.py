"""Exercise custom authoring and both presentations without touching arcade saves."""
import os
from pathlib import Path
import struct
import re
import subprocess
import sys
ROOT = Path(__file__).resolve().parents[1]
binary = (ROOT / sys.argv[1]).resolve()
out = ROOT / 'build/oracle/custom-stages'
out.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
names = ('refuel', 'landing', 'runway', 'runway-jungle', 'runway-takeoff', 'carrier-takeoff', 'jungle-takeoff')
def run(args, good=True):
    p = subprocess.run([str(binary), '--seed', '123', *args], cwd=ROOT, env=env, text=True, capture_output=True, timeout=180)
    if (p.returncode == 0) != good:
        raise RuntimeError(f'{args}: {p.stdout}\n{p.stderr}')
    return p.stdout + p.stderr

def pixels(path, top=220):
    b = path.read_bytes()
    offset = struct.unpack_from('<I', b, 10)[0]
    w, h = struct.unpack_from('<ii', b, 18)
    bits = struct.unpack_from('<H', b, 28)[0]
    assert (w, abs(h)) == (1152, 720) and bits in (24, 32)
    stride = ((w * bits + 31) // 32) * 4
    # World area only: changed HUD wording cannot pass the artwork check.
    return b''.join(b[offset + (719-y if h > 0 else y)*stride + 100*(bits//8):
                       offset + (719-y if h > 0 else y)*stride + 1050*(bits//8)] for y in range(top, 550))

for name in names:
    plain = out / f'{name}-plain.bmp'
    result = run(['--no-sprites', '--stage', name, '--demo', '--frames', '3600', '--screenshot', str(plain)])
    assert 'phase=3' in result, result
    if name == 'refuel':
        assert 'coupled=1' in result, result
        assert float(re.search(r'probe_error_px=([0-9.e+-]+)', result)[1]) < .01, result
    assert len(pixels(plain)) > 1000
    print(result.strip())
# Screens at a shared in-flight time, before the result, compare all objectives.
if '--roms' in sys.argv:
    for name in names:
        a, b = (out / f'{name}-{suffix}.bmp' for suffix in ('geometry', 'rom'))
        run(['--no-sprites', '--stage', name, '--demo', '--frames', '120', '--screenshot', str(a)])
        result = run(['--roms', 'roms', '--stage', name, '--demo', '--frames', '120', '--screenshot', str(b)])
        assert 'game_font=1' in result and int(re.search(r'hud_tiles=(\d+)',result)[1])>40,result
        stats = dict((k, int(v)) for k, v in re.findall(r'(scenery_sprites|scenery_drawn|trees_drawn|waves_drawn|band_height|sky_aircraft|native_props|original_runway|carrier_art)=(\d+)', result))
        assert stats['band_height'] >= 3, result
        if name != 'refuel': assert stats['sky_aircraft'] == 0, result
        else: assert stats['sky_aircraft'] == 0 and stats['trees_drawn'] > 256, result
        if name in ('landing', 'carrier-takeoff'):
            assert stats['waves_drawn'] > 256 and stats['trees_drawn'] == 0, result
        elif name != 'refuel':
            assert stats['trees_drawn'] > 256 and stats['waves_drawn'] == 0, result
        assert stats['scenery_sprites'] > 0 and stats['scenery_drawn'] > 0, (name, result)
        if name in ('landing', 'carrier-takeoff'):
            assert stats['carrier_art'] == 1 and stats['original_runway'] == 0, result
        elif name != 'refuel':
            assert stats['original_runway'] > 0 and stats['carrier_art'] == 0, result
        data = pixels(b); bpp = struct.unpack_from('<H', b.read_bytes(), 28)[0] // 8
        assert sum(data[i:i+3] == bytes((82,85,79)) for i in range(0, len(data), bpp)) < 50, f'{name}: replacement gray pavement returned'
        hidden = out/f'{name}-without-scenery.bmp'
        hidden_result = run(['--roms', 'roms', '--stage', name, '--demo', '--frames', '120', '--no-scenery', '--screenshot', str(hidden)])
        assert 'scenery_drawn=0' in hidden_result, hidden_result
        assert sum(x != y for x, y in zip(pixels(hidden, 110), pixels(b, 110))) > 100, f'{name}: scenery had no visible effect'
        assert sum(x != y for x, y in zip(pixels(a), pixels(b))) > 10000, name
    result = run(['--roms', 'roms', '--stage', 'carrier-takeoff', '--demo-hold', '--frames', '180', '--screenshot', str(out/'carrier-ready.bmp')])
    assert 'phase=6' in result and 'burner=0' in result and 'speed=0' in result, result
    result = run(['--roms', 'roms', '--stage', 'refuel', '--demo', '--frames', '3600', '--screenshot', str(out/'physical-docking.bmp')])
    assert 'coupled=1' in result and float(re.search(r'probe_error_px=([0-9.e+-]+)',result)[1]) < .01, result
    for stage in ('runway', 'runway-jungle', 'landing'):
        result = run(['--roms', 'roms', '--stage', stage, '--demo-hold', '--frames', '3600', '--screenshot', str(out/f'{stage}-serviced-ready.bmp')])
        assert 'phase=6' in result and 'burner=0' in result and 'fuel=1' in result and 'missiles=100' in result, result
    service = run(['--roms', 'roms', '--stage', 'runway', '--demo', '--frames', '1600', '--screenshot', str(out/'runway-ground-service.bmp')])
    assert 'phase=9' in service and 'fuel=1' in service and 50 < int(re.search(r'missiles=(\d+)', service)[1]) < 100, service
    smoke = run(['--roms', 'roms', '--stage', 'runway', '--demo', '--frames', '1176', '--screenshot', str(out/'touchdown-smoke.bmp')])
    assert 'phase=5' in smoke and 'player_shadow=1' in smoke and int(re.search(r'smoke_puffs=(\d+)', smoke)[1]) > 0, smoke
    for surface,tick in [('landing',1026),('runway-jungle',1176)]:
        smoke=run(['--roms','roms','--stage',surface,'--demo','--frames',str(tick),'--screenshot',str(out/f'{surface}-tire-smoke.bmp')])
        assert 'phase=5' in smoke and 'player_shadow=1' in smoke and int(re.search(r'smoke_puffs=(\d+)',smoke)[1])>0,smoke
    sound = run(['--roms', 'roms', '--stage', 'runway', '--demo', '--frames', '2300', '--audio-wav', str(out/'rearm-departure.wav')])
    assert int(re.search(r'rom_audio_energy=(\d+)', sound)[1]) > 0, sound
    assert 'reload_sounds=50' in sound and 'launch_sounds=1' in sound and 'touchdown_sounds=1' in sound, sound
    import wave
    with wave.open(str(out/'rearm-departure.wav')) as recorded:
        assert (recorded.getframerate(), recorded.getnchannels(), recorded.getnframes()) == (48000, 2, 2300*800)
    for tick in (130, 134, 138, 142, 146):
        flame = run(['--roms', 'roms', '--stage', 'carrier-takeoff', '--demo', '--frames', str(tick), '--screenshot', str(out/f'fixed-flame-{tick}.bmp')])
        assert 'burner=1' in flame and float(re.search(r'flame_error_px=([0-9.e+-]+)', flame)[1]) < .001, flame
    run(['--roms', str(out/'missing-roms'), '--frames', '1'], False)
result = run(['--no-sprites', '--stage-file', 'assets/custom_stages/carrier-landing.json', '--demo', '--frames', '3600'])
assert 'phase=3' in result and 'CARRIER / LANDING' in result, result
for index, content in enumerate(('{"version":1,"objective":"unknown"}', '{"version":1,"width":NaN}', '{"version":1,"width":10}', '{"version":1,"width":45,"width":50}', '{"version":1,"mystery":1}', '{"version":1,"presentation":{"clouds":1}}', '{"version":1,"presentation":{"hud":1}}', '{"version":1,"speed":"50oops"}', '{"version":1,"length":-1}', '{"version":1,"environment":"sea","objective":"landing"}', 'width=45', '{}', '{"version":2}', '{"version":1,"seamless_takeoff":1}', '{"version":1} trailing')):
    path = out/f'invalid-{index}.json'; path.write_text(content)
    run(['--no-sprites', '--stage-file', str(path), '--frames', '1'], False)
for args in (['--stage','bogus'], ['--frames','0'], ['--frames','2oops'], ['--screenshot','x.bmp'], ['--roms','roms','--no-sprites'], ['--seed','bad'], ['--unknown','x']):
    run(args, False)
print('Custom stages: seven completions; seamless service holds, physical docking, five attached flame frames, original runway, carrier isolation, scenery and CLI rejection passed')
