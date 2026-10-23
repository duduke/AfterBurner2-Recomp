#include <SDL3/SDL.h>
#include "frontend/native_slang.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
static void require(bool value,const std::string& why){if(!value)throw std::runtime_error(why+": "+SDL_GetError());}
static uint64_t image(SDL_Renderer* renderer,bool* lit=nullptr){
 auto* s=SDL_RenderReadPixels(renderer,nullptr);require(s,"read pixels");auto* rgba=SDL_ConvertSurface(s,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(s);require(rgba,"convert pixels");
 uint64_t hash=1469598103934665603ull;uint64_t energy=0;for(int y=0;y<rgba->h;++y){auto* p=(unsigned char*)rgba->pixels+y*rgba->pitch;for(int x=0;x<rgba->w*4;++x){hash^=p[x];hash*=1099511628211ull;if((x&3)!=3)energy+=p[x];}}
 if(lit)*lit=energy>uint64_t(rgba->w)*rgba->h*8;SDL_DestroySurface(rgba);return hash;
}
int main(int argc,char** argv){try{
 const std::filesystem::path output=SDL_getenv("AB2_SLANG_TEST_OUTPUT")?SDL_getenv("AB2_SLANG_TEST_OUTPUT"):"build/oracle/retroarch-probe";std::filesystem::create_directories(output);
 require(SDL_Init(SDL_INIT_VIDEO),"video");auto* w=SDL_CreateWindow("Slang renderer integration",960,672,SDL_WINDOW_HIDDEN);require(w,"window");
 auto* r=SDL_CreateRenderer(w,"metal");require(r,"native Metal renderer");
 {
 Frontend::NativeSlang shader(r,w);require(!shader.available(),"no implicit preset");
 require(!shader.load("missing.glslp"),"reject other formats");require(!shader.available(),"format error has no chain");
 auto* target=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,960,672);require(target,"target");
 auto* source=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,320,224);require(source,"source");
 std::vector<uint32_t> pixels(320*224);for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x){unsigned v=((x/8+y/8)&1)?224:32;pixels[y*320+x]=0xff000000u|((x*255/319)<<16)|((y*255/223)<<8)|v;}
 require(SDL_UpdateTexture(source,nullptr,pixels.data(),1280),"source update");require(SDL_SetTextureScaleMode(source,SDL_SCALEMODE_NEAREST),"nearest");
 require(SDL_SetRenderTarget(r,target),"test target");require(SDL_SetRenderLogicalPresentation(r,320,224,SDL_LOGICAL_PRESENTATION_LETTERBOX),"logical");
 auto draw=[&]{SDL_SetRenderDrawColor(r,0,0,0,255);require(SDL_RenderClear(r),"clear");require(SDL_RenderTexture(r,source,nullptr,nullptr),"source draw");};
 draw();const auto baseline=image(r);
 std::vector<std::string> paths{"tests/fixtures/native_slang/reference.slangp"};for(int i=1;i<argc;++i)paths.emplace_back(argv[i]);
 for(const auto& path:paths){
  require(shader.load(path),shader.error());require(shader.available(),"loaded");require(shader.needs_frame(50),"initial frame dirty");
  const auto state_target=SDL_GetRenderTarget(r);const SDL_Rect viewport{3,4,310,210},clip{9,10,250,180};
  require(SDL_SetRenderViewport(r,&viewport)&&SDL_SetRenderClipRect(r,&clip)&&SDL_SetRenderScale(r,1.125f,1.25f),"nondefault state");
  require(shader.begin(320,50),shader.error());draw();require(shader.end(50,59.6374,12345),shader.error());
  require(SDL_GetRenderTarget(r)==state_target,"target restored");int lw{},lh{};SDL_RendererLogicalPresentation lm{};float sx{},sy{};SDL_Rect vp{},cp{};
  require(SDL_GetRenderLogicalPresentation(r,&lw,&lh,&lm)&&lw==320&&lh==224&&lm==SDL_LOGICAL_PRESENTATION_LETTERBOX,"logical restored");
  require(SDL_GetRenderScale(r,&sx,&sy)&&sx==1.125f&&sy==1.25f,"scale restored");
  require(SDL_GetRenderViewport(r,&vp)&&vp.x==viewport.x&&vp.y==viewport.y&&vp.w==viewport.w&&vp.h==viewport.h,"viewport restored");
  require(SDL_GetRenderClipRect(r,&cp)&&cp.x==clip.x&&cp.y==clip.y&&cp.w==clip.w&&cp.h==clip.h,"clip restored");
  require(!shader.needs_frame(50),"same-frame shader history cached");require(SDL_SetRenderViewport(r,nullptr)&&SDL_SetRenderClipRect(r,nullptr)&&SDL_SetRenderScale(r,1,1),"reset assertion readback state");bool lit=false;const auto filtered=image(r,&lit);require(lit&&filtered!=baseline,"shader alters original and remains visible");
  if(path=="tests/fixtures/native_slang/reference.slangp"){
   auto* sample=SDL_RenderReadPixels(r,nullptr);require(sample,"reference output");auto* rgba=SDL_ConvertSurface(sample,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(sample);require(rgba,"reference format");
   for(unsigned y=0;y<672;++y)for(unsigned x=0;x<960;++x){const auto native=pixels[(y/3)*320+x/3];const auto* p=(unsigned char*)rgba->pixels+y*rgba->pitch+x*4;require(p[0]==(native&255)&&p[1]==((native>>8)&255)&&p[2]==((native>>16)&255)&&p[3]==255,"every channel-mapped source pixel identical");}SDL_DestroySurface(rgba);
  }
  shader.draw_cached();require(image(r)==filtered,"cached redraw is identical");shader.invalidate();require(shader.needs_frame(50),"same-frame visual invalidation");
  require(!shader.load("missing.slangp")&&shader.available(),"failed preset preserves working chain");
  require(!shader.set_parameter("__unknown",1),"unknown parameter rejected");
  if(!shader.parameters().empty()){const auto p=shader.parameters().front();require(!shader.set_parameter(p.name,p.maximum+1),"out of range rejected");require(shader.set_parameter(p.name,p.value),shader.error());}
  require(SDL_SetRenderViewport(r,nullptr)&&SDL_SetRenderClipRect(r,nullptr)&&SDL_SetRenderScale(r,1,1),"reset state");
  require(SDL_SetRenderLogicalPresentation(r,398,224,SDL_LOGICAL_PRESENTATION_LETTERBOX),"wide logical");require(shader.begin(398,51),shader.error());draw();require(shader.end(51,59.6374,12345,true),shader.error());require(!shader.needs_frame(51),"wide cache");
  auto* preview=SDL_RenderReadPixels(r,nullptr);require(preview,"preview");SDL_SaveBMP(preview,(output/(std::filesystem::path(path).stem().string()+".bmp")).string().c_str());SDL_DestroySurface(preview);require(SDL_SetRenderLogicalPresentation(r,320,224,SDL_LOGICAL_PRESENTATION_LETTERBOX),"restore logical");
  printf("PASS %s: live pipeline, visible output, renderer state, cached history, invalidation, transactional failure, parameters and widescreen\n",std::filesystem::path(path).filename().string().c_str());fflush(stdout);
 }
 // Independent time-uniform assertion rejects millisecond conversion.
 require(shader.load("tests/fixtures/native_slang/timing.slangp"),shader.error());
 require(shader.begin(320,80),shader.error());draw();require(shader.end(80,59.6374,12345),shader.error());
 auto* timing=SDL_RenderReadPixels(r,nullptr);require(timing,"timing output");auto* tr=SDL_ConvertSurface(timing,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(timing);require(tr,"timing format");
 auto* middle=(unsigned char*)tr->pixels+(tr->h/2)*tr->pitch+(tr->w/2)*4;require(middle[0]==0&&middle[1]==255&&middle[2]==0,"FrameTimeDelta binds raw microseconds");SDL_DestroySurface(tr);
 // Resize and 4K rendering use the real output area, including letterboxing.
 require(shader.load("tests/fixtures/native_slang/reference.slangp"),shader.error());
 for(const auto size:std::array<SDL_Point,3>{{{640,448},{3840,2160},{960,672}}}){
  require(SDL_SetRenderTarget(r,nullptr),"detach resize target");SDL_DestroyTexture(target);
  target=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_TARGET,size.x,size.y);require(target,"resize target");
  require(SDL_SetRenderTarget(r,target)&&SDL_SetRenderLogicalPresentation(r,320,224,SDL_LOGICAL_PRESENTATION_LETTERBOX),"resize coordinates");
  require(shader.needs_frame(80),"resize invalidates cached shader");require(shader.begin(320,80),shader.error());draw();require(shader.end(80,59.6374,12345),shader.error());
  bool visible=false;image(r,&visible);require(visible&&!shader.needs_frame(80),"resized output visible and cached");
 }
 // Remove only our scratch preset after loading. A failed reset must show the
 // intact captured source and restore all renderer state.
 const std::filesystem::path scratch=output/"fallback";std::filesystem::create_directories(scratch);
 std::filesystem::copy_file("tests/fixtures/native_slang/reference.slang",scratch/"reference.slang",std::filesystem::copy_options::overwrite_existing);
 std::filesystem::copy_file("tests/fixtures/native_slang/reference.slangp",scratch/"reference.slangp",std::filesystem::copy_options::overwrite_existing);
 require(shader.load((scratch/"reference.slangp").string()),shader.error());
 require(shader.begin(320,81),shader.error());draw();require(shader.end(81,59.6374,12345),shader.error());
 std::filesystem::remove(scratch/"reference.slangp");shader.invalidate();require(shader.begin(320,82),shader.error());draw();
 require(!shader.end(82,59.6374,12345),"missing reset preset fails visibly");require(SDL_GetRenderTarget(r)==target,"failure restores target");require(image(r)==baseline,"failure retains original game image");
 require(shader.load("tests/fixtures/native_slang/reference.slangp"),shader.error());require(shader.begin(320,83),shader.error());draw();require(shader.end(83,59.6374,12345),shader.error());require(shader.error().empty(),"successful frame clears transient error");
 // Genuine temporal dependence: red then blue must retain half the preceding
 // input. Cached UI redraws leave that history intact; invalidate clears it.
 require(shader.load("tests/fixtures/native_slang/history.slangp"),shader.error());
 auto solid=[&](uint32_t color){std::fill(pixels.begin(),pixels.end(),color);require(SDL_UpdateTexture(source,nullptr,pixels.data(),1280),"temporal source");};
 auto temporal_pixel=[&](int red,int green,int blue){auto* a=SDL_RenderReadPixels(r,nullptr);require(a,"temporal output");auto* b=SDL_ConvertSurface(a,SDL_PIXELFORMAT_RGBA32);SDL_DestroySurface(a);require(b,"temporal format");const auto* p=(unsigned char*)b->pixels+(b->h/2)*b->pitch+(b->w/2)*4;require(std::abs(int(p[0])-red)<=1&&std::abs(int(p[1])-green)<=1&&std::abs(int(p[2])-blue)<=1&&p[3]==255,"temporal RGB matches previous original frame");SDL_DestroySurface(b);};
 solid(0xffff0000u);require(shader.begin(320,90),shader.error());draw();require(shader.end(90,59.6374,12345),shader.error());temporal_pixel(128,0,0);
 solid(0xff0000ffu);require(shader.begin(320,91),shader.error());draw();require(shader.end(91,59.6374,12345),shader.error());temporal_pixel(128,0,128);
 const auto temporal_hash=image(r);require(!shader.needs_frame(91),"temporal same-frame cache");shader.draw_cached();require(image(r)==temporal_hash,"cached redraw preserves temporal image");
 shader.invalidate();require(shader.needs_frame(91),"temporal reset invalidates same frame");require(shader.begin(320,91),shader.error());draw();require(shader.end(91,59.6374,12345),shader.error());temporal_pixel(0,0,128);
 printf("PASS authored references: raw microseconds, resize through 4K, visible original on runtime failure, successful recovery, real previous-frame history and reset\n");
 require(SDL_SetRenderTarget(r,nullptr),"reset target");SDL_DestroyTexture(source);SDL_DestroyTexture(target);
 }
 SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();return 0;
 }catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());SDL_Quit();return 1;}}
