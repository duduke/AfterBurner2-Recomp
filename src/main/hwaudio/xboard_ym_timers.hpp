// SPDX-License-Identifier: BSD-3-Clause
// Timer/status subset derived from ymfm_opm.cpp, ymfm_fm.ipp and
// MAME ymfm_mame.h (Aaron Giles). FM sample synthesis is a separate owner.
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <stdexcept>

class XBoardYmTimers {
public:
    static constexpr uint64_t NEVER=std::numeric_limits<uint64_t>::max();
    std::function<void(bool)> irq_line;
    std::function<void(uint64_t,uint8_t)> synchronize_mode;
    // Timer B phase requires actual generated FM clocks from the stream owner.
    std::function<uint64_t(uint64_t)> fm_sample_clock;
    uint64_t deadline() const {return std::min(timer[0],timer[1]);}
    void advance(uint64_t ns) {
        if(ns<now)throw std::runtime_error("nonmonotonic YM timer time");
        while(deadline()<=ns) {
            const auto at=deadline();now=at;
            for(unsigned n=0;n<2;++n)if(timer[n]==at) {
                if(regs[0x14]&(4u<<n))status|=1u<<n;
                timer[n]=at+period(n);update_irq();
            }
        }
        now=ns;
    }
    uint8_t read(unsigned offset,uint64_t ns) {
        return offset&1?status|(ns<busy_end?0x80:0):0xff;
    }
    void write(unsigned offset,uint8_t data,uint64_t ns) {
        if(!(offset&1)){address=data;return;}
        busy_end=ns+64*250; // 32 * OPM prescale(2) chip clocks.
        if(address==0x14) {
            if(!synchronize_mode)throw std::runtime_error("unbound YM mode synchronization");
            synchronize_mode(ns,data);
        }else regs[address]=data;
    }
    // Host invokes the synchronized mode callback at the global frontier.
    void mode(uint8_t data,uint64_t ns) {
        // A synchronized callback retains its posted timestamp even when
        // delivered late after a different CPU advanced global basetime.
        regs[0x14]=data;
        if(data&0x10)status&=~1u;
        if(data&0x20)status&=~2u;
        update_irq();
        for(unsigned n=0;n<2;++n) {
            if(!(data&(1u<<n)))timer[n]=NEVER;
            else if(timer[n]==NEVER) {
                auto clocks=period(n)/250;
                // OPM total_clocks is the FM sample count (clock()/64).
                if(n){
                    if(!fm_sample_clock)throw std::runtime_error("unbound YM timer B FM clock owner");
                    clocks-=64*(fm_sample_clock(ns)&15);
                }
                timer[n]=ns+clocks*250;
            }
        }
    }
private:
    uint64_t period(unsigned n) const {
        const unsigned ticks=n?16*(256-regs[0x12]):1024-((unsigned(regs[0x10])<<2)|(regs[0x11]&3));
        return uint64_t(ticks)*64*250;
    }
    void update_irq() {const bool next=status&3;if(next!=irq){irq=next;if(irq_line)irq_line(irq);}}
    std::array<uint8_t,256> regs{};
    std::array<uint64_t,2> timer{NEVER,NEVER};
    uint64_t now{},busy_end{};uint8_t address{},status{};bool irq{};
};
