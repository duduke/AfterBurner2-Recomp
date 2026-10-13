#include <SDL3/SDL.h>
#include <iostream>
#include <fstream>
#include <vector>
#include "frontend/gpu_sprite_filter.hpp"
int main(int argc,char** argv)try {
    if(argc>2)throw std::runtime_error("Usage: gpu_sprite_filter_regression [native-plane-fixture]");
    if(!SDL_Init(SDL_INIT_VIDEO))throw std::runtime_error(SDL_GetError());
    auto* device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_SPIRV,true,nullptr);
    if(!device)throw std::runtime_error(SDL_GetError());
    auto* renderer=SDL_CreateGPURenderer(device,nullptr);if(!renderer)throw std::runtime_error(SDL_GetError());
    if(argc==2)std::cout<<"Native fixture replay: "<<argv[1]<<'\n';
    std::cout<<"GPU scaler backend: "<<SDL_GetGPUDeviceDriver(device)<<'\n';
    unsigned compared=0;uint64_t pixels=0;
    {
        Frontend::GPUSpriteScaler gpu(renderer);Frontend::SpriteScaler cpu;
        for(const auto [w,h]:{std::pair{1u,1u},std::pair{2u,3u},std::pair{7u,9u},std::pair{32u,24u},std::pair{320u,224u}}) {
            std::vector<uint32_t> sprites(w*h),background(w*h);std::vector<uint8_t> protection(w*h);
            auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w*3,h*3);
            if(!target)throw std::runtime_error(SDL_GetError());
            for(unsigned pattern=0;pattern<21;++pattern) {
                uint32_t random=0x12345678;
                for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){
                    random=random*1664525+1013904223;const auto i=y*w+x;
                    constexpr uint32_t colors[]{0xff000000,0xffffffff,0xffa04080,0xff102080,0xff70cc44};
                    const auto k=pattern==0?x>y:pattern==1?((x/3+y/3)%2):pattern==2?(x==y || x+y==25):((random>>24)%5);
                    sprites[i]=pattern<4?colors[k]:pattern==4?(x>y?colors[(x/3)%5]:0):((x+y)%4?colors[k]:0);
                    if(pattern==6)sprites[i]=0x80402010; // premultiplied partial alpha
                    if(pattern==7)sprites[i]=0;
                    if(pattern==8)sprites[i]=0xff000000|(random&0xffffff);
                    if(pattern==9){const unsigned a=random>>24; sprites[i]=(a<<24)|(((random&255)*a/255)<<16)|((((random>>8)&255)*a/255)<<8)|(((random>>16)&255)*a/255);}
                    background[i]=0xff000000|(random&0xffffff);protection[i]=pattern>=5 && (x+y)%7==0;
                }
                if(argc==2 && w==320 && h==224 && pattern==0) {
                    std::ifstream fixture(argv[1],std::ios::binary);
                    fixture.read(reinterpret_cast<char*>(background.data()),background.size()*4);
                    fixture.read(reinterpret_cast<char*>(sprites.data()),sprites.size()*4);
                    fixture.read(reinterpret_cast<char*>(protection.data()),protection.size());
                    if(!fixture)throw std::runtime_error("Invalid native sprite fixture");
                }
                std::vector<uint32_t> heads;std::vector<Presentation::Fragment> fragments;
                const unsigned cloud_alpha=pattern==10?0:pattern==12?255:160,trail_alpha=pattern==10?0:112;
                const unsigned explosion_alpha=pattern==15?0:pattern==17?255:pattern==19?Presentation::explosion_opacity:176,afterburner_alpha=pattern==16?0:pattern==17?255:pattern==20?Presentation::afterburner_opacity:192;
                auto reference_background=background;
                if(pattern>=10){heads.assign(w*h,UINT32_MAX);
                    for(unsigned i=0;i<w*h;++i){const auto add=[&](uint32_t color,unsigned kind){fragments.push_back({color,heads[i],kind,0});heads[i]=uint32_t(fragments.size()-1);};
                        if(i%3==0)add(0xff0030d0,0);
                        if(pattern==13 && i%2)add(background[i],3);
                        if(pattern==14 && i%2)add(0xff080810,4);if(i%4!=0)add(0xffeeeeee,2);if(i%5!=0)add(0xffff9000,1);
                        if(pattern>=15){if(i%3)add(0xffcc5000,5);if(i%5)add(0xffffb000,6);}
                        sprites[i]=Presentation::foreground(fragments,heads[i],cloud_alpha,trail_alpha,&reference_background[i],explosion_alpha,afterburner_alpha);
                    }
                }
                for(unsigned f=0;f<Frontend::sprite_filter_count;++f){
                    const auto filter=static_cast<Frontend::SpriteFilter>(f);const auto factor=Frontend::sprite_factor(filter);
                    // Alternate sizes/filters across calls to exercise resource recreation.
                    SDL_SetRenderTarget(renderer,target);
                    auto* output=gpu.compose(background,sprites,w,h,filter,protection,heads,fragments,cloud_alpha,trail_alpha,explosion_alpha,afterburner_alpha);
                    const auto expected=cpu.compose(reference_background,sprites,w,h,filter,protection);
                    SDL_SetRenderTarget(renderer,target);SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
                    SDL_FRect destination{0,0,float(w*factor),float(h*factor)};
                    if(!SDL_RenderTexture(renderer,output,nullptr,&destination))throw std::runtime_error(SDL_GetError());
                    SDL_Rect area{0,0,int(w*factor),int(h*factor)};
                    auto* read=SDL_RenderReadPixels(renderer,&area);if(!read)throw std::runtime_error(SDL_GetError());
                    auto* argb=SDL_ConvertSurface(read,SDL_PIXELFORMAT_ARGB8888);SDL_DestroySurface(read);
                    if(!argb)throw std::runtime_error(SDL_GetError());
                    for(unsigned y=0;y<h*factor;++y)for(unsigned x=0;x<w*factor;++x){
                        const auto actual=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(argb->pixels)+y*argb->pitch)[x];
                        if(actual!=expected[y*w*factor+x]){
                            std::cerr<<"Mismatch "<<w<<'x'<<h<<" pattern="<<pattern<<" filter="<<f<<" pixel="<<x<<','<<y<<" GPU="<<std::hex<<actual<<" CPU="<<expected[y*w*factor+x]<<std::dec<<'\n';
                            SDL_DestroySurface(argb);throw std::runtime_error("GPU scaler differs from CPU reference");
                        }
                    }
                    SDL_DestroySurface(argb);SDL_SetRenderTarget(renderer,nullptr);++compared;pixels+=expected.size();
                }
            }
            SDL_DestroyTexture(target);
        }
    }
    SDL_DestroyRenderer(renderer);SDL_DestroyGPUDevice(device);SDL_Quit();
    std::cout<<"GPU sprite filters: "<<compared<<" of "<<compared<<" identical, "<<pixels<<" pixels, 0 skipped.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';SDL_Quit();return 1;}
