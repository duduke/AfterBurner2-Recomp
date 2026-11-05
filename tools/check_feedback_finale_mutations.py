#!/usr/bin/env python3
"""Negative controls for the separately verified final carrier landing."""
from pathlib import Path
import os,subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
MUTATIONS=[
 ('wrong-aircraft','finale_record=a-18+64','finale_record=a-18','fixture'),
 ('missing-contact','p.pc==0x11f4c && a==finale_record+24','p.pc==0x11f4e && a==finale_record+24','fixture'),
 ('repeated-contact','&& !finale_landed','&& true','fixture'),
 ('wrong-contact-kind','finale_landed=true;emit(SequenceCue::touchdown','finale_landed=true;emit(SequenceCue::launch','fixture'),
 ('flight-only','mode>=16 && mode<=20','mode==16','fixture'),
 ('late-stop','!(finale_flags&0x4000)','true','fixture'),
 ('attract-motion','mode>=16 && mode<=20','mode>=1 && mode<=26','unit'),
]
def main():
 source=(ROOT/'src/main/engine/feedback_sequences.hpp').read_text();logs=ROOT/'build/haptics/finale-mutations';logs.mkdir(parents=True,exist_ok=True)
 flags=subprocess.check_output(['pkg-config','--cflags','--libs','sdl3'],text=True).split()
 for name,before,after,suite in MUTATIONS:
  assert source.count(before)==1,name+': ambiguous selector'
  with tempfile.TemporaryDirectory(prefix='ab2-finale-') as directory:
   overlay=Path(directory);header=overlay/'engine/feedback_sequences.hpp';header.parent.mkdir();header.write_text(source.replace(before,after));exe=overlay/'check'
   test='tests/feedback_finale_fixture.cpp' if suite=='fixture' else 'tests/native_haptics_regression.cpp'
   command=[os.environ.get('CXX','c++'),'-std=c++20','-O2','-I'+str(overlay),'-Isrc/main',test,'-o',str(exe),*flags]
   with (logs/(name+'.log')).open('w') as log:
    subprocess.run(command,cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
    result=subprocess.run([str(exe)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT)
   if result.returncode!=1:raise RuntimeError(name+': survived or crashed; see retained log')
  print('PASS rejected finale '+name,flush=True)
if __name__=='__main__':main()
