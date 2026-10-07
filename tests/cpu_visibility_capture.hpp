#pragma once
#include <array>
#include <fstream>
#include <vector>
#include <algorithm>
#include <iostream>
#include <cstring>
#include "hwmath/xboard_cpu_visibility.hpp"

// Captured physical levels are assertions only. Generated levels are the
// caller oracle's Poll inputs; bus/frontier gaps remain labelled boundaries.
struct CpuVisibilityCapture {
    struct Phase { uint64_t ns, previous_end; uint32_t pc,address; uint16_t data,mask; unsigned level; bool write; };
    std::vector<Phase> phases;
    const Phase* find(uint64_t ns) const {
        auto it=std::lower_bound(phases.begin(),phases.end(),ns,[](const Phase& p,uint64_t t){return p.ns<t;});
        return it!=phases.end()&&it->ns==ns?&*it:nullptr;
    }
    unsigned records=0,failed=0,delayed=0,adc_reads=0,frames=0,tails=0,levels[7]={};
    static uint64_t number(std::ifstream& f,unsigned n) {
        uint64_t v=0;while(n--){int c=f.get();if(c<0)throw std::runtime_error("truncated CPU visibility capture");v=v<<8|c;}return v;
    }
    explicit CpuVisibilityCapture(const char* path) {
        std::ifstream f(path,std::ios::binary);char magic[8];
        if(!f.read(magic,8)||std::memcmp(magic,"AB2VIS02",8))throw std::runtime_error("invalid CPU visibility capture");
        CompareTimer5250 timer;XBoardTimeline board(timer);XBoardCpuVisibility cpu(board);
        std::array<uint8_t,8> inputs{};bool initialized=false;uint8_t mux=0;
        board.adc.analog=[&]{return inputs[(mux>>2)&7];};
        try { while(f.peek()!=std::char_traits<char>::eof()) {
            auto t=number(f,8),end=number(f,8);auto pc=number(f,4),a=number(f,4);
            auto d=number(f,2),m=number(f,2);auto kind=number(f,1),observed=number(f,1),selected=number(f,1);
            std::array<uint8_t,8> current;for(auto& v:current)v=number(f,1);
            auto opcode=number(f,2),source=number(f,2),fetch=number(f,1);
            if(kind>2||observed>6||observed%2||fetch>1||(opcode&&!fetch))throw std::runtime_error("invalid visibility context");
            if(opcode&&m68000_completion_tail(opcode,source))++tails;
            if(!initialized){inputs=current;initialized=true;}
            bool ok=true;
            if(kind==2){board.advance_before(t);inputs=current;++frames;}
            else {
                cpu.resume(end,opcode,source);cpu.before_bus(t);
                // Read taps see the timer acknowledgement's line change.
                if(kind==0&&a>=0xe8000&&a<0xec000)timer.read(a>>1);
                if(kind==0&&a>=0x130000&&a<0x140000&&(m&255)){
                    ++adc_reads;ok &= (board.adc.read(board.now())&m)==(d&m&255);
                }
                if((pc>=0x1458a&&pc<=0x145b8)||(pc>=0x14856&&pc<=0x14878)||(pc>=0xb676&&pc<=0xb696)||pc==0x1466a||pc==0x14672)
                    phases.push_back({t,end,uint32_t(pc),uint32_t(a),uint16_t(d),uint16_t(m),board.main_irq_level(),kind==1});
                const auto line0=XBoardTimeline::RESET_LINE0_NS;
                if(t>=line0 && board.next_scanline()<=t)++delayed;
                ok &= inputs==current;
            }
            ok &= board.main_irq_level()==observed && mux==selected;
            if(!ok && ++failed<10)std::cerr<<"Visibility t="<<t<<" frontier="<<end<<" pc="<<std::hex<<pc<<" a="<<a<<std::dec<<" IPL="<<board.main_irq_level()<<'/'<<observed<<'\n';
            ++levels[board.main_irq_level()];++records;
            if(kind==1){
                if(a>=0xe8000&&a<0xec000)timer.write(a>>1,d,m);
                else if(a>=0x130000&&a<0x140000&&(m&255))board.adc.write(t);
                else if(a==0x140004&&(m&255))mux=d;
            }
        }} catch(...) { report();throw; }
    }
    void report() const {
        std::cout<<"CPU physical visibility: "<<records-failed<<" of "<<records<<" identical, "<<failed<<" different, 0 skipped; "<<delayed<<" delayed scanline observations, "<<adc_reads<<" ADC reads, "<<frames<<" input frames, "<<tails<<" completion tails; IPL2/4/6="<<levels[2]<<'/'<<levels[4]<<'/'<<levels[6]<<'\n';
    }
};
