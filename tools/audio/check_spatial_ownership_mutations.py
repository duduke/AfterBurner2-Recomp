#!/usr/bin/env python3
"""Fail retained negative controls without changing baseline sources/fixtures."""
from pathlib import Path
import shutil, subprocess, os
root=Path(__file__).resolve().parents[2];os.chdir(root)
folder=root/'build/oracle/spatial-audio/ownership-mutations';folder.mkdir(parents=True,exist_ok=True)
# Independently recorded IX/DE/BC is an assertion, never a playback provider.
fixture=Path('build/oracle/spatial-audio/routing.bin')
corrupt=folder/'wrong-bc.bin';data=bytearray(fixture.read_bytes());data[26]^=1;corrupt.write_bytes(data)
shutil.copyfile(str(fixture)+'.inputs',str(corrupt)+'.inputs')
with (folder/'wrong-bc.log').open('w') as log:
    result=subprocess.run(['./build/oracle_spatial_routing',str(corrupt)],stdout=log,stderr=subprocess.STDOUT,timeout=30)
if result.returncode==0:raise RuntimeError('wrong BC fixture accepted')
if 'transaction/IX/DE/BC mismatch' not in (folder/'wrong-bc.log').read_text():raise RuntimeError('wrong BC failed for unrelated reason')
print('Spatial ownership mutation rejected: recorded BC mismatch',flush=True)
# The native tap must classify sounds without changing native RGB/PCM. Disable
# only the observer in a private header; the 1800-frame integration must reject.
case=folder/'detached-owner';(case/'hwaudio').mkdir(parents=True,exist_ok=True)
header=Path('src/main/hwaudio/xboard_sound_board.hpp');source=header.read_text()
needle='if(cockpit && CockpitRouting::interesting(e))'
if source.count(needle)!=1:raise RuntimeError('ambiguous observer mutation')
(case/'hwaudio/xboard_sound_board.hpp').write_text(source.replace(needle,'if(false && cockpit && CockpitRouting::interesting(e))'))
objects=sorted(Path('build/native_obj/src').rglob('*.o'))
objects=[str(p) for p in objects if p.name!='main.o']
flags=subprocess.check_output(['pkg-config','--cflags','sdl3'],text=True).split()
libs=subprocess.check_output(['pkg-config','--libs','sdl3'],text=True).split()
exe=case/'check'
with (case/'build.log').open('w') as log:
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O3','-flto','-I'+str(case),'-Isrc/main',*flags,'tests/music_presentation_integration.cpp',*objects,'-o',str(exe),*libs,'-pthread'],stdout=log,stderr=subprocess.STDOUT,check=True)
with (case/'run.log').open('w') as log:
    result=subprocess.run([str(exe)],stdout=log,stderr=subprocess.STDOUT,timeout=180)
if result.returncode==0:raise RuntimeError('detached owner accepted')
if 'front/rear chip-rate coverage missing' not in (case/'run.log').read_text():raise RuntimeError('detached owner failed for unrelated reason')
print('Spatial ownership mutation rejected: detached passive observer')
# The catalog oracle checks intentional presentation choices as well as exact
# sound-CPU ownership. Private source variants retain the independent capture.
routing_header=Path('src/main/hwaudio/cockpit_audio.hpp')
routing_source=routing_header.read_text()
for name,needle,replacement,wanted in [
    ('passing-front','commands[0x6e]=rear;commands[0x6f]=rear;',
     'commands[0x6e]=front;commands[0x6f]=front;','passing sound policy mismatch'),
    ('enemy-launch-rear','commands[0xd8]=front;',
     'commands[0xd8]=rear;','spatial PCM policy mismatch'),
    ('neutral-directed','direct_group=get(direct,r.bc);',
     'direct_group=front;','spatial PCM policy mismatch'),
]:
    if routing_source.count(needle)!=1:raise RuntimeError('ambiguous routing mutation '+name)
    case=folder/name;(case/'hwaudio').mkdir(parents=True,exist_ok=True)
    (case/'hwaudio/cockpit_audio.hpp').write_text(routing_source.replace(needle,replacement))
    exe=case/'check'
    with (case/'build.log').open('w') as log:
        subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O3','-flto','-I'+str(case),'-Isrc/main',*flags,'tests/oracle_spatial_routing.cpp',*objects,'-o',str(exe),*libs,'-pthread'],stdout=log,stderr=subprocess.STDOUT,check=True)
    with (case/'run.log').open('w') as log:
        result=subprocess.run([str(exe)],stdout=log,stderr=subprocess.STDOUT,timeout=180)
    if result.returncode==0:raise RuntimeError('routing mutation accepted: '+name)
    if wanted not in (case/'run.log').read_text():raise RuntimeError('routing mutation failed for unrelated reason: '+name)
    print('Spatial ownership mutation rejected: '+name,flush=True)
# Prove native identity remains an active comparison; the integration changes
# only one reference sample without modifying either board's execution.
with (folder/'bad-native-reference.log').open('w') as log:
    result=subprocess.run(['./build/spatial_audio_game_integration','--bad-reference'],stdout=log,stderr=subprocess.STDOUT,timeout=180)
if result.returncode==0:raise RuntimeError('altered native PCM reference accepted')
if 'Spatial observer changed native PCM/time' not in (folder/'bad-native-reference.log').read_text():raise RuntimeError('native reference mutation failed for unrelated reason')
print('Spatial ownership mutation rejected: altered native PCM reference',flush=True)
