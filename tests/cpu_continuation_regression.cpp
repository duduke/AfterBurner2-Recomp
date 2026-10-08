#include <iostream>
#include <stdexcept>
#include "hwmath/xboard_cpu_continuation.hpp"
namespace {void check(bool value){if(!value)throw std::runtime_error("CPU continuation regression");}}
int main()try{
    XBoardCpuContinuation cpu;
    uint16_t shared=7,latched=0;unsigned reads=0,writes=0;
    cpu.start(0,[&]{
        cpu.clock(160);latched=shared;++reads;cpu.bus_completed(160);
        cpu.clock(640);shared=latched+1;++writes;cpu.bus_completed(640);
        cpu.clock(960);cpu.instruction_completed(960);
    });
    check(cpu.advance(80)==80&&reads==1&&writes==0); // internal clocks before bus cannot yield
    shared=100; // other CPU changes RAM while this RMW instruction is parked
    check(cpu.advance(240)==240&&reads==1&&writes==0); // private overrun debt
    check(cpu.advance(480)==480&&writes==0);
    check(cpu.advance(560)==560&&shared==8&&writes==1);
    check(cpu.advance(720)==720&&writes==1); // debt paid without repeating write
    check(cpu.advance(1040)==960&&cpu.done());
    cpu.start(960,[&]{cpu.clock(960);cpu.clock(1120);cpu.instruction_completed(1120);});
    check(cpu.advance(1040)==1040&&cpu.done());
    bool unpaid=false;try{cpu.start(1120,[]{});}catch(const std::runtime_error&){unpaid=true;}check(unpaid);
    check(cpu.advance(1200)==1120&&cpu.done());
    {XBoardCpuContinuation stopped;stopped.start(0,[&]{stopped.clock(80);throw std::runtime_error("must not reach cancelled bus");});}
    {XBoardCpuContinuation failed;failed.start(0,[&]{failed.clock(0);throw std::runtime_error("port failure");});
     bool caught=false;try{failed.advance(80);}catch(const std::runtime_error& e){caught=std::string(e.what())=="port failure";}check(caught);}
    std::cout<<"CPU continuation: indivisible internal clocks, private bus/tail debt, retained RMW, reuse, cancellation and error propagation passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
