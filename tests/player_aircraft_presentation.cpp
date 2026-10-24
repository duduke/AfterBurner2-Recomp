#ifndef AB2_PLAYER_AIRCRAFT_SYNTHETIC
#include "engine/game_session.hpp"
#include "frontend/native_player_aircraft.hpp"
#endif
#include "frontend/sprite_filter.hpp"
#include "hwvideo/hwsprites.hpp"
#include "hwvideo/player_aircraft.hpp"
#include "hwvideo/wide_scene.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <map>
#include <memory>
#include <string>

namespace {
constexpr unsigned width=320,height=224;
constexpr std::array<unsigned,2> registered_roots{0x100a0,0x10230};
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
uint32_t argb(RGB888 p){return 0xff000000u|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b;}
void word(uint8_t* bytes,unsigned at,uint16_t value){bytes[at]=value>>8;bytes[at+1]=value;}
void pointer(uint8_t* bytes,unsigned at,uint32_t value){word(bytes,at,value>>16);word(bytes,at+2,value);}
unsigned read_word(const uint8_t* bytes,unsigned at){return unsigned(bytes[at])<<8|bytes[at+1];}

// Deliberately generated records and graphics, independent of arcade data.
// The pointer shape follows the verified normal-flight producer, while the
// dimensions, colors and solid source pixels make ownership observable.
void synthetic_assets(Roms& rom){
    rom.maincpu.init(0x40000);rom.subcpu.init(0x20000);rom.tiles.init(0xc000);
    rom.road.init(0x10000);rom.sprites.init(0x40000);
    for(unsigned z=0;z<64;++z)word(rom.subcpu.rom,0x9e8+z*8,(z%5)*16);
    for(unsigned base:{0u,0x16500u,0x16550u})for(unsigned lod=0;lod<5;++lod){
        word(rom.subcpu.rom,base+lod*16+6,500);word(rom.subcpu.rom,base+lod*16+8,1);
    }
    for(unsigned variant=0;variant<2;++variant)pointer(rom.maincpu.rom,0xe89e + variant*4,0x1000);
    for(unsigned stream=0;stream<5;++stream)pointer(rom.maincpu.rom,0x1000+stream*4,0x1100);
    word(rom.maincpu.rom,0x1100,0xffff);
    for(unsigned phase=0;phase<2;++phase)for(unsigned pitch=0;pitch<5;++pitch)
        pointer(rom.maincpu.rom,(phase?0x2939c:0x29018)+pitch*4,0x1250);
    pointer(rom.maincpu.rom,0x28c94+2*4,0x3000);
    pointer(rom.maincpu.rom,0x3000,0x100a0);
    pointer(rom.maincpu.rom,0x2ff0,0x10230);pointer(rom.maincpu.rom,0x3010,0x10230);
    for(unsigned root:{0x100a0u,0x10230u})for(unsigned lod=0;lod<5;++lod){
        const unsigned a=root+lod*16,w=lod?8:16,h=lod?4:8,pitch=w/8,start=(root==0x10230?1024:0)+lod*32;
        word(rom.subcpu.rom,a,w);word(rom.subcpu.rom,a+2,h-1);word(rom.subcpu.rom,a+6,start);
        word(rom.subcpu.rom,a+8,pitch);word(rom.subcpu.rom,a+10,pitch*(h-1));
        for(unsigned y=0;y<h;++y)for(unsigned x=0;x<pitch;++x){
            const uint32_t pixels=x+1==pitch?0x111111f1:0x11111111;
            for(unsigned b=0;b<4;++b)rom.sprites.rom[(start+y*pitch+x)*4+b]=pixels>>(b*8);
        }
    }
    for(unsigned row=0;row<8;++row)for(unsigned x=0;x<2;++x){
        const uint32_t pixels=x?0xaaaaaafa:0xaaaaaaaa;
        for(unsigned b=0;b<4;++b)rom.sprites.rom[(256+row*2+x)*4+b]=pixels>>(b*8);
    }
    for(unsigned row=0;row<8;++row){
        const uint32_t pixels=0x111111f1;
        for(unsigned b=0;b<4;++b)rom.sprites.rom[(500+row)*4+b]=pixels>>(b*8);
    }
    for(unsigned row=0;row<8;++row)rom.tiles.rom[0x41*8+row]=0xff;
}

std::array<uint16_t,8> descriptor(const Roms& rom,unsigned frame,bool reverse=false,bool negative_pitch=false,bool negative_x=false,bool negative_y=false){
    const unsigned pitch=read_word(rom.subcpu.rom,frame+8),h=read_word(rom.subcpu.rom,frame+2)+1;
    const int signed_pitch=negative_pitch?-int(pitch):int(pitch);
    const unsigned start=read_word(rom.subcpu.rom,frame+6)+(reverse?pitch-1:0)+(negative_pitch?read_word(rom.subcpu.rom,frame+10):0);
    return {uint16_t(0x100+20),uint16_t(start),uint16_t((190+40)|((signed_pitch&127)<<9)),0x3200,
            uint16_t(0x200|(negative_pitch?0x1000:0)|(negative_x?0:0x2000)|(reverse?0:0x4000)|(negative_y?0:0x8000)),uint16_t(h-1),0xf2,0};
}
void put_sprite(Presentation::SceneClip& clip,unsigned slot,const std::array<uint16_t,8>& data){
    for(unsigned i=0;i<8;++i)word(clip.sprites.data(),slot*16+i*2,data[i]);
    word(clip.sprites.data(),(slot+1)*16,0x8000);
}

void matcher_fixtures(Roms& rom){
    Presentation::PlayerAircraftAssets assets({rom.maincpu.rom,rom.maincpu.length},{rom.subcpu.rom,rom.subcpu.length});
    unsigned matched=0;
    for(unsigned lod=0;lod<5;++lod)for(unsigned flags=0;flags<16;++flags){
        const auto d=descriptor(rom,0x100a0+lod*16,flags&1,flags&2,flags&4,flags&8);
        const auto got=assets.match(d,0,223);require(bool(got),"neutral family direction or LOD did not match");
        const unsigned sw=lod?8:16,sh=lod?4:8;
        require(got->frame_address==0x100a0+lod*16&&got->source_width==sw&&got->source_height==sh,"matcher changed stable frame identity or source extent");
        require(got->width==sw&&got->height==sh,"unscaled player extent changed");
        require(got->x==40-((flags&4)?int(sw-1):0)&&got->y==20-((flags&8)?int(sh-1):0),"player bounding origin lost hardware draw direction");
        require(got->flip_x==(bool(flags&1)!=bool(flags&4))&&got->flip_y==(bool(flags&2)!=bool(flags&8)),"player source flips disagree with hardware directions");
        ++matched;
    }
    constexpr std::array<unsigned,2> registered{0x100a0,0x10230};
    for(unsigned lod=0;lod<5;++lod)for(unsigned flags=0;flags<16;++flags){
        const auto d=descriptor(rom,0x10230+lod*16,flags&1,flags&2,flags&4,flags&8);
        require(!assets.match(d,0,223),"unregistered gentle bank lost Original fallback");
        const auto got=assets.match(d,0,223,INT32_MIN,registered);
        require(got&&got->root_frame_address==0x10230&&got->frame_address==0x10230+lod*16,
            "gentle-bank ROM family identity changed");
        require(got->source_width==(lod?8u:16u)&&got->source_height==(lod?4u:8u),
            "gentle-bank LOD source extent changed");
        require(got->flip_x==(bool(flags&1)!=bool(flags&4))&&got->flip_y==(bool(flags&2)!=bool(flags&8)),
            "gentle-bank source flips disagree with hardware directions");
        ++matched;
    }
    const auto gentle=descriptor(rom,0x10230);
    constexpr std::array<unsigned,1> missing{0x10400};
    require(!assets.match(gentle,0,223,INT32_MIN,missing),"missing authored pose lost Original fallback");
    // Either ROM-side bank pointer disagreement makes that family unsupported.
    // The neutral family remains available and unchanged.
    for(unsigned address:{0x2ff0u,0x3010u}){
        pointer(rom.maincpu.rom,address,0x10400);
        Presentation::PlayerAircraftAssets invalid({rom.maincpu.rom,rom.maincpu.length},{rom.subcpu.rom,rom.subcpu.length});
        require(!invalid.match(gentle,0,223,INT32_MIN,registered),"inconsistent gentle-bank ROM pointers were accepted");
        require(bool(invalid.match(descriptor(rom,0x100a0),0,223)),"bad gentle-bank pointers disabled valid neutral family");
        pointer(rom.maincpu.rom,address,0x10230);
    }
    auto d=descriptor(rom,0x100a0);
    const auto clipped=assets.match(d,23,25);require(clipped&&clipped->top==23&&clipped->bottom==25&&clipped->y==20,"partial-render clipping moved the player origin");
    const auto anchored=assets.match(d,0,223,40);require(anchored&&anchored->x==40,"native-aligned detached anchor did not match");
    require(!assets.match(d,0,223,200),"shifted detached anchor lost safe Original fallback");
    auto scaled=d;scaled[3]=0x3300;scaled[4]=(scaled[4]&0xf800)|0x300;scaled[5]=5;
    const auto zoomed=assets.match(scaled,0,223);require(zoomed&&zoomed->width==11&&zoomed->height==6,"player fractional zoom extent rounded incorrectly");
    for(unsigned change=0;change<7;++change){
        auto bad=d;
        if(change==0)bad[6]=0x80;
        if(change==1)bad[0]|=0x200;
        if(change==2)++bad[1];
        if(change==3)bad[2]+=0x200;
        if(change==4)bad[0]|=0x4000;
        if(change==5)bad[0]|=0x8000;
        if(change==6)bad[1]+=2; // a native clipped start is outside this prototype
        require(!assets.match(bad,0,223),"nonmatching descriptor lost Original fallback");
    }
    require(!assets.match(d,0,10),"off-clip player produced a draw");
    std::cout<<"Player matcher: "<<matched<<" generated neutral/gentle-bank LOD/direction cases, ROM bank-pointer agreement, registered-pose fallback, fractional zoom, clipping, detached anchor and seven Original fallbacks passed.\n";
}

std::vector<uint32_t> compose(Presentation::WideScene::View view){
    Frontend::SpriteScaler scaler;
    const auto out=scaler.compose(view.background,view.sprites,view.width,height,Frontend::SpriteFilter::none,view.protection);
    return {out.begin(),out.end()};
}
void synthetic_scene_fixtures(Roms& rom){
    for(unsigned viewport:{320u,358u,398u}){
        Presentation::SceneClip c;c.display=true;c.bottom=223;c.ns=1;
        c.palette[0xf21]=0x7fff;c.palette[0xe81]=0x00f0;
        c.palette[0x1c09]=0x000f;
        auto d=descriptor(rom,0x100a0);put_sprite(c,0,d);
        auto original_owner=std::make_unique<Presentation::WideScene>(rom,viewport),replacement_owner=std::make_unique<Presentation::WideScene>(rom,viewport);
        auto& original=*original_owner;auto& replacement=*replacement_owner;
        replacement.player_aircraft(true);
        original.render(std::span(&c,1),1,false,false);replacement.render(std::span(&c,1),1,false,false);
        const auto base=compose(original.view()),under=compose(replacement.view());
        require(replacement.view().player.draws.size()==1,"single neutral descriptor was not detached");
        require(base!=under,"detachment did not suppress the original aircraft");
        const auto& draw=replacement.view().player.draws.front();
        const auto mask=replacement.view().player.visibility;
        require(mask[draw.mask_offset+20*320+40]==1&&mask[draw.mask_offset+20*320+54]==1,"HD rectangle lost opaque or transparent native pixels");
        auto restored=under;
        // The generated airframe has fifteen pen-1 pixels per row, with one
        // transparent marker at source x=14. Restore that known shape without
        // invoking either sprite decoder or the new descriptor matcher.
        for(unsigned y=20;y<28;++y)for(unsigned x=40;x<56;++x)if(x!=54)
            restored[y*viewport+(viewport-320)/2+x]=0xffffffff;
        require(restored==base,"generated source reconstruction did not restore suppressed player exactly");
        // Authored root registration admits only that ROM family; matching an
        // available neutral image must never suppress a missing bank pose.
        constexpr std::array<unsigned,2> registered{0x100a0,0x10230};
        for(bool flip:{false,true}){
            auto banked=c;put_sprite(banked,0,descriptor(rom,0x10230,flip));
            auto bank_original_owner=std::make_unique<Presentation::WideScene>(rom,viewport),bank_replacement_owner=std::make_unique<Presentation::WideScene>(rom,viewport);
            auto& bank_original=*bank_original_owner;auto& bank_replacement=*bank_replacement_owner;
            bank_replacement.player_aircraft(true,0,registered);
            bank_original.render(std::span(&banked,1),1,false,false);
            bank_replacement.render(std::span(&banked,1),1,false,false);
            const auto bank=bank_replacement.view();
            require(bank.player.draws.size()==1&&bank.player.draws.front().root_frame_address==0x10230&&
                bank.player.draws.front().flip_x==flip,"registered gentle-bank scene dispatch or flip changed");
            require(compose(bank_original.view())!=compose(bank),"registered gentle-bank descriptor was not suppressed");
            bank_replacement.player_aircraft(true);bank_replacement.render(std::span(&banked,1),1,false,false);
            require(bank_replacement.view().player.draws.empty()&&compose(bank_replacement.view())==compose(bank_original.view()),
                "missing gentle-bank artwork did not retain Original");
        }
        // A detached aircraft owns only the native 320-column mask. Keep the
        // entire Original descriptor when its image reaches into a widescreen
        // margin, where that mask cannot restore its opaque source pixels.
        for(unsigned root:registered)for(int edge:{-8,312}){
            auto boundary=c;auto edge_d=descriptor(rom,root);
            edge_d[2]=uint16_t((edge_d[2]&0xfe00)|(190+edge));put_sprite(boundary,0,edge_d);
            auto edge_original_owner=std::make_unique<Presentation::WideScene>(rom,viewport),edge_replacement_owner=std::make_unique<Presentation::WideScene>(rom,viewport);
            auto& edge_original=*edge_original_owner;auto& edge_replacement=*edge_replacement_owner;
            edge_replacement.player_aircraft(true,0,registered);
            edge_original.render(std::span(&boundary,1),1,false,false);
            edge_replacement.render(std::span(&boundary,1),1,false,false);
            require(edge_replacement.view().player.draws.empty()&&compose(edge_replacement.view())==compose(edge_original.view()),
                "aircraft crossing native horizontal boundary lost Original fallback");
        }
        // A later raw sprite owns its opaque pixels even when the native
        // priority predicate hides that later sprite behind the text plane.
        auto cover=d;cover[6]=0xe8;cover[3]=0x200;put_sprite(c,1,cover);
        c.ns=2;original.render(std::span(&c,1),2,false,false);replacement.render(std::span(&c,1),2,false,false);
        const auto covered=replacement.view().player;
        require(covered.draws.size()==1&&covered.visibility[covered.draws.front().mask_offset+20*320+40]==0,"later raw sprite did not occlude replacement");
        require(compose(original.view())==compose(replacement.view()),"covered player changed Original composition");
        // An ambiguous duplicate candidate must preserve both Original sprites.
        put_sprite(c,1,d);c.ns=3;original.render(std::span(&c,1),3,false,false);replacement.render(std::span(&c,1),3,false,false);
        require(replacement.view().player.draws.empty()&&compose(original.view())==compose(replacement.view()),"ambiguous player descriptors did not retain Original fallback");
        put_sprite(c,0,d);c.text[(20/8)*64+24+40/8]=0x8241;c.ns=4;
        original.render(std::span(&c,1),4,false,false);replacement.render(std::span(&c,1),4,false,false);
        const auto text=replacement.view().player;
        require(text.draws.size()==1&&text.visibility[text.draws.front().mask_offset+20*320+40]==0,"category-1 HUD text lost priority over replacement");
        original.render(std::span(&c,1),4,false,false,false);
        replacement.render(std::span(&c,1),4,false,false,false);
        const auto custom_hud=replacement.view().player;
        require(custom_hud.draws.empty()&&compose(original.view())==compose(replacement.view()),"detached HUD priority conflict lost Original fallback");
        // The actual observed flight player uses priority 1. Its authored
        // surface remains below both native text categories, including a
        // replacement glyph drawn into a formerly transparent source hole.
        auto low=c;auto low_d=d;low_d[3]=0x1200;put_sprite(low,0,low_d);
        auto low_owner=std::make_unique<Presentation::WideScene>(rom,viewport);auto& low_replacement=*low_owner;low_replacement.player_aircraft(true);
        for(unsigned category=0;category<2;++category){
            low.text[(20/8)*64+24+40/8]=uint16_t(0x0241|(category<<15));
            low.text[(20/8)*64+24+48/8]=uint16_t(0x0241|(category<<15));low.ns=category+1;
            low_replacement.render(std::span(&low,1),category+1,false,false,false);
            const auto observed=low_replacement.view();
            require(observed.player.draws.size()==1,"observed priority-1 player lost custom HUD support");
            const auto offset=observed.player.draws.front().mask_offset;
            require(observed.player.visibility[offset+20*320+40]==0&&observed.player.visibility[offset+20*320+54]==0&&observed.player.visibility[offset+24*320+40]==1,"custom HUD detachment changed native text ownership over player");
            require(observed.hud.occlusion[20*320+40]==0&&observed.hud.occlusion[20*320+54]==0,"priority-1 player changed detached native text ownership");
        }
        original.render(std::span(&c,1),4,false,false);
        replacement.render(std::span(&c,1),4,false,false);
        replacement.player_aircraft(false);replacement.render(std::span(&c,1),4,false,false);
        require(replacement.view().player.draws.empty()&&replacement.view().player.visibility.empty()&&compose(original.view())==compose(replacement.view()),"same-frame Original toggle did not restore source sprites");
        replacement.player_aircraft(true);c.text.fill(0);
        cover=d;cover[1]=256;cover[3]=0x4200;cover[6]=0xe8;put_sprite(c,1,cover);c.ns=5;
        original.render(std::span(&c,1),5,false,false);replacement.render(std::span(&c,1),5,false,false);
        require(replacement.view().player.draws.empty()&&compose(original.view())==compose(replacement.view()),"later shadow overlap lost Original fallback");
        cover=d;cover[1]=500;cover[2]=uint16_t((190+40)|(1<<9));cover[6]=0xe8;put_sprite(c,1,cover);c.ns=6;
        original.render(std::span(&c,1),6,false,false);replacement.render(std::span(&c,1),6,false,false);
        require(replacement.view().player.draws.size()==1,"opaque cloud unnecessarily disabled player prototype");
        replacement.player_aircraft(true,1u<<unsigned(Presentation::Effect::cloud));
        replacement.render(std::span(&c,1),6,false,false);
        require(replacement.view().player.draws.empty()&&compose(original.view())==compose(replacement.view()),"translucent overlap lost Original fallback");
        replacement.player_aircraft(true);
        put_sprite(c,0,d);
        std::array<Presentation::SceneClip,2> frontiers{c,c};
        frontiers[0].top=0;frontiers[0].bottom=23;frontiers[0].ns=7;
        frontiers[1].top=24;frontiers[1].bottom=223;frontiers[1].ns=8;
        frontiers[1].palette[0xf21]=0x100f;
        original.render(frontiers,7,false,false);replacement.render(frontiers,7,false,false);
        const auto partial=replacement.view().player;
        require(partial.draws.size()==2&&partial.draws[0].top==20&&partial.draws[0].bottom==23&&partial.draws[1].top==24&&partial.draws[1].bottom==27,"player partial-render frontiers merged or moved");
        for(unsigned n=0;n<2;++n)for(unsigned y=0;y<224;++y)
            require(bool(partial.visibility[partial.draws[n].mask_offset+y*320+40])==(y>=partial.draws[n].top&&y<=partial.draws[n].bottom),"player visibility crossed partial-render frontier");
        const auto colors=compose(original.view());
        require(colors[20*viewport+(viewport-320)/2+40]==0xffffffff&&colors[24*viewport+(viewport-320)/2+40]==0xffff0000,"player original palette frontier changed");
        c.display=false;c.ns=9;replacement.render(std::span(&c,1),8,false,false);
        require(replacement.view().player.draws.empty(),"disabled display exposed a player replacement");
        replacement.reset();require(!replacement.view().ready&&replacement.view().player.draws.empty()&&replacement.view().player.visibility.empty(),"player data survived reset");
    }
    std::cout<<"Player scene: independent source restoration, transparent holes, later raw ownership/shadow, duplicate fallback, native/custom HUD priority, partial palette frontiers, same-frame toggle and display/reset at 320/358/398 passed.\n";
}

#ifndef AB2_PLAYER_AIRCRAFT_SYNTHETIC
constexpr const char* production_pack="assets/player-aircraft/neutral-gentle-v1.aircraft";
std::vector<unsigned> loaded_roots(){
    if(!SDL_getenv("SDL_VIDEODRIVER"))SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");
    require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    struct Quit{~Quit(){SDL_Quit();}} quit;
    using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>;
    using Renderer=std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)>;
    Surface canvas(SDL_CreateSurface(320,224,SDL_PIXELFORMAT_ARGB8888),SDL_DestroySurface);
    require(bool(canvas),SDL_GetError());
    Renderer renderer(SDL_CreateSoftwareRenderer(canvas.get()),SDL_DestroyRenderer);
    require(bool(renderer),SDL_GetError());
    Frontend::NativePlayerAircraft aircraft(renderer.get());
    const bool loaded=aircraft.load(production_pack);require(loaded,aircraft.load_error().c_str());
    const auto roots=aircraft.supported_roots();
    require(roots.size()==registered_roots.size()&&std::equal(roots.begin(),roots.end(),registered_roots.begin()),
        "production aircraft pack does not supply neutral and gentle-bank roots");
    return {roots.begin(),roots.end()};
}
void preview(Presentation::WideScene::View view,const std::filesystem::path& path){
    if(!SDL_getenv("SDL_VIDEODRIVER"))SDL_SetHint(SDL_HINT_VIDEO_DRIVER,"dummy");
    require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    struct Quit{~Quit(){SDL_Quit();}} quit;
    using Surface=std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)>;
    using Renderer=std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)>;
    using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
    Surface canvas(SDL_CreateSurface(960,672,SDL_PIXELFORMAT_ARGB8888),SDL_DestroySurface);
    require(bool(canvas),SDL_GetError());
    Renderer renderer(SDL_CreateSoftwareRenderer(canvas.get()),SDL_DestroyRenderer);
    require(bool(renderer),SDL_GetError());
    require(SDL_SetRenderLogicalPresentation(renderer.get(),view.width,224,SDL_LOGICAL_PRESENTATION_LETTERBOX),SDL_GetError());
    const auto underlay=compose(view);
    Texture background(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,view.width,224),SDL_DestroyTexture);
    require(bool(background),SDL_GetError());
    require(SDL_UpdateTexture(background.get(),nullptr,underlay.data(),view.width*sizeof(uint32_t)),SDL_GetError());
    require(SDL_SetTextureScaleMode(background.get(),SDL_SCALEMODE_NEAREST),SDL_GetError());
    require(SDL_RenderTexture(renderer.get(),background.get(),nullptr,nullptr),SDL_GetError());
    Frontend::NativePlayerAircraft aircraft(renderer.get());
    const bool loaded=aircraft.load(production_pack);
    require(loaded,aircraft.load_error().c_str());
    aircraft.draw(view.player,view.width);
    require(SDL_RenderPresent(renderer.get()),SDL_GetError());
    require(SDL_SaveBMP(canvas.get(),path.string().c_str()),SDL_GetError());
}

GameControls controls(unsigned frame){
    GameControls c;c.coin=frame>=360&&frame<372;c.start=frame>=420&&frame<432;
    // Retain the complete earlier 1,800-frame recording, then hold a neutral
    // physical pitch axis and apply small banks through the same ADC input.
    // Full stick deflection jumps past the ROM's gentle-bank selection.
    if(frame>=1800){
        constexpr std::array<float,10> banks{0,0.10f,-0.10f,0.20f,-0.20f,0.30f,-0.30f,0.50f,-0.50f,0};
        c.stick_x=banks[std::min((frame-1800)/120,9u)];return c;
    }
    if(frame>=550){const unsigned phase=((frame-550)/24)%4;c.stick_x=phase==0?-1:phase==1?1:0;c.stick_y=phase==2?-1:phase==3?1:0;}
    return c;
}
void same_native(GameSession& original,GameSession& captured){
    const auto a=original.frame(),b=captured.frame();
    require(a.width==320&&b.width==320&&a.height==224&&b.height==224,"player presentation changed native dimensions");
    require(a.info.number==b.info.number&&a.info.time_ns==b.info.time_ns,"player presentation changed frame/time");
    require(std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),[](auto p,auto q){return p.r==q.r&&p.g==q.g&&p.b==q.b;}),"player presentation changed native RGB");
    require(original.take_audio()==captured.take_audio(),"player presentation changed native PCM");
}
struct Aspect {
    Presentation::WideScene original,replacement;
    explicit Aspect(unsigned viewport,std::span<const unsigned> roots):original(roms,viewport),replacement(roms,viewport){replacement.player_aircraft(true,0,roots);}
};
struct Witnesses{
    uint64_t draws{},visible{},holes{},restored{},fallback_frames{},flight_draws{};
    uint64_t neutral_flight{},bank_unflipped_flight{},bank_mirrored_flight{};
    unsigned first_flight_frame=UINT32_MAX;
    std::map<unsigned,uint64_t> modes,frames;
};

// Independent original-sprite reconstruction uses the MAME-verified hardware
// renderer, never the new descriptor matcher, suppression loop or its source
// sampling. Each frontier's palette resolves its original F2 raw pixels.
void restore(Presentation::WideScene::View view,std::span<const Presentation::SceneClip> clips,HWSprites& hardware,std::vector<uint32_t>& pixels,Witnesses& w,unsigned frame){
    std::vector<uint16_t> raw(320*224,0xffff);HWPalette palette(8192);
    for(const auto& c:clips){
        if(!c.display)continue;
        for(unsigned i=c.top*320;i<(c.bottom+1)*320;++i)raw[i]=0xffff;
        auto bank=c.sprites;hardware.render_bank(bank.data(),raw.data(),320,224,c.top,c.bottom);
        for(unsigned i=0;i<c.palette.size();++i)palette.write(i,c.palette[i]);
        for(const auto& d:view.player.draws){
            const auto top=std::max(c.top,d.top),bottom=std::min(c.bottom,d.bottom);if(top>bottom)continue;
            ++w.modes[c.mode];++w.frames[d.frame_address];
            if(c.mode==0x10){
                ++w.flight_draws;w.first_flight_frame=std::min(w.first_flight_frame,frame);
                if(d.root_frame_address==0x100a0)++w.neutral_flight;
                if(d.root_frame_address==0x10230){if(d.flip_x)++w.bank_mirrored_flight;else ++w.bank_unflipped_flight;}
            }
            for(unsigned y=top;y<=bottom;++y)for(unsigned x=0;x<320;++x){
                const auto ownership=view.player.visibility[d.mask_offset+y*320+x];require(ownership<=1,"invalid player visibility ownership");
                if(!ownership)continue;++w.visible;
                const auto value=raw[y*320+x];
                if(value==0xffff||((value>>4)&255)!=0xf2){++w.holes;continue;}
                require((value&0x400f)!=0x400a,"unreviewed player shadow source");
                const auto i=y*view.width+view.origin+x;const auto color=argb(palette.pen(value&0xfff));
                w.restored+=pixels[i]!=color;pixels[i]=color;
            }
        }
    }
}
void integration(const std::filesystem::path& preview_path={}){
    constexpr unsigned physical_frames=3000;
    const auto roots=loaded_roots();
    require(roms.load_aburner2(),"ROMs required");
    auto original_owner=std::make_unique<GameSession>(roms),captured_owner=std::make_unique<GameSession>(roms);
    auto& original=*original_owner;auto& captured=*captured_owner;
    captured.capture_wide_scene(true,320);
    std::vector<Aspect> aspects;aspects.reserve(3);
    for(unsigned viewport:{320u,358u,398u})aspects.emplace_back(viewport,roots);
    HWSprites hardware;hardware.init(roms.sprites.rom,roms.sprites.length);Witnesses w;
    std::array<bool,3> previews{};
    for(unsigned frame=0;frame<physical_frames;++frame){
        require(original.advance(controls(frame))&&captured.advance(controls(frame)),"running player comparison did not advance");
        same_native(original,captured);const auto scene=captured.wide_scene();require(scene.ready,"player scene capture incomplete");
        const auto native=captured.frame();
        for(auto& aspect:aspects){
            aspect.original.render(scene.clips,scene.info.number,false,false);aspect.replacement.render(scene.clips,scene.info.number,false,false);
            const auto a=aspect.original.view(),b=aspect.replacement.view();require(a.ready&&b.ready,"player scene lost native row coverage");
            require(a.player.draws.empty()&&a.player.visibility.empty(),"Original unexpectedly captured replacement data");
            const auto expected=compose(a);auto restored=compose(b);w.draws+=b.player.draws.size();w.fallback_frames+=b.player.draws.empty();
            restore(b,scene.clips,hardware,restored,w,frame);
            if(!preview_path.empty())for(const auto& draw:b.player.draws){
                const bool flight=std::any_of(scene.clips.begin(),scene.clips.end(),[&](const auto& c){
                    return c.mode==0x10&&c.top<=draw.bottom&&c.bottom>=draw.top;
                });
                if(!flight)continue;
                const unsigned pose=draw.root_frame_address==0x100a0?0:draw.flip_x?2:1;
                if(previews[pose])continue;
                auto output=preview_path;
                if(pose)output=preview_path.parent_path()/(preview_path.stem().string()+(pose==1?"-bank-unflipped":"-bank-mirrored")+preview_path.extension().string());
                preview(b,output);previews[pose]=true;
                std::cout<<"Player active-flight HD preview: input frame "<<frame<<"; "<<output.string()<<'\n';
            }
            for(unsigned y=0;y<224;++y)for(unsigned x=0;x<a.width;++x){
                const auto i=y*a.width+x;
                if(restored[i]!=expected[i])throw std::runtime_error("player native restoration differs at frame "+std::to_string(frame)+" width "+std::to_string(a.width)+" x "+std::to_string(x)+" y "+std::to_string(y));
                if(x>=a.origin&&x<a.origin+320)require(expected[i]==argb(native.pixels[y*320+x-a.origin]),"Original player scene differs from native raster");
            }
        }
    }
    std::cout<<"Player active-flight pose witnesses: neutral "<<w.neutral_flight<<", gentle unflipped "<<w.bank_unflipped_flight
        <<", gentle mirrored "<<w.bank_mirrored_flight<<".\n";
    std::cout<<"Player witnessed modes:";for(const auto& [mode,count]:w.modes)std::cout<<" 0x"<<std::hex<<mode<<std::dec<<"="<<count;
    std::cout<<"; frame identities:";for(const auto& [frame,count]:w.frames)std::cout<<" sub_"<<std::hex<<frame<<std::dec<<"="<<count;std::cout<<std::endl;
    require(w.draws&&w.visible&&w.holes&&w.restored&&w.fallback_frames,"player integration lacks neutral, transparent, suppressed or fallback witnesses");
    require(w.flight_draws,"player integration has no mode-10 active flight replacement witness");
    require(w.neutral_flight&&w.bank_unflipped_flight&&w.bank_mirrored_flight,
        "player integration lacks neutral or both gentle-bank active-flight directions");
    require(preview_path.empty()||std::all_of(previews.begin(),previews.end(),[](bool value){return value;}),
        "requested neutral and both gentle-bank player previews were not written");
    captured.set_paused(true);original.restart();captured.restart();
    require(!captured.wide_scene().ready&&captured.take_audio().empty(),"restart retained player capture/audio");
    for(auto& aspect:aspects){aspect.original.reset();aspect.replacement.reset();}
    for(unsigned f=0;f<4;++f){original.advance({});captured.advance({});same_native(original,captured);}
    std::cout<<"Player presentation: "<<physical_frames<<" physical-input plus 4 restart frames exact native RGB/PCM/time; independent original-sprite restoration at 320/358/398; "<<w.draws<<" draws, "<<w.visible<<" visible mask pixels, "<<w.holes<<" source holes, "<<w.restored<<" restored source pixels, "<<w.fallback_frames<<" Original fallback views, "<<w.flight_draws<<" active-flight draws (neutral "<<w.neutral_flight<<", gentle unflipped "<<w.bank_unflipped_flight<<", gentle mirrored "<<w.bank_mirrored_flight<<"; first input frame "<<w.first_flight_frame<<"); 0 skipped.\n";
}
#endif
}
int main(int argc,char** argv)try{
    Roms synthetic;synthetic_assets(synthetic);matcher_fixtures(synthetic);synthetic_scene_fixtures(synthetic);
    if(argc==2&&std::string_view(argv[1])=="--synthetic")return 0;
#ifndef AB2_PLAYER_AIRCRAFT_SYNTHETIC
    if(argc==3&&std::string_view(argv[1])=="--preview")integration(argv[2]);else integration();return 0;
#else
    return 0;
#endif
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
