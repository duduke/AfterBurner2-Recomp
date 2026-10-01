// Main CPU write-set differential test, not a replay of captured writes.
// Runs the ROM-derived interrupt scheduler against MainBus from each before state.
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>
#include "roms.hpp"
#include "engine/omainframe.hpp"

namespace
{
    uint32_t be32(const uint8_t* b) { return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (b[2] << 8) | b[3]; }
    void read(std::ifstream& in, void* data, size_t size)
    {
        if (!in.read(static_cast<char*>(data), size)) throw std::runtime_error("truncated capture");
    }
    uint32_t u32(std::ifstream& in) { uint8_t b[4]; read(in,b,4); return be32(b); }
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
    if (std::memcmp(magic,"AB2FRM01",8)) throw std::runtime_error("not a mainframe capture");
    if (!roms.load_aburner2()) return 1;
    MainBus::Memory memory;
    constexpr size_t N = MainBus::MEMORY_BYTES;
    std::vector<uint8_t> before(N), after(N), expected_w(N), other_w(N), reads(N), writes(N);
    unsigned total=0, identical=0, failed=0, skipped=0, reported=0, active=0;
    std::vector<std::pair<uint32_t,unsigned>> entries;
    OMainFrame setup;
    while (in.peek() != std::char_traits<char>::eof())
    {
        uint8_t h[12]; read(in,h,12);
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
        struct Hook { uint32_t entry; std::vector<Event> writes; };
        std::vector<Hook> hooks;
        const unsigned hook_count=u32(in);
        for(unsigned i=0;i<hook_count;++i) {
            Hook h{u32(in),{}};unsigned n=u32(in);
            for(unsigned j=0;j<n;++j) {
                uint32_t a=u32(in),d=u32(in),m=u32(in);h.writes.emplace_back(a,d,m);
            }
            hooks.push_back(std::move(h));
        }
        ++total;
        const uint32_t entry=be32(h+8);
        auto covered=std::find_if(entries.begin(),entries.end(),[&](auto e){return e.first==entry;});
        if (covered==entries.end()) entries.emplace_back(entry,1); else ++covered->second;
        std::copy(before.begin(),before.end(),memory.bytes.begin());
        std::fill(reads.begin(),reads.end(),0); std::fill(writes.begin(),writes.end(),0);
        MainBus bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);
        MainBus::Trace trace{reads.data(),writes.data(),[&](bool w,uint32_t a,uint16_t d,uint16_t m) {
            if (w && MainBus::memory_offset(a)==MainBus::UNMAPPED) events.emplace_back(a,d & m,m);
        }};
        bus.trace=&trace;
        size_t hook_index=0;
        setup.interrupt(bus,[&](uint32_t e) {
            if(hook_index>=hooks.size() || hooks[hook_index].entry!=e)
                {std::cerr << "record " << be32(h) << " hook " << hook_index << " actual " << std::hex << e << " expected " << (hook_index<hooks.size()?hooks[hook_index].entry:0) << std::dec << "\n"; throw std::runtime_error("frame callee sequence differs");}
            bus.trace=nullptr; // External boundary inputs, never credited as port writes.
            for(auto [a,d,m]:hooks[hook_index++].writes) {
                if(m==0xffff) bus.write16(a,d);
                else if(m==0xff00) bus.write8(a,d>>8);
                else if(m==0xff) bus.write8(a+1,d);
                else throw std::runtime_error("unexpected hook byte mask");
            }
            bus.trace=&trace;
        });
        if(hook_index!=hooks.size()) throw std::runtime_error("missing frame callee");
        bool race=false;
        for (size_t i=0;i<N;++i) race |= reads[i] && other_w[i];
        if (race) { ++skipped; continue; }
        active += std::any_of(expected_w.begin(),expected_w.end(),[](uint8_t n){ return n!=0; });
        unsigned differences = events!=expected_events;
        for (size_t i=0;i<N;++i)
            differences += writes[i]!=expected_w[i] || (expected_w[i] && memory.bytes[i]!=after[i]);
        if (differences)
        {
            ++failed;
            if (reported++<8)
            {
                std::cerr << "record " << be32(h) << " frame " << be32(h+4)
                          << ": " << differences << " differences, I/O events equal=" << (events==expected_events) << '\n';
                for (size_t i=0;i<N;++i)
                    if (writes[i]!=expected_w[i] || (expected_w[i] && memory.bytes[i]!=after[i]))
                        std::cerr << "  byte " << std::hex << i << " expected " << unsigned(after[i])
                                  << " actual " << unsigned(memory.bytes[i]) << " writes " << unsigned(expected_w[i])
                                  << '/' << unsigned(writes[i]) << std::dec << '\n';
            }
        }
        else ++identical;
    }
    std::cout << "Main frame scheduler vs arcade: " << total << " records, " << identical
              << " identical, " << failed << " different, " << skipped << " skipped (other CPU wrote an input)\n"
              << "Coverage: " << active << " calls with writes; entries";
    for (auto e:entries) std::cout << ' ' << std::hex << e.first << std::dec << '=' << e.second;
    std::cout << '\n';
    return total && identical && !failed ? 0 : 1;
}
catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
