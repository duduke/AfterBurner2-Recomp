// Retained MAME roll frames: independent visible sky/terrain witnesses, not
// counts grouped by requested stick direction. Captures remain under build/.
#include "hwvideo/ground_horizon.hpp"
#include "hwvideo/screen.hpp"
#include "roms.hpp"
#include <fstream>
#include <filesystem>
#include <iostream>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
uint16_t word(const std::vector<uint8_t>& a,unsigned n){return uint16_t(a[2*n])<<8|a[2*n+1];}
void image(const std::string& path,std::span<const RGB888> pixels){std::ofstream f(path,std::ios::binary);f<<"P6\n320 224\n255\n";for(auto p:pixels){f.put(p.r);f.put(p.g);f.put(p.b);}}
int main(int argc,char** argv)try {
    const bool save_images=argc==1;require(argc==1 || (argc==2 && std::string_view(argv[1])=="--no-images"),"invalid fixture arguments");
    std::filesystem::create_directories("build/oracle/horizon");
    require(roms.load_aburner2(),"ROMs required");
    HWPalette palette(8192);HWRoad road(0x1700,0x1720,0x1780,-166);HWSprites sprites;HWTiles tiles;
    road.init(roms.road.rom,roms.road.length);sprites.init(roms.sprites.rom,roms.sprites.length);tiles.init_gfx(roms.tiles.rom,roms.tiles.length);
    Screen screen(320,224,palette,road,sprites,tiles);
    Presentation::EffectAssets assets({roms.maincpu.rom,roms.maincpu.length},{roms.subcpu.rom,roms.subcpu.length});
    Presentation::GroundHorizon correction({roms.maincpu.rom,roms.maincpu.length},{roms.subcpu.rom,roms.subcpu.length});
    std::ifstream in("build/oracle/frames_rolls.bin",std::ios::binary);char magic[8]{};in.read(magic,8);require(std::string_view(magic,8)=="AB2VID01","retained roll capture format differs");
    std::vector<uint8_t> rgb(320*224*4),bank(0x1000),ram(0x1000),latched(32),pal(0x4000),tile(0x10000),text(0x1000),priority(320*224);
    std::vector<uint16_t> background(320*224),horizon(320*224);std::vector<RGB888> original(320*224),enhanced(320*224);
    screen.layers_rendered=[&](unsigned,unsigned,auto indices,auto marks,const auto&){std::copy(indices.begin(),indices.end(),background.begin());std::copy(marks.begin(),marks.end(),priority.begin());};
    unsigned frames=0,witness_frames=0,no_endpoints=0;std::array<unsigned,2> sides{},best{};uint64_t wedges=0,preserved=0;
    while(in.peek()!=EOF){unsigned char header[4],control[2];in.read(reinterpret_cast<char*>(header),4);
        const unsigned frame=uint32_t(header[0])<<24|uint32_t(header[1])<<16|uint32_t(header[2])<<8|header[3];
        for(auto* v:{&rgb,&bank,&ram})in.read(reinterpret_cast<char*>(v->data()),v->size());in.read(reinterpret_cast<char*>(control),2);
        for(auto* v:{&latched,&pal,&tile,&text})in.read(reinterpret_cast<char*>(v->data()),v->size());require(bool(in),"truncated roll capture");
        for(unsigned i=0;i<8192;++i)palette.write(i,word(pal,i));
        for(unsigned i=0;i<0x800;++i){road.write16(i*2,word(ram,i));sprites.write16(i*2,word(bank,i));tiles.write_text(i,word(text,i));}
        road.read_road_control();road.write_road_control(control[1]);sprites.swap();
        for(unsigned i=0;i<0x8000;++i)tiles.write_tile(i,word(tile,i));
        for(unsigned i=0;i<16;++i)tiles.write_text(0x740+i,word(latched,i));tiles.latch_frame_registers();screen.render_frame(original.data());
        for(unsigned i=0;i<original.size();++i)require(original[i].r==rgb[4*i+2]&&original[i].g==rgb[4*i+1]&&original[i].b==rgb[4*i],"original roll frame differs from MAME");
        auto only_horizon=bank;
        for(unsigned slot=0;slot<256;++slot){std::array<uint16_t,8>d{};for(unsigned w=0;w<8;++w)d[w]=word(bank,slot*8+w);if(d[0]&0x8000)break;if(!correction.matches(d))only_horizon[slot*16]|=0x50;}
        std::fill(horizon.begin(),horizon.end(),0xffff);
        Presentation::decode(only_horizon,{roms.sprites.rom,roms.sprites.length},0,223,assets,[&](unsigned x,unsigned y,uint16_t raw,auto){horizon[y*320+x]=raw;});
        struct End{double x=0,y=0,count=0;};End sky,ground;
        std::array<int,320> top,bottom;std::array<int,224> left,right;top.fill(224);bottom.fill(-1);left.fill(320);right.fill(-1);
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x){auto raw=horizon[y*320+x];if(raw==0xffff)continue;
            top[x]=std::min(top[x],int(y));bottom[x]=std::max(bottom[x],int(y));left[y]=std::min(left[y],int(x));right[y]=std::max(right[y],int(x));
            auto* end=(raw&15)==1?&sky:(raw&15)==14?&ground:nullptr;if(end){end->x+=x;end->y+=y;++end->count;}
        }
        if(!sky.count || !ground.count){++frames;++no_endpoints;continue;}
        ++witness_frames;
        std::array<int,320> sky_y,ground_y;std::array<int,224> sky_x,ground_x;
        sky_y.fill(-1);ground_y.fill(-1);sky_x.fill(-1);ground_x.fill(-1);
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x){const auto pen=horizon[y*320+x]&15;if(horizon[y*320+x]==0xffff)continue;
            if(pen==1){sky_y[x]=y;sky_x[y]=x;}if(pen==14){ground_y[x]=y;ground_x[y]=x;}
        }
        std::array<int,320> column_direction{};std::array<int,224> row_direction{};
        int dx=0,dy=0;
        for(unsigned x=0;x<320;++x){int smin=224,smax=-1,gmin=224,gmax=-1;
            for(unsigned y=0;y<224;++y){auto raw=horizon[y*320+x];if(raw==0xffff)continue;if((raw&15)==1){smin=std::min(smin,int(y));smax=std::max(smax,int(y));}if((raw&15)==14){gmin=std::min(gmin,int(y));gmax=std::max(gmax,int(y));}}
            if(smax>=0&&gmax>=0)column_direction[x]=smax<gmin?1:gmax<smin?-1:0;dy+=column_direction[x];
        }
        for(unsigned y=0;y<224;++y){int smin=320,smax=-1,gmin=320,gmax=-1;
            for(unsigned x=0;x<320;++x){auto raw=horizon[y*320+x];if(raw==0xffff)continue;if((raw&15)==1){smin=std::min(smin,int(x));smax=std::max(smax,int(x));}if((raw&15)==14){gmin=std::min(gmin,int(x));gmax=std::max(gmax,int(x));}}
            if(smax>=0&&gmax>=0)row_direction[y]=smax<gmin?1:gmax<smin?-1:0;dx+=row_direction[y];
        }
        const unsigned side=dx>0;auto corrected=background;
        correction.apply(corrected,priority,ram,control[1],bank,{roms.sprites.rom,roms.sprites.length},0,223,0,assets);
        auto sprite=screen.sprite_words();unsigned visible_wedge=0;
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x){const auto i=y*320+x;
            const bool known_y=bottom[x]>=0 && column_direction[x];
            const bool known_x=right[y]>=0 && row_direction[y];
            const bool sky_y_side=known_y && (column_direction[x]>0?int(y)<top[x]:int(y)>bottom[x]);
            const bool sky_x_side=known_x && (row_direction[y]>0?int(x)<left[y]:int(x)>right[y]);
            const bool sky_side=(known_y||known_x) && (!known_y||sky_y_side) && (!known_x||sky_x_side);
            const bool ground_side=(known_y && (column_direction[x]>0?int(y)>bottom[x]:int(y)<top[x])) || (known_x && (row_direction[y]>0?int(x)>right[y]:int(x)<left[y]));
            if(ground_side){require(corrected[i]==background[i],"ground correction erased sea below published horizon");++preserved;}
            if(priority[i])require(corrected[i]==background[i],"ground correction changed foreground");
            if(control[1]==4 && sky_side && !priority[i] && background[i]>=0x1700 && background[i]<0x1708 && !(word(ram,y)&0xa00)){
                if(corrected[i]!=(0x1720^((word(ram,0x600+y)>>8)&15))) {std::cerr<<"Witness failure frame="<<frame<<" xy="<<x<<','<<y<<" normal="<<dx<<','<<dy<<'\n';throw std::runtime_error("blue wedge remains above published horizon");}
                if(sprite[i]==0xffff){++wedges;++visible_wedge;}
            }
            const auto raw=sprite[i];const bool visible=raw!=0xffff&&(1<<((raw>>12)&3))>priority[i];
            enhanced[i]=palette.pen(!visible?corrected[i]:(raw&0x400f)==0x400a?corrected[i]+palette.num_entries():raw&0xfff);
        }
        if(visible_wedge){++sides[side];if(dy>0 && visible_wedge>best[side]){best[side]=visible_wedge;
            const auto stem="build/oracle/horizon/verified-"+std::string(side?"right":"left");if(save_images){image(stem+"-before.ppm",original);image(stem+"-after.ppm",enhanced);}std::cout<<"Witness "<<(side?"right":"left")<<" frame="<<frame<<" wedge pixels="<<visible_wedge<<'\n';}}
        ++frames;
    }
    require(frames==160 && sides[0]&&sides[1]&&wedges>10000&&preserved>10000,"actual opposite-roll/wedge/sea coverage missing");
    std::cout<<"MAME roll witnesses: 160/160 original RGB frames identical, zero skipped; enhanced witnesses="<<witness_frames<<", endpoint-unavailable="<<no_endpoints<<"; actual bank sides="<<sides[0]<<'/'<<sides[1]<<"; "<<wedges<<" visible wedge pixels removed; "<<preserved<<" terrain pixels preserved\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
