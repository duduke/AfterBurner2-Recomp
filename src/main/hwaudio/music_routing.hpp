#pragma once
#include "hwaudio/xboard_sound_cpu.hpp"
#include <array>
#include <cmath>
#include <span>
#include <vector>
#include <stdexcept>
// Passive sound-ROM metadata. All emulated instructions continue normally.
class MusicRouting {
public:
    enum class Kind { start, stop, fade };
    struct Event {uint64_t ns;Kind kind;int track;float gain=1;};
    inline static constexpr std::array<uint8_t,6> commands{0x92,0x91,0x94,0x93,0x95,0x96};
    uint32_t fm_music=0;int track=-1;bool playing=false;float fade=1;
    std::vector<Event> events;
    explicit MusicRouting(std::span<const uint8_t> rom):rom(rom) {
        if(rom.size()<0xf000)throw std::runtime_error("incomplete sound ROM");
        for(unsigned i=0;i<6;++i) {
            // $071F-$0734: descriptor and handler tables; $0C74 initializes
            // sixteen $2B-byte music records at F820 (8 FM, 8 PCM).
            const auto table=0x735+2*(commands[i]-0x40);roots[i]=word(table);
            if(size_t(roots[i])+145>=rom.size() || rom[roots[i]]!=16)throw std::runtime_error("unsupported music descriptor");
            for(unsigned n=0;n<16;++n)if(rom[roots[i]+2+n*9]!=(n<8?0x80+n:0x40+n-8))throw std::runtime_error("unsupported music voice layout");
        }
    }
    static bool interesting(const XBoardSoundCpu::Event& e) {
        using K=XBoardSoundCpu::Kind;
        return (e.kind==K::MemoryRead && e.pc==0xc76) ||
            (e.kind==K::IoWrite && e.address<=1) ||
            (e.kind==K::MemoryWrite && (e.address==0xf810 || e.address==0xfeb8 ||
             (e.address>=0xf820 && e.address<0xfad0 && (e.address-0xf820)%43==0)));
    }
    void observe(const XBoardSoundCpu::Event& e,uint64_t ns,const XBoardSoundCpu::Registers& r,const std::array<uint8_t,0x800>& ram) {
        using K=XBoardSoundCpu::Kind;
        // Actual sequencer initialization, not an incoming request: $0BCF may
        // postpone a command in F810 while the previous music fades.
        if(e.kind==K::MemoryRead && e.pc==0xc76)for(int i=0;i<6;++i)if(e.address==roots[i]) {
            track=i;playing=true;fade=1;events.push_back({ns,Kind::start,i});break;
        }
        if(e.kind==K::MemoryWrite && e.address==0xf810 && !e.data && (e.pc==0x658 || e.pc==0x68f))stop(ns);
        if(e.kind==K::MemoryWrite && e.address==0xfeb8 && playing) {
            // $0E63/$0F30: the first FM music fade record; YM TL steps are
            // 0.75 dB. Only external music needs this gain; native stems have it.
            fade=std::pow(2.f,-float(e.data)/8.f);events.push_back({ns,Kind::fade,track,fade});
        }
        if(e.kind==K::MemoryWrite && e.address>=0xf820 && e.address<0xfad0 && (e.address-0xf820)%43==0 && playing) {
            bool active=false;for(unsigned i=0;i<16;++i)active|=bool(ram[0x20+i*43]&0x80);
            if(!active)stop(ns);
        }
        if(e.kind==K::IoWrite && (e.address&0xff)==0)ym_address=e.data;
        if(e.kind==K::IoWrite && (e.address&0xff)==1 && r.ix>=0xf820 && r.ix<=0xfe82 && (r.ix-0xf820)%43==0) {
            int channel=-1;if(ym_address>=0x20)channel=ym_address&7;
            if(ym_address==8 && (e.data&0x78))channel=e.data&7;
            if(channel>=0) {
                // $0D42 skips music writes when the physical voice is stolen.
                // Effects use records FAD0 onward, including FM channels 4..7.
                const unsigned bit=1u<<channel;
                if(r.ix<0xfad0)fm_music|=bit;else fm_music&=~bit;
            }
        }
    }
private:
    std::span<const uint8_t> rom;std::array<uint16_t,6> roots{};uint8_t ym_address{};
    uint16_t word(unsigned a)const{return rom[a]|uint16_t(rom[a+1])<<8;}
    void stop(uint64_t ns){if(playing){playing=false;events.push_back({ns,Kind::stop,track});}}
};
