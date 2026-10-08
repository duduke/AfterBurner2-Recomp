#pragma once
#include <algorithm>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>

// Cooperative adapter for the literal 68000 ports. Only the granted CPU runs;
// the other worker is parked before its next clock/bus checkpoint. A host
// thread preserves a port's local variables across a grant (notably RMW data).
// m68000_device::execute_run retains overrun as a private instruction-step
// debt and reports only the granted clocks. Internal clocks before a bus do
// not yield separately. No transactions are sorted by local time.
class XBoardCpuContinuation {
public:
    static constexpr uint64_t CYCLE_NS=80, BUS_NS=4*CYCLE_NS;
    XBoardCpuContinuation():worker([this]{work();}){}
    XBoardCpuContinuation(const XBoardCpuContinuation&)=delete;
    ~XBoardCpuContinuation(){
        {std::lock_guard lock(mutex);stopping=true;cv.notify_all();}
        worker.join();
    }
    void start(uint64_t ns,std::function<void()> instruction){
        std::unique_lock lock(mutex);
        if(job||!finished||local<bus_end)throw std::runtime_error("CPU continuation still active");
        local=bus_end=ns;finished=false;parked=false;job=std::move(instruction);
        cv.notify_all();cv.wait(lock,[&]{return parked||finished;});rethrow();
    }
    // Called by the port at every existing clock checkpoint, before accessing
    // shared state. Repeated selection/prefetch checkpoints at one time are OK.
    void clock(uint64_t ns){
        std::unique_lock lock(mutex);
        if(ns<bus_end)throw std::runtime_error("CPU clock precedes completed bus");
        next=ns;parked=true;cv.notify_all();
        cv.wait(lock,[&]{return !parked||stopping;});
        if(stopping||cancelling)throw Stop{};
    }
    // Called after each actual bus access, before the next clock checkpoint.
    void bus_completed(uint64_t ns,uint64_t duration=BUS_NS){bus_end=ns+duration;}
    // Interruptible memory before_time (68000 VPA) is a real wait frontier,
    // unlike ordinary internal microcode clocks before a program bus.
    void hold_until(uint64_t ns){
        if(ns<bus_end)throw std::runtime_error("CPU wait precedes completed bus");
        bus_end=ns;clock(ns);
    }
    uint64_t advance(uint64_t target){
        std::unique_lock lock(mutex);rethrow();
        if(target<local)throw std::runtime_error("CPU grant behind local clock");
        while(!finished){
            // A synchronized device request steals the remaining public
            // grant at the callback clock. The current microcode step has
            // already completed and its bus/internal overrun remains owed.
            if(aborted&&bus_end>abort_step_start){local=std::min(target,abort_ns);aborted=false;return local;}
            // execute_run pays m_count_before_instruction_step first. The
            // observable CPU local clock stops at target, while the effective
            // microcode clock can already be beyond it.
            if(bus_end>=target){local=target;return local;}
            parked=false;cv.notify_all();cv.wait(lock,[&]{return parked||finished;});rethrow();
        }
        if(aborted){local=std::min(target,abort_ns);aborted=false;return local;}
        local=std::min(target,std::max(local,bus_end));return local;
    }
    // Called by an executing port's hardware callback, before its next clock
    // checkpoint; mirrors device_execute_interface::abort_timeslice.
    void abort_grant(uint64_t ns){
        std::lock_guard lock(mutex);
        if(finished||ns<local)throw std::runtime_error("invalid CPU grant abort");
        if(!aborted){abort_ns=ns;abort_step_start=bus_end;}
        else if(ns<abort_ns)abort_ns=ns;
        aborted=true;
    }
    // RESET replaces the instruction state and clears private step debt
    // (m68000_device::device_reset). Invoke only between CPU grants.
    void reset(uint64_t ns){
        std::unique_lock lock(mutex);rethrow();
        if(!finished&&!parked)throw std::runtime_error("reset of executing CPU");
        cancelling=true;parked=false;cv.notify_all();
        cv.wait(lock,[&]{return finished;});rethrow();
        cancelling=false;aborted=false;local=bus_end=next=ns;
    }
    void instruction_completed(uint64_t ns){bus_end=ns;}

    bool done()const{return finished;}
    uint64_t time()const{return local;}
private:
    struct Stop{};
    std::mutex mutex;std::condition_variable cv;
    std::function<void()> job;std::exception_ptr error;
    bool parked=false,finished=true,stopping=false,cancelling=false,aborted=false;
    uint64_t local{},bus_end{},next{},abort_ns{},abort_step_start{};
    std::thread worker; // start only after all synchronization/state fields exist
    void rethrow(){if(error)std::rethrow_exception(error);}
    void work(){
        std::unique_lock lock(mutex);
        while(true){
            cv.wait(lock,[&]{return job||stopping;});if(stopping)return;
            auto run=std::move(job);job={};lock.unlock();
            try{run();}catch(const Stop&){}catch(...){error=std::current_exception();}
            lock.lock();finished=true;parked=false;cv.notify_all();if(stopping)return;
        }
    }
};
