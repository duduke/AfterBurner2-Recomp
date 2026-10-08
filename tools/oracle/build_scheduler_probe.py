#!/usr/bin/env python3
"""Build a read-only scheduler trace MAME beside its existing binary.

Uses existing MAME objects; never changes its source, objects, or installed
binary. Trace output is an ignored oracle capture. CPU budgets, returns,
quantum assertions and next timer boundaries contain no ROM bytes. The
optional CPU observer saves instruction-entry registers for Lua without
changing CPU state or execution timing.
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
FILE* ab2_devices(){static FILE* f=[](){auto p=std::getenv("AB2_DEVICE_TRACE");return p?std::fopen(p,"w"):nullptr;}();return f;}
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
s=s.replace('// by default, assume our target is the end of the next quantum', '''if(auto f=ab2_devices())std::fprintf(f,"H %llu %llu %llu %s\\n",(unsigned long long)ab2_grant_index,ab2_time(m_basetime),ab2_time(m_timer_list->m_expire),m_timer_list->m_callback.name()?m_timer_list->m_callback.name():"<none>");
        // by default, assume our target is the end of the next quantum''')
s=s.replace('timer.m_callback(timer.m_param);', '''if(auto f=ab2_devices())std::fprintf(f,"F %llu %llu %s\\n",ab2_time(m_basetime),ab2_time(timer.m_expire),timer.m_callback.name());
                timer.m_callback(timer.m_param);''')
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
# Read-only per-device instruction snapshots, before any instruction effects.
# Lua reads these saved observer arrays; no debugger focus or CPU setters.
cpusource=(root/'src/devices/cpu/m68000/m68000.cpp').read_text()
cpusource=cpusource.replace('#include "m68000.h"', '#include "m68000.h"\n#include <cstdlib>\n#include <cstring>\nnamespace { std::array<std::array<u64,22>,2> ab2_snapshots{},ab2_reset_snapshots{}; std::array<std::array<u64,24>,2> ab2_irq_snapshots{};\nint ab2_slot(const char* tag){return !std::strcmp(tag,":mainpcb:maincpu")?0:!std::strcmp(tag,":mainpcb:subcpu")?1:-1;} }')
needle='\tsave_item(NAME(m_da));'
assert cpusource.count(needle)==1
cpusource=cpusource.replace(needle,needle+'\n\tif(std::getenv("AB2_CPU_PROBE") && ab2_slot(tag())>=0){auto& ab2_cpu_snapshot=ab2_snapshots[ab2_slot(tag())]; save_item(NAME(ab2_cpu_snapshot));}')
cpusource=cpusource.replace(needle,needle+'\n\tif(std::getenv("AB2_CPU_PROBE") && ab2_slot(tag())>=0){auto& ab2_reset_snapshot=ab2_reset_snapshots[ab2_slot(tag())]; save_item(NAME(ab2_reset_snapshot));}')
cpusource=cpusource.replace(needle,needle+'\n\tif(std::getenv("AB2_CPU_PROBE") && ab2_slot(tag())>=0){auto& ab2_irq_snapshot=ab2_irq_snapshots[ab2_slot(tag())]; save_item(NAME(ab2_irq_snapshot));}')
needle='\t\t\t(this->*(m_handlers_f[m_inst_state]))();'
assert cpusource.count(needle)==1
cpusource=cpusource.replace(needle,'''\t\t\tif(m_inst_state==S_INTERRUPT && std::getenv("AB2_CPU_PROBE") && ab2_slot(tag())>=0){
                auto& v=ab2_irq_snapshots[ab2_slot(tag())]; ++v[0];
                v[1]=total_cycles(); v[2]=machine().time().as_ticks(1000000000);
                v[3]=m_pc-2; v[4]=m_sr;
                for(unsigned i=0;i<17;++i)v[5+i]=m_da[i];
                v[22]=(m_next_state>>24)&7; v[23]=m_ipc;
            }
\t\t\tif(m_inst_state==S_RESET && std::getenv("AB2_CPU_PROBE") && ab2_slot(tag())>=0){
                auto& v=ab2_reset_snapshots[ab2_slot(tag())]; ++v[0];
                v[1]=total_cycles(); v[2]=machine().time().as_ticks(1000000000);
                v[3]=m_ipc; v[4]=m_sr;
                for(unsigned i=0;i<17;++i)v[5+i]=m_da[i];
            }
'''+needle)
needle='\t\t\t\tm_irdi = m_ird;'
assert cpusource.count(needle)==1
cpusource=cpusource.replace(needle,needle+'''
                if(std::getenv("AB2_CPU_PROBE") && ab2_slot(tag())>=0){
                    auto& v=ab2_snapshots[ab2_slot(tag())]; ++v[0];
                    v[1]=total_cycles(); v[2]=machine().time().as_ticks(1000000000);
                    v[3]=m_ipc; v[4]=m_sr;
                    for(unsigned i=0;i<17;++i)v[5+i]=m_da[i];
                }
''')
cpupath=out/'m68000.cpp';cpupath.write_text(cpusource)
command=dry('optional.make','../../../../osx_clang/obj/x64/Release/src/devices/cpu/m68000/m68000.o','../../../../../src/devices/cpu/m68000/m68000.cpp')
for i,a in enumerate(command):
 if a=='-o':command[i+1]=str(out/'m68000.o')
 if a=='../../../../../src/devices/cpu/m68000/m68000.cpp':command[i]=str(cpupath)
if '-include' in command:
 i=command.index('-include');del command[i:i+2]
command+=['-I'+str(root/'src/devices/cpu/m68000')]
subprocess.run(command,cwd=cwd,check=True)
optional=out/'liboptional.a';optional.write_bytes((root/'build/osx_clang/bin/x64/Release/mame_mame/liboptional.a').read_bytes())
subprocess.run(['ar','r',str(optional),str(out/'m68000.o')],check=True)
roadsource=(root/'src/mame/sega/segaic16_road.cpp').read_text()
roadsource=roadsource.replace('#include "emu.h"','#include "emu.h"\n#include <cstdlib>')
needle='info->buffer = std::make_unique<u16[]>(0x1000/2);'
assert roadsource.count(needle)==1
roadsource=roadsource.replace(needle,needle+'\n\t\t\tif(std::getenv("AB2_CPU_PROBE")){auto& ab2_road_buffer=info->buffer;save_pointer(NAME(ab2_road_buffer),0x1000/2,which);}')
roadpath=out/'segaic16_road.cpp';roadpath.write_text(roadsource)
command=dry('sega.make','../../../../osx_clang/obj/x64/Release/src/mame/sega/segaic16_road.o','../../../../../src/mame/sega/segaic16_road.cpp')
for i,a in enumerate(command):
 if a=='-o':command[i+1]=str(out/'segaic16_road.o')
 if a=='../../../../../src/mame/sega/segaic16_road.cpp':command[i]=str(roadpath)
if '-include' in command:
 i=command.index('-include');del command[i:i+2]
command+=['-I'+str(root/'src/mame/sega')]
subprocess.run(command,cwd=cwd,check=True)
sega=out/'libsega.a';sega.write_bytes((root/'build/osx_clang/bin/x64/Release/mame_mame/libsega.a').read_bytes())
subprocess.run(['ar','r',str(sega),str(out/'segaic16_road.o')],check=True)
xsource=(root/'src/mame/sega/segaxbd.cpp').read_text()
xsource=xsource.replace('#include "emu.h"','#include "emu.h"\n#include <cstdio>\n#include <cstdlib>')
needle='uint32_t segaxbd_state::screen_update(screen_device &screen, bitmap_ind16 &bitmap, const rectangle &cliprect)\n{'
assert xsource.count(needle)==1
xsource=xsource.replace(needle,needle+'''
    if(auto p=std::getenv("AB2_SCREEN_TRACE")){
        static FILE* trace=std::fopen(p,"w");
        if(trace)std::fprintf(trace,"%llu %d %d %d %d %d\\n",(unsigned long long)machine().time().as_ticks(1000000000),cliprect.min_x,cliprect.max_x,cliprect.min_y,cliprect.max_y,int(m_segaic16vid->m_display_enable));
    }
''')
xpath=out/'segaxbd.cpp';xpath.write_text(xsource)
command=dry('sega.make','../../../../osx_clang/obj/x64/Release/src/mame/sega/segaxbd.o','../../../../../src/mame/sega/segaxbd.cpp')
for i,a in enumerate(command):
 if a=='-o':command[i+1]=str(out/'segaxbd.o')
 if a=='../../../../../src/mame/sega/segaxbd.cpp':command[i]=str(xpath)
if '-include' in command:
 i=command.index('-include');del command[i:i+2]
command+=['-I'+str(root/'src/mame/sega')]
subprocess.run(command,cwd=cwd,check=True)
subprocess.run(['ar','r',str(sega),str(out/'segaxbd.o')],check=True)
link=dry('mame.make',changed='../../../../osx_clang/bin/x64/Release/libemu.a')
for i,a in enumerate(link):
 if a=='-o':link[i+1]=str(out/'mame')
 if a.endswith('/libemu.a'):link[i]=str(archive)
 if a.endswith('/liboptional.a'):link[i]=str(optional)
 if a.endswith('/libsega.a'):link[i]=str(sega)
subprocess.run(link,cwd=cwd,check=True)
print(out/'mame')
