#!/usr/bin/env python3
"""Require joint MAME latch/pixel/bank assertions to reject video mutations."""
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

root=Path(__file__).resolve().parents[2]
command_text=subprocess.run(['make','-n','-W','tests/oracle_publication_schedule.cpp','build/oracle_publication_schedule'],cwd=root,text=True,capture_output=True,check=True).stdout
commands=[shlex.split(line) for line in command_text.splitlines() if 'tests/oracle_publication_schedule.cpp' in line and ' -o ' in line]
assert len(commands)==1
sources=['tests/oracle_publication_schedule.cpp','src/main/hwvideo/hwtiles.cpp','src/main/hwvideo/screen.cpp','src/main/hwvideo/xboard_video_timing.hpp']
original={p:(root/p).read_text() for p in sources}
mutations=[
 ('latch-line',sources[3],'if(line==261&&ns>start&&latch)latch(ns);','if(line==260&&ns>start&&latch)latch(ns);',False),
 ('alternate-set',sources[1],'const bool alternate = (rowscroll & 0x8000) != 0;','const bool alternate = false;',False),
 ('row-origin',sources[1],'static_cast<uint32_t>(y / 8)','static_cast<uint32_t>((y+8) / 8)',True),
 ('column-origin',sources[1],'static_cast<uint32_t>((x + 8) / 16)','static_cast<uint32_t>(x / 16)',True),
 ('text-palette',sources[1],'const int color = (entry >> 9) & 0x07;','const int color = (entry >> 8) & 0x07;',False),
 ('sprite-priority',sources[2],'(1 << spr_priority) > priority[i]','(1 << spr_priority) >= priority[i]',True),
 ('effects-bank',sources[2],'pixels[i] + num_entries','pixels[i]',True),
 ('immediate-globals',sources[0],'video_text();HWTiles::FrameRegisters live{};','video_text();video_tiles.latch_frame_registers();HWTiles::FrameRegisters live{};',True),
 ('display-frontier',sources[3],'if(enabled!=display)partial(ns,vpos(ns));','/* omitted display partial update */',True),
 ('text-frontier',sources[3],'if(byte_offset>=0xe80)partial(ns,vpos(ns));','/* omitted text register frontier */',True),
 ('first-scanline',sources[3],'if(line==0){last_partial=0;partial_reset=ns;}','/* omitted frame cursor reset */',True),
 ('vblank-frontier',sources[3],'partial(ns,223);if(frame)frame(ns);','if(frame)frame(ns);',True),
 ('display-blank',sources[2],'RGB888{0,0,0}','RGB888{1,0,0}',True),
]
if len(sys.argv)>1:
 requested=set(sys.argv[1:]);known={m[0] for m in mutations}
 if requested-known:raise SystemExit('Unknown video mutations: '+', '.join(sorted(requested-known)))
 mutations=[m for m in mutations if m[0] in requested]

with tempfile.TemporaryDirectory(prefix='joint-video-mutations-',dir=root/'build/oracle') as tmp:
 overlay=Path(tmp)
 for name,target,old,new,directed in mutations:
  assert original[target].count(old)==1, name
  paths={}
  for path,source in original.items():
   local=overlay/path
   local.parent.mkdir(parents=True,exist_ok=True)
   local.write_text(source.replace(old,new) if path==target else source)
   paths[path]=str(local)
  binary=overlay/name
  cmd=[paths.get(arg,arg) for arg in commands[0]]
  cmd[1:1]=['-I'+str(overlay/'src/main')]
  cmd+=['-Itests'] # Temporary oracle copies retain shared test-header includes.
  cmd[cmd.index('-o')+1]=str(binary)
  subprocess.run(cmd,cwd=root,check=True,capture_output=True)
  capture='joint_video_owned_directed' if directed else 'joint_speaker_full'
  args=[str(binary),f'build/oracle/{capture}.bin',f'build/oracle/{capture}.txt','--require-main-closed','--require-sub-continuity','--require-video-rgb','--require-native-deadlines','--require-speaker']
  if directed:args+=['--require-video-fixture']
  result=subprocess.run(args,cwd=root,capture_output=True,text=True)
  # Missing frame renders may first fail MAME's hidden scaler-feedback bank.
  errors=['computed tile latch mismatch','computed joint RGB pixel mismatch','computed screen frontier mismatch','computed RGB frontier mismatch']
  if name in ('first-scanline','vblank-frontier'):errors.append('computed joint video bank mismatch')
  comparison=next((error for error in errors if error in result.stderr),None)
  if result.returncode!=1 or not comparison:raise SystemExit(f'Mutation {name} not rejected by a video comparison: {result.stderr}')
  print(f'Joint video mutation {name}: rejected by {capture} ({comparison})',flush=True)
