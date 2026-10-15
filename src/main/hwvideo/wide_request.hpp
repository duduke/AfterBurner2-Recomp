#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>

namespace Presentation {
// $B628..$B64A's verified producer format, before native capacity rejection.
inline bool native_request_rejected(uint16_t count){return int16_t(count)>=0xc0;}
inline std::array<uint8_t,16> wide_request(std::span<const uint8_t> object){
    if(object.size()<21)throw std::invalid_argument("Truncated sprite producer object");
    std::array<uint8_t,16> q;std::copy_n(object.begin(),16,q.begin());q[12]=object[20];return q;
}
}
