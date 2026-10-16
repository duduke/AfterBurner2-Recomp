#pragma once
#include "hwvideo/scene_snapshot.hpp"
#include "hwvideo/ground_horizon.hpp"
#include "hwvideo/hwroad.hpp"
#include "hwvideo/hwpalette.hpp"
#include "roms.hpp"
#include <algorithm>
#include <vector>

namespace Presentation {
// Separate read-only presentation renderer. Never invokes HWSprites or changes
// native buffers/feedback. The original scene remains the gameplay owner.
class WideScene {
public:
    struct View {
        unsigned width{},height=224,origin{};
        std::span<const uint32_t> background,sprites,heads;
        std::span<const uint8_t> protection;
        std::span<const Fragment> fragments;
        bool ready{};std::span<const uint16_t> ground_indices;
    };
    explicit WideScene(const Roms& rom,unsigned width=398):w(width),margin((width-320)/2),
        road(0x1700,0x1720,0x1780,-166),palette(8192),
        assets(std::span(rom.maincpu.rom,rom.maincpu.length),std::span(rom.subcpu.rom,rom.subcpu.length)),
        ground(std::span(rom.maincpu.rom,rom.maincpu.length),std::span(rom.subcpu.rom,rom.subcpu.length)),
        sprite_rom(rom.sprites.rom,rom.sprites.length) {
        if(w<320 || w>640 || w%2)throw std::invalid_argument("Unsupported wide scene width");
        road.init(rom.road.rom,rom.road.length);tiles.init_gfx(rom.tiles.rom,rom.tiles.length);reset();
    }
    void reset(){
        ground_indices.assign(w*224,0);indices.assign(w*224,0);priority.assign(w*224,0);raw.assign(w*224,0xffff);
        background.assign(w*224,0xff000000);sprites.assign(w*224,0);protection.assign(w*224,0);
        for(auto& r:row_fragments)r.clear();row_heads.assign(w*224,UINT32_MAX);heads.assign(w*224,UINT32_MAX);seen.fill(false);
        fragments.clear();last_frame=0;last_ns=0;loaded_tile_gen=loaded_palette_gen=0;ready=false;
    }
    void render(std::span<const SceneClip> clips,uint64_t frame,bool corrected_ground,bool extend_visibility=true){
        if(frame==last_frame || clips.empty())return;
        if(frame<last_frame)reset();last_frame=frame;
        const SceneClip* prev=nullptr;
        for(const auto& c:clips){
            if(c.top>c.bottom || c.bottom>=224 || c.ns<last_ns)throw std::invalid_argument("Unordered wide scene clips");
            last_ns=c.ns;draw(c,corrected_ground,extend_visibility,prev);
            if(c.display)prev=&c;
        }
        ready=std::all_of(seen.begin(),seen.end(),[](bool x){return x;});flatten();
    }
    View view()const{return {w,224,margin,background,sprites,heads,protection,fragments,ready,ground_indices};}
private:
    unsigned w,margin;HWRoad road;HWTiles tiles;HWPalette palette;
    EffectAssets assets;GroundHorizon ground;std::span<const uint8_t> sprite_rom;
    std::vector<uint16_t> ground_indices,indices,raw,text_indices=std::vector<uint16_t>(320*224);
    std::vector<uint8_t> priority,protection,text_priority=std::vector<uint8_t>(320*224),projected;
    std::vector<int> anchors;
    std::vector<uint32_t> background,sprites,row_heads,heads;
    std::vector<Fragment> fragments;std::array<std::vector<Fragment>,224> row_fragments;
    std::array<bool,224> seen{};uint64_t last_frame{},last_ns{},loaded_tile_gen{},loaded_palette_gen{};bool ready{};
    static uint32_t argb(RGB888 p){return 0xff000000|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b;}
    void draw(const SceneClip& c,bool corrected,bool extend_visibility,const SceneClip* prev=nullptr){
        projected.clear();anchors.clear();std::span<const uint8_t> bank=c.sprites;
        if(w>320&&extend_visibility&&!c.extended_sprites.empty()){
            projected.resize((c.extended_sprites.size()+1)*16);anchors.assign(c.extended_sprites.size()+1,INT32_MIN);
            for(unsigned i=0;i<c.extended_sprites.size();++i){std::copy(c.extended_sprites[i].bytes.begin(),c.extended_sprites[i].bytes.end(),projected.begin()+i*16);anchors[i]=c.extended_sprites[i].x;}
            projected[c.extended_sprites.size()*16]=0x80;bank=projected;
        }
        for(unsigned y=c.top;y<=c.bottom;++y){seen[y]=true;row_fragments[y].clear();
            std::fill(row_heads.begin()+y*w,row_heads.begin()+(y+1)*w,UINT32_MAX);}
        if(!c.display){for(unsigned i=c.top*w;i<(c.bottom+1)*w;++i){background[i]=0xff000000;sprites[i]=0;protection[i]=0;}return;}
        if(c.tile_gen ? loaded_tile_gen!=c.tile_gen : (!prev || prev->tiles!=c.tiles)){
            for(unsigned i=0;i<c.tiles.size();++i)tiles.write_tile(i,c.tiles[i]);
            loaded_tile_gen=c.tile_gen;
        }
        for(unsigned i=0;i<c.text.size();++i)tiles.write_text(i,c.text[i]);
        tiles.load_initial_frame_registers(c.registers);
        if(c.palette_gen ? loaded_palette_gen!=c.palette_gen : (!prev || prev->palette!=c.palette)){
            for(unsigned i=0;i<c.palette.size();++i)palette.write(i,c.palette[i]);
            loaded_palette_gen=c.palette_gen;
        }
        if(!prev || prev->road!=c.road || prev->road_control!=c.road_control){
            for(unsigned i=0;i<0x800;++i)road.write16(2*i,uint16_t(c.road[2*i]<<8|c.road[2*i+1]));
            road.read_road_control();road.write_road_control(c.road_control);
        }
        std::fill(priority.begin()+c.top*w,priority.begin()+(c.bottom+1)*w,0);
        std::fill(raw.begin()+c.top*w,raw.begin()+(c.bottom+1)*w,0xffff);
        road.render_background(indices.data(),w,224,c.top,c.bottom);road.render_foreground(indices.data(),w,224,c.top,c.bottom,-int(margin));
        tiles.draw_layer(indices.data(),priority.data(),w,224,1,-1,1,c.top,c.bottom,-int(margin));
        tiles.draw_layer(indices.data(),priority.data(),w,224,0,-1,2,c.top,c.bottom,-int(margin));
        // Text/HUD remains in the native safe area, without duplicated side tiles.
        std::fill(text_priority.begin()+c.top*320,text_priority.begin()+(c.bottom+1)*320,0);
        tiles.draw_text(text_indices.data(),text_priority.data(),320,224,-1,4,c.top,c.bottom);
        for(unsigned y=c.top;y<=c.bottom;++y)for(unsigned x=0;x<320;++x){unsigned a=y*320+x,b=y*w+x+margin;
            if(text_priority[a]){indices[b]=text_indices[a];priority[b]|=text_priority[a];}}
        if(corrected)ground.apply(indices,priority,c.road,c.road_control,bank,sprite_rom,c.top,c.bottom,uint8_t(c.roll),assets,w,-int(margin),anchors);
        std::copy(indices.begin()+c.top*w,indices.begin()+(c.bottom+1)*w,ground_indices.begin()+c.top*w);
        const bool boost=full_throttle_flame(c.mode,c.throttle);
        decode(bank,sprite_rom,c.top,c.bottom,assets,[&](int x,unsigned y,uint16_t word,Effect kind){
            const unsigned i=y*w+unsigned(x+int(margin));raw[i]=word;
            if(kind==Effect::afterburner&&!boost)kind=Effect::opaque;
            const bool visible=(1<<((word>>12)&3))>priority[i];
            const unsigned pen=!visible?indices[i]:(word&0x400f)==0x400a?indices[i]+palette.num_entries():word&0xfff;
            auto& row=row_fragments[y];row.push_back({argb(palette.pen(pen)),row_heads[i],uint32_t(!visible?Effect::background:(word&0x400f)==0x400a?Effect::shadow:kind),word});row_heads[i]=uint32_t(row.size()-1);
        },-int(margin),int(w-margin)-1,anchors);
        for(unsigned i=c.top*w;i<(c.bottom+1)*w;++i){
            background[i]=argb(palette.pen(indices[i]));const auto word=raw[i];
            const bool visible=word!=0xffff&&(1<<((word>>12)&3))>priority[i];
            const bool shadow=visible&&(word&0x400f)==0x400a;
            protection[i]=(!visible&&priority[i])||shadow;sprites[i]=visible&&!shadow?argb(palette.pen(word&0xfff)):0;
            if(shadow){indices[i]+=palette.num_entries();background[i]=argb(palette.pen(indices[i]));}
            else if(visible)indices[i]=word&0xfff;
        }
    }
    void flatten(){
        std::copy(row_heads.begin(),row_heads.end(),heads.begin());
        size_t total=0;for(const auto& r:row_fragments)total+=r.size();
        fragments.resize(total);
        unsigned base=0;
        for(unsigned y=0;y<224;++y){
            const auto& r=row_fragments[y];if(r.empty())continue;
            auto* dst=fragments.data()+base;const unsigned n=unsigned(r.size());
            for(unsigned j=0;j<n;++j){auto f=r[j];if(f.previous!=UINT32_MAX)f.previous+=base;dst[j]=f;}
            if(base){auto* h=heads.data()+y*w;for(unsigned x=0;x<w;++x)if(h[x]!=UINT32_MAX)h[x]+=base;}
            base+=n;
        }
    }
};
}
