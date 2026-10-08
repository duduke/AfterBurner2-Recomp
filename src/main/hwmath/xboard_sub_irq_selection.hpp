#pragma once
#include <algorithm>
#include <cstdint>
#include "hwmath/xboard_timeline.hpp"
// Completed bus and instruction-tail yields delimit delivery. Internal
// clocks before a bus do not deliver a new scanline edge by themselves.
class XBoardSubIrqSelection {
public:
    void resume(uint64_t ns){yield=ns;}
    void complete_bus(uint64_t ns){yield=ns+320;}
    void complete_instruction(uint64_t ns){yield=ns;}
    unsigned level(uint64_t poll_ns)const {
        const auto delivered=std::min(poll_ns,yield);
        constexpr auto first=XBoardTimeline::RESET_LINE0_NS+223*XBoardTimeline::LINE_NS;
        return delivered>=first&&(delivered-first)%XBoardTimeline::FRAME_NS<XBoardTimeline::LINE_NS?4:0;
    }
private:
    uint64_t yield{};
};
