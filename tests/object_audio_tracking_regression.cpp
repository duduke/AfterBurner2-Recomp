#include "hwaudio/object_audio_tracking.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <array>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
using namespace ObjectAudio;
struct Main {std::array<uint8_t,0x8000> m{};
    uint16_t read(uint32_t a)const{const auto o=(a&0x7fff);return unsigned(m[o])<<8|m[(o+1)&0x7fff];}
    void word(uint32_t a,uint16_t v){m[a&0x7fff]=v>>8;m[(a+1)&0x7fff]=v;}
    void longword(uint32_t a,uint32_t v){word(a,v>>16);word(a+2,v);}
};
int main()try{
    std::cerr<<"Object tracking unit: entered main"<<std::endl;
    std::vector<uint8_t> rom(0xf000);auto root=[&](unsigned c,unsigned p){rom[0x735+2*(c-0x40)]=p;rom[0x736+2*(c-0x40)]=p>>8;};root(0x6e,0x3100);root(0x6f,0x3200);root(0xd8,0x3300);
    Main ram;auto read=[&](auto a){return ram.read(a);};Tracking t;t.configure(rom);t.prime(read);
    constexpr uint32_t p=0x3fa800,sp=0x3fff00;
    auto write=[&](uint64_t ns,uint32_t pc,uint32_t a,uint16_t d,uint16_t mask=65535){t.main_write(ns,pc,a,d,mask,16,read);auto old=ram.read(a);ram.word(a,(old&~mask)|(d&mask));};
    auto activate=[&](uint64_t ns){write(ns,0x100,p,0x8000);write(ns+1,0x100,p+16,1);write(ns+2,0x100,p+18,0x56ce);};
    std::cerr<<"Object tracking unit: configured"<<std::endl;
    activate(10);ram.longword(p+24,0xffe08000);ram.longword(p+30,0x00120010);ram.word(p+36,0xffe0);ram.word(0xffff8d18,0xc00);
    auto f=t.frame(1,20,read);require(f.sources.size()==1&&f.sources[0].position_known,"missile source registry missing");const auto id=f.sources[0].id;
    require(f.sources[0].position.x==int32_t(0xffe08000)&&f.sources[0].position.y==0x120010&&f.sources[0].position.depth==-32&&f.sources[0].position.player_depth==3072,"raw native coordinate contract");
    std::cerr<<"Object tracking unit: missile coordinates"<<std::endl;
    ram.longword(sp,0x1571c);t.context(0xb868,p,sp,0);write(30,0xb868,0x3f890c,0x6e00,0xff00);
    t.context(0xb81e,0,sp,0xfeed0000);write(40,0xb81e,0xe8016,0x6e,255);
    using K=XBoardSoundCpu::Kind;XBoardSoundCpu::Registers r{};
    auto sound=[&](uint64_t ns,K k,unsigned pc,unsigned a,unsigned d){XBoardSoundCpu::Event e{k,0,uint16_t(pc),uint16_t(a),uint8_t(d)};t.observe_sound(e,r,ns);};
    sound(50,K::IoRead,0x7a,0x40,0x6e);sound(51,K::MemoryWrite,0x7c,0xf800,0x6e);sound(60,K::MemoryRead,0x702,0xf800,0x6e);
    r.de=0xfad0;sound(70,K::MemoryRead,0xc76,0x3100,2);r.ix=0xfad0;sound(80,K::IoWrite,0,0,0x28);sound(81,K::IoWrite,0,1,42);
    sound(82,K::MemoryWrite,0,0xf086,0);require(t.fm[0].id==id&&t.pcm[0].id==id,"object identity lost through queue/NMI/sequencer/physical voices");
    r.de=0xfad0;sound(85,K::MemoryRead,0xc76,0x3200,1);sound(86,K::MemoryWrite,0,0xf086,0);require(!t.pcm[0].id,"wrong descriptor root inherited ownership");
    std::cerr<<"Object tracking unit: first sequencer and voices"<<std::endl;
    // Copy queue metadata by actual LDIR source and destination, including overlap.
    t.context(0xb868,p,sp,0);write(90,0xb868,0x3f890c,0x6e,255);t.context(0xb81e,0,sp,1);write(91,0xb81e,0xe8016,0x6e,255);
    sound(92,K::IoRead,0x7a,0x40,0x6e);sound(93,K::MemoryWrite,0x7c,0xf801,0x6e);sound(94,K::MemoryRead,0x70c,0xf801,0x6e);sound(95,K::MemoryWrite,0x70c,0xf800,0x6e);sound(96,K::MemoryRead,0x702,0xf800,0x6e);
    r.de=0xfb26;sound(97,K::MemoryRead,0xc76,0x3100,1);r.ix=0xfb26;sound(98,K::MemoryWrite,0,0xf08e,0);require(t.pcm[1].id==id,"Z80 queue shift lost object identity");
    // An unowned command overwrites the hardware latch; do not match by byte alone.
    t.context(0xb81e,0,sp,7);write(100,0xb81e,0xe8016,0x6e,255);sound(101,K::IoRead,0x7a,0x40,0x6e);sound(102,K::MemoryWrite,0x7c,0xf800,0x6e);sound(103,K::MemoryRead,0x702,0xf800,0x6e);r.de=0xfad0;sound(104,K::MemoryRead,0xc76,0x3100,1);r.ix=0xfad0;sound(105,K::MemoryWrite,0,0xf086,0);require(!t.pcm[0].id,"unknown same-byte latch stole an object's owner");
    std::cerr<<"Object tracking unit: queue/latch controls"<<std::endl;
    write(110,0x100,p,0);activate(120);const auto reused=t.frame(2,130,read);require(reused.sources.size()==1,"reused source missing");const auto newer=reused.sources[0].id;require(newer.generation>id.generation&&!t.alive(id,130),"reused slot inherited old identity");
    require(!t.frame(3,130,read).fm[0].id&&!t.frame(3,130,read).pcm[1].id,"retired object remained on physical voices");
    const auto epoch=t.current_epoch();write(140,0x100,0x140004,0,255);require(t.current_epoch()>epoch&&!t.alive(newer,141),"sound reset retained old identity");
    t.reset(150);require(t.frame(4,151,read).sources.empty()&&t.take_events(151).size()==1,"restart leaked registry or events");
    std::cerr<<"Object tracking unit: lifetime/reset"<<std::endl;
    // The natural positive family is the firing aircraft of enemy launch $D8.
    constexpr uint32_t shooter=0x3faa00,second=0x3fab00;
    write(200,0x100,shooter,0x8000);write(201,0x100,shooter+16,1);write(202,0x100,shooter+18,0x1234);
    ram.longword(sp,0x14da0);t.context(0xb868,shooter,sp,0);write(203,0xb868,0x3f890c,0xd800,0xff00);t.context(0xb81e,0,sp,0);write(204,0xb81e,0xe8016,0xd8,255);
    sound(205,K::IoRead,0x7a,0x40,0xd8);sound(206,K::MemoryWrite,0x7c,0xf800,0xd8);sound(207,K::MemoryRead,0x702,0xf800,0xd8);r.de=r.ix=0xfad0;sound(208,K::MemoryRead,0xc76,0x3300,1);sound(209,K::MemoryWrite,0,0xf086,0);
    const auto shot_id=t.pcm[0].id;require(bool(shot_id)&&shot_id.record==shooter,"enemy launch did not retain shooter identity");
    const auto shot_frame=t.frame(5,210,read);require(shot_frame.sources.size()==1&&!shot_frame.sources[0].position_known,"unverified aircraft coordinates were invented");
    ram.longword(sp,0x1571c);t.context(0xb868,shooter,sp,0);write(211,0xb868,0x3f890c,0xd800,0xff00);t.context(0xb81e,0,sp,0);write(212,0xb81e,0xe8016,0xd8,255);
    sound(213,K::IoRead,0x7a,0x40,0xd8);sound(214,K::MemoryWrite,0x7c,0xf800,0xd8);sound(215,K::MemoryRead,0x702,0xf800,0xd8);sound(216,K::MemoryRead,0xc76,0x3300,1);sound(217,K::MemoryWrite,0,0xf086,0);require(!t.pcm[0].id,"unrelated caller acquired shooter ownership");
    ram.longword(sp,0x14da0);t.context(0xb868,shooter,sp,0);write(220,0xb868,0x3f890c,0xd800,0xff00);t.context(0xb81e,0,sp,0);write(221,0xb81e,0xe8016,0xd8,255);
    write(280,0x100,second,0x8000);t.context(0xb868,second,sp,0);write(300,0xb868,0x3f890c,0xd800,0xff00);t.context(0xb81e,0,sp,0);write(310,0xb81e,0xe8016,0xd8,255);
    sound(250,K::IoRead,0x7a,0x40,0xd8);sound(251,K::MemoryWrite,0x7c,0xf800,0xd8);sound(252,K::MemoryRead,0x702,0xf800,0xd8);sound(253,K::MemoryRead,0xc76,0x3300,1);sound(254,K::MemoryWrite,0,0xf086,0);require(t.pcm[0].id==shot_id,"future latch delivery acquired an earlier read");
    std::cerr<<"Object tracking unit: aircraft timing"<<std::endl;
    // Expanded presentation contracts remain separate from the retained raw
    // tracker fixture: a lock marker belongs to its live aircraft parent.
    std::vector<uint8_t> main_rom(0x40000);main_rom[0x33e58]=128;main_rom[0xa716]=0x40;
    root(0xe0,0x3400);Tracking moving;moving.configure(rom,main_rom);Main scene;auto view=[&](auto a){return scene.read(a);};moving.prime(view);
    constexpr unsigned parent=0x3fb000,marker=parent+64;
    auto put=[&](uint64_t ns,unsigned a,unsigned d){moving.main_write(ns,0,a,d,65535,16,view);scene.word(a,d);};
    put(100,parent,0x8000);put(101,parent+16,1);put(102,parent+18,0x1234);
    put(103,marker,0x8000);put(104,marker+16,0);put(105,marker+18,0x7bb6);
    scene.word(parent+42,32);scene.word(parent+44,0);scene.word(parent+36,1024);scene.word(0xffff8d18,1024);scene.longword(sp,0x7bac);
    moving.context(0xb868,marker,sp,0);moving.main_write(110,0xb868,0x3f890c,0xe000,0xff00,16,view);
    moving.context(0xb81e,0,sp,0);moving.main_write(120,0xb81e,0xe8016,0xe0,255,16,view);
    auto cue=[&](uint64_t ns,K k,unsigned pc,unsigned a,unsigned d){XBoardSoundCpu::Event e{k,0,uint16_t(pc),uint16_t(a),uint8_t(d)};moving.observe_sound(e,r,ns);};
    cue(130,K::IoRead,0x7a,0x40,0xe0);cue(131,K::MemoryWrite,0x7c,0xf800,0xe0);cue(132,K::MemoryRead,0x702,0xf800,0xe0);
    r.de=r.ix=0xfad0;cue(133,K::MemoryRead,0xc76,0x3400,1);cue(134,K::IoWrite,0,0,0x28);cue(135,K::IoWrite,0,1,42);
    const auto aircraft=moving.fm[0].id;require(aircraft.record==parent,"FM lock cue followed the marker instead of its aircraft");
    std::array<float,directions> w{};require(!moving.render(aircraft,140,w),"future trajectory applied before its timestamp");
    moving.frame(1,1000000,view);require(moving.render(aircraft,1000000,w,false)&&w[14]>.999f,"native positive X was not rotated to listener-left");
    scene.word(parent+42,65504);moving.frame(2,18000000,view);
    require(moving.render(aircraft,17000000,w)&&w[14]>.999f,"queued position was replaced by a future frame");
    require(moving.render(aircraft,18000000,w)&&w[14]>.999f,"movement did not start continuously from the previous bearing");
    require(moving.render(aircraft,34000000,w)&&w[10]>.999f,"movement did not reach listener-right");
    put(35000000,parent,0);require(!moving.render(aircraft,35000001,w),"retired aircraft audio followed its surviving marker");
    std::cerr<<"Object tracking unit: lock parent and movement"<<std::endl;
    main_rom[0x91b8]=0x50;constexpr unsigned blast=0x3fc000;
    put(36000000,blast,0x8000);put(36000001,blast+16,0);put(36000002,blast+18,0x90ac);
    scene.longword(blast+54,0x29c190);scene.longword(0x29c190,64000);scene.longword(0x29c194,12800);
    scene.word(blast+42,32);scene.word(blast+36,1024);scene.word(0x29c092,65408);scene.word(0x29c090,0);scene.longword(sp,0x91ac);
    moving.context(0xb868,blast,sp,0);moving.main_write(36000003,0xb868,0x3f890c,0x5000,0xff00,16,view);
    const auto blast_frame=moving.frame(3,37000000,view);const auto found=std::find_if(blast_frame.sources.begin(),blast_frame.sources.end(),[](const auto& source){return source.id.record==blast;});
    require(found!=blast_frame.sources.end(),"world explosion producer lost its source");
    require(moving.render(found->id,37000000,w,false),"shared-RAM world explosion bearing unavailable");
    const auto expected_world=weights(468,100,0);for(unsigned d=0;d<directions;++d)require(std::abs(w[d]-expected_world[d])<1e-5,"world shared offsets or elevation were lost");
    std::cout<<"Object audio: generation-safe slots, raw missile coordinates, main ring/latch, Z80 NMI/queue shifts, sequencer/voices, unknown fallback, reuse and reset passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
