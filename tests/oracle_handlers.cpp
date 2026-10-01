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
#include "engine/omaininput.hpp"
#include "engine/omainplayer.hpp"
#include "engine/omaindamage.hpp"
#include "engine/omainweapons.hpp"
#include "engine/omainworld.hpp"


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
    const bool io_capture=!std::memcmp(magic,"AB2HND02",8);
    if (!io_capture && std::memcmp(magic,"AB2HND01",8)) throw std::runtime_error("not a handler capture");
    if (!roms.load_aburner2()) return 1;
    MainBus::Memory memory;
    constexpr size_t N = MainBus::MEMORY_BYTES;
    std::vector<uint8_t> before(N), after(N), expected_w(N), other_w(N), reads(N), writes(N);
    unsigned total=0, identical=0, failed=0, skipped=0, reported=0, active=0;
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
        auto r=registers(), expected_r=registers();
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
        bus.trace=&trace;
        if(entry==0xb0ac) OMainInput::update(bus,r);
        else if(entry==0x14976) OMainInput::buttons(bus,r);
        else if((entry>=0xe662 && entry<=0xecac) || (entry>=0xf3e2 && entry<=0x10346)) OMainDamage::run(bus,r,entry);
        else if(entry==0x9810) OMainPlayer::initialize(bus,r);
        else if(entry==0x98c0) OMainPlayer::update(bus,r);
        else if(entry==0x9c34) OMainPlayer::shadow_initialize(bus,r);
        else if(entry==0x9c9e) OMainPlayer::shadow_update(bus,r);
        else if(entry==0xae8c || entry==0xaee4 || entry==0xaf08) OMainPlayer::exhaust_initialize(bus,r,entry);
        else if(entry==0xaf3c || entry==0xafcc || entry==0xb05c) OMainPlayer::exhaust_update(bus,r,entry);
        else if(entry==0x1653a || entry==0x1656a || entry==0x16572 || entry==0x1657a) OMainPlayer::sequence_update(bus,r,entry);
        else if(entry==0x1678e) OMainPlayer::sequence_project(bus,r);
        else if(entry==0x16c8a) OMainWeapons::reload(bus,r);
        else if(entry==0xcfc4) OMainWeapons::missile_hud(bus,r);
        else if(entry==0xd062) OMainWeapons::decimal(r);
        else if(entry==0x9d2c) OMainWeapons::initialize(bus,r);
        else if(entry==0x9d56) OMainWeapons::update(bus,r);
        else if(entry==0x9df8) OMainWeapons::missile_fire(bus,r);
        else if(entry==0x9e50) OMainWeapons::vulcan_fire(bus,r);
        else if(entry==0x9e82) OMainWeapons::missile_allocate(bus,r);
        else if(entry==0x9ed8) OMainWeapons::vulcan_allocate(bus,r);
        else if(entry==0x9f08 || entry==0x9f22 || entry==0x9f44 || entry==0x9f5e)
            OMainWeapons::pool(bus,r,entry==0x9f44 || entry==0x9f5e,entry==0x9f08 || entry==0x9f44);
        else if(entry==0x9626 || entry==0x964c) OMainPlayer::recover(bus,r,entry);
        else if(entry==0x88c2) OMainWorld::initialize(bus,r);
        else if(entry==0x8af2) OMainWorld::update(bus,r);
        else if(entry==0x91c0) OMainWorld::explosion(bus,r);
        else if(entry==0x8d44 || entry==0x8d58) OMainWorld::effect_initialize(bus,r,entry==0x8d44);
        else if(entry==0x8e18 || entry==0x8ece) OMainWorld::effect_update(bus,r,entry==0x8e18);
        else if(entry==0x8cc8) OMainWorld::player_collision(bus,r);
        else if(entry==0x9556) OMainWorld::release(bus,r);
        else if(entry==0x9f7c) OMainWeapons::missile_initialize(bus,r);
        else if(entry==0xa030) OMainWeapons::vulcan_initialize(bus,r);
        else if(entry==0xac4c) OMainWeapons::vulcan_update(bus,r);
        else if(entry==0xae1c || entry==0xae2e || entry==0xae50) OMainWeapons::remove(bus,r,entry!=0xae50,entry==0xae1c);
        else if(entry==0xa234 || entry==0xa382 || entry==0xa53a) OMainWeapons::missile_update(bus,r,entry);
        else if(entry==0xa9f2) OMainWeapons::trail_allocate(bus,r);
        else if(entry==0xaa32 || entry==0xaaee) OMainWeapons::trail(bus,r,entry==0xaa32);
        else if(entry==0xae32) OMainWeapons::remove(bus,r,true,false,false);
        else if(entry>=0x14dc4 && entry<=0x1586a) OMainWeapons::enemy(bus,r,entry);
        else throw std::runtime_error("unknown handler entry");
        bus.trace=nullptr;
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
        bool contract=r.d==expected_r.d && r.a==expected_r.a;
        bool chips=true;
        // Sub chip snapshots are inputs; their concurrent outputs belong to
        // the other CPU. Only compare chips this handler actually owns.
        for(unsigned i=0;i<8;++i) {
            chips &= bus.mul.read(i)==math_after[i];
            chips &= bus.div.read(i)==math_after[8+i];
        }
        bool race=false;
        for (size_t i=0;i<N;++i) race |= reads[i] && other_w[i];
        if (race) { ++skipped; ++outcomes[entry][2]; continue; }
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
        std::cout << "Damage path coverage (captured, including any races):";
        for(const auto& [name,n]:damage_paths) std::cout << ' ' << name << '=' << n;
        std::cout << '\n';
    }
    return total && identical && !failed ? 0 : 1;
}
catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
