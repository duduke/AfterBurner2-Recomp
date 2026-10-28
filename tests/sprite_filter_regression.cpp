#include "frontend/sprite_filter.hpp"
#include "hwvideo/sprite_scene.hpp"
#include <chrono>
#include <iostream>
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif
#include "frontend/scalers/2xsai.reference.cpp"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
uint16_t rgb565(uint32_t p){return ((p>>8)&0xf800)|((p>>5)&0x07e0)|((p>>3)&31);}
uint32_t argb(uint16_t p){return 0xff000000u|uint32_t(p&0xf800)<<8|uint32_t(p&0x07e0)<<5|uint32_t(p&31)<<3;}
int main(int argc,char** argv)try{
 // Full linked lists are the independent reference for terminal compaction.
 // Interleave pixels so accidental sharing/reuse of another head is visible.
 unsigned compact_comparisons=0;size_t compact_size=0,full_size=0;
 for(unsigned sequence=0;sequence<2401;++sequence){
  std::vector<Presentation::Fragment> full,compact;
  std::array<uint32_t,3> original_heads,compact_heads;original_heads.fill(UINT32_MAX);compact_heads.fill(UINT32_MAX);
  unsigned kinds=sequence;
  for(unsigned depth=0;depth<4;++depth){const unsigned kind=kinds%7;kinds/=7;
   for(unsigned pixel=0;pixel<3;++pixel){
    Presentation::Fragment f{0xff000000u|((depth+1)*0x192b37u)^(pixel*0x152713u),original_heads[pixel],kind,depth*3+pixel};
    original_heads[pixel]=uint32_t(full.size());full.push_back(f);
    Presentation::append_fragment(compact,compact_heads[pixel],f);
   }
  }
  for(unsigned cloud:{0u,128u,255u})for(unsigned trail:{0u,128u,255u})for(unsigned explosion:{0u,128u,255u})for(unsigned flame:{0u,128u,255u})for(unsigned pixel=0;pixel<3;++pixel){
   uint32_t a=0xffabcdef,b=a;
   const auto expected=Presentation::foreground(full,original_heads[pixel],cloud,trail,&a,explosion,flame);
   const auto actual=Presentation::foreground(compact,compact_heads[pixel],cloud,trail,&b,explosion,flame);
   require(expected==actual&&a==b,"terminal fragment compaction changed opacity/ownership");++compact_comparisons;
  }
  compact_size+=compact.size();full_size+=full.size();
 }
 require(compact_size<full_size,"terminal fragment compaction retained all hidden fragments");
 std::cout<<"Terminal fragments: "<<compact_comparisons<<" full-chain foreground/background comparisons identical\n";
 if(argc==2&&std::string_view(argv[1])=="--fragments-only")return 0;
 using Frontend::SpriteFilter;Frontend::SpriteScaler scaler;
 const std::vector<uint32_t> background(9,0xff305070),empty(9,0);
 for(int f=0;f<Frontend::sprite_filter_count;++f){
  const auto filter=static_cast<SpriteFilter>(f);const auto n=Frontend::sprite_factor(filter);
  const auto output=scaler.compose(background,empty,3,3,filter);
  require(output.size()==9*n*n && std::all_of(output.begin(),output.end(),[](auto p){return p==0xff305070;}),"transparent sprite changed background");
  const std::vector<uint32_t> opaque(9,0xffabcd12);
  const auto solid=scaler.compose(background,opaque,3,3,filter);
  require(std::all_of(solid.begin(),solid.end(),[](auto p){return p==0xffabcd12;}),"solid sprite changed at clamped edges");
  const std::vector<uint8_t> protection(9,1);
  const auto protected_output=scaler.compose(background,opaque,3,3,filter,protection);
  require(std::all_of(protected_output.begin(),protected_output.end(),[](auto p){return p==0xff305070;}),"sprite filtering covered protected text/shadow");
 }
 // Published ScaleNx corner: matching north/west replace only the NW corner.
 const uint32_t white=0xffffffffu,red=0xffff0000u,blue=0xff0000ffu;
 const std::vector<uint32_t> corner{white,red,white,red,white,blue,white,blue,white};
 for(auto f:{SpriteFilter::epx,SpriteFilter::scale3x}){
  const auto n=Frontend::sprite_factor(f);const auto output=scaler.compose(background,corner,3,3,f);
  require(output[n*3*n+n]==red && output[(n*2-1)*3*n+n*2-1]==blue,"ScaleNx corner rule failed");
 }
 // Premultiplied transparency must reveal background without a black fringe.
 const std::vector<uint32_t> semi(9,0x80800000u);
 const auto blended=scaler.compose(background,semi,3,3,SpriteFilter::none);
 require(blended[4]==0xff982838u,"premultiplied alpha composition failed");
 // A uniform EPX neighbourhood leaves every source pixel unchanged. Compare
 // its retained composition path with Off across every alpha and protection.
 for(unsigned alpha=0;alpha<256;++alpha)for(uint8_t protect:{uint8_t(0),uint8_t(1)}){
  const std::vector<uint32_t> bg1{0xff305070u},fg1{(alpha<<24)|(alpha<<16)|((alpha/2)<<8)|(alpha/3)};
  const std::vector<uint8_t> mask1{protect};
  const auto off=scaler.compose(bg1,fg1,1,1,SpriteFilter::none,mask1)[0];
  const auto epx=scaler.compose(bg1,fg1,1,1,SpriteFilter::epx,mask1);
  require(std::all_of(epx.begin(),epx.end(),[off](auto p){return p==off;}),"Off differs from unchanged uniform EPX composition");
 }
 bool rejected=false;try{scaler.compose({},empty,3,3,SpriteFilter::epx);}catch(const std::invalid_argument&){rejected=true;}
 require(rejected,"invalid sprite planes were accepted");
 // Compare adaptations to unchanged upstream 16-bit Snes9x entry points.
 constexpr int w=16,h=12,stride=w+3;std::vector<uint32_t> pixels(w*h),bg(w*h,0xff000000);
 std::vector<uint16_t> padded((h+3)*stride),expected(w*h*4);
 uint32_t random=0x19283746;
 for(int pattern=0;pattern<12;++pattern){
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){random=random*1664525+1013904223;
   const uint16_t colors[]{0,0xffff,0xf800,0x07e0,0x001f,0x84f1};
   const int c=pattern<4?((x+pattern*y)/std::max(1,pattern)%6):(random>>24)%6;pixels[y*w+x]=argb(colors[c]);}
  for(int y=-1;y<h+2;++y)for(int x=-1;x<w+2;++x)padded[(y+1)*stride+x+1]=rgb565(pixels[std::clamp(y,0,h-1)*w+std::clamp(x,0,w-1)]);
  for(auto filter:{SpriteFilter::sai,SpriteFilter::super_sai}){
   auto* src=reinterpret_cast<uint8_t*>(padded.data()+stride+1);auto* dst=reinterpret_cast<uint8_t*>(expected.data());
   if(filter==SpriteFilter::sai)_2xSaI(src,stride*2,dst,w*4,w,h);else Super2xSaI(src,stride*2,dst,w*4,w,h);
   const auto actual=scaler.compose(bg,pixels,w,h,filter);
   for(size_t i=0;i<actual.size();++i)require(rgb565(actual[i])==expected[i],"SaI differs from unmodified upstream RGB565 reference");
  }
 }
 // Fixed fingerprints from the independent upstream OpenGL reference tool.
 // First four use unchanged GLSL; last two use only the documented alpha metric.
 const uint64_t hashes[]{0xf9a9b8673bad49a4ull,0x02e5a4970c3d8665ull,0xbb646f9e5f2a067dull,
                         0xdc17664f111389d4ull,0x1d23af9014217d7cull,0xfcea67d11031e514ull};
 std::vector<uint32_t> fx(32*24),fxbg(32*24,0xff000000);
 for(int pattern=0;pattern<6;++pattern){
  uint32_t r=0x12345678;
  for(int y=0;y<24;++y)for(int x=0;x<32;++x){r=r*1664525+1013904223;
   const int k=pattern==0?x>y:pattern==1?((x/3+y/3)%2):pattern==2?(x==y || x+y==25):((r>>24)%5);
   constexpr uint32_t colors[]{0xff000000,0xffffffff,0xffa04080,0xff102080,0xff70cc44};
   fx[y*32+x]=pattern<4?colors[k]:pattern==4?(x>y?colors[(x/3)%5]:0):((x+y)%4?colors[k]:0);
  }
  const auto output=scaler.compose(fxbg,fx,32,24,SpriteFilter::scalefx);
  uint64_t hash=14695981039346656037ull;
  for(auto pixel:output)for(unsigned shift:{0u,8u,16u,24u}){hash^=(pixel>>shift)&255;hash*=1099511628211ull;}
  require(hash==hashes[pattern],"ScaleFX differs from retained independent GLSL reference");
 }
 // All filters must preserve input spans, including opaque black sprites.
 const auto before=pixels;scaler.compose(bg,pixels,w,h,SpriteFilter::scalefx);require(pixels==before,"filter mutated native input");
 // Finite CPU cost measurement; includes scaling + composition, excludes core.
 std::vector<uint32_t> large(320*224,0),ground(320*224,0xff404080);
 for(unsigned y=30;y<200;++y)for(unsigned x=40;x<280;++x)if((x+y)%7<4)large[y*320+x]=0xff60c080;
 for(int f=1;f<Frontend::sprite_filter_count;++f){
  const auto start=std::chrono::steady_clock::now();
  for(int i=0;i<20;++i)scaler.compose(ground,large,320,224,static_cast<SpriteFilter>(f));
  const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/20;
  std::cout<<Frontend::sprite_filter_labels[f]<<" synthetic CPU presentation: "<<ms<<" ms/frame\n";
 }
 std::cout<<"Sprite filters passed: transparent/solid borders, priority protection, ScaleNx corners, alpha, invalid planes, upstream SaI/ScaleFX comparisons and input isolation.\n";
 return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
