#!/usr/bin/env python3
"""Retain and reject SDL playback priming/starvation faults without hardware."""
from datetime import datetime, timezone
import os
from pathlib import Path
import shlex
import subprocess

ROOT = Path(__file__).resolve().parents[2]
destination = ROOT/'build/oracle/sdl-recovery-mutations'/datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
source = (ROOT/'src/main/frontend/audio_output.hpp').read_text()
regression = (ROOT/'tests/spatial_audio_regression.cpp').read_text()
flags = subprocess.run([os.environ.get('PKG_CONFIG', 'pkg-config'), '--cflags', '--libs', 'sdl3'],
                       capture_output=True, text=True, check=True).stdout
mutations = {
    'ignored-playback-demand': ('if(additional>0)', 'if(false)', ('SDL software starvation was not counted',)),
    'premature-playback': ('bytes>=15360', 'bytes>=3840', ('SDL playback started before recovery buffer filled',)),
    'missing-starvation-counter': ('++report.underruns;++report.reprimes;', '++report.reprimes;', ('SDL software starvation was not counted',)),
    'missing-reprime': ('sdl_play(false);sdl_priming=true;sdl_last_packet=0;', 'sdl_play(false);sdl_priming=false;sdl_last_packet=0;',
                        ('SDL starvation did not re-prime', 'SDL software starvation was not counted')),
    'discarded-native-pcm': ('if(!SDL_PutAudioStreamData(sdl,packet.original.data(),int(packet.original.size()*2)))',
                             'if(!SDL_PutAudioStreamData(sdl,packet.original.data(),0))', ('SDL playback started before recovery buffer filled',)),
}
for name, change in [('baseline', None), *mutations.items()]:
    folder = destination/name
    (folder/'frontend').mkdir(parents=True)
    body = source
    if change:
        before, after, _ = change
        assert body.count(before) == 1, name
        body = body.replace(before, after)
    (folder/'frontend/audio_output.hpp').write_text(body)
    (folder/'regression.cpp').write_text(regression)
    binary = folder/('check.exe' if os.name == 'nt' else 'check')
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-O2', '-UAB2_OPENAL', '-pthread',
               '-I'+str(folder), '-I'+str(ROOT/'src/main'), str(folder/'regression.cpp'), '-o', str(binary), *shlex.split(flags)]
    with (folder/'build.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=180)
    assert result.returncode == 0, folder/'build.log'
    result = subprocess.run([str(binary)], cwd=ROOT, capture_output=True, text=True, timeout=30)
    (folder/'run.log').write_text(result.stdout+result.stderr)
    assert (result.returncode == 0 if change is None else
            result.returncode == 1 and any(message in result.stderr for message in change[2])), folder/'run.log'
    print(f'PASS {name}: {folder}', flush=True)
