// Discovery-only native stage/input smoke. Shares opt-in stage setup with SDL.
// ROM event records are read at runtime; inputs never replace game logic.
#include "engine/native_stage_skip.hpp"
#include "engine/game_controls.hpp"
#include <iostream>
#include <array>
#include <string>
#include <string_view>
#include <set>

using Host=XBoardGame::Host;
using NativeStageSkip::peek16;
using NativeStageSkip::peek32;
static uint16_t rom16(uint32_t address) { return NativeStageSkip::rom16(roms,address); }
static void tick(XBoardGame& game,const GameControls& controls) {
    const auto pins=arcade_inputs(controls);
    Host::Input input;input.buttons=pins.buttons;input.analog=pins.analog;
    game.tick(input);game.take_audio();
}
int main(int argc,char** argv) try {
    const bool probe=argc==2 && std::string_view(argv[1])=="--probe";
    const bool single=argc==3 && std::string_view(argv[1])=="--stage";
    if(argc!=1 && !probe && !single) throw std::runtime_error("Usage: stage_input_sweep [--probe | --stage N]");
    unsigned requested=0;
    if(single) {
        size_t used=0;requested=std::stoul(argv[2],&used);
        if(used!=std::string_view(argv[2]).size()) throw std::runtime_error("Invalid stage number");
    }
    if(!roms.load_aburner2()) throw std::runtime_error("ROMs required");
    const unsigned stages=NativeStageSkip::stage_count(roms);
    if(single && (!requested || requested>stages)) throw std::runtime_error("Stage number out of range");
    std::cout<<"Ordered ROM stage slots="<<stages<<" frames_per_scenario=600 (~10.06 seconds)\n";
    const char* names[]={"full-up","full-down","bank-left","bank-right","roll-left-gesture","roll-right-gesture","vulcan","missiles"};
    for(unsigned stage=single?requested-1:0;stage<(single?requested:probe?1:stages);++stage) {
        unsigned completed=0;std::string_view phase="boot";
        std::array<unsigned,8> delivered{};
        std::array<unsigned,2> roll_frames{};
        unsigned missile_initializers=0,vulcan_allocations=0;
        std::set<uint32_t> scripts;
        try {
            XBoardGame game(roms);auto& h=game.board();
            for(unsigned f=0;f<600;++f) {
                GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;tick(game,c);
            }
            NativeStageSkip::wait_for_flight(h,[&]{tick(game,{});});
            std::cout<<"stage="<<stage+1<<" boot_mode="<<peek16(h,0x29c09c)
                     <<" boot_index="<<peek16(h,0x29c060)<<std::endl;
            uint32_t event=peek32(h,0xffff8a08),transition=0;unsigned advances=0;
            // $DE68..$DE8C consumes 8-byte event records; command 12 at
            // $DEC0 branches to $DFC4, increments stage/index and runs $13C92.
            for(unsigned n=0;n<8192;++n,event+=8) {
                if(event+8>roms.maincpu.length) break;
                if(roms.maincpu.rom[event+2]==12) {if(!transition) transition=event;++advances;}
                if(rom16(event)==0xffff) break;
            }
            if(probe) {
                std::cout<<"advance_records="<<advances<<" transition_found="<<bool(transition)<<" address="<<std::hex<<transition
                         <<" threshold="<<(transition?rom16(transition):0)<<std::dec<<'\n';
                return 0;
            }
            if(stage) {
                phase="directed-stage-entry";
                NativeStageSkip::enter(h,roms,stage+1,[&]{tick(game,{});});
            }
            h.phase=[&](const Host::Phase& p) {
                if(p.cpu!=0 || !p.entry) return;
                if(p.entry->state.pc==0x315e || p.entry->state.pc==0x30a4) {
                    // $3092/$3096 or $314C/$3150 consumed a six-byte duration/callee record.
                    // Observe its ROM program for instruction-closure analysis.
                    const auto next=peek32(h,p.entry->state.registers.a[5]+30);
                    if(next>=6 && next<=roms.maincpu.length) scripts.insert(next-6);
                }
                if(p.entry->state.pc==0x9f7c) ++missile_initializers;
                if(p.entry->state.pc==0x9ed8) ++vulcan_allocations;
            };
            for(unsigned f=0;f<600;++f) {
                const unsigned segment=f/75,local=f%75;phase=names[segment];
                GameControls c;
                if(segment==0)c.stick_y=1;
                if(segment==1)c.stick_y=-1;
                if(segment==2)c.stick_x=-1;
                if(segment==3)c.stick_x=1;
                // Rapid opposite-to-requested endpoint flicks; actual ROM roll
                // activation is observed separately rather than forced in RAM.
                if(segment==4 || segment==5) {
                    const float direction=segment==4?-1.f:1.f;
                    c.stick_x=local%30<6?-direction:direction;
                }
                if(segment==6)c.cannon=true;
                if(segment==7)c.missile=local%15<4;
                tick(game,c);++completed;++delivered[segment];
                // Shared roll angle is sampled without issuing a bus event.
                // Nonzero confirms rotation was active, not its direction or
                // a completed roll; disabled/death modes may suppress input.
                if((segment==4 || segment==5) && peek16(h,0x29c0ae))
                    ++roll_frames[segment-4];
                if(peek16(h,0x29c060)!=stage)
                    throw std::runtime_error("Stage changed during input window");
            }
            std::cout<<"RESULT stage="<<stage+1<<" status=pass frames="<<completed<<std::endl;
        } catch(const std::exception& error) {
            std::cout<<"RESULT stage="<<stage+1<<" status=stop phase="<<phase
                     <<" frames="<<completed<<" error="<<error.what()<<std::endl;
        }
        for(auto address:scripts) std::cout<<"SCRIPT stage="<<stage+1<<" address="<<std::hex<<address<<std::dec<<'\n';
        std::cout<<"COVERAGE stage="<<stage+1;
        for(unsigned i=0;i<8;++i) std::cout<<' '<<names[i]<<'='<<delivered[i];
        std::cout<<" roll_left_active_frames="<<roll_frames[0]
                 <<" roll_right_active_frames="<<roll_frames[1]
                 <<" vulcan_allocator_entries="<<vulcan_allocations
                 <<" missile_initializer_entries="<<missile_initializers<<std::endl;
    }
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
