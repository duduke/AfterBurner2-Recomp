#!/usr/bin/env python3
"""Reject private deadline/stream/boost mutations against native MAME gates."""
from pathlib import Path
import shlex
import subprocess
import tempfile

root=Path(__file__).resolve().parents[2]
audio='src/main/hwaudio/xboard_audio_deadlines.hpp'
devices='src/main/hwmath/xboard_device_deadlines.hpp'
joint='tests/oracle_publication_schedule.cpp'
original={p:(root/p).read_text() for p in (audio,devices,joint)}
mutations=[
 ('sample-index',audio,'const auto target=ns/16000+1;','const auto target=ns/16000;',False),
 ('pcm-index',audio,'const auto target=ns/32000+1;','const auto target=ns/32000;',False),
 ('timer-period',audio,'uint64_t(clocks)*250,++serial','uint64_t(clocks)*251,++serial',False),
 ('late-mode',audio,'current=at;','current=ns;',False),
 ('sound-update-period',devices,'i==SoundUpdate?20000000:','i==SoundUpdate?21000000:',False),
 ('tile-head',devices,'next{37*64000,','next{36*64000,',False),
 ('first-scanline-head',devices,'38*64000,262*64000,','39*64000,262*64000,',False),
 ('vblank-begin-head',devices,'262*64000,\n','261*64000,\n',False),
 ('vblank-end-head',devices,'300*64000,20000000','299*64000,20000000',False),
 ('pcm-update',audio,'update_fm(ns);update_pcm(ns);','update_fm(ns);',False),
 ('irq-boost',joint,'if(native_boost&&main_board.main_irq_level())native_boost(board_advancing?main_board.now():main_phase_ns);','/* omitted IRQ boost owner */',False),
 ('reset-boost',joint,'if(owns_boost){native_boost(ns);++index;}','if(owns_boost){++index;}',False),
 ('timer-b-schedule',audio,'void ymfm_set_timer(uint32_t n,int32_t clocks) override {timers[n]=','void ymfm_set_timer(uint32_t n,int32_t clocks) override {if(n==1)return;timers[n]=',True),
]
commands={}
for target,source in [('build/oracle_publication_schedule',joint),('build/oracle_sound_execution','tests/oracle_sound_execution.cpp')]:
 text=subprocess.run(['make','-n','-W',source,target],cwd=root,capture_output=True,text=True,check=True).stdout
 lines=[shlex.split(line) for line in text.splitlines() if source in line and ' -o ' in line]
 assert len(lines)==1
 commands[target]=lines[0]
with tempfile.TemporaryDirectory(prefix='audio-deadline-mutations-',dir=root/'build/oracle') as tmp:
 overlay=Path(tmp)
 for name,target,old,new,directed in mutations:
  files=dict(original)
  if directed:
   for p in (root/'src/third_party/ymfm').iterdir():
    if p.suffix in {'.h','.cpp','.ipp'}:files[str(p.relative_to(root))]=p.read_text()
  assert files[target].count(old)==1,name
  for path,source in files.items():
   local=overlay/path;local.parent.mkdir(parents=True,exist_ok=True)
   if path==target:source=source.replace(old,new)
   if path==audio and not directed:source=source.replace('"../../third_party/ymfm/ymfm_opm.h"',f'"{root}/src/third_party/ymfm/ymfm_opm.h"')
   local.write_text(source)
  binary=overlay/name
  cmd=list(commands['build/oracle_sound_execution' if directed else 'build/oracle_publication_schedule'])
  if not directed:cmd=[str(overlay/a) if a==joint else a for a in cmd]
  if directed:cmd=[str(overlay/a) if a=='src/third_party/ymfm/ymfm_opm.cpp' else a for a in cmd]
  cmd[1:1]=['-I'+str(overlay/'src/main'),'-Itests']
  cmd[cmd.index('-o')+1]=str(binary)
  subprocess.run(cmd,cwd=root,check=True,capture_output=True)
  capture='sound_audio_timer_b' if directed else 'joint_audio_full'
  args=[str(binary),f'build/oracle/{capture}.bin',f'build/oracle/{capture}.txt']
  args+=['--native-audio'] if directed else ['--require-reset','--require-main-closed','--require-sub-continuity','--require-video-rgb','--require-native-deadlines']
  result=subprocess.run(args,cwd=root,capture_output=True,text=True,timeout=180)
  (root/f'build/oracle/audio_mutation_{name}.log').write_text(result.stdout+result.stderr)
  errors=('native audio batch deadline/index differs','native raw audio samples differ','native continuous sound differs','native device deadline differs','native outer timer delivery differs','native per-owner timer callback differs','native boost owner count differs','native boost owner timestamp differs','native IRQ timestamp or retained caller differs')
  if result.returncode!=1 or not any(e in result.stderr for e in errors):raise SystemExit(f'Mutation {name} not rejected by comparison: {result.stderr[-2000:]}')
  print(f'Audio/device mutation {name}: rejected by {capture}',flush=True)
