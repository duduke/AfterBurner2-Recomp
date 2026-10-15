#pragma once
#include "hwvideo/hwtiles.hpp"
#include <array>
#include <span>
#include <vector>
#include <cstdint>
#include <stdexcept>

namespace Presentation {
struct WideDescriptor {std::array<uint8_t,16> bytes{};int x=INT32_MIN;unsigned depth{};bool recovered{};unsigned kind{};};
// Immutable inputs at a native partial-render frontier, before sprite feedback.
// Runtime ROM assets stay external; no CPU/bus reads are performed here.
struct SceneClip {
    uint64_t ns{};unsigned top{},bottom{};bool display{};
    std::array<uint16_t,0x8000> tiles{};
    std::array<uint16_t,0x800> text{};
    std::array<uint16_t,8192> palette{};
    std::array<uint8_t,0x1000> road{},sprites{};
    std::vector<WideDescriptor> extended_sprites;
    HWTiles::FrameRegisters registers{};
    uint8_t road_control{};uint16_t mode{},throttle{},roll{};
    template<size_t N> static void words(std::array<uint16_t,N>& out,std::span<const uint8_t> bytes){
        if(bytes.size()!=N*2)throw std::invalid_argument("Invalid scene snapshot region");
        for(size_t i=0;i<N;++i)out[i]=uint16_t(bytes[2*i]<<8|bytes[2*i+1]);
    }
};
}
