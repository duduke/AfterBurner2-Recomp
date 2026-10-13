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
    void restart(){state.reset();state=std::make_unique<State>(rom);if(capture_music_)state->enable_music(rom);}
    void tick(Host::Input input){
        auto& h=state->host;input.ns=h.time();h.input(input);
        const auto end=h.frames+1;
        while(h.frames<end){h.step();h.deliver();}
    }
    const std::vector<RGB888>& pixels()const{return state->host.pixels;}
    std::vector<int16_t> take_audio(){auto out=std::move(state->audio);state->audio.clear();return out;}
    struct AudioPresentation {
        uint64_t first{};std::vector<XBoardSpeaker::Stereo> music,effects;std::vector<MusicRouting::Event> events;
    };
    void capture_music(bool enabled) {
        capture_music_=enabled;
        if(enabled && !state->music_enabled) {
            if(state->host.frames)throw std::runtime_error("music capture must begin before the first board frame");
            state->enable_music(rom);
        }
        if(!enabled && state->music_enabled){state->sound.disable_music_presentation();state->music.clear();state->music_enabled=false;state->music_samples=0;}
    }
    std::vector<AudioPresentation> take_music(){auto out=std::move(state->music);state->music.clear();return out;}
    Host& board(){return state->host;}
    const Host& board()const{return state->host;}
    HWTiles::FrameRegisters video_registers()const{return state->tiles.frame_registers();}
private:
    struct State {
        HWPalette palette{8192};HWRoad road{0x1700,0x1720,0x1780,-166};HWSprites sprites;HWTiles tiles;
        std::vector<AudioPresentation> music;bool music_enabled{};uint64_t music_samples{};
        void enable_music(Roms& rom) {
            sound.enable_music_presentation({rom.soundcpu.rom,rom.soundcpu.length});music_enabled=true;
            sound.presentation_output=[&](uint64_t,const auto& m,const auto& f,const auto& events){music.push_back({music_samples,m,f,events});music_samples+=m.size();};
        }
        XBoardSoundBoard sound;std::vector<int16_t> audio;Host host;
        explicit State(Roms& rom):sound({rom.soundcpu.rom,rom.soundcpu.length},{rom.pcm.rom,rom.pcm.length}),host(rom,palette,road,sprites,tiles,sound){
            road.init(rom.road.rom,rom.road.length);sprites.init(rom.sprites.rom,rom.sprites.length);tiles.init_gfx(rom.tiles.rom,rom.tiles.length);
            sound.output=[&](uint64_t,const auto& out){for(auto v:out)for(auto ch:v)audio.push_back(XBoardSpeaker::pcm(ch));};
        }
    };
    Roms& rom;bool capture_music_{};std::unique_ptr<State> state;
};
