// Main CPU write-set differential test, not a replay of captured writes.
// Runs the ROM-derived ADC sampler against MainBus from each before state.
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <tuple>
#include <vector>
#include "roms.hpp"
#include "engine/omainadc.hpp"

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
    const char* path = argc > 1 ? argv[1] : "build/oracle/main.bin";
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(std::string("No main CPU capture: ") + path);
    char magic[8]; read(in,magic,8);
    if (std::memcmp(magic,"AB2MAIN1",8)) throw std::runtime_error("not a main CPU capture");
    if (!roms.load_aburner2()) return 1;
    MainBus::Memory memory;
    constexpr size_t N = MainBus::MEMORY_BYTES;
    std::vector<uint8_t> before(N), after(N), expected_w(N), other_w(N), reads(N), writes(N);
    unsigned total=0, identical=0, failed=0, skipped=0, wraps=0, reported=0;
    unsigned samples[6] = {}, inputs[256] = {};
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
        ++total;
        std::copy(before.begin(),before.end(),memory.bytes.begin());
        std::fill(reads.begin(),reads.end(),0); std::fill(writes.begin(),writes.end(),0);
        MainBus bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);
        bus.io[0].write(6,h[10]); bus.io[0].write(7,h[11]);
        uint8_t portc=0;
        bus.io[0].output = [&](unsigned p,uint8_t d,uint8_t) { if (p==2) portc=d; };
        bus.endpoints.adc_read = [&] { return h[8]; };
        MainBus::Trace trace{reads.data(),writes.data(),[&](bool w,uint32_t a,uint16_t d,uint16_t m) {
            if (w && MainBus::memory_offset(a)==MainBus::UNMAPPED) events.emplace_back(a,d & m,m);
        }};
        bus.trace=&trace;
        const unsigned slot = bus.read16(0xffff8890);
        main_adc_sample(bus);
        bool race=false;
        for (size_t i=0;i<N;++i) race |= reads[i] && other_w[i];
        if (race) { ++skipped; continue; }
        if (slot < 6) ++samples[slot];
        if (slot==5) ++wraps;
        ++inputs[h[8]];
        unsigned differences = events!=expected_events || portc!=h[9];
        for (size_t i=0;i<N;++i)
            differences += writes[i]!=expected_w[i] || (expected_w[i] && memory.bytes[i]!=after[i]);
        if (differences)
        {
            ++failed;
            if (reported++<8)
            {
                std::cerr << "record " << be32(h) << " frame " << be32(h+4)
                          << ": " << differences << " differences, I/O events equal=" << (events==expected_events)
                          << ", port C expected=" << unsigned(h[9]) << " actual=" << unsigned(portc) << '\n';
                for (size_t i=0;i<N;++i)
                    if (writes[i]!=expected_w[i] || (expected_w[i] && memory.bytes[i]!=after[i]))
                        std::cerr << "  byte " << std::hex << i << " expected " << unsigned(after[i])
                                  << " actual " << unsigned(memory.bytes[i]) << " writes " << unsigned(expected_w[i])
                                  << '/' << unsigned(writes[i]) << std::dec << '\n';
            }
        }
        else ++identical;
    }
    const unsigned values = std::count_if(std::begin(inputs),std::end(inputs),[](unsigned n){ return n!=0; });
    std::cout << "Main CPU ADC sampler vs arcade: " << total << " records, " << identical
              << " identical, " << failed << " different, " << skipped << " skipped (other CPU wrote an input)\n"
              << "Coverage: slots";
    for (unsigned i=0;i<6;++i) std::cout << ' ' << i << '=' << samples[i];
    std::cout << ", " << wraps << " wraps, " << values << " distinct ADC values\n";
    return total && identical && !failed ? 0 : 1;
}
catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
