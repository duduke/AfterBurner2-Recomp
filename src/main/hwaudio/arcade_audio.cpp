#include "hwaudio/arcade_audio.hpp"
#include "hwaudio/xboard_audio_runtime.hpp"

struct ArcadeAudio::State {
    XBoardAudioRuntime board;
    explicit State(Roms& roms):board({roms.soundcpu.rom,roms.soundcpu.length},{roms.pcm.rom,roms.pcm.length}){}
};
ArcadeAudio::ArcadeAudio(Roms& roms):state(std::make_unique<State>(roms)){}
ArcadeAudio::~ArcadeAudio()=default;
void ArcadeAudio::command(uint8_t value){state->board.command(value);}
std::vector<int16_t> ArcadeAudio::render(unsigned frames){return state->board.render(frames);}
