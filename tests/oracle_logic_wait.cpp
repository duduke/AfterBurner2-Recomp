#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include "engine/omainirqservice.hpp"
#include "roms.hpp"
namespace {
uint64_t num(std::ifstream& f,unsigned n) {uint64_t v=0;while(n--){int c=f.get();if(c<0)throw std::runtime_error("truncated wait capture");v=v<<8|c;}return v;}
OMainObjects::Registers regs(std::ifstream& f) {OMainObjects::Registers r;for(auto& v:r.d)v=num(f,4);for(auto& v:r.a)v=num(f,4);return r;}
struct Event {uint64_t ns;uint32_t pc,a;uint16_t d,m;bool w;};
Event event(std::ifstream& f) {Event e;e.ns=num(f,8);e.pc=num(f,4);e.a=num(f,4);e.d=num(f,2);e.m=num(f,2);e.w=num(f,1);return e;}
}
int main(int argc,char** argv) try {
    std::ifstream f(argc>1?argv[1]:"build/oracle/logic_wait.bin",std::ios::binary);
    char magic[8];if(!f.read(magic,8)||memcmp(magic,"AB2WAIT1",8))throw std::runtime_error("invalid wait capture");
    if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
    MainBus::Memory memory;MainBus bus(roms.maincpu.rom,roms.maincpu.length,nullptr,0,memory);
    unsigned calls=0,identical=0,failed=0,events=0,differences=0,waiting=0,ready=0,missed=0,carry=0;
    while(f.peek()!=std::char_traits<char>::eof()) {
        OMainIrqService::Context c{};c.interrupted_pc=0x14856;c.boundary_ns=num(f,8);c.supervisor_sp=num(f,4);c.sr=num(f,2);
        const auto count=num(f,2),late=num(f,2);c.return_pc=num(f,4);c.registers=regs(f);
        bus.write16(0x2a3c00,count);bus.write16(0xffff8ba4,late);bus.write32(c.supervisor_sp,c.return_pc);
        const auto end=num(f,8),n=num(f,4);if(n>12)throw std::runtime_error("invalid wait events");
        std::vector<Event> expected;for(unsigned i=0;i<n;++i)expected.push_back(event(f));
        auto post=regs(f);const auto sp=num(f,4),sr=num(f,2),pc=num(f,4),postcount=num(f,2),postlate=num(f,2);
        if(int16_t(count)>=0)++waiting;else{++ready;if(count!=0xffff){++missed;if(late==0xffff)++carry;}}
        bool ok=true;size_t cursor=0;uint64_t now=c.boundary_ns;OMainIrqService service;
        MainBus::Trace trace{nullptr,nullptr,[&](bool w,uint32_t a,uint16_t d,uint16_t m) {
            ++events;bool equal=cursor<expected.size();
            if(equal){const auto& e=expected[cursor++];equal=e.ns==now&&e.pc==service.current_pc()&&e.a==a&&e.m==m&&(e.d&m)==(d&m)&&e.w==w;
                if(!equal&&differences<8)std::cerr<<std::hex<<"Wait pc="<<service.current_pc()<<'/'<<e.pc<<" a="<<a<<'/'<<e.a<<std::dec<<" time="<<now<<'/'<<e.ns<<'\n';}
            if(!equal){++differences;ok=false;}
        }};
        bus.trace=&trace;auto result=service.logic_step(bus,c,[&](uint64_t ns){now=ns;});bus.trace=nullptr;
        ok &= cursor==expected.size()&&result.ns==end&&result.sp==sp&&result.sr==sr&&result.pc==pc&&result.registers.d==post.d&&result.registers.a==post.a&&bus.read16(0x2a3c00)==postcount&&bus.read16(0xffff8ba4)==postlate;
        ++calls;if(ok)++identical;else{++failed;if(failed<6)std::cerr<<"Wait call "<<calls<<" end="<<result.ns<<'/'<<end<<" sr="<<result.sr<<'/'<<sr<<'\n';}
    }
    std::cout<<"Logic wait timing: "<<identical<<" of "<<calls<<" identical, "<<failed<<" different, 0 skipped (uninterrupted records)\nLogic wait bus events: "<<events-differences<<" of "<<events<<" identical, "<<differences<<" different, 0 skipped\nCoverage: waiting="<<waiting<<" ready="<<ready<<" missed="<<missed<<" carry="<<carry<<'\n';
    return calls&&!failed&&!differences?0:1;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
