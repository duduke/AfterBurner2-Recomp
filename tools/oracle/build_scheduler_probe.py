#!/usr/bin/env python3
"""Build an isolated scheduler/video trace MAME beside its existing binary.

Uses existing MAME objects; never changes its source, objects, or installed
binary. Trace output is an ignored oracle capture. CPU budgets, returns,
quantum assertions and next timer boundaries contain no ROM bytes. The
optional CPU observer saves instruction-entry registers for Lua without
changing CPU state or execution timing. AB2_CPU_IRQ_FIXTURE explicitly
drives directed main IRQ pins after a real SR write/VPA lookup; normal
captures leave it disabled. AB2_DISPLAY_FIXTURE explicitly
enables separately recorded test display changes; natural runs omit it.
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
                    if(std::getenv("AB2_CPU_IRQ_FIXTURE") && ab2_slot(tag())==0 && m_ipc==0x3ffd00){
                        set_input_line(4,CLEAR_LINE);set_input_line(6,CLEAR_LINE);set_input_line(2,ASSERT_LINE);
                    }
                }
''')
# Assert the higher pin after the first VPA/vector lookup, whose normal
# microcode clears the previous selection. No microcode/state patch.
irq_end='\tm_int_vector = (m_edb & 0xff) << 2;\n\tm_int_next_state = 0;'
assert cpusource.count(irq_end)==1
cpusource=cpusource.replace(irq_end,irq_end+'\n\tif(std::getenv("AB2_CPU_IRQ_FIXTURE") && ab2_slot(tag())==0 && m_int_vector==26*4)execute_set_input(6,ASSERT_LINE);')
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
# Read-only Z80 interrupt acknowledgements, including retained caller registers.
# The generated decoder is copied; the original MAME sources stay untouched.
zsource=(root/'src/devices/cpu/z80/z80.cpp').read_text()
zsource=zsource.replace('#include "emu.h"','#include "emu.h"\n#include <cstdio>\n#include <cstdlib>')
zh=(root/'build/generated/emu/cpu/z80/z80.hxx').read_text()
needle='m_tmp_irq_vector = (intf != nullptr) ? intf->z80daisy_irq_ack() : standard_irq_callback(0, m_pc.w);'
assert zh.count(needle)==2
zh=zh.replace(needle,needle+'''
        if(auto path=std::getenv("AB2_Z80_IRQ_TRACE")) {
            static FILE* file=std::fopen(path,"w");
            if(file)std::fprintf(file,"%llu %u %u %u %u %u %u %u %u\\n",
                (unsigned long long)machine().time().as_ticks(1000000000),
                m_prvpc.w,m_pc.w,m_sp.w,(m_af.b.h<<8)|get_f(),m_bc.w,m_de.w,m_hl.w,m_tmp_irq_vector&255);
        }
''')
zdir=out/'cpu/z80';zdir.mkdir(parents=True,exist_ok=True)
(zdir/'z80.hxx').write_text(zh)
zpath=out/'z80.cpp';zpath.write_text(zsource)
command=dry('optional.make','../../../../osx_clang/obj/x64/Release/src/devices/cpu/z80/z80.o','../../../../../src/devices/cpu/z80/z80.cpp')
for i,a in enumerate(command):
 if a=='-o':command[i+1]=str(out/'z80.o')
 if a=='../../../../../src/devices/cpu/z80/z80.cpp':command[i]=str(zpath)
if '-include' in command:
 i=command.index('-include');del command[i:i+2]
command.insert(1,'-I'+str(out))
command+=['-I'+str(root/'src/devices/cpu/z80')]
subprocess.run(command,cwd=cwd,check=True)
subprocess.run(['ar','r',str(optional),str(out/'z80.o')],check=True)
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
# At function exit all async sprite work has completed in the enabled path.
xsource=xsource.replace(needle,needle+'''
    auto ab2_pixels = [&, this]() {
        if(auto p=std::getenv("AB2_PIXEL_TRACE")) {
            static FILE* f=std::fopen(p,"wb");
            auto put=[&](u64 v,unsigned bytes){while(bytes--)std::fputc((v>>(bytes*8))&255,f);};
            if(!f)return;
            put(machine().time().as_ticks(1000000000),8);put(cliprect.min_y,2);put(cliprect.max_y,2);put(m_segaic16vid->m_display_enable,1);
            for(int y=cliprect.min_y;y<=cliprect.max_y;++y)for(int x=cliprect.min_x;x<=cliprect.max_x;++x){
                auto index=bitmap.pix(y,x);put(index,2);put(screen.priority().pix(y,x),1);put(u32(m_palette->pen_color(index))&0xffffff,3);
            }
        }
    };
    struct ab2_on_exit { decltype(ab2_pixels)& fn; ~ab2_on_exit(){fn();} } ab2_guard{ab2_pixels};
''')
# Explicit directed device fixture, separately captured and replayed as supplied.
needle_fixture='TIMER_CALLBACK_MEMBER(segaxbd_state::scanline_tick)\n{'
assert xsource.count(needle_fixture)==1
xsource=xsource.replace(needle_fixture,needle_fixture+'''
    if(auto p=std::getenv("AB2_DISPLAY_FIXTURE")) {
        const auto ns=machine().time().as_ticks(1000000000);
        if(ns>1000000000 && (param==64 || param==128 || param==260)) {
            static FILE* f=std::fopen(p,"w");
            const unsigned enabled=param!=64;
            static unsigned address=[this](){
                const auto* gfx=reinterpret_cast<const u32*>(memregion("sprites")->base());
                for(unsigned a=0;a<65536;++a){bool has_a=false;for(unsigned n=0;n<8;++n)has_a|=((gfx[a]>>(4*n))&15)==10;
                    if(has_a&&(gfx[a]&0xf0)==0xf0)return a;}
                fatalerror("directed sprite fixture requires an actual ROM row with effect pen and end marker");return 0u;
            }();
            const unsigned count=param==64?0:112+257+8192;
            if(f)std::fprintf(f,"%llu %u %u",(unsigned long long)ns,enabled,count);
            m_segaic16vid->set_display_enable(enabled);
            for(unsigned i=0;i<count;++i){
                if(i>=112 && i<369){const unsigned word=i-112,sprite=word/8,slot=word%8;
                    const u16 descriptor[]={0x100,u16(address),u16(189+sprite*8),u16(0x4200|((sprite%4)<<12)),0xe200,223,u16(0x20+sprite),u16(address)};
                    const u16 value=word==256?0x8000:descriptor[slot];
                    if(f)std::fprintf(f," %u %u",0x1000+word,unsigned(value));m_sprites->buffer()[word]=value;continue;}
                if(i>=369){const unsigned word=i-369;
                    const u16 value=(m_paletteram[word]&0x7fff)|(((ns/16768000)&1)?0x8000:0);
                    if(f)std::fprintf(f," %u %u",0x2000+word,unsigned(value));paletteram_w(word,value,0xffff);continue;}
                const unsigned offset=i<4?0x740+i:i<8?0x748+i-4:i<12?0x74c+i-8:
                    i<56?0x78b+((i-12)/22)*0x20+(i-12)%22:0x7c0+((i-56)/28)*0x20+(i-56)%28;
                const u16 old=m_segaic16vid->m_textram[offset];
                const u16 value=i<4?old^0x1111:i<8?((old^0x100)&0x1ff)|0x8000:i<12?((old^0x200)&0x3ff)|0x8000:
                    i<56?((i-12)*13)&0x1ff:(((i-56)*23)&0x3ff)|(i%4==0?0x8000:0);
                if(f)std::fprintf(f," %u %u",offset,unsigned(value));
                m_segaic16vid->textram_w(offset,value);
            }
            if(f)std::fprintf(f,"\\n");
        }
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
tilesource=(root/'src/mame/sega/segaic16.cpp').read_text().replace('#include "emu.h"','#include "emu.h"\n#include <cstdio>\n#include <cstdlib>')
needle='\t/* set a timer to do this again next frame */'
assert tilesource.count(needle)==1
tilesource=tilesource.replace(needle,'''
    if(auto p=std::getenv("AB2_LATCH_TRACE")) {
        static FILE* f=std::fopen(p,"w");
        if(f){std::fprintf(f,"%llu",(unsigned long long)machine().time().as_ticks(1000000000));
            for(auto v:info->latched_pageselect)std::fprintf(f," %u",v);
            for(auto v:info->latched_yscroll)std::fprintf(f," %u",v);
            for(auto v:info->latched_xscroll)std::fprintf(f," %u",v);
            std::fprintf(f,"\\n");}
    }
'''+needle)
tilepath=out/'segaic16.cpp';tilepath.write_text(tilesource)
command=dry('sega.make','../../../../osx_clang/obj/x64/Release/src/mame/sega/segaic16.o','../../../../../src/mame/sega/segaic16.cpp')
for i,a in enumerate(command):
 if a=='-o':command[i+1]=str(out/'segaic16.o')
 if a=='../../../../../src/mame/sega/segaic16.cpp':command[i]=str(tilepath)
if '-include' in command:
 i=command.index('-include');del command[i:i+2]
command+=['-I'+str(root/'src/mame/sega')]
subprocess.run(command,cwd=cwd,check=True)
subprocess.run(['ar','r',str(sega),str(out/'segaic16.o')],check=True)
# Read-only native audio stream observer. Batch sizes, indexes and time are
# assertions, never playback inputs. Observe before speaker gain/resampling.
audio=(root/'src/emu/sound.cpp').read_text()
audio=audio.replace('#include "emu.h"','#include "emu.h"\n#include <cstdio>\n#include <cstdlib>\n#include <cmath>')
needle='\t// Update the indexes\n\tm_output_buffer.commit(m_samples_to_update);'
assert audio.count(needle)==1
audio=audio.replace(needle,r'''
    if(auto path=std::getenv("AB2_AUDIO_TRACE")) {
        const bool pcm=std::strcmp(m_device.tag(),":mainpcb:pcm")==0;
        const bool fm=std::strcmp(m_device.tag(),":mainpcb:ymsnd")==0;
        if(pcm||fm){
            static FILE* files[2]={nullptr,nullptr};const unsigned slot=pcm?1:0;
            if(!files[slot]){std::string p=std::string(path)+(pcm?".pcm":".fm");
                files[slot]=std::fopen(p.c_str(),"wb");if(files[slot])std::fwrite("AB2AST01",1,8,files[slot]);}
            if(auto f=files[slot]){
                auto put=[&](uint64_t v,unsigned n){while(n--)std::fputc((v>>(n*8))&255,f);};
                put(m_device.machine().time().as_ticks(1000000000),8);
                put(m_output_buffer.write_sample(),8);put(m_samples_to_update,4);
                for(u32 i=0;i<m_samples_to_update;++i)for(unsigned ch=0;ch<2;++ch)
                    put(int32_t(std::llround(get_output(ch,i)*32768.0)),4);
            }
        }
    }
'''+needle)
# Observe MAME's mixed speaker inputs before optional host effects. Float bits
# are exact assertions; speaker PCM conversion is MAME's truncation + clamp.
needle='\tauto &spk = m_speakers[id];\n\tauto &out = spk.m_buffer;'
assert audio.count(needle)==1
audio=audio.replace(needle,r'''
    if(auto path=std::getenv("AB2_AUDIO_TRACE")) {
        static FILE* f=nullptr;static uint64_t first=0;
        if(!f){std::string p=std::string(path)+".speaker";f=std::fopen(p.c_str(),"wb");if(f)std::fwrite("AB2SPK01",1,8,f);}
        if(f){auto put=[&](uint64_t v,unsigned n){while(n--)std::fputc((v>>(n*8))&255,f);};
            put(machine().time().as_ticks(1000000000),8);put(first,8);put(stream.samples(),4);
            for(int i=0;i<stream.samples();++i)for(int ch=0;ch<stream.input_count();++ch){
                float value=stream.get(ch,i);uint32_t bits;std::memcpy(&bits,&value,4);put(bits,4);}
            first+=stream.samples();
        }
    }
'''+needle)
audiopath=out/'sound.cpp';audiopath.write_text(audio)
command=dry('emu.make','../../../../osx_clang/obj/x64/Release/src/emu/sound.o','../../../../../src/emu/sound.cpp')
for i,a in enumerate(command):
 if a=='-o':command[i+1]=str(out/'sound.o')
 if a=='../../../../../src/emu/sound.cpp':command[i]=str(audiopath)
if '-include' in command:
 i=command.index('-include');del command[i:i+2]
command+=['-I'+str(root/'src/emu')]
subprocess.run(command,cwd=cwd,check=True)
subprocess.run(['ar','r',str(archive),str(out/'sound.o')],check=True)
link=dry('mame.make',changed='../../../../osx_clang/bin/x64/Release/libemu.a')
for i,a in enumerate(link):
 if a=='-o':link[i+1]=str(out/'mame')
 if a.endswith('/libemu.a'):link[i]=str(archive)
 if a.endswith('/liboptional.a'):link[i]=str(optional)
 if a.endswith('/libsega.a'):link[i]=str(sega)
subprocess.run(link,cwd=cwd,check=True)
print(out/'mame')
