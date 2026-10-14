#include "hwvideo/wide_scene.hpp"
#include "frontend/sprite_filter.hpp"
#include <iostream>
#include <cstring>
void require(bool c,const char* m){if(!c)throw std::runtime_error(m);}
int main()try {
    require(roms.load_aburner2(),"ROMs required");Frontend::SpriteScaler scaler;
    // Independent side-column reference: decode the original planar tile ROM
    // directly and select deliberately distinct virtual tile/column entries.
    Presentation::SceneClip synthetic;synthetic.display=true;synthetic.bottom=223;synthetic.ns=1;
    synthetic.sprites[0]=0x80;synthetic.registers[4]=synthetic.registers[5]=0x8000;
    for(unsigned i=0;i<8192;++i)synthetic.palette[i]=uint16_t((i*13)&0x7fff);
    for(unsigned i=0;i<synthetic.tiles.size();++i)synthetic.tiles[i]=uint16_t((i*7+1)&0x1fff);
    for(unsigned i=0;i<0x200;++i){synthetic.road[2*i]=8;synthetic.road[2*i+1]=5;}
    for(unsigned layer=0;layer<2;++layer)for(unsigned col=0;col<32;++col)synthetic.text[0xf16/2+layer*32+col]=col*8;
    const auto before_bank=synthetic.sprites;Presentation::WideScene column_scene(roms);column_scene.render(std::span(&synthetic,1),1,false);
    const auto colview=column_scene.view();HWPalette column_palette(8192);for(unsigned i=0;i<8192;++i)column_palette.write(i,synthetic.palette[i]);
    auto colpixels=scaler.compose(colview.background,colview.sprites,398,224,Frontend::SpriteFilter::none,colview.protection);
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<398;++x)if(x<39||x>=359){int nx=int(x)-39;unsigned vx=(nx+192)&1023,col=((nx+8)&511)/16,vy=(y+col*8)&511;
        auto entry=synthetic.tiles[((vy&255)>>3)*64+((vx&511)>>3)];const unsigned code=(entry&8191)%(roms.tiles.length/24),off=code*8+(vy&7),third=roms.tiles.length/3;
        unsigned pen=0;for(unsigned plane=0;plane<3;++plane)pen|=((roms.tiles.rom[plane*third+off]>>(7-(vx&7)))&1)<<plane;
        unsigned index=pen?0x1c00+((entry>>6)&127)*8+pen:0x1785;auto p=column_palette.pen(index);
        require(colpixels[y*398+x]==(0xff000000u|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b),"wide side column/source wrap differs from independent planar-ROM reference");}
    require(synthetic.sprites==before_bank,"wide renderer wrote native sprite feedback");
    auto upper=synthetic,lower=synthetic;upper.bottom=111;lower.top=112;lower.ns=2;
    upper.palette.fill(0x001f);lower.palette.fill(0x7c00);
    Presentation::WideScene partial_scene(roms);std::array parts{upper,lower};partial_scene.render(parts,1,false);
    auto partial=partial_scene.view();HWPalette partial_palette(8192);partial_palette.write(0,0x001f);auto red=partial_palette.pen(0);partial_palette.write(0,0x7c00);auto blue=partial_palette.pen(0);
    require(partial.background[100*398]==(0xff000000u|uint32_t(red.r)<<16|uint32_t(red.g)<<8|red.b) &&
        partial.background[120*398]==(0xff000000u|uint32_t(blue.r)<<16|uint32_t(blue.g)<<8|blue.b),"wide renderer lost partial-frontier palette/row ownership");

    unsigned blank=UINT32_MAX,third=roms.tiles.length/3;
    for(unsigned tile=0;tile<512;++tile){bool empty=true;for(unsigned plane=0;plane<3;++plane)for(unsigned y=0;y<8;++y)empty&=roms.tiles.rom[plane*third+tile*8+y]==0;if(empty){blank=tile;break;}}
    require(blank!=UINT32_MAX,"blank tile fixture missing");auto road_clip=synthetic;road_clip.registers.fill(0);road_clip.tiles.fill(blank);
    std::fill(road_clip.text.begin(),road_clip.text.begin()+0x700,uint16_t(blank));road_clip.road.fill(0);road_clip.road_control=4;
    auto road_word=[&](unsigned i,uint16_t v){road_clip.road[2*i]=v>>8;road_clip.road[2*i+1]=v;};
    for(unsigned y=0;y<224;++y){road_word(y,y*2);road_word(0x100+y,0x800);road_word(0x200+y,0x5f8-166+(y*7)%512);road_word(0x600+y,uint16_t(y*131));}
    HWRoad original_road(0x1700,0x1720,0x1780,-166);original_road.init(roms.road.rom,roms.road.length);
    for(unsigned i=0;i<0x800;++i)original_road.write16(2*i,uint16_t(road_clip.road[2*i]<<8|road_clip.road[2*i+1]));original_road.read_road_control();original_road.write_road_control(4);
    std::vector<uint16_t> road_indices(320*224);original_road.render_background(road_indices.data(),320,224);original_road.render_foreground(road_indices.data(),320,224);
    Presentation::WideScene road_scene(roms);road_scene.render(std::span(&road_clip,1),1,false);auto rv=road_scene.view();
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x){auto p=column_palette.pen(road_indices[y*320+x]);require(rv.background[y*398+x+39]==(0xff000000u|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b),"wide road origin changed the native center");}
    // Runtime sprite data, with a terminal nibble and visible pens. Synthetic
    // descriptors exercise signed left and wrapped right-facing coordinates.
    unsigned sprite_offset=UINT32_MAX;
    for(unsigned i=0;i<roms.sprites.length;i+=4){uint32_t word=0;for(unsigned b=0;b<4;++b)word|=uint32_t(roms.sprites.rom[i+b])<<(b*8);
        bool colored=false;for(unsigned k=0;k<8;++k){auto pen=(word>>(k*4))&15;colored|=pen>0&&pen<15;}
        if(colored&&((word>>24)&15)==15){sprite_offset=i;break;}}
    require(sprite_offset!=UINT32_MAX,"sprite edge fixture source missing");
    for(bool right:{false,true}){
        auto edge=synthetic;edge.registers[4]=edge.registers[5]=0;edge.sprites.fill(0);edge.sprites[16]=0x80;
        uint16_t d[]{uint16_t(256+80+((sprite_offset/0x40000)<<9)),uint16_t((sprite_offset%0x40000)/4),uint16_t(right?8:178),0x3200,uint16_t(right?0x8200:0xa200),0,1,0};
        for(unsigned i=0;i<8;++i){edge.sprites[2*i]=d[i]>>8;edge.sprites[2*i+1]=d[i];}
        const auto bank=edge.sprites;Presentation::WideScene renderer(roms);renderer.render(std::span(&edge,1),1,false);auto view=renderer.view();
        unsigned visible=0;for(unsigned x=0;x<398;++x)if((right?x>=359:x<39)&&view.sprites[80*398+x])++visible;
        require(visible>0,"sprite disappeared at a native edge instead of a wide edge");require(edge.sprites==bank,"wide sprite feedback write");
    }
    std::cout<<"Wide geometry: independent planar-ROM side/column wrap, partial palettes, both signed sprite edges and immutable feedback passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
