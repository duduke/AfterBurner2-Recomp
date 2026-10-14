#include "engine/xboard_game.hpp"
#include <fstream>
#include <iostream>
#include <set>
uint64_t number(std::ifstream& in,unsigned n){uint64_t x=0;while(n--){auto b=in.get();if(b==EOF)throw std::runtime_error("truncated spatial oracle");x=(x<<8)|unsigned(b);}return x;}
void tag(std::ifstream& in,const char* wanted){char b[8];if(!in.read(b,8)||std::string_view(b,8)!=std::string_view(wanted,8))throw std::runtime_error("invalid spatial oracle");}
int main(int argc,char** argv)try{
    const std::string path=argc>1?argv[1]:"build/oracle/spatial-audio/routing.bin";
    if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
    std::ifstream data(path,std::ios::binary);tag(data,"AB2SPA01");
    struct Row{unsigned k;uint64_t ns;uint16_t pc,a;uint8_t value;uint16_t ix,de,bc;};std::vector<Row> rows;uint64_t end=0;
    while(data.peek()!=EOF){const auto k=number(data,1);if(k==255){end=number(data,8);break;}rows.push_back({unsigned(k),number(data,8),uint16_t(number(data,2)),uint16_t(number(data,2)),uint8_t(number(data,1)),uint16_t(number(data,2)),uint16_t(number(data,2)),uint16_t(number(data,2))});}
    if(!end||data.peek()!=EOF)throw std::runtime_error("incomplete spatial oracle");
    HWPalette palette{8192};HWRoad road{0x1700,0x1720,0x1780,-166};HWSprites sprites;HWTiles tiles;
    road.init(roms.road.rom,roms.road.length);sprites.init(roms.sprites.rom,roms.sprites.length);tiles.init_gfx(roms.tiles.rom,roms.tiles.length);
    XBoardSoundBoard sound({roms.soundcpu.rom,roms.soundcpu.length},{roms.pcm.rom,roms.pcm.length});XBoardGame::Host host(roms,palette,road,sprites,tiles,sound);
    sound.enable_music_presentation({roms.soundcpu.rom,roms.soundcpu.length});sound.enable_cockpit({roms.soundcpu.rom,roms.soundcpu.length},{roms.maincpu.rom,roms.maincpu.length});
    std::ifstream inputs(path+".inputs",std::ios::binary);tag(inputs,"AB2MIP01");for(auto& v:host.switches)v=number(inputs,1);
    while(inputs.peek()!=EOF){XBoardGame::Host::Input v;v.ns=number(inputs,8);v.buttons=number(inputs,1);for(auto& a:v.analog)a=number(inputs,1);host.input(v);}
    CockpitRouting::Block latest;uint64_t pcm_samples=0;
    const auto keep_spatial=sound.audio.spatial;const auto keep_stream=sound.audio.stream;
    sound.audio.spatial=[&](const auto& b){latest=b;keep_spatial(b);};
    sound.audio.stream=[&](unsigned chip,uint64_t ns,uint64_t first,const auto& samples){
        if(chip==1){if(latest.chip!=chip||latest.first!=first||latest.music.size()!=samples.size())throw std::runtime_error("PCM tap frontier mismatch");
            for(size_t i=0;i<samples.size();++i){auto sum=latest.music[i];for(const auto& group:latest.groups){sum.left+=group[i].left;sum.right+=group[i].right;}if(sum.left!=samples[i].left||sum.right!=samples[i].right)throw std::runtime_error("PCM voice tap lost or duplicated samples");++pcm_samples;}}
        keep_stream(chip,ns,first,samples);
    };
    size_t count=0;uint64_t descriptor=0,direct=0,fm=0,pcm=0;std::set<uint16_t> owners;
    sound.observer=[&](const auto& e,uint64_t ns){if(ns>=end)return;const auto k=unsigned(e.kind);
        if(!((k==0&&(e.pc==0xc76||(e.pc==0x17a2&&e.address==e.pc)))||(k==1&&e.address>=0xf000&&e.address<0xf100)||(k==3&&e.address<=1)))return;
        if(count>=rows.size())throw std::runtime_error("extra spatial transaction");const auto& row=rows[count++];const auto r=sound.cpu.registers();
        if(k!=row.k||ns!=row.ns||e.pc!=row.pc||e.address!=row.a||e.data!=row.value||r.ix!=row.ix||r.de!=row.de||r.bc!=row.bc)throw std::runtime_error("spatial MAME transaction/IX/DE/BC mismatch at row "+std::to_string(count));
        if(k==0&&e.pc==0xc76&&e.address!=e.pc)++descriptor;
        if(k==0&&e.pc==0x17a2)++direct;
        if(k==1){++pcm;owners.insert(r.ix);}if(k==3)++fm;
    };
    while(host.time()<end){host.step();host.deliver();sound.spatial_blocks={};}
    if(count!=rows.size()||!descriptor||!direct||owners.size()<8)throw std::runtime_error("spatial oracle coverage incomplete");
    std::cout<<"Spatial ownership MAME: "<<count<<" of "<<rows.size()<<" identical transactions/IX/DE/BC, 0 skipped; descriptors="<<descriptor<<" direct samples="<<direct<<" PCM writes="<<pcm<<" FM writes="<<fm<<" observed owners="<<owners.size()<<" exact PCM sums="<<pcm_samples<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
