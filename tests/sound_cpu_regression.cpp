#include "hwaudio/xboard_sound_cpu.hpp"
#include "hwaudio/xboard_pcm_startup.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <algorithm>

namespace {
void check(bool ok,const char* reason) {if(!ok)throw std::runtime_error(reason);}
void run(XBoardSoundCpu& cpu,unsigned clocks) {while(clocks--)cpu.tick();}
// Handwritten test programs, not commercial ROM bytes or gameplay fixtures.
void put(std::vector<uint8_t>& p,unsigned a,std::initializer_list<uint8_t> code) {
    std::copy(code.begin(),code.end(),p.begin()+a);
}
}
int main() try {
    XBoardPcmStartup stopped_pcm;
    check(stopped_pcm.read(0)==0xff&&stopped_pcm.read(0x86)==0xff,"PCM stopped startup state");
    stopped_pcm.write(0x84,0x12);check(stopped_pcm.read(0x84)==0x12,"PCM stopped register write");
    bool active_rejected=false;
    try{stopped_pcm.write(0xfe,0);}catch(const std::runtime_error&){active_rejected=true;}
    check(active_rejected&&stopped_pcm.read(0xfe)==0xff,"active PCM channel silently accepted");
    std::vector<uint8_t> p(0x10000,0);
    // LD SP,FF00; LD A,12; LD BC,3456; EI; NOP; JR self.
    put(p,0,{0x31,0,0xff,0x3e,0x12,0x01,0x56,0x34,0xfb,0,0x18,0xfe});
    // NMI: preserve AF, acknowledge latch, store command, RETN.
    put(p,0x66,{0xf5,0xdb,0x7f,0x32,0,0xf8,0xf1,0xed,0x45});
    // IM1 IRQ: preserve AF, mark RAM, enable interrupts, RETI.
    put(p,0x38,{0xf5,0x3e,0x77,0x32,1,0xf8,0xf1,0xfb,0xed,0x4d});
    XBoardSoundCpu cpu(p.data(),p.size());
    unsigned latches=0,acks=0,loop=0;
    cpu.latch_read=[&](){++latches;cpu.nmi_line(false);return uint8_t(0xa5);};
    cpu.trace=[&](const XBoardSoundCpu::Event& e){
        if(e.kind==XBoardSoundCpu::Kind::InterruptAck){++acks;cpu.irq_line(false);}
        if(e.kind==XBoardSoundCpu::Kind::MemoryRead&&e.pc==10&&e.address==10)++loop;
    };
    run(cpu,150);const auto before=cpu.registers();
    check(loop>2&&before.iff1&&before.bc==0x3456,"initial caller failed");
    cpu.nmi_line(true);run(cpu,200);const auto after=cpu.registers();
    check(latches==1&&cpu.ram[0]==0xa5&&after.af==before.af&&after.bc==before.bc&&after.sp==before.sp&&after.iff1,
          "NMI did not retain caller through acknowledgement and RETN");
    // Retrigger after the acknowledged line has fallen; no queued command gap.
    cpu.nmi_line(true);run(cpu,200);check(latches==2,"second NMI edge lost");
    // Decoder's IM1 is set with an actual instruction, never seeded registers.
    std::vector<uint8_t> irq_program=p;
    put(irq_program,8,{0xed,0x56,0xfb,0,0x18,0xfe});
    XBoardSoundCpu irq_cpu(irq_program.data(),irq_program.size());
    irq_cpu.trace=[&](const XBoardSoundCpu::Event& e){if(e.kind==XBoardSoundCpu::Kind::InterruptAck){++acks;irq_cpu.irq_line(false);}};
    run(irq_cpu,150);auto saved=irq_cpu.registers();irq_cpu.irq_line(true);run(irq_cpu,250);auto restored=irq_cpu.registers();
    check(acks==1&&irq_cpu.ram[1]==0x77&&saved.af==restored.af&&saved.bc==restored.bc&&saved.sp==restored.sp&&restored.iff1,
          "IRQ caller/RETI retention failed");
    const auto clock=cpu.clocks();const auto fetches=loop;cpu.reset_line(true);run(cpu,20);
    check(cpu.clocks()==clock+20&&loop==fetches&&cpu.ram[0]==0xa5,"reset hold changed board RAM or executed CPU");
    cpu.reset_line(false);run(cpu,150);check(cpu.registers().bc==0x3456&&loop>fetches,"reset release did not restart ROM");

    // Exercise the globally masked I/O map and PCM aliases with CPU accesses.
    std::fill(p.begin(),p.end(),0);
    put(p,0,{0x3e,0x5a,0x32,0x23,0xf7,0x3a,0x23,0xf0,0x32,2,0xf8,
             0xd3,0x3e,0xd3,0x3f,0xdb,0x3e,0x32,3,0xf8,0xdb,0x3f,0x32,4,0xf8,
             0xdb,0xff,0x32,5,0xf8,0xd3,0x80,0x76});
    XBoardSoundCpu mapped(p.data(),p.size());
    uint8_t pcm=0;unsigned pcm_reads=0,pcm_writes=0,ym_reads=0,ym_writes=0;
    mapped.pcm_write=[&](uint8_t a,uint8_t d){check(a==0x23,"PCM write mirror");pcm=d;++pcm_writes;};
    mapped.pcm_read=[&](uint8_t a){check(a==0x23,"PCM read mirror");++pcm_reads;return pcm;};
    mapped.ym_write=[&](uint8_t a,uint8_t d){check(a==ym_writes&&d==0x5a,"YM A0 decode");++ym_writes;};
    mapped.ym_read=[&](uint8_t a){check(a==ym_reads,"YM read A0 decode");++ym_reads;return uint8_t(a?0x80:0xff);};
    run(mapped,500);
    check(pcm_reads==1&&pcm_writes==1&&ym_reads==2&&ym_writes==2&&mapped.ram[2]==0x5a&&mapped.ram[3]==0xff&&mapped.ram[4]==0x80&&mapped.ram[5]==0xff,
          "mirrored endpoints repeated or produced wrong data");
    put(p,0,{0xdb,1});XBoardSoundCpu absent(p.data(),p.size());bool rejected=false;
    try{run(absent,30);}catch(const std::runtime_error& e){rejected=std::string(e.what())=="unbound sound YM read";}
    check(rejected,"unimplemented device read silently accepted");
    std::cout<<"Sound CPU: NMI/RETN, IRQ/RETI caller retention, reset hold/release, mirrored endpoints and missing-device rejection passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
