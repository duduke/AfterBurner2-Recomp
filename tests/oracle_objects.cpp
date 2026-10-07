// Main CPU write-set differential test, not a replay of captured writes.
// Runs the ROM-derived object initialization, dispatch, RNG and sound queue routines against MainBus from each before state.
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>
#include "roms.hpp"
#include "engine/omainobjects.hpp"
#include "engine/omainhost.hpp"
#include "engine/omainsound.hpp"


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
    const char* path = argc > 1 ? argv[1] : "build/oracle/objects.bin";
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(std::string("No object capture: ") + path);
    char magic[8]; read(in,magic,8);
    if (std::memcmp(magic,"AB2OBJ01",8)) throw std::runtime_error("not an object capture");
    if (!roms.load_aburner2()) return 1;
    MainBus::Memory memory;
    constexpr size_t N = MainBus::MEMORY_BYTES;
    std::vector<uint8_t> before(N), after(N), expected_w(N), other_w(N), reads(N), writes(N);
    unsigned total=0, identical=0, failed=0, skipped=0, reported=0, active=0;
    std::vector<std::pair<uint32_t,unsigned>> entries;
    std::map<uint32_t,unsigned> handler_counts;

    while (in.peek() != std::char_traits<char>::eof())
    {
        uint8_t h[12]; read(in,h,12);
        auto registers=[&] {
            OMainObjects::Registers r;
            for(auto& v:r.d) v=u32(in);
            for(auto& v:r.a) v=u32(in);
            return r;
        };
        auto r=registers(), expected_r=registers();
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
        struct Hook {
            uint32_t entry;
            OMainObjects::Registers pre,post;
            std::vector<Event> writes;
        };
        std::vector<Hook> hooks;
        unsigned hook_count=u32(in);
        if(hook_count>65536) throw std::runtime_error("invalid hook count");
        for(unsigned i=0;i<hook_count;++i) {
            Hook h; h.entry=u32(in); h.pre=registers(); h.post=registers();
            unsigned n=u32(in);
            if(n>1000000) throw std::runtime_error("invalid hook write count");
            for(unsigned j=0;j<n;++j) {
                uint32_t a=u32(in),d=u32(in),m=u32(in);
                h.writes.emplace_back(a,d,m);
            }
            ++handler_counts[h.entry];
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
        unsigned hook_index=0;
        bool contract=true;
        if(entry==0x1488e || entry==0x16892 || entry==0x16976 || entry==0x169b2 || entry==0xc5a8)
            OMainHost::call(bus,r,entry);
        else if(entry==0xb7f8) OMainSound::consume(bus,r);
        else if(entry==0xb840 || entry==0xb84c) OMainSound::enqueue(bus,r,entry==0xb84c);
        else if(entry==0x14594) OMainObjects::dispatch(bus,r,[&](uint32_t target,auto& state) {
            if(hook_index>=hooks.size()) throw std::runtime_error("unexpected object call");
            const auto& h=hooks[hook_index++];
            contract &= target==h.entry && state.d==h.pre.d && state.a==h.pre.a;
            // External handler boundaries, not implementations or correctness
            // claims for the unported handlers. Disable both write/read traces.
            bus.trace=nullptr;
            for(auto [a,d,m]:h.writes) {
                if(m==0xffff) bus.write16(a,d);
                else {
                    if(m&0xff00) bus.write8(a,d>>8);
                    if(m&0xff) bus.write8(a+1,d);
                }
            }
            bus.trace=&trace; state=h.post;
        });
        else throw std::runtime_error("unknown object entry");
        contract &= hook_index==hooks.size() && r.d==expected_r.d && r.a==expected_r.a;
        bool race=false;
        for (size_t i=0;i<N;++i) race |= reads[i] && other_w[i];
        if (race) { ++skipped; continue; }
        active += std::any_of(expected_w.begin(),expected_w.end(),[](uint8_t n){ return n!=0; });
        unsigned differences = (events!=expected_events) + !contract;
        for (size_t i=0;i<N;++i)
            differences += writes[i]!=expected_w[i] || (expected_w[i] && memory.bytes[i]!=after[i]);
        if (differences)
        {
            ++failed;
            if (reported++<8)
            {
                std::cerr << "record " << be32(h) << " frame " << be32(h+4)
                          << ": " << differences << " differences, register/call contract=" << contract << ", I/O events equal=" << (events==expected_events) << '\n';
                for (size_t i=0;i<N;++i)
                    if (writes[i]!=expected_w[i] || (expected_w[i] && memory.bytes[i]!=after[i]))
                        std::cerr << "  byte " << std::hex << i << " expected " << unsigned(after[i])
                                  << " actual " << unsigned(memory.bytes[i]) << " writes " << unsigned(expected_w[i])
                                  << '/' << unsigned(writes[i]) << std::dec << '\n';
            }
        }
        else ++identical;
    }
    std::cout << "Main objects vs arcade: " << total << " records, " << identical
              << " identical, " << failed << " different, " << skipped << " skipped (other CPU wrote an input)\n"
              << "Coverage: " << active << " calls with writes; entries";
    for (auto e:entries) std::cout << ' ' << std::hex << e.first << std::dec << '=' << e.second;
    std::cout << '\n';
    MainBus inventory_bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);
    auto templates=OMainObjects::inventory(inventory_bus);
    std::cout << "Runtime template count: " << templates.size() << '\n';
    std::map<uint32_t,unsigned> template_counts;
    for(const auto& t:templates) ++template_counts[t.handler];
    std::cout << "Runtime template handlers:";
    for(auto [target,n]:template_counts) std::cout << ' ' << std::hex << target << std::dec << '=' << n;
    std::cout << "\nObserved handler boundaries (external, unported):";
    for(auto [target,n]:handler_counts) std::cout << ' ' << std::hex << target << std::dec << '=' << n;
    std::cout << '\n';
    return total && identical && !failed ? 0 : 1;
}
catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
