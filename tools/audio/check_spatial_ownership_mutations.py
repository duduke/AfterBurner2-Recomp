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
