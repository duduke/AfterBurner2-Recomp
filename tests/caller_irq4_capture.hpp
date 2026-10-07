#pragma once
#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>
#include <cstring>
#include "engine/omainirqservice.hpp"

// Companion IRQ4 observations from the same MAME run. No execution callbacks:
// registers, timestamps and main-owned writes are assertions. Sub lanes remain
// explicitly supplied at their captured operand frontiers.
struct CallerIrq4Capture {
    struct Event { uint64_t ns;uint32_t pc,a;uint16_t d,m;bool w; };
    struct Sub { unsigned frontier;Event event; };
    struct Hook { uint32_t entry;uint64_t start,end;OMainObjects::Registers pre,post; };
    struct Envelope {
        OMainIrqService::Context context{};
        std::array<uint8_t,MainBus::MEMORY_BYTES> memory;
        uint64_t end;uint32_t sp;uint16_t sr;
        OMainObjects::Registers post;
        std::vector<Event> events;
        std::vector<Sub> sub;
        std::vector<Hook> hooks;
    };
    std::map<std::pair<uint64_t,uint32_t>,Envelope> envelopes;
    static uint64_t number(std::ifstream& f,unsigned n) {
        uint64_t v=0;while(n--){int c=f.get();if(c<0)throw std::runtime_error("truncated companion IRQ4 capture");v=v<<8|c;}return v;
    }
    static OMainObjects::Registers registers(std::ifstream& f){
        OMainObjects::Registers r;for(auto& v:r.d)v=number(f,4);for(auto& v:r.a)v=number(f,4);return r;
    }
    static Event event(std::ifstream& f){
        return {number(f,8),uint32_t(number(f,4)),uint32_t(number(f,4)),uint16_t(number(f,2)),uint16_t(number(f,2)),number(f,1)!=0};
    }
    explicit CallerIrq4Capture(const std::string& path,MainBus& bus){
        std::ifstream f(path,std::ios::binary);char magic[8];
        if(!f.read(magic,8)||std::memcmp(magic,"AB2IRQ46",8))throw std::runtime_error("invalid companion IRQ4 capture");
        while(f.peek()!=std::char_traits<char>::eof()){
            Envelope e;auto& c=e.context;c.level=4;c.boundary_ns=number(f,8);c.interrupted_pc=number(f,4);
            c.return_pc=number(f,4);c.sr=number(f,2);c.supervisor_sp=number(f,4);const auto source=number(f,2);
            c.registers=registers(f);
            if(!f.read(reinterpret_cast<char*>(e.memory.data()),e.memory.size()))throw std::runtime_error("truncated companion IRQ4 RAM");
            e.end=number(f,8);const auto n=number(f,4),nh=number(f,4),nsub=number(f,4);
            if(n>20000||nh>30||nsub>200000)throw std::runtime_error("invalid companion IRQ4 counts");
            for(unsigned i=0;i<nsub;++i){auto frontier=number(f,4);e.sub.push_back({unsigned(frontier),event(f)});}
            for(unsigned i=0;i<n;++i)e.events.push_back(event(f));
            for(unsigned i=0;i<nh;++i){
                Hook h;h.entry=number(f,4);h.start=number(f,8);h.end=number(f,8);auto count=number(f,4);
                h.pre=registers(f);h.post=registers(f);if(count>200000)throw std::runtime_error("invalid companion IRQ4 callee count "+std::to_string(count)+" at "+std::to_string(f.tellg()));
                for(unsigned j=0;j<count;++j)e.events.push_back(event(f));e.hooks.push_back(h);
            }
            e.post=registers(f);e.sp=number(f,4);e.sr=number(f,2);
            std::sort(e.events.begin(),e.events.end(),[](const Event& a,const Event& b){return a.ns<b.ns;});
            const auto offset=MainBus::memory_offset(c.interrupted_pc);
            const auto opcode=offset==MainBus::UNMAPPED?bus.read16(c.interrupted_pc):
                uint16_t((e.memory[offset]<<8)|e.memory[offset+1]);
            c.boundary_ns+=80*OMainIrqService::completion_tail(opcode,source);
            const auto key=std::pair(c.boundary_ns,c.return_pc);
            if(!envelopes.emplace(key,std::move(e)).second)throw std::runtime_error("duplicate companion IRQ4 context");
        }
    }
    const Envelope& find(uint64_t ns,uint32_t return_pc) const {
        auto i=envelopes.find({ns,return_pc});if(i==envelopes.end())throw std::runtime_error("missing companion IRQ4 entry at "+std::to_string(ns));return i->second;
    }
};
