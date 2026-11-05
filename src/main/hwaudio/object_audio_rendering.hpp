#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <optional>
#include <cstdint>

namespace ObjectAudio {
// Presentation policy, not emulated cabinet wiring. A bounded virtual speaker
// grid makes direction changes part of the queued PCM, rather than changing
// the position of older audio already waiting in OpenAL. Music stays dry.
inline constexpr unsigned directions=24;
inline constexpr float pi=3.14159265358979323846f;
inline std::array<float,3> direction(unsigned i){
    const float az=(i%8)*pi/4,el=(int(i/8)-1)*pi/6;
    return {std::sin(az)*std::cos(el),std::sin(el),-std::cos(az)*std::cos(el)};
}
inline std::array<float,directions> weights(float x,float y,float z){
    std::array<float,directions> out{};
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z))return out;
    if(x*x+y*y+z*z<1e-12f)z=-1;
    float az=std::atan2(x,-z)*4/pi;if(az<0)az+=8;
    const float el=std::clamp(std::atan2(y,std::hypot(x,z))*6/pi+1,0.f,2.f);
    const unsigned a=unsigned(az)%8,b=std::min(unsigned(el),1u);const float u=az-std::floor(az),v=el-b;
    // Constant power on the four nearest virtual speakers. Exact cardinal
    // bearings use one source; movement and elevation never replay a sound.
    out[b*8+a]=std::sqrt((1-u)*(1-v));out[b*8+(a+1)%8]=std::sqrt(u*(1-v));
    out[(b+1)*8+a]=std::sqrt((1-u)*v);out[(b+1)*8+(a+1)%8]=std::sqrt(u*v);
    return out;
}
// $9122/$9274/$92BA: pre-perspective world terms. Preserve the
// native 32-bit product/offset wrap before converting to presentation floats.
template<class Read> std::optional<std::array<float,3>> world_bearing(Read read,uint32_t p){
    auto longword=[&](uint32_t a){return uint32_t(read(a))<<16|read(a+2);};
    const auto base=longword(p+54)&0x3fffff;
    if(!((base>=0x3f8000&&base<=0x3ffff8)||(base>=0x280000&&base<=0x2bfff8)))return {};
    const int32_t x=int16_t(read(p+42));
    const int32_t vx=int32_t(uint32_t(int32_t(int16_t(read(0x29c092)))*x)+longword(base));
    const int32_t vy=int32_t(uint32_t(int32_t(int16_t(read(0x29c090)))*x)-longword(base+4));
    return std::array<float,3>{vx/128.f,-vy/128.f,float(int16_t(read(0xffff8d18))-int16_t(read(p+36)))};
}

}
