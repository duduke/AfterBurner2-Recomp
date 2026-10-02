// Compose the live dispatcher and verified handlers without replaying their
// outputs. Unknown caller transcripts remain explicit external caller inputs.
// IRQ contexts/resumed operand frontiers and changing sub size operands are
// captured inputs. Complete windows also execute the publication wait contract.
// These checks do not predict arbitrary CPU instruction timing.
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
#include "oracle_shared_inputs.hpp"
#include "engine/omainhost.hpp"
#include "engine/omainadc.hpp"
#include "engine/omainframe.hpp"
#include "engine/omainpalette.hpp"
#include "engine/omainsprites.hpp"
#include "engine/oscenery.hpp"

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
std::vector<SharedOperand> operands(std::ifstream& in) {
    auto n=u32(in);if(n>100000) throw std::runtime_error("invalid shared operand count");
    std::vector<SharedOperand> result;
    while(n--) { auto a=u32(in),d=u32(in),m=u32(in),sm=u32(in);
        if(!shared_operand_address(a) || d>65535 || (m!=0xffff && m!=0xff00 && m!=255) || (sm&~m))
            throw std::runtime_error("invalid shared operand");
        result.push_back({a,uint16_t(d),uint16_t(m),uint16_t(sm)});
    } return result;
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
    0x71e4,0x7bb6,0x805a,0xd0da,0x13236,0x18e8a
};
struct Interrupt { uint32_t entry,ordinal,owner; Registers pre,post; uint8_t adc,io; bool write; uint32_t address; uint16_t mask; std::vector<Event> writes; std::vector<struct Hook> hooks; uint8_t mutex=0,lock=0;std::vector<uint8_t> hidden; };
struct Hook { uint32_t entry;Registers pre,post;uint8_t io;Math before,after;std::vector<Event> writes; std::vector<SharedOperand> inputs;uint32_t blit_ptr=0,blit_cp=0,blit_count=0;std::vector<uint8_t> blit; };
}

int main(int argc,char** argv) try {
    const char* path=argc>1 ? argv[1] : "build/oracle/host_prefix.bin";
    std::ifstream in(path,std::ios::binary);if(!in) throw std::runtime_error("missing host capture");
    char magic[8];read(in,magic,8);const bool complete=std::memcmp(magic,"AB2OBJ07",8)==0;
    const bool shared=complete || std::memcmp(magic,"AB2OBJ06",8)==0;
    const bool sizes=shared || std::memcmp(magic,"AB2OBJ05",8)==0;
    const bool prefix=!complete && (sizes || std::memcmp(magic,"AB2OBJ04",8)==0);
    const bool interleave=complete || prefix || std::memcmp(magic,"AB2OBJ03",8)==0;
    if(!interleave && std::memcmp(magic,"AB2OBJ02",8)) throw std::runtime_error("not a host object capture");
    if(!roms.load_aburner2()) return 1;
    MainBus::Memory memory;
    constexpr size_t N=MainBus::MEMORY_BYTES;
    std::vector<uint8_t> before(N),after(N),expected(N),other(N),reads(N),writes(N),supplied(N),write_values(N);
    std::ofstream requests; if(argc>2) { requests.open(argv[2],std::ios::binary);requests.write("AB2REQ03",8); }
    unsigned irq2=0,irq4=0,irq6=0,latches=0,accepted_irq2=0,accepted_irq4=0,accepted_irq6=0,accepted_latches=0;
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
            h.io=u32(in);h.before=math(in);h.after=math(in);h.writes=events(in);if(shared) h.inputs=operands(in);hooks.push_back(std::move(h));
        }
        std::vector<Interrupt> interrupts;
        if(interleave) {
            n=u32(in);if(n>1000) throw std::runtime_error("invalid interrupt count");
            while(n--) {
                Interrupt q;q.entry=u32(in);q.ordinal=u32(in);q.owner=u32(in);
                q.pre=registers(in);q.post=registers(in);q.adc=u32(in);q.io=u32(in);
                q.write=u32(in);q.address=u32(in);q.mask=u32(in);q.writes=events(in);
                auto k=u32(in);if(shared) {q.mutex=u32(in);q.lock=u32(in);auto bytes=u32(in);if(bytes && bytes!=4096) throw std::runtime_error("invalid hidden IRQ bank");q.hidden.resize(bytes);read(in,q.hidden.data(),bytes);}
                if(k>64) throw std::runtime_error("invalid frame callback count");
                while(k--) { Hook h;h.entry=u32(in);h.pre=registers(in);h.post=registers(in);h.io=u32(in);h.before=math(in);h.after=math(in);h.writes=events(in);if(shared) {
                    h.inputs=operands(in);auto bytes=u32(in);
                    if(bytes) {h.blit_ptr=u32(in);h.blit_cp=u32(in);h.blit_count=u32(in);
                        if(h.blit_count>192 || bytes!=12+(h.blit_count+1)*16) throw std::runtime_error("invalid IRQ sub list");
                        h.blit.resize(bytes-12);read(in,h.blit.data(),h.blit.size());}
                }q.hooks.push_back(std::move(h)); }
                interrupts.push_back(std::move(q));
            }
        }
        auto size_inputs=sizes ? events(in) : std::vector<Event>{};
        unsigned size_cursor=0;
        auto base_irq2=irq2,base_irq4=irq4,base_irq6=irq6,base_latches=latches;
        ++total;
        memory.bytes= {};std::copy(before.begin(),before.end(),memory.bytes.begin());
        std::fill(supplied.begin(),supplied.end(),0);
        std::fill(reads.begin(),reads.end(),0);std::fill(writes.begin(),writes.end(),0);
        MainBus bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);
        load_math(bus,initial_math);
        unsigned ordinal=0,irq_cursor=0;bool in_irq=false;
        unsigned cursor=0;bool contract=true,external_call=false;uint8_t io=0xff;
        std::vector<Event> handler_events,dispatcher_io;
        uint32_t producer_ptr=0,producer_count=0;std::vector<uint8_t> producer_bytes;
        std::vector<std::pair<uint32_t,uint16_t>> producer_targets;
        bus.io[1].input=[&](unsigned p){ return p==0 ? io : 0xff; };
        MainBus::Trace trace{reads.data(),writes.data(),{}, {}};
        trace.write_values=write_values.data();
        MainBus::Trace external_trace{};external_trace.write_values=write_values.data();
        // Trace ownership switches at actual dispatcher call boundaries.
        bool inside=false;const std::vector<SharedOperand>* active_inputs=nullptr;size_t input_cursor=0;unsigned mutex_reads=0;
        trace.access=[&](bool w,uint32_t a,uint16_t d,uint16_t m) {
            if(!w) return;
            if(in_irq) return;
            ++ordinal;
            if(inside) handler_events.emplace_back(a,d&m,m);
            else if(MainBus::memory_offset(a)==MainBus::UNMAPPED) dispatcher_io.emplace_back(a,d&m,m);
        };
        std::function<void()> service;
        service=[&] {
            auto active_trace=bus.trace;bus.trace=&trace;
            const auto& q=interrupts.at(irq_cursor++);in_irq=true;
            auto saved_inputs=active_inputs;auto saved_cursor=input_cursor;active_inputs=nullptr;mutex_reads=0;
            q.entry==0x1461c ? ++irq2 : q.entry==0x14674 ? ++irq4 : ++irq6;
            contract &= equal(q.pre,q.post);
            std::vector<Event> actual;
            auto previous=trace.access;
            trace.access=[&](bool w,uint32_t a,uint16_t d,uint16_t m) {
                if(w) {actual.emplace_back(a,d&m,m);if(a==0xe8016 && (m&255)) ++latches;}
            };
            const auto saved_io=io;io=q.io;bus.endpoints.adc_read=[&]{return q.adc;};
            if(q.entry==0x1461c) main_compare_interrupt(bus,q.pre);
            else if(q.entry==0x1466c) main_compare_ack(bus,q.pre);
            else if(q.entry==0x14674) {
                unsigned j=0;
                if(shared && !q.hidden.empty()) std::copy(q.hidden.begin(),q.hidden.end(),memory.sprite_buffer.begin());
                auto result=OMainFrame{}.interrupt(bus,[&](uint32_t e) {
                    const auto& h=q.hooks.at(j++);
                    if(e!=h.entry) throw std::runtime_error("IRQ frame callback sequence differs");
                    io=h.io;load_math(bus,h.before,false);
                    if(shared) {active_inputs=&h.inputs;input_cursor=0;}
                    auto scratch=h.pre;std::vector<Event> produced;
                    auto irq_trace=trace.access;
                    trace.access=[&](bool w,uint32_t a,uint16_t d,uint16_t m) { if(w) {produced.emplace_back(a,d&m,m);actual.emplace_back(a,d&m,m);} };
                    bool verified=true;
                    if(e==0x148be) OMainFrame{}.timers(bus);
                    else if(e==0x1495c) OMainFrame{}.buttons(bus);
                    else if(e==0x148b8) OMainFrame{}.bookkeeping(bus);
                    else if(e==0x147e8) OMainFrame{}.service_edge(bus);
                    else if(e==0xb7c2 || e==0xb7d2) {
                        if(shared) {
                            if((bus.read32(0x2a3c14)+(e==0xb7d2?0xc00:0))!=h.blit_ptr || bus.read32(0x2a3c1c)!=h.blit_cp)
                                throw std::runtime_error("IRQ blit pointer differs");
                            bus.trace=nullptr;bus.write16(h.blit_cp,h.blit_count);
                            auto o=MainBus::memory_offset(h.blit_cp);supplied[o]=supplied[o+1]=1;
                            for(size_t k=0;k<h.blit.size();++k) {bus.write8(h.blit_ptr+k,h.blit[k]);supplied[MainBus::memory_offset(h.blit_ptr+k)]=1;}
                            bus.trace=&trace;
                        }
                        OMainSprites{}.blit(bus,e==0xb7d2);
                    }
                    else if(e==0x18444) OScenery{}.stage_events(bus);
                    else if(e==0x14a86 || e==0x13f92 || e==0x13bcc || e==0x13bb6 || e==0x13bfa || e==0x1329e || e==0x13e5c) OMainPalette{}.run(bus,e);
                    else if(e==0x16c8a) { OMainHost::call(bus,scratch,e); contract &= equal(scratch,h.post); }
                    else if(e==0x17146 || e==0x14990 || e==0xc6f8 || e==0xe2da) {
                        verified=false;bus.trace=shared ? &external_trace : nullptr;
                        for(const auto& ev:h.writes) { apply(bus,ev);actual.push_back(ev); }
                        load_math(bus,h.after);bus.trace=&trace;
                    } else throw std::runtime_error("unlisted IRQ frame external caller");
                    if(verified) { contract &= (!shared || input_cursor==h.inputs.size()) && produced==h.writes && equal_math(bus,h.after);for(const auto& ev:h.writes) mark(expected,ev); }
                    active_inputs=nullptr;trace.access=irq_trace;
                });
                contract &= !result.service_redirect && j==q.hooks.size() && (!shared || mutex_reads==2);
            } else throw std::runtime_error("unlisted interrupt entry");
            if(actual!=q.writes && reported++<8) {
                std::cerr<<"IRQ "<<std::hex<<q.entry<<std::dec<<" ordinal "<<q.ordinal<<" writes "<<actual.size()<<'/'<<q.writes.size()<<'\n';
                for(unsigned j=0;j<std::min(actual.size(),q.writes.size());++j) if(actual[j]!=q.writes[j]) { auto [a,d,m]=actual[j];auto [x,y,z]=q.writes[j];std::cerr<<" IRQ event "<<j<<' '<<std::hex<<a<<':'<<d<<':'<<m<<'/'<<x<<':'<<y<<':'<<z<<std::dec<<'\n';break; }
            }
            contract &= actual==q.writes;
            active_inputs=saved_inputs;input_cursor=saved_cursor;trace.access=previous;io=saved_io;in_irq=false;bus.trace=active_trace;
        };
        trace.before_access=[&](bool w,uint32_t a,uint16_t m) {
            if(in_irq) {
                if(shared && !w && a==0x29c0d6 && m==0xff00 && ++mutex_reads<=2) {
                    auto old_trace=bus.trace;bus.trace=nullptr;
                    auto old=bus.read8(a);const auto& current=interrupts.at(irq_cursor-1);auto input=mutex_reads==1?current.lock:current.mutex;
                    if((old&~2)!=(input&~2)) {std::cerr<<"IRQ mutex read "<<mutex_reads<<" old="<<unsigned(old)<<" input="<<unsigned(input)<<" frame="<<frame<<"\n";throw std::runtime_error("IRQ mutex flags differ");}
                    bus.write8(a,input);supplied[MainBus::memory_offset(a)]=1;bus.trace=old_trace;
                }
                if(shared && !w && active_inputs) contract &= shared_operand(bus,*active_inputs,input_cursor,a,m,supplied);
                return;
            }
            if(!shared && sizes && !w && a>=0x2a3800 && a<0x2a3c00) {
                auto [x,d,mask]=size_inputs.at(size_cursor++);
                if(x!=a || mask!=m) throw std::runtime_error("scaled-size external input access sequence differs");
                // Only the sub CPU's independently produced size-table operand
                // is supplied here. Never replace a main-owned handler result.
                auto saved=bus.trace;bus.trace=nullptr;apply(bus,{a,d,m});bus.trace=saved;
            }
            while(irq_cursor<interrupts.size()) {
            const auto& q=interrupts[irq_cursor];
            // Literal ports omit 68000 dummy RMW/CLR reads. If the
            // captured resumed operand is such a read, the next write at
            // the same frontier is the equivalent routine-effect boundary.
            if(ordinal==q.ordinal && ((w==q.write && a==q.address && m==q.mask) || (w && !q.write))) service();
            else break;
            }
            if(shared && !w && active_inputs) contract &= shared_operand(bus,*active_inputs,input_cursor,a,m,supplied);
        };
        bus.trace=&trace;
        struct PublicationBoundary {};
        try { OMainHost::dispatch(bus,r,[&](uint32_t target,Registers& state) {
            const auto& h=hooks.at(cursor-1);
            if(target==0xb676) {
                // Wait loops have no RAM writes; captured IRQ boundaries supply
                // progress. Publication itself always executes the port.
                while(irq_cursor<interrupts.size() && interrupts[irq_cursor].ordinal==ordinal) service();
                return;
            }
            if(target!=h.entry) throw std::runtime_error("nested unported caller reached; cannot replay a handler prefix");
            if(std::find(std::begin(external_callers),std::end(external_callers),target)==std::end(external_callers)) {
                contract=false;
                if(reported++<8) std::cerr<<"verified entry was downgraded to external replay: "<<std::hex<<target<<std::dec<<'\n';
            }
            external_call=true;++external;++external_entries[target];
            bus.trace=shared ? &external_trace : nullptr;
            for(const auto& e:h.writes) {
                while(irq_cursor<interrupts.size() && interrupts[irq_cursor].ordinal==ordinal) service();
                apply(bus,e);++ordinal;
            }
            while(irq_cursor<interrupts.size() && interrupts[irq_cursor].ordinal==ordinal && interrupts[irq_cursor].owner==cursor) service();
            load_math(bus,h.after);state=h.post;bus.trace=&trace;
        },[&](bool entering,uint32_t target,const Registers& state) {
            if(entering) {
                if(prefix && target==0xb676) {
                    // An IRQ can land after the pointer load but before JSR;
                    // there is then no next operand before this prefix ends.
                    while(irq_cursor<interrupts.size() && interrupts[irq_cursor].ordinal==ordinal) service();
                    if(irq_cursor<interrupts.size() && reported<8) std::cerr<<"prefix pending ordinal="<<ordinal<<" IRQ ordinal="<<interrupts[irq_cursor].ordinal<<" owner="<<interrupts[irq_cursor].owner<<" resume="<<std::hex<<interrupts[irq_cursor].address<<std::dec<<'\n';
                    throw PublicationBoundary{};
                }
                const auto& h=hooks.at(cursor++);
                if(complete && target==0xb676 && requests) {
                    auto saved_trace=bus.trace;bus.trace=nullptr;
                    producer_ptr=bus.read32(0x2a3c04)&0x3fffff;producer_count=bus.read16(0x2a3cf0);
                    if(producer_count>192) throw std::runtime_error("invalid computed producer count");
                    for(unsigned i=0;i<producer_count*16;++i) producer_bytes.push_back(bus.read8(producer_ptr+i));
                    for(uint32_t a:{0x29c0ceu,0x29c0d0u,0x29c0c2u,0x29c0c0u,0x29c0aau}) producer_targets.emplace_back(a,bus.read16(a));
                    bus.trace=saved_trace;
                }
                contract &= target==h.entry && equal(state,h.pre) && equal_math(bus,h.before);
                // The other CPU's independent chip state is an external input.
                load_math(bus,h.before,false);io=h.io;
                handler_events.clear();external_call=false;inside=true;
                if(shared) {active_inputs=&h.inputs;input_cursor=0;}
            } else {
                const auto& h=hooks.at(cursor-1);inside=false;
                if(!external_call) {
                    ++executed;++executed_entries[target];
                    const bool ok=(!shared || input_cursor==h.inputs.size()) && equal(state,h.post) && equal_math(bus,h.after) && handler_events==h.writes;
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
        }); } catch(const PublicationBoundary&) {}
        bus.trace=nullptr;
        bool race=false;for(size_t i=0;i<N;++i) if(shared || !sizes || i<0xf800 || i>=0xfc00) race |= reads[i] && other[i] && !supplied[i];
        if(race) {
            ++skipped;
            if(requests) {for(uint32_t v:{index,1u}) {char b[]={char(v>>24),char(v>>16),char(v>>8),char(v)};requests.write(b,4);}}
            continue;
        }
        unsigned differences=!(contract && cursor==hooks.size() && equal(r,post) && irq_cursor==interrupts.size() && size_cursor==size_inputs.size())+(dispatcher_io!=expected_io);
        for(size_t i=0;i<N;++i) differences += writes[i]!=expected[i] || (expected[i] && (shared ? write_values[i] : memory.bytes[i])!=after[i]);
        if(differences) { ++failed;if(reported++<8) { std::cerr<<"record "<<index<<" frame "<<frame<<": "<<differences<<" differences contract="<<contract<<" finalregs="<<equal(r,post)<<" irq="<<irq_cursor<<'/'<<interrupts.size()<<" sizes="<<size_cursor<<'/'<<size_inputs.size()<<"\n";
            for(size_t i=0;i<N;++i) if(writes[i]!=expected[i] || (expected[i] && (shared ? write_values[i] : memory.bytes[i])!=after[i])) std::cerr<<" byte "<<std::hex<<i<<" actual="<<unsigned(shared ? write_values[i] : memory.bytes[i])<<" expected="<<unsigned(after[i])<<" marks="<<unsigned(writes[i])<<'/'<<unsigned(expected[i])<<std::dec<<'\n'; } }
        else {
            ++identical;accepted_irq2+=irq2-base_irq2;accepted_irq4+=irq4-base_irq4;accepted_irq6+=irq6-base_irq6;accepted_latches+=latches-base_latches;
            if(requests) {
                auto put=[&](uint32_t v) { char b[]={char(v>>24),char(v>>16),char(v>>8),char(v)};requests.write(b,4); };
                const auto ptr=complete ? producer_ptr : bus.read32(0x2a3c04)&0x3fffff;
                auto count=complete ? producer_count : bus.read16(0x2a3cf0);
                if(count>192) throw std::runtime_error("invalid computed request count");
                put(index);put(0);put(frame);put(ptr);put(count);
                for(unsigned i=0;i<count*16;++i) requests.put(complete ? producer_bytes.at(i) : bus.read8(ptr+i));
                if(complete) {for(auto [a,v]:producer_targets) {put(a);put(v);}}
                else for(uint32_t a:{0x29c0ceu,0x29c0d0u,0x29c0c2u,0x29c0c0u,0x29c0aau}) { put(a);put(bus.read16(a)); }
            }
        }
    }
    std::cout<<(interleave ? "Interleaved host objects vs arcade: " : "Host objects vs arcade: ")<<identical<<'/'<<total<<" identical, "<<failed<<" different, "<<skipped<<" skipped (sub CPU changed a read input)\n";
    std::cout<<"IRQs in identical windows: IRQ2="<<accepted_irq2<<" IRQ4="<<accepted_irq4<<" IRQ6="<<accepted_irq6<<" sound latches="<<accepted_latches<<'\n';
    std::cout<<"IRQs executed (including raced windows): IRQ2="<<irq2<<" IRQ4="<<irq4<<" IRQ6="<<irq6<<" sound latches="<<latches<<'\n';
    std::cout<<"Executed handlers (including raced windows): "<<executed<<"; external caller transcripts: "<<external<<"\nExecuted entries:";
    for(auto [e,n]:executed_entries) std::cout<<' '<<std::hex<<e<<std::dec<<'='<<n;
    std::cout<<"\nExternal entries:";for(auto [e,n]:external_entries) std::cout<<' '<<std::hex<<e<<std::dec<<'='<<n;std::cout<<'\n';
    return failed || !identical || !executed;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
