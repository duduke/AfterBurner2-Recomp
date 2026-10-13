// Reuses original MAME completed bus phases; never runs or seeds the game.
#include "engine/feedback_cues.hpp"
#include "engine/mainbus.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
uint64_t be(const unsigned char* p,unsigned n){uint64_t v=0;while(n--)v=v<<8|*p++;return v;}
void read(std::ifstream& f,void* p,unsigned n){if(!f.read(static_cast<char*>(p),n))throw std::runtime_error("truncated MAME feedback fixture");}
void require(bool c,const char* m){if(!c)throw std::runtime_error(m);}
struct Phase {unsigned cpu{};bool write{};unsigned address{},data{},mask{},pc{},space{};uint64_t ns{};};
int main(int argc,char** argv)try {
    std::ifstream f(argc>1?argv[1]:"build/oracle/feedback.bin",std::ios::binary);char magic[8];read(f,magic,8);
    if(!memcmp(magic,"AB2HAP01",8)) {
        Feedback::Capture capture;std::array<unsigned,3> expected{},actual{};unsigned frames=0,boost=0,suppressed=0,unrelated=0;uint64_t seq=0;
        while(f.peek()!=EOF){const auto tag=f.get();if(tag==1){unsigned char b[25];read(f,b,25);
            Phase p{0,true,unsigned(be(b+12,4)),unsigned(be(b+16,2)),unsigned(be(b+18,2)),unsigned(be(b+8,4)),0,be(b,8)};
            const auto mode=be(b+20,2);capture.observe(p,mode==0x10);
            // Independent MAME reference observes the active record's committed
            // 32-bit handler value, regardless of the producing instruction PC.
            int cue=p.data==0xa030?0:p.data==0x9f7c?1:p.data==0xe662?2:-1;
            if(cue>=0 && p.mask==0xffff && (b[22]&0x80) && !be(b+23,2)) {
                if(mode==0x10)++expected[cue];else ++suppressed;
            }else ++unrelated;
        }else if(tag==2){unsigned char b[16];read(f,b,16);++frames;const auto ns=be(b+4,8),mode=be(b+12,2),throttle=be(b+14,2);
            auto cues=capture.take(be(b,4),ns,mode,throttle);for(auto e:cues.events){require(e.sequence>seq,"MAME event sequence repeated");seq=e.sequence;++actual[unsigned(e.cue)];}
            const bool reference=mode==16 && int16_t(throttle)>=253;require(cues.boost==reference,"MAME throttle state differs");boost+=reference;
        }else throw std::runtime_error("invalid MAME feedback record");}
        std::cout<<"MAME feedback: "<<frames<<" frames, cannon/missile/impact="<<actual[0]<<'/'<<actual[1]<<'/'<<actual[2]<<", boost="<<boost<<", suppressed/unrelated="<<suppressed<<'/'<<unrelated<<"\n";
        require(actual==expected,"cue assignment differs from independent MAME handler witnesses");require(actual[0]&&actual[1]&&actual[2]&&boost&&suppressed&&unrelated,"MAME positive/negative witnesses missing");return 0;
    }
    require(!memcmp(magic,"AB2ARB10",8),"MAME fixture version mismatch");
    MainBus::Memory memory;read(f,memory.bytes.data(),memory.bytes.size());f.ignore(65);
    Feedback::Capture capture;std::array<unsigned,3> expected{},actual{};uint64_t phases=0;unsigned attempted_cannon=0,attempted_missile=0;
    auto word=[&](uint32_t a){auto o=MainBus::memory_offset(a);return unsigned(memory.bytes[o])<<8|memory.bytes[o+1];};
    while(f.peek()!=EOF){unsigned char head[20];read(f,head,20);const auto count=be(head+16,4);require(count<1000000,"MAME group count invalid");
        uint64_t last_ns=0;
        for(unsigned i=0;i<count;++i){unsigned char b[24];read(f,b,24);Phase p{unsigned(b[0]-1),bool(b[9]),unsigned(be(b+10,4)),unsigned(be(b+14,2)),unsigned(be(b+16,2)),unsigned(be(b+18,4)),b[22],be(b+1,8)};last_ns=p.ns;
            if(b[23]){unsigned char entry[94];read(f,entry,94);if(!p.cpu){auto pc=be(entry+8,4);attempted_cannon+=pc==0x9ed8;attempted_missile+=pc==0x9e82;}}
            const bool flight=word(0x29c09c)==0x10;capture.observe(p,flight);
            if(!p.cpu&&p.write&&!p.space){const auto offset=MainBus::memory_offset(p.address);
                if(offset!=MainBus::UNMAPPED){
                    // Independent reference: an active 64-byte work-RAM record
                    // receives one of the three new 32-bit handler pointers.
                    if(flight&&p.mask==0xffff&&offset>=0x2012&&offset<0x6012&&(offset&63)==18 &&
                        (memory.bytes[offset-18]&0x80) && memory.bytes[offset-2]==0 && memory.bytes[offset-1]==0){
                        if(p.data==0xa030)++expected[0];if(p.data==0x9f7c)++expected[1];if(p.data==0xe662)++expected[2];
                    }
                    if(p.mask&0xff00)memory.bytes[offset]=p.data>>8;if(p.mask&255)memory.bytes[offset+1]=p.data;
                }
            }++phases;
        }
        auto cues=capture.take(phases,last_ns,word(0x29c09c),word(0x29c0d2));for(auto e:cues.events)++actual[unsigned(e.cue)];
    }
    std::cout<<"counts="<<actual[0]<<","<<actual[1]<<","<<actual[2]<<" expected="<<expected[0]<<","<<expected[1]<<","<<expected[2]<<" attempts="<<attempted_cannon<<","<<attempted_missile<<"\n";
    require(expected==actual,"feedback differs from independent MAME active-record handler assignments");require(actual==std::array<unsigned,3>{}&&attempted_cannon&&attempted_missile,"MAME attract suppression witnesses missing");
    require(attempted_cannon>=expected[0]&&attempted_missile>=expected[1],"MAME allocation call coverage");
    std::cout<<"MAME feedback fixture: "<<phases<<" phases, cannon/missile/impact="<<actual[0]<<'/'<<actual[1]<<'/'<<actual[2]<<", allocation attempts="<<attempted_cannon<<'/'<<attempted_missile<<"; all cue assignments identical, 0 skipped\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
