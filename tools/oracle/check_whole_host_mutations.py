#!/usr/bin/env python3
"""Require passive whole-host MAME comparisons to reject each owner mutation."""
from pathlib import Path
import shlex
import subprocess
import tempfile
import sys
root=Path(__file__).resolve().parents[2]
test='tests/oracle_whole_host.cpp'
mutations=[
 ('main-reset','src/main/engine/omainirqservice.cpp','idle(8);const auto high_sp=read16(0),low_sp=read16(2);','idle(6);const auto high_sp=read16(0),low_sp=read16(2);'),
 ('sub-reset','src/main/engine/osubirq.cpp','s.ns+=8*80;','s.ns+=6*80;'),
 ('dispatcher','src/main/engine/xboard_host.hpp','bus_pc[c]=state.pc;','bus_pc[c]=state.pc+2;'),
 ('main-irq','src/main/engine/xboard_host.hpp','return lines.level();','return 0;'),
 ('sub-irq','src/main/engine/xboard_host.hpp','<XBoardTimeline::LINE_NS?4u:0u;','<XBoardTimeline::LINE_NS?0u:0u;'),
 ('io','src/main/engine/xboard_host.hpp','switches[p-1]:uint8_t(0xff)','uint8_t(switches[p-1]^0x20):uint8_t(0xff)'),
 ('bank','src/main/engine/mainbus.hpp','sprite_buffer{}, road_buffer{};','sprite_buffer{1}, road_buffer{};'),
 ('renderer','src/main/hwvideo/xboard_video_timing.hpp','if(latch)latch(first_latch);','/* missing first latch */'),
 ('scheduler','src/main/hwmath/xboard_bus_arbiter.hpp','1000000000000000000ULL/6000','1000000000000000000ULL/6001'),
 ('deadline','src/main/hwmath/xboard_device_deadlines.hpp','37*64000,38*64000','36*64000,38*64000'),
 ('z80-clock','src/main/hwaudio/xboard_sound_board.hpp','grant_ns=begin;','grant_ns=begin+250;'),
 ('fm','src/main/hwaudio/xboard_sound_board.hpp','audio.ym_write(a,d,now());','audio.ym_write(a,d^1,now());'),
 ('pcm','src/main/hwaudio/xboard_sound_board.hpp','audio.pcm_write(a,d,now());','audio.pcm_write(a,d^1,now());'),
 ('speaker','src/main/hwaudio/xboard_speaker.hpp','route(1,31250,0.35f,out);','route(1,31250,0.36f,out);'),
 ('producer','src/main/engine/omainirqservice.cpp','constexpr uint16_t armed=1;','constexpr uint16_t armed=2;'),
]
if len(sys.argv)>1:
 mutations=[m for m in mutations if m[0] in sys.argv[1:]]
 if not mutations: raise SystemExit('unknown mutation selection')
output=subprocess.run(['make','-n','-W',test,'build/oracle_whole_host'],cwd=root,text=True,capture_output=True,check=True).stdout
commands=[shlex.split(line) for line in output.splitlines() if test in line and ' -o ' in line]
assert len(commands)==1
with tempfile.TemporaryDirectory(prefix='whole-host-mutations-',dir=root/'build/oracle') as temp:
 for name,target,old,new in mutations:
  overlay=Path(temp)/name;local=overlay/target;local.parent.mkdir(parents=True,exist_ok=True)
  source=(root/target).read_text();assert source.count(old)==1,(name,source.count(old));local.write_text(source.replace(old,new))
  cmd=commands[0].copy();cmd[1:1]=['-I'+str(overlay/'src/main'),'-Isrc/main/engine']
  compiled=target if target.endswith('.cpp') else 'src/main/engine/omainirqservice.cpp' if target.endswith('.inc') else test
  if compiled!=target:
   copy=overlay/compiled;copy.parent.mkdir(parents=True,exist_ok=True);copy.write_text((root/compiled).read_text())
  for i,arg in enumerate(cmd):
   if arg in (compiled,'build/oracle_obj/'+compiled.replace('.cpp','.o')):cmd[i]=str(overlay/compiled)
  binary=overlay/'mutation';cmd[cmd.index('-o')+1]=str(binary)
  subprocess.run(cmd,cwd=root,capture_output=True,check=True)
  corpus='joint_cold_full' if name=='speaker' else 'joint_cold'
  result=subprocess.run([str(binary),f'build/oracle/{corpus}.bin',f'build/oracle/{corpus}.txt'],cwd=root,capture_output=True,text=True)
  errors=('whole-host bus phase differs','whole-host retained CPU state differs','bank','latch','whole-host quantum/device head differs','whole-host CPU grant differs','whole-host timer delivery selection differs','whole-host boost owner differs','whole-host sound CPU transaction differs','audio batch frontier differs','raw audio sample differs','mixed speaker float differs','device callback frontier differs','pixel frontier differs','whole-host RGB/index/priority differs')
  if result.returncode!=1 or not any(e in result.stderr for e in errors):raise SystemExit(f'Mutation {name} escaped passive comparison: {result.stderr}')
  print(f'Whole-host owner mutation {name}: rejected by passive MAME comparison',flush=True)
