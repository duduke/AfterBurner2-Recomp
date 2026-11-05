#!/usr/bin/env python3
"""Retained negative controls for object identity and object-to-voice metadata."""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]
MUTATIONS=[
 ('slot-reuse','++s.generation;s.born=ns;','s.born=ns;','unit'),
 ('source-retirement','it->second.dead=ns;event(ns,Kind::retired','it->second.dead=never;event(ns,Kind::retired','unit'),
 ('caller-guess','command==0xd8&&caller==0x14da0','command==0xd8','unit'),
 ('ring-slot','tag=ring[ctx.d1];','tag=ring[(ctx.d1+1)&63];','oracle'),
 ('future-latch','latches.front().ns<=ns','true','unit'),
 ('z80-queue-copy','tag=transfer.tag;','tag={};','unit'),
 ('descriptor-root','a!=root(tag.command)','a==root(tag.command)','unit'),
 ('voice-steal','assign(pcm[channel],(e.data&1)?Tag{}:tag','if(tag.id)assign(pcm[channel],(e.data&1)?Tag{}:tag','unit'),
 ('lock-parent','const unsigned parent=(ctx.a5-64-0x3fa000)/64','const unsigned parent=(ctx.a5-0x3fa000)/64','unit'),
 ('view-side','const float view_x=(cosine*x+sine*y)/128','const float view_x=-(cosine*x+sine*y)/128','unit'),
 ('future-position','return t<p.ns;','return false;','unit'),
 ('movement-discontinuity','if(smooth&&end!=path.begin())','if(false&&end!=path.begin())','unit'),
 ('natural-fm-owner','assign(fm[unsigned(channel)],tag,','assign(fm[unsigned(channel)],Tag{},','expanded'),
 ('natural-lock-producer','longword(read,ctx.sp)==0x7bac','longword(read,ctx.sp)==0x7bad','expanded'),
 ('listener-depth','float x=0,y=0,z=int16_t(read(p+36))-int16_t(read(0xffff8d18));','float x=0,y=0,z=int16_t(read(0xffff8d18))-int16_t(read(p+36));','expanded'),
 ('coordinate-x','if(source.position_known)source.position={int32_t(longword(read,p+24))','if(source.position_known)source.position={int32_t(longword(read,p+4))','oracle'),
 ('player-depth','int16_t(read(0xffff8d18))};','int16_t(read(0xffff8d14))};','oracle'),
]

def main():
 source=(ROOT/'src/main/hwaudio/object_audio_tracking.hpp').read_text()
 logs=ROOT/'build/oracle/object-audio/mutations';logs.mkdir(parents=True,exist_ok=True)
 for name,before,after,suite in MUTATIONS:
  assert source.count(before)==1, name+': mutation anchor must be unique'
  with tempfile.TemporaryDirectory(prefix='ab2-object-audio-') as directory:
   overlay=Path(directory);header=overlay/'hwaudio/object_audio_tracking.hpp';header.parent.mkdir();header.write_text(source.replace(before,after))
   executable=overlay/'check';test='tests/object_audio_tracking_regression.cpp' if suite=='unit' else 'tests/oracle_object_audio.cpp'
   command=[os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(overlay),'-Isrc/main',test,'-o',str(executable)]
   with (logs/(name+'.log')).open('w') as log:
    subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    arguments=[str(executable)]+(["build/oracle/object-audio/families-v3.txt"] if suite=="expanded" else [])
    result=subprocess.run(arguments,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
   if result.returncode==0:raise RuntimeError(name+': mutation survived')
   print('PASS rejected object-audio '+name,flush=True)

 geometry=(ROOT/'src/main/hwaudio/object_audio_rendering.hpp').read_text()
 for name,before,after in [
  ('world-offset','+longword(base)', '-longword(base)'),
  ('world-elevation','-vy/128.f','vy/128.f')]:
  assert geometry.count(before)==1,name+': mutation anchor must be unique'
  with tempfile.TemporaryDirectory(prefix='ab2-object-geometry-') as directory:
   overlay=Path(directory);header=overlay/'hwaudio/object_audio_rendering.hpp';header.parent.mkdir();header.write_text(geometry.replace(before,after));executable=overlay/'check'
   with (logs/(name+'.log')).open('w') as log:
    subprocess.run([os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(overlay),'-Isrc/main','tests/object_audio_tracking_regression.cpp','-o',str(executable)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    result=subprocess.run([str(executable)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
   if result.returncode==0:raise RuntimeError(name+': mutation survived')
   print('PASS rejected object-audio '+name,flush=True)

if __name__=='__main__':main()
