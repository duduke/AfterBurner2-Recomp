#include <iostream>
#include <chrono>
#include <thread>
#include <stdexcept>
#include "hwmath/xboard_cpu_continuation.hpp"
#include "engine/osubprogress_manifest.hpp"
#include <cfenv>
namespace {void check(bool value){if(!value)throw std::runtime_error("CPU continuation regression");}}
int main()try{
    // The live host defers an instruction until its first grant. It must not
    // touch the bus when merely prepared, or after RESET cancels that job.
    {XBoardCpuContinuation deferred;unsigned visits=0;
     auto instruction=[&]{deferred.clock(0);++visits;deferred.bus_completed(0);
         deferred.clock(320);++visits;deferred.bus_completed(320);
         deferred.clock(640);deferred.instruction_completed(640);};
     deferred.start(0,instruction,false);check(visits==0);
     deferred.reset(0);check(deferred.done()&&visits==0);
     deferred.start(0,instruction,false);check(visits==0);
     check(deferred.advance(80)==80&&visits==1&&!deferred.done());
     check(deferred.advance(720)==640&&visits==2&&deferred.done());}
    // Lookup must retain exactly the generated whitelist, including odd PCs
    // and gaps. Compare every address with a single monotonic manifest cursor.
    size_t cursor=0;
    for(uint32_t pc=0;pc<=sub_instructions.back().pc+2;++pc){
        while(cursor<sub_instructions.size()&&sub_instructions[cursor].pc<pc)++cursor;
        const auto* expected=cursor<sub_instructions.size()&&sub_instructions[cursor].pc==pc?&sub_instructions[cursor]:nullptr;
        check(sub_instruction(pc)==expected);
    }
    // Independent stacks must preserve live locals and floating-point control
    // state across interleaved grants, RESET unwinding and destruction.
    {XBoardCpuContinuation first,second;int unwound=0;
     struct Guard{int& count;~Guard(){++count;}};
     auto operation=[&](XBoardCpuContinuation& c,int mode){
         c.clock(0);Guard guard{unwound};std::fesetround(mode);
         volatile double value=1.25;
         c.bus_completed(0);c.clock(320);
         check(std::fegetround()==mode&&value==1.25);
         c.bus_completed(320);c.clock(640);c.instruction_completed(640);
     };
     const auto rounding=std::fegetround();
     first.start(0,[&]{operation(first,FE_DOWNWARD);});
     second.start(0,[&]{operation(second,FE_UPWARD);});
     check(first.advance(80)==80&&second.advance(80)==80);
     check(std::fegetround()==rounding);
     check(first.advance(400)==400&&second.advance(400)==400);
     first.reset(400);second.reset(400);check(unwound==2);
     check(std::fegetround()==rounding);
    }
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
    // VPA before_time waits consume grants without executing the acknowledge.
    {XBoardCpuContinuation waiting;unsigned acks=0;
     waiting.start(0,[&]{waiting.clock(0);waiting.bus_completed(0);
         waiting.hold_until(1120);++acks;waiting.bus_completed(1120,400);
         waiting.clock(1840);waiting.instruction_completed(1840);});
     check(waiting.advance(320)==320&&acks==0);
     check(waiting.advance(800)==800&&acks==0);
     check(waiting.advance(1120)==1120&&acks==0);
     check(waiting.advance(1200)==1200&&acks==1);
     check(waiting.advance(1520)==1520&&acks==1);
     check(waiting.advance(1920)==1840&&waiting.done());}
    {XBoardCpuContinuation synchronized;unsigned phases=0;
     synchronized.start(0,[&]{synchronized.clock(160);++phases;
         synchronized.abort_grant(160);synchronized.bus_completed(160);
         synchronized.clock(480);++phases;synchronized.bus_completed(480);
         synchronized.clock(800);synchronized.instruction_completed(800);});
     check(synchronized.advance(960)==160&&phases==1&&!synchronized.done());
     check(synchronized.advance(400)==400&&phases==1);
     check(synchronized.advance(480)==480&&phases==1);
     check(synchronized.advance(560)==560&&phases==2);
     synchronized.reset(560);check(synchronized.done()&&synchronized.time()==560);
     synchronized.start(560,[&]{synchronized.clock(560);++phases;synchronized.bus_completed(560);
         synchronized.clock(880);synchronized.instruction_completed(880);});
     check(synchronized.advance(960)==880&&phases==3&&synchronized.done());}
    // RESET callbacks run inside an uninterrupted internal microcode loop.
    // A grant abort there still allows its following bus step to finish.
    {XBoardCpuContinuation internal;unsigned prefetches=0;
     internal.start(0,[&]{internal.abort_grant(320);internal.abort_grant(10240);
         internal.clock(10240);++prefetches;internal.bus_completed(10240);
         internal.clock(10560);internal.instruction_completed(10560);});
     check(internal.advance(640)==320&&prefetches==1&&!internal.done());
     check(internal.advance(10000)==10000&&prefetches==1);
     check(internal.advance(10640)==10560&&internal.done());}
    {XBoardCpuContinuation stopped;stopped.start(0,[&]{stopped.clock(80);throw std::runtime_error("must not reach cancelled bus");});}
    {XBoardCpuContinuation failed;failed.start(0,[&]{failed.clock(0);throw std::runtime_error("port failure");});
     bool caught=false;try{failed.advance(80);}catch(const std::runtime_error& e){caught=std::string(e.what())=="port failure";}check(caught);}
    // Idle RESET must not wake an empty job. Exercise actual sleep fallback
    // as well as rapid repeated release/acquire publication of plain bus data.
    {XBoardCpuContinuation idle;idle.reset(800);check(idle.done()&&idle.time()==800);
     unsigned published=0;
     for(unsigned i=0;i<2000;++i){const auto base=800+640ULL*i;
         idle.start(base,[&,base,i]{idle.clock(base);check(published==i);
             published=i+1;idle.bus_completed(base);idle.clock(base+320);
             idle.instruction_completed(base+640);});
         check(idle.advance(base+640)==base+640&&idle.done()&&published==i+1);
     }
     idle.reset(0);check(idle.done()&&idle.time()==0);
     idle.start(0,[&]{std::this_thread::sleep_for(std::chrono::milliseconds(5));
         idle.clock(0);published=42;idle.bus_completed(0);idle.instruction_completed(320);});
     std::this_thread::sleep_for(std::chrono::milliseconds(5));
     check(idle.advance(320)==320&&idle.done()&&published==42);}
    std::cout<<"CPU continuation: indivisible internal clocks, private bus/tail debt, retained RMW, reuse, cancellation and error propagation passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
