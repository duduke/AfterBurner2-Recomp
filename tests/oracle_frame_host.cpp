// Composed VBLANK write-set differential test, not a replay of captured writes.
// Executes verified VBLANK callees; only named 3e caller outputs are supplied.
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>
#include "roms.hpp"
#include "engine/omainframe.hpp"
#include "engine/omainhost.hpp"
#include "engine/omainpalette.hpp"
#include "engine/omainsprites.hpp"
#include "engine/oscenery.hpp"
#include <array>
#include <map>

namespace
{
    using Registers=OMainHost::Registers;
    using Math=std::array<uint16_t,32>;
    uint32_t be32(const uint8_t* b) { return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (b[2] << 8) | b[3]; }
    void read(std::ifstream& in, void* data, size_t size)
    {
        if (!in.read(static_cast<char*>(data), size)) throw std::runtime_error("truncated capture");
    }
    uint32_t u32(std::ifstream& in) { uint8_t b[4]; read(in,b,4); return be32(b); }
Registers registers(std::ifstream& in) {
    Registers r; for(auto& v:r.d) v=u32(in); for(auto& v:r.a) v=u32(in); return r;
}
bool equal(const Registers& a,const Registers& b) { return a.d==b.d && a.a==b.a; }
Math math(std::ifstream& in) { Math m;for(auto& v:m) v=u32(in);return m; }
void load_math(MainBus& b,const Math& m,bool main=true) {
    if(main) b.mul.load(m[0],m[1]);
    b.sub.mul.load(m[16],m[17]);
    for(unsigned base: {8u,24u}) {
        uint16_t v[]={m[base],m[base+1],m[base+2],m[base+4],m[base+5],m[base+6]};
        if(base==8) { if(main) b.div.load(v); } else b.sub.div.load(v);
    }
}
bool equal_math(MainBus& b,const Math& m) {
    for(unsigned i=0;i<8;++i) if(b.mul.read(i)!=m[i] || b.div.read(i)!=m[8+i]) return false;
    return true;
}
    void offsets(std::ifstream& in, std::vector<uint8_t>& marks)
    {
        std::fill(marks.begin(), marks.end(), 0);
        const uint32_t n = u32(in);
        if (n > marks.size()) throw std::runtime_error("invalid write-set count");
        for (uint32_t i = 0; i < n; ++i) marks.at(u32(in)) = 1;
    }
    using Event = std::tuple<uint32_t,uint16_t,uint16_t>;
}

int main(int argc, char** argv) try
{
    const char* path = argc > 1 ? argv[1] : "build/oracle/mainframe.bin";
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(std::string("No mainframe capture: ") + path);
    char magic[8]; read(in,magic,8);
    if (std::memcmp(magic,"AB2FRM05",8)) throw std::runtime_error("not a mainframe capture");
    if (!roms.load_aburner2()) return 1;
    MainBus::Memory memory;
    constexpr size_t N = MainBus::MEMORY_BYTES;
    std::vector<uint8_t> before(N), after(N), expected_w(N), other_w(N), reads(N), writes(N), supplied(N);
    unsigned total=0, identical=0, failed=0, skipped=0, reported=0, active=0,accepted_logic=0,accepted_normal=0;
    std::vector<std::pair<uint32_t,unsigned>> entries;
    OMainFrame setup;
    std::map<uint32_t,unsigned> verified_calls,external_calls;
    while (in.peek() != std::char_traits<char>::eof())
    {
        uint8_t hh[12]; read(in,hh,12);
        auto initial_math=math(in);auto io=u32(in);
        read(in,memory.sprite_buffer.data(),memory.sprite_buffer.size());
        auto mutex_clear_input=u32(in);if(mutex_clear_input>255) throw std::runtime_error("invalid shared mutex input");
        read(in,before.data(),N); read(in,after.data(),N);
        offsets(in,expected_w); offsets(in,other_w);
        std::vector<Event> expected_events, events;
        const unsigned count = u32(in);
        if (count > 100) throw std::runtime_error("invalid event count");
        for (unsigned i=0; i<count; ++i)
        {
            const uint32_t a=u32(in), d=u32(in), m=u32(in);
            expected_events.emplace_back(a,static_cast<uint16_t>(d & m),static_cast<uint16_t>(m));
        }
        struct Hook { uint32_t entry;Registers pre,post;uint8_t io;Math before,after;uint32_t blit_ptr=0,blit_count_ptr=0,blit_count=0;std::vector<uint8_t> blit; std::vector<Event> writes; };
        std::vector<Hook> hooks;
        const unsigned hook_count=u32(in);
        for(unsigned i=0;i<hook_count;++i) {
            Hook h;h.entry=u32(in);h.pre=registers(in);h.post=registers(in);h.io=u32(in);h.before=math(in);h.after=math(in);unsigned bytes=u32(in);
            if(bytes) {h.blit_ptr=u32(in);h.blit_count_ptr=u32(in);h.blit_count=u32(in);if(h.blit_count>192 || bytes!=12+(h.blit_count+1)*16) throw std::runtime_error("invalid sub blit input");h.blit.resize(bytes-12);read(in,h.blit.data(),h.blit.size());}
            unsigned n=u32(in);
            for(unsigned j=0;j<n;++j) {
                uint32_t a=u32(in),d=u32(in),m=u32(in);h.writes.emplace_back(a,d&m,m);
            }
            hooks.push_back(std::move(h));
        }
        ++total;
        const uint32_t entry=be32(hh+8);
        auto covered=std::find_if(entries.begin(),entries.end(),[&](auto e){return e.first==entry;});
        if (covered==entries.end()) entries.emplace_back(entry,1); else ++covered->second;
        std::copy(before.begin(),before.end(),memory.bytes.begin());
        std::fill(supplied.begin(),supplied.end(),0);
        std::fill(reads.begin(),reads.end(),0); std::fill(writes.begin(),writes.end(),0);
        MainBus bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);
        MainBus::Trace trace{reads.data(),writes.data(),[&](bool w,uint32_t a,uint16_t d,uint16_t m) {
            if (w && MainBus::memory_offset(a)==MainBus::UNMAPPED) events.emplace_back(a,d & m,m);
        }};
        unsigned mutex_reads=0;
        trace.before_access=[&](bool w,uint32_t a,uint16_t m) {
            if(!w && a==0x29c0d6 && m==0xff00 && ++mutex_reads==2) {
                // The sub CPU may finish during VBLANK. Supply only its
                // captured mutex bit at the real BCLR operand boundary.
                bus.trace=nullptr;auto old=bus.read8(a);
                if((old&0xfe)!=(mutex_clear_input&0xfe)) throw std::runtime_error("unlisted shared mutex flag change");
                bus.write8(a,mutex_clear_input);supplied[MainBus::memory_offset(a)]=1;bus.trace=&trace;
            }
        };
        load_math(bus,initial_math);bus.io[1].input=[&](unsigned p){return p==0 ? io : 0xff;};
        bus.trace=&trace;
        size_t hook_index=0;bool contract=true;
        auto phase=setup.interrupt(bus,[&](uint32_t e) {
            if(hook_index>=hooks.size() || hooks[hook_index].entry!=e)
                {std::cerr << "record " << be32(hh) << " hook " << hook_index << " actual " << std::hex << e << " expected " << (hook_index<hooks.size()?hooks[hook_index].entry:0) << std::dec << "\n"; throw std::runtime_error("frame callee sequence differs");}
            const auto& h=hooks[hook_index++];io=h.io;load_math(bus,h.before,false);
            std::vector<Event> produced;auto prior=trace.access;
            trace.access=[&](bool w,uint32_t a,uint16_t d,uint16_t m) {if(w) produced.emplace_back(a,d&m,m);};
            bool verified=true;auto r=h.pre;
            if(e==0x148be) setup.timers(bus);
            else if(e==0x1495c) setup.buttons(bus);
            else if(e==0x148b8) setup.bookkeeping(bus);
            else if(e==0x147e8) setup.service_edge(bus);
            else if(e==0xb7c2 || e==0xb7d2) {
                auto ptr=bus.read32(0x2a3c14)+(e==0xb7d2 ? 0xc00 : 0);
                auto cp=bus.read32(0x2a3c1c);
                if(ptr!=h.blit_ptr || cp!=h.blit_count_ptr) throw std::runtime_error("computed VBLANK blit pointer differs");
                // The concurrently completed sub list is a boundary input here;
                // the paired video check independently computes this list.
                bus.trace=nullptr;
                bus.write16(cp,h.blit_count);auto o=MainBus::memory_offset(cp);supplied.at(o)=supplied.at(o+1)=1;
                for(unsigned i=0;i<h.blit.size();++i) {bus.write8(ptr+i,h.blit[i]);supplied.at(MainBus::memory_offset(ptr+i))=1;}
                bus.trace=&trace;OMainSprites{}.blit(bus,e==0xb7d2);
            }
            else if(e==0x18444) OScenery{}.stage_events(bus);
            else if(e==0x14a86 || e==0x13f92 || e==0x13bcc || e==0x13bb6 || e==0x13bfa || e==0x1329e || e==0x13e5c) OMainPalette{}.run(bus,e);
            else if(e==0x16c8a) {OMainHost::call(bus,r,e);contract &= equal(r,h.post);}
            else if(e==0x17146 || e==0x14990 || e==0xc6f8 || e==0xe2da) {
                verified=false;++external_calls[e];bus.trace=nullptr;
                for(auto [a,d,m]:h.writes) {if(m==0xffff) bus.write16(a,d);else if(m==0xff) bus.write8(a+1,d);else if(m==0xff00) bus.write8(a,d>>8);else throw std::runtime_error("bad frame mask");}
                load_math(bus,h.after);bus.trace=&trace;
            } else throw std::runtime_error("unlisted frame external caller");
            if(verified) {
                ++verified_calls[e];
                if(produced!=h.writes || !equal_math(bus,h.after)) {std::cerr<<"frame "<<be32(hh+4)<<" caller "<<std::hex<<e<<std::dec<<" writes="<<(produced==h.writes)<<" chips="<<equal_math(bus,h.after)<<'\n';for(auto [a,d,m]:produced) std::cerr<<" actual "<<std::hex<<a<<':'<<d<<':'<<m<<std::dec<<'\n';for(auto [a,d,m]:h.writes) std::cerr<<" expected "<<std::hex<<a<<':'<<d<<':'<<m<<std::dec<<'\n';contract=false;}
                for(auto [a,d,m]:h.writes) { (void)d;auto o=MainBus::memory_offset(a);if(o!=MainBus::UNMAPPED) {if(m&0xff00) expected_w[o]=1;if(m&255) expected_w[o+1]=1;} }
            }
            trace.access=prior;
        });
        if(hook_index!=hooks.size()) throw std::runtime_error("missing frame callee");
        bool race=false;
        for (size_t i=0;i<N;++i) race |= reads[i] && other_w[i] && !supplied[i];
        if (race) { ++skipped;continue; }
        active += std::any_of(expected_w.begin(),expected_w.end(),[](uint8_t n){ return n!=0; });
        unsigned differences = (events!=expected_events)+!contract+(mutex_reads!=2);
        for (size_t i=0;i<N;++i)
            differences += writes[i]!=expected_w[i] || (expected_w[i] && memory.bytes[i]!=after[i]);
        if (differences)
        {
            ++failed;
            if (reported++<8)
            {
                std::cerr << "record " << be32(hh) << " frame " << be32(hh+4)
                          << ": " << differences << " differences, I/O events equal=" << (events==expected_events) << '\n';
                for (size_t i=0;i<N;++i)
                    if (writes[i]!=expected_w[i] || (expected_w[i] && memory.bytes[i]!=after[i]))
                        std::cerr << "  byte " << std::hex << i << " expected " << unsigned(after[i])
                                  << " actual " << unsigned(memory.bytes[i]) << " writes " << unsigned(expected_w[i])
                                  << '/' << unsigned(writes[i]) << std::dec << '\n';
            }
        }
        else {++identical;phase.logic ? ++accepted_logic : ++accepted_normal;}
    }
    std::cout << "Composed VBLANK vs arcade: " << total << " records, " << identical
              << " identical, " << failed << " different, " << skipped << " skipped (other CPU wrote an input)\n"
              << "Coverage: " << active << " calls with writes; entries";
    for (auto e:entries) std::cout << ' ' << std::hex << e.first << std::dec << '=' << e.second;
    std::cout << '\n';
    std::cout<<"Identical VBLANK phases: "<<accepted_logic<<" logic/interpolated, "<<accepted_normal<<" normal\n";
    std::cout<<"Executed VBLANK entries:";for(auto [e,n]:verified_calls) std::cout<<' '<<std::hex<<e<<std::dec<<'='<<n;std::cout<<"\nExternal VBLANK entries:";for(auto [e,n]:external_calls) std::cout<<' '<<std::hex<<e<<std::dec<<'='<<n;std::cout<<'\n';
    return total && identical && !failed ? 0 : 1;
}
catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
