#include "sound_execution_fixture.hpp"
#include "hwmath/xboard_device_deadlines.hpp"
#include <sstream>
int main(int argc,char** argv) try {
    SoundExecutionFixture sound(argc>1?argv[1]:"build/oracle/sound_execution.bin");
    const bool audio=argc>3&&std::string(argv[3])=="--native-audio";
    if(argc>3&&!audio)throw std::runtime_error("unexpected sound execution option");
    if(audio)sound.enable_audio(std::string(argv[1])+".audio");
    XBoardDeviceDeadlines timers;
    timers.callback=[&](XBoardDeviceDeadlines::Owner owner,uint64_t ns){if(audio&&owner==XBoardDeviceDeadlines::SoundUpdate)sound.audio->update(ns);};
    std::ifstream schedule(argc>2?argv[2]:"build/oracle/sound_execution.txt");
    if(!schedule)throw std::runtime_error("missing sound execution scheduler assertion");
    uint64_t group=0;
    for(std::string line;std::getline(schedule,line);){std::istringstream s(line);char kind;s>>kind;
        if(kind=='G'){uint64_t base,deadline,quantum;s>>group>>base>>deadline>>quantum;
            sound.supply_boot(group);
        }else if(kind=='T'){uint64_t attos;s>>attos;const auto ns=attos/1000000000ULL;
            sound.advance(ns);
            if(audio)timers.advance(ns);
        }else if(kind=='R'){std::string tag;uint64_t begin,target;unsigned budget,ran;s>>tag>>begin>>target>>budget>>ran;
            if(tag!=":mainpcb:soundcpu")continue;
            const auto actual=sound.run(group,begin/1000000000ULL,budget);
            if(actual!=ran){std::cerr<<"Sound grant "<<group<<" cycles="<<actual<<'/'<<ran<<" budget="<<budget<<'\n';throw std::runtime_error("native sound grant completion differs");}
        }
    }
    sound.finish(group);sound.report();
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
