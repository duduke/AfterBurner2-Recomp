#include "frontend/native_crt.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void require(bool ok,const std::string& message) {
    if(!ok)throw std::runtime_error(message+": "+SDL_GetError());
}
struct SurfaceDelete {void operator()(SDL_Surface* s)const{SDL_DestroySurface(s);}};
struct RendererDelete {void operator()(SDL_Renderer* r)const{SDL_DestroyRenderer(r);}};
struct WindowDelete {void operator()(SDL_Window* w)const{SDL_DestroyWindow(w);}};
struct TextureDelete {void operator()(SDL_Texture* t)const{SDL_DestroyTexture(t);}};
struct DeviceDelete {void operator()(SDL_GPUDevice* d)const{if(d)SDL_DestroyGPUDevice(d);}};
using Surface=std::unique_ptr<SDL_Surface,SurfaceDelete>;
using Renderer=std::unique_ptr<SDL_Renderer,RendererDelete>;
using Window=std::unique_ptr<SDL_Window,WindowDelete>;
using Texture=std::unique_ptr<SDL_Texture,TextureDelete>;
using Device=std::unique_ptr<SDL_GPUDevice,DeviceDelete>;
struct Quit {~Quit(){SDL_Quit();}};

uint32_t pixel(const Surface& image,int x,int y) {
    return reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(image->pixels)+y*image->pitch)[x];
}
struct State {
    SDL_Texture* target{};
    int logical_w{},logical_h{};
    SDL_RendererLogicalPresentation presentation{};
    SDL_Rect viewport{},clip{};
    bool viewport_set{},clip_enabled{};
    float scale_x{},scale_y{};
    std::array<uint8_t,4> color{};
    SDL_BlendMode blend{};
    explicit State(SDL_Renderer* r) {
        target=SDL_GetRenderTarget(r);
        require(SDL_GetRenderLogicalPresentation(r,&logical_w,&logical_h,&presentation) &&
            SDL_GetRenderViewport(r,&viewport) && SDL_GetRenderClipRect(r,&clip) &&
            SDL_GetRenderScale(r,&scale_x,&scale_y) &&
            SDL_GetRenderDrawColor(r,&color[0],&color[1],&color[2],&color[3]) &&
            SDL_GetRenderDrawBlendMode(r,&blend),"CRT renderer state query failed");
        viewport_set=SDL_RenderViewportSet(r);clip_enabled=SDL_RenderClipEnabled(r);
    }
    bool operator==(const State& other)const {
        const auto rect_equal=[](const SDL_Rect& a,const SDL_Rect& b){return a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h;};
        return target==other.target && logical_w==other.logical_w && logical_h==other.logical_h && presentation==other.presentation &&
            viewport_set==other.viewport_set && clip_enabled==other.clip_enabled &&
            rect_equal(viewport,other.viewport) && rect_equal(clip,other.clip) &&
            scale_x==other.scale_x && scale_y==other.scale_y && color==other.color && blend==other.blend;
    }
};
void physical(SDL_Renderer* r) {
    require(SDL_SetRenderLogicalPresentation(r,0,0,SDL_LOGICAL_PRESENTATION_DISABLED) &&
        SDL_SetRenderViewport(r,nullptr) && SDL_SetRenderScale(r,1,1) && SDL_SetRenderClipRect(r,nullptr),
        "CRT physical output setup failed");
}
Surface read(SDL_Renderer* r) {
    physical(r);
    Surface raw(SDL_RenderReadPixels(r,nullptr));require(bool(raw),"CRT output read failed");
    Surface converted(SDL_ConvertSurface(raw.get(),SDL_PIXELFORMAT_ARGB8888));
    require(bool(converted),"CRT output conversion failed");return converted;
}

struct Fixture {
    Surface canvas;
    Window window;
    Renderer renderer;
    Texture source;
    std::unique_ptr<Frontend::NativeCrt> crt;
    std::vector<uint32_t> input;
    int width{},height{};
    Fixture(SDL_GPUDevice* gpu,int w,int h,bool high_dpi=false) {
        if(gpu) {
            window.reset(SDL_CreateWindow("CRT renderer regression",w,h,
                SDL_WINDOW_HIDDEN|(high_dpi?SDL_WINDOW_HIGH_PIXEL_DENSITY:0)));
            require(bool(window),"CRT GPU window creation failed");
            renderer.reset(SDL_CreateGPURenderer(gpu,window.get()));
        } else {
            canvas.reset(SDL_CreateSurface(w,h,SDL_PIXELFORMAT_ARGB8888));
            require(bool(canvas),"CRT software canvas allocation failed");
            renderer.reset(SDL_CreateSoftwareRenderer(canvas.get()));
        }
        require(bool(renderer),"CRT renderer creation failed");
        crt=std::make_unique<Frontend::NativeCrt>(renderer.get());
        refresh_source();
    }
    void refresh_source() {
        physical(renderer.get());
        require(SDL_GetRenderOutputSize(renderer.get(),&width,&height),"CRT output size query failed");
        require(width>0 && height>0 && uint64_t(width)*height<=64u*1024u*1024u/4,
            "CRT test output exceeds the 64 MiB fixture budget");
        input.resize(size_t(width)*height);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            // Independent multichannel ramp includes black, full intensity and
            // intermediate values, and keeps the bars nonblack for clipping checks.
            input[size_t(y)*width+x]=0xff000000u|uint32_t((x*17+y*11+53)&255)<<16|
                uint32_t((x*7+y*23+101)&255)<<8|uint32_t((x*31+y*3+197)&255);
        }
        source.reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STATIC,width,height));
        require(bool(source) && SDL_UpdateTexture(source.get(),nullptr,input.data(),width*4) &&
            SDL_SetTextureBlendMode(source.get(),SDL_BLENDMODE_NONE) &&
            SDL_SetTextureScaleMode(source.get(),SDL_SCALEMODE_NEAREST),"CRT source texture upload failed");
    }
    SDL_FRect paint(unsigned logical_w,SDL_RendererLogicalPresentation mode,bool custom_state=false) {
        physical(renderer.get());
        require(SDL_RenderTexture(renderer.get(),source.get(),nullptr,nullptr),"CRT source rendering failed");
        require(SDL_SetRenderLogicalPresentation(renderer.get(),logical_w,224,mode),"CRT logical presentation failed");
        SDL_FRect rect;require(SDL_GetRenderLogicalPresentationRect(renderer.get(),&rect),"CRT game rectangle query failed");
        require(SDL_SetRenderDrawColor(renderer.get(),11,22,33,44) &&
            SDL_SetRenderDrawBlendMode(renderer.get(),SDL_BLENDMODE_ADD),"CRT draw state setup failed");
        if(custom_state) {
            const SDL_Rect viewport{7,9,int(logical_w)-17,197},clip{3,5,47,31};
            require(SDL_SetRenderViewport(renderer.get(),&viewport) &&
                SDL_SetRenderScale(renderer.get(),1.25f,.75f) &&
                SDL_SetRenderClipRect(renderer.get(),&clip),"CRT custom state setup failed");
        }
        return rect;
    }
};

bool active(const SDL_FRect& rect,int x,int y) {
    return x+.5>=rect.x && x+.5<rect.x+rect.w && y+.5>=rect.y && y+.5<rect.y+rect.h;
}
// This reference evaluates an analytic phosphor/beam response against the
// original source, rather than inspecting or reusing the renderer's texture.
uint32_t expected(uint32_t source,int x,int y,const SDL_FRect& rect,const Frontend::Settings& settings) {
    if(settings.crt==Frontend::Crt::off || settings.crt==Frontend::Crt::retroarch || !active(rect,x,y))return source;
    const double vertical_scale=double(rect.h)/224;
    const double amplitude=double(settings.crt_scanlines)/100*std::clamp(vertical_scale-1.,0.,1.);
    const double row=(y-rect.y)/vertical_scale;
    const double scan_gain=1-amplitude*(1+std::cos(2*std::numbers::pi*row))/2;
    uint32_t result=source&0xff000000;
    for(unsigned channel=0;channel<3;++channel) {
        const unsigned shift=16-8*channel,original=(source>>shift)&255;
        const double mask_gain=x%3==int(channel)?1:1-double(settings.crt_mask)/100;
        const auto weight=std::lround(255*scan_gain*mask_gain);
        result|=uint32_t(std::lround(double(original)*weight/255))<<shift;
    }
    return result;
}
void compare(const Fixture& fixture,const Surface& output,const SDL_FRect& rect,
             const Frontend::Settings& settings,const std::string& context,bool exact=false) {
    require(output->w==fixture.width && output->h==fixture.height,context+" output dimensions changed");
    size_t inspected=0;
    for(int y=0;y<fixture.height;++y)for(int x=0;x<fixture.width;++x) {
        // Full scans at ordinary sizes; deterministic dense sampling at 4K
        // still includes every row/column phase, all edges and letterbox bars.
        if(!exact && fixture.width>1920 && x>8 && y>8 && x<fixture.width-9 && y<fixture.height-9 &&
           x%17!=0 && y%19!=0)continue;
        const auto wanted=expected(fixture.input[size_t(y)*fixture.width+x],x,y,rect,settings);
        const auto actual=pixel(output,x,y);
        const int tolerance=exact || !active(rect,x,y)?0:2;
        bool match=(actual>>24)==(wanted>>24);
        for(unsigned shift:{0u,8u,16u})
            match=match && std::abs(int((actual>>shift)&255)-int((wanted>>shift)&255))<=tolerance;
        if(!match)throw std::runtime_error(context+" pixel ("+std::to_string(x)+","+std::to_string(y)+
            ") differs: actual="+std::to_string(actual)+" expected="+std::to_string(wanted));
        ++inspected;
    }
    require(inspected>0,context+" inspected no pixels");
}
void check_layout(Fixture& fixture,unsigned logical_w,SDL_RendererLogicalPresentation mode,
                  const Frontend::Settings& settings,bool custom_state=false) {
    const auto original_input=fixture.input;
    const auto rect=fixture.paint(logical_w,mode,custom_state);
    const State before(fixture.renderer.get());
    require(fixture.crt->draw(settings),"CRT valid settings rejected");
    require(State(fixture.renderer.get())==before,"CRT changed caller renderer state");
    const auto image=read(fixture.renderer.get());
    compare(fixture,image,rect,settings,std::to_string(logical_w)+"x224 at "+
        std::to_string(fixture.width)+"x"+std::to_string(fixture.height));
    require(fixture.input==original_input,"CRT mutated original pixel buffer");
    require(fixture.crt->cached_bytes()>0 && fixture.crt->cached_bytes()<=size_t(fixture.width)*fixture.height*4,
        "CRT cache exceeds one output-resolution RGBA texture");
}
void no_op_and_invalid(Fixture& fixture) {
    Frontend::Settings settings;settings.crt=Frontend::Crt::arcade;
    check_layout(fixture,358,SDL_LOGICAL_PRESENTATION_LETTERBOX,settings,true);
    for(unsigned variant=0;variant<3;++variant) {
        const auto rect=fixture.paint(358,SDL_LOGICAL_PRESENTATION_LETTERBOX,true);
        const State before(fixture.renderer.get());
        settings.crt=variant==2?Frontend::Crt::retroarch:(variant?Frontend::Crt::arcade:Frontend::Crt::off);
        settings.crt_scanlines=variant==1?0:100;settings.crt_mask=variant==1?0:100;
        const auto rebuilds=fixture.crt->rebuilds();
        require(fixture.crt->draw(settings),"CRT no-op rejected");
        require(State(fixture.renderer.get())==before,"CRT no-op changed renderer state");
        require(fixture.crt->cached_bytes()==0 && fixture.crt->rebuilds()==rebuilds,
            "CRT no-op retained or rebuilt its cache");
        compare(fixture,read(fixture.renderer.get()),rect,settings,"CRT exact no-op",true);
    }
    settings=Frontend::Settings{};settings.crt=Frontend::Crt::subtle;
    check_layout(fixture,320,SDL_LOGICAL_PRESENTATION_INTEGER_SCALE,settings);
    const auto bytes=fixture.crt->cached_bytes();const auto rebuilds=fixture.crt->rebuilds();
    for(unsigned variant=0;variant<3;++variant) {
        auto invalid=settings;
        if(variant==0)invalid.crt_scanlines=101;
        if(variant==1)invalid.crt_mask=101;
        if(variant==2)invalid.crt=static_cast<Frontend::Crt>(Frontend::crt_count);
        const auto rect=fixture.paint(320,SDL_LOGICAL_PRESENTATION_INTEGER_SCALE,true);
        const State before(fixture.renderer.get());
        require(!fixture.crt->draw(invalid),"CRT accepted invalid mode/strength");
        require(State(fixture.renderer.get())==before && fixture.crt->cached_bytes()==bytes &&
            fixture.crt->rebuilds()==rebuilds,"CRT invalid input altered renderer/cache");
        auto off=settings;off.crt=Frontend::Crt::off;
        compare(fixture,read(fixture.renderer.get()),rect,off,"CRT invalid-input exact no-op",true);
    }
    fixture.crt->clear();require(fixture.crt->cached_bytes()==0,"CRT clear retained texture storage");
}
void cache_and_ui(Fixture& fixture) {
    Frontend::Settings settings;settings.crt=Frontend::Crt::arcade;
    check_layout(fixture,398,SDL_LOGICAL_PRESENTATION_LETTERBOX,settings);
    const auto bytes=fixture.crt->cached_bytes();const auto rebuilds=fixture.crt->rebuilds();
    for(unsigned i=0;i<24;++i) {
        fixture.paint(398,SDL_LOGICAL_PRESENTATION_LETTERBOX);
        require(fixture.crt->draw(settings),"CRT cached draw failed");
    }
    require(fixture.crt->cached_bytes()==bytes && fixture.crt->rebuilds()==rebuilds,
        "CRT rebuilt unchanged settings/output or accumulated texture storage");
    // UI is drawn after the CRT pass in the original caller coordinate system.
    require(SDL_SetRenderDrawBlendMode(fixture.renderer.get(),SDL_BLENDMODE_NONE) &&
        SDL_SetRenderDrawColor(fixture.renderer.get(),203,151,79,255),"CRT UI setup failed");
    const SDL_FRect ui{90,72,27,19};
    require(SDL_RenderFillRect(fixture.renderer.get(),&ui),"CRT post-pass UI draw failed");
    SDL_FRect rect;require(SDL_GetRenderLogicalPresentationRect(fixture.renderer.get(),&rect),"CRT UI rectangle query failed");
    const auto image=read(fixture.renderer.get());
    const int x=int(rect.x+(ui.x+ui.w/2)*rect.w/398),y=int(rect.y+(ui.y+ui.h/2)*rect.h/224);
    require(pixel(image,x,y)==0xffcb974f,"CRT filtered subsequent UI or changed its coordinates");
    settings.crt_mask=37;check_layout(fixture,398,SDL_LOGICAL_PRESENTATION_LETTERBOX,settings);
    require(fixture.crt->rebuilds()==rebuilds+1 && fixture.crt->cached_bytes()<=size_t(fixture.width)*fixture.height*4,
        "CRT settings update did not replace exactly one cached texture");
}
void beam_contrast(SDL_GPUDevice* device) {
    Frontend::Settings settings;settings.crt=Frontend::Crt::arcade;
    settings.crt_scanlines=80;settings.crt_mask=0;
    for(unsigned scale:{2u,3u}) {
        Fixture fixture(device,320*scale,224*scale);
        std::fill(fixture.input.begin(),fixture.input.end(),0xffffffffu);
        require(SDL_UpdateTexture(fixture.source.get(),nullptr,fixture.input.data(),fixture.width*4),
            "CRT beam white-field upload failed");
        fixture.paint(320,SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
        require(fixture.crt->draw(settings),"CRT beam contrast draw failed");
        const auto image=read(fixture.renderer.get());
        unsigned darkest=255,brightest=0;
        for(unsigned y=0;y<scale;++y) {
            const unsigned value=(pixel(image,fixture.width/2,y)>>16)&255;
            darkest=std::min(darkest,value);brightest=std::max(brightest,value);
        }
        // Independent behavioral check: two or three physical rows per native
        // line must show a beam and a gap, rather than uniform darkening.
        require(brightest>=200 && darkest<=65 && brightest-darkest>=140,
            "CRT scanlines lost visible beam/gap contrast at exact "+std::to_string(scale)+"x");
    }
}
void active_target_and_limits(SDL_GPUDevice* device) {
    Fixture fixture(device,640,448);
    Texture target(SDL_CreateTexture(fixture.renderer.get(),SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET,640,448));
    require(bool(target) && SDL_SetRenderTarget(fixture.renderer.get(),target.get()),
        "CRT active render-target setup failed");
    Frontend::Settings settings;settings.crt=Frontend::Crt::arcade;
    check_layout(fixture,358,SDL_LOGICAL_PRESENTATION_LETTERBOX,settings,true);
    require(SDL_GetRenderTarget(fixture.renderer.get())==target.get(),"CRT changed active render target");
    const auto bytes=fixture.crt->cached_bytes();const auto rebuilds=fixture.crt->rebuilds();
    // The output is larger than the documented cache limit, with no giant
    // source fixture. Rejection must retain the earlier accepted texture.
    Texture oversized(SDL_CreateTexture(fixture.renderer.get(),SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET,8192,2160));
    require(bool(oversized) && SDL_SetRenderTarget(fixture.renderer.get(),oversized.get()),
        "CRT oversized render-target setup failed");
    physical(fixture.renderer.get());
    require(SDL_SetRenderDrawColor(fixture.renderer.get(),71,93,129,255) && SDL_RenderClear(fixture.renderer.get()),
        "CRT oversized target clear failed");
    const SDL_Rect viewport{7,9,8000,2100},clip{3,5,47,31};
    require(SDL_SetRenderViewport(fixture.renderer.get(),&viewport) &&
        SDL_SetRenderScale(fixture.renderer.get(),1.25f,.75f) &&
        SDL_SetRenderClipRect(fixture.renderer.get(),&clip) &&
        SDL_SetRenderDrawBlendMode(fixture.renderer.get(),SDL_BLENDMODE_ADD),"CRT oversized target state failed");
    const State before(fixture.renderer.get());
    require(!fixture.crt->draw(settings),"CRT accepted an overlay larger than 64 MiB");
    require(State(fixture.renderer.get())==before && fixture.crt->cached_bytes()==bytes &&
        fixture.crt->rebuilds()==rebuilds,"CRT oversized rejection changed state or accepted cache");
    physical(fixture.renderer.get());const SDL_Rect sample{17,19,1,1};
    Surface raw(SDL_RenderReadPixels(fixture.renderer.get(),&sample));require(bool(raw),"CRT oversized rejection read failed");
    Surface converted(SDL_ConvertSurface(raw.get(),SDL_PIXELFORMAT_ARGB8888));
    require(bool(converted) && pixel(converted,0,0)==0xff475d81,"CRT oversized rejection changed output pixels");
    require(SDL_SetRenderTarget(fixture.renderer.get(),target.get()),"CRT accepted target restoration failed");
    check_layout(fixture,358,SDL_LOGICAL_PRESENTATION_LETTERBOX,settings,true);
    require(fixture.crt->rebuilds()==rebuilds,"CRT oversized rejection damaged accepted cache reuse");
    require(SDL_SetRenderTarget(fixture.renderer.get(),nullptr),"CRT final target restoration failed");
}
void timing(Fixture& fixture) {
    Frontend::Settings settings;settings.crt=Frontend::Crt::arcade;
    auto* device=static_cast<SDL_GPUDevice*>(SDL_GetPointerProperty(SDL_GetRendererProperties(fixture.renderer.get()),
        SDL_PROP_RENDERER_GPU_DEVICE_POINTER,nullptr));
    const bool vsync_disabled=SDL_SetRenderVSync(fixture.renderer.get(),0);
    int vsync{};const bool vsync_queried=SDL_GetRenderVSync(fixture.renderer.get(),&vsync);
    const auto idle=[&](){if(device)require(SDL_WaitForGPUIdle(device),"CRT timing GPU synchronization failed");};
    fixture.crt->clear();fixture.paint(398,SDL_LOGICAL_PRESENTATION_LETTERBOX);
    idle();
    const auto warm=SDL_GetPerformanceCounter();
    require(fixture.crt->draw(settings) && SDL_RenderPresent(fixture.renderer.get()),"CRT warm timing draw failed");
    idle();const auto warmed=SDL_GetPerformanceCounter();
    constexpr unsigned frames=36;
    const auto sample=[&](const Frontend::Settings& state) {
        idle();const auto begin=SDL_GetPerformanceCounter();
        for(unsigned i=0;i<frames;++i) {
            fixture.paint(398,SDL_LOGICAL_PRESENTATION_LETTERBOX);
            require(fixture.crt->draw(state) && SDL_RenderPresent(fixture.renderer.get()),"CRT timing frame failed");
        }
        idle();return SDL_GetPerformanceCounter()-begin;
    };
    auto off=settings;off.crt=Frontend::Crt::off;
    const auto baseline=sample(off);
    fixture.paint(398,SDL_LOGICAL_PRESENTATION_LETTERBOX);
    require(fixture.crt->draw(settings) && SDL_RenderPresent(fixture.renderer.get()),"CRT benchmark cache warmup failed");
    idle();const auto rebuilds=fixture.crt->rebuilds();
    const auto arcade=sample(settings);
    require(fixture.crt->rebuilds()==rebuilds,"CRT benchmark rebuilt cached texture");
    const double milliseconds=1000./SDL_GetPerformanceFrequency();
    std::cout<<"CRT "<<SDL_GetRendererName(fixture.renderer.get())<<' '<<fixture.width<<'x'<<fixture.height
        <<": vsync="<<(vsync_queried?std::to_string(vsync):"unavailable")
        <<(vsync_disabled?" (requested off)":" (off request unsupported)")
        <<"; first pass/present="<<(warmed-warm)*milliseconds<<" ms; same-source Off="
        <<baseline*milliseconds/frames<<" ms/frame; cached Arcade="<<arcade*milliseconds/frames
        <<" ms/frame; delta="<<(double(arcade)-baseline)*milliseconds/frames
        <<" ms/frame ("<<frames<<" frames each, source+pass/present"
        <<(device?", GPU idle at aggregate boundaries":"")<<"; finite sample).\n";
}
void window_lifecycle(SDL_GPUDevice* device) {
    Fixture fixture(device,640,448,true);
    Frontend::Settings settings;settings.crt=Frontend::Crt::subtle;
    check_layout(fixture,358,SDL_LOGICAL_PRESENTATION_LETTERBOX,settings,true);
    const auto rebuilds=fixture.crt->rebuilds();
    require(SDL_SetWindowSize(fixture.window.get(),800,600) && SDL_SyncWindow(fixture.window.get()),
        "CRT GPU window resize failed");
    // SDL's renderer observes pixel-size events and acquires the resized
    // swapchain on presentation; the window API can report the new size first.
    SDL_PumpEvents();require(SDL_RenderPresent(fixture.renderer.get()),"CRT resized swapchain synchronization failed");
    fixture.refresh_source();check_layout(fixture,398,SDL_LOGICAL_PRESENTATION_INTEGER_SCALE,settings,true);
    require(fixture.crt->rebuilds()==rebuilds+1,"CRT resize did not rebuild once for the new output/layout");
    std::cout<<"CRT high-density resize output="<<fixture.width<<'x'<<fixture.height<<" passed.\n";
    // Hidden windows permit a real fullscreen transition without covering the
    // user's desktop. Drivers may legitimately reject this unsupported path.
    if(SDL_SetWindowFullscreen(fixture.window.get(),true) && SDL_SyncWindow(fixture.window.get())) {
        SDL_PumpEvents();require(SDL_RenderPresent(fixture.renderer.get()),"CRT fullscreen swapchain synchronization failed");
        const bool observed_fullscreen=(SDL_GetWindowFlags(fixture.window.get())&SDL_WINDOW_FULLSCREEN)!=0;
        int full_w{},full_h{};
        require(SDL_GetRenderOutputSize(fixture.renderer.get(),&full_w,&full_h),"CRT fullscreen output query failed");
        const bool within_budget=full_w>0 && full_h>0 && uint64_t(full_w)*full_h<=64u*1024u*1024u/4;
        if(within_budget) {
            fixture.refresh_source();check_layout(fixture,320,SDL_LOGICAL_PRESENTATION_LETTERBOX,settings,true);
        } else {
            std::cout<<"CRT fullscreen output "<<full_w<<'x'<<full_h
                <<" exceeds the 64 MiB source-fixture budget; fullscreen image check not exercised.\n";
        }
        require(SDL_SetWindowFullscreen(fixture.window.get(),false) && SDL_SyncWindow(fixture.window.get()),
            "CRT GPU fullscreen restoration failed");
        SDL_PumpEvents();require(SDL_RenderPresent(fixture.renderer.get()),"CRT restored swapchain synchronization failed");
        require(!(SDL_GetWindowFlags(fixture.window.get())&SDL_WINDOW_FULLSCREEN),"CRT windowed state was not restored");
        fixture.refresh_source();check_layout(fixture,358,SDL_LOGICAL_PRESENTATION_LETTERBOX,settings,true);
        if(observed_fullscreen && within_budget)std::cout<<"CRT fullscreen: 1 of 1 observed round trips passed, 0 skipped.\n";
        else std::cout<<"CRT fullscreen: 0 of 1 observed round trips, 1 skipped ("
            <<(!observed_fullscreen?"window manager accepted the request without applying fullscreen":"output exceeds the fixture budget")
            <<"); visible-window acceptance remains in verify-gpu-native-menu.\n";
    } else {
        SDL_SetWindowFullscreen(fixture.window.get(),false);
        std::cout<<"CRT fullscreen: 0 of 1 observed round trips, 1 skipped (API unavailable: "<<SDL_GetError()
            <<"); visible-window acceptance remains in verify-gpu-native-menu.\n";
    }
}
}

int main(int argc,char** argv)try {
    const bool gpu=argc==2 && std::string_view(argv[1])=="--gpu";
    require(SDL_Init(SDL_INIT_VIDEO),"CRT SDL video initialization failed");Quit quit;
    Device device(gpu?SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_SPIRV,true,nullptr):nullptr);
    require(!gpu || bool(device),"CRT GPU device creation failed");
    Frontend::Settings settings;settings.crt=Frontend::Crt::arcade;
    for(const auto dimensions:{std::array{320,224},std::array{640,336},std::array{640,448},std::array{960,672},std::array{3840,2160}}) {
        Fixture fixture(device.get(),dimensions[0],dimensions[1]);
        for(unsigned width:{320u,358u,398u})for(auto mode:{SDL_LOGICAL_PRESENTATION_LETTERBOX,SDL_LOGICAL_PRESENTATION_INTEGER_SCALE})
            check_layout(fixture,width,mode,settings,true);
        // Separate terms prevent a missing mask or beam pattern from being
        // hidden by the other attenuation; endpoints exercise full black.
        for(const auto strengths:{std::array{0u,100u},std::array{100u,0u},std::array{100u,100u}}) {
            settings.crt_scanlines=strengths[0];settings.crt_mask=strengths[1];
            check_layout(fixture,358,SDL_LOGICAL_PRESENTATION_LETTERBOX,settings);
        }
        settings=Frontend::Settings{};settings.crt=Frontend::Crt::arcade;
        if(dimensions[0]==960) {no_op_and_invalid(fixture);cache_and_ui(fixture);}
        if(dimensions[0]==3840)timing(fixture);
    }
    {Fixture fixture(device.get(),1920,1080);timing(fixture);}
    beam_contrast(device.get());active_target_and_limits(device.get());
    if(gpu)window_lifecycle(device.get());
    std::cout<<"Native CRT: independent RGB/beam readbacks across 320/358/398 viewports, letterbox/integer and 1x through 4K; exact off/zero/invalid pixels, preserved source/state/UI, bounded cache and output lifecycle passed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
