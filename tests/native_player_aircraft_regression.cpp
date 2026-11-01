#include "frontend/native_player_aircraft.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <random>
#include <string_view>

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
            ("ab2-native-aircraft-"+std::to_string(SDL_GetPerformanceCounter())+"-"+std::to_string(random()));
        if(!std::filesystem::create_directory(path))continue;
        std::filesystem::permissions(path,std::filesystem::perms::owner_all);return path;
    }
    throw std::runtime_error("cannot create isolated aircraft fixture directory");
}
Surface surface(unsigned width,unsigned height) {
    Surface image(SDL_CreateSurface(width,height,SDL_PIXELFORMAT_RGBA32));
    require(bool(image),"aircraft surface allocation failed");
    std::memset(image->pixels,0,image->pitch*image->h);return image;
}
void rgba(const Surface& image,unsigned x,unsigned y,std::array<uint8_t,4> color) {
    auto* p=static_cast<uint8_t*>(image->pixels)+y*image->pitch+x*4;
    std::copy(color.begin(),color.end(),p);
}
void save(const Surface& image,const std::filesystem::path& path) {
    require(SDL_SaveBMP(image.get(),path.string().c_str()),"aircraft BMP fixture save failed");
}
Surface read(SDL_Renderer* renderer) {
    Surface raw(SDL_RenderReadPixels(renderer,nullptr));require(bool(raw),"aircraft pixel read failed");
    Surface image(SDL_ConvertSurface(raw.get(),SDL_PIXELFORMAT_ARGB8888));
    require(bool(image),"aircraft pixel conversion failed");return image;
}
uint32_t pixel(const Surface& image,unsigned x,unsigned y) {
    return reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(image->pixels)+y*image->pitch)[x];
}
constexpr uint32_t background=0xff080c10;
constexpr std::array<uint32_t,3> solid{0xffe02030,0xff20d040,0xff3040e0};
void clear(SDL_Renderer* renderer) {
    require(SDL_SetRenderDrawColor(renderer,8,12,16,255) && SDL_RenderClear(renderer),"aircraft clear failed");
}
Surface authored_image() {
    // Asymmetric canvas padding makes accidental use of canvas dimensions
    // visible. The transparent padding carries RGB that must not leak in.
    auto image=surface(160,80);
    for(unsigned y=0;y<80;++y)for(unsigned x=0;x<160;++x)rgba(image,x,y,{255,0,255,0});
    for(unsigned y=0;y<48;++y)for(unsigned x=0;x<120;++x) {
        const unsigned quadrant=(y>=24?2:0)+(x>=60?1:0);
        constexpr std::array<std::array<uint8_t,4>,4> colors{{
            {224,32,48,255},{32,208,64,255},{48,64,224,255},{240,192,24,128}}};
        rgba(image,13+x,9+y,colors[quadrant]);
    }
    return image;
}
void place(Presentation::PlayerAircraftDraw& draw,bool flip_x=false,bool flip_y=false) {
    draw.frame_address=draw.root_frame_address=0x100a0;draw.source_width=120;draw.source_height=48;
    draw.x=80;draw.y=64;draw.width=120;draw.height=48;
    draw.top=64;draw.bottom=111;draw.mask_offset=0;draw.flip_x=flip_x;draw.flip_y=flip_y;
}

void write_pack(const std::filesystem::path& path,const std::string& rows) {
    std::ofstream file(path);file<<"aircraft-pack 1\n"<<rows;
    require(bool(file),"aircraft pack fixture write failed");
}
std::string pose(unsigned root,const std::string& image,const std::string& registration) {
    return "pose "+std::to_string(root)+" \""+image+"\" "+registration+"\n";
}
void fill_mask(std::vector<uint8_t>& mask) {
    std::fill(mask.begin(),mask.end(),0);
    for(unsigned y=64;y<112;++y)for(unsigned x=80;x<200;++x)mask[y*320+x]=1;
}
bool near(uint32_t actual,uint32_t expected,unsigned tolerance=2) {
    for(unsigned shift:{0u,8u,16u})
        if(std::abs(int((actual>>shift)&255)-int((expected>>shift)&255))>int(tolerance))return false;
    return true;
}
}

int main(int argc,char** argv)try {
    const bool gpu=argc==2 && std::string_view(argv[1])=="--gpu";
    require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    Device device(gpu?SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_SPIRV,true,nullptr):nullptr);
    require(!gpu || bool(device),"aircraft GPU device creation failed");
    Temporary directory{temporary_directory()};const auto path=directory.path/"aircraft.bmp";
    Surface canvas=surface(522,224);
    Window window(gpu?SDL_CreateWindow("Aircraft renderer regression",522,224,SDL_WINDOW_HIDDEN):nullptr);
    require(!gpu || bool(window),"aircraft GPU window creation failed");
    Renderer renderer(gpu?SDL_CreateGPURenderer(device.get(),window.get()):SDL_CreateSoftwareRenderer(canvas.get()));
    require(bool(renderer),"aircraft renderer failed");
    Frontend::NativePlayerAircraft aircraft(renderer.get());
    require(!aircraft.available(),"aircraft replacement unexpectedly available");
    std::array<Presentation::PlayerAircraftDraw,1> draws;place(draws[0]);
    std::vector<uint8_t> visibility(320*224);fill_mask(visibility);
    Presentation::PlayerAircraftScene scene{draws,visibility};
    clear(renderer.get());aircraft.draw(scene,320);
    require(pixel(read(renderer.get()),100,80)==background,"missing art changed original presentation");
    auto image=authored_image();save(image,path);
    require(aircraft.load(path) && aircraft.available() && aircraft.load_error().empty(),"valid transparent BMP aircraft rejected");

    // The scene owns native coverage; art is cropped to its alpha bounds and
    // sampled at native coordinates without generator padding shifting it.
    for(unsigned width:{320u,358u,398u,522u}) {
        clear(renderer.get());aircraft.draw(scene,width);auto result=read(renderer.get());
        const unsigned origin=(width-320)/2;
        require(pixel(result,origin+80,64)==solid[0] && pixel(result,origin+199,64)==solid[1],
            "aircraft alpha bounds or viewport origin changed");
        require(pixel(result,origin+79,80)==background && pixel(result,origin+200,80)==background,
            "aircraft escaped native ownership rectangle");
        require(pixel(result,origin+100,100)==solid[2],"aircraft lower-left texture sample changed");
        // 50% straight alpha should blend once after premultiplication.
        require(near(pixel(result,origin+180,100),0xff7c6614),"aircraft alpha was darkened or blended twice");
    }
    // Reflection changes both texture orientation and partial mask sampling.
    for(bool horizontal:{false,true})for(bool vertical:{false,true}) {
        place(draws[0],horizontal,vertical);clear(renderer.get());aircraft.draw(scene,320);
        const auto result=read(renderer.get());
        const unsigned quadrant=(vertical?2:0)+(horizontal?1:0);
        const uint32_t expected=quadrant==3?0xff7c6614:solid[quadrant];
        require(near(pixel(result,100,76),expected),"aircraft texture reflection changed");
    }
    place(draws[0]);draws[0].top=66;draws[0].bottom=70;
    visibility[68*320+110]=0;
    clear(renderer.get());aircraft.draw(scene,320);auto result=read(renderer.get());
    require(pixel(result,100,65)==background && pixel(result,100,66)==solid[0] &&
        pixel(result,100,70)==solid[0] && pixel(result,100,71)==background,"aircraft ignored partial native row clip");
    require(pixel(result,109,68)==solid[0] && pixel(result,110,68)==background && pixel(result,111,68)==solid[0],
        "aircraft filled a native ownership hole");
    place(draws[0],true,true);clear(renderer.get());aircraft.draw(scene,320);result=read(renderer.get());
    require(near(pixel(result,109,68),0xff7c6614) && pixel(result,110,68)==background &&
        near(pixel(result,111,68),0xff7c6614),"reflected aircraft ignored partial native ownership");
    place(draws[0]);fill_mask(visibility);
    clear(renderer.get());const SDL_Rect caller_clip{80,64,3,48};
    require(SDL_SetRenderClipRect(renderer.get(),&caller_clip),"aircraft caller clip setup failed");
    aircraft.draw(scene,320);result=read(renderer.get());
    require(pixel(result,81,80)==solid[0] && pixel(result,84,80)==background,"aircraft ignored caller renderer clip");
    SDL_Rect retained;require(SDL_GetRenderClipRect(renderer.get(),&retained) && SDL_RenderClipEnabled(renderer.get()) &&
        retained.x==80 && retained.y==64 && retained.w==3 && retained.h==48,"aircraft replaced caller renderer clip");
    require(SDL_SetRenderClipRect(renderer.get(),nullptr),"aircraft caller clip reset failed");

    // Reject malformed public scene inputs before texture sampling can read
    // outside an ownership mask or divide by an invalid rectangle extent.
    const auto reject_scene=[&](const Presentation::PlayerAircraftScene& candidate,unsigned width=320) {
        bool rejected=false;try{aircraft.draw(candidate,width);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"aircraft accepted malformed scene input");
    };
    for(unsigned width:{0u,319u,321u,642u})reject_scene(scene,width);
    for(unsigned dimension:{0u,32769u}) {
        auto malformed=draws;malformed[0].width=dimension;reject_scene({malformed,visibility});
        malformed=draws;malformed[0].height=dimension;reject_scene({malformed,visibility});
    }
    auto malformed=draws;malformed[0].top=112;reject_scene({malformed,visibility});
    malformed=draws;malformed[0].bottom=224;reject_scene({malformed,visibility});
    for(size_t offset:{size_t(1),visibility.size()+1,std::numeric_limits<size_t>::max()}) {
        malformed=draws;malformed[0].mask_offset=offset;reject_scene({malformed,visibility});
    }
    reject_scene({draws,std::span(visibility).first(visibility.size()-1)});
    reject_scene({draws,{}});

    // Loading is transactional: every rejected replacement must leave the
    // previously accepted texture usable, including after an I/O failure.
    const auto rejected=[&](const std::filesystem::path& candidate,const char* message) {
        require(!aircraft.load(candidate) && !aircraft.load_error().empty() && aircraft.available(),message);
        clear(renderer.get());aircraft.draw(scene,320);
        require(pixel(read(renderer.get()),100,80)==solid[0],"rejected aircraft replaced the accepted texture");
    };
    rejected(path.string()+".missing","missing aircraft accepted");
    {std::ofstream file(path,std::ios::binary);file<<"invalid image fixture";}
    rejected(path,"invalid aircraft accepted");
    image=surface(119,46);save(image,path);rejected(path,"undersized aircraft accepted");
    image=surface(4097,47);save(image,path);rejected(path,"oversized aircraft accepted");
    image=surface(160,80);save(image,path);rejected(path,"empty aircraft accepted");
    for(unsigned y=0;y<80;++y)for(unsigned x=0;x<160;++x)rgba(image,x,y,{1,2,3,255});
    save(image,path);rejected(path,"opaque aircraft background accepted");

    // Registered art preserves the artist's source rectangle and pivot. Here
    // the explicit rectangle intentionally retains transparent canvas padding,
    // and the art/native pivots put its first opaque pixel eight cells inward.
    image=authored_image();save(image,path);
    auto bank_image=authored_image();
    for(unsigned y=9;y<57;++y)for(unsigned x=13;x<133;++x)rgba(bank_image,x,y,{24,224,224,255});
    const auto bank_path=directory.path/"bank.bmp";save(bank_image,bank_path);
    const auto pack_path=directory.path/"registered.aircraft";
    const std::string neutral=pose(0x100a0,"aircraft.bmp","5 3 140 64 120 48 17 11 12 8 1 1");
    const std::string bank=pose(0x10230,"bank.bmp","13 9 120 48 120 48 73 33 60 24 1 1");
    const std::string annotations="nozzle 65696 \"left exhaust\" 73 33 68 30\n"
        "nozzle 66096 \"right exhaust\" 93 33 80 24\n"
        "lod 65696 65712 0 0 2 2\n";
    write_pack(pack_path,"");
    require(!aircraft.load(pack_path)&&aircraft.available()&&aircraft.poses().empty()&&
        aircraft.supported_roots().size()==1&&aircraft.supported_roots()[0]==0x100a0,
        "rejected pack changed the accepted legacy image or root");
    clear(renderer.get());aircraft.draw(scene,320);
    require(pixel(read(renderer.get()),100,80)==solid[0],"rejected pack replaced the accepted legacy texture");
    write_pack(pack_path,neutral+bank+annotations);
    const bool pack_loaded=aircraft.load(pack_path);
    if(!pack_loaded)throw std::runtime_error("valid registered aircraft pack rejected: "+aircraft.load_error());
    require(aircraft.available()&&aircraft.load_error().empty(),"valid registered aircraft pack unavailable");
    const auto roots=aircraft.supported_roots();
    require(roots.size()==2&&roots[0]==0x100a0&&roots[1]==0x10230,"aircraft pack lost supported pose roots");
    const auto poses=aircraft.poses();
    require(poses.size()==2&&poses[0].root_frame_address==0x100a0&&poses[1].root_frame_address==0x10230,
        "aircraft pack metadata lost stable pose identities");
    const auto& registered=poses[0];
    require(registered.source.x==5&&registered.source.y==3&&registered.source.w==140&&registered.source.h==64&&
        registered.base_width==120&&registered.base_height==48&&registered.registration.art_pivot.x==17&&
        registered.registration.art_pivot.y==11&&registered.registration.native_pivot.x==12&&
        registered.registration.native_pivot.y==8&&registered.registration.pixels_per_native.x==1&&
        registered.registration.pixels_per_native.y==1,"registered source bounds or pivot metadata changed");
    require(registered.nozzles.size()==1&&registered.nozzles[0].name=="left exhaust"&&
        registered.nozzles[0].art.x==73&&registered.nozzles[0].art.y==33&&
        registered.nozzles[0].native.x==68&&registered.nozzles[0].native.y==30,
        "aircraft pack lost named nozzle annotation coordinates");
    require(registered.lods.size()==1&&registered.lods[0].frame_address==0x100b0&&
        registered.lods[0].native_pivot.x==0&&registered.lods[0].pixels_per_native.x==2,
        "aircraft pack lost explicit LOD registration");
    for(unsigned width:{320u,358u,398u,522u})for(bool horizontal:{false,true})for(bool vertical:{false,true}){
        place(draws[0],horizontal,vertical);clear(renderer.get());aircraft.draw(scene,width);result=read(renderer.get());
        const unsigned origin=(width-320)/2;
        require(pixel(result,origin+(horizontal?190:90),vertical?104:72)==solid[0],
            "registered aircraft pivot, source padding or flipped alignment changed");
        require(pixel(result,origin+(horizontal?199:80),vertical?111:64)==background,
            "registered aircraft incorrectly trimmed explicit source padding");
    }
    place(draws[0]);clear(renderer.get());aircraft.draw(scene,320);result=read(renderer.get());
    require(pixel(result,87,72)==background&&pixel(result,88,72)==solid[0]&&
        pixel(result,90,69)==background&&pixel(result,90,70)==solid[0],
        "registered aircraft art/native pivot did not set the authored silhouette origin");
    place(draws[0]);draws[0].frame_address=0x100b0;draws[0].source_width=60;draws[0].source_height=24;
    clear(renderer.get());aircraft.draw(scene,320);result=read(renderer.get());
    require(pixel(result,80,64)==solid[0],"aircraft ignored explicit LOD pivot or scale");
    place(draws[0]);draws[0].frame_address=draws[0].root_frame_address=0x10230;
    clear(renderer.get());aircraft.draw(scene,320);
    require(pixel(read(renderer.get()),100,80)==0xff18e0e0,"registered aircraft selected the wrong pose texture");
    draws[0].frame_address=draws[0].root_frame_address=0x10400;
    clear(renderer.get());aircraft.draw(scene,320);
    require(pixel(read(renderer.get()),100,80)==background,"missing registered pose drew another pose's texture");
    place(draws[0]);
    draws[0].top=72;draws[0].bottom=76;visibility[74*320+110]=0;
    clear(renderer.get());aircraft.draw(scene,320);result=read(renderer.get());
    require(pixel(result,100,71)==background&&pixel(result,100,72)==solid[0]&&
        pixel(result,100,76)==solid[0]&&pixel(result,100,77)==background,
        "registered aircraft ignored partial native row clip");
    require(pixel(result,109,74)==solid[0]&&pixel(result,110,74)==background&&pixel(result,111,74)==solid[0],
        "registered aircraft filled a native ownership hole");
    place(draws[0]);fill_mask(visibility);
    clear(renderer.get());const SDL_Rect registered_clip{90,72,3,40};
    require(SDL_SetRenderClipRect(renderer.get(),&registered_clip),"registered aircraft caller clip setup failed");
    aircraft.draw(scene,320);result=read(renderer.get());
    require(pixel(result,91,80)==solid[0]&&pixel(result,94,80)==background,"registered aircraft ignored caller renderer clip");
    require(SDL_GetRenderClipRect(renderer.get(),&retained)&&SDL_RenderClipEnabled(renderer.get())&&
        retained.x==90&&retained.y==72&&retained.w==3&&retained.h==40,"registered aircraft replaced caller renderer clip");
    require(SDL_SetRenderClipRect(renderer.get(),nullptr),"registered aircraft caller clip reset failed");
    malformed=draws;malformed[0].source_width=0;reject_scene({malformed,visibility});
    malformed=draws;malformed[0].source_height=4097;reject_scene({malformed,visibility});
    reject_scene({draws,std::span(visibility).first(visibility.size()-1)});
    // A bounded source rectangle can contain an opaque edge. Its outside
    // pixels must stay clipped even when the image holds more visible artwork.
    write_pack(pack_path,pose(0x100a0,"aircraft.bmp","13 9 60 48 120 48 13 9 0 0 1 1"));
    require(aircraft.load(pack_path),"bounded-source aircraft pack rejected");
    clear(renderer.get());aircraft.draw(scene,320);result=read(renderer.get());
    require(pixel(result,139,80)==solid[0]&&pixel(result,140,80)==background&&pixel(result,150,80)==background,
        "registered aircraft sampled outside explicit source bounds");
    write_pack(pack_path,neutral+"lod 65696 65712 100 40 2 2\n");
    require(aircraft.load(pack_path),"ROM-sized optional LOD registration rejected before source matching");
    draws[0].frame_address=0x100b0;draws[0].source_width=60;draws[0].source_height=24;
    clear(renderer.get());aircraft.draw(scene,320);result=read(renderer.get());
    require(pixel(result,87,72)==background&&pixel(result,88,72)==solid[0]&&pixel(result,80,64)==background,
        "invalid ROM-sized LOD override did not retain base registration");
    place(draws[0]);
    write_pack(pack_path,neutral+bank+annotations);
    require(aircraft.load(pack_path),"registered aircraft fixture reload failed");

    // Every parser/metadata/I/O failure must retain the entire accepted pack,
    // rather than partially committing whichever pose happened to load first.
    const auto rejected_pack=[&](const std::string& rows,const char* message){
        write_pack(pack_path,rows);
        require(!aircraft.load(pack_path)&&!aircraft.load_error().empty()&&aircraft.available(),message);
        require(aircraft.supported_roots().size()==2&&aircraft.poses().size()==2,"rejected pack changed registered pose availability");
        clear(renderer.get());aircraft.draw(scene,320);
        require(pixel(read(renderer.get()),90,72)==solid[0]&&pixel(read(renderer.get()),80,64)==background,
            "rejected aircraft pack changed accepted registration");
        draws[0].frame_address=draws[0].root_frame_address=0x10230;
        clear(renderer.get());aircraft.draw(scene,320);
        require(pixel(read(renderer.get()),100,80)==0xff18e0e0,"rejected aircraft pack partially replaced a pose");
        place(draws[0]);
    };
    rejected_pack("","empty aircraft pack accepted");
    rejected_pack(neutral+neutral,"duplicate aircraft pose accepted");
    rejected_pack(neutral+pose(0x10230,"missing.bmp","13 9 120 48 120 48 73 33 60 24 1 1"),"missing pack image accepted");
    rejected_pack(pose(0x100a0,"aircraft.bmp","5 3 160 64 120 48 17 11 12 8 1 1"),"out-of-image source bounds accepted");
    rejected_pack(pose(0x100a0,"aircraft.bmp","5 3 0 64 120 48 17 11 12 8 1 1"),"empty source bounds accepted");
    rejected_pack(pose(0x100a0,"aircraft.bmp","-1 3 140 64 120 48 17 11 12 8 1 1"),"negative source origin accepted");
    rejected_pack(pose(0x100a0,"aircraft.bmp","5 3 140 64 120 48 200 11 12 8 1 1"),"out-of-source art pivot accepted");
    rejected_pack(pose(0x100a0,"aircraft.bmp","5 3 140 64 120 48 17 11 121 8 1 1"),"out-of-source native pivot accepted");
    rejected_pack(pose(0x100a0,"aircraft.bmp","5 3 140 64 120 48 17 11 12 8 0 1"),"zero aircraft registration scale accepted");
    rejected_pack(pose(0x100a0,"aircraft.bmp","5 3 140 64 120 48 17 11 12 8 1e-35 1"),"tiny aircraft registration scale accepted");
    rejected_pack(pose(0x100a0,"aircraft.bmp","5 3 140 64 120 48 nan 11 12 8 1 1"),"nonfinite aircraft registration accepted");
    rejected_pack(neutral+"nozzle 66096 \"missing\" 73 33 68 30\n","annotation of missing pose accepted");
    rejected_pack(neutral+"nozzle 65696 \"outside\" 200 33 68 30\n","out-of-source nozzle accepted");
    rejected_pack(neutral+"nozzle 65696 \"outside\" 73 33 121 30\n","out-of-source native nozzle accepted");
    rejected_pack(neutral+"nozzle 65696 \"duplicate\" 73 33 68 30\nnozzle 65696 \"duplicate\" 73 33 68 30\n","duplicate nozzle name accepted");
    rejected_pack(neutral+"lod 66096 65712 0 0 2 2\n","LOD of missing pose accepted");
    rejected_pack(neutral+"lod 65696 65712 0 0 0 2\n","zero LOD registration scale accepted");
    rejected_pack(neutral+"lod 65696 65712 0 0 1e-35 2\n","tiny LOD registration scale accepted");
    rejected_pack(neutral+"lod 65696 65712 0 0 2 2\nlod 65696 65712 0 0 2 2\n","duplicate LOD registration accepted");
    rejected_pack(neutral+"unknown 1\n","unknown aircraft pack row accepted");
    rejected_pack(neutral+"pose malformed\n","malformed aircraft pack row accepted");
    rejected_pack(neutral+"nozzle 65696 \"valid\" 73 33 68 30 extra\n","trailing aircraft annotation fields accepted");
    rejected_pack(pose(0x100a0,path.string(),"5 3 140 64 120 48 17 11 12 8 1 1"),"absolute aircraft image path accepted");
    rejected_pack(pose(0x100a0,"../aircraft.bmp","5 3 140 64 120 48 17 11 12 8 1 1"),"parent-escaping aircraft image path accepted");
    Temporary outside{temporary_directory()};save(image,outside.path/"outside.bmp");
    std::filesystem::create_symlink(outside.path/"outside.bmp",directory.path/"escape.bmp");
    rejected_pack(pose(0x100a0,"escape.bmp","5 3 140 64 120 48 17 11 12 8 1 1"),"symlink-escaping aircraft image path accepted");

    // A larger output must preserve authored detail inside one native cell,
    // including every reflection. Collapsing the image to the 320x224 mask or
    // a software flipped blit's native rectangle loses these red/green pairs.
    image=surface(264,112);
    for(unsigned y=0;y<96;++y)for(unsigned x=0;x<240;++x)
        rgba(image,11+x,7+y,(x&1)?std::array<uint8_t,4>{24,224,32,255}:std::array<uint8_t,4>{224,24,32,255});
#if SDL_VERSION_ATLEAST(3,4,0)
    const auto high_path=directory.path/"aircraft.png";
    require(SDL_SavePNG(image.get(),high_path.string().c_str()),"aircraft PNG fixture save failed");
#else
    const auto high_path=directory.path/"aircraft-hd.bmp";save(image,high_path);
#endif
    const auto translucent_path=directory.path/"aircraft-alpha-hd.bmp";
    auto translucent_image=surface(264,112);
    for(unsigned y=0;y<96;++y)for(unsigned x=0;x<240;++x)rgba(translucent_image,11+x,7+y,{240,192,24,128});
    save(translucent_image,translucent_path);
    for(unsigned output_mode:{0u,1u,2u})for(unsigned width:{320u,358u,398u,522u}){
        const bool three_x=output_mode==0,fractional=output_mode==2;
        if(fractional&&width==320)continue; // Already covered by exact 3x.
        const unsigned output_width=three_x?width*3:fractional?960:3840,output_height=output_mode==1?2160:672;
        Surface high_canvas=surface(output_width,output_height);
        Window high_window(gpu?SDL_CreateWindow("HD aircraft renderer regression",output_width,output_height,SDL_WINDOW_HIDDEN):nullptr);
        require(!gpu || bool(high_window),"HD aircraft GPU window creation failed");
        Renderer high(gpu?SDL_CreateGPURenderer(device.get(),high_window.get()):SDL_CreateSoftwareRenderer(high_canvas.get()));
        require(bool(high),"HD aircraft renderer failed");
        Frontend::NativePlayerAircraft high_aircraft(high.get());
        require(high_aircraft.load(high_path),"valid HD aircraft rejected");
        for(bool registered_high:{false,true})for(bool translucent_high:{false,true}){
        if(translucent_high&&!fractional)continue;
        const auto& authored_path=translucent_high?translucent_path:high_path;
        if(registered_high){
            const auto high_pack=directory.path/"high.aircraft";
            write_pack(high_pack,pose(0x100a0,authored_path.filename().string(),"0 0 264 112 120 48 11 7 0 0 2 2"));
            require(high_aircraft.load(high_pack),"registered HD aircraft pack rejected");
        }else require(high_aircraft.load(authored_path),"HD aircraft fixture reload failed");
        for(bool horizontal:{false,true})for(bool vertical:{false,true}){
        place(draws[0],horizontal,vertical);
        if(fractional&&translucent_high)visibility[90*320+110]=0;
        require(SDL_SetRenderLogicalPresentation(high.get(),width,224,SDL_LOGICAL_PRESENTATION_LETTERBOX),"HD aircraft logical layout failed");
        clear(high.get());high_aircraft.draw(scene,width);
        SDL_FRect presentation;require(SDL_GetRenderLogicalPresentationRect(high.get(),&presentation),"HD aircraft layout query failed");
        // SDL 3.4.16 UpdatePixelViewport floors the logical offset before
        // rasterization. A 960x672 / 522x224 layout reports y=130.5 but
        // starts its physical viewport at row 130 on both renderers.
        presentation.x=std::floor(presentation.x);presentation.y=std::floor(presentation.y);
        require(SDL_SetRenderLogicalPresentation(high.get(),0,0,SDL_LOGICAL_PRESENTATION_DISABLED),"HD aircraft readback viewport failed");
        result=read(high.get());if(fractional&&translucent_high)fill_mask(visibility);
        const float scale=presentation.w/width,origin=float(width-320)/2;
        const unsigned y=unsigned(presentation.y+80.5f*(fractional?presentation.h/224:scale));
        const unsigned red_x=unsigned(presentation.x+(origin+100.25f)*scale),green_x=unsigned(presentation.x+(origin+100.75f)*scale);
        const auto left=pixel(result,red_x,y),right=pixel(result,green_x,y);
        const auto red=horizontal?right:left,green=horizontal?left:right;
        bool detail=((red>>16)&255)>((green>>16)&255)+50 && ((green>>8)&255)>((red>>8)&255)+50;
        if(fractional){
            // Every physical pixel center in this inset native rectangle is
            // owned and samples fully opaque authored art. Fractional output
            // scaling must not leave background cracks between native rows.
            const float vertical_scale=presentation.h/224;
            const unsigned x0=unsigned(std::ceil(presentation.x+(origin+81)*scale-0.5f));
            const unsigned x1=unsigned(std::ceil(presentation.x+(origin+199)*scale-0.5f));
            const unsigned y0=unsigned(std::ceil(presentation.y+65*vertical_scale-0.5f));
            const unsigned y1=unsigned(std::ceil(presentation.y+111*vertical_scale-0.5f));
            const unsigned hole_x0=unsigned(std::ceil(presentation.x+(origin+110)*scale-0.5f));
            const unsigned hole_x1=unsigned(std::ceil(presentation.x+(origin+111)*scale-0.5f));
            const unsigned hole_y0=unsigned(std::ceil(presentation.y+90*vertical_scale-0.5f));
            const unsigned hole_y1=unsigned(std::ceil(presentation.y+91*vertical_scale-0.5f));
            for(unsigned py=y0;py<y1;++py)for(unsigned px=x0;px<x1;++px){
                const bool hole=translucent_high&&px>=hole_x0&&px<hole_x1&&py>=hole_y0&&py<hole_y1;
                if(hole){require(pixel(result,px,py)==background,"fractional HD aircraft filled an unowned mask cell");continue;}
                if(translucent_high?!near(pixel(result,px,py),0xff7c6614):pixel(result,px,py)==background)
                    throw std::runtime_error(std::string(translucent_high?
                        "fractional HD aircraft alpha seam was missing or blended twice":"fractional HD aircraft lost an owned output pixel")+
                        "; viewport "+std::to_string(width)+" x "+std::to_string(px)+" y "+std::to_string(py));
            }
            // Pixel-center ownership is half-open at each native rectangle
            // edge. Closing cracks must not paint a neighbouring unowned cell.
            const unsigned left_edge=unsigned(std::ceil(presentation.x+(origin+80)*scale-0.5f));
            const unsigned right_edge=unsigned(std::ceil(presentation.x+(origin+200)*scale-0.5f));
            const unsigned top_edge=unsigned(std::ceil(presentation.y+64*vertical_scale-0.5f));
            const unsigned bottom_edge=unsigned(std::ceil(presentation.y+112*vertical_scale-0.5f));
            for(unsigned py=top_edge;py<bottom_edge;++py)
                require(pixel(result,left_edge-1,py)==background&&pixel(result,right_edge,py)==background,
                    "fractional HD aircraft crossed half-open horizontal ownership");
            for(unsigned px=left_edge;px<right_edge;++px)
                if(pixel(result,px,top_edge-1)!=background||pixel(result,px,bottom_edge)!=background)
                    throw std::runtime_error("fractional HD aircraft crossed half-open vertical ownership; viewport "+
                        std::to_string(width)+" x "+std::to_string(px)+" top "+std::to_string(top_edge)+
                        " bottom "+std::to_string(bottom_edge)+" registered "+std::to_string(registered_high)+
                        " translucent "+std::to_string(translucent_high));
        }
        if(translucent_high)continue;
        if(three_x||fractional){
            // At 3x, a 2x source alternates between output sample phases.
            // Measure every output sample whose center lies inside each
            // native cell, then average its red/green intensity range;
            // a native-resolution intermediate produces zero contrast.
            int red_contrast=0,green_contrast=0;
            for(unsigned cell=90;cell<122;++cell){
                const unsigned x0=unsigned(std::ceil(presentation.x+(origin+cell)*scale-0.5f));
                const unsigned x1=unsigned(std::ceil(presentation.x+(origin+cell+1)*scale-0.5f));
                unsigned min_red=255,max_red=0,min_green=255,max_green=0;
                for(unsigned x=x0;x<x1;++x){
                    const auto value=pixel(result,x,y);const unsigned r=(value>>16)&255,g=(value>>8)&255;
                    min_red=std::min(min_red,r);max_red=std::max(max_red,r);
                    min_green=std::min(min_green,g);max_green=std::max(max_green,g);
                }
                red_contrast+=max_red-min_red;green_contrast+=max_green-min_green;
            }
            detail=red_contrast>32*50&&green_contrast>32*50;
            if(!detail)std::cerr<<"3x aircraft subpixel contrast: "<<red_contrast/32.0<<", "<<green_contrast/32.0
                <<"; pair "<<std::hex<<left<<", "<<right<<std::dec<<"; sample "<<red_x<<", "<<y
                <<"; presentation "<<presentation.x<<", "<<presentation.y<<", "<<presentation.w<<", "<<presentation.h<<'\n';
        }
        if(!detail)std::cerr<<"HD aircraft sample pair: "<<std::hex<<left<<", "<<right<<std::dec<<'\n';
        if(!detail)throw std::runtime_error(std::string(horizontal||vertical?
            "reflected HD aircraft lost detail inside a native mask cell":"HD aircraft lost detail inside a native mask cell or viewport alignment")+
            "; output "+std::to_string(output_width)+" viewport "+std::to_string(width)+" horizontal "+std::to_string(horizontal)+" vertical "+std::to_string(vertical));
        require(pixel(result,unsigned(presentation.x+(origin+79)*scale),y)==background,
            "HD aircraft escaped native coverage");
        }
        }
    }
    std::cout<<"Native aircraft ("<<SDL_GetRendererName(renderer.get())<<"): transparent BMP/PNG bounds, registered source/pivot/nozzles/LOD, per-pose dispatch, transactional image/pack rejection, premultiplied alpha, flips, native/caller clips, malformed scenes and HD detail across 320/358/398/522 viewports passed.\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
