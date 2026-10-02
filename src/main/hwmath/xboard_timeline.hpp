#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include "hwmath/adc0804.hpp"
#include "hwmath/compare_timer.hpp"

// Physical X-Board timeline from MAME segaxbd.cpp: 50 MHz / 8 pixels,
// 400 pixels per line, 262 lines. scanline_tick clocks main EXCK with V0,
// asserts both CPUs' VBLANK on line 223 and clears it on line 224.
// No CPU execution/interrupt latency is implied by these line levels.
class XBoardTimeline {
public:
    static constexpr uint64_t LINE_NS = 64000;
    static constexpr unsigned LINES = 262;
    static constexpr uint64_t FRAME_NS = LINE_NS * LINES;
    // A fresh MAME screen starts at line 224; board reset arms line 0.
    static constexpr uint64_t RESET_LINE0_NS = (LINES - 224) * LINE_NS;

    explicit XBoardTimeline(CompareTimer5250& timer,
                            uint64_t first_line0 = RESET_LINE0_NS)
        : timer(timer), next_line(first_line0) {}

    ADC0804 adc;
    // Notification after each scanline's hardware changes, at its exact time.
    std::function<void(uint64_t, unsigned)> scanline;

    void advance(uint64_t ns) { advance_to(ns, true); }
    // The MAME frame notifier at line 224 precedes scanline_tick at the
    // same timestamp. External inputs change before that edge is delivered.
    void advance_before(uint64_t ns) { advance_to(ns, false); }

    uint64_t now() const { return time; }
    uint64_t next_scanline() const { return next_line; }
    bool vblank() const { return blank; }
    // segaxbd_state::update_main_irqs: timer=2, VBLANK=4, together=6.
    unsigned main_irq_level() const { return (timer.state().irq ? 2 : 0) | (blank ? 4 : 0); }
    unsigned sub_irq_level() const { return blank ? 4 : 0; }
private:
    void advance_to(uint64_t ns, bool inclusive) {
        if (ns < time) throw std::runtime_error("nonmonotonic X-Board time");
        // Complete conversions at their own deadlines, before a later mux or
        // input change. Jumping across scanlines must retain chronological order.
        while (std::min(next_line, adc.deadline()) < ns ||
               (inclusive && std::min(next_line, adc.deadline()) == ns)) {
            time = std::min(next_line, adc.deadline());
            adc.advance(time);
            if (next_line == time) {
                timer.clock(line & 1);
                if (line == 223) blank = true;
                else if (line == 224) blank = false;
                const unsigned current = line;
                line = (line + 1) % LINES;
                next_line += LINE_NS;
                if (scanline) scanline(time, current);
            }
        }
        time = ns;
    }

    CompareTimer5250& timer;
    uint64_t time = 0, next_line;
    unsigned line = 0;
    bool blank = false;
};
