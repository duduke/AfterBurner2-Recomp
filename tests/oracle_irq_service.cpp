#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include "engine/omainirqservice.hpp"
#include "hwmath/xboard_timeline.hpp"
#include "roms.hpp"

namespace {
uint64_t number(std::ifstream& f,unsigned n) {
    uint64_t v=0;while(n--) { int c=f.get();if(c<0)throw std::runtime_error("truncated service capture");v=(v<<8)|c; }return v;
}
void bytes(std::ifstream& f,uint8_t* p,size_t n) {
    if(!f.read(reinterpret_cast<char*>(p),n))throw std::runtime_error("truncated service RAM");
}
OMainObjects::Registers regs(std::ifstream& f) {
    OMainObjects::Registers r;for(auto& v:r.d)v=number(f,4);for(auto& v:r.a)v=number(f,4);return r;
}
struct Event {
    uint64_t ns;uint32_t pc,address;uint16_t data,mask;unsigned kind;
    std::array<uint8_t,8> inputs{};
};
Event event(std::ifstream& f) {
    Event e{};e.ns=number(f,8);e.pc=number(f,4);e.address=number(f,4);e.data=number(f,2);e.mask=number(f,2);e.kind=number(f,1);
    if(e.kind==2)bytes(f,e.inputs.data(),8);return e;
}
}
int main(int argc,char** argv) try {
    std::ifstream f(argc>1?argv[1]:"build/oracle/service.bin",std::ios::binary);
    char magic[8];if(!f.read(magic,8)||std::memcmp(magic,"AB2SVC02",8))throw std::runtime_error("invalid CPU service capture");
    if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
    MainBus::Memory memory;MainBus bus(roms.maincpu.rom,roms.maincpu.length,nullptr,0,memory);
    XBoardTimeline board(bus.compare_timer);
    uint8_t mux=0;std::array<uint8_t,8> inputs{};inputs.fill(0x10);bool seen_frame=false;
    board.adc.analog=[&] { return inputs[(mux>>2)&7]; };
    bus.endpoints.adc_read=[&] { return board.adc.read(board.now()); };
    bus.endpoints.adc_write=[&](uint8_t) { board.adc.write(board.now()); };
    bus.io[0].output=[&](unsigned p,uint8_t d,uint8_t) { if(p==2)mux=d; };
    unsigned calls=0,identical=0,failed=0,events=0,event_failed=0,hardware=0,hardware_failed=0,level2=0,level6=0;
    unsigned mask_baseline=0,mask_probes=0,pending_masked=0,unmasked=0;
    unsigned tails=0,reg_shifts=0,frames_seen=0;
    unsigned phase[10]={},slots[6]={},sound=0,disabled=0,even=0,empty=0;
    uint64_t min_cycles=~uint64_t(0),max_cycles=0;
    while(f.peek()!=std::char_traits<char>::eof()) {
        unsigned kind=number(f,1);
        if(kind>=7 && kind<=9) {
            const unsigned stage=number(f,1);const uint64_t ns=number(f,8);
            const uint16_t sr=number(f,2);const unsigned level=number(f,1),delta=number(f,4);
            board.advance(ns);bool equal=board.main_irq_level()==level;
            if(kind==7) {
                if(!stage)mask_baseline=calls;
                equal &= !OMainIrqService::eligible(level,sr)&&delta==0&&calls==mask_baseline;
                ++mask_probes;if(level)++pending_masked;
            } else if(kind==8) { equal &= OMainIrqService::eligible(level,sr); }
            else { equal &= calls-mask_baseline==delta&&delta>0;++unmasked; }
            ++hardware;if(!equal)++hardware_failed;
            continue;
        }
        if(kind!=6) {
            const uint64_t ns=number(f,8);const uint32_t a=number(f,4);
            const uint16_t d=number(f,2),mask=number(f,2);
            const unsigned irq=number(f,1),blank=number(f,1);
            std::array<uint8_t,8> values;bytes(f,values.data(),8);
            if(kind==5)board.advance_before(ns);else board.advance(ns);
            bool ok=bus.compare_timer.state().irq==bool(irq)&&board.vblank()==bool(blank);
            if(kind==1)ok &= (board.adc.read(ns)&mask)==(d&mask);
            else if(kind==2 && (mask&255))board.adc.write(ns);
            else if(kind==3 && (mask&255))mux=d;
            else if(kind==4)bus.compare_timer.write(a>>1,d,mask);
            if(kind==5){inputs=values;seen_frame=true;}
            else if(inputs!=values) { if(seen_frame)ok=false;inputs=values; }
            ++hardware;if(!ok)++hardware_failed;
            continue;
        }
        OMainIrqService::Context c{};c.boundary_ns=number(f,8);c.interrupted_pc=number(f,4);c.return_pc=number(f,4);
        c.sr=number(f,2);const unsigned captured_level=number(f,1);const unsigned irq=number(f,1),blank=number(f,1),selected=number(f,1);
        c.supervisor_sp=number(f,4);c.registers=regs(f);
        std::array<uint8_t,0x8000> before,after;bytes(f,before.data(),before.size());
        std::array<uint8_t,8> values;bytes(f,values.data(),8);
        const unsigned control0=number(f,1),control1=number(f,1);
        const unsigned shift_source=number(f,2);
        const uint64_t end=number(f,8);const unsigned count=number(f,4);
        if(count>200)throw std::runtime_error("invalid service event count");
        std::vector<Event> expected,frames;
        for(unsigned i=0;i<count;++i) {auto e=event(f);if(e.kind==2)frames.push_back(e);else expected.push_back(e);}
        const auto post=regs(f);bytes(f,after.data(),after.size());
        const uint32_t post_sp=number(f,4);const uint16_t post_sr=number(f,2);
        std::copy(before.begin(),before.end(),memory.bytes.begin());
        const uint16_t opcode=bus.read16(c.interrupted_pc);
        const unsigned tail=OMainIrqService::completion_tail(opcode,shift_source);
        if(tail)++tails;
        if((opcode&0xf020)==0xe020 && (opcode&0xc0)!=0xc0)++reg_shifts;
        c.boundary_ns+=80*tail;
        frames_seen+=frames.size();
        bus.io[0].write(6,control0);bus.io[0].write(7,control1);
        bool ok=inputs==values && mux==selected;
        // The boundary CPU context is supplied; subsequent service access
        // and return timestamps are outputs of 80 ns bus/internal cycles.
        board.advance(c.boundary_ns);
        ok &= bus.compare_timer.state().irq==bool(irq)&&board.vblank()==bool(blank);
        c.level=board.main_irq_level();
        ok &= c.level==captured_level&&OMainIrqService::eligible(c.level,c.sr);
        const unsigned slot=(before[0x890]<<8)|before[0x891];if(slot<6)++slots[slot];
        if(!before[0x902])++disabled;else if(!(((before[0x900]<<8)|before[0x901])+1&1))++even;else if(!before[0x906])++empty;
        if(c.level==2)++level2;else ++level6;
        ++phase[(c.boundary_ns/80+10)%10];
        size_t cursor=0,frame_cursor=0;unsigned mismatches=0;
        OMainIrqService service;
        MainBus::Trace trace{nullptr,nullptr,[&](bool w,uint32_t a,uint16_t d,uint16_t m) {
            ++events;bool equal=cursor<expected.size();
            if(equal) { const auto& e=expected[cursor++];equal=e.ns==board.now()&&e.address==a&&e.mask==m&&(e.data&m)==(d&m)
                && e.kind==unsigned(w)&&e.pc==service.current_pc(); }
            if(w&&a==0xe8016)++sound;
            if(!equal) {
                if(event_failed<8 && cursor && cursor<=expected.size()) {
                    const auto& e=expected[cursor-1];
                    std::cerr<<"Service bus mismatch pc="<<std::hex<<service.current_pc()<<" a="<<a<<" expected="<<e.address
                        <<" data="<<(d&m)<<'/'<<(e.data&e.mask)<<std::dec<<" ns="<<board.now()<<'/'<<e.ns<<'\n';
                }
                ++event_failed;++mismatches;
            }
        }};
        auto advance=[&](uint64_t ns) {
            while(frame_cursor<frames.size()&&frames[frame_cursor].ns<=ns) {
                const auto& e=frames[frame_cursor++];board.advance_before(e.ns);inputs=e.inputs;
            }
            board.advance(ns);
        };
        bus.trace=&trace;
        const auto result=service.run(bus,c,advance);
        bus.trace=nullptr;
        ok &= !mismatches&&cursor==expected.size()&&frame_cursor==frames.size()&&result.ns==end
            && result.registers.d==post.d&&result.registers.a==post.a&&result.pc==c.return_pc&&result.sp==post_sp&&result.sr==post_sr&&OMainIrqService::eligible(c.level,c.sr);
        std::array<uint8_t,0x8000> marked{};
        for(const auto& e:expected)if(e.kind==1) {
            auto o=MainBus::memory_offset(e.address);if(o<marked.size()-1){if(e.mask&0xff00)marked[o]=1;if(e.mask&255)marked[o+1]=1;}
        }
        for(size_t i=0;i<marked.size();++i)if(marked[i])ok &= memory.bytes[i]==after[i];
        const uint64_t elapsed=(result.ns-c.boundary_ns)/80;min_cycles=std::min(min_cycles,elapsed);max_cycles=std::max(max_cycles,elapsed);
        ++calls;if(ok)++identical;else {++failed;if(failed<9)std::cerr<<"Service "<<calls<<" pc="<<std::hex<<c.interrupted_pc<<std::dec
            <<" events="<<mismatches<<" end="<<result.ns<<'/'<<end<<'\n';}
    }
    std::cout<<"CPU service vs arcade: "<<calls<<" calls, "<<identical<<" identical, "<<failed<<" different, 0 skipped\n";
    std::cout<<"Service bus timing: "<<events<<" events, "<<events-event_failed<<" identical, "<<event_failed<<" different, 0 skipped"
        <<"\nService external hardware: "<<hardware<<" events, "<<hardware-hardware_failed<<" identical, "<<hardware_failed<<" different, 0 skipped\nMask coverage: "<<mask_probes<<" probes, "<<pending_masked<<" pending while masked, "<<unmasked<<" serviced after unmask\nCoverage: IRQ2="<<level2<<" IRQ6="<<level6
        <<" cycles="<<min_cycles<<".."<<max_cycles<<" completion tails="<<tails<<" register shifts="<<reg_shifts<<" input frames during service="<<frames_seen<<" latch="<<sound<<" disabled/even/empty="<<disabled<<'/'<<even<<'/'<<empty<<"; E phases";
    for(unsigned i=0;i<10;++i)std::cout<<' '<<i<<'='<<phase[i];std::cout<<"; slots";for(unsigned i=0;i<6;++i)std::cout<<' '<<i<<'='<<slots[i];std::cout<<'\n';
    return calls&&!failed&&!event_failed&&!hardware_failed?0:1;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
