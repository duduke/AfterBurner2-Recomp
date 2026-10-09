#!/usr/bin/env python3
"""Reject startup owner changes by exact cold MAME bus/video comparisons."""
from pathlib import Path
import shlex, subprocess, tempfile
root=Path(__file__).resolve().parents[2]
main='src/main/engine/omainirqservice.cpp'
boot='src/main/engine/omainbootschedule.inc'
test='tests/oracle_publication_schedule.cpp'
mutations=[
 ('ram-reset','src/main/engine/mainbus.hpp','std::array<uint8_t, MEMORY_BYTES> bytes{};','std::array<uint8_t, MEMORY_BYTES> bytes{1};'),
 ('bank-reset','src/main/engine/mainbus.hpp','sprite_buffer{}, road_buffer{};','sprite_buffer{1}, road_buffer{};'),
 ('reset-clocks',main,'idle(8);const auto high_sp=read16(0),low_sp=read16(2);','idle(6);const auto high_sp=read16(0),low_sp=read16(2);'),
 ('startup-stack',boot,'write16(sp+2,0x44ec);','write16(sp+2,0x44ee);'),
 ('startup-mask',boot,'const uint32_t value=0x700;','const uint32_t value=0x600;'),
 ('trap-vector',main,'?0xa8:0xb0;','?0xac:0xb0;'),
 ('first-latch','src/main/hwvideo/xboard_video_timing.hpp','if(latch)latch(first_latch);','/* omit initial latch */'),
 ('dip-switches',test,'p<=3?physical_switches[p-1]:0xff','p<=3?uint8_t(physical_switches[p-1]^0x20):0xff'),
]
command_text=subprocess.run(['make','-n','-W',test,'build/oracle_publication_schedule'],cwd=root,text=True,capture_output=True,check=True).stdout
commands=[shlex.split(line) for line in command_text.splitlines() if test in line and ' -o ' in line];assert len(commands)==1
with tempfile.TemporaryDirectory(prefix='joint-cold-mutations-',dir=root/'build/oracle') as temp:
 for name,target,old,new in mutations:
  overlay=Path(temp)/name
  engine=overlay/'src/main/engine';engine.mkdir(parents=True,exist_ok=True)
  for f in engine.iterdir():f.unlink()
  source=(root/target).read_text();assert source.count(old)==1,(name,source.count(old))
  local=overlay/target;local.parent.mkdir(parents=True,exist_ok=True);local.write_text(source.replace(old,new))
  cmd=commands[0].copy();cmd[1:1]=['-I'+str(overlay/'src/main'),'-Isrc/main/engine','-Itests']
  compiled=main if target in (main,boot) else test
  if compiled!=target:
   copy=overlay/compiled;copy.parent.mkdir(parents=True,exist_ok=True);copy.write_text((root/compiled).read_text())
  for i,arg in enumerate(cmd):
   if arg in (compiled,'build/oracle_obj/'+compiled.replace('.cpp','.o')):cmd[i]=str(overlay/compiled)
  binary=overlay/"mutation";cmd[cmd.index('-o')+1]=str(binary)
  subprocess.run(cmd,cwd=root,capture_output=True,check=True)
  result=subprocess.run([str(binary),'build/oracle/joint_cold.bin','build/oracle/joint_cold.txt','--require-cold','--require-main-closed','--require-sub-continuity','--require-video-rgb','--require-native-deadlines','--require-speaker'],cwd=root,capture_output=True,text=True)
  errors=('native publication phase mismatch','retained native instruction state mismatch','computed tile latch mismatch','computed RGB frontier mismatch','cold RAM initialization differs','cold hidden banks differ')
  if result.returncode!=1 or not any(e in result.stderr for e in errors):raise SystemExit(f'Mutation {name} escaped comparison: {result.stderr}')
  print(f'Cold startup mutation {name}: rejected by MAME comparison',flush=True)
