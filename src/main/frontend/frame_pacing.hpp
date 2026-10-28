#pragma once
#include <algorithm>

namespace Frontend {
// Host presentation only: no emulated clocks, frame skips or input batches.
// Retain ordinary driver stalls long enough to repay their elapsed time;
// still bound catch-up after suspension or a debugger stop.
class FramePacing {
public:
    explicit FramePacing(double period):period(period){}
    void reset(){accumulated=0;}
    void elapsed(double seconds){accumulated=std::min(period*16.0,accumulated+seconds);}
    double delay(double since_last_counter=0)const{return std::max(0.0,period-accumulated-since_last_counter);}
    void waited(double seconds){accumulated+=seconds;}
    void presented(){accumulated-=period;}
private:
    double period,accumulated{};
};
}
