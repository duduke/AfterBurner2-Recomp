#include "hwaudio/xboard_pcm.hpp"
#include "romloader.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <stdexcept>
#include <algorithm>
#include <string>

static uint64_t number(std::ifstream& in,unsigned n) {
    uint64_t v=0;while(n--){int c=in.get();if(c==EOF)throw std::runtime_error("truncated PCM capture");v=(v<<8)|c;}return v;
}
static XBoardPcm::State state(std::ifstream& in) {
    XBoardPcm::State s;
    if(!in.read(reinterpret_cast<char*>(s.ram.data()),s.ram.size())||!in.read(reinterpret_cast<char*>(s.low.data()),s.low.size()))
        throw std::runtime_error("truncated PCM state");
    return s;
}
int main(int argc,char** argv) try {
    std::ifstream in(argc>1?argv[1]:"build/oracle/pcm.bin",std::ios::binary);
    char magic[8];if(!in.read(magic,8)||std::memcmp(magic,"AB2PCM02",8))throw std::runtime_error("invalid PCM capture");
    const auto rom_kind=number(in,1);if(rom_kind>1)throw std::runtime_error("invalid PCM ROM fixture kind");
    RomLoader rom;rom.init(0x80000);std::fill(rom.rom,rom.rom+rom.length,0xff);
    if(rom_kind==1) {
        for(unsigned a=0;a<rom.length;++a)rom.rom[a]=((a*37)^(a>>8)^((a>>16)*53))&255;
    }else if(rom.load_rom("mpr-10931.11",0,0x20000,0x9209068f,RomLoader::NORMAL)||
       rom.load_rom("mpr-10930.12",0x20000,0x20000,0x6493368b,RomLoader::NORMAL)||
       rom.load_rom("epr-11102.13",0x40000,0x20000,0x6c07c78d,RomLoader::NORMAL))
        throw std::runtime_error("PCM ROM load failed");
    XBoardPcm pcm({rom.rom,rom.length});
    pcm.load_initial(state(in)); // one initial device state, no later reseeding
    uint64_t batches=0,samples=0,reads=0,writes=0,nonzero=0,unclamped=0;
    while(in.peek()!=EOF) {
        auto kind=number(in,1);
        if(kind==1||kind==3) {
            const auto a=number(in,2),d=number(in,1);
            if(a>=0x800)throw std::runtime_error("invalid PCM register address");
            if(kind==1){pcm.write(a,d);++writes;}
            else {if(pcm.read(a)!=d)throw std::runtime_error("PCM register read differs");++reads;}
        } else if(kind==2) {
            const auto count=number(in,4);if(count>1000000)throw std::runtime_error("invalid PCM batch length");
            for(uint64_t i=0;i<count;++i) {
                const int32_t left=static_cast<int32_t>(number(in,4)),right=static_cast<int32_t>(number(in,4));
                const auto actual=pcm.tick();
                if(actual.left!=left||actual.right!=right) {
                    std::cerr<<"PCM sample "<<samples<<" actual "<<actual.left<<','<<actual.right<<" expected "<<left<<','<<right<<'\n';
                    throw std::runtime_error("PCM sample differs");
                }
                ++samples;if(left||right)++nonzero;
                if(left>32767||left< -32768||right>32767||right< -32768)++unclamped;
            }
            const auto expected=state(in);
            if(pcm.state().ram!=expected.ram||pcm.state().low!=expected.low)throw std::runtime_error("PCM register/fraction write-back differs");
            ++batches;
        } else throw std::runtime_error("unknown PCM event");
    }
    const auto& c=pcm.coverage;
    if(!batches||!reads||!writes||!nonzero||!c.fraction_carries)throw std::runtime_error("incomplete active PCM coverage");
    if(argc>2&&std::string(argv[2])=="--require-directed"&&
       (!c.loops||!c.stops||!c.address_wraps||!c.end_wraps||!c.high_volumes||!c.zero_deltas||!c.ignored_controls||
        !c.positive_samples||!c.negative_samples||!c.disables_retaining_fraction||c.channels!=0xffff||c.banks!=0xff||!unclamped))
        throw std::runtime_error("incomplete directed PCM coverage");
    std::cout<<"Continuous PCM vs arcade: "<<samples<<'/'<<samples<<" samples identical, "<<batches<<'/'<<batches
             <<" write-back states identical, 0 different, 0 skipped; "<<writes<<" writes, "<<reads<<" reads, "<<nonzero<<" nonzero samples, "<<unclamped<<" unclamped sums\n";
    std::cout<<"PCM coverage: loops="<<c.loops<<" stops="<<c.stops<<" fraction-carries="<<c.fraction_carries
             <<" address-wraps="<<c.address_wraps<<" end-wraps="<<c.end_wraps<<" high-volume-masks="<<c.high_volumes
             <<" channels="<<std::hex<<c.channels<<" banks="<<unsigned(c.banks)<<std::dec<<"; ROM="<<(rom_kind?"synthetic":"arcade")<<", batch deadlines and CPU clocks supplied\n";
    std::cout<<"PCM directed paths: zero-deltas="<<c.zero_deltas<<" ignored-controls="<<c.ignored_controls
             <<" positive-samples="<<c.positive_samples<<" negative-samples="<<c.negative_samples
             <<" disables-retaining-fraction="<<c.disables_retaining_fraction<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
