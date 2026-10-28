#include "hwvideo/ground_horizon.hpp"
#include "ground_correction_reference.hpp"
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
    Presentation::GroundHorizon::Profile empty;
    for(unsigned y=0;y<224;++y)require(empty.left[y]==-1&&empty.right[y]==-1,"ground empty-row sentinel differs");
    for(unsigned x=0;x<320;++x)require(empty.upper[x]==-1&&empty.lower[x]==-1,"ground empty-column sentinel differs");
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
    // A local measured direction can disagree with the corner projection
    // near an axis crossing. Neither extrapolated side may override it.
    Presentation::GroundHorizon::Profile folded(398);folded.measured=true;
    for(unsigned y:{0u,1u}){folded.pixel(200,y,1);folded.pixel(210,y,14);}
    folded.pixel(80,100,14);folded.pixel(100,100,1);
    wide.assign(wide.size(),0x1720);wide[100*398+50]=0x1703;
    Presentation::GroundHorizon::correct(wide,wide_priority,road,0,223,127,0,folded,-39);
    require(wide[100*398+250]==0x1720,"ground corner projection inserted white into measured sky");
    require(wide[100*398+50]==0x1703,"ground corner projection cut blue into measured terrain");
    // A clipped ground row may borrow a flat field only from matching color
    // control and the same clip. Distinct pens need an identical palette word.
    std::vector<uint16_t> palette(8192);
    for(unsigned fault=0;fault<6;++fault){
        wide.assign(wide.size(),0x1720);std::fill(wide_priority.begin(),wide_priority.end(),0);
        std::fill(road.begin(),road.end(),0);palette[0x1702]=palette[0x1704]=0x7fff;
        wide[180*398+100]=0x1702;wide[180*398+101]=0x1704;
        if(fault==1)palette[0x1704]=0;
        if(fault==2)road[0xc00+150*2+1]=1;
        if(fault==4)wide_priority[180*398+100]=wide_priority[180*398+101]=4;
        if(fault==5)road[180*2]=8;
        wide_priority[150*398+20]=4;
        Presentation::GroundHorizon::correct(wide,wide_priority,road,fault==3?140:0,fault==3?160:223,0,128,ground,-39,palette);
        require(wide[150*398+200]==(fault?0x1720:0x1702),"ground flat-field donor color/control/clip protection differs");
        require(wide[150*398+20]==0x1720,"ground flat-field donor overwrote priority");
    }
    std::fill(road.begin(),road.end(),0);std::fill(wide_priority.begin(),wide_priority.end(),0);
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
    // Stage 6 fast-roll reproduction: the current roll crosses an axis
    // before the displayed/interpolated strip. In a clipped row only one
    // marker survives, so fallback must follow the observed raster quadrant.
    for(unsigned width:{358u,398u})for(bool flip_x:{false,true})for(bool flip_y:{false,true})for(bool transpose:{false,true}){
        Presentation::GroundHorizon::Profile delayed(width);delayed.measured=true;
        const auto tx=[&](unsigned x){return flip_x?width-1-x:x;};
        const auto ty=[&](unsigned y){return flip_y?223-y:y;};
        for(unsigned u=0;u<(transpose?224u:width);++u)for(unsigned pen=1;pen<=14;++pen){
            const unsigned v=100+u/3+(pen-1)*30/13;
            const unsigned x=transpose?v:u,y=transpose?u:v;
            if(x<width&&y<224)delayed.pixel(tx(x),ty(y),pen);
        }
        std::vector<uint16_t> delayed_pixels(width*224,0x1704);
        std::vector<uint8_t> delayed_priority(delayed_pixels.size());
        const auto index=[&](unsigned u,unsigned v){return ty(transpose?u:v)*width+tx(transpose?v:u);};
        const auto sky=index(100,110),terrain=index(100,190);
        const auto protected_sky=index(100,109);delayed_priority[protected_sky]=4;
        Presentation::GroundHorizon::correct(delayed_pixels,delayed_priority,road,0,223,
            (flip_x?-1:1)*(transpose?127:21),(flip_y?-1:1)*(transpose?21:127),delayed,-int((width-320)/2));
        require(delayed_pixels[sky]==0x1720,"ground delayed roll retained a sky triangle");
        require(delayed_pixels[terrain]==0x1704,"ground delayed roll erased observed terrain");
        require(delayed_pixels[protected_sky]==0x1704,"ground delayed roll overwrote priority");
    }
    // With neither axis locally ordered, global raster ordering still owns
    // the clipped silhouette fallback (retain both stale-quadrant controls).
    for(bool transpose:{false,true}){
        Presentation::GroundHorizon::Profile inferred(398);inferred.measured=true;
        const auto point=[&](unsigned x,unsigned y,unsigned pen){inferred.pixel(transpose?y:x,transpose?x:y,pen);};
        for(unsigned y:{0u,1u}){point(100,y,14);point(120,y,1);}point(80,100,1);
        wide.assign(wide.size(),0x1704);std::fill(wide_priority.begin(),wide_priority.end(),0);
        Presentation::GroundHorizon::correct(wide,wide_priority,road,0,223,transpose?0:21,transpose?21:0,inferred,-39);
        const unsigned x=transpose?100:200,y=transpose?200:100;
        require(wide[y*398+x]==0x1720,"ground inferred clipped quadrant retained a white triangle");
    }
    reset();pixels[180*320]=0x1720;Presentation::GroundHorizon::correct(pixels,priority,road,0,223,0,128,flat);
    require(pixels[180*320]==0x1720,"wide ground extension changed original viewport policy");
    unsigned differential_cases=0;
    for(unsigned width:{320u,358u,398u})for(unsigned pattern=0;pattern<16;++pattern){
        Presentation::GroundHorizon::Profile profile(width);profile.measured=true;
        for(unsigned x=0;x<width;++x){const unsigned edge=20+(x*(pattern%5+1)/3)%170;
            profile.pixel(x,edge,pattern&1?14:1);profile.pixel(x,edge+10,pattern&1?1:14);}
        std::vector<uint16_t> input(width*224),palette(8192);
        std::vector<uint8_t> marks(input.size()),geometry(input.size());
        uint32_t random=0x13572468+pattern;
        for(unsigned i=0;i<input.size();++i){random=random*1664525+1013904223;
            input[i]=(random&3)?uint16_t(0x1700+(random>>16)%8):uint16_t(0x1720+(random>>16)%16);marks[i]=(random>>24)%11==0?2:0;}
        for(unsigned i=0;i<palette.size();++i)palette[i]=uint16_t(pattern&2?i:i/8);
        for(unsigned y=0;y<224;++y){road[2*y]=y%19==0?0xa:0;road[2*y+1]=0;
            road[2*(0x600+y)]=uint8_t(y%7);road[2*(0x600+y)+1]=uint8_t(y%3);}
        const int nx=pattern&4?-57:57,ny=pattern&8?-113:113,left=-int((width-320)/2);
        const unsigned top=pattern&2?13:0,bottom=pattern&2?190:223;
        auto expected=input;const auto changed=TestReference::ground_correction(expected,marks,road,top,bottom,nx,ny,profile,left,palette);
        for(bool cached:{false,true}){auto actual=input;
            const auto count=Presentation::GroundHorizon::correct(actual,marks,road,top,bottom,nx,ny,profile,left,palette,cached?&geometry:nullptr);
            require(actual==expected&&count==changed,"ground optimized correction differs from retained full-plane reference");++differential_cases;}
    }
    std::cout<<"Ground correction: opposite/inverted planes, priority, native pattern, solid fills, palette and partial clips passed; "<<differential_cases<<" full-plane reference comparisons identical\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
