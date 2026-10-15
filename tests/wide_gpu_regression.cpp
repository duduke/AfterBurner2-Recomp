#include "frontend/gpu_sprite_filter.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
void require(bool c,const char* m){if(!c)throw std::runtime_error(m);}
int main(int argc,char** argv)try{
    require(argc<=2,"Usage: wide_gpu_regression [wide-scene-fixture]");
    const char* path=argc==2?argv[1]:"build/oracle/widescreen/scene.bin";
    std::cout<<"Wide GPU fixture: "<<path<<'\n';
    std::ifstream f(path,std::ios::binary);char magic[8];uint32_t dim[3];
    require(bool(f.read(magic,8))&&!memcmp(magic,"AB2WIDE1",8)&&bool(f.read(reinterpret_cast<char*>(dim),sizeof(dim))),"wide GPU fixture missing");
    const unsigned w=dim[0],h=dim[1],count=dim[2],n=w*h;require(w==398&&h==224&&count<8000000,"wide GPU fixture dimensions");
    std::vector<uint32_t> bg(n),sp(n),heads(n);std::vector<uint8_t> protection(n);std::vector<Presentation::Fragment> fragments(count);
    for(auto* data:{&bg,&sp,&heads})require(bool(f.read(reinterpret_cast<char*>(data->data()),n*4)),"wide GPU planes truncated");
    require(bool(f.read(reinterpret_cast<char*>(protection.data()),n))&&bool(f.read(reinterpret_cast<char*>(fragments.data()),count*sizeof(Presentation::Fragment)))&&f.peek()==EOF,"wide GPU scene truncated");
    require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());auto* window=SDL_CreateWindow("16:9 renderer validation",796,448,0);require(window,"wide GPU window unavailable");
    auto* device=SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL|SDL_GPU_SHADERFORMAT_SPIRV,true,nullptr);require(device,"wide GPU backend unavailable");
    auto* renderer=SDL_CreateGPURenderer(device,window);require(renderer,"wide GPU renderer unavailable");uint64_t pixels=0;
    {
        Frontend::GPUSpriteScaler gpu(renderer);Frontend::SpriteScaler cpu;
        for(unsigned policy=0;policy<2;++policy)for(unsigned filter=0;filter<Frontend::sprite_filter_count;++filter){
            unsigned cloud=policy?160:255,trail=policy?112:255,explosion=policy?128:255,flame=policy?128:255;
            std::vector<uint32_t> background=bg,foreground(n);for(unsigned i=0;i<n;++i)foreground[i]=Presentation::foreground(fragments,heads[i],cloud,trail,&background[i],explosion,flame);
            auto expected=cpu.compose(background,foreground,w,h,static_cast<Frontend::SpriteFilter>(filter),protection);
            auto* output=gpu.compose(bg,sp,w,h,static_cast<Frontend::SpriteFilter>(filter),protection,heads,fragments,cloud,trail,explosion,flame);
            const unsigned factor=Frontend::sprite_factor(static_cast<Frontend::SpriteFilter>(filter));auto* target=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,w*factor,h*factor);
            require(target&&SDL_SetRenderTarget(renderer,target)&&SDL_SetRenderLogicalPresentation(renderer,0,0,SDL_LOGICAL_PRESENTATION_DISABLED),"wide GPU readback target");
            require(SDL_RenderTexture(renderer,output,nullptr,nullptr),"wide GPU draw failed");auto* raw=SDL_RenderReadPixels(renderer,nullptr);require(raw,"wide GPU readback failed");auto* image=SDL_ConvertSurface(raw,SDL_PIXELFORMAT_ARGB8888);SDL_DestroySurface(raw);require(image,"wide GPU pixel conversion");
            require(image->w==int(w*factor)&&image->h==int(h*factor),"wide GPU dimensions changed");
            for(unsigned y=0;y<h*factor;++y){auto* row=reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(image->pixels)+y*image->pitch);for(unsigned x=0;x<w*factor;++x){if(row[x]!=expected[y*w*factor+x]){std::cerr<<"policy="<<policy<<" filter="<<filter<<" x="<<x<<" y="<<y<<'\n';throw std::runtime_error("wide GPU differs from CPU");}++pixels;}}
            SDL_DestroySurface(image);SDL_SetRenderTarget(renderer,nullptr);SDL_DestroyTexture(target);
        }
    }
    SDL_DestroyRenderer(renderer);SDL_DestroyGPUDevice(device);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"Wide GPU: 12/12 native wide scenes across all six filters/opacity policies identical to CPU, "<<pixels<<" pixels, 0 skipped\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}
