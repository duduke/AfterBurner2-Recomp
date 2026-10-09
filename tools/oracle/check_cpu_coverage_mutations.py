#!/usr/bin/env python3
"""Reject source mutations against original-ROM instruction/exception captures."""
from pathlib import Path
import argparse,shlex,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]
main='src/main/engine/omainirqservice.cpp';sub='src/main/engine/osubirq.cpp'
mutations=[
 ('divide-zero-return',main,'write16(sp-2,target);sp-=6;write16(sp,saved);','write16(sp-2,target+2);sp-=6;write16(sp,saved);','cpu_coverage'),
 ('divide-zero-clock',main,'sr=(sr&~0x8000)|0x2000;idle(8);','sr=(sr&~0x8000)|0x2000;idle(6);','cpu_coverage'),
 ('divide-overflow-flags',main,'idle(6);sr=(sr&0xfff0)|0xa;','idle(6);sr=(sr&0xfff0)|0x2;','cpu_coverage'),
 ('main-rte-vector',main,'const auto target=read32(0x20);read16(target);','const auto target=read32(0x24);read16(target);','cpu_coverage'),
 ('sub-rte-vector',sub,'auto high=read(0x20);next=(uint32_t(high)<<16)|read(0x22);','auto high=read(0x24);next=(uint32_t(high)<<16)|read(0x26);','cpu_coverage'),
 ('absent-main-addq','src/main/engine/omainhostcallerschedule.inc','case 0x9626: {\n    const uint32_t operand=1;','case 0x9626: {\n    const uint32_t operand=2;','cpu_coverage_expanded_full'),
 ('absent-sub-dbf','src/main/engine/osubprogress.inc','idle(2);read16(0x19aa);const auto count=uint16_t(r.d[5]-1);','idle(2);read16(0x19aa);const auto count=uint16_t(r.d[5]-2);','cpu_coverage_expanded_full'),
 ('memory-bcd-clock',main,'idle(2);auto& sa=src==7?sp:r.a[src];','idle(4);auto& sa=src==7?sp:r.a[src];','cpu_coverage_expanded_full'),
 ('nested-mask',main,'(c.sr&0x38ff)|0x2000|(c.level<<8)','(c.sr&0x38ff)|0x2000|(6<<8)','cpu_nested_irq'),
]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--recovery',nargs='*',help='Recovery capture(s); omitted paths use the retained default')
parser.add_argument('--only',help='One named mutation for an incremental capture; default runs all retained mutations')
args=parser.parse_args()
recovery_captures=args.recovery or ['build/oracle/cpu_recovery.bin']
if args.recovery is not None:
 mutations=[
  ('recovery-hud-countdown','src/main/engine/omainrecoveryschedule.inc','case 0xdd7a: {\n    read16(0xdd7e);\n    const uint32_t operand=0x1;','case 0xdd7a: {\n    read16(0xdd7e);\n    const uint32_t operand=0x2;','cpu_recovery'),
  ('crash-dispatch-target','src/main/engine/omainrecoveryschedule.inc','case 0xe99a: {\n    branch(true,0xeb5e,4);','case 0xe99a: {\n    branch(true,0xeb60,4);','cpu_recovery'),
  ('crash-descriptor-pointer','src/main/engine/omainrecoveryschedule.inc','case 0xe662: {\n    r.a[0]=0x2bb36;','case 0xe662: {\n    r.a[0]=0x2bb38;','cpu_recovery'),
  ('stage-indicator-branch','src/main/engine/omainrecoveryschedule.inc','case 0x19cc6: {\n    branch(!(sr&4),0x19d1c,4);','case 0x19cc6: {\n    branch(!(sr&4),0x19d1e,4);','cpu_recovery'),
  ('collision-success','src/main/engine/omainrecoveryschedule.inc','case 0x15178: {\n    r.d[6]=0x1;','case 0x15178: {\n    r.d[6]=0x0;','cpu_recovery'),
  ('object-motion-entry','src/main/engine/omainrecoveryschedule.inc','case 0x153e0: {\n    read16(0x153e4);','case 0x153e0: {\n    read16(0x153e6);','cpu_recovery'),
  ('hit-response-return','src/main/engine/omainrecoveryschedule.inc','case 0x7330: {\n    branch(true,0x5af2,4);','case 0x7330: {\n    branch(true,0x5af4,4);','cpu_recovery'),
  ('gameplay-write','src/main/engine/omainrecoveryschedule.inc','read16(0x5cee);\n    nz(value);\n    write16(r.a[5]+(60),value);','read16(0x5cee);\n    nz(value);\n    write16(r.a[5]+(60),value+1);','cpu_recovery'),
  ('attract-st','src/main/engine/omainrecoveryschedule.inc','write8(r.a[0]+(2),0xff);','write8(r.a[0]+(2),0xfe);','cpu_recovery'),
  ('formation-tas','src/main/engine/omainrecoveryschedule.inc','byte(r.d[3],old|0x80);','byte(r.d[3],old|0x40);','cpu_recovery'),
  ('signed-multiply-clock',main,'for(auto v=uint16_t(operand^(operand<<1));v;v>>=1)transitions+=v&1;','for(auto v=operand;v;v>>=1)transitions+=v&1;','cpu_recovery'),
  ('formation-asl-carry','src/main/engine/omainrecoveryschedule.inc','(old&0x8000?17:0)|((old^value)&0x8000?2:0)','(old&0x8000?0:0)|((old^value)&0x8000?2:0)','cpu_recovery'),
 ]
if args.only:
 selected=[m for m in mutations if m[0]==args.only]
 if not selected:parser.error('Unknown mutation: '+args.only)
 mutations=selected
if args.recovery is not None:
 # A failed baseline must never masquerade as a rejected mutation when
 # independent increments are supplied directly rather than through make.
 subprocess.run(['make','build/oracle_cpu_coverage'],cwd=root,capture_output=True,check=True)
 for fixture in recovery_captures:
  baseline=subprocess.run([str(root/'build/oracle_cpu_coverage'),fixture],cwd=root,capture_output=True,text=True)
  if baseline.returncode!=0:
   raise SystemExit(f'Baseline recovery capture failed: {fixture}; mutation evidence is invalid.\n{baseline.stderr[-2000:]}')
with tempfile.TemporaryDirectory(prefix='cpu-mutations-',dir=root/'build/oracle') as temp:
 overlay=Path(temp);engine=overlay/'src/main/engine';engine.mkdir(parents=True)
 for name,target,old,new,capture in mutations:
  for f in engine.iterdir():f.unlink()
  text=(root/target).read_text();assert text.count(old)==1,name
  local=engine/Path(target).name;local.write_text(text.replace(old,new))
  binary=overlay/name;test='tests/oracle_cpu_nested_irq.cpp' if capture=='cpu_nested_irq' else 'tests/oracle_cpu_coverage.cpp';exe='build/oracle_cpu_nested_irq' if capture=='cpu_nested_irq' else 'build/oracle_cpu_coverage'
  dry=subprocess.run(['make','-n','-W',test,exe],cwd=root,capture_output=True,text=True,check=True).stdout
  commands=[shlex.split(line) for line in dry.splitlines() if test in line and ' -o ' in line];assert len(commands)==1
  cmd=commands[0];cmd[1:1]=['-I'+str(overlay/'src/main'),'-Isrc/main/engine']
  source=main if target==main or (target.endswith('.inc') and Path(target).name.startswith('omain')) else 'src/main/engine/osubprogress.cpp' if target.endswith('osubprogress.inc') else sub
  if target.endswith('.inc'):(engine/Path(source).name).write_text((root/source).read_text())
  for i,arg in enumerate(cmd):
   if arg==source or arg=='build/oracle_obj/'+source.replace('.cpp','.o'):cmd[i]=str(engine/Path(source).name)
  cmd[cmd.index('-o')+1]=str(binary)
  subprocess.run(cmd,cwd=root,capture_output=True,check=True)
  errors=('CPU fixture bus comparison failed','CPU fixture state comparison failed','nested phase differs','nested post-state differs','nested selection differs')
  rejected=False
  captures=recovery_captures if capture=='cpu_recovery' else [f'build/oracle/{capture}.bin']
  for fixture in captures:
   result=subprocess.run([str(binary),fixture],cwd=root,capture_output=True,text=True)
   if result.returncode==1 and any(e in result.stderr for e in errors):rejected=True
   elif result.returncode!=0:raise SystemExit(f'Mutation {name} failed without fixture evidence: {result.stderr}')
  if not rejected:raise SystemExit(f'Mutation {name} escaped comparison')
  print(f'CPU coverage mutation {name}: rejected by {capture}',flush=True)
