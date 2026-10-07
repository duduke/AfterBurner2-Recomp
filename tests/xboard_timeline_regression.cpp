#include <iostream>
#include <vector>
#include "hwmath/xboard_cpu_visibility.hpp"

void check(bool condition) {
    if(!condition) throw std::runtime_error("X-Board timeline regression failed");
}

int main() {
    CompareTimer5250 timer;
    XBoardTimeline board(timer);
    uint8_t analog=0x20;
    board.adc.analog=[&] { return analog; };
    board.adc.write(board.now());
    board.advance(59199); check(board.adc.busy());
    analog=0xe0;
    board.advance(59200); check(!board.adc.busy() && board.adc.value()==0xe0);
    // A subsequent input change cannot retroactively change a completed sample.
    analog=0x80;
    board.advance(XBoardTimeline::RESET_LINE0_NS);
    check(board.adc.read(board.now())==0xe0 && !timer.state().exck);
    board.advance(XBoardTimeline::RESET_LINE0_NS+64000);
    check(timer.state().exck);
    bool rejected=false;
    try { board.advance(0); } catch(const std::runtime_error&) { rejected=true; }
    check(rejected);

    // Large jumps and single-nanosecond stepping must emit identical edges;
    // callbacks can change analog input while a conversion is pending.
    CompareTimer5250 a,b;
    XBoardTimeline coarse(a,0),fine(b,0);
    std::vector<std::pair<uint64_t,unsigned>> ca,cb;
    uint8_t va=0x20,vb=0x20;
    coarse.adc.analog=[&] { return va; };fine.adc.analog=[&] { return vb; };
    coarse.scanline=[&](uint64_t t,unsigned l) { ca.emplace_back(t,l);va=0xe0; };
    fine.scanline=[&](uint64_t t,unsigned l) { cb.emplace_back(t,l);vb=0xe0; };
    coarse.adc.write(0);fine.adc.write(0);
    coarse.advance(128000);
    for(uint64_t t=0;t<=128000;++t) fine.advance(t);
    check(ca==cb && coarse.adc.value()==0xe0 && fine.adc.value()==0xe0);

    // Only the main timer receives EXCK. Both CPU VBLANK lines pulse for
    // one scanline; the main CPU's simultaneous timer/VBLANK level is six.
    auto state=timer.state();state.irq=true;timer.load(state);
    const uint64_t blank=XBoardTimeline::RESET_LINE0_NS+223*64000;
    board.advance(blank-1); check(!board.vblank() && board.main_irq_level()==2);
    board.advance(blank); check(board.vblank() && board.main_irq_level()==6 && board.sub_irq_level()==4);
    timer.write(9,0);check(board.main_irq_level()==4);
    board.advance_before(blank+64000);check(board.vblank() && board.main_irq_level()==4);
    board.advance(blank+64000);check(!board.vblank() && board.main_irq_level()==0 && board.sub_irq_level()==0);
    board.advance(blank+XBoardTimeline::FRAME_NS);check(board.vblank());
    // Internal clocks overrun the line-223 deadline before the next bus.
    // The bus sees old IPL; the following bus sees the delivered timer.
    CompareTimer5250 physical;
    XBoardTimeline delivery(physical,0);
    XBoardCpuVisibility cpu(delivery);
    const uint64_t edge=223*XBoardTimeline::LINE_NS;
    cpu.resume(edge-160);
    check(cpu.before_bus(edge+160)==0 && !delivery.vblank());
    cpu.complete_bus(edge+160);
    check(cpu.before_bus(edge+480)==4 && delivery.vblank());
    // A shift can select an edge delivered at its prefetch completion.
    // Forecasting must preserve live time/timer state and emit no callbacks.
    CompareTimer5250 forecast_timer;XBoardTimeline forecast(forecast_timer,0);
    auto fs=forecast_timer.state();fs.counter=0xfff;forecast_timer.load(fs);
    unsigned callbacks=0;forecast_timer.irq_line=[&](bool){++callbacks;};
    check(forecast.irq_at(63999)==0 && forecast.irq_at(64000)==2);
    check(forecast.now()==0 && forecast_timer.state().counter==0xfff && !forecast_timer.state().irq && callbacks==0);
    forecast.advance(64000);check(forecast.main_irq_level()==2 && callbacks==1);
    // VBLANK delivery likewise includes exactly the completed bus deadline.
    check(delivery.irq_at(edge+XBoardTimeline::LINE_NS)==0 && delivery.vblank());
    // A completed register shift yields after its internal tail even when
    // its final prefetch ended before the hardware deadline.
    CompareTimer5250 tail_timer;XBoardTimeline tail_board(tail_timer,0);
    XBoardCpuVisibility tail_cpu(tail_board);
    tail_cpu.resume(edge-160,0xee80); // ASR.L #7: 18 completion clocks
    check(tail_cpu.before_bus(edge+1280)==4);
    // A phase ending exactly at the clear deadline yields before the next
    // access at that timestamp; no arbitrary extra IRQ delay is added.
    cpu.resume(edge+XBoardTimeline::LINE_NS);
    check(cpu.before_bus(edge+XBoardTimeline::LINE_NS)==0);

    std::cout << "X-Board timeline regression checks passed\n";
}
