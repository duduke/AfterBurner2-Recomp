#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Presentation {

// Coordinates remain in the native 320x224 safe area. Each draw has its own
// 320x224 mask, including transparent holes in the original aircraft. The
// frontend samples authored art at output resolution and clips it to that mask.
struct PlayerAircraftDraw {
    unsigned frame_address{},source_width{},source_height{};
    int x{},y{};
    unsigned width{},height{},top{},bottom{};
    bool flip_x{},flip_y{};
    size_t mask_offset{};
    unsigned root_frame_address{};
};
struct PlayerAircraftScene {
    std::span<const PlayerAircraftDraw> draws;
    std::span<const uint8_t> visibility;
};

// The level neutral and gentle-bank roots come from main $28C94's pitch-2
// table; sub $9E4 supplies their LODs. A pack must explicitly admit a root
// before any of its hardware descriptors may be detached. The empty list
// retains the original single-image prototype's neutral-only contract.
// All geometry and hardware source addresses are read from the external ROM.
class PlayerAircraftAssets {
    struct Frame {unsigned address,width,height,bank,offset,pitch,reverse,root;};
    std::vector<Frame> frames;
public:
    PlayerAircraftAssets(std::span<const uint8_t> main,std::span<const uint8_t> sub) {
        const auto word=[](std::span<const uint8_t> data,unsigned address)->unsigned {
            return address+1<data.size()?unsigned(data[address])<<8|data[address+1]:0;
        };
        const auto pointer=[&](unsigned address)->unsigned {
            if(address+3>=main.size())return 0;
            return ((word(main,address)<<16)|word(main,address+2))&0xffffff;
        };
        const unsigned table=pointer(0x28c94+2*4),root=pointer(table);
        if(!table || root!=0x100a0 || sub.size()<0x100b0)return;
        std::vector<int> offsets{0};
        for(unsigned zoom=1;zoom<64;++zoom){
            const unsigned entry=0x9e4+zoom*8+4;if(entry+1>=sub.size())return;
            const int delta=int16_t(word(sub,entry));
            if(std::find(offsets.begin(),offsets.end(),delta)==offsets.end())offsets.push_back(delta);
        }
        const auto add_family=[&](unsigned family_root){
            std::vector<Frame> family;
            for(const int delta:offsets){
                const int address=int(family_root)+delta;
                if(address<0 || unsigned(address)+15>=sub.size())return;
                Frame frame{unsigned(address),word(sub,address),word(sub,address+2)+1,
                    word(sub,address+4)>>9,word(sub,address+6),word(sub,address+8),word(sub,address+10),family_root};
                if(!frame.width || !frame.pitch || frame.pitch>127 || frame.width!=frame.pitch*8 ||
                    frame.height>4096 || (word(sub,address+4)&~0xe00) ||
                    frame.reverse!=frame.pitch*(frame.height-1) || word(sub,address+12) || word(sub,address+14))return;
                family.push_back(frame);
            }
            frames.insert(frames.end(),family.begin(),family.end());
        };
        add_family(root);
        // $99E4..$9A48 selects signed banks in 16-byte records. Both gentle
        // directions reuse this source, with request bit 13 controlling the
        // hardware source flip. Other pitch/bank roots remain unsupported.
        if(table>=16 && pointer(table-16)==0x10230 && pointer(table+16)==0x10230)
            add_family(pointer(table+16));
    }
    bool available()const{return !frames.empty();}

    std::optional<PlayerAircraftDraw> match(const std::array<uint16_t,8>& d,
            unsigned top,unsigned bottom,int anchor=INT32_MIN,
            std::span<const unsigned> supported_roots={})const {
        if((d[0]&0xd000) || (d[6]&255)!=0xf2 || (d[3]&0xc800) || (d[4]&0x0800) ||
            top>bottom || bottom>=224)return {};
        const int pitch=int16_t((d[2]>>1)|((d[4]&0x1000)<<3))>>8;
        const bool backwards=(d[4]&0x4000)==0,negative_pitch=pitch<0;
        std::optional<PlayerAircraftDraw> candidate;
        for(const auto& frame:frames){
            if(supported_roots.empty()?frame.root!=0x100a0:
                std::find(supported_roots.begin(),supported_roots.end(),frame.root)==supported_roots.end())continue;
            if(((d[0]>>9)&7)!=frame.bank || unsigned(pitch<0?-pitch:pitch)!=frame.pitch)continue;
            const unsigned start=uint16_t(frame.offset+(backwards?frame.pitch-1:0)+(negative_pitch?frame.reverse:0));
            // Adjusted offscreen source starts are not yet mapped to authored
            // art. Retain the original descriptor rather than guessing a crop.
            if(d[1]!=start)continue;
            const unsigned hz=std::max(64u,unsigned(d[4]&2047)),vz=std::max(64u,unsigned(d[3]&2047));
            const unsigned rows=(d[5]&4095)+1;
            if(((rows-1)*vz>>9)>=frame.height)return {};
            const unsigned width=(frame.width*512+hz-1)/hz,height=(frame.height*512+vz-1)/vz;
            const bool leftwards=(d[4]&0x2000)==0,upwards=(d[4]&0x8000)==0;
            int x=d[2]&511;if(x<128&&leftwards)x+=512;x-=190;
            if(anchor!=INT32_MIN && anchor!=x)return {};
            const int y=int(d[0]&511)-256,first=upwards?y-int(rows)+1:y,last=upwards?y:y+int(rows)-1;
            const int clip_top=std::max(int(top),first),clip_bottom=std::min(int(bottom),last);
            if(clip_top>clip_bottom)return {};
            // Aliased source records cannot identify one authored pose safely.
            if(candidate)return {};
            candidate=PlayerAircraftDraw{frame.address,frame.width,frame.height,
                leftwards?x-int(width)+1:x,upwards?y-int(height)+1:y,width,height,
                unsigned(clip_top),unsigned(clip_bottom),backwards!=leftwards,negative_pitch!=upwards,0,frame.root};
        }
        return candidate;
    }
};
}
