#pragma once
#include <cstdint>
#include <stdexcept>
#include <vector>
#include "engine/mainbus.hpp"

// Only lanes written by the sub since the last main write may be supplied.
// Other lanes must equal the computed port state. Captured operands establish
// composition at access boundaries, not independent CPU scheduling.
struct SharedOperand {
    uint32_t address; uint16_t data,mask,sub_mask;
};
inline bool shared_operand_address(uint32_t a) {
    return (a>=0x29c088 && a<0x29c09a) || a==0x29c09e || a==0x29c0a0 ||
        a==0x29c0a8 || a==0x29c0b4 || (a>=0x29c1ba && a<0x29c1c0) ||
        (a>=0x29c620 && a<0x29c632) || (a>=0x2a3800 && a<0x2a3c00);
}
inline bool shared_operand(MainBus& bus,const std::vector<SharedOperand>& inputs,
                           size_t& cursor,uint32_t a,uint16_t mask,
                           std::vector<uint8_t>& supplied) {
    if(!shared_operand_address(a)) return true;
    const auto& q=inputs.at(cursor++);
    if(q.address!=a || q.mask!=mask || (q.sub_mask&~mask))
        throw std::runtime_error("shared operand access/provenance differs");
    auto trace=bus.trace;bus.trace=nullptr;
    const auto old=bus.read16(a);
    const bool equal=(old&mask&~q.sub_mask)==(q.data&mask&~q.sub_mask);
    if(q.sub_mask&0xff00) {bus.write8(a,q.data>>8);supplied.at(MainBus::memory_offset(a))=1;}
    if(q.sub_mask&255) {bus.write8(a+1,q.data);supplied.at(MainBus::memory_offset(a)+1)=1;}
    // Even an unchanged operand is checked at its chronological boundary;
    // a later sub write cannot retroactively turn that read into a race.
    auto o=MainBus::memory_offset(a);
    if(mask&0xff00) supplied.at(o)=1;
    if(mask&255) supplied.at(o+1)=1;
    bus.trace=trace;return equal;
}
