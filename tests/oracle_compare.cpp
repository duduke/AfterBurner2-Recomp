// Replay actual 315-5250 bus traffic and scanline clocks, checking readable
// comparator registers, sound data and the board's timer IRQ line in MAME.
#include <cstring>
#include <fstream>
#include <iostream>
#include "hwmath/compare_timer.hpp"

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "build/oracle/compare.bin";
    std::ifstream in(path,std::ios::binary);
    char magic[8]; in.read(magic,8);
    if (!in || std::memcmp(magic,"AB2CMP01",8)) { std::cerr << "Missing/invalid compare capture\n"; return 1; }
    CompareTimer5250 chips[2];
    uint32_t clocks=0;
    unsigned total=0, identical=0, failed=0, irq_acks=0, sound_reads=0, compares=0;
    while (in.peek()!=std::char_traits<char>::eof())
    {
        uint8_t b[16];
        if (!in.read(reinterpret_cast<char*>(b),16)) { std::cerr << "Truncated compare capture\n"; return 1; }
        auto u32 = [](const uint8_t* p){ return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(p[2]<<8)|p[3]; };
        const uint32_t ticks=u32(b), reg=u32(b+4);
        const unsigned kind=b[8], chip=b[9];
        const uint16_t data=static_cast<uint16_t>((b[10]<<8)|b[11]), mask=static_cast<uint16_t>((b[12]<<8)|b[13]);
        if (chip>1 || kind<1 || kind>4 || ticks<clocks) { std::cerr << "Invalid compare record\n"; return 1; }
        while (clocks<ticks) { chips[0].clock(false); chips[0].clock(true); ++clocks; }
        bool match=true;
        if (kind==1)
        {
            // Write taps run before the handler; read taps run after it.
            match = chips[0].state().irq == bool(b[14]);
            if (chip==0 && ((reg&15)==9 || (reg&15)==13) && chips[0].state().irq) ++irq_acks;
            if (chip==1 && (reg==2 || reg==6)) ++compares;
            chips[chip].write(reg,data,mask);
        }
        else if (kind==2)
        {
            match = (chips[chip].read(reg)&mask)==(data&mask) && chips[0].state().irq==bool(b[14]);
        }
        else if (kind==3) { ++sound_reads; match=chips[0].sound_read()==static_cast<uint8_t>(data); }
        else match=chips[0].state().irq==bool(b[14]);
        ++total;
        if (match) ++identical;
        else
        {
            if (++failed<=12) std::cerr << "compare event " << total << " tick " << ticks << " kind " << kind
                                       << " chip " << chip << " reg " << reg << " disagrees\n";
        }
    }
    std::cout << "315-5250 vs MAME: " << total << " events, " << identical << " identical, " << failed
              << " different, 0 skipped; " << irq_acks << " timer IRQ acknowledgements, " << sound_reads
              << " sound reads, " << compares << " comparator writes\n";
    return identical && !failed && irq_acks && sound_reads && compares ? 0 : 1;
}
