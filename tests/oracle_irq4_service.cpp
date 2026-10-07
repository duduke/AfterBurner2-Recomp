#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>
#include <set>
#include "engine/omaincallerschedule.hpp"
#include "roms.hpp"
namespace {
uint64_t num(std::ifstream& f,unsigned n) {uint64_t v=0;while(n--){int c=f.get();if(c<0)throw std::runtime_error("truncated IRQ4 capture");v=v<<8|c;}return v;}
void bytes(std::ifstream& f,uint8_t* p,size_t n) {if(!f.read(reinterpret_cast<char*>(p),n))throw std::runtime_error("truncated IRQ4 RAM");}
OMainObjects::Registers regs(std::ifstream& f) {OMainObjects::Registers r;for(auto& v:r.d)v=num(f,4);for(auto& v:r.a)v=num(f,4);return r;}
bool same(const OMainObjects::Registers& a,const OMainObjects::Registers& b) {return a.d==b.d&&a.a==b.a;}
struct Event {uint64_t ns;uint32_t pc,a;uint16_t d,m;bool w;};
Event event(std::ifstream& f) {Event e;e.ns=num(f,8);e.pc=num(f,4);e.a=num(f,4);e.d=num(f,2);e.m=num(f,2);e.w=num(f,1);return e;}
struct SubEvent {unsigned frontier;Event event;};
struct Hook {uint32_t entry;uint64_t start,end;OMainObjects::Registers pre,post;std::vector<Event> events;};
bool leaf(uint32_t p) {return p==0x1495c||p==0x148be||p==0x148b8||p==0x147e8||p==0xb7c2||p==0xb7d2||p==0x13bb6||p==0x13bcc||p==0x13bfa||p==0x1329e||p==0x14a86||p==0x13f92||p==0x16c8a||p==0x18444||p==0x13e5c;}
}
int main(int argc,char** argv) try {
    std::ifstream f(argc>1?argv[1]:"build/oracle/irq4_service.bin",std::ios::binary);
    char magic[8];if(!f.read(magic,8)||memcmp(magic,"AB2IRQ46",8))throw std::runtime_error("invalid IRQ4 capture (regenerate with ROM operand reads)");
    std::ifstream return_file;
    if(argc>2){return_file.open(argv[2],std::ios::binary);char header[8];
        if(!return_file.read(header,8)||memcmp(header,"AB2RTE42",8))throw std::runtime_error("invalid IRQ4 return capture");}
    unsigned return_calls=0,return_bad=0,immediate=0,user_returns=0,entry_events=0,entry_bad=0,entry_state_bad=0;
    if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
    MainBus::Memory memory;MainBus bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);
    unsigned calls=0,identical=0,failed=0,events=0,differences=0,external=0,leaves=0,logic=0,normal=0,overflow=0;
    unsigned redirects=0,interpolated=0,normal_blits=0,draw_skipped=0,rollovers=0;
    unsigned cycle_calls=0,cycle_empty=0,cycle_ordinary=0,cycle_alternate=0,cycle_fade=0;
    std::vector<uint16_t> cycle_selectors,cycle_phases;
    unsigned road_full=0,road_partial=0,road_repeat=0;
    bool road_sources[256]{},road_destinations[8]{};
    std::vector<uint16_t> road_stages;
    unsigned phases[10]{};
    std::set<uint32_t> callee_pcs;
    std::set<unsigned> fade_banks,fade_masks;
    unsigned trigger=0,capture_bank=0,fade_upload=0,blend_setup=0,blend_step=0,reload_hud=0,scenery_clear=0,scenery_setup=0,scenery_words=0,view_init=0,transition_end=0;
    unsigned callee_calls[5]{};
    const uint32_t complex_entries[]={0x14a86,0x13f92,0x16c8a,0x18444,0x13e5c};
    uint64_t now=0;
    while(f.peek()!=std::char_traits<char>::eof()) {
        OMainIrqService::Context c{};c.level=4;c.boundary_ns=num(f,8);c.interrupted_pc=num(f,4);c.return_pc=num(f,4);c.sr=num(f,2);c.supervisor_sp=num(f,4);
        const auto source=num(f,2);c.registers=regs(f);bytes(f,memory.bytes.data(),memory.bytes.size());
        const auto end=num(f,8);const auto n=num(f,4),nh=num(f,4),nsub=num(f,4);
        std::vector<SubEvent> subevents;for(unsigned i=0;i<nsub;++i){auto frontier=num(f,4);subevents.push_back({unsigned(frontier),event(f)});}
        if(n>20000||nh>30||nsub>200000)throw std::runtime_error("invalid IRQ4 event counts");
        std::vector<Event> expected;for(unsigned i=0;i<n;++i)expected.push_back(event(f));
        std::vector<Hook> hooks;
        for(unsigned i=0;i<nh;++i) {
            Hook h;h.entry=num(f,4);h.start=num(f,8);h.end=num(f,8);auto count=num(f,4);h.pre=regs(f);h.post=regs(f);
            if(count>200000)throw std::runtime_error("invalid IRQ4 callee count");
            for(unsigned j=0;j<count;++j)h.events.push_back(event(f));
            if(h.entry==0xb7c2)++normal_blits;
            if(h.entry==0xb7d2)++interpolated;
            if(h.entry==0x13bb6)++road_full;
            if(h.entry==0x13bcc)++road_partial;
            if(h.entry==0x13bfa)++road_repeat;
            if(h.entry==0x1329e) {
                ++cycle_calls;bool copied=false;
                for(const auto& e:h.events) {
                    if(!e.w&&e.pc==0x1329e&&e.a==0x29c09c&&std::find(cycle_selectors.begin(),cycle_selectors.end(),e.d)==cycle_selectors.end())cycle_selectors.push_back(e.d);
                    if(!e.w&&(e.pc==0x132b4||e.pc==0x132ea)&&std::find(cycle_phases.begin(),cycle_phases.end(),e.d)==cycle_phases.end())cycle_phases.push_back(e.d);
                    if(e.w&&(e.pc==0x132da||e.pc==0x13310)&&!copied) {
                        copied=true;if(e.pc==0x132da)++cycle_ordinary;else ++cycle_alternate;
                        if(e.a==0xcdab0)++cycle_fade;
                    }
                }
                if(!copied)++cycle_empty;
            }
            for(const auto& e:h.events)if(!e.w) {
                if(e.pc==0x13bd2&&e.a==0x3f9114)road_sources[e.d&255]=true;
                if(e.pc==0x13be4&&e.a==0x3f9114)road_destinations[(e.d>>8)&7]=true;
                if(e.pc==0x13bfa&&e.a==0x29c060&&std::find(road_stages.begin(),road_stages.end(),e.d)==road_stages.end())road_stages.push_back(e.d);
            }
            for(const auto& e:h.events)if(!e.w){
                if(e.pc==0x14ab6)++trigger;if(e.pc==0x14b14)++capture_bank;
                if(e.pc==0x14b34){++fade_upload;fade_banks.insert(e.d);}
                if(e.pc==0x14bc0)fade_masks.insert(e.d>>8);
                if(e.pc==0x14326)++blend_setup;if(e.pc==0x143c6)++blend_step;
                if(e.pc==0xcfc4)++reload_hud;if(e.pc==0x13a98)++scenery_clear;
                if(e.pc==0x184e6)++scenery_setup;if(e.pc==0x186b2)++scenery_words;
                if(e.pc==0x1871a)++view_init;if(e.pc==0x13e8e)++transition_end;
            }
            for(unsigned k=0;k<5;++k)if(h.entry==complex_entries[k]){++callee_calls[k];for(const auto& e:h.events)callee_pcs.insert(e.pc);}
            if(leaf(h.entry)){expected.insert(expected.end(),h.events.begin(),h.events.end());++leaves;}
            hooks.push_back(std::move(h));
        }
        const auto post=regs(f);const auto sp=num(f,4),sr=num(f,2);
        size_t captured_operands=n;for(const auto& h:hooks)captured_operands+=h.events.size();
        if(sr==0x700)++redirects;
        for(const auto& e:expected)if(e.w&&(e.pc==0x148d2||e.pc==0x148f2))++rollovers;
        if(std::none_of(hooks.begin(),hooks.end(),[](const Hook& h){return h.entry==0xb7c2||h.entry==0xb7d2;}))++draw_skipped;
        std::sort(expected.begin(),expected.end(),[](const auto& a,const auto& b){return a.ns<b.ns;});
        c.boundary_ns+=80*OMainIrqService::completion_tail(bus.read16(c.interrupted_pc),source);
        ++phases[(c.boundary_ns/80)%10];
        const auto count=bus.read16(0x2a3c00);if(int16_t(count)<=0)++logic;else ++normal;if(count==0x8000)++overflow;
        bool ok=true;size_t cursor=0,hcursor=0;OMainIrqService service;OMainCallerSchedule schedule;
        const bool linked=return_file.is_open();
        uint32_t usp=0,active_sp=0,prefetched_pc=0;uint64_t selection_ns=0;unsigned physical=0;
        if(linked){usp=num(return_file,4);selection_ns=num(return_file,8);physical=num(return_file,1);active_sp=num(return_file,4);prefetched_pc=num(return_file,4);}
        auto current_pc=[&](){return linked?schedule.current_pc():service.current_pc();};
        size_t subcursor=0;
        unsigned operand_frontier=0;
        auto apply_sub=[&](){
            while(subcursor<subevents.size()&&subevents[subcursor].frontier<=operand_frontier){
                const auto& e=subevents[subcursor++].event;const auto o=MainBus::memory_offset(e.a);
                if(!((o>=MainBus::SHARED&&o<MainBus::TILE)||(o>=MainBus::ROAD&&o+1<memory.bytes.size())))throw std::runtime_error("invalid sub-owned write lane");
                if(e.a==0x29c0d6){
                    if(e.pc!=0x458||e.m!=0xff00)throw std::runtime_error("unexpected sub mutex producer");
                    // $000458 BSET #1: supply its owned bit, preserving main
                    // bit 0 despite the other CPU's stale RMW byte.
                    memory.bytes[o]=(memory.bytes[o]&~2)|((e.d>>8)&2);
                } else if(e.m&0xff00)memory.bytes[o]=e.d>>8;
                if(e.m&255)memory.bytes[o+1]=e.d;
            }
        };
        MainBus::Trace trace{nullptr,nullptr,[&](bool w,uint32_t a,uint16_t d,uint16_t m) {
            ++operand_frontier;
            const auto p=current_pc();
            if(!w&&a==p+4&&(p==0x13bc4||(p>=0x13bf0&&p<=0x13bf6)||p==0x13c12||(p>=0x13c28&&p<=0x13c36)||(p>=0x132da&&p<=0x132e6)||(p>=0x13310&&p<=0x1331c)))
                throw std::runtime_error("palette operand aliases instruction prefetch excluded by capture");
            // Conservative maximum extension/prefetch window for the new
            // callee graph. Reject operands Lua could mistake for a fetch.
            if(!w&&a>=p+2&&a<=p+10)throw std::runtime_error("callee operand aliases excluded instruction fetch");
            ++events;bool equal=cursor<expected.size();
            if(equal) {const auto& e=expected[cursor++];equal=e.w==w&&e.ns==now&&e.pc==current_pc()&&e.a==a&&e.m==m&&(e.d&m)==(d&m);
                if(!equal&&differences<20)std::cerr<<std::hex<<"IRQ4 bus pc="<<current_pc()<<'/'<<e.pc<<" a="<<a<<'/'<<e.a<<" data="<<(d&m)<<'/'<<(e.d&e.m)<<std::dec<<" time="<<now<<'/'<<e.ns<<'\n';}
            if(!equal){++differences;ok=false;}
        }};
        // Sub-owned write frontiers and physical input are supplied
        // to executed leaves; main's lock bit 0 must match computed state.
        trace.before_access=[&](bool w,uint32_t a,uint16_t m) {
            apply_sub();
            if(w||cursor>=expected.size())return;
            const auto& e=expected[cursor];
            if(e.a!=a||e.m!=m)return;
            if(current_pc()==0xb7e2 || (current_pc()>=0xb7ea&&current_pc()<=0xb7f0)) {
                // The sub-produced sorted list/count remains an operand
                // boundary; main-produced pointer loads are compared.
                const auto o=MainBus::memory_offset(a);
                if(o<MainBus::SHARED || o>=MainBus::TILE)throw std::runtime_error("invalid sub blit operand");
                memory.bytes[o]=e.d>>8;memory.bytes[o+1]=e.d;
            } else if(a==0x150000)bus.io[1].input=[&,v=uint8_t(e.d)](unsigned p){return p==0?v:uint8_t(0xff);};
        };
        auto advance=[&](uint64_t ns){if(ns<now)throw std::runtime_error("nonchronological service clock");now=ns;};
        now=c.boundary_ns;
        auto boundary=[&](uint32_t entry,OMainObjects::Registers& r,uint32_t stack,uint64_t start) {
            if(hcursor==hooks.size())throw std::runtime_error("missing IRQ4 callee boundary");
            const auto& h=hooks[hcursor++];if(calls==0 && (h.entry!=entry||h.start!=start||!same(r,h.pre))) std::cerr<<std::hex<<"Boundary "<<entry<<std::dec<<" start "<<start<<'/'<<h.start<<" regs "<<same(r,h.pre)<<'\n';
            ok &= h.entry==entry&&h.start==start&&same(r,h.pre);
            bus.trace=nullptr;
            ok &= bus.read32(stack)>=0x14674&&bus.read32(stack)<=0x147c2;
            // Explicit external routine: supplied state/events/duration are
            // excluded from the verified bus-event denominator.
            bus.trace=nullptr;
            for(const auto& e:h.events){apply_sub();if(e.w){advance(start+e.ns-h.start);if(e.m==0xffff)bus.write16(e.a,e.d);else bus.write8(e.a+(e.m==255),e.m==255?e.d:e.d>>8);}++operand_frontier;}apply_sub();
            bus.trace=&trace;r=h.post;++external;
            return unsigned((h.end-h.start)/80);
        };
        auto observe=[&](bool entering,uint32_t entry,const OMainObjects::Registers& r,uint64_t at) {
            if(hcursor>=hooks.size())throw std::runtime_error("missing timed leaf capture");
            const auto& h=hooks[hcursor];
            if(failed<8&&(h.entry!=entry||at!=(entering?h.start:h.end)||!same(r,entering?h.pre:h.post)))
                std::cerr<<std::hex<<"Leaf "<<entry<<std::dec<<" entering="<<entering<<" time="<<at<<'/'<<(entering?h.start:h.end)<<" regs="<<same(r,entering?h.pre:h.post)<<'\n';
            ok &= h.entry==entry&&at==(entering?h.start:h.end)&&same(r,entering?h.pre:h.post);
            if(!entering)++hcursor;
        };
        bus.trace=&trace;OMainIrqService::Result result;
        if(linked){
            // Interrupted instruction completion and physical IPL are still
            // boundary inputs; the entered handler and returned CPU state are computed.
            schedule.resume_instruction({{c.registers,c.return_pc,(c.sr&0x2000)?c.supervisor_sp:usp,c.sr,c.boundary_ns},4,0},c.interrupted_pc,usp);
            schedule.enter(bus,c.supervisor_sp,advance);
            result=schedule.finish(bus,advance,[&](uint64_t at){
                if(at!=selection_ns){ok=false;++return_bad;}return physical;
            },boundary,observe);
            const auto selected=num(return_file,1);
            // At the last RTE prefetch MAME exposes return PC + 2.
            const bool equal=result.sp==active_sp&&result.pc+2==prefetched_pc&&schedule.supervisor_stack()==sp&&schedule.state().irq==selected;
            if(!equal){ok=false;++return_bad;if(return_bad<6)std::cerr<<std::hex<<"IRQ4 return sp="<<result.sp<<'/'<<active_sp<<" pc="<<result.pc<<'/'<<prefetched_pc<<" SSP="<<schedule.supervisor_stack()<<'/'<<sp<<std::dec<<" IRQ="<<schedule.state().irq<<'/'<<selected<<'\n';}
            ++return_calls;if(!(sr&0x2000))++user_returns;
            if(selected){
                ++immediate;const auto entry_end=num(return_file,8);auto entry_n=num(return_file,4);
                if(entry_n>10)throw std::runtime_error("invalid IRQ4 immediate entry count");
                std::vector<Event> entry_expected;while(entry_n--)entry_expected.push_back(event(return_file));
                const auto entry_sp=num(return_file,4),entry_sr=num(return_file,2);const auto entry_regs=regs(return_file);
                size_t entry_cursor=0;
                MainBus::Trace entry_trace{nullptr,nullptr,[&](bool w,uint32_t a,uint16_t d,uint16_t m){
                    ++entry_events;bool same=entry_cursor<entry_expected.size();
                    if(same){const auto& e=entry_expected[entry_cursor++];same=e.ns==now&&e.pc==schedule.current_pc()&&e.a==a&&e.m==m&&e.w==w&&(e.d&m)==(d&m);}
                    if(!same){ok=false;++entry_bad;}
                }};bus.trace=&entry_trace;
                if(schedule.state().irq==selected){
                    const auto entered=schedule.enter(bus,schedule.supervisor_stack(),advance);
                    const bool same=entry_cursor==entry_expected.size()&&entered.ns==entry_end&&entered.sp==entry_sp&&entered.sr==entry_sr&&::same(entered.registers,entry_regs);
                    if(!same){ok=false;++entry_state_bad;}
                }else {ok=false;++entry_state_bad;}
                bus.trace=&trace;
            }
        }else result=service.run(bus,c,advance,boundary,observe);
        bus.trace=nullptr;apply_sub();
        ok &= operand_frontier==captured_operands&&subcursor==subevents.size()&&cursor==expected.size()&&hcursor==hooks.size()&&result.ns==end&&(linked?schedule.supervisor_stack():result.sp)==sp&&result.sr==sr&&same(result.registers,post)&&result.pc==(sr==0x700?0x1a130:c.return_pc);
        ++calls;if(ok)++identical;else {++failed;if(failed<8)std::cerr<<"IRQ4 call "<<calls<<" end "<<result.ns<<'/'<<end<<" consumed "<<cursor<<'/'<<expected.size()<<'\n';}
    }
    std::cout<<"IRQ4 service envelope: "<<identical<<" of "<<calls<<" identical, "<<failed<<" different, 0 skipped\nIRQ4 predicted bus events: "<<events-differences<<" of "<<events<<" identical, "<<differences<<" different, 0 skipped\nCoverage: logic="<<logic<<" normal="<<normal<<" signed-overflow="<<overflow<<" timed leaves="<<leaves<<" external callee boundaries="<<external<<"\nEdges: redirects="<<redirects<<" normal/interpolated blits="<<normal_blits<<'/'<<interpolated<<" skipped draw="<<draw_skipped<<" timer rollovers="<<rollovers<<"; E phases";
    for(unsigned i=0;i<10;++i)std::cout<<' '<<i<<'='<<phases[i];std::cout<<'\n';
    std::cout<<"Road palette calls: full="<<road_full<<" partial="<<road_partial<<" repeated="<<road_repeat
        <<" source selectors="<<std::count(std::begin(road_sources),std::end(road_sources),true)
        <<" destination banks="<<std::count(std::begin(road_destinations),std::end(road_destinations),true)
        <<" stage operands="<<road_stages.size()<<'\n';
    std::cout<<"Palette cycle calls: "<<cycle_calls<<" empty="<<cycle_empty<<" ordinary="<<cycle_ordinary<<" alternate="<<cycle_alternate<<" fade destinations="<<cycle_fade<<" selectors="<<cycle_selectors.size()<<" phase words="<<cycle_phases.size()<<'\n';
    std::cout<<"Computed complex callees:";for(unsigned k=0;k<5;++k)std::cout<<' '<<std::hex<<complex_entries[k]<<std::dec<<'='<<callee_calls[k];std::cout<<" operand/stack PCs="<<callee_pcs.size()<<'\n';
    std::cout<<"Callee paths: fade trigger/capture/upload="<<trigger<<'/'<<capture_bank<<'/'<<fade_upload<<" banks/masks="<<fade_banks.size()<<'/'<<fade_masks.size()<<" blend setup/step operands="<<blend_setup<<'/'<<blend_step<<" reload HUD="<<reload_hud<<" scenery clear/setup/word tiles/view init="<<scenery_clear<<'/'<<scenery_setup<<'/'<<scenery_words<<'/'<<view_init<<" transition completion="<<transition_end<<'\n';
    if(return_file.is_open()&&return_file.peek()!=std::char_traits<char>::eof())throw std::runtime_error("unconsumed IRQ4 return capture");
    std::cout<<"Linked IRQ4 RTE: "<<return_calls-return_bad<<" of "<<return_calls<<" identical, "<<return_bad<<" different, 0 skipped; "<<immediate<<" immediate entry assertions; "<<user_returns<<" user returns\n";
    std::cout<<"IRQ4 immediate entries: "<<immediate-entry_state_bad<<" of "<<immediate<<" identical, "<<entry_state_bad<<" different, 0 skipped\n";
    std::cout<<"IRQ4 immediate entry bus events: "<<entry_events-entry_bad<<" of "<<entry_events<<" identical, "<<entry_bad<<" different, 0 skipped\n";
    return calls&&!failed&&!differences&&!return_bad&&!entry_bad&&!entry_state_bad?0:1;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
