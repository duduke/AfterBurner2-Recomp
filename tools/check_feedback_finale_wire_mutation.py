#!/usr/bin/env python3
"""The Make target runs the unmodified native baseline before this control."""
from pathlib import Path
import shlex
import subprocess
import tempfile
ROOT=Path(__file__).resolve().parents[1]
def main():
 plan=subprocess.check_output(['make','--no-print-directory','-Bn','build/feedback_finale_integration'],cwd=ROOT,text=True)
 commands=[shlex.split(line) for line in plan.splitlines() if line.startswith(('c++ ','clang++ ','g++ ','/'))]
 compile_command=next(c for c in commands if '-c' in c and 'tests/feedback_finale_integration.cpp' in c)
 link_command=next(c for c in reversed(commands) if '-o' in c and c[c.index('-o')+1]=='build/feedback_finale_integration')
 source=(ROOT/'src/main/engine/game_session.hpp').read_text();before='finale?feedback_word(finale+24):0'
 assert source.count(before)==1,'finale export mutation anchor must be unique'
 log_path=ROOT/'build/haptics/finale-mutations/detached-flags-export.log';log_path.parent.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='ab2-finale-wire-') as directory:
  overlay=Path(directory);header=overlay/'engine/game_session.hpp';header.parent.mkdir();header.write_text(source.replace(before,'0'))
  obj=str(overlay/'check.o');exe=str(overlay/'check');compile_command.insert(1,'-I'+str(overlay));compile_command[compile_command.index('-o')+1]=obj
  link_command[link_command.index('build/native_obj/tests/feedback_finale_integration.o')]=obj;link_command[link_command.index('-o')+1]=exe
  with log_path.open('w') as log:
   subprocess.run(compile_command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
   subprocess.run(link_command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
   result=subprocess.run([exe,'practice'],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
  if result.returncode==0 or 'finale Practice coverage incomplete' not in log_path.read_text():raise RuntimeError('detached finale export survived or failed for an unrelated reason')
 print('PASS rejected detached finale flags export')
if __name__=='__main__':main()
