#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "hwmath/adc0804.hpp"
#include "hwmath/compare_timer.hpp"

namespace {
uint64_t number(std::ifstream& f, unsigned n) {
    uint64_t v=0;
    while(n--) { int c=f.get(); if(c<0) throw std::runtime_error("truncated ADC timeline"); v=(v<<8)|c; }
    return v;
}
}
int main(int argc,char** argv) try {
    std::ifstream f(argc>1 ? argv[1] : "build/oracle/adc_timing.bin",std::ios::binary);
    char magic[8]; if(!f.read(magic,8) || std::memcmp(magic,"AB2ADT01",8)) throw std::runtime_error("invalid ADC timeline");
    uint64_t edge=number(f,8); unsigned records=0,identical=0,failed=0,conversions=0,reads=0;
    ADC0804 adc; CompareTimer5250 timer;
    bool seen_frame=false;
    uint8_t mux=0; std::array<uint8_t,8> inputs{}; inputs.fill(0x10);
    adc.analog=[&] { return inputs[(mux>>2)&7]; };
    while(f.peek()!=std::char_traits<char>::eof()) {
        uint64_t ns=number(f,8); uint32_t pc=number(f,4),address=number(f,4);
        uint16_t data=number(f,2),mask=number(f,2);
        unsigned kind=number(f,1),result=number(f,1);
        number(f,1); // Reserved byte: this MAME build exposes no internal INTR state.
        unsigned irq=number(f,1),selected=number(f,1);
        std::array<uint8_t,8> current; for(auto& v:current) v=number(f,1);
        while(edge<=ns) { timer.clock(false); timer.clock(true); edge+=128000; }
        if(adc.busy() && adc.deadline()<=ns) ++conversions;
        adc.advance(ns);
        // Read taps follow the device read (which acknowledges INTR).
        uint8_t read=0; if(kind==1) { read=adc.read(ns); ++reads; }
        bool equal=(kind!=1 || adc.value()==result) && timer.state().irq==bool(irq) && mux==selected;
        if(kind==1) equal &= (data&mask)==(read&mask);
        if(kind==2 && (mask&0xff)) adc.write(ns);
        else if(kind==3 && (mask&0xff)) mux=data;
        else if(kind==4) timer.write((address>>1)&15,data,mask);
        // Input fields are frame-boundary changes. Other events assert that
        // no unsampled changes occurred between the captured boundaries.
        if(kind==5) { inputs=current; seen_frame=true; }
        else if(current!=inputs) {
            if(seen_frame) equal=false;
            inputs=current; // Boot defaults before first notifier.
        }
        ++records;
        if(equal) ++identical;
        else {
            ++failed;
            if(failed<9) std::cerr << "ADC event " << records-1 << " t=" << ns << " PC=" << std::hex << pc
                << " kind=" << kind << " result=" << unsigned(adc.value()) << '/' << result
                << " INTR(model)=" << adc.intr() << " IRQ=" << timer.state().irq << '/' << irq
                << " mux=" << unsigned(mux) << '/' << selected << std::dec << '\n';
        }
    }
    std::cout << "ADC/timer vs arcade: " << records << " records, " << identical << " identical, " << failed
        << " different, 0 skipped; " << conversions << " completed conversions, " << reads << " reads\n";
    return records && conversions && !failed ? 0 : 1;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
