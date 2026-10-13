#include "engine/xboard_game.hpp"
#include <fstream>
#include <iostream>
#include <cmath>
uint64_t number(std::ifstream& in,unsigned n){uint64_t x=0;while(n--){const auto b=in.get();if(b==EOF)throw std::runtime_error("truncated music oracle");x=x<<8|unsigned(b);}return x;}
void tag(std::ifstream& in,const char* expected){char b[8];if(!in.read(b,8)||std::string_view(b,8)!=std::string_view(expected,8))throw std::runtime_error("invalid music oracle");}
int main(int argc,char** argv)try {
    const bool require_effects=argc>2 && std::string_view(argv[2])=="--require-fm-effects";
    const std::string path=argc>1?argv[1]:"build/oracle/music-routing-781-v3.bin";
    if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
    std::ifstream data(path,std::ios::binary);tag(data,"AB2MUS01");
    struct Row{unsigned kind;uint64_t ns;uint16_t pc,address;uint8_t value;uint16_t ix;uint8_t track,playing,mask,attenuation;};
    std::vector<Row> rows;uint64_t end=0;
    while(data.peek()!=EOF){const auto kind=number(data,1);if(kind==255){end=number(data,8);break;}Row r{};r.kind=kind;r.ns=number(data,8);r.pc=number(data,2);r.address=number(data,2);r.value=number(data,1);r.ix=number(data,2);r.track=number(data,1);r.playing=number(data,1);r.mask=number(data,1);r.attenuation=number(data,1);rows.push_back(r);}
    if(!end || data.peek()!=EOF)throw std::runtime_error("incomplete music oracle");
    HWPalette palette{8192};HWRoad road{0x1700,0x1720,0x1780,-166};HWSprites sprites;HWTiles tiles;
    road.init(roms.road.rom,roms.road.length);sprites.init(roms.sprites.rom,roms.sprites.length);tiles.init_gfx(roms.tiles.rom,roms.tiles.length);
    XBoardSoundBoard sound({roms.soundcpu.rom,roms.soundcpu.length},{roms.pcm.rom,roms.pcm.length});
    XBoardGame::Host host(roms,palette,road,sprites,tiles,sound);sound.enable_music_presentation({roms.soundcpu.rom,roms.soundcpu.length});
    std::ifstream inputs(path+".inputs",std::ios::binary);tag(inputs,"AB2MIP01");for(auto& v:host.switches)v=number(inputs,1);
    while(inputs.peek()!=EOF){XBoardGame::Host::Input v;v.ns=number(inputs,8);v.buttons=number(inputs,1);for(auto& a:v.analog)a=number(inputs,1);host.input(v);}
    size_t count=0;unsigned shared_fm=0,ym_register=0;std::array<unsigned,6> starts{};
    sound.observer=[&](const auto& e,uint64_t ns){
        if(ns>=end)return;
        const auto k=unsigned(e.kind);bool interesting=false;
        if(k==0 && e.pc==0xc76){for(auto cmd:MusicRouting::commands){const auto index=0x735+2*(cmd-0x40);const auto root=roms.soundcpu.rom[index]|roms.soundcpu.rom[index+1]<<8;if(e.address==root)interesting=true;}}
        if(k==1 && ((e.address==0xf810 && !e.data && (e.pc==0x658 || e.pc==0x68f)) ||
            (e.address==0xfeb8 && sound.music_metadata()->playing) ||
            (e.address>=0xf820 && e.address<0xfad0 && (e.address-0xf820)%43==0) || (e.address>=0xf000 && e.address<0xf100)))interesting=true;
        if(k==3 && (e.address==0 || e.address==1))interesting=true;
        if(!interesting)return;
        if(count>=rows.size())throw std::runtime_error("extra native music observation");const auto& r=rows[count++];const auto& m=*sound.music_metadata();const auto ix=sound.cpu.registers().ix;
        if(r.kind!=k || r.ns!=ns || r.pc!=e.pc || r.address!=e.address || r.value!=e.data || r.ix!=ix || r.track!=m.track+1 || bool(r.playing)!=m.playing || r.mask!=m.fm_music || std::abs(m.fade-std::pow(2.f,-float(r.attenuation)/8))>1e-6) {
            std::cerr<<"Music oracle row "<<count<<" ns="<<ns<<'/'<<r.ns<<" pc="<<std::hex<<e.pc<<'/'<<r.pc<<" address="<<e.address<<'/'<<r.address<<" IX="<<ix<<'/'<<r.ix<<" mask="<<m.fm_music<<'/'<<unsigned(r.mask)<<std::dec<<" track="<<m.track+1<<'/'<<unsigned(r.track)<<" playing="<<m.playing<<'/'<<unsigned(r.playing)<<'\n';throw std::runtime_error("native music routing differs from MAME");
        }
        if(k==0 && e.pc==0xc76 && m.track>=0)++starts[m.track];
        if(k==3 && e.address==0)ym_register=e.data;
        if(k==3 && e.address==1 && ix>=0xfad0 && ix<=0xfe82 && (ix-0xf820)%43==0 && (ym_register>=0x20 || (ym_register==8 && (e.data&0x78))))++shared_fm;
    };
    while(host.time()<end){host.step();host.deliver();}
    if(count!=rows.size() || !starts[0] || !starts[1] || (require_effects && !shared_fm))throw std::runtime_error("music oracle coverage incomplete");
    std::cout<<"Music MAME oracle: "<<count<<" of "<<rows.size()<<" exact transactions/IX/track/FM ownership, 0 skipped; shared FM writes="<<shared_fm<<", starts=";for(auto n:starts)std::cout<<n<<',';std::cout<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
