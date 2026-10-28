#include "hwvideo/screen.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}

int main()try{
    constexpr unsigned native_width=320,height=224;
    HWRoad road(0x1700,0x1720,0x1780,-166);HWTiles tiles;HWSprites sprites;HWPalette palette(8192);
    // Synthetic graphics and state only: no ROM or capture data in this fixture.
    std::vector<uint8_t> roadrom(0x10000),tilerom(512*24),spriterom(0x40000);
    for(size_t i=0;i<roadrom.size();++i)roadrom[i]=uint8_t(i*37+(i>>8));
    for(size_t i=0;i<tilerom.size();++i)tilerom[i]=uint8_t(i*71+(i>>3));
    for(size_t i=0;i<spriterom.size();i+=4){
        const uint32_t word=(i/4)%4==3?0x123456f8u:0x12a45678u;
        for(unsigned b=0;b<4;++b)spriterom[i+b]=uint8_t(word>>(b*8));
    }
    road.init(roadrom.data(),roadrom.size());tiles.init_gfx(tilerom.data(),tilerom.size());sprites.init(spriterom.data(),spriterom.size());
    for(unsigned i=0;i<8192;++i)palette.write(i,uint16_t(i*173));
    for(unsigned i=0;i<0x8000;++i)tiles.write_tile(i,uint16_t(i*137));
    for(unsigned i=0;i<0x800;++i)tiles.write_text(i,uint16_t(i*317));
    for(unsigned i=0;i<0x800;++i)road.write16(i*2,uint16_t(i*113));
    road.read_road_control();
    std::array<uint8_t,0x1000> bank{};
    auto put=[&](unsigned slot,unsigned word,uint16_t value){bank[slot*16+word*2]=uint8_t(value>>8);bank[slot*16+word*2+1]=uint8_t(value);};
    for(unsigned slot=0;slot<5;++slot){
        put(slot,0,uint16_t(256+(slot?43:0)));put(slot,1,uint16_t(slot*4));
        put(slot,2,uint16_t(slot==1?508:190+slot*12));put(slot,3,uint16_t((slot%4)*0x1000+512));
        put(slot,4,0xe200);put(slot,5,180);put(slot,6,uint16_t(slot*17));put(slot,7,0xbeef);
    }
    put(2,3,0x6200); // Shadow-capable sprite, with a distinct priority.
    put(4,0,0x5100);put(5,0,0x8000);
    unsigned cases=0,feedback_witnesses=0,edge_witnesses=0;
    for(unsigned logical_width:{320u,358u,398u,522u,640u})for(unsigned control=0;control<8;++control)for(bool scrolling:{false,true}){
        road.write_road_control(control);
        for(unsigned i=0;i<4;++i){
            tiles.write_text(0x740+i,uint16_t(0x3210+i*0x1111));
            tiles.write_text(0x748+i,uint16_t((scrolling?0x8000:0)+i*73));
            tiles.write_text(0x74c+i,uint16_t((scrolling?0x8000:0)+i*91));
        }
        tiles.latch_frame_registers();
        Screen reference(320,224,palette,road,sprites,tiles),integrated(320,224,palette,road,sprites,tiles);
        Screen::ViewportLayers viewport(logical_width);
        require(viewport.native_sprite_words().empty(),"unrendered primary viewport exposed a native sprite raster");
        std::vector<RGB888> expected(320*224,{11,22,33}),actual=expected;
        std::vector<uint16_t> layers(logical_width*224,0);
        std::vector<uint8_t> priority(layers.size(),0);
        auto expected_bank=bank,actual_bank=bank;
        auto reference_underlay=[&](int top,int bottom,bool display){
            std::fill(priority.begin()+top*logical_width,priority.begin()+(bottom+1)*logical_width,0);
            if(display){
                road.render_background(layers.data(),logical_width,height,top,bottom);
                road.render_foreground(layers.data(),logical_width,height,top,bottom,-int(viewport.origin()));
                // Independent retained two-category traversal.
                for(int which:{1,0})for(int category:{0,1})
                    tiles.draw_layer(layers.data(),priority.data(),logical_width,height,which,category,uint8_t((which?1:2)<<category),top,bottom,-int(viewport.origin()));
            }else std::fill(layers.begin()+top*logical_width,layers.begin()+(bottom+1)*logical_width,0);
        };
        // Populate all rows first: disabled clips must erase real prior-frame
        // content, and partial clips must preserve other populated rows.
        reference.render_partial(expected.data(),0,223,true,expected_bank.data());
        integrated.render_integrated_partial(actual.data(),viewport,0,223,true,actual_bank.data());
        reference_underlay(0,223,true);
        unsigned native_observations=0;
        // The reference observer stores pre-sprite planes for the comparison.
        std::vector<uint16_t> native_layers(320*224);
        std::vector<uint8_t> native_priorities(320*224);
        reference.layers_rendered=[&](auto top,auto bottom,auto indices,auto priorities,const HWPalette&){
            std::copy(indices.begin()+top*320,indices.begin()+(bottom+1)*320,native_layers.begin()+top*320);
            std::copy(priorities.begin()+top*320,priorities.begin()+(bottom+1)*320,native_priorities.begin()+top*320);
        };
        integrated.layers_rendered=[&](auto top,auto bottom,auto indices,auto priorities,const HWPalette&){
            ++native_observations;
            for(unsigned y=top;y<=bottom;++y)for(unsigned x=0;x<320;++x){const auto i=y*320+x;
                require(indices[i]==native_layers[i]&&priorities[i]==native_priorities[i],"native layer observer changed");}
        };
        const std::array<std::pair<int,int>,5> clips{{{0,0},{1,7},{8,117},{118,222},{223,223}}};
        for(auto [top,bottom]:clips){
            const bool display=top!=118;
            const auto before=actual_bank;
            reference.render_partial(expected.data(),top,bottom,display,expected_bank.data());
            integrated.render_integrated_partial(actual.data(),viewport,top,bottom,display,actual_bank.data());
            require(std::memcmp(expected.data(),actual.data(),actual.size()*sizeof(RGB888))==0,"integrated native RGB differs");
            require(expected_bank==actual_bank,"native sprite feedback differs");
            require(std::equal(reference.sprite_words().begin(),reference.sprite_words().end(),integrated.sprite_words().begin()),"native sprite raster differs");
            require(viewport.native_sprite_words().size()==reference.sprite_words().size() &&
                std::equal(reference.sprite_words().begin(),reference.sprite_words().end(),viewport.native_sprite_words().begin()),"shared native sprite raster differs or changed other clip rows");
            if(display)feedback_witnesses+=actual_bank!=before;
            else require(actual_bank==before,"disabled display changed sprite feedback");
            reference_underlay(top,bottom,display);
            require(std::equal(layers.begin(),layers.end(),viewport.indices().begin()),"wide underlay differs or clip wrote outside rows");
            require(std::equal(priority.begin(),priority.end(),viewport.priorities().begin()),"wide priority differs or clip wrote outside rows");
        }
        require(native_observations==4,"native observer count changed");
        if(viewport.origin())for(unsigned y=0;y<118;++y)edge_witnesses+=viewport.indices()[y*logical_width]!=viewport.indices()[y*logical_width+viewport.origin()];
        ++cases;
    }
    require(feedback_witnesses>0,"fixture never exercised sprite feedback");
    require(edge_witnesses>0,"fixture never exercised distinct extended pixels");
    for(unsigned invalid:{0u,319u,321u,523u,642u,0xffffffffu}){
        bool rejected=false;try{Screen::ViewportLayers bad(invalid);}catch(const std::invalid_argument&){rejected=true;}
        require(rejected,"invalid viewport width accepted");
    }
    std::cout<<"Integrated renderer: "<<cases<<" of "<<cases<<" synthetic cases identical, 0 skipped; native RGB/layers/sprite raster/feedback, wide underlay/priority, partial and disabled clips, widths 320/358/398/522/640; feedback witnesses="<<feedback_witnesses<<", edge witnesses="<<edge_witnesses<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
