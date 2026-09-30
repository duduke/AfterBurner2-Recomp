#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include "roms.hpp"

// Original Z80 sound program + YM2151/SegaPCM; game logic remains native C++.
class ArcadeAudio
{
public:
    explicit ArcadeAudio(Roms& roms);
    ~ArcadeAudio();
    ArcadeAudio(const ArcadeAudio&) = delete;
    ArcadeAudio& operator=(const ArcadeAudio&) = delete;
    void command(uint8_t value);
    std::vector<int16_t> render(unsigned stereo_frames);
    static constexpr int SAMPLE_RATE = 48000;
private:
    struct State;
    std::unique_ptr<State> state;
};
