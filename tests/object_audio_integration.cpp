#include "engine/game_session.hpp"
#include <fstream>
#include <iostream>
#include <set>
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char** argv)try{
    require(roms.load_aburner2(),"ROMs required");
    std::ifstream input(argc>1?argv[1]:"build/oracle/object-audio/weapons-v4.txt.inputs");std::string magic;unsigned frames,a,b,c;
    require(bool(input>>magic>>frames>>a>>b>>c)&&magic=="AB2PHYS1"&&frames==8000&&a<256&&b<256&&c<256,"actual MAME input recording required");
    GameSession plain(roms),tracked(roms);const std::array<uint8_t,3> dip{uint8_t(a),uint8_t(b),uint8_t(c)};
    plain.set_dip_switches(dip);tracked.set_dip_switches(dip);plain.capture_music(true);tracked.capture_music(true);tracked.capture_cockpit(true);
    unsigned positions=0,owned=0,events=0;std::array<uint64_t,2> rendered{};uint64_t epoch=0;std::set<ObjectAudio::Id> identities;
    for(unsigned n=0;n<frames;++n){
        unsigned index,pins;std::array<unsigned,8> adc{};require(bool(input>>index>>pins)&&index==n&&pins<256,"physical input gap");for(auto& v:adc)require(bool(input>>v)&&v<256,"physical ADC");
        auto axis=[](unsigned v,unsigned lo,unsigned hi){return (float(v)-128.f)/float(v<128?128-lo:hi-128);};
        GameControls control{axis(adc[0],32,224),axis(adc[1],64,192),axis(adc[2],0,255),!(pins&16),!(pins&32),!(pins&64),!(pins&8)};
        const auto roundtrip=arcade_inputs(control);require(roundtrip.buttons==pins,"button roundtrip");for(unsigned i=0;i<8;++i)require(roundtrip.analog[i]==adc[i],"ADC roundtrip");
        tracked.capture_cockpit(true); // Polling must not reset identities.
        plain.advance(control);tracked.advance(control);auto x=plain.frame(),y=tracked.frame();
        require(x.info.number==y.info.number&&x.info.time_ns==y.info.time_ns,"tracking changed timing");
        require(std::equal(x.pixels.begin(),x.pixels.end(),y.pixels.begin(),[](auto p,auto q){return p.r==q.r&&p.g==q.g&&p.b==q.b;}),"tracking changed RGB");
        require(plain.take_audio()==tracked.take_audio(),"tracking changed PCM");
        auto p=plain.take_music(),q=tracked.take_music();require(p.size()==q.size(),"tracking changed presentation batches");
        for(unsigned i=0;i<p.size();++i){require(p[i].music==q[i].music&&p[i].effects==q[i].effects&&p[i].first==q[i].first,"tracking changed music/effects export");events+=q[i].object_events.size();for(const auto& block:q[i].spatial)for(const auto& direction:block.objects)for(float v:direction)rendered[block.chip]+=v!=0;}
        const auto f=tracked.take_audio_objects();require(f.number==y.info.number&&f.ns==y.info.time_ns,"object frame timestamp");epoch=f.epoch;
        for(const auto& s:f.sources){require(s.id.epoch==epoch,"stale source epoch");identities.insert(s.id);positions+=s.position_known;}
        for(const auto& v:f.pcm)if(v.id){++owned;require(std::any_of(f.sources.begin(),f.sources.end(),[&](const auto& s){return s.id==v.id;}),"voice follows a retired/reused object");}
        require(tracked.take_audio_objects().number==0,"object frame consumed twice");
        if(n%2000==1999)std::cout<<"Object tracking native checkpoint "<<n+1<<" frames\n"<<std::flush;
    }
    require(rendered[0]>0&&rendered[1]>0,"natural object FM/PCM rendering coverage missing");
    require(positions>0&&owned>0&&events>0&&identities.size()>2,"native tracking coverage incomplete");
    tracked.capture_audio_objects(false);require(tracked.take_audio_objects().number==0,"disable retained object frame");
    tracked.capture_cockpit(true);plain.advance({});tracked.advance({});require(plain.take_audio()==tracked.take_audio(),"capture toggle changed audio");
    require(tracked.take_audio_objects().epoch>epoch,"re-enable reused old epoch");
    plain.restart();tracked.restart();for(unsigned n=0;n<8;++n){plain.advance({});tracked.advance({});require(plain.take_audio()==tracked.take_audio(),"restart changed audio");require(tracked.take_audio_objects().sources.empty(),"restart retained old source");}
    std::cout<<"Object tracking native: 8000 of 8000 RGB/PCM/time and music/effects frames identical, 0 skipped; missile positions="<<positions<<" owned PCM observations="<<owned<<" identities="<<identities.size()<<" exported events="<<events<<" rendered FM/PCM samples="<<rendered[0]<<'/'<<rendered[1]<<"; toggle/epoch/restart passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
