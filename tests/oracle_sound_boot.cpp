#include "hwaudio/xboard_sound_cpu.hpp"
#include "hwaudio/xboard_pcm_startup.hpp"
#include "hwmath/compare_timer.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <vector>
#include <cstring>

struct Record { unsigned kind; uint64_t ns; uint16_t pc,address; uint8_t data; };
static uint64_t number(std::ifstream& in,unsigned n) {
    uint64_t v=0;while(n--) {int c=in.get();if(c==EOF)throw std::runtime_error("truncated sound boot capture");v=(v<<8)|c;}return v;
}
struct Boundary {};
int main(int argc,char** argv) try {
    std::ifstream rom("roms/epr-11112.17",std::ios::binary);
    std::vector<uint8_t> program{std::istreambuf_iterator<char>(rom),{}};
    XBoardSoundCpu cpu(program.data(),program.size());
    std::ifstream in(argc>1?argv[1]:"build/oracle/sound_boot.bin",std::ios::binary);
    char magic[8];if(!in.read(magic,8)||std::memcmp(magic,"AB2ZBT03",8))throw std::runtime_error("invalid sound boot capture");
    if(!in.read(reinterpret_cast<char*>(cpu.ram.data()),cpu.ram.size()))throw std::runtime_error("missing initial sound RAM");
    std::vector<Record> records;
    bool bounded=false;
    while(in.peek()!=EOF) {
        Record r{};r.kind=number(in,1);
        if(r.kind==6) {bounded=true;if(in.peek()!=EOF)throw std::runtime_error("data after sound boundary");break;}
        r.ns=number(in,8);r.pc=number(in,2);r.address=number(in,2);r.data=number(in,1);
        if(r.kind>3||(!records.empty()&&r.ns<records.back().ns))throw std::runtime_error("bad sound boot record");
        records.push_back(r);
    }
    if(records.size()<2||records.front().kind!=0||records.front().address!=0)throw std::runtime_error("missing reset entry");
    const auto terminal=records.back();
    const bool active_pcm=terminal.kind==1&&terminal.address>=0xf000&&terminal.address<0xf800&&
        (terminal.address&0x87)==0x86&&!(terminal.data&1);
    if(!bounded&&terminal.kind!=2&&!active_pcm)throw std::runtime_error("missing device boundary");
    const size_t expected=records.size()-(bounded?0:1);
    size_t cursor=0;unsigned writes=0,latches=0,pcm_reads=0,pcm_writes=0,timing_matches=0;
    uint64_t origin_clock=0,max_timing_delta=0;
    CompareTimer5250 latch;
    cpu.latch_read=[&](){++latches;return latch.sound_read();};
    XBoardPcmStartup pcm;
    cpu.pcm_read=[&](uint8_t offset){++pcm_reads;return pcm.read(offset);};
    cpu.pcm_write=[&](uint8_t offset,uint8_t data){
        if((offset&0x87)==0x86&&!(data&1)) {
            if(bounded||cursor+1!=records.size()||!active_pcm||offset!=(terminal.address&255)||data!=terminal.data)
                throw std::runtime_error("unexpected active PCM boundary");
            throw Boundary{};
        }
        ++pcm_writes;pcm.write(offset,data);
    };
    cpu.ym_write=[](uint8_t,uint8_t){};
    auto stop=[&](unsigned kind,unsigned address) -> uint8_t {
        if(cursor+1!=records.size()||terminal.kind!=kind||terminal.address!=address)
            throw std::runtime_error("unexpected sound device read boundary");
        throw Boundary{}; // no captured device value enters the CPU
    };
    cpu.ym_read=[&](uint8_t offset){return stop(2,offset);};
    cpu.trace=[&](const XBoardSoundCpu::Event& e) {
        if(cursor>=expected)throw std::runtime_error("sound boot passed device boundary");
        const auto& r=records[cursor];
        if(static_cast<unsigned>(e.kind)!=r.kind||e.address!=r.address||e.data!=r.data||e.pc!=r.pc) {
            std::cerr<<"sound boot transaction "<<cursor<<" actual pc/address/data="<<std::hex<<e.pc<<'/'<<e.address<<'/'<<unsigned(e.data)
                     <<" expected="<<r.pc<<'/'<<r.address<<'/'<<unsigned(r.data)<<std::dec<<'\n';
            throw std::runtime_error("native sound boot differs");
        }
        if(r.kind==1||r.kind==3)++writes;
        // Diagnostic only: the current pin decoder's access phase placement
        // is not assumed to equal MAME's. Never drive clocks from these times.
        if(cursor==0)origin_clock=e.clock;
        const auto native_ns=(e.clock-origin_clock)*250;
        const auto expected_ns=r.ns-records.front().ns;
        const auto delta=native_ns>expected_ns?native_ns-expected_ns:expected_ns-native_ns;
        if(!delta)++timing_matches;
        if(delta>max_timing_delta)max_timing_delta=delta;
        ++cursor;
        if(bounded&&cursor==expected)throw Boundary{};
    };
    bool ended=false;
    try {while(cpu.clocks()<1000000)cpu.tick();}catch(const Boundary&){ended=true;}
    if(!ended||cursor!=expected||!latches||!pcm_reads||!pcm_writes)throw std::runtime_error("incomplete sound boot coverage");
    std::cout<<"Native sound boot vs arcade: "<<cursor<<'/'<<expected<<" transactions identical, 0 different, 0 skipped; "
             <<writes<<" writes, "<<latches<<" computed latch reads, "<<pcm_reads<<" computed PCM reads, "<<pcm_writes<<" PCM writes, "<<cpu.clocks()<<" native clocks; command/reset/device continuation and scheduler timestamps unverified\n";
    std::cout<<"Sound boot timing diagnostic (not acceptance): "<<timing_matches<<'/'<<expected
             <<" relative phase times equal, maximum delta "<<max_timing_delta<<" ns\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
