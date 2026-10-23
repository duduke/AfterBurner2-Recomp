#include "frontend/native_hud.hpp"
#include <cstring>
#include <iostream>
#include <random>

namespace {
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
struct SurfaceDelete {void operator()(SDL_Surface* s)const{SDL_DestroySurface(s);}};
struct RendererDelete {void operator()(SDL_Renderer* r)const{SDL_DestroyRenderer(r);}};
struct WindowDelete {void operator()(SDL_Window* w)const{SDL_DestroyWindow(w);}};
struct DeviceDelete {void operator()(SDL_GPUDevice* d)const{if(d)SDL_DestroyGPUDevice(d);}};
using Surface=std::unique_ptr<SDL_Surface,SurfaceDelete>;
using Renderer=std::unique_ptr<SDL_Renderer,RendererDelete>;
using Window=std::unique_ptr<SDL_Window,WindowDelete>;
using Device=std::unique_ptr<SDL_GPUDevice,DeviceDelete>;
struct Temporary {
    std::filesystem::path path;
    ~Temporary(){std::error_code ec;std::filesystem::remove_all(path,ec);}
};
std::filesystem::path temporary_directory() {
    std::random_device random;
    for(unsigned attempt=0;attempt<10;++attempt) {
        const auto path=std::filesystem::temp_directory_path()/
            ("ab2-native-hud-"+std::to_string(SDL_GetPerformanceCounter())+"-"+std::to_string(random()));
        if(!std::filesystem::create_directory(path))continue;
        std::filesystem::permissions(path,std::filesystem::perms::owner_all);
        return path;
    }
    throw std::runtime_error("cannot create isolated HUD fixture directory");
}
uint32_t pixel(const Surface& image,unsigned x,unsigned y) {
    return reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(image->pixels)+y*image->pitch)[x];
}
Surface read(SDL_Renderer* renderer) {
    Surface raw(SDL_RenderReadPixels(renderer,nullptr));require(bool(raw),"HUD pixel read failed");
    Surface result(SDL_ConvertSurface(raw.get(),SDL_PIXELFORMAT_ARGB8888));
    require(bool(result),"HUD pixel conversion failed");return result;
}
void clear(SDL_Renderer* renderer) {
    require(SDL_SetRenderDrawColor(renderer,8,12,16,255) && SDL_RenderClear(renderer),"HUD clear failed");
}
Surface atlas(unsigned edge=16) {
    Surface image(SDL_CreateSurface(16*edge,32*edge,SDL_PIXELFORMAT_RGBA32));
    require(bool(image),"HUD atlas allocation failed");
    std::memset(image->pixels,0,image->pitch*image->h);return image;
}
void atlas_pixel(const Surface& image,unsigned code,unsigned x,unsigned y,uint8_t pen,uint8_t alpha=255) {
    const unsigned edge=image->w/16;
    auto* p=static_cast<uint8_t*>(image->pixels)+(code/16*edge+y)*image->pitch+(code%16*edge+x)*4;
    p[0]=pen;p[1]=127;p[2]=213;p[3]=alpha;
}
void save(const Surface& image,const std::filesystem::path& path) {
    require(SDL_SaveBMP(image.get(),path.string().c_str()),"HUD synthetic atlas save failed");
}
void rom_pen(Roms& rom,unsigned code,unsigned x,unsigned y,uint8_t pen) {
    const unsigned third=rom.tiles.length/3;
    for(unsigned plane=0;plane<3;++plane)
        if(pen&(1<<plane))rom.tiles.rom[plane*third+code*8+y]|=1<<(7-x);
}
Presentation::HudClip clip() {
    Presentation::HudClip result;result.top=8;result.bottom=15;
    result.entries[41]=1;result.entries[42]=1|(1<<9);result.entries[43]=2;result.entries[44]=3;
    result.colors[1]=0xffc01428;result.colors[2]=0xff18b830;result.colors[3]=0xff2040e0;
    result.colors[4]=0xff28b440;result.colors[9]=0xffe0c020;
    result.colors[65]=0xff601420;return result;
}
}

int main(int argc,char** argv)try {
    const bool gpu=argc==2 && std::string_view(argv[1])=="--gpu";
    require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    Device device(gpu?SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_SPIRV,true,nullptr):nullptr);
    require(!gpu || bool(device),"HUD GPU device creation failed");
    Roms rom;rom.tiles.init(512*8*3);
    // Synthetic pens exercise the same three-plane order without ROM data.
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
        if(x<=y)rom_pen(rom,1,x,y,1);
        if((x+y)%2==0)rom_pen(rom,2,x,y,4);
    }
    Surface canvas(SDL_CreateSurface(320,224,SDL_PIXELFORMAT_ARGB8888));require(bool(canvas),"HUD canvas failed");
    Window window(gpu?SDL_CreateWindow("HUD renderer regression",320,224,SDL_WINDOW_HIDDEN):nullptr);
    require(!gpu || bool(window),"HUD GPU window creation failed");
    Renderer renderer(gpu?SDL_CreateGPURenderer(device.get(),window.get()):SDL_CreateSoftwareRenderer(canvas.get()));
    require(bool(renderer),"HUD renderer failed");
    Frontend::NativeHud hud(renderer.get(),rom);
    require(!hud.custom_available(),"HUD custom atlas unexpectedly present");
    Temporary directory{temporary_directory()};const auto path=directory.path/"atlas.bmp";
    auto image=atlas();
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
        atlas_pixel(image,1,x,y,1);atlas_pixel(image,3,x,y,3,128);
    }
    save(image,path);require(hud.load_custom(path) && hud.custom_available(),"valid indexed HUD atlas rejected");
    std::array<Presentation::HudClip,2> clips{clip(),clip()};clips[0].bottom=11;clips[1].top=12;
    clips[1].colors[1]=0xff2030e0;
    std::vector<uint8_t> occlusion(320*224);occlusion[9*320+9]=1;occlusion[9*320+10]=2;
    Presentation::HudScene scene{clips,occlusion};
    clear(renderer.get());hud.draw(scene,Frontend::Hud::original,320);
    require(pixel(read(renderer.get()),8,8)==0xff080c10,"Original HUD mode drew replacement text");
    require(SDL_SetRenderDrawColor(renderer.get(),11,22,33,44) &&
        SDL_SetRenderDrawBlendMode(renderer.get(),SDL_BLENDMODE_ADD),"HUD state setup failed");
    const SDL_Rect clip_state{0,0,320,224};require(SDL_SetRenderClipRect(renderer.get(),&clip_state),"HUD clip setup failed");
    hud.draw(scene,Frontend::Hud::custom,320);auto rendered=read(renderer.get());
    require(pixel(rendered,8,8)==0xffc01428 && pixel(rendered,15,11)==0xffc01428,"custom indexed pen/color failed");
    require(pixel(rendered,8,12)==0xff2030e0,"HUD partial-row palette frontier failed");
    require(pixel(rendered,9,9)==0xff601420,"HUD hardware shadow/effects palette failed");
    require(pixel(rendered,10,9)==0xff080c10,"HUD native occlusion failed");
    require(pixel(rendered,16,8)==0xffe0c020,"HUD cache mixed live palettes");
    require(pixel(rendered,24,8)==0xff28b440 && pixel(rendered,25,8)==0xff080c10,"empty custom tile lost native fallback/transparency");
    const auto half=pixel(rendered,32,8);
    require(((half>>16)&255)>=19 && ((half>>16)&255)<=20 && (half&255)>=119 && (half&255)<=120,"custom alpha coverage failed");
    uint8_t r,g,b,a;SDL_BlendMode blend;SDL_Rect retained;
    require(SDL_GetRenderDrawColor(renderer.get(),&r,&g,&b,&a) && r==11 && g==22 && b==33 && a==44 &&
        SDL_GetRenderDrawBlendMode(renderer.get(),&blend) && blend==SDL_BLENDMODE_ADD &&
        SDL_GetRenderClipRect(renderer.get(),&retained) && SDL_RenderClipEnabled(renderer.get()) &&
        retained.x==0 && retained.y==0 && retained.w==320 && retained.h==224,"HUD changed renderer state");
    SDL_SetRenderClipRect(renderer.get(),nullptr);
    // Rejection is transactional, even after the old atlas populated the cache.
    atlas_pixel(image,1,0,0,8);save(image,path);
    require(!hud.load_custom(path) && !hud.custom_error().empty() && hud.custom_available(),"malformed HUD pens accepted");
    clear(renderer.get());hud.draw(scene,Frontend::Hud::custom,320);
    require(pixel(read(renderer.get()),8,8)==0xffc01428,"failed HUD reload replaced accepted atlas");
    auto malformed=atlas(8);save(malformed,path);require(!hud.load_custom(path),"undersized HUD atlas accepted");
    malformed.reset(SDL_CreateSurface(256,511,SDL_PIXELFORMAT_RGBA32));save(malformed,path);
    require(!hud.load_custom(path),"malformed HUD atlas dimensions accepted");
    require(!hud.load_custom(path.string()+".missing"),"missing HUD atlas accepted");
    clear(renderer.get());hud.draw(scene,Frontend::Hud::sharp,320);rendered=read(renderer.get());
    require(pixel(rendered,15,8)==0xff080c10 && pixel(rendered,8,15)!=0xff080c10,"ROM-derived Sharp geometry/transparency failed");
    // A partial renderer clip is honored without replacing it.
    clear(renderer.get());const SDL_Rect narrow{8,8,2,8};SDL_SetRenderClipRect(renderer.get(),&narrow);
    hud.draw(scene,Frontend::Hud::custom,320);rendered=read(renderer.get());
    require(pixel(rendered,8,8)==0xffc01428 && pixel(rendered,11,8)==0xff080c10,"HUD ignored caller clip");
    SDL_SetRenderClipRect(renderer.get(),nullptr);
    // Bound cache creation under changing native palettes and reject bad input.
    auto changing=clip();changing.entries.fill(0);changing.entries[41]=1;
    for(unsigned i=0;i<1100;++i){changing.colors[1]=0xff000000u|i;hud.draw({std::span(&changing,1),occlusion},Frontend::Hud::custom,320);}
    bool rejected=false;try{hud.draw({clips,{}},Frontend::Hud::sharp,320);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"HUD accepted invalid occlusion dimensions");
    // At 4K, replacement detail is sampled from the high-resolution glyph,
    // rather than first collapsing the atlas to the native eight-pixel tile.
    Surface large(SDL_CreateSurface(3840,2160,SDL_PIXELFORMAT_ARGB8888));require(bool(large),"4K canvas failed");
    Window high_window(gpu?SDL_CreateWindow("4K HUD renderer regression",3840,2160,SDL_WINDOW_HIDDEN):nullptr);
    require(!gpu || bool(high_window),"4K HUD GPU window creation failed");
    Renderer high(gpu?SDL_CreateGPURenderer(device.get(),high_window.get()):SDL_CreateSoftwareRenderer(large.get()));
    require(bool(high),"4K renderer failed");
    Frontend::NativeHud high_hud(high.get(),rom);image=atlas();
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
        atlas_pixel(image,1,x,y,(x&1)?2:1);
        if(x<8)atlas_pixel(image,5,x,y,1);
    }
#if SDL_VERSION_ATLEAST(3,4,0)
    const auto png=directory.path/"atlas.png";
    require(SDL_SavePNG(image.get(),png.string().c_str()) && high_hud.load_custom(png),"PNG HUD atlas load failed");
#else
    save(image,path);require(high_hud.load_custom(path),"4K BMP HUD atlas load failed");
#endif
    auto high_clip=clip();high_clip.entries.fill(0);high_clip.entries[41]=1;high_clip.entries[42]=5;
    std::fill(occlusion.begin(),occlusion.end(),0);
    for(unsigned width:{320u,358u,398u})for(auto mode:{SDL_LOGICAL_PRESENTATION_LETTERBOX,SDL_LOGICAL_PRESENTATION_INTEGER_SCALE}) {
        require(SDL_SetRenderLogicalPresentation(high.get(),width,224,mode),"4K logical presentation failed");
        clear(high.get());high_hud.draw({std::span(&high_clip,1),occlusion},Frontend::Hud::custom,width);
        SDL_FRect presentation;require(SDL_GetRenderLogicalPresentationRect(high.get(),&presentation),"4K layout query failed");
        // ReadPixels clips to the active logical viewport. Expanding that
        // viewport after drawing permits inspection of the full 4K output.
        require(SDL_SetRenderLogicalPresentation(high.get(),0,0,SDL_LOGICAL_PRESENTATION_DISABLED),"4K readback viewport failed");
        auto result=read(high.get());require(result->w==3840 && result->h==2160,"4K output dimensions changed");
        const float scale=presentation.w/width,left=float(width-320)/2;
        const unsigned y=unsigned(presentation.y+9*scale);
        const unsigned red_x=unsigned(presentation.x+(left+8.25f)*scale),green_x=unsigned(presentation.x+(left+8.75f)*scale);
        const auto red=pixel(result,red_x,y),green=pixel(result,green_x,y);
        require(((red>>16)&255)>((green>>16)&255)+50 && ((green>>8)&255)>((red>>8)&255)+50,"4K HUD lost subnative atlas detail or safe-area alignment");
        require(pixel(result,unsigned(presentation.x+(left+7)*scale),y)==0xff080c10,"4K HUD escaped tile bounds");
        if(mode==SDL_LOGICAL_PRESENTATION_INTEGER_SCALE) {
            const unsigned edge_x=unsigned(presentation.x+(left+20)*scale);
            const float source_x=((edge_x+.5f-presentation.x)/scale-left-16)*2-.5f;
            const float coverage=std::clamp(8-source_x,0.f,1.f);
            const auto boundary=pixel(result,edge_x,y);
            if(std::abs(float((boundary>>16)&255)-(8+(192-8)*coverage))>=14)
                std::cerr<<"HUD edge: width="<<width<<" x="<<edge_x<<" alpha="<<coverage
                    <<" red="<<((boundary>>16)&255)<<" expected="<<(8+(192-8)*coverage)<<'\n';
            require(std::abs(float((boundary>>16)&255)-(8+(192-8)*coverage))<14,
                "4K linear HUD edge darkened alpha coverage");
        }
    }
    std::cout<<"Native HUD ("<<SDL_GetRendererName(renderer.get())<<"): indexed BMP/PNG, transactional reload, ROM fallback, alpha, live palette/frontiers, occlusion, renderer state, bounded cache and 3840x2160 across all three viewports passed.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
