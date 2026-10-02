// Compose the live dispatcher and verified handlers without replaying their
// outputs. Unknown caller transcripts remain explicit external caller inputs.
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>
#include <vector>
#include "roms.hpp"
#include "engine/omainhost.hpp"

namespace {
using Registers = OMainHost::Registers;
using Event = std::tuple<uint32_t,uint16_t,uint16_t>;
using Math = std::array<uint16_t,32>;
void read(std::ifstream& in,void* data,size_t n) {
    if(!in.read(static_cast<char*>(data),n)) throw std::runtime_error("truncated host capture");
}
uint32_t u32(std::ifstream& in) {
    uint8_t b[4]; read(in,b,4);
    return (uint32_t(b[0])<<24)|(uint32_t(b[1])<<16)|(b[2]<<8)|b[3];
}
Registers registers(std::ifstream& in) {
    Registers r; for(auto& v:r.d) v=u32(in); for(auto& v:r.a) v=u32(in); return r;
}
bool equal(const Registers& a,const Registers& b) { return a.d==b.d && a.a==b.a; }
Math math(std::ifstream& in) { Math m;for(auto& v:m) v=u32(in);return m; }
void load_math(MainBus& b,const Math& m,bool main=true) {
    if(main) b.mul.load(m[0],m[1]);
    b.sub.mul.load(m[16],m[17]);
    for(unsigned base: {8u,24u}) {
        uint16_t v[]={m[base],m[base+1],m[base+2],m[base+4],m[base+5],m[base+6]};
        if(base==8) { if(main) b.div.load(v); } else b.sub.div.load(v);
    }
}
bool equal_math(MainBus& b,const Math& m) {
    for(unsigned i=0;i<8;++i) if(b.mul.read(i)!=m[i] || b.div.read(i)!=m[8+i]) return false;
    return true;
}
void offsets(std::ifstream& in,std::vector<uint8_t>& marks) {
    std::fill(marks.begin(),marks.end(),0);auto n=u32(in);
    if(n>marks.size()) throw std::runtime_error("invalid write set");
    while(n--) marks.at(u32(in))=1;
}
std::vector<Event> events(std::ifstream& in) {
    auto n=u32(in);if(n>1000000) throw std::runtime_error("invalid event count");
    std::vector<Event> result;
    while(n--) { auto a=u32(in),d=u32(in),m=u32(in);result.emplace_back(a,d&m,m); }
    return result;
}
void mark(std::vector<uint8_t>& marks,const Event& e) {
    auto [a,d,m]=e;(void)d;auto o=MainBus::memory_offset(a);
    if(o==MainBus::UNMAPPED) return;
    if(m&0xff00) marks.at(o)=1;if(m&0xff) marks.at(o+1)=1;
}
void apply(MainBus& b,const Event& e) {
    auto [a,d,m]=e;
    if(m==0xffff) b.write16(a,d);
    else { if(m&0xff00) b.write8(a,d>>8);if(m&0xff) b.write8(a+1,d); }
}
// Explicit unported caller allowlist observed in these directed windows.
// A verified handler may never be downgraded to transcript replay.
constexpr uint32_t external_callers[]={
    0x427c,0x6252,0x6466,0x64d4,0x65ec,0x65fa,0x6844,0x6d18,
    0x71e4,0x7bb6,0x805a,0xb676,0xd0da,0x13236,0x18e8a
};
struct Hook { uint32_t entry;Registers pre,post;uint8_t io;Math before,after;std::vector<Event> writes; };
}

int main(int argc,char** argv) try {
    const char* path=argc>1 ? argv[1] : "build/oracle/host_objects.bin";
    std::ifstream in(path,std::ios::binary);if(!in) throw std::runtime_error("missing host capture");
    char magic[8];read(in,magic,8);if(std::memcmp(magic,"AB2OBJ02",8)) throw std::runtime_error("not a host object capture");
    if(!roms.load_aburner2()) return 1;
    MainBus::Memory memory;
    constexpr size_t N=MainBus::MEMORY_BYTES;
    std::vector<uint8_t> before(N),after(N),expected(N),other(N),reads(N),writes(N);
    unsigned total=0,identical=0,failed=0,skipped=0,executed=0,external=0,reported=0;
    std::map<uint32_t,unsigned> executed_entries,external_entries;
    while(in.peek()!=std::char_traits<char>::eof()) {
        auto index=u32(in),frame=u32(in),entry=u32(in);
        if(entry!=0x14594) throw std::runtime_error("host corpus must contain dispatcher windows only");
        auto r=registers(in),post=registers(in);auto initial_math=math(in);
        read(in,before.data(),N);read(in,after.data(),N);
        offsets(in,expected);offsets(in,other);auto expected_io=events(in);
        auto n=u32(in);if(n>254) throw std::runtime_error("invalid hook count");
        std::vector<Hook> hooks;
        while(n--) {
            Hook h;h.entry=u32(in);h.pre=registers(in);h.post=registers(in);
            h.io=u32(in);h.before=math(in);h.after=math(in);h.writes=events(in);hooks.push_back(std::move(h));
        }
        ++total;
        memory.bytes= {};std::copy(before.begin(),before.end(),memory.bytes.begin());
        std::fill(reads.begin(),reads.end(),0);std::fill(writes.begin(),writes.end(),0);
        MainBus bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);
        load_math(bus,initial_math);
        unsigned cursor=0;bool contract=true,external_call=false;uint8_t io=0xff;
        std::vector<Event> handler_events,dispatcher_io;
        bus.io[1].input=[&](unsigned p){ return p==0 ? io : 0xff; };
        MainBus::Trace trace{reads.data(),writes.data(),{}};
        // Trace ownership switches at actual dispatcher call boundaries.
        bool inside=false;
        trace.access=[&](bool w,uint32_t a,uint16_t d,uint16_t m) {
            if(!w) return;
            if(inside) handler_events.emplace_back(a,d&m,m);
            else if(MainBus::memory_offset(a)==MainBus::UNMAPPED) dispatcher_io.emplace_back(a,d&m,m);
        };
        bus.trace=&trace;
        OMainHost::dispatch(bus,r,[&](uint32_t target,Registers& state) {
            const auto& h=hooks.at(cursor-1);
            if(target!=h.entry) throw std::runtime_error("nested unported caller reached; cannot replay a handler prefix");
            if(std::find(std::begin(external_callers),std::end(external_callers),target)==std::end(external_callers)) {
                contract=false;
                if(reported++<8) std::cerr<<"verified entry was downgraded to external replay: "<<std::hex<<target<<std::dec<<'\n';
            }
            external_call=true;++external;++external_entries[target];
            bus.trace=nullptr;
            for(const auto& e:h.writes) apply(bus,e);
            load_math(bus,h.after);state=h.post;bus.trace=&trace;
        },[&](bool entering,uint32_t target,const Registers& state) {
            if(entering) {
                const auto& h=hooks.at(cursor++);
                contract &= target==h.entry && equal(state,h.pre) && equal_math(bus,h.before);
                // The other CPU's independent chip state is an external input.
                load_math(bus,h.before,false);io=h.io;
                handler_events.clear();external_call=false;inside=true;
            } else {
                const auto& h=hooks.at(cursor-1);inside=false;
                if(!external_call) {
                    ++executed;++executed_entries[target];
                    const bool ok=equal(state,h.post) && equal_math(bus,h.after) && handler_events==h.writes;
                    contract &= ok;
                    for(const auto& e:h.writes) mark(expected,e);
                    if(!ok && reported++<8) {
                        std::cerr<<"record "<<index<<" frame "<<frame<<" handler "<<std::hex<<target<<std::dec
                                 <<" registers="<<equal(state,h.post)<<" chips="<<equal_math(bus,h.after)
                                 <<" ordered writes="<<(handler_events==h.writes)<<'\n';
                        for(unsigned j=0;j<8;++j) if(state.d[j]!=h.post.d[j]) std::cerr<<" D"<<j<<' '<<std::hex<<state.d[j]<<'/'<<h.post.d[j]<<std::dec<<'\n';
                        for(unsigned j=0;j<7;++j) if(state.a[j]!=h.post.a[j]) std::cerr<<" A"<<j<<' '<<std::hex<<state.a[j]<<'/'<<h.post.a[j]<<std::dec<<'\n';
                        for(unsigned j=0;j<std::min(handler_events.size(),h.writes.size());++j) if(handler_events[j]!=h.writes[j]) {
                            auto [a,d,m]=handler_events[j];auto [x,y,z]=h.writes[j];
                            std::cerr<<" event "<<j<<' '<<std::hex<<a<<':'<<d<<':'<<m<<'/'<<x<<':'<<y<<':'<<z<<std::dec<<'\n';break;
                        }
                    }
                }
            }
        });
        bus.trace=nullptr;
        bool race=false;for(size_t i=0;i<N;++i) race |= reads[i] && other[i];
        if(race) { ++skipped;continue; }
        unsigned differences=!(contract && cursor==hooks.size() && equal(r,post))+(dispatcher_io!=expected_io);
        for(size_t i=0;i<N;++i) differences += writes[i]!=expected[i] || (expected[i] && memory.bytes[i]!=after[i]);
        if(differences) { ++failed;if(reported++<8) std::cerr<<"record "<<index<<" frame "<<frame<<": "<<differences<<" differences\n"; }
        else ++identical;
    }
    std::cout<<"Host objects vs arcade: "<<identical<<'/'<<total<<" identical, "<<failed<<" different, "<<skipped<<" skipped (sub CPU changed a read input)\n";
    std::cout<<"Executed handlers (including raced windows): "<<executed<<"; external caller transcripts: "<<external<<"\nExecuted entries:";
    for(auto [e,n]:executed_entries) std::cout<<' '<<std::hex<<e<<std::dec<<'='<<n;
    std::cout<<"\nExternal entries:";for(auto [e,n]:external_entries) std::cout<<' '<<std::hex<<e<<std::dec<<'='<<n;std::cout<<'\n';
    return failed || !identical || !executed;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
