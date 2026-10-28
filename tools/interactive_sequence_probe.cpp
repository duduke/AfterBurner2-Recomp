// Read-only discovery of original sequence frames/requests after the existing
// directed Practice startup. Generated pictures/logs belong under build/.
#include "engine/native_stage_skip.hpp"
#include "engine/game_controls.hpp"
#include <fstream>
#include <iostream>
#include <filesystem>
#include <set>
int main(int argc,char** argv)try{
    if(argc!=3)throw std::runtime_error("usage: interactive_sequence_probe stage output-directory");
    size_t used=0;unsigned stage=std::stoul(argv[1],&used);
    if(used!=std::string(argv[1]).size())throw std::runtime_error("Stage must be an integer");
    const auto root=std::filesystem::weakly_canonical("build/oracle");
    const auto out=std::filesystem::weakly_canonical(argv[2]);
    const auto relative=out.lexically_relative(root);
    if(relative.empty() || relative.is_absolute() || *relative.begin()=="..")throw std::runtime_error("Probe images must remain under build/oracle");
    std::filesystem::create_directories(out);
    if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
    XBoardGame game(roms);auto& h=game.board();
    auto tick=[&](GameControls c){auto pins=arcade_inputs(c);XBoardGame::Host::Input in;in.buttons=pins.buttons;in.analog=pins.analog;game.tick(in);game.take_audio();};
    for(unsigned f=0;f<600;++f){GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;tick(c);}
    NativeStageSkip::enter(h,roms,stage,[&]{tick({});});
    std::set<uint32_t> handlers;std::set<uint32_t> tanker;std::set<uint64_t> art_seen;
    for(unsigned f=0;f<1800;++f){
        tick({});
        if(f%30==0){
            std::cout<<f<<" mode="<<NativeStageSkip::peek16(h,0x29c09c)<<" missiles="<<NativeStageSkip::peek16(h,0xffff8d66)<<" pending="<<NativeStageSkip::peek16(h,0xffff9100)<<" selector="<<NativeStageSkip::peek16(h,0xffff90b0)<<'\n';
            for(unsigned i=0;i<256;++i){unsigned p=0xffffa000+i*64;
                if(!(h.memory.bytes.at(MainBus::memory_offset(p))&0x80))continue;
                auto pc=NativeStageSkip::peek32(h,p+16);if(pc>=0x11000&&pc<0x13100&&handlers.insert(pc).second)
                    std::cout<<" handler="<<std::hex<<pc<<" slot="<<p<<std::dec<<" state="<<NativeStageSkip::peek16(h,p+40)<<'\n';
                auto art=NativeStageSkip::peek32(h,p+12);
                if(art>=0x10000 && art+16<roms.subcpu.length && art_seen.insert((uint64_t(art)<<32)|pc).second)
                    std::cout<<" art="<<std::hex<<art<<" pc="<<pc<<" palette="<<unsigned(h.memory.bytes.at(MainBus::memory_offset(p+3)))<<std::dec<<" xyz="<<int16_t(NativeStageSkip::peek16(h,p+42))<<','<<int16_t(NativeStageSkip::peek16(h,p+44))<<','<<int16_t(NativeStageSkip::peek16(h,p+36))<<'\n';
                if((art==0x1d770||art==0x1d780)&&tanker.insert(p).second)
                    std::cout<<" tanker="<<std::hex<<art<<" flags="<<NativeStageSkip::peek16(h,p)<<std::dec<<" xy="<<int16_t(NativeStageSkip::peek16(h,p+4))<<','<<int16_t(NativeStageSkip::peek16(h,p+6))<<'\n';
            }
        }
        if(f%150==0){std::ofstream terrain(out/(std::to_string(f)+"-terrain.ppm"),std::ios::binary);terrain<<"P6\n320 224\n255\n";for(auto c:game.terrain_material()){terrain.put(c>>16);terrain.put(c>>8);terrain.put(c);}}
        if(f%150==0){std::ofstream image(out/(std::to_string(f)+".ppm"),std::ios::binary);image<<"P6\n320 224\n255\n";for(auto p:game.pixels()){image.put(p.r);image.put(p.g);image.put(p.b);}}
    }
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
