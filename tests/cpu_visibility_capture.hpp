#pragma once
#include <array>
#include <fstream>
#include <vector>
#include <map>
#include <algorithm>
#include <iostream>
#include <cstring>
#include "engine/omainio.hpp"
#include "engine/omaininputschedule.hpp"
#include "engine/omainflightschedule.hpp"
#include "engine/omaincompanionschedule.hpp"

// Captured physical levels are assertions only. Generated levels are the
// caller oracle's Poll inputs; bus/frontier gaps remain labelled boundaries.
struct CpuVisibilityCapture {
    struct Phase { uint64_t ns, previous_end; uint32_t pc,address; uint16_t data,mask; unsigned level; bool write; uint8_t adc,buttons; std::array<uint8_t,6> sampled; unsigned sample_mask; };
    std::vector<Phase> phases;
    const Phase* find(uint64_t ns) const {
        auto it=std::lower_bound(phases.begin(),phases.end(),ns,[](const Phase& p,uint64_t t){return p.ns<t;});
        return it!=phases.end()&&it->ns==ns?&*it:nullptr;
    }
    static bool internal(uint32_t pc) {
        if(const auto* i=main_input_instruction(pc);i&&i->tail)return true;
        if(const auto* i=main_flight_instruction(pc);i&&i->tail)return true;
        if(const auto* i=main_companion_instruction(pc);i&&i->tail)return true;
        switch(pc) {
        case 0xab02:case 0xab42:case 0xab4a:case 0xab68:case 0xab9a:case 0xab9c:case 0xab9e:case 0xabc2:case 0xac02:case 0xac04:case 0xac06:case 0xac24:return true;
        case 0x88f0:case 0x88f2:case 0x88f4:case 0x88fa:case 0x8928:case 0x892a:case 0x892c:
        case 0xb636:case 0xb63e:case 0x8b06:case 0x8b24:case 0x8b2e:case 0x8b40:case 0x8b5e:
        case 0x8b76:case 0x8b78:case 0x8b86:case 0x8b88:case 0x8ba8:case 0x8bce:case 0x8bd0:case 0x8bde:case 0x8be0:return true;
        default:return false;
        }
    }
    std::map<uint64_t,unsigned> completion_levels;
    unsigned internal_poll(uint64_t prefetch_ns) const {
        auto it=completion_levels.find(prefetch_ns);
        if(it==completion_levels.end())throw std::runtime_error("missing internal completion poll");
        return it->second;
    }
    std::vector<std::pair<uint64_t,uint8_t>> button_inputs;
    uint8_t buttons_at(uint64_t ns) const {
        if(button_inputs.empty())throw std::runtime_error("missing physical button inputs");
        auto it=std::upper_bound(button_inputs.begin(),button_inputs.end(),ns,
            [](uint64_t t,const auto& p){return t<p.first;});
        return it==button_inputs.begin()?button_inputs.front().second:std::prev(it)->second;
    }
    bool physical_buttons=false;
    unsigned adc_stores=0,external_adc_stores=0;
    unsigned records=0,failed=0,delayed=0,adc_reads=0,frames=0,tails=0,levels[7]={};
    static uint64_t number(std::ifstream& f,unsigned n) {
        uint64_t v=0;while(n--){int c=f.get();if(c<0)throw std::runtime_error("truncated CPU visibility capture");v=v<<8|c;}return v;
    }
    explicit CpuVisibilityCapture(const char* path) {
        std::ifstream f(path,std::ios::binary);char magic[8];
        if(!f.read(magic,8)||(std::memcmp(magic,"AB2VIS02",8)&&std::memcmp(magic,"AB2VIS03",8)))throw std::runtime_error("invalid CPU visibility capture");
        physical_buttons=!std::memcmp(magic,"AB2VIS03",8);
        MainBus::Memory memory;MainBus bus(nullptr,0,nullptr,0,memory);
        OMainIO hardware(bus);auto& board=hardware.board;auto& timer=bus.compare_timer;
        std::array<uint8_t,6> sampled{};unsigned sample_mask=0;uint8_t adc_result=0;unsigned adc_lane=0;bool adc_result_known=false;
        std::array<uint8_t,8> inputs{};bool initialized=false;
        try { while(f.peek()!=std::char_traits<char>::eof()) {
            auto t=number(f,8),end=number(f,8);auto pc=number(f,4),a=number(f,4);
            auto d=number(f,2),m=number(f,2);auto kind=number(f,1),observed=number(f,1),selected=number(f,1);
            std::array<uint8_t,8> current;for(auto& v:current)v=number(f,1);
            const uint8_t buttons=physical_buttons?number(f,1):0xff;
            if(physical_buttons&&(button_inputs.empty()||button_inputs.back().second!=buttons))button_inputs.emplace_back(t,buttons);
            hardware.set_buttons(buttons);
            auto opcode=number(f,2),source=number(f,2),fetch=number(f,1);
            if(kind>2||observed>6||observed%2||fetch>1||(opcode&&!fetch))throw std::runtime_error("invalid visibility context");
            if(opcode&&m68000_completion_tail(opcode,source))++tails;
            if(!initialized){inputs=current;hardware.set_inputs(inputs);initialized=true;}
            bool ok=true;
            if(kind==2){board.advance_before(t);inputs=current;hardware.set_inputs(inputs);++frames;}
            else {
                hardware.before_bus(t,end,opcode,source);
                // Read taps see the timer acknowledgement's line change.
                if(kind==0&&a>=0xe8000&&a<0xec000)timer.read(a>>1);
                if(kind==0&&a>=0x130000&&a<0x140000&&(m&255)){
                    adc_result=bus.endpoints.adc_read();adc_lane=(hardware.selected_mux()>>2)&7;adc_result_known=true;
                    ++adc_reads;ok &= (bus.endpoints.adc_read()&m)==(d&m&255);
                }
                if((physical_buttons&&a==0x150000)||main_input_instruction(pc)||main_flight_instruction(pc)||main_companion_instruction(pc)||pc==0xd0da||(pc>=0xb84c&&pc<=0xb884)||(pc>=0x1458a&&pc<=0x145b8)||(pc>=0x14856&&pc<=0x14878)||(pc>=0xb676&&pc<=0xb7c0)||pc==0x1466a||pc==0x14672||pc==0x147c6||pc==0x147e6||(pc>=0x8b02&&pc<=0x8c1a)||(pc>=0x9556&&pc<=0x9572)||pc==0x8c1a||(pc>=0xb636&&pc<=0xb654)||pc==0x8afe||pc==0xb632||pc==0x8af8||pc==0xb62e||(pc>=0x88c2&&pc<=0x8944)||pc==0x8af2||(pc>=0x7bb6&&pc<=0x7c10)||(pc>=0xc082&&pc<=0xc098)||(pc>=0xaaee&&pc<=0xac4a)||(pc>=0xae32&&pc<=0xae4e)||pc==0xb66e||pc==0x14ee2||pc==0xb628||pc==0x15bfe||(kind==0&&a>=0x130000&&a<0x140000&&(m&255)))
                    phases.push_back({t,end,uint32_t(pc),uint32_t(a),uint16_t(d),uint16_t(m),board.main_irq_level(),kind==1,board.adc.value(),buttons,sampled,sample_mask});
                // Register shifts and ADDA select at nbcr3/roml3, after
                // their prefetch. MAME can deliver an edge at the completed
                // four-clock bus, but cannot yield inside the internal tail.
                if(internal(pc)) {
                    completion_levels.emplace(t,board.irq_at(std::max(board.now(),t+320)));
                }
                const auto line0=XBoardTimeline::RESET_LINE0_NS;
                if(t>=line0 && board.next_scanline()<=t)++delayed;
                ok &= inputs==current;
            }
            ok &= board.main_irq_level()==observed && hardware.selected_mux()==selected;
            if(kind==1){
                if(physical_buttons&&a>=0x3f8892&&a<=0x3f8896){
                    for(unsigned lane=0;lane<2;++lane)if(m&(lane?255:0xff00)){
                        const unsigned index=a+lane-0x3f8892;if(index>=6)continue;
                        if(pc==0x14632){
                            if(!adc_result_known||index!=adc_lane||uint8_t(d>>(lane?0:8))!=adc_result){ok=false;if(failed<10)std::cerr<<"ADC sampler store mismatch at "<<t<<'\n';}
                            if(adc_lane>=6)throw std::runtime_error("invalid ADC sampler mux lane");
                            sampled[adc_lane]=adc_result;sample_mask|=1u<<adc_lane;++adc_stores;
                        }else {sample_mask&=~(1u<<index);++external_adc_stores;}
                    }
                }
                if(a>=0xe8000&&a<0xec000)timer.write(a>>1,d,m);
                else if(a>=0x130000&&a<0x140000&&(m&255))bus.endpoints.adc_write(d);
                else if(a==0x140004&&(m&255))bus.io[0].output(2,d,0xff);
            }
            if(!ok && ++failed<10)std::cerr<<"Visibility t="<<t<<" frontier="<<end<<" pc="<<std::hex<<pc<<" a="<<a<<std::dec<<" IPL="<<board.main_irq_level()<<'/'<<observed<<'\n';
            ++levels[board.main_irq_level()];++records;
        }} catch(...) { report();throw; }
    }
    void report() const {
        std::cout<<"CPU physical visibility: "<<records-failed<<" of "<<records<<" identical, "<<failed<<" different, 0 skipped; "<<delayed<<" delayed scanline observations, "<<adc_reads<<" ADC reads, "<<frames<<" input frames, "<<tails<<" completion tails; IPL2/4/6="<<levels[2]<<'/'<<levels[4]<<'/'<<levels[6]<<"; computed ADC sample stores="<<adc_stores<<" external raw-input writes="<<external_adc_stores<<'\n';
    }
};
