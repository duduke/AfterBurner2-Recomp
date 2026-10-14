#include "engine/game_session.hpp"
#include "hwvideo/wide_scene.hpp"
#include "frontend/sprite_filter.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
void require(bool c,const char* m){if(!c)throw std::runtime_error(m);}
int main()try{
    require(roms.load_aburner2(),"ROMs required");GameSession original(roms),captured(roms);
    captured.capture_wide_scene(true);Presentation::WideScene wide(roms),native(roms,320),corrected(roms);Frontend::SpriteScaler scaler,native_scaler;
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
    uint64_t clips=0,side_changes=0,side_sprites=0,ground_changes=0;unsigned left=0,right=0,columns=0;
    for(unsigned f=0;f<1560;++f){GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;
        if(f>=600){c.stick_x=((f-600)/60)%2?1:-1;c.throttle=1;c.cannon=true;c.missile=f%90<12;}
        original.advance(c);captured.advance(c);
        auto a=original.frame(),b=captured.frame();require(a.info.number==b.info.number&&a.info.time_ns==b.info.time_ns,"wide tap changed time");
        require(std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),[](auto p,auto q){return p.r==q.r&&p.g==q.g&&p.b==q.b;}),"wide tap changed native RGB");require(original.take_audio()==captured.take_audio(),"wide tap changed PCM");
        auto scene=captured.wide_scene();require(scene.ready,"wide scene frontier missing");clips+=scene.clips.size();
        for(auto& clip:scene.clips)columns+=(clip.registers[4]&0x8000)||(clip.registers[5]&0x8000);
        wide.render(scene.clips,scene.info.number,false);native.render(scene.clips,scene.info.number,false);corrected.render(scene.clips,scene.info.number,true);auto w=wide.view(),n=native.view(),q=corrected.view();
        require(std::equal(w.sprites.begin(),w.sprites.end(),q.sprites.begin())&&std::equal(w.protection.begin(),w.protection.end(),q.protection.begin()),"wide ground correction changed aircraft/layer protection");
        for(unsigned i=0;i<w.ground_indices.size();++i)if(w.ground_indices[i]!=q.ground_indices[i]){++ground_changes;require(w.ground_indices[i]>=0x1700&&w.ground_indices[i]<0x1708&&q.ground_indices[i]>=0x1720&&q.ground_indices[i]<0x1730,"wide ground correction overwrote terrain/tile indices");}require(w.ready&&n.ready,"wide scene row coverage missing");
        auto output=scaler.compose(w.background,w.sprites,w.width,224,Frontend::SpriteFilter::none,w.protection);
        auto normal=native_scaler.compose(n.background,n.sprites,320,224,Frontend::SpriteFilter::none,n.protection);
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x){unsigned i=y*320+x;auto p=a.pixels[i];auto expected=0xff000000u|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b;
            if(normal[i]!=expected || output[y*w.width+x+w.origin]!=expected){std::cerr<<" normal-bg="<<std::hex<<n.background[i]<<" wide-bg="<<w.background[y*w.width+x+w.origin]<<" normal-sprite="<<n.sprites[i]<<" wide-sprite="<<w.sprites[y*w.width+x+w.origin]<<std::dec<<" ";std::cerr<<"frame="<<f<<" x="<<x<<" y="<<y<<" native="<<std::hex<<expected<<" reconstructed="<<normal[i]<<" wide="<<output[y*w.width+x+w.origin]<<std::dec<<'\n';throw std::runtime_error("wide center differs from native raster");}}
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<w.origin;++x){side_sprites+=w.sprites[y*w.width+x]!=0;side_sprites+=w.sprites[y*w.width+w.width-1-x]!=0;side_changes+=output[y*w.width+x]!=output[y*w.width+w.origin];side_changes+=output[y*w.width+w.width-1-x]!=output[y*w.width+w.width-1-w.origin];}
        if(f>=1080){if(c.stick_x<0)++left;if(c.stick_x>0)++right;}
        if(f==1275){std::filesystem::create_directories("build/oracle/widescreen");std::ofstream file("build/oracle/widescreen/scene.bin",std::ios::binary);file.write("AB2WIDE1",8);uint32_t dims[]{w.width,224,uint32_t(w.fragments.size())};file.write(reinterpret_cast<const char*>(dims),sizeof(dims));
            for(auto data:{w.background,w.sprites,w.heads})file.write(reinterpret_cast<const char*>(data.data()),data.size_bytes());
            file.write(reinterpret_cast<const char*>(w.protection.data()),w.protection.size_bytes());file.write(reinterpret_cast<const char*>(w.fragments.data()),w.fragments.size_bytes());}
        if(f==1275||f==1287){Frontend::SpriteScaler corrected_scaler;auto corrected_output=corrected_scaler.compose(q.background,q.sprites,q.width,224,Frontend::SpriteFilter::none,q.protection);std::ofstream img("build/oracle/widescreen/corrected-"+std::to_string(f)+".ppm",std::ios::binary);img<<"P6\n"<<q.width<<" 224\n255\n";for(auto p:corrected_output)for(unsigned shift:{16u,8u,0u})img.put(char(p>>shift));}
        if(f==1275||f==1287){std::filesystem::create_directories("build/oracle/widescreen");std::ofstream img("build/oracle/widescreen/scene-"+std::to_string(f)+".ppm",std::ios::binary);img<<"P6\n"<<w.width<<" 224\n255\n";for(auto p:output)for(unsigned shift:{16u,8u,0u})img.put(char(p>>shift));}
    }
    require(side_changes&&side_sprites&&ground_changes&&left&&right,"wide side/roll witnesses missing");
    captured.set_paused(true);const auto before=captured.info();require(!captured.advance({})&&captured.info().number==before.number,"paused wide capture advanced");
    captured.capture_sprite_scene(true);captured.capture_ground_horizon(true);captured.set_paused(false);for(unsigned i=0;i<8;++i){original.advance({});captured.advance({});require(original.take_audio()==captured.take_audio(),"combined observers changed PCM");require(captured.wide_scene().ready&&captured.sprite_scene().ready,"wide capture replaced effect/ground observers");}
    captured.restart();wide.reset();for(unsigned i=0;i<8;++i){captured.advance({});captured.take_audio();wide.render(captured.wide_scene().clips,captured.info().number,false);}require(wide.view().ready,"wide restart missing rows");
    captured.capture_sprite_presentation(false);captured.advance({});captured.take_audio();require(captured.wide_scene().ready,"disabling sprite capture removed wide observer");captured.capture_wide_scene(false);require(!captured.wide_scene().ready,"wide disable retained snapshots");
    std::cout<<"Wide scene: 1560 native RGB/PCM/time frames and 320-center reconstructions identical; "<<clips<<" partial clips, "<<side_changes<<" non-repeated side pixels, "<<side_sprites<<" side sprite pixels; "<<ground_changes<<" sky-fill corrections with terrain preserved; independent column wrap passed; both rolls="<<left<<'/'<<right<<", column-scroll clips="<<columns<<"; shared observers, pause/reset/disable passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
