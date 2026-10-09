#!/usr/bin/env python3
"""Reject timing, interrupt, reset and device mutations in private builds."""
from pathlib import Path
import os, shlex, subprocess, tempfile

root=Path(__file__).resolve().parents[2]
fixtures={'boot':'sound_execution','nmi':'sound_execution_full','irq':'sound_irq'}
for name in fixtures.values():
    result=subprocess.run([str(root/'build/oracle_sound_execution'),str(root/f'build/oracle/{name}.bin'),str(root/f'build/oracle/{name}.txt')],cwd=root,capture_output=True,text=True,timeout=30)
    if result.returncode:raise SystemExit(f'Baseline {name} failed: {result.stderr}')
cpu=(root/'src/main/hwaudio/xboard_sound_cpu.cpp').read_text().replace('"../../third_party/z80.h"',f'"{root}/src/third_party/z80.h"')
helper=(root/'tests/sound_execution_fixture.hpp').read_text()
timers=(root/'src/main/hwaudio/xboard_ym_timers.hpp').read_text()
mutations=[
 ('bus-phase','boot','cpu','?2:1;','?2:0;'),
 ('nmi-bus','nmi','cpu','&&s.cpu.step==Z80_NMI_STEP)', '&&s.cpu.step==Z80_NMI_STEP&&false)'),
 ('latch-value','nmi','cpu','d=latch_read();','d=latch_read()^1;'),
 ('irq-disabled','irq','cpu','state->irq=asserted;','asserted=false;state->irq=asserted;'),
 ('reset-registers','irq','cpu','s.cpu.af=saved.af;','s.cpu.af=0xffff;'),
 ('nmi-clear','nmi','helper','nmi.emplace_back(running?now():provider_ns,asserted);','if(asserted)nmi.emplace_back(running?now():provider_ns,asserted);'),
 ('ym-timer-a','irq','timers','1024-((unsigned','1025-((unsigned'),
]
with tempfile.TemporaryDirectory(prefix='sound-execution-mutations-',dir=root/'build/oracle') as directory:
    work=Path(directory)
    for name,fixture,owner,old,new in mutations:
        originals={'cpu':cpu,'timers':timers,'helper':helper}
        if originals[owner].count(old)!=1:raise SystemExit(f'Mutation anchor missing or ambiguous: {name}')
        cpp=work/'cpu.cpp';cpp.write_text(cpu.replace(old,new) if owner=='cpu' else cpu)
        timer=work/'timers.hpp';timer.write_text(timers.replace(old,new) if owner=='timers' else timers)
        h=work/'sound_execution_fixture.hpp';h.write_text((helper.replace(old,new) if owner=='helper' else helper).replace('"hwaudio/xboard_ym_timers.hpp"',f'"{timer}"'))
        source=work/'oracle.cpp';source.write_text((root/'tests/oracle_sound_execution.cpp').read_text())
        binary=work/name
        command=shlex.split(os.environ.get('CXX','c++'))+shlex.split(os.environ.get('CXXFLAGS','-std=c++20 -Wall -Wextra -O2'))
        subprocess.run(command+['-Isrc/main',str(source),str(cpp),'src/third_party/ymfm/ymfm_opm.cpp','-o',str(binary)],cwd=root,check=True)
        stem=fixtures[fixture]
        result=subprocess.run([str(binary),str(root/f'build/oracle/{stem}.bin'),str(root/f'build/oracle/{stem}.txt')],cwd=root,capture_output=True,text=True,timeout=30)
        if result.returncode!=1 or not any(message in result.stderr for message in ['native continuous sound differs','native IRQ timestamp or retained caller differs','native sound grant completion differs']):
            raise SystemExit(f'Mutation {name} did not produce an oracle mismatch: {result.stdout}{result.stderr}')
        print(f'Sound execution mutation {name}: rejected by MAME comparison')
