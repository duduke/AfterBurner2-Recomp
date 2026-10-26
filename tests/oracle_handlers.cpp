// Main CPU write-set differential test, not a replay of captured writes.
// Independently executes ROM-derived handlers with captured entry RAM,
// registers, math-chip state and input port; compares CPU-owned ordered writes.
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
#include "engine/omainobjects.hpp"
#include "engine/omainhost.hpp"
#include "hwvideo/wide_scenery.hpp"
#include "hwvideo/wide_explosion.hpp"
#include "hwvideo/wide_hit_effect.hpp"
#include "hwvideo/wide_trail_effect.hpp"


namespace
{
    uint32_t be32(const uint8_t* b) { return (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (b[2] << 8) | b[3]; }
    void read(std::ifstream& in, void* data, size_t size)
    {
        if (!in.read(static_cast<char*>(data), size)) throw std::runtime_error("truncated capture");
    }
    uint32_t u32(std::ifstream& in) { uint8_t b[4]; read(in,b,4); return be32(b); }
    void offsets(std::ifstream& in, std::vector<uint8_t>& marks)
    {
        std::fill(marks.begin(), marks.end(), 0);
        const uint32_t n = u32(in);
        if (n > marks.size()) throw std::runtime_error("invalid write-set count");
        for (uint32_t i = 0; i < n; ++i) marks.at(u32(in)) = 1;
    }
    using Event = std::tuple<uint32_t,uint16_t,uint16_t>;
}

int main(int argc, char** argv) try
{
    const char* path = argc > 1 ? argv[1] : "build/oracle/input.bin";
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error(std::string("No object capture: ") + path);
    char magic[8]; read(in,magic,8);
    const bool prefix_capture=!std::memcmp(magic,"AB2HND03",8);
    const bool io_capture=prefix_capture || !std::memcmp(magic,"AB2HND02",8);
    if (!io_capture && std::memcmp(magic,"AB2HND01",8)) throw std::runtime_error("not a handler capture");
    if (!roms.load_aburner2()) return 1;
    MainBus::Memory memory;
    constexpr size_t N = MainBus::MEMORY_BYTES;
    std::vector<uint8_t> before(N), after(N), expected_w(N), other_w(N), reads(N), writes(N);
    unsigned scenery_kept=0,scenery_culled=0;unsigned total=0, identical=0, failed=0, skipped=0, reported=0, active=0;
    unsigned explosion_visible=0,explosion_hidden=0,explosion_retired=0;
    std::map<std::string,unsigned> explosion_paths;
    std::map<uint32_t,std::map<std::string,unsigned>> hit_effect_paths;
    std::map<uint32_t,std::map<std::string,unsigned>> trail_effect_paths;
    std::map<uint32_t,unsigned> trail_races;
    std::vector<std::pair<uint32_t,unsigned>> entries;
    std::map<uint32_t,std::array<unsigned,3>> outcomes;
    std::map<std::string,unsigned> damage_paths;

    while (in.peek() != std::char_traits<char>::eof())
    {
        uint8_t h[12]; read(in,h,12);
        auto registers=[&] {
            OMainObjects::Registers r;
            for(auto& v:r.d) v=u32(in);
            for(auto& v:r.a) v=u32(in);
            return r;
        };
        const uint32_t terminal=prefix_capture ? u32(in) : 0;
        auto r=registers(), expected_r=registers();const auto projection_entry=r;
        const uint8_t io_input=io_capture ? u32(in) : 0xff;
        uint16_t math_before[32], math_after[32];
        for(auto& v:math_before) v=u32(in);
        for(auto& v:math_after) v=u32(in);
        read(in,before.data(),N); read(in,after.data(),N);
        offsets(in,expected_w); offsets(in,other_w);
        std::vector<Event> expected_events, events;
        const unsigned count = u32(in);
        if (count > 1000000) throw std::runtime_error("invalid event count");
        for (unsigned i=0; i<count; ++i)
        {
            const uint32_t a=u32(in), d=u32(in), m=u32(in);
            expected_events.emplace_back(a,static_cast<uint16_t>(d & m),static_cast<uint16_t>(m));
        }
        ++total;
        const uint32_t entry=be32(h+8);
        auto covered=std::find_if(entries.begin(),entries.end(),[&](auto e){return e.first==entry;});
        if (covered==entries.end()) entries.emplace_back(entry,1); else ++covered->second;
        std::copy(before.begin(),before.end(),memory.bytes.begin());
        std::fill(reads.begin(),reads.end(),0); std::fill(writes.begin(),writes.end(),0);
        MainBus bus(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,memory);
        bus.io[1].input=[&](unsigned p) { return p==0 ? io_input : 0xff; };
        bus.mul.load(math_before[0],math_before[1]);
        bus.sub.mul.load(math_before[16],math_before[17]);
        for(unsigned base : {8u,24u}) {
            uint16_t v[]={math_before[base],math_before[base+1],math_before[base+2],
                          math_before[base+4],math_before[base+5],math_before[base+6]};
            if(base==8) bus.div.load(v); else bus.sub.div.load(v);
        }
        MainBus::Trace trace{reads.data(),writes.data(),[&](bool w,uint32_t a,uint16_t d,uint16_t m) {
            if(w) events.emplace_back(a,d&m,m);
        }};
        // Inspect fixture inputs before enabling trace: coverage inspection
        // must not add reads to the routine's input-race footprint.
        if(entry==0xb0ac) {
            const unsigned lo[]={0x20,0x40,0},hi[]={0xe0,0xc0,255};
            for(unsigned lane=0;lane<3;++lane) {
                const auto value=bus.read8(0xffff8892+lane);
                const char* label=value==0x80 ? "neutral" : value==lo[lane] ? "min" : value==hi[lane] ? "max" : "other";
                ++damage_paths["input-lane-"+std::to_string(lane)+"-"+label];
            }
        }
        if(entry==0x14976) {
            const auto previous=(bus.read8(0x29c076)>>4)&3;
            const auto current=((~io_input)>>4)&3;
            ++damage_paths["weapon-buttons-"+std::to_string(previous)+"-to-"+std::to_string(current)];
        }
        if(entry==0xe744) {
            ++damage_paths[bus.read8(0xffff8e7f)<4 ? "pool-under-four" : "pool-allocate"];
        }
        if(entry==0xe662) ++damage_paths["damage-initialize"];
        if(entry==0xe8d0 || entry==0xe94e) {
            if(!(bus.read8(0x29c200)&4)) ++damage_paths["controller-disabled"];
            else if(bus.read8(0xffff91d0)&4) ++damage_paths["controller-draw-only"];
            else ++damage_paths[std::string(entry==0xe8d0 ? "intact-state-" : "breakup-state-")+std::to_string(bus.read16(0xffff9124))];
        }
        if(entry==0xf640) {
            if(bus.read8(r.a[5]+38)&1) ++damage_paths["fuselage-complete"];
            else if(int16_t(bus.read16(r.a[5]+50))<4) ++damage_paths["fuselage-wait"];
            else {
                auto index=uint16_t((bus.read16(r.a[5]+50)-4)*8);
                ++damage_paths[bus.read16(bus.read32(r.a[5]+26)+int16_t(index))==0xffff ? "fuselage-end" : "fuselage-animation"];
            }
            ++damage_paths[int8_t(bus.read8(0xffff9118))>1 ? "fuselage-sound-wait" : "fuselage-sound-select"];
        }
        if(entry==0xf766 || entry==0xebd2 || entry==0xebf6 || entry==0xec1a || entry==0xec3e) {
            if(entry!=0xf766 && int16_t(bus.read16(0xffff91b4))<4) ++damage_paths["debris-wait"];
            else if(!(bus.read8(0x29c200)&4)) ++damage_paths["debris-disabled"];
            else if(bus.read8(0xffff91d0)&4) ++damage_paths["debris-draw-only"];
            else {
                auto marker=bus.read16(bus.read32(r.a[5]+26));
                ++damage_paths[marker==0xeeee ? "debris-delay" : marker==0xffff ? "debris-end" : "debris-animation"];
            }
        }
        if(entry>=0xfb08 && entry<=0x10310) {
            if(entry==0xfb08 || entry==0xfb9c || entry==0xffbe)
                ++damage_paths[bus.read8(0xffff8e7f) ? "companion-pool-available" : "companion-pool-empty"];
            if(entry==0xfb16)
                ++damage_paths["companion-emitter-state-"+std::to_string(bus.read16(0xffff9124))];
            if(entry==0xfdfa || entry==0x1003e || entry==0x10148 || entry==0x10252) {
                auto timer=bus.read16(r.a[5]+32);
                ++damage_paths[timer==0x8000 ? "companion-frame-subq-overflow" : int16_t(timer)<=1 ? "companion-frame-advance-input" : "companion-frame-wait-input"];
            }
            if(entry==0x100dc || entry==0x101e6)
                ++damage_paths[int16_t(bus.read16(0xffff9124))<(entry==0x100dc ? 2 : 4) ? "companion-state-wait" : "companion-state-ready"];
            if(entry==0xfcb4 || entry==0xfdfa || entry==0x1003e || entry==0x10148 || entry==0x10252) {
                if(!(bus.read8(0x29c200)&4)) ++damage_paths["companion-disabled"];
                else if(bus.read8(0xffff91d0)&4) ++damage_paths["companion-draw-only"];
                else ++damage_paths["companion-active"];
            }
        }
        if(entry==0x103ea)
            ++damage_paths["carrier-state-"+std::to_string(bus.read16(0xffff90b0)&15)];
        if(entry==0x103c6)
            ++damage_paths[bus.read16(0x29c09c)==15 ? "carrier-selector-ready" : "carrier-selector-wait"];
        if(entry==0x10b82 || entry==0x10ba8)
            ++damage_paths[bus.read8(0xffff8e7f) ? "carrier-pool-available" : "carrier-pool-empty"];
        if(entry==0x10ed8)
            ++damage_paths[bus.read8(0x29c200)&0x20 ? "horizon-enabled" : "horizon-disabled"];
        if(entry==0x1586c || entry==0x158a8) {
            auto timer=bus.read8(r.a[5]+58);
            ++damage_paths[timer==0x80 ? "tracking-subq-overflow" : int8_t(timer)<=1 ? "tracking-expire-input" : "tracking-wait-input"];
        }
        if(entry==0x15968)
            ++damage_paths["launch-direction-"+std::to_string(bus.read16(r.a[5]+52))];
        if(entry==0x15c62 || entry==0x15c7a || entry==0x15c94 || entry==0x15c9a || entry==0x15ca2 || entry==0x15caa || entry==0x15cb0)
            ++damage_paths[bus.read8(0xffff8e7f)<6 ? "burst-pool-insufficient" : "burst-pool-available"];
        if(entry==0x8c1c)
            ++damage_paths[bus.read8(0xffff8d80) ? "world-hit-pool-available" : "world-hit-pool-empty"];
        if(entry==0x90ac)
            ++damage_paths["explosion-type-"+std::to_string(bus.read8(r.a[5]+24))];
        if(entry==0x169d8) {
            ++damage_paths[bus.read8(r.a[5]+52) ? "refuel-position-wait" : "refuel-position-update"];
            ++damage_paths[bus.read16(0xffff90fc)==2 ? "refuel-pitch-fixed" : "refuel-pitch-scan"];
        }
        struct Boundary { uint32_t target; };
        uint32_t observed_terminal=0;
        bus.trace=&trace;
        try {
        if(!OMainHost::execute(bus,r,entry,[](uint32_t target,OMainObjects::Registers&) { throw Boundary{target}; }))
            throw std::runtime_error("unknown handler entry");
        } catch(const Boundary& stop) { observed_terminal=stop.target; }
        bus.trace=nullptr;
        if(terminal) ++damage_paths[terminal==0xe542 ? "score-boundary-prefix" : "unknown-boundary-prefix"];
        if(entry==0xe662) ++damage_paths[bus.read32(r.a[5]+16)==0xe8d0 ? "initialize-intact" : "initialize-breakup"];
        if(entry==0xe8d0 || entry==0xe94e)
            ++damage_paths["controller-result-"+std::to_string(bus.read16(0xffff9124))];
        if(entry==0xe744 && uint16_t(r.d[5])>=4)
            ++damage_paths[uint16_t(r.d[7]) ? "pool-orientation-one" : "pool-orientation-zero"];
        if(entry>=0xfb08 && entry<=0x10310) {
            if(bus.read32(r.a[5]+16)!=entry)
                ++damage_paths["companion-result-handler-"+std::to_string(bus.read32(r.a[5]+16))];
            if(!(bus.read8(r.a[5])&0x80)) ++damage_paths["companion-inactive-result"];
        }
        bool contract=r.d==expected_r.d && r.a==expected_r.a && terminal==observed_terminal;
        bool chips=true;
        // Sub chip snapshots are inputs; their concurrent outputs belong to
        // the other CPU. Only compare chips this handler actually owns.
        for(unsigned i=0;i<8;++i) {
            chips &= bus.mul.read(i)==math_after[i];
            chips &= bus.div.read(i)==math_after[8+i];
        }
        bool race=false;
        for (size_t i=0;i<N;++i) race |= reads[i] && other_w[i];
        if (race) {
            if((entry==0xaaee||entry==0x14ee2||entry==0xfcb4||entry==0xfdfa)&&trail_races[entry]++==0){
                std::cout<<"Trail-effect raced input offsets for "<<std::hex<<entry<<':';unsigned shown=0;
                for(unsigned i=0;i<N&&shown<6;++i)if(reads[i]&&other_w[i]){std::cout<<' '<<i;++shown;}
                std::cout<<std::dec<<'\n';
            }
            ++skipped; ++outcomes[entry][2]; continue;
        }
        if(entry==0x8af2){
            MainBus::Memory detached;std::copy(before.begin(),before.end(),detached.bytes.begin());
            MainBus shadow(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,detached);
            auto input=projection_entry;
            const bool kept=Presentation::SceneryProjection::update(shadow,input,0);
            // $9556 returns the size slot through the native free-list counter.
            const bool culled=expected_w[MainBus::memory_offset(0xffff8d80)]!=0;
            if(kept==culled)throw std::runtime_error("detached scenery cull differs from MAME");
            if(kept){++scenery_kept;
                const auto object=MainBus::memory_offset(input.a[5]);
                for(unsigned n=0;n<64;++n)if(detached.bytes[object+n]!=after[object+n])throw std::runtime_error("detached scenery evolution differs from MAME");
            }else ++scenery_culled;
        }

        if(entry==0x91c0){
            MainBus::Memory detached;std::copy(before.begin(),before.end(),detached.bytes.begin());
            MainBus shadow(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,detached);
            auto input=projection_entry;
            const auto state=Presentation::ExplosionProjection::update(shadow,input,0);
            const bool culled=expected_w[MainBus::memory_offset(0xffff8d80)]!=0;
            // Append can reject a visible object when the native queue is full.
            const auto count_offset=MainBus::memory_offset(0x2a3cf0);
            const bool queue_full=int16_t(uint16_t(before[count_offset]<<8|before[count_offset+1]))>=0xc0;
            const bool published=expected_w[count_offset]!=0;
            using State=Presentation::ExplosionProjection::State;
            if((state==State::retired)!=culled || (!queue_full && (state==State::visible)!=published))
                throw std::runtime_error("detached explosion lifetime differs from MAME");
            const auto object=MainBus::memory_offset(input.a[5]);
            if(culled){
                if(int16_t(shadow.read16(input.a[5]+36))<0)++explosion_paths["near-depth"];
                else if(expected_w[object+6])++explosion_paths["below-screen"];
                else ++explosion_paths[int16_t(shadow.read16(input.a[5]+4))<0?"left-edge":"right-edge"];
            }
            if(before[MainBus::memory_offset(0xffff91d0)]&1)++explosion_paths["paused"];
            if(!culled){
                for(unsigned n=0;n<64;++n)if(detached.bytes[object+n]!=after[object+n])
                    throw std::runtime_error("detached explosion evolution differs from MAME");
                if(before[object+47]!=after[object+47])++explosion_paths["animation-frame"];
                if(state==State::visible)++explosion_visible;else ++explosion_hidden;
            }else ++explosion_retired;
        }
        if(entry==0x8e18||entry==0x8ece){
            MainBus::Memory detached;std::copy(before.begin(),before.end(),detached.bytes.begin());
            MainBus shadow(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,detached);
            auto input=projection_entry;
            const auto state=Presentation::HitEffectProjection::update(shadow,input,0,entry==0x8e18);
            using State=Presentation::HitEffectProjection::State;
            const bool culled=expected_w[MainBus::memory_offset(0xffff8d80)]!=0;
            const auto count_offset=MainBus::memory_offset(0x2a3cf0),object=MainBus::memory_offset(input.a[5]);
            auto preword=[&](uint32_t address){const auto o=MainBus::memory_offset(address);return uint16_t(before[o]<<8|before[o+1]);};
            const bool queue_full=int16_t(preword(0x2a3cf0))>=0xc0;
            const bool published=expected_w[count_offset]!=0;
            if((state==State::retired)!=culled||(!queue_full&&(state==State::visible)!=published))
                throw std::runtime_error("detached hit-effect lifetime differs from MAME");
            auto& paths=hit_effect_paths[entry];
            if(culled){
                if(int16_t(shadow.read16(input.a[5]+36))<0)++paths["near-depth"];
                else if(expected_w[object+6])++paths["below-screen"];
                else ++paths[int16_t(shadow.read16(input.a[5]+4))<0?"left-edge":"right-edge"];
            }else{
                ++paths[state==State::visible?"visible":"hidden"];
                for(unsigned n=0;n<64;++n)if(detached.bytes[object+n]!=after[object+n])
                    throw std::runtime_error("detached hit-effect evolution differs from MAME");
            }
            if(before[MainBus::memory_offset(0xffff91d0)]&1)++paths["paused"];
            else if(!culled&&state==State::visible){
                ++paths[before[object+47]!=after[object+47]?"frame-advanced":"frame-held"];
                if(before[object+46]==before[object+47]&&!(shadow.read16(input.a[5]+38)&1))++paths["frame-limit-held"];
                ++paths[int16_t(shadow.read16(input.a[5]+38))>4?"age-capped":"age-uncapped"];
                ++paths[entry==0x8e18&&preword(0x29c33c)?"ground-drift":"sway"];
                if(preword(0x29c0a4))++paths["direction-offset"];
            }
        }
        if(entry==0xaaee||entry==0x14ee2||entry==0xfcb4||entry==0xfdfa){
            MainBus::Memory detached;std::copy(before.begin(),before.end(),detached.bytes.begin());
            MainBus shadow(roms.maincpu.rom,roms.maincpu.length,roms.subcpu.rom,roms.subcpu.length,detached);
            auto input=projection_entry;const auto family=entry==0xaaee?1u:entry==0x14ee2?2u:3u;
            const auto object=MainBus::memory_offset(input.a[5]);
            const auto state=Presentation::TrailEffectProjection::update(shadow,input,0,family,entry);
            using State=Presentation::TrailEffectProjection::State;
            const auto count_offset=MainBus::memory_offset(0x2a3cf0);
            auto preword=[&](uint32_t a){const auto o=MainBus::memory_offset(a);return uint16_t(before[o]<<8|before[o+1]);};
            const bool published=expected_w[count_offset]!=0,queue_full=int16_t(preword(0x2a3cf0))>=0xc0;
            const bool culled=expected_w[MainBus::memory_offset(0xffff8e7f)]!=0 || (family==2&&!(before[object]&0x80)&&!published);
            if((state==State::retired)!=culled||(!culled&&!queue_full&&(state==State::visible)!=published))
                throw std::runtime_error("detached trail-effect lifetime differs from MAME");
            auto& paths=trail_effect_paths[entry];
            ++paths[state==State::retired?"retired":state==State::hidden?"hidden":"visible"];
            if(!culled)for(unsigned n=0;n<64;++n){
                if(detached.bytes[object+n]!=after[object+n])throw std::runtime_error("detached trail-effect evolution differs from MAME");
            }
            for(unsigned n=0;n<N;++n)if((n<object||n>=object+64)&&detached.bytes[n]!=before[n])
                throw std::runtime_error("detached trail effect wrote outside its object");
            if(culled){
                const bool disabled=family!=1&&!(before[MainBus::memory_offset(0x29c200)]&(family==2?2:4));
                if(disabled)++paths["disabled"];
                else if(family==2&&int16_t(shadow.read16(input.a[5]+44))>=36)++paths["age-limit"];
                else if(family==2?shadow.read16(input.a[5]+36)>=0x7fe0:int16_t(shadow.read16(input.a[5]+36))<0)++paths["depth-limit"];
                else if(family!=2&&expected_w[object+6])++paths["bottom"];
                else ++paths[int16_t(shadow.read16(input.a[5]+4))<0?"left":"right"];
            }else{
                if(family!=1&&(before[MainBus::memory_offset(0xffff91d0)]&(family==2?2:4)))++paths["draw-only"];
                if(before[object+50]!=after[object+50]||before[object+51]!=after[object+51])++paths["ramp-step"];
                if(family==2&&preword(input.a[5]+44)>=29)++paths["taper"];
                if(family==3&&before[object+31]!=after[object+31])++paths["animation-step"];
                if(entry==0xfcb4&&preword(0xffff9124)>=5)++paths["animation-transition"];
            }
        }
        active += std::any_of(expected_w.begin(),expected_w.end(),[](uint8_t n){ return n!=0; });
        unsigned differences = (events!=expected_events) + !contract + !chips;
        for (size_t i=0;i<N;++i)
            differences += writes[i]!=expected_w[i] || (expected_w[i] && memory.bytes[i]!=after[i]);
        if (differences)
        {
            ++failed; ++outcomes[entry][1];
            if (reported++<8)
            {
                std::cerr << "record " << be32(h) << " frame " << be32(h+4)
                          << ": " << differences << " differences, register/call contract=" << contract << ", chips=" << chips << ", ordered writes equal=" << (events==expected_events) << '\n';
                for(unsigned i=0;i<8;++i) if(r.d[i]!=expected_r.d[i])
                    std::cerr << " D" << i << " " << std::hex << r.d[i] << "/" << expected_r.d[i] << std::dec << '\n';
                for(unsigned i=0;i<7;++i) if(r.a[i]!=expected_r.a[i])
                    std::cerr << " A" << i << " " << std::hex << r.a[i] << "/" << expected_r.a[i] << std::dec << '\n';
                if(events!=expected_events) {
                    std::cerr << " event counts " << events.size() << "/" << expected_events.size() << '\n';
                    for(unsigned i=0;i<std::min(events.size(),expected_events.size());++i) if(events[i]!=expected_events[i]) {
                        auto [a,d,m]=events[i]; auto [x,y,z]=expected_events[i];
                        std::cerr << " first event mismatch " << i << ": " << std::hex << a << ":" << d << ":" << m << "/" << x << ":" << y << ":" << z << std::dec << '\n';break;
                    }
                }
                for (size_t i=0;i<N;++i)
                    if (writes[i]!=expected_w[i] || (expected_w[i] && memory.bytes[i]!=after[i]))
                        std::cerr << "  byte " << std::hex << i << " expected " << unsigned(after[i])
                                  << " actual " << unsigned(memory.bytes[i]) << " writes " << unsigned(expected_w[i])
                                  << '/' << unsigned(writes[i]) << std::dec << '\n';
            }
        }
        else { ++identical; ++outcomes[entry][0]; }
    }
    std::cout << "Main handlers vs arcade: " << total << " records, " << identical
              << " identical, " << failed << " different, " << skipped << " skipped (other CPU wrote an input)\n"
              << "Coverage: " << active << " calls with writes; entries";
    for (auto e:entries) std::cout << ' ' << std::hex << e.first << std::dec << '=' << e.second;
    std::cout << '\n' << "Per-entry identical/different/skipped:";
    for(auto& [entry,n]:outcomes) std::cout << ' ' << std::hex << entry << std::dec << '=' << n[0] << '/' << n[1] << '/' << n[2];
    std::cout << '\n';
    if(!damage_paths.empty()) {
        std::cout << "Handler path coverage (captured, including any races):";
        for(const auto& [name,n]:damage_paths) std::cout << ' ' << name << '=' << n;
        std::cout << '\n';
    }
    if(scenery_kept||scenery_culled)std::cout<<"Detached scenery: "<<scenery_kept<<" accepted updates match MAME, "<<scenery_culled<<" native culls\n";
    if(explosion_visible||explosion_hidden||explosion_retired)std::cout<<"Detached explosions: "<<explosion_visible<<" visible, "<<explosion_hidden<<" hidden, "<<explosion_retired<<" retired match MAME\n";
    if(!explosion_paths.empty()){
        std::cout<<"Detached explosion coverage:";
        for(const auto& [path,n]:explosion_paths)std::cout<<' '<<path<<'='<<n;
        std::cout<<'\n';
    }
    if(argc==3&&std::string_view(argv[2])=="--explosion-coverage"){
        for(const auto* path:{"near-depth","below-screen","left-edge","right-edge","paused","animation-frame"})
            if(!explosion_paths[path])throw std::runtime_error(std::string("Missing explosion coverage: ")+path);
        if(!explosion_visible||!explosion_hidden||!explosion_retired||skipped)throw std::runtime_error("Incomplete explosion lifetime coverage");
    }
    for(const auto& [entry,paths]:hit_effect_paths){
        std::cout<<"Detached hit-effect "<<std::hex<<entry<<std::dec<<" coverage:";
        for(const auto& [path,n]:paths)std::cout<<' '<<path<<'='<<n;
        std::cout<<'\n';
    }
    if(argc==3&&std::string_view(argv[2])=="--hit-effect-coverage"){
        for(auto entry:{0x8e18u,0x8eceu}){
            auto& paths=hit_effect_paths[entry];
            for(auto path:{"near-depth","below-screen","left-edge","right-edge","paused","visible","hidden","frame-advanced","frame-held","frame-limit-held","age-capped","age-uncapped","direction-offset","sway"})
                if(!paths[path])throw std::runtime_error(std::string("Missing hit-effect coverage: ")+path);
            if(entry==0x8e18&&!paths["ground-drift"])throw std::runtime_error("Missing hit-effect ground drift");
        }
        if(skipped)throw std::runtime_error("Hit-effect coverage contains races");
    }
    for(const auto& [entry,paths]:trail_effect_paths){
        std::cout<<"Detached trail-effect "<<std::hex<<entry<<std::dec<<" coverage:";
        for(const auto& [path,n]:paths)std::cout<<' '<<path<<'='<<n;
        std::cout<<'\n';
    }
    if(argc==3&&std::string_view(argv[2])=="--trail-effect-coverage"){
        for(auto entry:{0xaaeeu,0x14ee2u,0xfcb4u,0xfdfau}){
            auto& paths=trail_effect_paths[entry];
            for(auto path:{"visible","retired","left","right","depth-limit"})
                if(!paths[path])throw std::runtime_error(std::string("Missing trail-effect coverage: ")+path);
            if(entry!=0x14ee2){for(auto path:{"hidden","bottom","ramp-step"})if(!paths[path])throw std::runtime_error(std::string("Missing trail-effect coverage: ")+path);}
            if(entry!=0xaaee){for(auto path:{"disabled","draw-only"})if(!paths[path])throw std::runtime_error(std::string("Missing trail-effect coverage: ")+path);}
        }
        if(!trail_effect_paths[0x14ee2]["age-limit"]||!trail_effect_paths[0x14ee2]["taper"]||!trail_effect_paths[0xfcb4]["animation-transition"]||!trail_effect_paths[0xfdfa]["animation-step"])
            throw std::runtime_error("Missing trail-effect age/animation coverage");
        if(skipped)throw std::runtime_error("Trail-effect coverage contains races");
    }
    return total && identical && !failed ? 0 : 1;
}
catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
