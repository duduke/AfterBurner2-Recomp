#pragma once
#include "hwaudio/music_routing.hpp"
#include "hwaudio/xboard_pcm.hpp"
#include <map>

// Enhanced presentation policy. This never substitutes commands or clocks a
// voice. Categories follow completed Z80 initialization/device writes; unknown
// owners stay on a nonpositional stereo bus. Cabinet geometry is user-selected.
class CockpitRouting {
public:
    enum Group : unsigned { neutral, front, rear, unknown=neutral };
    struct Block {unsigned chip{};uint64_t first{};std::array<std::vector<XBoardPcm::Sample>,3> groups;std::vector<XBoardPcm::Sample> music;};
    std::array<Group,8> fm{};std::array<Group,16> pcm{};
    explicit CockpitRouting(std::span<const uint8_t> sound,std::span<const uint8_t> main):rom(sound){
        // Verified main producers: cannon $9ED8, reload $16CEC, missiles
        // $A002/$A716, explosion $9122/$91B8, crash $E70E/$EA86, debris $F72E.
        std::array<Group,256> commands{};
        commands[0x90]=front;commands[0x9b]=front;
        commands[0x85]=rear;commands[0xda]=rear;commands[0xf6]=rear;
        // $156F8-$1571C emits a left/right passing sound only after the
        // object's depth crosses the player. Keep its original stereo side.
        commands[0x6e]=rear;commands[0x6f]=rear;
        // $14D96: enemy weapon allocation. Front is the enhanced cockpit
        // presentation choice, not an assertion about cabinet wiring.
        commands[0xd8]=front;
        // Reviewed neutral cues: coin/credit $C79C ($7E), timed flag $DC5E
        // ($9A), stage entry $D804 ($A5 -> direct sample $06), startup
        // $BF56/$BF60 ($4C/$4E -> $26/$28), carrier sequence $104EE
        // ($EF -> $46). Service labels identify INSERTED COIN, READY and
        // FLIGHT 1 R2/L2; timer/carrier names and directions remain unverified.
        // These cues intentionally retain original stereo.
        auto table=[&](unsigned address,unsigned n,unsigned stride,Group group){if(address+n*stride>main.size())throw std::runtime_error("cockpit sound table outside ROM");for(unsigned i=0;i<n;++i)commands[main[address+i*stride]]=group;};
        table(0xa716,16,1,front);table(0x91b8,8,1,rear);table(0xf757,8,2,rear);
        // $0A65 initializes FB A7 for the four propulsion variants. These
        // descriptors retain the engine sequencer; no throttle-driven replay.
        for(unsigned c=0x40;c<=0x43;++c)commands[c]=rear;
        for(unsigned c=0x40;c<256;++c){const auto root=word(0x735+2*(c-0x40)),handler=word(0x8b3+2*(c-0x40));
            // All these handlers enter the same $17A2 direct allocator.
            // $AA6 sets flags before $AAE; $B02/$B22/$B44 jump directly.
            if(handler==0xac9||handler==0xad5||handler==0xad2||handler==0xa99||handler==0xa6b||handler==0xaa6||handler==0xaae||handler==0xb02||handler==0xb22||handler==0xb44){merge(direct,root,commands[c]);}
            else if(commands[c]!=unknown){merge(descriptors,root,commands[c]);}
        }
    }
    static bool interesting(const XBoardSoundCpu::Event& e){using K=XBoardSoundCpu::Kind;return
        (e.kind==K::MemoryRead&&(e.pc==0xc76||(e.pc==0x17a2&&e.address==e.pc)))||
        (e.kind==K::MemoryWrite&&e.address>=0xf000&&e.address<0xf100)||
        (e.kind==K::IoWrite&&e.address<=1);}
    void observe(const XBoardSoundCpu::Event& e,const XBoardSoundCpu::Registers& r){
        using K=XBoardSoundCpu::Kind;
        if(e.kind==K::MemoryRead&&e.pc==0xc76&&e.address!=e.pc){
            if(e.address<rom.size()&&r.de>=0xf820&&r.de<=0xfe82&&(r.de-0xf820)%43==0){
                const auto group=get(descriptors,e.address);const unsigned count=rom[e.address];
                for(unsigned i=0;i<count&&r.de+i*43<=0xfe82;++i)owners[(r.de-0xf820)/43+i]=group;
            }
        }
        if(e.kind==K::MemoryRead&&e.pc==0x17a2&&e.address==e.pc)direct_group=get(direct,r.bc);
        const auto group=r.ix==0xf808?direct_group:r.ix>=0xf820&&r.ix<=0xfe82&&(r.ix-0xf820)%43==0?owners[(r.ix-0xf820)/43]:unknown;
        if(e.kind==K::MemoryWrite&&e.address>=0xf000&&e.address<0xf100&&(e.address&0x87)==0x86&&!(e.data&1)){const unsigned ch=((e.address-0xf000)&0x7f)/8;pcm[ch]=group;}
        if(e.kind==K::IoWrite&&e.address==0)ym_address=e.data;
        if(e.kind==K::IoWrite&&e.address==1){int ch=-1;if(ym_address>=0x20)ch=ym_address&7;if(ym_address==8&&(e.data&0x78))ch=e.data&7;if(ch>=0)fm[ch]=group;}
    }
    std::array<uint32_t,3> masks(uint32_t music)const{std::array<uint32_t,3> out{};for(unsigned i=0;i<8;++i)if(!(music&(1u<<i)))out[fm[i]]|=1u<<i;return out;}
private:
    std::span<const uint8_t> rom;std::map<unsigned,Group> descriptors,direct;std::array<Group,39> owners{};Group direct_group{};uint8_t ym_address{};
    unsigned word(unsigned a)const{if(a+1>=rom.size())throw std::runtime_error("incomplete cockpit sound ROM");return rom[a]|unsigned(rom[a+1])<<8;}
    static Group get(const std::map<unsigned,Group>& map,unsigned key){auto it=map.find(key);return it==map.end()?unknown:it->second;}
    static void merge(std::map<unsigned,Group>& map,unsigned key,Group group){auto [it,added]=map.emplace(key,group);if(!added&&it->second!=group)it->second=unknown;}
};
