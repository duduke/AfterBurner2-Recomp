#!/usr/bin/env python3
"""Require actual joint MAME latch/pixel assertions to reject video mutations."""
from pathlib import Path
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
command_text=subprocess.run(['make','-n','-W','tests/oracle_publication_schedule.cpp','build/oracle_publication_schedule'],cwd=root,text=True,capture_output=True,check=True).stdout
commands=[shlex.split(line) for line in command_text.splitlines() if 'tests/oracle_publication_schedule.cpp' in line and ' -o ' in line]
assert len(commands)==1
sources=['tests/oracle_publication_schedule.cpp','src/main/hwvideo/hwtiles.cpp','src/main/hwvideo/screen.cpp']
original={p:(root/p).read_text() for p in sources}
mutations=[
 ('latch-line',sources[0],'if(line==261)video_latch(t);','if(line==260)video_latch(t);',False),
 ('alternate-set',sources[1],'const bool alternate = (rowscroll & 0x8000) != 0;','const bool alternate = false;',False),
 ('row-origin',sources[1],'static_cast<uint32_t>(y / 8)','static_cast<uint32_t>((y+8) / 8)',True),
 ('column-origin',sources[1],'static_cast<uint32_t>((x + 8) / 16)','static_cast<uint32_t>(x / 16)',True),
 ('text-palette',sources[1],'const int color = (entry >> 9) & 0x07;','const int color = (entry >> 8) & 0x07;',False),
 ('sprite-priority',sources[2],'(1 << spr_priority) > priority[i]','(1 << spr_priority) >= priority[i]',True),
 ('effects-bank',sources[2],'pixels[i] + num_entries','pixels[i]',True),
 ('immediate-globals',sources[0],'video_text();HWTiles::FrameRegisters live{};','video_text();video_tiles.latch_frame_registers();HWTiles::FrameRegisters live{};',True),
 ('display-frontier',sources[0],'if(enabled!=video_display)video_partial(ns,video_vpos(ns));','/* omitted display partial update */',True),
 ('display-blank',sources[2],'RGB888{0,0,0}','RGB888{1,0,0}',True),
]
with tempfile.TemporaryDirectory(prefix='joint-video-mutations-',dir=root/'build/oracle') as tmp:
 overlay=Path(tmp)
 for name,target,old,new,directed in mutations:
  assert original[target].count(old)==1, name
  paths={}
  for path,source in original.items():
   local=overlay/Path(path).name
   local.write_text(source.replace(old,new) if path==target else source)
   paths[path]=str(local)
  binary=overlay/name
  cmd=[paths.get(arg,arg) for arg in commands[0]]
  cmd+=['-Itests'] # Temporary oracle copies retain shared test-header includes.
  cmd[cmd.index('-o')+1]=str(binary)
  subprocess.run(cmd,cwd=root,check=True,capture_output=True)
  capture='joint_video_directed' if directed else ('joint_main_progress' if name in ('latch-line','text-palette') else 'joint_main_progress_full')
  args=[str(binary),f'build/oracle/{capture}.bin',f'build/oracle/{capture}.txt','--require-main-closed','--require-sub-continuity','--require-video-rgb','--require-sound-handshake']
  if directed:args+=['--require-video-fixture']
  result=subprocess.run(args,cwd=root,capture_output=True,text=True)
  comparison=any(error in result.stderr for error in ('computed tile latch mismatch','computed joint RGB pixel mismatch','computed screen frontier mismatch','computed RGB frontier mismatch'))
  if result.returncode!=1 or not comparison:raise SystemExit(f'Mutation {name} not rejected by a latch/pixel comparison: {result.stderr}')
  print(f'Joint video mutation {name}: rejected by {capture}',flush=True)
