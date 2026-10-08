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
        if(stopping)throw Stop{};
    }
    // Called after each actual bus access, before the next clock checkpoint.
    void bus_completed(uint64_t ns){bus_end=ns+BUS_NS;}
    uint64_t advance(uint64_t target){
        std::unique_lock lock(mutex);rethrow();
        if(target<local)throw std::runtime_error("CPU grant behind local clock");
        while(!finished){
            // execute_run pays m_count_before_instruction_step first. The
            // observable CPU local clock stops at target, while the effective
            // microcode clock can already be beyond it.
            if(bus_end>=target){local=target;return local;}
            parked=false;cv.notify_all();cv.wait(lock,[&]{return parked||finished;});rethrow();
        }
        local=std::min(target,std::max(local,bus_end));return local;
    }
    void instruction_completed(uint64_t ns){bus_end=ns;}

    bool done()const{return finished;}
    uint64_t time()const{return local;}
private:
    struct Stop{};
    std::mutex mutex;std::condition_variable cv;
    std::function<void()> job;std::exception_ptr error;
    bool parked=false,finished=true,stopping=false;
    uint64_t local{},bus_end{},next{};
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
