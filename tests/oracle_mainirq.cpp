// Execute the complete comparator IRQ against a continuous MAME hardware
// timeline. Only external inputs and bus timestamps are replayed; ADC results,
// timer state, sample writes and sound commands are computed by the port.
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>
#include "engine/omainadc.hpp"
#include "hwmath/xboard_timeline.hpp"

namespace {
uint64_t number(std::ifstream& f,unsigned n) {
    uint64_t v=0;
    while(n--) { int c=f.get(); if(c<0) throw std::runtime_error("truncated IRQ timeline"); v=(v<<8)|c; }
    return v;
}
void bytes(std::ifstream& f,uint8_t* p,size_t n) {
    if(!f.read(reinterpret_cast<char*>(p),n)) throw std::runtime_error("truncated IRQ RAM");
}
OMainObjects::Registers registers(std::ifstream& f) {
    OMainObjects::Registers r;
    for(auto& v:r.d) v=number(f,4);
    for(auto& v:r.a) v=number(f,4);
    return r;
}
struct Event {
    uint64_t ns;
    uint32_t pc,address;
    uint16_t data,mask;
    unsigned kind,result,irq,mux;
    std::array<uint8_t,8> inputs;
};
Event event(std::ifstream& f) {
    Event e{};
    e.ns=number(f,8);e.pc=number(f,4);e.address=number(f,4);
    e.data=number(f,2);e.mask=number(f,2);e.kind=number(f,1);e.result=number(f,1);
    number(f,1);e.irq=number(f,1);e.mux=number(f,1);
    for(auto& v:e.inputs) v=number(f,1);
    return e;
}
using Write=std::tuple<uint32_t,uint16_t,uint16_t>;
}
int main(int argc,char** argv) try {
    std::ifstream f(argc>1 ? argv[1] : "build/oracle/mainirq.bin",std::ios::binary);
    char magic[8];if(!f.read(magic,8) || std::memcmp(magic,"AB2IRQ01",8)) throw std::runtime_error("invalid IRQ timeline");
    const uint64_t captured_rise=number(f,8);
    if(captured_rise!=XBoardTimeline::RESET_LINE0_NS+XBoardTimeline::LINE_NS)
        throw std::runtime_error("unexpected MAME reset phase");
    uint64_t now=0;
    MainBus::Memory memory;
    MainBus bus(nullptr,0,nullptr,0,memory);
    XBoardTimeline board(bus.compare_timer);
    auto& adc=board.adc;
    uint8_t mux=0;std::array<uint8_t,8> inputs{};inputs.fill(0x10);
    adc.analog=[&] { return inputs[(mux>>2)&7]; };
    bus.endpoints.adc_read=[&] { return adc.read(now); };
    bus.endpoints.adc_write=[&](uint8_t) { adc.write(now); };
    bus.io[0].output=[&](unsigned p,uint8_t d,uint8_t) { if(p==2) mux=d; };
    unsigned events=0,event_identical=0,event_failed=0,calls=0,identical=0,failed=0,skipped=0;
    unsigned sound=0,conversions=0,slots[6]={};
    unsigned raw[3][256]={};
    bool seen_frame=false,active=false;
    OMainObjects::Registers pre;
    std::array<uint8_t,0x8000> before{},after{};
    std::vector<Event> pending;
    // Advance physical devices before a transaction. Frame records supply
    // external input changes, sampled by ADC at completion rather than start.
    auto prepare=[&](const Event& e) {
        if(e.ns<now) throw std::runtime_error("nonmonotonic hardware timeline");
        now=e.ns;
        if(adc.busy() && adc.deadline()<=now) ++conversions;
        if(e.kind==5) board.advance_before(now); else board.advance(now);
        bool equal=bus.compare_timer.state().irq==bool(e.irq) && mux==e.mux;
        if(e.kind==1) equal &= adc.value()==e.result && (adc.value()&e.mask)==(e.data&e.mask);
        if(e.kind==5) { inputs=e.inputs;seen_frame=true; }
        else if(inputs!=e.inputs) { if(seen_frame) equal=false; inputs=e.inputs; }
        ++events;
        if(equal) ++event_identical;
        else {
            ++event_failed;
            if(event_failed<9) std::cerr<<"hardware event "<<events<<" PC="<<std::hex<<e.pc<<std::dec<<" kind="<<e.kind<<" t="<<now<<'\n';
        }
    };
    auto apply=[&](const Event& e) {
        prepare(e);
        switch(e.kind) {
        case 1: adc.read(now);break;
        case 2: if(e.mask&255) adc.write(now);break;
        case 3: if(e.mask&255) mux=e.data;break;
        case 4: bus.compare_timer.write(e.address>>1,e.data,e.mask);break;
        default: break;
        }
    };
    while(f.peek()!=std::char_traits<char>::eof()) {
        Event e=event(f);
        if(e.kind==6) {
            if(active) throw std::runtime_error("nested IRQ capture");
            prepare(e);pre=registers(f);bytes(f,before.data(),before.size());
            bus.io[0].write(6,number(f,1));bus.io[0].write(7,number(f,1));
            pending.clear();active=true;
        } else if(e.kind==7) {
            if(!active || pending.empty()) throw std::runtime_error("missing IRQ entry");
            const auto post=registers(f);bytes(f,after.data(),after.size());
            std::vector<Write> expected,writes;
            const unsigned count=number(f,4);
            if(count>100) throw std::runtime_error("invalid IRQ write count");
            for(unsigned i=0;i<count;++i) {
                const uint32_t a=number(f,4);const uint16_t d=number(f,2),m=number(f,2);
                expected.emplace_back(a,d&m,m);
            }
            std::copy(before.begin(),before.end(),memory.bytes.begin());
            const unsigned slot=(before[0x890]<<8)|before[0x891];
            if(slot<6) ++slots[slot];
            size_t cursor=0;bool io_equal=true;
            auto next=[&] {
                // A notifier during the IRQ is an external input boundary.
                while(cursor<pending.size() && pending[cursor].kind==5) apply(pending[cursor++]);
                if(cursor<pending.size()) prepare(pending[cursor]);
            };
            MainBus::Trace trace{nullptr,nullptr,[&](bool w,uint32_t a,uint16_t d,uint16_t m) {
                const uint32_t offset=MainBus::memory_offset(a);
                if(offset!=MainBus::UNMAPPED) {
                    if(w) writes.emplace_back(offset,d&m,m);
                    return;
                }
                if(cursor>=pending.size()) { io_equal=false;return; }
                const auto& actual=pending[cursor];
                io_equal &= actual.address==a && actual.mask==m && (actual.data&m)==(d&m)
                    && w==(actual.kind!=1);
                if(w && a==0xe8016) ++sound;
                ++cursor;next();
            }};
            next();bus.trace=&trace;
            main_compare_interrupt(bus,pre);
            bus.trace=nullptr;
            // A missing transaction is a failed contract, not a replay input.
            if(cursor!=pending.size()) {
                throw std::runtime_error("IRQ omitted a captured I/O transaction");
            }
            prepare(e);
            bool equal=io_equal && writes==expected && pre.d==post.d && pre.a==post.a;
            std::array<uint8_t,0x8000> marked{};
            for(auto [a,d,m]:expected) {
                if(a+1>=marked.size()) throw std::runtime_error("IRQ wrote outside work RAM");
                if(m&0xff00) marked[a]=1;
                if(m&255) marked[a+1]=1;
            }
            for(size_t i=0;i<marked.size();++i) if(marked[i]) equal &= memory.bytes[i]==after[i];
            for(unsigned lane=0;lane<3;++lane) ++raw[lane][memory.bytes[0x892+lane]];
            ++calls;
            if(equal) ++identical;
            else {
                ++failed;
                if(failed<9) std::cerr<<"IRQ "<<calls<<" slot="<<slot<<" I/O="<<io_equal
                    <<" writes="<<(writes==expected)<<" registers="<<(pre.d==post.d && pre.a==post.a)<<'\n';
            }
            active=false;
        } else if(e.kind==8) {
            if(!active) throw std::runtime_error("IRQ discard without entry");
            for(const auto& p:pending) apply(p);
            prepare(e);active=false;++skipped;
        } else if(active) pending.push_back(e);
        else apply(e);
    }
    if(active) throw std::runtime_error("unfinished IRQ capture");
    std::cout<<"Comparator IRQ vs arcade: "<<calls+skipped<<" records, "<<identical<<" identical, "<<failed
        <<" different, "<<skipped<<" skipped (interrupted IRQ window)\n";
    std::cout<<"IRQ hardware timeline: "<<events<<" events, "<<event_identical<<" identical, "<<event_failed
        <<" different; "<<conversions<<" conversions, "<<sound<<" ported sound-latch writes\nCoverage: slots";
    for(unsigned i=0;i<6;++i) std::cout<<' '<<i<<'='<<slots[i];
    for(unsigned lane=0;lane<3;++lane) {
        const unsigned lo[]={0x20,0x40,0},hi[]={0xe0,0xc0,255};
        std::cout<<"; lane "<<lane<<" neutral/min/max="<<raw[lane][128]<<'/'<<raw[lane][lo[lane]]<<'/'<<raw[lane][hi[lane]];
    }
    std::cout<<'\n';
    const bool all_slots=std::all_of(std::begin(slots),std::end(slots),[](unsigned n) { return n!=0; });
    return calls && conversions && all_slots && !failed && !event_failed ? 0 : 1;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
