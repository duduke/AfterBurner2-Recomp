#include "hwaudio/xboard_sound_cpu.hpp"
#include <stdexcept>
#define CHIPS_IMPL
#include "../../third_party/z80.h"

// Both runners use the existing third-party decoder; this runner does not
// queue sound commands.
struct XBoardSoundCpu::State {
    z80_t cpu{};
    uint64_t pins{}, clock{}, request{};
    uint64_t pending_request{}, pending_clock{}; uint8_t pending_data{};
    uint64_t machine_end{};bool aborted{};
    Coverage coverage{};uint8_t first_opcode{};
    const uint8_t* program;
    size_t length;
    uint16_t pc{};
    bool reset{}, nmi{}, irq{};
    State(const uint8_t* p, size_t n) : program(p), length(n) {
        pins=z80_init(&cpu);
        // MAME z80_device::device_start: zero general/alternate registers,
        // Z set, IX/IY FFFF. Do not substitute the pin core's FFFF defaults.
        cpu.af=0x0040;cpu.bc=cpu.de=cpu.hl=cpu.sp=cpu.wz=0;
        cpu.af2=cpu.bc2=cpu.de2=cpu.hl2=0;
    }
};
XBoardSoundCpu::XBoardSoundCpu(const uint8_t* p, size_t n) : state(std::make_unique<State>(p,n)) {
    if(!p || n<0xf000) throw std::invalid_argument("sound program must cover 0000-EFFF");
}
XBoardSoundCpu::~XBoardSoundCpu() = default;
void XBoardSoundCpu::reset_line(bool asserted) {
    auto& s=*state;
    if(asserted && !s.reset) {
        // MAME device_reset resets PC/WZ/I/R/IFF and pending NMI, retaining
        // the register banks, stack pointer, IX/IY and interrupt mode.
        const auto saved=registers();s.pins=z80_reset(&s.cpu);
        s.cpu.af=saved.af;s.cpu.bc=saved.bc;s.cpu.de=saved.de;s.cpu.hl=saved.hl;
        s.cpu.sp=saved.sp;s.cpu.ix=saved.ix;s.cpu.iy=saved.iy;
        s.cpu.af2=saved.af2;s.cpu.bc2=saved.bc2;s.cpu.de2=saved.de2;s.cpu.hl2=saved.hl2;s.cpu.im=saved.im;
        s.cpu.wz=0;s.cpu.pins=(s.cpu.pins&~Z80_NMI)|(s.nmi?Z80_NMI:0);
        s.cpu.int_bits=s.irq?Z80_INT:0;
        s.request=0;s.pending_request=0;s.machine_end=s.clock;s.pc=0;
    }
    s.reset=asserted; // CPU reset does not erase board RAM or reset YM/PCM
}
void XBoardSoundCpu::nmi_line(bool asserted) {
    auto& s=*state;
    // MAME execute_set_input latches the edge when the synchronized input
    // is delivered, including immediately before instruction selection.
    if(asserted&&!s.nmi&&s.clock&&!s.reset){s.cpu.int_bits|=Z80_NMI;s.cpu.pins|=Z80_NMI;}
    s.nmi=asserted;
}
void XBoardSoundCpu::irq_line(bool asserted) {
    state->irq=asserted;
    state->cpu.int_bits=(state->cpu.int_bits&~Z80_INT)|(asserted?Z80_INT:0);
}
uint64_t XBoardSoundCpu::clocks() const { return state->clock; }
XBoardSoundCpu::Coverage XBoardSoundCpu::coverage() const{return state->coverage;}
void XBoardSoundCpu::abort_grant(){state->aborted=true;}
unsigned XBoardSoundCpu::run(unsigned budget){
    auto& s=*state;const auto start=s.clock,target=start+budget;s.aborted=false;
    while((s.clock<target&&!s.aborted)||s.clock<s.machine_end)tick();
    return unsigned(s.clock-start);
}
XBoardSoundCpu::Registers XBoardSoundCpu::registers() const {
    const auto& c=state->cpu;
    return {c.pc,c.sp,c.af,c.bc,c.de,c.hl,c.ix,c.iy,c.af2,c.bc2,c.de2,c.hl2,c.i,c.r,c.im,c.iff1,c.iff2};
}
void XBoardSoundCpu::tick() {
    auto& s=*state;++s.clock;if(s.reset)return;
    s.pins=(s.pins&~(Z80_INT|Z80_NMI))|(s.irq?Z80_INT:0)|(s.nmi?Z80_NMI:0);
    s.pins=z80_tick(&s.cpu,s.pins);
    const bool irq_entry=s.cpu.step==Z80_INT_IM0_STEP||s.cpu.step==Z80_INT_IM1_STEP||s.cpu.step==Z80_INT_IM2_STEP;
    if(irq_entry){
        // MAME irqfetch precedes its 2-clock and 5-clock latency groups.
        // The pin decoder asserts IORQ two ticks later; deliver the vector
        // there, but observe the acknowledge at the accepted entry edge.
        ++s.coverage.irq_entries;s.machine_end=s.clock+1;
        if(trace)trace({Kind::InterruptAck,s.clock,s.pc,0,0xff});
    }
    auto key=[](uint64_t pins) {
        const bool request=(pins&Z80_MREQ)&&(pins&(Z80_RD|Z80_WR));
        const bool io=pins&Z80_IORQ;
        return request||io?(pins&(Z80_MREQ|Z80_IORQ|Z80_RD|Z80_WR|Z80_M1))|Z80_GET_ADDR(pins):uint64_t(0);
    };
    // MAME z80.lst issues memory/I/O accesses at the start of the 3/4-T
    // machine cycle. The pin decoder asserts them at T2/T3 instead. Preview
    // only the next two private decoder ticks, stopping at the first request:
    // issue its side effect now and retain its read result for the real pins.
    // Never run the preview through a read with unknown data.
    auto access=[&](uint64_t p) -> uint8_t {
    const uint16_t a=Z80_GET_ADDR(p);
    uint8_t d=Z80_GET_DATA(p);
    const bool ack=(p&(Z80_IORQ|Z80_M1))==(Z80_IORQ|Z80_M1);
    const bool mem=(p&Z80_MREQ) && (p&(Z80_RD|Z80_WR));
    const bool io=(p&Z80_IORQ) && (p&(Z80_RD|Z80_WR)) && !ack;
    s.machine_end=s.clock-1+(io?4:(p&Z80_M1)?2:3);
    if(mem && (p&Z80_M1) && !s.cpu.prefix_active){s.pc=a;++s.coverage.instructions;}
    Kind kind;
    if(ack) {kind=Kind::InterruptAck;d=0xff;++s.coverage.irq_entries;}
    else if(mem) {
        if(p&Z80_RD) {
            kind=Kind::MemoryRead;
            if(a<0xf000)d=s.program[a];
            else if(a>=0xf800)d=ram[a&0x7ff];
            else {if(!pcm_read)throw std::runtime_error("unbound sound PCM read");d=pcm_read(a&255);}
            /* result is delivered to real decoder pins below */
        } else {
            kind=Kind::MemoryWrite;
            if(a>=0xf800)ram[a&0x7ff]=d;
            else if(a>=0xf000) {if(!pcm_write)throw std::runtime_error("unbound sound PCM write");pcm_write(a&255,d);}
        }
    } else {
        const uint8_t port=a&255;
        if(p&Z80_RD) {
            kind=Kind::IoRead;d=0xff;
            if(port<0x40) {if(!ym_read)throw std::runtime_error("unbound sound YM read");d=ym_read(port&1);}
            else if(port<0x80) {if(!latch_read)throw std::runtime_error("unbound sound latch read");d=latch_read();}
            /* result is delivered to real decoder pins below */
        } else {
            kind=Kind::IoWrite;
            if(port<0x40) {if(!ym_write)throw std::runtime_error("unbound sound YM write");ym_write(port&1,d);}
        }
    }
    if(mem&&(p&Z80_M1)){
        if(!s.cpu.prefix_active)s.first_opcode=d;
        else if(s.first_opcode==0xed){if(d==0x4d)++s.coverage.reti;else if((d&0xc7)==0x45)++s.coverage.retn;}
    }
    // MAME's sound I/O space globally masks A15..A8, including tracing.
    if(trace)trace({kind,s.clock,s.pc,static_cast<uint16_t>(io?(a&255):a),d});
    return d;
    };
    const auto actual=key(s.pins);
    if(actual && actual!=s.request) {
        uint8_t data;
        if((s.pins&(Z80_IORQ|Z80_M1))==(Z80_IORQ|Z80_M1)) {
            data=0xff;s.machine_end=s.clock-1+5;
        }else if((s.pins&(Z80_MREQ|Z80_M1))==(Z80_MREQ|Z80_M1)&&s.cpu.step==Z80_NMI_STEP) {
            // MAME take_nmi: five private clocks, then two stack writes.
            // The pin core's discarded M1 read is not a MAME bus access;
            // retain the interrupted instruction PC for both stack phases.
            data=0;s.machine_end=s.clock-1+5;++s.coverage.nmi_entries;
        }else if(s.pending_request) {
            if(actual!=s.pending_request || s.clock!=s.pending_clock)
                throw std::runtime_error("sound machine-cycle preview diverged");
            data=s.pending_data;s.pending_request=0;
        } else data=access(s.pins);
        Z80_SET_DATA(s.pins,data);
    }
    s.request=actual;
    // MAME's opcode-read macro groups its two refresh/internal clocks.
    if(s.pins&Z80_RFSH)s.machine_end=std::max(s.machine_end,s.clock+1);
    if(s.pending_request)return;
    auto preview=s.cpu;auto pins=s.pins;auto prior=actual;
    for(unsigned ahead=1;ahead<=2;++ahead) {
        pins=(pins&~(Z80_INT|Z80_NMI))|(s.irq?Z80_INT:0)|(s.nmi?Z80_NMI:0);
        pins=z80_tick(&preview,pins);
        const auto next=key(pins);
        if(next && next!=prior) {
            const bool m1=pins&Z80_M1;
            // Pin writes assert IORQ one tick before pin reads (OUT n/C
            // versus IN n/C). MAME starts both at the machine-cycle edge.
            const unsigned lead=m1?0:((pins&Z80_IORQ)&&(pins&Z80_RD))?2:1;
            if(ahead==lead) {
                s.pending_request=next;s.pending_clock=s.clock+ahead;
                s.pending_data=access(pins);
            }
            break;
        }
        prior=next;
    }
}
