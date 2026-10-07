#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "engine/omainio.hpp"

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
    const uint64_t edge=number(f,8);
    if(edge!=XBoardTimeline::RESET_LINE0_NS+XBoardTimeline::LINE_NS)
        throw std::runtime_error("unexpected reset phase");
    unsigned records=0,identical=0,failed=0,conversions=0,reads=0;
    MainBus::Memory memory;MainBus bus(nullptr,0,nullptr,0,memory);
    OMainIO hardware(bus);auto& board=hardware.board;auto& adc=board.adc;
    auto& timer=bus.compare_timer;
    bool seen_frame=false;
    std::array<uint8_t,8> inputs{}; inputs.fill(0x10);
    hardware.set_inputs(inputs);
    while(f.peek()!=std::char_traits<char>::eof()) {
        uint64_t ns=number(f,8); uint32_t pc=number(f,4),address=number(f,4);
        uint16_t data=number(f,2),mask=number(f,2);
        unsigned kind=number(f,1),result=number(f,1);
        number(f,1); // Reserved byte: this MAME build exposes no internal INTR state.
        unsigned irq=number(f,1),selected=number(f,1);
        std::array<uint8_t,8> current; for(auto& v:current) v=number(f,1);
        if(adc.busy() && adc.deadline()<=ns) ++conversions;
        if(kind==5) board.advance_before(ns);else board.advance(ns);
        // Read taps follow the device read (which acknowledges INTR).
        uint8_t read=0; if(kind==1) { read=bus.read8(0x130001); ++reads; }
        bool equal=(kind!=1 || adc.value()==result) && timer.state().irq==bool(irq) && hardware.selected_mux()==selected;
        if(kind==1) equal &= (data&mask)==(read&mask);
        if(kind==2 && (mask&0xff)) bus.write8(0x130001,data);
        else if(kind==3 && (mask&0xff)) bus.io[0].output(2,data,0xff);
        else if(kind==4) timer.write((address>>1)&15,data,mask);
        // Input fields are frame-boundary changes. Other events assert that
        // no unsampled changes occurred between the captured boundaries.
        if(kind==5) { inputs=current; seen_frame=true; }
        else if(current!=inputs) {
            if(seen_frame) equal=false;
            inputs=current; // Boot defaults before first notifier.
        }
        hardware.set_inputs(inputs);
        ++records;
        if(equal) ++identical;
        else {
            ++failed;
            if(failed<9) std::cerr << "ADC event " << records-1 << " t=" << ns << " PC=" << std::hex << pc
                << " kind=" << kind << " result=" << unsigned(adc.value()) << '/' << result
                << " INTR(model)=" << adc.intr() << " IRQ=" << timer.state().irq << '/' << irq
                << " mux=" << unsigned(hardware.selected_mux()) << '/' << selected << std::dec << '\n';
        }
    }
    std::cout << "ADC/timer vs arcade: " << records << " records, " << identical << " identical, " << failed
        << " different, 0 skipped; " << conversions << " completed conversions, " << reads << " reads\n";
    return records && conversions && !failed ? 0 : 1;
} catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
