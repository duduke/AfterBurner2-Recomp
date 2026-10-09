#pragma once
#include "engine/xboard_host.hpp"
#include "hwaudio/xboard_sound_board.hpp"
#include <memory>

// Application adapter: physical inputs and completed screen/speaker output.
// Restart creates a fresh board; no gameplay RAM or native producer is seeded.
class XBoardGame {
public:
    using Host=XBoardHost<XBoardSoundBoard>;
    explicit XBoardGame(Roms& rom):rom(rom){restart();}
    void restart(){state.reset();state=std::make_unique<State>(rom);}
    void tick(Host::Input input){
        auto& h=state->host;input.ns=h.time();h.input(input);
        const auto end=h.frames+1;
        while(h.frames<end){h.step();h.deliver();}
    }
    const std::vector<RGB888>& pixels()const{return state->host.pixels;}
    std::vector<int16_t> take_audio(){auto out=std::move(state->audio);state->audio.clear();return out;}
    Host& board(){return state->host;}
    const Host& board()const{return state->host;}
    HWTiles::FrameRegisters video_registers()const{return state->tiles.frame_registers();}
private:
    struct State {
        HWPalette palette{8192};HWRoad road{0x1700,0x1720,0x1780,-166};HWSprites sprites;HWTiles tiles;
        XBoardSoundBoard sound;std::vector<int16_t> audio;Host host;
        explicit State(Roms& rom):sound({rom.soundcpu.rom,rom.soundcpu.length},{rom.pcm.rom,rom.pcm.length}),host(rom,palette,road,sprites,tiles,sound){
            road.init(rom.road.rom,rom.road.length);sprites.init(rom.sprites.rom,rom.sprites.length);tiles.init_gfx(rom.tiles.rom,rom.tiles.length);
            sound.output=[&](uint64_t,const auto& out){for(auto v:out)for(auto ch:v)audio.push_back(XBoardSpeaker::pcm(ch));};
        }
    };
    Roms& rom;std::unique_ptr<State> state;
};
