#pragma once
#include <array>
#include <cstdint>
#include <stdexcept>

// Bounded SegaPCM register endpoint while every channel remains stopped.
// MAME segapcm.cpp initializes RAM to FF. With bit 0 set in every control
// register, stream updates leave RAM unchanged. Active channel stepping is
// deliberately rejected until separately verified; CPU reset does not reset
// this device. CPU-visible 8-bit offsets already include the X-Board mirrors.
class XBoardPcmStartup
{
public:
    XBoardPcmStartup() { regs.fill(0xff); }
    uint8_t read(uint8_t offset) const { return regs[offset]; }
    void write(uint8_t offset,uint8_t data) {
        if((offset&0x87)==0x86 && !(data&1))
            throw std::runtime_error("unverified active PCM channel stepping");
        regs[offset]=data;
    }
private:
    std::array<uint8_t,256> regs;
};
