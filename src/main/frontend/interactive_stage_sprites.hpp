#pragma once

#include <SDL3/SDL.h>
#include "roms.hpp"
#include "frontend/custom_stage_scenery.hpp"
#include "frontend/custom_stage_guidance.hpp"
#include "frontend/custom_stage_shadow.hpp"
#include "frontend/custom_stage_hud.hpp"
#include "engine/sprite_frame.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/hwpalette.hpp"
#include <memory>
#include <stdexcept>
#include <vector>
#include <cmath>
#include <array>
#include <algorithm>

namespace Frontend {
// Presentation-only ROM assets. Use the existing verified hardware decoder and
// palette DAC once at startup, then cache native-resolution SDL textures.
// Position, scale and rotation below belong to the authored practice scene.
class InteractiveStageSprites {
    struct Image {
        std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> texture{nullptr,SDL_DestroyTexture};
        float width{},height{};StagePoint top_anchor{},tip{},docking_anchor{};std::array<StagePoint,2> engines{};
    };
    std::unique_ptr<CustomStageHud> interface;
    Image neutral,banked,carrier,tanker_body,tanker_wing,hose,crew,autumn_tree,palms,palms_tilt,refuel_jet,gear_plane,service_left,service_right;
    std::vector<Image> chase_bike,chase_car;
    std::array<std::vector<Image>,5> pitch_poses,refuel_pitch_poses;Image tire_smoke,player_shadow_art;
    std::vector<Image> waves;std::vector<unsigned> wave_roots;
    std::vector<Image> flames;
    unsigned takeoff_flame{};
    SDL_Renderer* renderer;
    const Roms& assets;
    HWSprites scaler;
    struct Scenery {Image image;unsigned root,palette;float x,z;bool flip,air;};
    std::array<std::vector<Scenery>,4> scenery;
    struct NativePlacement{size_t art;float x,z;bool flip;};
    std::array<std::vector<NativePlacement>,4> native_layout;
    std::array<bool,4> layout_ready{};
    Image frame(unsigned address,unsigned palette,unsigned palette_source=0,const HWPalette* live=nullptr,bool water=false,bool smoke=false){
        const auto& rom=assets;
        if(address+16>rom.subcpu.length)throw std::runtime_error("Practice frame outside Sub ROM");
        const auto f=read_sprite_frame(rom.subcpu.rom,rom.subcpu.length,address);
        if(!f.width || f.width>1024 || !f.height || f.height>512 || !f.pitch || f.pitch>127 || f.bank>7)
            throw std::runtime_error("Invalid practice sprite frame");
        // Main $13B1C copies palettes from $2EDF4; $13F42 supplies F2's
        // fourteen player colors from $2BF60 (pen 1 onward).
        const unsigned source=palette_source?palette_source:0x2edf4+palette*32;
        if(source+32>rom.maincpu.length)throw std::runtime_error("Practice palette outside Main ROM");
        HWPalette colors(16);
        for(unsigned pen=1;pen<15;++pen)
            colors.write(pen,rom.maincpu.read16(source+(palette_source?(pen-1)*2:pen*2)));
        scaler.write16(0,0x100|(f.bank<<9));scaler.write16(2,f.offset);
        scaler.write16(4,190|((f.pitch&127)<<9));scaler.write16(6,0x200);
        scaler.write16(8,0xe200);scaler.write16(10,f.height-1);scaler.write16(12,0);
        scaler.write16(16,0x8000);scaler.swap();
        std::vector<uint16_t> pens(f.width*f.height);
        scaler.render(pens.data(),f.width,f.height);
        std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> surface(SDL_CreateSurface(f.width,f.height,SDL_PIXELFORMAT_ARGB8888),SDL_DestroySurface);
        if(!surface)throw std::runtime_error(SDL_GetError());
        int first_row=f.height,last_row=-1,first_left=0,first_right=0,last_left=0,last_right=0;
        unsigned center_row=f.height,center_x=f.width/2;
        double red_x[2]{},red_y[2]{};unsigned red_n[2]{};
        for(unsigned y=0;y<f.height;++y){auto* row=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(surface->pixels)+y*surface->pitch);
            for(unsigned x=0;x<f.width;++x){const unsigned pen=pens[y*f.width+x]&15;
                const auto c=live?live->pen(palette*16+pen):colors.pen(pen);row[x]=pen && pen!=15?0xff000000u|(unsigned(c.r)<<16)|(unsigned(c.g)<<8)|c.b:0;
                if(water && c.b>c.g*1.7 && c.b>c.r*1.7)row[x]=0;
                if(smoke && (row[x]>>24)){const unsigned gray=std::max({c.r,c.g,c.b});row[x]=0xff000000u|gray*0x010101u;}
                if(pen && pen!=15){
                    // The first opaque centerline pixel is canopy/nose, not
                    // the empty midpoint between the two tall tail fins.
                    if(std::abs(int(x)-int(f.width/2))<=1 && y<center_row){center_row=y;center_x=x;}
                    if(int(y)<first_row){first_row=y;first_left=first_right=x;}else if(int(y)==first_row)first_right=x;
                    if(int(y)>last_row){last_row=y;last_left=last_right=x;}else if(int(y)==last_row)last_right=x;
                    if(c.r>100 && c.r>c.g*1.6 && c.r>c.b*1.6){const auto side=x>=f.width/2;red_x[side]+=x;red_y[side]+=y;++red_n[side];}
                }}}
        Image image;image.width=f.width;image.height=f.height;
        image.docking_anchor={double(center_x)-f.width*.5,double(center_row)-f.height*.5};
        image.top_anchor={(first_left+first_right)*.5-f.width*.5,first_row-f.height*.5};
        image.tip={last_left+.5-f.width*.5,last_row+.5-f.height*.5};
        for(unsigned side=0;side<2;++side)image.engines[side]=red_n[side]?StagePoint{red_x[side]/red_n[side]-f.width*.5,red_y[side]/red_n[side]-f.height*.5}:StagePoint{(side?1:-1)*f.width*.14,f.height*.22};
        image.texture.reset(SDL_CreateTextureFromSurface(renderer,surface.get()));
        if(!image.texture || !SDL_SetTextureScaleMode(image.texture.get(),SDL_SCALEMODE_NEAREST) ||
           !SDL_SetTextureBlendMode(image.texture.get(),SDL_BLENDMODE_BLEND))throw std::runtime_error(SDL_GetError());
        return image;
    }
public:
    InteractiveStageSprites(SDL_Renderer* renderer,const Roms& rom,bool custom_art=false):renderer(renderer),assets(rom) {
        scaler.init(rom.sprites.rom,rom.sprites.length);
        // Main $99E4/$28C94: neutral-pitch bank records. Keep the verified
        // neutral/gentle families; mirror the gentle source for opposite bank.
        const auto table=rom.maincpu.read32(0x28c94+2*4);
        if(table<16 || table+20>rom.maincpu.length)throw std::runtime_error("Invalid practice aircraft table");
        neutral=frame(rom.maincpu.read32(table),0xf2,0x2bf60);
        banked=frame(rom.maincpu.read32(table+16),0xf2,0x2bf60);
        // $108AC: carrier hull frame and palette. $16892/$168C8: tanker
        // fuselage and mirrored wing parts, each with its native palette.
        carrier=frame(0x19c70,0x6f);
        tanker_body=frame(0x1d770,0x46);
        tanker_wing=frame(0x1d780,0x70);
        if(custom_art){
        interface=std::make_unique<CustomStageHud>(renderer,rom);
        gear_plane=frame(0x10140,0xf2,0x2bf60);
        // Project the F14's own opaque silhouette; keep its softened texture
        // separate so the aircraft itself retains nearest-neighbor ROM art.
        player_shadow_art=frame(0x10140,0xf2,0x2bf60);
        SDL_SetTextureScaleMode(player_shadow_art.texture.get(),SDL_SCALEMODE_LINEAR);
        // Main $119A2/$11A1E: both main-wheel clouds use this frame/palette.
        tire_smoke=frame(0x164b0,0xef,0,nullptr,false,true);
        for(unsigned pitch=0;pitch<5;++pitch){const auto row=rom.maincpu.read32(0x28c94+pitch*4);
            for(unsigned bank=0;bank<=5;++bank)pitch_poses[pitch].push_back(frame(rom.maincpu.read32(row+bank*16),0xf2,0x2bf60));}
        service_left=frame(rom.maincpu.read32(0x12a2a+8),rom.maincpu.rom[0x12a2a+12]);
        service_right=frame(rom.maincpu.read32(0x12a3a+8),rom.maincpu.rom[0x12a3a+12]);
        for(unsigned i=0;i<3;++i){chase_bike.push_back(frame(rom.maincpu.read32(0x130c0+i*8),rom.maincpu.rom[0x130c0+i*8+4]));
            chase_car.push_back(frame(rom.maincpu.read32(0x130d8+i*8),rom.maincpu.rom[0x130d8+i*8+4]));}
        // Original wide-stage tree families and the same water table consumed
        // by WideVisibility. These are cached once and freely instanced.
        const auto refuel_table=rom.maincpu.read32(0x16b9e + 2*4);
        refuel_jet=frame(rom.maincpu.read32(refuel_table),rom.maincpu.rom[0x16b8a]);
        autumn_tree=frame(0x1daa0,0x12);palms=frame(0x140f0,0xd6);palms_tilt=frame(0x14140,0xd6);
        const unsigned count=rom.maincpu.rom[0x3f918];
        if(!count || count>16)throw std::runtime_error("Invalid native water animation table");
        for(unsigned i=0;i<count;++i){const auto root=rom.maincpu.read32(0x3f91c+i*4)&0xffffff;
            wave_roots.push_back(root);waves.push_back(frame(root,0x23));}
        // Main $1686A selects the deployed apparatus from this ROM table.
        hose=frame(rom.maincpu.read32(0x166ce + 9*8+2),0x8c);
        crew=frame(rom.maincpu.read32(0x12a1a),rom.maincpu.rom[0x12a2a+2*16+12]);
        for(unsigned i=0;i<5;++i){const unsigned p=0x12416+i*10;
            flames.push_back(frame(rom.maincpu.read32(p),0x80));
        }
        // Main $11318 sets the takeoff exhaust selector to a byte offset
        // into $12416. These five geometries are not a clock-driven flipbook.
        const auto offset=rom.maincpu.read16(0x1131a);
        if(offset%10 || offset/10>=flames.size())throw std::runtime_error("Invalid native takeoff exhaust selector");
        takeoff_flame=offset/10;
        }
    }
    CustomStageHud* hud()const{return interface.get();}
    void refuel_colors(const HWPalette& live){
        const auto table=assets.maincpu.read32(0x16b9e + 2*4);
        refuel_jet=frame(assets.maincpu.read32(table),assets.maincpu.rom[0x16b8a],0,&live);
        for(unsigned pitch=0;pitch<5;++pitch){const auto row=assets.maincpu.read32(0x28c94+pitch*4);
            refuel_pitch_poses[pitch].clear();for(unsigned bank=0;bank<=5;++bank)refuel_pitch_poses[pitch].push_back(frame(assets.maincpu.read32(row+bank*16),assets.maincpu.rom[0x16b8a],0,&live));}
    }
    void reset_tanker_scenery(){scenery[3].clear();native_layout[3].clear();layout_ready[3]=false;}
    void cache_scenery(unsigned environment,unsigned root,unsigned palette,float x,float z,bool flip,bool air){
        auto& list=scenery.at(environment);
        if(std::find(wave_roots.begin(),wave_roots.end(),root)!=wave_roots.end())return;
        if(std::any_of(list.begin(),list.end(),[&](const auto& v){return v.root==root && v.palette==palette;}))return;
        if(root<0x10000 || root+16>assets.subcpu.length)return;
        const auto f=read_sprite_frame(assets.subcpu.rom,assets.subcpu.length,root);
        if(!f.width || f.width>512 || !f.height || f.height>512 || !f.pitch || f.pitch>127)return;
        list.push_back({frame(root,palette),root,palette,x,z,flip,air});
    }
    void begin_native_layout(unsigned env){native_layout.at(env).clear();layout_ready.at(env)=true;}
    void native_prop(unsigned env,unsigned root,unsigned palette,float x,float z,bool flip){
        if(root==0x14140 || root==0x140f0 || root==0x1daa0)return;
        cache_scenery(env,root,palette,x,z,flip,false);const auto& list=scenery.at(env);
        for(size_t n=0;n<list.size();++n)if(list[n].root==root && list[n].palette==palette){native_layout.at(env).push_back({n,x,z,flip});break;}
    }
    size_t native_prop_count(unsigned env)const{return native_layout.at(env).size();}
    size_t scenery_count(unsigned env)const{return scenery.at(env).size()+(env==0?waves.size():env==1?1:env==2?2:0);}
    struct SceneryCounts{unsigned drawn{},trees{},waves{},aircraft{};};
    template<class Camera>
    SceneryCounts draw_scenery(unsigned env,const Camera& c,double near,double /*length*/,double ground,double width,bool sea,unsigned ticks,const Camera* flight_camera=nullptr)const{
        struct Placed{const Image* art;double x,z,height,span;bool flip;unsigned type;};std::vector<Placed> placed;
        // No native slot/bucket limit. The field covers the complete frustum.
        std::vector<const Image*> refuel_ground;
        if(env==3)for(const auto& v:scenery[3])if(!v.air)refuel_ground.push_back(&v.image);
        if(env<=3){for(const auto cell:CustomSceneryField::visible(c,near,env==3?0:width,sea,ground)){
            const Image* art=sea?&waves[(ticks/5+cell.variation)%waves.size()]:env==1?&autumn_tree:&palms;
            if(env==3){if(refuel_ground.empty())continue;art=refuel_ground[cell.variation%refuel_ground.size()];}
            const double span=sea?18.:env==1?19.:23.;
            const double scale=.88+(cell.variation%25)*.01;
            const double h=sea?ground:ground+span*scale*art->height/art->width/2;
            placed.push_back({art,cell.x,cell.z,h,span*scale,false,sea?2u:1u});
        }}
        if(layout_ready.at(env) && !sea)for(const auto v:native_layout.at(env)){
            const auto& art=scenery.at(env)[v.art].image;
            // Preserve native signed lateral positions and longitudinal order,
            // with one common native-unit-to-meter conversion. No repeats or
            // forced side offsets are applied to the original non-tree props.
            placed.push_back({&art,v.x/16.,near+v.z/16.,ground+art.height*.06,art.width*.12,v.flip,0});
        }

        std::sort(placed.begin(),placed.end(),[](auto a,auto b){return a.z>b.z;});SceneryCounts count;
        for(auto v:placed){const auto& projection=v.type==3 && flight_camera?*flight_camera:c;const auto p=projection.project(v.x,v.height,v.z);if(!p.visible)continue;
            const float span=float(c.span(v.span,v.z));
            const float native_height=span*v.art->height/v.art->width;
            const float vertical_scale=v.type==2?std::max(2.f/std::max(1.f,native_height),float(std::clamp((c.height+20-ground)/(v.z+c.behind)*.2,.025,.12))):1.f;
            const float height=span*v.art->height/v.art->width*vertical_scale;
            if(span<1 || (v.type==2 && (span<8 || p.y<c.horizon+32)) || p.x+span/2<40 || p.x-span/2>920 || p.y+height/2<78 || p.y-height/2>470)continue;
            if(v.type==2){SDL_SetTextureColorMod(v.art->texture.get(),255,255,255);
                SDL_SetTextureAlphaMod(v.art->texture.get(),Uint8(255*std::clamp((p.y-c.horizon-32)/16.,0.,1.)));}
            draw(*v.art,p.x,p.y,span,0,v.flip,vertical_scale);++count.drawn;if(v.type==1)++count.trees;if(v.type==2)++count.waves;if(v.type==3)++count.aircraft;
        }
        return count;
    }
    void draw(const Image& image,float x,float y,float width,double rotation=0,bool flip=false,float vertical_scale=1) const {
        const float height=width*image.height/image.width*vertical_scale;
        SDL_FRect destination{x-width*.5f,y-height*.5f,width,height};
        SDL_RenderTextureRotated(renderer,image.texture.get(),nullptr,&destination,rotation,nullptr,flip?SDL_FLIP_HORIZONTAL:SDL_FLIP_NONE);
    }
    void player(float x,float y,float bank) const {
        const bool turning=std::abs(bank)>.18f;
        draw(turning?banked:neutral,x,y,160,bank*12,turning && bank<0);
    }
    bool player_shadow(float x,float y,float width,double opacity,double softness,const std::array<StagePoint,4>& surface)const{
        if(!player_shadow_art.texture || opacity<1)return false;
        const float height=width*player_shadow_art.height/player_shadow_art.width*.65f;bool drawn=false;
        // Small normalized blur kernel softens the silhouette without an
        // offscreen hardware-sized buffer or an opaque painted oval.
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){const float cx=x+float(dx*softness),cy=y+float(dy*softness);
            const auto p=CustomStageShadow::clip({{{cx-width/2,cy-height/2},{0,0}},{{cx+width/2,cy-height/2},{1,0}},{{cx+width/2,cy+height/2},{1,1}},{{cx-width/2,cy+height/2},{0,1}}},surface);
            if(p.size()<3)continue;
            const float alpha=float(opacity/255*(dx==0?2:1)*(dy==0?2:1)/16);
            std::vector<SDL_Vertex> vertices;for(auto v:p)vertices.push_back({{float(v.point.x),float(v.point.y)},{0,0,0,alpha},{float(v.uv.x),float(v.uv.y)}});
            std::vector<int> indices;for(size_t i=1;i+1<p.size();++i){indices.push_back(0);indices.push_back(int(i));indices.push_back(int(i+1));}
            SDL_RenderGeometry(renderer,player_shadow_art.texture.get(),vertices.data(),int(vertices.size()),indices.data(),int(indices.size()));drawn=true;
        }
        return drawn;
    }
    unsigned last_pitch_pose=2,last_player_frame=0;double last_flame_error{};
    unsigned brake_smoke(float x,float y,double time)const{
        if(!tire_smoke.texture)return 0;
        unsigned count=0;for(int side:{-1,1})for(unsigned n=0;n<8;++n){const double age=time-n*.065;
            if(age<0 || age>=1.6)continue;
            const auto alpha=Uint8(150*std::pow(1-age/1.6,1.35));
            SDL_SetTextureAlphaMod(tire_smoke.texture.get(),alpha);
            const float width=float(12+age*52),xx=x+side*float(29+age*15),yy=y+float(19+age*22);
            draw(tire_smoke,xx,yy,width,0,side<0);++count;}
        SDL_SetTextureAlphaMod(tire_smoke.texture.get(),255);return count;
    }
    void fighter(float x,float y,float width,float bank,bool burner,unsigned clock,bool refueling=false,bool gear=false,float pitch=0,bool grounded=false) {
        const bool turning=!grounded && std::abs(bank)>.18f;
        const auto pose=CustomStageGuidance::pitch_pose(pitch);last_pitch_pose=pose;
        const auto bank_pose=unsigned(std::clamp(std::lround(std::abs(bank)*5),0l,5l));
        const auto& poses=refueling && !refuel_pitch_poses[pose].empty()?refuel_pitch_poses:pitch_poses;
        const bool native_pitch=!poses[pose].empty() && !grounded;
        const bool flip=bank<0 && (native_pitch?bank_pose>0:turning);
        const auto& image=grounded&&gear?gear_plane:native_pitch?poses[pose][bank_pose]:refueling?refuel_jet:turning?banked:neutral;
        last_player_frame=grounded&&gear?0x10140:native_pitch?assets.maincpu.read32(assets.maincpu.read32(0x28c94+pose*4)+bank_pose*16):0;
        const double scale=width/image.width,angle=bank*12;last_flame_error=0;
        if(burner){const auto& f=flames[takeoff_flame];
            // A restrained brightness pulse animates full power without moving
            // the nozzle/root or switching among unrelated plume geometries.
            SDL_SetTextureAlphaMod(f.texture.get(),Uint8(248+7*std::sin(clock*.22)));
            for(unsigned side=0;side<2;++side){auto engine=image.engines[side];if(flip)engine.x=-engine.x;
                const auto local=rotate_stage_point({engine.x*scale,engine.y*scale},angle);const StagePoint anchor{x+local.x,y+local.y};
                const bool flame_flip=(side==1)!=flip;const auto centre=anchored_stage_image(anchor,f.top_anchor,scale,angle,flame_flip);
                draw(f,centre.x,centre.y,f.width*scale,angle,flame_flip);
                auto root=f.top_anchor;if(flame_flip)root.x=-root.x;const auto restored=rotate_stage_point({root.x*scale,root.y*scale},angle);
                last_flame_error=std::max(last_flame_error,std::hypot(centre.x+restored.x-anchor.x,centre.y+restored.y-anchor.y));
            }
            SDL_SetTextureAlphaMod(f.texture.get(),255);}
        draw(image,x,y,width,angle,flip);
    }
    template<class Camera> void ground_service(const Camera& c,bool jungle,bool carrier,bool servicing,double time,double speed,unsigned ticks)const{
        auto sprite=[&](const Image& art,double x,double z,double w){const double h=w*art.height/art.width;auto p=c.project(x,h/2,z);if(p.visible)draw(art,p.x,p.y,float(c.span(w,z)));};
        if(!carrier && !servicing && speed>5){const auto& frames=jungle?chase_car:chase_bike;sprite(frames[std::min(2u,ticks/60)],-8,-3, jungle?3.8:1.4);}
        if(servicing){const double arrival=time<2?std::clamp(time/2,0.,1.):time>8?std::clamp((10-time)/2,0.,1.):1.;
            if(!carrier)sprite(service_right,28-16*arrival,5,8);
            sprite(crew,-6,4,1.1);sprite(crew,7,6,1.1);

        }
    }
    void ground_crew(float x,float y,float width)const{draw(crew,x,y,width);}
    void ship(float x,float y,float width) const {draw(carrier,x,y,width);}
    float carrier_contact_center(float player_y,float width,double progress)const{
        // Native stern-to-bow deck rows, measured in the original image. Keep
        // that artwork intact while the authored camera advances over it.
        const float deck_row=carrier.height*float(.70-.39*std::clamp(progress,0.,1.));
        return player_y+(carrier.height*.5f-deck_row)*width/carrier.width;
    }
    void tanker(float x,float y,float scale) const {
        // $167CE..$16846: all three parts share zoom. The +/-$208
        // projected X and +$50 world Y become approximately 130/20 source
        // pixels at unit zoom. $1690C flips the LEFT wing, not the right.
        draw(tanker_wing,x-130*scale,y+20*scale,192*scale,0,true);
        draw(tanker_wing,x+130*scale,y+20*scale,192*scale);
        draw(tanker_body,x,y,152*scale);
    }
    // Main $16848..$1688C: deployed table entry 9 supplies the vertical
    // offset; $16766 supplies X. Multiplier coordinates use four units/pixel.
    StagePoint hose_offset()const{return {int16_t(assets.maincpu.read16(0x16766+9*2))/4.,int16_t(assets.maincpu.read16(0x166ce + 9*8+6))/4.};}
    StagePoint tanker_tip()const{const auto p=hose_offset();return {p.x+hose.tip.x,p.y+hose.tip.y};}
    void refueling_hose(float x,float y,float scale)const{const auto p=hose_offset();draw(hose,x+p.x*scale,y+p.y*scale,hose.width*scale);}
    void refueling_tanker(float x,float y,float scale)const{tanker(x,y,scale);refueling_hose(x,y,scale);}
    StagePoint refuel_contact(float x,float y,float width,float bank,float pitch)const{
        const auto pose=CustomStageGuidance::pitch_pose(pitch),b=unsigned(std::clamp(std::lround(std::abs(bank)*5),0l,5l));
        const auto& poses=refuel_pitch_poses[pose].empty()?pitch_poses:refuel_pitch_poses;
        const auto& image=poses[pose].empty()?refuel_jet:poses[pose][b];
        auto anchor=image.docking_anchor;anchor.y+=1; // one native pixel of overlap at the aircraft's upper silhouette
        if(bank<0 && b>0)anchor.x=-anchor.x;
        const auto delta=rotate_stage_point({anchor.x*width/image.width,anchor.y*width/image.width},bank*12);
        return {x+delta.x,y+delta.y};
    }

};
}
