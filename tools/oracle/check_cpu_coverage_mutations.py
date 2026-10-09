#!/usr/bin/env python3
"""Reject source mutations against original-ROM instruction/exception captures."""
from pathlib import Path
import shlex,subprocess,tempfile
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
  source=main if target==main or target.endswith('omainhostcallerschedule.inc') else 'src/main/engine/osubprogress.cpp' if target.endswith('osubprogress.inc') else sub
  if target.endswith('.inc'):(engine/Path(source).name).write_text((root/source).read_text())
  for i,arg in enumerate(cmd):
   if arg==source or arg=='build/oracle_obj/'+source.replace('.cpp','.o'):cmd[i]=str(engine/Path(source).name)
  cmd[cmd.index('-o')+1]=str(binary)
  subprocess.run(cmd,cwd=root,capture_output=True,check=True)
  result=subprocess.run([str(binary),f'build/oracle/{capture}.bin'],cwd=root,capture_output=True,text=True)
  errors=('CPU fixture bus comparison failed','CPU fixture state comparison failed','nested phase differs','nested post-state differs','nested selection differs')
  if result.returncode!=1 or not any(e in result.stderr for e in errors):raise SystemExit(f'Mutation {name} escaped comparison: {result.stderr}')
  print(f'CPU coverage mutation {name}: rejected by {capture}',flush=True)
