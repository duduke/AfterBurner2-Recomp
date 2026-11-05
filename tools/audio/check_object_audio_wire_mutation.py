#!/usr/bin/env python3
"""The Make target runs the unmodified native baseline before this control."""
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[2]
def main():
 plan=subprocess.check_output(['make','--no-print-directory','-Bn','build/object_audio_integration'],cwd=ROOT,text=True)
 commands=[shlex.split(line) for line in plan.splitlines() if line.startswith(('c++ ','clang++ ','g++ ','/'))]
 compile_command=next(c for c in commands if '-c' in c and 'tests/object_audio_integration.cpp' in c)
 link_command=next(c for c in reversed(commands) if '-o' in c and c[c.index('-o')+1]=='build/object_audio_integration')
 source=(ROOT/'src/main/hwaudio/xboard_sound_board.hpp').read_text();before='if(object_tracking&&ObjectAudio::Tracking::interesting(e))'
 assert source.count(before)==1,'sound observer mutation anchor must be unique'
 log_path=ROOT/'build/oracle/object-audio/mutations/detached-sound-observer.log';log_path.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='ab2-object-wire-') as directory:
  overlay=Path(directory);header=overlay/'hwaudio/xboard_sound_board.hpp';header.parent.mkdir();header.write_text(source.replace(before,'if(false)'))
  obj=str(overlay/'check.o');exe=str(overlay/'check');compile_command.insert(1,'-I'+str(overlay));compile_command[compile_command.index('-o')+1]=obj
  link_command[link_command.index('build/native_obj/tests/object_audio_integration.o')]=obj;link_command[link_command.index('-o')+1]=exe
  with log_path.open('w') as log:
   subprocess.run(compile_command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
   subprocess.run(link_command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
   result=subprocess.run([exe],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
  if result.returncode==0 or not any(message in log_path.read_text() for message in ('native tracking coverage incomplete','natural object FM/PCM rendering coverage missing')):raise RuntimeError('detached observer survived or failed for an unrelated reason')
 print('PASS rejected detached object sound observer')
if __name__=='__main__':main()
