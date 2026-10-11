#include "hwvideo/screen.hpp"
#include <iostream>
#include <stdexcept>

int main() try {
    constexpr int width=320, height=224;
    HWRoad road(0x1700,0x1720,0x1780,-166);
    HWTiles tiles;
    std::vector<uint8_t> roadrom(0x10000), tilerom(64*24);
    for (size_t i=0;i<roadrom.size();++i) roadrom[i]=uint8_t(i*37+(i>>8));
    for (size_t i=0;i<tilerom.size();++i) tilerom[i]=uint8_t(i*71+(i>>3));
    road.init(roadrom.data(),roadrom.size()); tiles.init_gfx(tilerom.data(),tilerom.size());
    for(unsigned i=0;i<0x8000;++i) tiles.write_tile(i,uint16_t(i*137));
    for(unsigned i=0;i<0x800;++i) tiles.write_text(i,uint16_t(i*317));
    for(unsigned i=0;i<4;++i) {
        tiles.write_text(0x740+i,uint16_t(0x3210+i*0x1111));
        tiles.write_text(0x748+i,uint16_t(0x8000+i*73));
        tiles.write_text(0x74c+i,uint16_t(0x8000+i*91));
    }
    tiles.latch_frame_registers();
    // Combined category traversal must equal the original two-pass ordering.
    for(int layer=0;layer<3;++layer) {
        std::vector<uint16_t> separate(width*height,0x5555), combined=separate;
        std::vector<uint8_t> seppri(width*height,0x20), combpri=seppri;
        auto draw=[&](auto& p,auto& pri,int category,unsigned mark) {
            if(layer<2) tiles.draw_layer(p.data(),pri.data(),width,height,layer,category,mark,7,117);
            else tiles.draw_text(p.data(),pri.data(),width,height,category,mark,7,117);
        };
        draw(separate,seppri,0,2); draw(separate,seppri,1,4);
        draw(combined,combpri,-1,2);
        if(separate!=combined || seppri!=combpri) throw std::runtime_error("combined categories differ from separate passes");
    }
    for(unsigned i=0;i<0x800;++i) road.write16(i*2,uint16_t(i*113));
    road.read_road_control();
    unsigned cases=0;
    const std::pair<int,int> clips[]={{0,0},{1,7},{7,8},{43,117},{223,223},{0,223}};
    for(unsigned control=0;control<8;++control) {
        road.write_road_control(control);
        for(auto [top,bottom]:clips) {
            auto compare=[&](auto draw) {
                std::vector<uint16_t> full(width*height,0x5555), clipped=full;
                std::vector<uint8_t> fullpri(width*height,0x20), clippri=fullpri;
                draw(full,fullpri,0,height-1); draw(clipped,clippri,top,bottom);
                for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
                    const auto i=y*width+x; const bool inside=y>=top&&y<=bottom;
                    if(clipped[i]!=(inside?full[i]:0x5555) || clippri[i]!=(inside?fullpri[i]:0x20))
                        throw std::runtime_error("clip differs from full raster or writes outside its rows");
                }
                ++cases;
            };
            compare([&](auto& p,auto&,int t,int b){road.render_background(p.data(),width,height,t,b);});
            compare([&](auto& p,auto&,int t,int b){road.render_foreground(p.data(),width,height,t,b);});
            for(int layer=0;layer<2;++layer) for(int category=0;category<2;++category)
                compare([&](auto& p,auto& pri,int t,int b){tiles.draw_layer(p.data(),pri.data(),width,height,layer,category,4,t,b);});
            for(int category=0;category<2;++category)
                compare([&](auto& p,auto& pri,int t,int b){tiles.draw_text(p.data(),pri.data(),width,height,category,8,t,b);});
        }
    }
    std::cout << "Video clip regression passed: " << cases << " full-raster comparisons; single rows, tile boundaries, scroll tables, road controls and untouched outside rows\n";
    return 0;
} catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
