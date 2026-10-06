#pragma once
#include "hwmath/xboard_timeline.hpp"
#include "hwmath/m68000_completion.hpp"

// MAME schedule.cpp runs to the next timer deadline; m68000-sif/sip yield
// after a four-clock bus phase, not after each internal clock. An access
// begun after an internal overrun still sees the pre-delivery board lines.
// An instruction completing its internal tail also yields in execute_run;
// the previous opcode/source identifies that distinct completion frontier.
// Across unported CPU graphs the last completed phase is an explicit input.
class XBoardCpuVisibility {
public:
    explicit XBoardCpuVisibility(XBoardTimeline& timeline) : board(timeline) {}
    void resume(uint64_t previous_phase_end, uint16_t completed_opcode=0,
                uint16_t shift_source=0) {
        end = previous_phase_end + 80 * m68000_completion_tail(completed_opcode,shift_source);
    }
    unsigned before_bus(uint64_t ns) {
        const auto delivered = std::max(board.now(), std::min(ns, end));
        if (ns < board.now()) throw std::runtime_error("nonmonotonic CPU bus time");
        board.advance(delivered);
        return board.main_irq_level();
    }
    void complete_bus(uint64_t ns) { end = ns + 4 * 80; }
    uint64_t phase_end() const { return end; }
private:
    XBoardTimeline& board;
    uint64_t end = 0;
};
