#include "hwvideo/ground_horizon.hpp"
#include <iostream>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main()try {
    constexpr unsigned n=320*224;std::vector<uint16_t> pixels(n,0x1700);std::vector<uint8_t> priority(n),road(0x1000);
    // Copyright-free ROM-table fixture: bind sky/ground tests to the actual
    // signed table convention, not a freely chosen normal passed by the test.
    std::vector<uint8_t> main(0x33f98),sub(0x1000);
    sub[9]=1;main[0x33e58]=128;main[0x33e98+64]=127;
    main[0x33e58+128]=127;main[0x33e98+192]=128;
    Presentation::GroundHorizon horizon(main,sub);
    require(horizon.normal(0)==std::array<int,2>{0,128},"ground level ROM orientation differs");
    require(horizon.normal(64)==std::array<int,2>{127,0},"ground right ROM orientation differs");
    require(horizon.normal(128)==std::array<int,2>{0,-127},"ground inverted ROM orientation differs");
    require(horizon.normal(192)==std::array<int,2>{-128,0},"ground left ROM orientation differs");
    Presentation::GroundHorizon::Profile flat;flat.upper.fill(100);flat.lower.fill(100);
    Presentation::GroundHorizon::correct(pixels,priority,road,0,223,horizon.normal(0)[0],horizon.normal(0)[1],flat);
    require(pixels[99*320]==0x1720 && pixels[100*320]==0x1700 && pixels[223*320]==0x1700,"ground level sky/sea classification differs");
    const auto reset=[&]{std::fill(pixels.begin(),pixels.end(),0x1700);std::fill(priority.begin(),priority.end(),0);std::fill(road.begin(),road.end(),0);};
    reset();Presentation::GroundHorizon::Profile diagonal;for(unsigned x=0;x<320;++x)diagonal.upper[x]=x;
    Presentation::GroundHorizon::correct(pixels,priority,road,0,223,-1,1,diagonal);
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x)require(pixels[y*320+x]==(y<x?0x1720:0x1700),"ground sky-plane classification differs");
    reset();Presentation::GroundHorizon::Profile opposite;for(unsigned x=0;x<320;++x)opposite.upper[x]=320-x;
    Presentation::GroundHorizon::correct(pixels,priority,road,0,223,1,1,opposite);
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x)require(pixels[y*320+x]==(x+y<320?0x1720:0x1700),"opposite roll classification differs");
    reset();Presentation::GroundHorizon::correct(pixels,priority,road,0,223,0,-1,flat);
    require(pixels[99*320]==0x1700 && pixels[101*320]==0x1720,"inverted ground classification differs");
    reset();priority[0]=4;pixels[1]=0x1708;road[4]=8;road[5]=0;road[0xc06]=3;
    Presentation::GroundHorizon::correct(pixels,priority,road,0,3,0,1,flat);
    require(pixels[0]==0x1700 && pixels[1]==0x1708,"ground priority protection differs");
    require(pixels[640]==0x1700 && pixels[3*320]==0x1723 && pixels[4*320]==0x1700,"ground clip/solid/palette protection differs");
    reset();Presentation::GroundHorizon::Profile right_roll;right_roll.left.fill(120);
    Presentation::GroundHorizon::correct(pixels,priority,road,0,223,128,0,right_roll);
    require(pixels[100*320+119]==0x1720 && pixels[100*320+120]==0x1700 && pixels[100*320+319]==0x1700,"ground right-roll sea preservation differs");
    reset();Presentation::GroundHorizon::Profile left_roll;left_roll.right.fill(200);
    Presentation::GroundHorizon::correct(pixels,priority,road,0,223,-128,0,left_roll);
    require(pixels[100*320+201]==0x1720 && pixels[100*320+200]==0x1700 && pixels[100*320]==0x1700,"ground left-roll sea preservation differs");
    reset();Presentation::GroundHorizon::Profile stairs;for(unsigned x=0;x<320;++x)stairs.upper[x]=70+(x/20)*3;
    Presentation::GroundHorizon::correct(pixels,priority,road,0,223,-1,1,stairs);
    for(unsigned x=0;x<320;++x){const unsigned edge=70+(x/20)*3;require(pixels[(edge-1)*320+x]==0x1720 && pixels[edge*320+x]==0x1700 && pixels[223*320+x]==0x1700,"ground silhouette edge or sea preservation differs");}
    // A clipped inverted strip must face upward even though its global
    // endpoint centroids are skewed sideways by the viewport crop.
    Presentation::GroundHorizon::Profile clipped;clipped.measured=true;
    for(unsigned x=0;x<320;++x){const unsigned sky=100+x/2,sea=50+x/2;if(sky<224)clipped.pixel(x,sky,1);if(sea<224)clipped.pixel(x,sea,14);}
    require(clipped.sky(0,180,0,128) && !clipped.sky(0,0,0,128),"ground clipped inverted sky/sea differs");
    Presentation::GroundHorizon::Profile upright;upright.measured=true;
    for(unsigned x=0;x<320;++x){const unsigned sky=x/2,sea=sky+50;if(sky<224)upright.pixel(x,sky,1);if(sea<224)upright.pixel(x,sea,14);}
    require(upright.sky(0,0,0,-128)==false && !upright.sky(0,223,0,-128) && upright.sky(100,0,0,-128),"ground clipped upright sky/sea differs");
    std::cout<<"Ground correction: opposite/inverted planes, priority, native pattern, solid fills, palette and partial clips passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
