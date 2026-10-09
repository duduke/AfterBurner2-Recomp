#include "hwaudio/xboard_audio_deadlines.hpp"
#include "hwmath/xboard_device_deadlines.hpp"
#include <iostream>
#include <stdexcept>
#include <source_location>

static void check(bool v,std::source_location at=std::source_location::current()){if(!v)throw std::runtime_error("audio deadline regression line "+std::to_string(at.line()));}
int main()try{
    std::vector<uint8_t> rom(0x80000,0xff);XBoardPcm pcm(rom);
    XBoardAudioDeadlines audio(pcm);unsigned posted=0;
    audio.synchronize=[&]{++posted;};
    audio.advance(0);audio.update(0);check(!audio.fm_samples()&&!audio.pcm_samples());
    auto reg=[&](unsigned a,unsigned d,uint64_t ns){audio.ym_write(0,a,ns);audio.ym_write(1,d,ns);};
    reg(0x12,0xff,32000);reg(0x14,0x0a,32000);
    check(audio.deadline()==32000&&posted==1);
    audio.advance(40000); // Late mode delivery keeps the posted timestamp.
    if(audio.fm_samples()!=3||audio.deadline()!=240000)throw std::runtime_error("FM count="+std::to_string(audio.fm_samples())+" B deadline="+std::to_string(audio.deadline()));
    check(audio.ym_read(1,40000)&0x80);
    audio.advance(240000);check(audio.ym_read(1,240000)&2);
    check(audio.deadline()==496000);
    reg(0x14,0x20,240000);audio.advance(240000);
    check(!(audio.ym_read(1,240000)&3)&&audio.deadline()==audio.NEVER);
    audio.update(1000000);check(audio.fm_samples()==63&&audio.pcm_samples()==32);
    XBoardDeviceDeadlines devices;
    check(devices.deadline()==2368000);devices.advance(40000000);
    check(devices.deliveries[XBoardDeviceDeadlines::SoundUpdate]==2);
    check(devices.deliveries[XBoardDeviceDeadlines::TileLatch]==3);
    check(devices.deadline()>40000000);
    std::cout<<"Audio deadlines: independent periodic heads, stream indexes, timer B FM phase, late mode delivery and status passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
