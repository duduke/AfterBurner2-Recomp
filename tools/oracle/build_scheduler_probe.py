#!/usr/bin/env python3
"""Build a read-only scheduler trace MAME beside its existing binary.

Uses existing MAME objects; never changes its source, objects, or installed
binary. Trace output is an ignored oracle capture. CPU budgets, returns,
quantum assertions and next timer boundaries contain no ROM bytes.
"""
from pathlib import Path
import argparse,shlex,subprocess
p=argparse.ArgumentParser();p.add_argument('mame',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
root=args.mame.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
cwd=root/'build/projects/sdl3/mame/gmake-osx-clang'
s=(root/'src/emu/schedule.cpp').read_text()
helper='''
// Read-only AB2 oracle instrumentation. No gameplay or scheduling mutations.
namespace {
u64 ab2_grant_index=0;
FILE* ab2_trace(){static FILE* f=[](){auto p=std::getenv("AB2_SCHED_TRACE");return p?std::fopen(p,"w"):nullptr;}();return f;}
unsigned long long ab2_time(attotime t){return (unsigned long long)t.seconds()*1000000000000000000ULL+t.attoseconds();}
}
'''
s=s.replace('machine.save().save_item(NAME(m_basetime));', 'machine.save().save_item(NAME(m_basetime));\n\tif(ab2_trace())machine.save().save_item(NAME(ab2_grant_index));')
s=s.replace('#include "debugger.h"','#include "debugger.h"\n#include <cstdio>\n#include <cstdlib>\n'+helper)
s=s.replace('bool call_debugger =', 'if(auto f=ab2_trace())std::fprintf(f,"T %llu\\n",ab2_time(m_basetime));\n\tbool call_debugger =')
s=s.replace('// by default, assume our target is the end of the next quantum', '''if(auto f=ab2_trace())std::fprintf(f,"G %llu %llu %llu %lld\\n",(unsigned long long)++ab2_grant_index,ab2_time(m_basetime),ab2_time(m_timer_list->m_expire),(long long)m_quantum_list.first()->m_actual);
		// by default, assume our target is the end of the next quantum''')
s=s.replace('// only process if this CPU', 'if(auto f=ab2_trace())std::fprintf(f,"C %s %llu %u %d\\n",exec->device().tag(),ab2_time(exec->m_localtime),exec->m_suspend,exec->m_eatcycles);\n\t\t\t// only process if this CPU')
s=s.replace('// compute how many attoseconds to execute this CPU', '''auto ab2_start=exec->m_localtime;
				// compute how many attoseconds to execute this CPU''')
s=s.replace('// if we\'re not suspended, actually execute', 'const int ab2_budget=ran;\n\t\t\t\t\t// if we\'re not suspended, actually execute')
s=s.replace('// account for these cycles', '''if(auto f=ab2_trace())std::fprintf(f,"R %s %llu %llu %d %d\\n",exec->device().tag(),ab2_time(ab2_start),ab2_time(target),ab2_budget,ran);
					// account for these cycles''')
s=s.replace('m_basetime = target;','m_basetime = target;\n\t\tif(auto f=ab2_trace())std::fprintf(f,"E %llu\\n",ab2_time(m_basetime));')
needle='void device_scheduler::add_quantum(const attotime &quantum, const attotime &duration)\n{'
assert needle in s
s=s.replace(needle,needle+'\n\tif(auto f=ab2_trace())std::fprintf(f,"Q %llu %llu %llu\\n",ab2_time(time()),ab2_time(quantum),ab2_time(duration));')
source=out/'schedule.cpp';source.write_text(s)
def dry(makefile,target=None,changed=None):
 cmd=['make','-n','-f',makefile,'config=release64']
 if target:cmd.append(target)
 if changed:cmd+=['-W',changed]
 result=subprocess.run(cmd,cwd=cwd,text=True,check=True,capture_output=True)
 lines=[l for l in result.stdout.splitlines()if l.startswith('clang++ ')]
 assert len(lines)==1,(makefile,len(lines))
 return shlex.split(lines[0])
compile=dry('emu.make','../../../../osx_clang/obj/x64/Release/src/emu/schedule.o','../../../../../src/emu/schedule.cpp')
for i,a in enumerate(compile):
 if a=='-o':compile[i+1]=str(out/'schedule.o')
 if a=='../../../../../src/emu/schedule.cpp':compile[i]=str(source)
# Compile without the stale bundled PCH; the source includes emu.h.
if '-include' in compile:
 i=compile.index('-include');del compile[i:i+2]
# Dependency files must stay beside our new object.
subprocess.run(compile,cwd=cwd,check=True)
archive=out/'libemu.a';archive.write_bytes((root/'build/osx_clang/bin/x64/Release/libemu.a').read_bytes())
subprocess.run(['ar','r',str(archive),str(out/'schedule.o')],check=True)
link=dry('mame.make',changed='../../../../osx_clang/bin/x64/Release/libemu.a')
for i,a in enumerate(link):
 if a=='-o':link[i+1]=str(out/'mame')
 if a.endswith('/libemu.a'):link[i]=str(archive)
subprocess.run(link,cwd=cwd,check=True)
print(out/'mame')
