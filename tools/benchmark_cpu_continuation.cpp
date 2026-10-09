// ROM-free wall-time benchmark; do not use host timing as emulated CPU time.
#include "hwmath/xboard_cpu_continuation.hpp"
#include <chrono>
#include <iostream>
int main(){
    XBoardCpuContinuation cpu;
    constexpr unsigned checkpoints=100000;
    unsigned visits=0;
    const auto begin=std::chrono::steady_clock::now();
    cpu.start(0,[&]{
        for(unsigned i=0;i<checkpoints;++i){
            cpu.clock(i*320ULL);++visits;cpu.bus_completed(i*320ULL);
        }
        cpu.instruction_completed(checkpoints*320ULL);
    });
    const auto end=cpu.advance(checkpoints*320ULL);
    const auto seconds=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
    if(visits!=checkpoints||end!=checkpoints*320ULL||!cpu.done())return 1;
    std::cout<<checkpoints<<" checkpoints "<<seconds<<" seconds\n";
}
