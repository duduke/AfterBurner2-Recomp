#!/usr/bin/env python3
"""Build a read-only PCM observer alongside existing MAME objects.

Never changes MAME sources, objects or installed binary. Captures PCM
register accesses, actual pre-routing stream samples and address/fraction
write-back. Stream update sizes/times remain supplied acceptance boundaries.
"""
import argparse
from pathlib import Path
import shlex
import subprocess

p=argparse.ArgumentParser()
p.add_argument('mame',type=Path)
p.add_argument('output',type=Path)
args=p.parse_args()
root=args.mame.resolve();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
cwd=root/'build/projects/sdl3/mame/gmake-osx-clang'
source=(root/'src/devices/sound/segapcm.cpp').read_text()
helper=r'''
namespace {
FILE* ab2_pcm_trace(){static FILE* f=[](){auto p=std::getenv("AB2_PCM_TRACE");return p?std::fopen(p,"wb"):nullptr;}();return f;}
void ab2_pcm_number(FILE* f,uint64_t v,unsigned n){while(n--)std::fputc((v>>(8*n))&255,f);}
}
'''
source=source.replace('#include "segapcm.h"','#include "segapcm.h"\n#include <cstdio>\n#include <cstdlib>\n#include <cmath>\n'+helper)
def replace(old,new):
    global source
    if source.count(old)!=1:raise SystemExit(f'MAME probe anchor missing/ambiguous: {old[:70]}')
    source=source.replace(old,new)
replace('\tsave_item(NAME(m_low));',r'''
    if(auto f=ab2_pcm_trace()){
        std::fwrite("AB2PCM02",1,8,f);
        std::fputc(std::getenv("AB2_PCM_SYNTHETIC")?1:0,f);
        std::fwrite(m_ram.get(),1,0x800,f);std::fwrite(m_low,1,16,f);
    }
'''+'\tsave_item(NAME(m_low));')
replace('\t\t\tm_low[ch] = regs[0x86] & 1 ? 0 : addr;\n\t\t}\n\t}\n}',r'''
            m_low[ch] = regs[0x86] & 1 ? 0 : addr;
        }
    }
    if(auto f=ab2_pcm_trace()){
        std::fputc(2,f);ab2_pcm_number(f,stream.samples(),4);
        for(int i=0;i<stream.samples();++i){
            ab2_pcm_number(f,int32_t(std::llround(stream.get_output(0,i)*32768.0)),4);
            ab2_pcm_number(f,int32_t(std::llround(stream.get_output(1,i)*32768.0)),4);
        }
        std::fwrite(m_ram.get(),1,0x800,f);std::fwrite(m_low,1,16,f);
    }
}
''')
replace('\tm_ram[offset & 0x07ff] = data;',r'''
    m_ram[offset & 0x07ff] = data;
    if(auto f=ab2_pcm_trace()){
        std::fputc(1,f);ab2_pcm_number(f,offset&0x7ff,2);std::fputc(data,f);
    }
''')
replace('\treturn m_ram[offset & 0x07ff];',r'''
    if(auto f=ab2_pcm_trace()){
        std::fputc(3,f);ab2_pcm_number(f,offset&0x7ff,2);std::fputc(m_ram[offset&0x7ff],f);
    }
    return m_ram[offset & 0x07ff];
''')
cpp=out/'segapcm.cpp';cpp.write_text(source)
def dry(makefile,target=None,changed=None):
    command=['make','-n','-f',makefile,'config=release64']
    if target:command.append(target)
    if changed:command+=['-W',changed]
    result=subprocess.run(command,cwd=cwd,text=True,capture_output=True,check=True)
    commands=[line for line in result.stdout.splitlines()if line.startswith('clang++ ')]
    if len(commands)!=1:raise SystemExit(f'Expected one compile/link command: {makefile}, got {len(commands)}')
    return shlex.split(commands[0])
command=dry('optional.make','../../../../osx_clang/obj/x64/Release/src/devices/sound/segapcm.o','../../../../../src/devices/sound/segapcm.cpp')
for i,a in enumerate(command):
    if a=='-o':command[i+1]=str(out/'segapcm.o')
    if a=='../../../../../src/devices/sound/segapcm.cpp':command[i]=str(cpp)
if '-include' in command:
    i=command.index('-include');del command[i:i+2]
command+=['-I'+str(root/'src/devices/sound')]
subprocess.run(command,cwd=cwd,check=True)
archive=out/'liboptional.a'
archive.write_bytes((root/'build/osx_clang/bin/x64/Release/mame_mame/liboptional.a').read_bytes())
subprocess.run(['ar','r',str(archive),str(out/'segapcm.o')],check=True)
link=dry('mame.make',changed='../../../../osx_clang/bin/x64/Release/mame_mame/liboptional.a')
for i,a in enumerate(link):
    if a=='-o':link[i+1]=str(out/'mame')
    if a.endswith('/liboptional.a'):link[i]=str(archive)
subprocess.run(link,cwd=cwd,check=True)
print(out/'mame')
