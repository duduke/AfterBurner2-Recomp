#pragma once
#include "hwvideo/scene_snapshot.hpp"
#include "hwvideo/ground_horizon.hpp"
#include "hwvideo/hud_scene.hpp"
#include "hwvideo/player_aircraft.hpp"
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
        HudScene hud;
        PlayerAircraftScene player;
    };
    explicit WideScene(const Roms& rom,unsigned width=398):w(width),margin((width-320)/2),
        road(0x1700,0x1720,0x1780,-166),palette(8192),
        assets(std::span(rom.maincpu.rom,rom.maincpu.length),std::span(rom.subcpu.rom,rom.subcpu.length)),
        player_assets(std::span(rom.maincpu.rom,rom.maincpu.length),std::span(rom.subcpu.rom,rom.subcpu.length)),
        ground(std::span(rom.maincpu.rom,rom.maincpu.length),std::span(rom.subcpu.rom,rom.subcpu.length)),
        sprite_rom(rom.sprites.rom,rom.sprites.length) {
        if(w<320 || w>640 || w%2)throw std::invalid_argument("Unsupported wide scene width");
        road.init(rom.road.rom,rom.road.length);tiles.init_gfx(rom.tiles.rom,rom.tiles.length);reset();
    }
    void reset(){
        ground_indices.assign(w*224,0);indices.assign(w*224,0);priority.assign(w*224,0);raw.assign(w*224,0xffff);
        background.assign(w*224,0xff000000);sprites.assign(w*224,0);protection.assign(w*224,0);
        for(auto& r:row_fragments)r.clear();row_heads.assign(w*224,UINT32_MAX);heads.assign(w*224,UINT32_MAX);seen.fill(false);
        fragments.clear();hud_clips.clear();hud_raw.clear();hud_occlusion_pixels.clear();
        player_draws.clear();player_visibility.clear();player_bank.clear();
        last_frame=0;last_ns=0;loaded_tile_gen=loaded_palette_gen=0;ready=false;
    }
    void player_aircraft(bool enabled,unsigned translucent_effects=0,
            std::span<const unsigned> supported_roots={}){
        if(enabled!=player_enabled || translucent_effects!=player_translucent_effects ||
                !std::equal(supported_roots.begin(),supported_roots.end(),player_supported_roots.begin(),player_supported_roots.end())){
            player_enabled=enabled;player_translucent_effects=translucent_effects;
            player_supported_roots.assign(supported_roots.begin(),supported_roots.end());reset();
        }
    }
    void render(std::span<const SceneClip> clips,uint64_t frame,bool corrected_ground,bool extend_visibility=true,bool original_hud=true){
        if(clips.empty())return;
        if(frame<last_frame || original_hud!=last_original_hud)reset();
        if(frame==last_frame)return;
        last_frame=frame;last_original_hud=original_hud;hud_clips.clear();player_draws.clear();player_visibility.clear();
        if(!original_hud){hud_raw.resize(320*224,0xffff);hud_occlusion_pixels.resize(320*224,2);}
        const SceneClip* prev=nullptr;
        for(const auto& c:clips){
            if(c.top>c.bottom || c.bottom>=224 || c.ns<last_ns)throw std::invalid_argument("Unordered wide scene clips");
            last_ns=c.ns;draw(c,corrected_ground,extend_visibility,original_hud,prev);
            if(c.display)prev=&c;
        }
        ready=std::all_of(seen.begin(),seen.end(),[](bool x){return x;});flatten();
    }
    View view()const{return {w,224,margin,background,sprites,heads,protection,fragments,ready,ground_indices,{hud_clips,hud_occlusion_pixels},{player_draws,player_visibility}};}
private:
    unsigned w,margin;HWRoad road;HWTiles tiles;HWPalette palette;
    EffectAssets assets;PlayerAircraftAssets player_assets;GroundHorizon ground;std::span<const uint8_t> sprite_rom;
    std::vector<uint16_t> ground_indices,indices,raw,text_indices=std::vector<uint16_t>(320*224);
    std::vector<uint8_t> priority,protection,text_priority=std::vector<uint8_t>(320*224),projected;
    std::vector<int> anchors;
    std::vector<uint32_t> background,sprites,row_heads,heads;
    std::vector<Fragment> fragments;std::array<std::vector<Fragment>,224> row_fragments;
    std::vector<HudClip> hud_clips;
    std::vector<uint16_t> hud_raw;
    std::vector<uint8_t> hud_occlusion_pixels;
    std::vector<PlayerAircraftDraw> player_draws;
    std::vector<uint8_t> player_visibility,player_bank;
    bool player_enabled{};
    unsigned player_translucent_effects{};
    std::vector<unsigned> player_supported_roots;
    std::array<bool,224> seen{};uint64_t last_frame{},last_ns{},loaded_tile_gen{},loaded_palette_gen{};bool ready{};
    bool last_original_hud{true};
    static uint32_t argb(RGB888 p){return 0xff000000|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b;}
    void draw(const SceneClip& c,bool corrected,bool extend_visibility,bool original_hud,const SceneClip* prev=nullptr){
        projected.clear();anchors.clear();std::span<const uint8_t> bank=c.sprites;
        if(w>320&&extend_visibility&&!c.extended_sprites.empty()){
            projected.resize((c.extended_sprites.size()+1)*16);anchors.assign(c.extended_sprites.size()+1,INT32_MIN);
            for(unsigned i=0;i<c.extended_sprites.size();++i){std::copy(c.extended_sprites[i].bytes.begin(),c.extended_sprites[i].bytes.end(),projected.begin()+i*16);anchors[i]=c.extended_sprites[i].x;}
            projected[c.extended_sprites.size()*16]=0x80;bank=projected;
        }
        for(unsigned y=c.top;y<=c.bottom;++y){seen[y]=true;row_fragments[y].clear();
            std::fill(row_heads.begin()+y*w,row_heads.begin()+(y+1)*w,UINT32_MAX);}
        if(!c.display){for(unsigned i=c.top*w;i<(c.bottom+1)*w;++i){background[i]=0xff000000;sprites[i]=0;protection[i]=0;}
            if(!original_hud)std::fill(hud_occlusion_pixels.begin()+c.top*320,hud_occlusion_pixels.begin()+(c.bottom+1)*320,2);return;}
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
        if(!original_hud){
            auto& hud=hud_clips.emplace_back();hud.top=c.top;hud.bottom=c.bottom;
            for(unsigned y=0;y<28;++y)std::copy_n(c.text.begin()+y*64+24,40,hud.entries.begin()+y*40);
            for(unsigned i=0;i<64;++i){hud.colors[i]=argb(palette.pen(0x1c00+i));hud.colors[64+i]=argb(palette.pen(0x1c00+i+palette.num_entries()));}
            std::fill(hud_raw.begin()+c.top*320,hud_raw.begin()+(c.bottom+1)*320,0xffff);
            // Detached visibility must not change native text ownership. Decode
            // the original bank separately, retaining only its top raster word.
            decode(c.sprites,sprite_rom,c.top,c.bottom,assets,[&](unsigned x,unsigned y,uint16_t word,Effect){hud_raw[y*320+x]=word;});
            for(unsigned y=c.top;y<=c.bottom;++y)for(unsigned x=0;x<320;++x){const unsigned i=y*320+x;
                hud_occlusion_pixels[i]=hud_occlusion(hud.entries[(y/8)*40+x/8],hud_raw[i]);}
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
        // Replacement HUD owns text separately; its underlay and sprite
        // decisions must not contain the old glyphs or their priority marks.
        if(original_hud){
            std::fill(text_priority.begin()+c.top*320,text_priority.begin()+(c.bottom+1)*320,0);
            tiles.draw_text(text_indices.data(),text_priority.data(),320,224,-1,4,c.top,c.bottom);
            for(unsigned y=c.top;y<=c.bottom;++y)for(unsigned x=0;x<320;++x){unsigned a=y*320+x,b=y*w+x+margin;
                if(text_priority[a]){indices[b]=text_indices[a];priority[b]|=text_priority[a];}}
        }
        if(player_enabled&&!original_hud){
            // Detached text is drawn later, but still retains its original
            // hardware ownership over the authored aircraft's full rectangle.
            std::fill(text_priority.begin()+c.top*320,text_priority.begin()+(c.bottom+1)*320,0);
            tiles.draw_text(text_indices.data(),text_priority.data(),320,224,-1,4,c.top,c.bottom);
        }
        if(corrected)ground.apply(indices,priority,c.road,c.road_control,bank,sprite_rom,c.top,c.bottom,uint8_t(c.roll),assets,w,-int(margin),anchors);
        std::copy(indices.begin()+c.top*w,indices.begin()+(c.bottom+1)*w,ground_indices.begin()+c.top*w);
        const bool boost=full_throttle_flame(c.mode,c.throttle);
        if(player_enabled)bank=replace_player(bank,c.top,c.bottom,boost,original_hud);
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
    std::span<const uint8_t> replace_player(std::span<const uint8_t> bank,unsigned top,unsigned bottom,bool boost,bool original_hud){
        std::optional<PlayerAircraftDraw> candidate;unsigned selected=0,priority_mark=0;
        for(unsigned slot=0;slot<bank.size()/16;++slot){
            std::array<uint16_t,8>d{};for(unsigned i=0;i<8;++i)d[i]=uint16_t(bank[slot*16+i*2])<<8|bank[slot*16+i*2+1];
            if(d[0]&0x8000)break;
            const auto draw=player_assets.match(d,top,bottom,anchors.empty()?INT32_MIN:anchors[slot],player_supported_roots);
            if(!draw)continue;
            // Reused/ambiguous descriptors are kept native until their object
            // ownership is independently established.
            if(candidate)return bank;
            candidate=draw;selected=slot;priority_mark=1u<<((d[3]>>12)&3);
        }
        if(!candidate)return bank;
        // Detached HUD alpha ownership is based on the original aircraft and
        // glyphs. Authored glyphs may fill even a blank native cell, so a
        // priority-3 aircraft cannot safely change either silhouette here.
        // Lower aircraft priorities remain below both text categories.
        if(!original_hud && priority_mark>4)return bank;
        auto draw=*candidate;
        // The adapter's ownership mask covers only the native 320 columns.
        // Suppressing a descriptor that reaches a widescreen margin would
        // remove pixels the HD draw cannot restore. Keep Original until a
        // wider mask and matching frontend sampling are supported.
        if(draw.x<0 || draw.x+int(draw.width)>320)return bank;
        const int left=std::max(0,draw.x),right=std::min(320,draw.x+int(draw.width));
        if(left>=right)return bank;
        std::array<uint8_t,320*224> visibility{};
        for(unsigned y=draw.top;y<=draw.bottom;++y)for(int x=left;x<right;++x){
            const unsigned i=y*320+unsigned(x),wide=y*w+unsigned(x)+margin;
            visibility[i]=priority_mark>(priority[wide]|text_priority[i]);
        }
        bool unsupported_effect=false;
        const auto later=bank.subspan((selected+1)*16);
        if(!later.empty())decode(later,sprite_rom,top,bottom,assets,[&](int x,unsigned y,uint16_t word,Effect kind){
            if(x<left||x>=right||y<draw.top||y>draw.bottom)return;
            const unsigned i=y*320+unsigned(x);
            // A full-colour overlay has no hardware effects palette or later
            // translucent-fragment pass. Retain native aircraft when either
            // could overlap it, preserving every existing opacity option.
            if(kind==Effect::afterburner&&!boost)kind=Effect::opaque;
            if(visibility[i] && ((word&0x400f)==0x400a ||
                (player_translucent_effects&(1u<<unsigned(kind)))))unsupported_effect=true;
            visibility[i]=0;
        },-int(margin),int(w-margin)-1,anchors.empty()?std::span<const int>{}:std::span<const int>(anchors).subspan(selected+1));
        if(unsupported_effect || std::none_of(visibility.begin(),visibility.end(),[](uint8_t value){return value!=0;}))return bank;
        draw.mask_offset=player_visibility.size();player_draws.push_back(draw);
        player_visibility.insert(player_visibility.end(),visibility.begin(),visibility.end());
        player_bank.assign(bank.begin(),bank.end());player_bank[selected*16]|=0x40;
        return player_bank;
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
