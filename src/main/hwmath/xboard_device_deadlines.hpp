#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include "hwmath/xboard_timeline.hpp"

// MAME screen.cpp, segaic16.cpp and sound.cpp: fresh screen starts at
// line 224; tile latch is line 261, first scanline line 0, sound flush 50 Hz.
// These hardware timers are independent of CPU quantum/grant boundaries.
class XBoardDeviceDeadlines {
public:
    enum Owner {TileLatch,FirstScanline,VblankBegin,VblankEnd,SoundUpdate,Count};
    std::function<void(Owner,uint64_t)> callback;
    uint64_t deadline() const {return *std::min_element(next.begin(),next.end());}
    void advance(uint64_t ns) {
        if(ns<now)throw std::runtime_error("nonmonotonic device deadline time");
        while(deadline()<=ns){const auto at=deadline();
            for(unsigned i=0;i<Count;++i)if(next[i]==at){
                next[i]+=i==SoundUpdate?20000000:XBoardTimeline::FRAME_NS;
                ++deliveries[i];if(callback)callback(Owner(i),at);
            }
        }
        now=ns;
    }
    std::array<uint64_t,Count> deliveries{};
private:
    uint64_t now{};
    std::array<uint64_t,Count> next{37*64000,38*64000,262*64000,
                                  300*64000,20000000};
};
