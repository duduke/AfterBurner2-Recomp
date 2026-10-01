#pragma once
#include <cstdint>
#include <functional>
#include <limits>

// X-Board ADC0804 in strobed-read mode. Hardware behavior from MAME's
// adc0804.cpp (BSD-3-Clause, AJR); clock from segaxbd.cpp: 1.25 MHz,
// 74 clocks per conversion. The caller supplies monotonically increasing ns.
class ADC0804 {
public:
    std::function<uint8_t()> analog;
    void advance(uint64_t ns) {
        if(pending && ns>=completion) {
            result=analog ? analog() : 0; pending=false; interrupt=true;
        }
    }
    uint8_t read(uint64_t ns) { advance(ns); interrupt=false; return result; }
    void write(uint64_t ns) {
        advance(ns); interrupt=false;
        // A write while busy clears INTR but does not restart conversion.
        if(!pending) { completion=ns+59200; pending=true; }
    }
    uint8_t value() const { return result; }
    bool intr() const { return interrupt; }
    bool busy() const { return pending; }
    uint64_t deadline() const { return pending ? completion : std::numeric_limits<uint64_t>::max(); }
private:
    uint8_t result=0;
    bool pending=false, interrupt=false;
    uint64_t completion=0;
};
