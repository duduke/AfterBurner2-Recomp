#include "hwaudio/xboard_sound_cpu.hpp"
#include <stdexcept>
#define CHIPS_IMPL
#include "../../third_party/z80.h"

// Both runners use the existing third-party decoder; this runner does not
// queue sound commands.
struct XBoardSoundCpu::State {
    z80_t cpu{};
    uint64_t pins{}, clock{}, request{};
    const uint8_t* program;
    size_t length;
    uint16_t pc{};
    bool reset{}, nmi{}, irq{};
    State(const uint8_t* p, size_t n) : program(p), length(n) { pins=z80_init(&cpu); }
};
XBoardSoundCpu::XBoardSoundCpu(const uint8_t* p, size_t n) : state(std::make_unique<State>(p,n)) {
    if(!p || n<0xf000) throw std::invalid_argument("sound program must cover 0000-EFFF");
}
XBoardSoundCpu::~XBoardSoundCpu() = default;
void XBoardSoundCpu::reset_line(bool asserted) {
    auto& s=*state;
    if(asserted && !s.reset) { s.pins=z80_reset(&s.cpu);s.request=0;s.pc=0; }
    s.reset=asserted; // CPU reset does not erase board RAM or reset YM/PCM
}
void XBoardSoundCpu::nmi_line(bool asserted) { state->nmi=asserted; }
void XBoardSoundCpu::irq_line(bool asserted) { state->irq=asserted; }
uint64_t XBoardSoundCpu::clocks() const { return state->clock; }
XBoardSoundCpu::Registers XBoardSoundCpu::registers() const {
    const auto& c=state->cpu;
    return {c.pc,c.sp,c.af,c.bc,c.de,c.hl,c.ix,c.iy,c.af2,c.bc2,c.de2,c.hl2,c.i,c.r,c.im,c.iff1,c.iff2};
}
void XBoardSoundCpu::tick() {
    auto& s=*state;++s.clock;if(s.reset)return;
    s.pins=(s.pins&~(Z80_INT|Z80_NMI))|(s.irq?Z80_INT:0)|(s.nmi?Z80_NMI:0);
    s.pins=z80_tick(&s.cpu,s.pins);
    const auto p=s.pins;
    const uint16_t a=Z80_GET_ADDR(p);
    uint8_t d=Z80_GET_DATA(p);
    const bool ack=(p&(Z80_IORQ|Z80_M1))==(Z80_IORQ|Z80_M1);
    const bool mem=(p&Z80_MREQ) && (p&(Z80_RD|Z80_WR));
    const bool io=(p&Z80_IORQ) && (p&(Z80_RD|Z80_WR)) && !ack;
    // A physical bus request can remain asserted across multiple ticks.
    // Device reads/writes and acknowledgements occur once per request.
    const uint64_t request=(mem||io||ack)?(p&(Z80_MREQ|Z80_IORQ|Z80_RD|Z80_WR|Z80_M1))|a:0;
    if(!request || request==s.request) {s.request=request;return;}
    s.request=request;
    if(mem && (p&Z80_M1) && !s.cpu.prefix_active)s.pc=a;
    Kind kind;
    if(ack) {kind=Kind::InterruptAck;d=0xff;Z80_SET_DATA(s.pins,d);}
    else if(mem) {
        if(p&Z80_RD) {
            kind=Kind::MemoryRead;
            if(a<0xf000)d=s.program[a];
            else if(a>=0xf800)d=ram[a&0x7ff];
            else {if(!pcm_read)throw std::runtime_error("unbound sound PCM read");d=pcm_read(a&255);}
            Z80_SET_DATA(s.pins,d);
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
            Z80_SET_DATA(s.pins,d);
        } else {
            kind=Kind::IoWrite;
            if(port<0x40) {if(!ym_write)throw std::runtime_error("unbound sound YM write");ym_write(port&1,d);}
        }
    }
    // MAME's sound I/O space globally masks A15..A8, including tracing.
    if(trace)trace({kind,s.clock,s.pc,static_cast<uint16_t>(io?(a&255):a),d});
}
