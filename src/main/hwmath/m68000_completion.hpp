#pragma once
#include <cstdint>

// Bounded completion clocks from MAME m68000-sif/sip (BSD-3-Clause,
// Olivier Galibert). Shared by IRQ-entry and physical visibility checks.
inline unsigned m68000_completion_tail(uint16_t op,uint16_t source) {
    const unsigned family=op>>12,mode=(op>>3)&7,size=(op>>6)&3;
    if(family==14 && size!=3) {
        unsigned count=op&0x20 ? source&63 : (op>>9)&7;
        if(!(op&0x20) && !count) count=8;
        return (size==2 ? 4 : 2)+2*count;
    }
    // MAME's roml3/rcal3: register-source long ALU ends with four/two
    // internal clocks; memory-source long ALU ends with two clocks.
    if((family==8 || family==9 || family==11 || family==12 || family==13) && size==2 && !(op&0x100))
        return mode<2 ? (family==11 ? 2 : 4) : 2;
    // ADDA/SUBA/CMPA word ends with four clocks; long memory source with
    // two. CMPA uses its own rcal3 two-clock ending.
    if((family==9 || family==11 || family==13) && size==3)
        return family==11 ? 2 : (!(op&0x100) || mode<2 ? 4 : 2);
    // ANDI.L #imm,Dn: roal3/roal4 complete four clocks after prefetch.
    if((op&0xfff8)==0x0280) return 4;
    // Immediate BTST on Dn finishes its Z flag two clocks after prefetch.
    if((op&0xfff8)==0x0800) return 2;
    // EXG's register exchange completes two clocks after its prefetch.
    if((op&0xf1f8)==0xc140 || (op&0xf1f8)==0xc148 || (op&0xf1f8)==0xc188) return 2;
    return 0;
}
