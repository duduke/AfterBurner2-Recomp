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
    Presentation::GroundHorizon::Profile flat;std::fill(flat.upper.begin(),flat.upper.end(),100);std::fill(flat.lower.begin(),flat.lower.end(),100);
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
    // The sky marker can remain visible when the ground marker is below the
    // screen. A new side band still has a ROM-derived normal and a real edge.
    Presentation::GroundHorizon::Profile bottom(398);bottom.measured=true;
    for(unsigned x=0;x<398;++x)bottom.pixel(x,215+x/100,1);
    std::vector<uint16_t> wide(398*224,0x1704);std::vector<uint8_t> wide_priority(wide.size());
    std::fill(road.begin(),road.end(),0);
    Presentation::GroundHorizon::correct(wide,wide_priority,road,0,223,0,128,bottom,-39);
    require(wide[20*398+380]==0x1720,"ground bottom-clipped outer sky column survived");
    require(wide[20*398+200]==0x1720,"ground clipped wide center retained a sky wedge");
    require(wide[216*398+200]==0x1720&&wide[216*398+380]==0x1720,"ground local clipped fallback crossed its observed plane");
    Presentation::GroundHorizon::Profile native_clipped;native_clipped.measured=true;
    for(unsigned x=0;x<320;++x)native_clipped.pixel(x,215,1);
    reset();Presentation::GroundHorizon::correct(pixels,priority,road,0,223,0,128,native_clipped);
    require(pixels[20*320+200]==0x1700,"ground wide clipped fallback leaked into original viewport");
    Presentation::GroundHorizon::Profile ground(398);ground.measured=true;
    for(unsigned x=0;x<398;++x){ground.pixel(x,90,1);ground.pixel(x,110,14);}
    wide.assign(wide.size(),0x1720);for(unsigned y=111;y<224;++y)for(unsigned x=39;x<359;++x)wide[y*398+x]=0x1703;
    wide_priority[180*398]=4;wide[180*398+1]=0x1c00;
    wide[180*398+39]=wide[180*398+40]=0x1720;
    Presentation::GroundHorizon::correct(wide,wide_priority,road,0,223,0,128,ground,-39);
    require(wide[180*398+20]==0x1703&&wide[180*398+380]==0x1703,"outer ground fill missing native-row color");
    require(wide[180*398+39]==0x1703&&wide[180*398+40]==0x1703,"ground native-edge seam remains in wide plane");
    require(wide[80*398+20]==0x1720&&wide[100*398+20]==0x1720,"outer ground fill crossed the strip");
    require(wide[180*398]==0x1720&&wide[180*398+1]==0x1c00,"outer ground fill overwrote protection or tiles");
    wide.assign(wide.size(),0x1720);std::fill(wide_priority.begin(),wide_priority.end(),0);
    Presentation::GroundHorizon::correct(wide,wide_priority,road,0,223,0,128,ground,-39);
    require(wide[180*398+20]==0x1720,"outer ground fill invented a missing native color");
    // At a banked viewport corner neither its row nor its column intersects
    // the visible strip. The observed envelope still defines the same plane.
    for(int sign:{-1,1}){
        Presentation::GroundHorizon::Profile corner(398);corner.measured=true;
        for(unsigned x=0;x<350;++x){const int sky=100-int(x)/3,earth=sky+16;
            if(sky>=0&&sky<224)corner.pixel(x,sign>0?sky:223-sky,1);
            if(earth>=0&&earth<224)corner.pixel(x,sign>0?earth:223-earth,14);}
        wide.assign(wide.size(),0x1720);
        const unsigned gy=sign>0?180:40,sy=sign>0?0:223;
        for(unsigned x=39;x<359;++x)wide[gy*398+x]=0x1703;
        wide[sy*398]=0x1704;
        Presentation::GroundHorizon::correct(wide,wide_priority,road,0,223,1,3*sign,corner,-39);
        require(wide[gy*398+390]==0x1703,"banked outer ground corner lost its plane");
        require(wide[sy*398]==0x1720,"inverted outer sky corner retained road fill");
        require(wide[gy*398+200]==0x1703,"banked plane changed native ground color");
    }
    reset();pixels[180*320]=0x1720;Presentation::GroundHorizon::correct(pixels,priority,road,0,223,0,128,flat);
    require(pixels[180*320]==0x1720,"wide ground extension changed original viewport policy");
    std::cout<<"Ground correction: opposite/inverted planes, priority, native pattern, solid fills, palette and partial clips passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
