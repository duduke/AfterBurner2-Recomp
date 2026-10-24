// Reuse the unmodified independent MAME RGB comparator, substituting only the
// primary renderer's entry point. Captures and comparison tolerance stay intact.
#include "hwvideo/screen.hpp"
#include <charconv>
#include <iostream>
#include <string_view>
namespace {
unsigned integrated_oracle_width=398;
class IntegratedOracleScreen:public Screen {
    ViewportLayers viewport;
    int rows;
public:
    IntegratedOracleScreen(int w,int h,HWPalette& p,HWRoad& r,HWSprites& s,HWTiles& t)
        :Screen(w,h,p,r,s,t),viewport(integrated_oracle_width),rows(h){}
    void render_frame(RGB888* output){render_integrated_partial(output,viewport,0,rows-1,true);}
};
}
#define Screen IntegratedOracleScreen
#define main independent_video_oracle_main
#include "oracle_video.cpp"
#undef main
#undef Screen

int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("Usage: oracle_integrated_video width MAME-capture.bin");
    const std::string_view value(argv[1]);
    const auto r=std::from_chars(value.data(),value.data()+value.size(),integrated_oracle_width);
    if(r.ec!=std::errc{}||r.ptr!=value.data()+value.size())throw std::runtime_error("Invalid oracle width");
    std::cout<<"Independent MAME primary-renderer width="<<integrated_oracle_width<<", exact RGB tolerance=0\n";
    return independent_video_oracle_main(argc-1,argv+1);
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
