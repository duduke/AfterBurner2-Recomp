#pragma once
#include "wide_ground_reference.hpp"

// Independent marker-order witness for every measured row/column, without
// calling Profile::sky/ground or using the current roll / projected plane.
inline std::array<uint64_t,3> check_horizon_sides(
        const Presentation::SceneClip& clip,const Presentation::WideScene::View& before,
        const Presentation::WideScene::View& after,const Presentation::GroundHorizon& family,
        const Presentation::EffectAssets& assets){
    if(clip.road_control!=4)return {};
    struct Edge {
        int lo=10000,hi=-1,slo=10000,shi=-1,glo=10000,ghi=-1;
        void add(int p,unsigned pen){lo=std::min(lo,p);hi=std::max(hi,p);
            if(pen==1){slo=std::min(slo,p);shi=std::max(shi,p);}
            if(pen==14){glo=std::min(glo,p);ghi=std::max(ghi,p);}}
        int direction()const{return shi<0||ghi<0?0:shi<glo?1:ghi<slo?-1:0;}
    };
    const unsigned width=before.width;const int left=-int(before.origin);
    std::vector<uint8_t> bank;std::vector<int> anchors;
    if(clip.extended_sprites.empty())bank.assign(clip.sprites.begin(),clip.sprites.end());
    else {for(const auto& d:clip.extended_sprites){bank.insert(bank.end(),d.bytes.begin(),d.bytes.end());anchors.push_back(d.x);}
        bank.resize(bank.size()+16);bank[bank.size()-16]=0x80;anchors.push_back(INT32_MIN);}
    for(unsigned slot=0;slot<bank.size()/16;++slot){std::array<uint16_t,8> d{};
        for(unsigned w=0;w<8;++w)d[w]=uint16_t(bank[slot*16+2*w])<<8|bank[slot*16+2*w+1];
        if(d[0]&0x8000)break;if(!family.matches(d))bank[slot*16]|=0x50;
    }
    std::vector<uint8_t> pens(width*224);
    Presentation::decode(bank,{roms.sprites.rom,roms.sprites.length},0,223,assets,
        [&](int x,unsigned y,uint16_t raw,auto){pens[y*width+unsigned(x-left)]=raw&15;},left,left+int(width)-1,anchors);
    std::array<Edge,224> rows;std::vector<Edge> columns(width);
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<width;++x)if(const auto pen=pens[y*width+x]){rows[y].add(x,pen);columns[x].add(y,pen);}
    const auto donors=wide_ground_fill_witness(before,std::span(&clip,1));
    std::array<uint64_t,3> counts{};
    for(unsigned y=clip.top;y<=clip.bottom;++y){
        if((unsigned(clip.road[2*y])<<8|clip.road[2*y+1])&0xa00)continue;
        for(unsigned x=0;x<width;++x){const unsigned i=y*width+x,a=before.ground_indices[i],b=after.ground_indices[i];
            if(before.protection[i]||!((a>=0x1700&&a<0x1708)||(a>=0x1720&&a<0x1730)))continue;
            const auto& r=rows[y];const auto& c=columns[x];const int rx=r.direction(),cy=c.direction();
            if(!rx&&!cy)continue;
            const bool sky=(!rx||(rx>0?int(x)<r.lo:int(x)>r.hi))&&(!cy||(cy>0?int(y)<c.lo:int(y)>c.hi));
            const bool ground=(!rx||(rx>0?int(x)>r.hi:int(x)<r.lo))&&(!cy||(cy>0?int(y)>c.hi:int(y)<c.lo));
            if(sky){if(b<0x1720||b>=0x1730)throw std::runtime_error("stage 6 measured sky retained white at "+std::to_string(x)+","+std::to_string(y));++counts[0];}
            if(ground&&donors[y]){if(b<0x1700||b>=0x1708)throw std::runtime_error("stage 6 measured ground retained blue at "+std::to_string(x)+","+std::to_string(y));++counts[1];}
            else if(ground)++counts[2];
        }
    }
    return counts;
}
