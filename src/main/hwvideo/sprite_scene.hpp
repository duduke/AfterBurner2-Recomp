#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace Presentation {
enum class Effect : uint32_t { opaque, cloud, missile_trail, background, shadow, explosion, afterburner };
struct Fragment {uint32_t color{},previous=UINT32_MAX,kind{},raw{};};
// Classification is asset-based. Cloud silhouettes were inspected from ROM
// frame records $16500/$16550; $28C64 supplies player trail art and enemy
// $14EC8 uses the same $164B0 record. LOD offsets come from sub $9E4.
class EffectAssets {
    struct Range {unsigned bank,begin,end,pitch,color;Effect kind;bool operator==(const Range&)const=default;};
    std::vector<Range> ranges;
public:
    EffectAssets(std::span<const uint8_t> main,std::span<const uint8_t> sub){
        const auto word=[&](uint32_t a){if(a+1>=sub.size())throw std::runtime_error("Effect frame outside sub ROM");return unsigned(sub[a])<<8|sub[a+1];};
        std::vector<int> offsets{0};for(unsigned z=1;z<64;++z){int o=int16_t(word(0x9e4+8*z+4));if(std::find(offsets.begin(),offsets.end(),o)==offsets.end())offsets.push_back(o);}
        const auto family=[&](uint32_t frame,Effect kind,unsigned color=256){for(int o:offsets){const uint32_t a=frame+o;unsigned height=word(a+2)+1,bank=(word(a+4)>>9)&7,start=word(a+6),pitch=word(a+8);if(!pitch || height>4096 || start+pitch*height>65536)throw std::runtime_error("Invalid effect asset extent");Range range{bank,start,start+pitch*height,pitch,color,kind};if(std::find(ranges.begin(),ranges.end(),range)==ranges.end())ranges.push_back(range);}};
        if(main.size()<0x28c68)throw std::runtime_error("Effect pointer outside main ROM");
        uint32_t trail=0;for(unsigned i=0;i<4;++i)trail=trail<<8|main[0x28c64+i];
        family(trail&0xffffff,Effect::missile_trail);family(0x16500,Effect::cloud);family(0x16550,Effect::cloud);
        const auto mainword=[&](uint32_t a){if(a+1>=main.size())throw std::runtime_error("Effect table outside main ROM");return unsigned(main[a])<<8|main[a+1];};
        const auto pointer=[&](uint32_t a){return (mainword(a)<<16|mainword(a+2))&0xffffff;};
        // $90AC/$9306: ten explosion animation lists, including gate types.
        for(unsigned type=0;type<10;++type) {
            const auto table=pointer(0x347be + type*4);const auto count=mainword(table)+1;
            if(count>256)throw std::runtime_error("Invalid explosion animation length");
            for(unsigned n=0;n<count;++n){const auto entry=table+2+n*8;family(pointer(entry),Effect::explosion,mainword(entry+4)>>8);}
        }
        // $995C/$9780: five pitch tables, bank -5..5, sixteen-byte entries.
        // $14062 changes only palette F2 pens 1, 2 and 4 (the flame).
        for(unsigned pitch=0;pitch<5;++pitch)for(int bank=-5;bank<=5;++bank)
            family(pointer(pointer(0x28c94+pitch*4)+bank*16),Effect::afterburner,0xf2);

    }
    Effect identify(const std::array<uint16_t,8>& d)const{
        const unsigned bank=(d[0]>>9)&7,color=d[6]&255;
        const int pitch=int16_t((d[2]>>1)|((d[4]&0x1000)<<3))>>8;
        for(const auto& r:ranges)if(bank==r.bank && d[1]>=r.begin && d[1]<r.end && unsigned(std::abs(pitch))==r.pitch){
            if(r.color!=256 && r.color!=color)continue;
            if(r.kind==Effect::missile_trail && !((color>=0xe9&&color<=0xf0)||(color>=0xf5&&color<=0xfc)))continue;
            return r.kind;
        }
        return Effect::opaque;
    }
};
// Passive decoder of published descriptors; never writes the scaler's word-7
// feedback. Ordering, address wrapping, clipping and zoom follow HWSprites.
template<class Pixel>
void decode(std::span<const uint8_t> bank,std::span<const uint8_t> rom,unsigned top,unsigned bottom,
            const EffectAssets& assets,Pixel emit){
    if(bank.size()!=0x1000 || rom.empty() || rom.size()%0x40000 || top>bottom || bottom>=224)throw std::invalid_argument("Invalid sprite scene source");
    for(unsigned slot=0;slot<256;++slot){
        std::array<uint16_t,8>d{};for(unsigned i=0;i<8;++i)d[i]=uint16_t(bank[slot*16+i*2])<<8|bank[slot*16+i*2+1];
        if(d[0]&0x8000)break;if(d[0]&0x5000)continue;
        const auto kind=assets.identify(d);
        const int ystart=(d[0]&511)-256,yd=(d[4]&0x8000)?1:-1,xd=(d[4]&0x2000)?1:-1;
        int xs=d[2]&511;if(xs<128 && xd<0)xs+=512;xs-=190;
        const unsigned hz=std::max(64u,unsigned(d[4]&2047)),vz=std::max(64u,unsigned(d[3]&2047)),rows=(d[5]&4095)+1;
        const int pitch=int16_t((d[2]>>1)|((d[4]&0x1000)<<3))>>8;
        const unsigned rb=((d[0]>>9)&7)%(rom.size()/0x40000),flags=((d[6]&255)<<4)|(((d[3]>>12)&7)<<12);
        const bool flip=(~d[4]>>14)&1;
        for(unsigned row=0;row<rows;++row){int y=ystart+yd*int(row);if(y<int(top)||y>int(bottom))continue;
            uint16_t address=uint16_t(d[1]+pitch*int((row*vz)>>9));int x=xs;unsigned acc=0;
            while(xd>0?x<320:x>=0){
                const size_t offset=rb*0x40000+unsigned(address)*4;
                uint32_t word=uint32_t(rom[offset])|uint32_t(rom[offset+1])<<8|uint32_t(rom[offset+2])<<16|uint32_t(rom[offset+3])<<24;
                address=uint16_t(address+(flip?-1:1));
                bool last=false;
                for(unsigned k=0;k<8;++k){const unsigned pen=(word>>(flip?k*4:(7-k)*4))&15;if(k==6)last=pen==15;
                    while(acc<512){if(x>=0&&x<320&&pen!=0&&pen!=15)emit(unsigned(x),unsigned(y),uint16_t(flags|pen),kind==Effect::afterburner && pen!=1 && pen!=2 && pen!=4?Effect::opaque:kind);x+=xd;acc+=hz;}
                    acc-=512;
                }
                if(last)break;
            }
        }
    }
}
inline uint32_t foreground(std::span<const Fragment> fragments,uint32_t head,unsigned clouds,unsigned trails,uint32_t* background=nullptr,unsigned explosions=255,unsigned afterburner=255){
    unsigned trans=255,alpha=0;std::array<unsigned,3> rgb{};
    while(head!=UINT32_MAX && trans){const auto& f=fragments[head];
        if(f.kind==3 || f.kind==4){if(f.kind==4 && background)*background=f.color;break;}
        unsigned a=f.kind==1?clouds:f.kind==2?trails:f.kind==5?explosions:f.kind==6?afterburner:255;
        unsigned coverage=(a*trans+127)/255;alpha+=coverage;
        for(unsigned c=0;c<3;++c)rgb[c]+=(((f.color>>(c*8))&255)*coverage+127)/255;
        trans=(trans*(255-a)+127)/255;head=f.previous;
    }
    uint32_t result=std::min(alpha,255u)<<24;for(unsigned c=0;c<3;++c)result|=std::min(rgb[c],255u)<<(c*8);return result;
}
}
