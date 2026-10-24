#include "hwvideo/wide_scene.hpp"
#include <iostream>
#include <memory>

static void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
template<class A,class B>static void same(const A& a,const B& b,const char* why){
    require(a.size()==b.size()&&std::equal(a.begin(),a.end(),b.begin()),why);
}
int main()try{
    require(roms.load_aburner2(),"ROMs required");
    auto road=std::make_unique<HWRoad>(0x1700,0x1720,0x1780,-166);
    auto tiles=std::make_unique<HWTiles>();auto sprites=std::make_unique<HWSprites>();HWPalette palette(8192);
    road->init(roms.road.rom,roms.road.length);tiles->init_gfx(roms.tiles.rom,roms.tiles.length);sprites->init(roms.sprites.rom,roms.sprites.length);
    unsigned compared=0;
    for(unsigned width:{320u,358u,398u,522u,640u})for(bool original_hud:{false,true}){
        Screen screen(320,224,palette,*road,*sprites,*tiles);Screen::ViewportLayers primary(width);
        auto live=std::make_unique<Presentation::WideScene>(roms,width),reference=std::make_unique<Presentation::WideScene>(roms,width);
        std::vector<RGB888> native(320*224);
        std::vector<Presentation::SceneClip> clips(2);
        for(unsigned frame=1;frame<=3;++frame){
            for(unsigned index=0;index<clips.size();++index){auto& c=clips[index];
                c.ns=frame*10+index;c.top=index?112:0;c.bottom=index?223:111;c.display=frame!=2;
                c.sprites.fill(0);c.sprites[0]=0x80;c.registers.fill(0);c.registers[4]=c.registers[5]=0x8000;
                for(unsigned i=0;i<c.tiles.size();++i)c.tiles[i]=uint16_t(i*137+frame);
                for(unsigned i=0;i<c.text.size();++i)c.text[i]=uint16_t(i*317);
                for(unsigned i=0;i<c.palette.size();++i)c.palette[i]=uint16_t(i*173+index*0x300+frame);
                for(unsigned i=0;i<0x200;++i){c.road[2*i]=8;c.road[2*i+1]=uint8_t(index+5);}
                for(unsigned i=0;i<c.tiles.size();++i)tiles->write_tile(i,c.tiles[i]);
                for(unsigned i=0;i<c.text.size();++i)tiles->write_text(i,c.text[i]);
                tiles->load_initial_frame_registers(c.registers);
                for(unsigned i=0;i<c.palette.size();++i)palette.write(i,c.palette[i]);
                for(unsigned i=0;i<0x800;++i)road->write16(i*2,uint16_t(c.road[2*i]<<8|c.road[2*i+1]));
                road->read_road_control();road->write_road_control(c.road_control);
                auto bank=c.sprites;screen.render_integrated_partial(native.data(),primary,c.top,c.bottom,c.display,bank.data());
                // Tile snapshots are intentionally poisoned only for the live
                // consumer: it must use primary layers, never rerasterize them.
                auto metadata=std::make_unique<Presentation::SceneClip>(c);metadata->tiles.fill(0xdead);
                live->render_primary_clip(*metadata,frame,primary,false,true,original_hud);
                require(!live->view().ready,"partial primary scene published early");
            }
            live->finish_primary_frame(frame);reference->render(clips,frame,false,true,original_hud);
            const auto a=live->view(),b=reference->view();require(a.ready&&b.ready,"completed primary scene missing rows");
            same(a.background,b.background,"primary clip background differs");same(a.sprites,b.sprites,"primary clip sprites differ");
            same(a.protection,b.protection,"primary clip priority/protection differs");same(a.ground_indices,b.ground_indices,"primary clip ground indices differ");
            same(a.heads,b.heads,"primary clip heads differ");same(a.hud.occlusion,b.hud.occlusion,"primary clip HUD occlusion differs");
            require(a.hud.clips.size()==b.hud.clips.size(),"primary clip HUD frontiers differ");
            for(size_t i=0;i<a.hud.clips.size();++i)require(a.hud.clips[i].entries==b.hud.clips[i].entries&&a.hud.clips[i].colors==b.hud.clips[i].colors,"primary clip HUD palette differs");
            ++compared;
        }
    }
    std::cout<<"Integrated primary clip: "<<compared<<" of "<<compared<<" synthetic-state/external-ROM-graphics comparisons identical, 0 skipped; poisoned redundant tile metadata, native/ultrawide geometry, partial palettes, disabled display and HUD ownership\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
