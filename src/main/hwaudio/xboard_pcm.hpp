// SPDX-License-Identifier: BSD-3-Clause
// Copyright holders: Hiromitsu Shioya, Olivier Galibert
// See LICENSES/MAME-SegaPCM.txt for the original algorithm's license.
#pragma once
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

// SegaPCM BANK_512 device algorithm from MAME segapcm.cpp (BSD-3-Clause,
// Hiromitsu Shioya, Olivier Galibert). One tick is one 4MHz/128 sample.
// The host must update the stream before CPU reads/writes, as MAME does.
// Output is the raw integer sum before speaker gains/clipping/resampling.
class XBoardPcm
{
public:
    struct State {std::array<uint8_t,0x800> ram;std::array<uint8_t,16> low{};};
    struct Sample {int32_t left{},right{};};
    struct Coverage {
        uint64_t active{},loops{},stops{},fraction_carries{},address_wraps{},end_wraps{},high_volumes{};
        uint64_t zero_deltas{},ignored_controls{},positive_samples{},negative_samples{},disables_retaining_fraction{};
        uint16_t channels{};uint8_t banks{};
    } coverage;
    explicit XBoardPcm(std::span<const uint8_t> rom) : rom(rom) {
        if(rom.size()!=0x80000)throw std::invalid_argument("PCM needs the complete 80000-byte ERASEFF region");
        s.ram.fill(0xff);
    }
    const State& state() const {return s;}
    void load_initial(const State& initial) {s=initial;}
    uint8_t read(uint16_t offset) const {return s.ram[offset&0x7ff];}
    void write(uint16_t offset,uint8_t data) {
        offset&=0x7ff;
        if(offset<0x100&&(offset&0x87)==0x86&&(data&1)&&!(s.ram[offset]&1)&&s.low[(offset-0x86)/8])
            ++coverage.disables_retaining_fraction;
        s.ram[offset]=data;
    }
    Sample tick(std::array<Sample,16>* voices=nullptr) {
        if(voices)voices->fill({});
        Sample out;
        // MAME sound_stream_update: channel enable, BANK_512 and 24-bit
        // fixed-point address (eight fractional bits retained internally).
        for(unsigned ch=0;ch<16;++ch) {
            auto* r=s.ram.data()+8*ch;
            if(r[0x86]&1)continue;
            ++coverage.active;coverage.channels|=1u<<ch;
            coverage.banks|=1u<<((r[0x86]&0x70)>>4);
            if(!r[7])++coverage.zero_deltas;
            if(r[0x86]&0x8c)++coverage.ignored_controls;
            const uint32_t bank=(r[0x86]&0x70)<<12;
            uint32_t addr=(uint32_t(r[0x85])<<16)|(uint32_t(r[0x84])<<8)|s.low[ch];
            const uint32_t loop=(uint32_t(r[5])<<16)|(uint32_t(r[4])<<8);
            const uint8_t end=static_cast<uint8_t>(r[6]+1);
            if(r[6]==0xff)++coverage.end_wraps;
            bool stopped=false;
            if((addr>>16)==end) {
                if(r[0x86]&2) {r[0x86]|=1;stopped=true;++coverage.stops;}
                else {addr=loop;++coverage.loops;}
            }
            if(!stopped) {
                // MAME read_byte returns unsigned samples centered on 80;
                // only seven volume bits participate. No per-device clamp.
                const int32_t v=int32_t(rom[bank+(addr>>8)])-0x80;
                out.left+=v*(r[2]&0x7f);out.right+=v*(r[3]&0x7f);
                if(voices)(*voices)[ch]={v*(r[2]&0x7f),v*(r[3]&0x7f)};
                if((r[2]|r[3])&0x80)++coverage.high_volumes;
                if((addr&255)+r[7]>255)++coverage.fraction_carries;
                if(addr+r[7]>0xffffff)++coverage.address_wraps;
                addr=(addr+r[7])&0xffffff;
            }
            // MAME writes back the integer address even on a stop; the
            // internal fraction clears only when the disable bit is set.
            r[0x84]=addr>>8;r[0x85]=addr>>16;
            s.low[ch]=(r[0x86]&1)?0:static_cast<uint8_t>(addr);
        }
        if(out.left>0||out.right>0)++coverage.positive_samples;
        if(out.left<0||out.right<0)++coverage.negative_samples;
        return out;
    }
private:
    State s;
    std::span<const uint8_t> rom;
};
