#!/usr/bin/env python3
"""Reject speaker routing, phase, history, filter and PCM mutations."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[2]
header=root/'src/main/hwaudio/xboard_speaker.hpp'
source=header.read_text()
mutations=[
 ('fm-gain','62500,0.15f','62500,0.16f'),
 ('pcm-gain','31250,0.35f','31250,0.34f'),
 ('channel-swap','return s.data[index-s.base][ch];','return s.data[index-s.base][ch^1];'),
 ('history','index-=4*divide;','index-=3*divide;'),
 ('initial-phase','unsigned phase=ssamp&0xfff;','unsigned phase=0;'),
 ('step','/48000/divide;','/48000/divide+1;'),
 ('filter','t[0][i]=(p-p*p*p)/6;','t[0][i]=(p-p*p*p)/5;'),
 ('sample-index','/1000000000+1;','/1000000000;'),
 ('clipping','-32768,32767','-32768,32766'),
 ('conversion','int(value*32768)','int(value*32767)'),
]
with tempfile.TemporaryDirectory(prefix='speaker-mutations-',dir=root/'build/oracle') as tmp:
 overlay=Path(tmp);local=overlay/'hwaudio/xboard_speaker.hpp';local.parent.mkdir()
 for name,old,new in mutations:
  assert source.count(old)==1,name
  local.write_text(source.replace(old,new));binary=overlay/name
  subprocess.run(['clang++','-std=c++20','-O2','-I'+str(overlay),'-Isrc/main','tests/oracle_speaker.cpp','-o',str(binary)],cwd=root,check=True,capture_output=True)
  for capture in ['joint_speaker_full.bin.audio','speaker_edges']:
   result=subprocess.run([str(binary),'build/oracle/'+capture],cwd=root,capture_output=True,text=True,timeout=30)
   if result.returncode:break
  (root/f'build/oracle/speaker_mutation_{name}.log').write_text(result.stdout+result.stderr)
  errors=['mixed speaker float differs','speaker sample count differs','speaker PCM conversion differs']
  if result.returncode!=1 or not any(e in result.stderr for e in errors):raise SystemExit(f'Mutation {name} not rejected: {result.stderr[-1000:]}')
  print(f'Speaker mutation {name}: rejected by {capture}',flush=True)
