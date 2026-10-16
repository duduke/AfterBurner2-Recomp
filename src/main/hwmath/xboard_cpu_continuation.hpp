#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <memory>

// On Apple Silicon the cooperative CPUs share the host thread. Save the ABI's
// callee-saved registers on separate stacks rather than waking OS workers at
// every instruction. Windows uses OS fibers for the same cooperative handoff.
// The worker backend remains available for differential validation.
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(_WIN32) && !defined(AB2_FORCE_THREAD_CONTINUATION)
#define AB2_WINDOWS_FIBER 1
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#if defined(__APPLE__) && defined(__aarch64__) && !defined(AB2_FORCE_THREAD_CONTINUATION) && !defined(__SANITIZE_ADDRESS__) && !__has_feature(address_sanitizer) && !__has_feature(thread_sanitizer) && !defined(__arm64e__)
#define AB2_STACK_CONTINUATION 1
extern "C" void ab2_context_swap(void**,void*);
extern "C" void ab2_context_enter();
asm(
".text\n.p2align 2\n.weak_definition _ab2_context_swap\n_ab2_context_swap:\n"
"sub sp,sp,#176\n"
"stp x19,x20,[sp,#0]\nstp x21,x22,[sp,#16]\nstp x23,x24,[sp,#32]\n"
"stp x25,x26,[sp,#48]\nstp x27,x28,[sp,#64]\nstp x29,x30,[sp,#80]\n"
"stp d8,d9,[sp,#96]\nstp d10,d11,[sp,#112]\nstp d12,d13,[sp,#128]\nstp d14,d15,[sp,#144]\n"
"mrs x9,fpcr\nmrs x10,fpsr\nstp x9,x10,[sp,#160]\n"
"mov x9,sp\nstr x9,[x0]\nmov sp,x1\n"
"ldp x9,x10,[sp,#160]\nmsr fpcr,x9\nmsr fpsr,x10\n"
"ldp d8,d9,[sp,#96]\nldp d10,d11,[sp,#112]\nldp d12,d13,[sp,#128]\nldp d14,d15,[sp,#144]\n"
"ldp x19,x20,[sp,#0]\nldp x21,x22,[sp,#16]\nldp x23,x24,[sp,#32]\n"
"ldp x25,x26,[sp,#48]\nldp x27,x28,[sp,#64]\nldp x29,x30,[sp,#80]\n"
"add sp,sp,#176\nret\n"
".p2align 2\n.weak_definition _ab2_context_enter\n_ab2_context_enter:\n"
"mov x0,x19\nblr x20\nbrk #0\n");
#endif

#if AB2_STACK_CONTINUATION || AB2_WINDOWS_FIBER
#define AB2_COOPERATIVE_CONTINUATION 1
#endif

// Cooperative adapter for the literal 68000 ports. Only the granted CPU runs;
// the other continuation is parked before its next clock/bus checkpoint. A host
// stack preserves a port's local variables across a grant (notably RMW data).
// m68000_device::execute_run retains overrun as a private instruction-step
// debt and reports only the granted clocks. Internal clocks before a bus do
// not yield separately. No transactions are sorted by local time.
class XBoardCpuContinuation {
public:
    static constexpr uint64_t CYCLE_NS=80, BUS_NS=4*CYCLE_NS;
    static constexpr const char* backend_name(){
#if AB2_WINDOWS_FIBER
        return "windows-fiber";
#elif AB2_STACK_CONTINUATION
        return "apple-arm64-stack";
#else
        return "worker-thread";
#endif
    }
#if AB2_STACK_CONTINUATION
    XBoardCpuContinuation():stack(new uint64_t[STACK_WORDS]{}){
        auto top=reinterpret_cast<uintptr_t>(stack.get()+STACK_WORDS)&~uintptr_t(15);
        auto* saved=reinterpret_cast<uint64_t*>(top-176);
        saved[0]=reinterpret_cast<uintptr_t>(this);
        saved[1]=reinterpret_cast<uintptr_t>(&enter);
        saved[11]=reinterpret_cast<uintptr_t>(&ab2_context_enter);
        asm volatile("mrs %0,fpcr" : "=r"(saved[20]));
        asm volatile("mrs %0,fpsr" : "=r"(saved[21]));
        fiber_stack=saved;
    }
#elif AB2_WINDOWS_FIBER
    XBoardCpuContinuation():host_fiber(acquire_host_fiber()){
        fiber=CreateFiberEx(0,1024*1024,FIBER_FLAG_FLOAT_SWITCH,&enter,this);
        if(!fiber)throw std::runtime_error("CreateFiberEx failed: "+std::to_string(GetLastError()));
    }
#else
    XBoardCpuContinuation():worker([this]{work();}){}
#endif
    XBoardCpuContinuation(const XBoardCpuContinuation&)=delete;
    ~XBoardCpuContinuation(){
        stopping=true;publish(Stopping);
#if AB2_COOPERATIVE_CONTINUATION
        if(fiber_started)resume();
#if AB2_WINDOWS_FIBER
        DeleteFiber(fiber);
#endif
#else
        worker.join();
#endif
    }
    void start(uint64_t ns,std::function<void()> instruction,bool eager=true){
        if(job||!finished||local<bus_end)throw std::runtime_error("CPU continuation still active");
        local=bus_end=ns;grant_active=false;finished=false;parked=false;job=std::move(instruction);
        scheduled=!eager;
        if(eager){publish(Running);wait_change(Running);rethrow();}
    }
    // Called by the port at every existing clock checkpoint, before accessing
    // shared state. Repeated selection/prefetch checkpoints at one time are OK.
    void clock(uint64_t ns){
        if(ns<bus_end)throw std::runtime_error("CPU clock precedes completed bus");
        // The host cannot run another owner until this grant returns. Keep
        // executing its indivisible microsteps without an OS-thread handoff
        // while the same debt/abort rules still permit them.
        if(grant_active&&bus_end<grant_target&&!(aborted&&bus_end>abort_step_start))return;
        next=ns;parked=true;publish(Parked);
        wait_change(Parked);
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
        rethrow();
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
            grant_target=target;grant_active=true;
            scheduled=false;
            parked=false;publish(Running);wait_change(Running);grant_active=false;rethrow();
        }
        if(aborted){local=std::min(target,abort_ns);aborted=false;return local;}
        local=std::min(target,std::max(local,bus_end));return local;
    }
    // Called by an executing port's hardware callback, before its next clock
    // checkpoint; mirrors device_execute_interface::abort_timeslice.
    void abort_grant(uint64_t ns){
        if(finished||ns<local)throw std::runtime_error("invalid CPU grant abort");
        if(!aborted){abort_ns=ns;abort_step_start=bus_end;}
        else if(ns<abort_ns)abort_ns=ns;
        aborted=true;
    }
    // RESET replaces the instruction state and clears private step debt
    // (m68000_device::device_reset). Invoke only between CPU grants.
    void reset(uint64_t ns){
        rethrow();
        if(scheduled){job={};scheduled=false;finished=true;}
        if(!finished&&!parked)throw std::runtime_error("reset of executing CPU");
        if(!finished){
            cancelling=true;parked=false;publish(Running);
            wait_change(Running);rethrow();
        }
        cancelling=false;aborted=false;local=bus_end=next=ns;
    }
    void instruction_completed(uint64_t ns){bus_end=ns;}

    bool done()const{return finished;}
    uint64_t time()const{return local;}
private:
    struct Stop{};
    // A single host and one worker exchange exclusive ownership. Release/
    // acquire publishes the port's plain state and shared bus effects at each
    // checkpoint. Brief hot handoffs spin on spare CPU cores; long waits park.
    enum Turn : unsigned {Finished,Running,Parked,Stopping};
    std::atomic<unsigned> turn{Finished};
    std::atomic<unsigned> sleepers{0};
    void publish(Turn value){
        // The sequentially consistent registration/recheck below closes the
        // sleep race. Hot handoffs need no kernel wakeup when nobody slept.
        turn.store(value,std::memory_order_seq_cst);
        if(sleepers.load(std::memory_order_seq_cst))turn.notify_one();
    }
    void wait_change(Turn value){
#if AB2_STACK_CONTINUATION
        while(turn.load(std::memory_order_acquire)==value){
            if(in_fiber)ab2_context_swap(&fiber_stack,host_stack);
            else resume();
        }
#elif AB2_WINDOWS_FIBER
        while(turn.load(std::memory_order_acquire)==value){
            if(in_fiber)SwitchToFiber(host_fiber->pointer);
            else resume();
        }
#else
        for(unsigned n=0;n<2048;++n){
            if(turn.load(std::memory_order_acquire)!=value)return;
#if defined(__aarch64__)
            asm volatile("" ::: "memory");
#elif defined(__x86_64__) || defined(__i386__)
            asm volatile("pause");
#else
            if(!(n&63))std::this_thread::yield();
#endif
        }
        const unsigned bit=1u<<value;
        sleepers.fetch_or(bit,std::memory_order_seq_cst);
        while(turn.load(std::memory_order_seq_cst)==value)turn.wait(value,std::memory_order_acquire);
        sleepers.fetch_and(~bit,std::memory_order_seq_cst);
#endif
    }
    std::function<void()> job;std::exception_ptr error;
    bool parked=false,finished=true,stopping=false,cancelling=false,aborted=false,grant_active=false,scheduled=false;
    uint64_t local{},bus_end{},next{},abort_ns{},abort_step_start{},grant_target{};
#if AB2_STACK_CONTINUATION
    static constexpr size_t STACK_WORDS=128*1024; // 1 MiB, separate from the host
    std::unique_ptr<uint64_t[]> stack;
    void *fiber_stack{},*host_stack{};
    bool in_fiber=false,fiber_started=false;
    void resume(){
        fiber_started=true;in_fiber=true;
        ab2_context_swap(&host_stack,fiber_stack);
        in_fiber=false;
    }
    static void enter(XBoardCpuContinuation* self){
        self->work();
        // Return to the destructor only after the instruction's C++ locals
        // have unwound. This completed stack is never resumed again.
        ab2_context_swap(&self->fiber_stack,self->host_stack);
        std::terminate();
    }
#elif AB2_WINDOWS_FIBER
    // All continuations on this host thread share its conversion. Revert only
    // our own conversion after the last continuation dies, in any order.
    // A caller that already runs as a fiber retains ownership of that fiber.
    struct HostFiber {
        void* pointer{};
        bool converted=false;
        HostFiber(){
            if(IsThreadAFiber())pointer=GetCurrentFiber();
            else {
                pointer=ConvertThreadToFiberEx(nullptr,FIBER_FLAG_FLOAT_SWITCH);
                if(!pointer)throw std::runtime_error("ConvertThreadToFiberEx failed: "+std::to_string(GetLastError()));
                converted=true;
            }
        }
        ~HostFiber(){if(converted)ConvertFiberToThread();}
    };
    static std::shared_ptr<HostFiber> acquire_host_fiber(){
        static thread_local std::weak_ptr<HostFiber> current;
        auto host=current.lock();
        if(!host){host=std::make_shared<HostFiber>();current=host;}
        return host;
    }
    std::shared_ptr<HostFiber> host_fiber;
    void* fiber{};
    bool in_fiber=false,fiber_started=false;
    void resume(){
        fiber_started=true;in_fiber=true;
        SwitchToFiber(fiber);
        in_fiber=false;
    }
    static void WINAPI enter(void* pointer){
        auto* self=static_cast<XBoardCpuContinuation*>(pointer);
        self->work();
        // Unwind the instruction before the host deletes this fiber stack.
        SwitchToFiber(self->host_fiber->pointer);
        std::terminate();
    }
#else
    std::thread worker; // start only after all synchronization/state fields exist
#endif
    void rethrow(){if(error)std::rethrow_exception(error);}
    void work(){
        while(true){
            wait_change(Finished);if(stopping)return;
            auto run=std::move(job);job={};
            try{run();}catch(const Stop&){}catch(...){error=std::current_exception();}
            finished=true;parked=false;if(stopping)return;publish(Finished);
        }
    }
};
