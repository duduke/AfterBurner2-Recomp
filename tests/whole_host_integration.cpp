#include "engine/xboard_game.hpp"
#include <iostream>
#include <cmath>

void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
XBoardGame::Host::Input input(unsigned frame){
    XBoardGame::Host::Input v;v.analog.fill(0x10);for(unsigned i=0;i<5;++i)v.analog[i]=0x80;
    if(frame>=360&&frame<372)v.buttons&=~0x40;
    if(frame>=420&&frame<432)v.buttons&=~0x08;
    if(frame>=480){const auto phase=((frame-480)/24)%12;
        constexpr uint8_t x[]={0x80,0x20,0xe0,0x80,0x80,0x80,0x80,0x20,0xe0,0x80,0x80,0x80};
        constexpr uint8_t y[]={0x80,0x80,0x80,0x40,0xc0,0x80,0x80,0xc0,0x40,0x80,0x80,0x80};
        v.analog[0]=x[phase];v.analog[1]=y[phase];v.analog[2]=phase==5?0:phase==6?0xff:0x80;
        if(phase==7||phase==9||phase==10)v.buttons&=~0x10;
        if(phase==8||phase==9||phase==10)v.buttons&=~0x20;
    }
    return v;
}
int main()try{
    require(roms.load_aburner2(),"ROMs required");XBoardGame game(roms);double energy=0;uint64_t samples=0;unsigned changed=0;
    for(unsigned f=0;f<781;++f){game.tick(input(f));auto audio=game.take_audio();samples+=audio.size();for(auto s:audio)energy+=double(s)*s;
        if(f>480)for(auto p:game.pixels())changed+=p.r||p.g||p.b;
    }
    require(samples>100000&&energy>1000000&&changed>10000,"native game must produce visible frames and audible sound");
    require(game.board().reset_deliveries>0&&game.board().instructions>10000000,"native game must execute retained CPUs and sub resets");
    // Recreate after active gameplay (possibly mid-instruction). Compare
    // real memory, hidden banks, pixels and audio with an independent boot.
    game.restart();XBoardGame fresh(roms);
    for(unsigned f=0;f<8;++f){game.tick(input(f));fresh.tick(input(f));require(game.take_audio()==fresh.take_audio(),"restart audio differs from cold boot");}
    require(game.board().memory.bytes==fresh.board().memory.bytes&&game.board().memory.sprite_buffer==fresh.board().memory.sprite_buffer&&game.board().memory.road_buffer==fresh.board().memory.road_buffer,"restart RAM/banks differ from cold boot");
    const auto& a=game.pixels();const auto& b=fresh.pixels();for(size_t i=0;i<a.size();++i)require(a[i].r==b[i].r&&a[i].g==b[i].g&&a[i].b==b[i].b,"restart RGB differs from cold boot");
    std::cout<<"Whole-host game integration: 781 physical frames, endpoint/weapon inputs, native RGB/audio and retained CPUs; restart matches fresh cold boot. Audio RMS "<<std::sqrt(energy/samples)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
