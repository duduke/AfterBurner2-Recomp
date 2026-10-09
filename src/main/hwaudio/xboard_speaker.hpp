// SPDX-License-Identifier: BSD-3-Clause
// Copyright holders: Olivier Galibert, Aaron Giles
// Adapted from MAME resampler.cpp (LoFi), sound.cpp and segaxbd.cpp.
// See LICENSES/MAME-Audio.txt. Default MAME resampler, 48 kHz stereo,
// before optional OS audio effects; history and phase survive host chunks.
#pragma once
#include "hwaudio/xboard_pcm.hpp"
#include <algorithm>
#include <array>
#include <deque>
#include <stdexcept>
#include <vector>

class XBoardSpeaker {
public:
    using Stereo=std::array<float,2>;
    void append(unsigned device,uint64_t first,const std::vector<XBoardPcm::Sample>& samples){
        auto& s=sources.at(device);
        if(first!=s.base+s.data.size())throw std::runtime_error("noncontiguous speaker source");
        for(auto v:samples)s.data.push_back({float(double(v.left)/32768),float(double(v.right)/32768)});
    }
    uint64_t samples()const{return count;}
    std::vector<Stereo> update(uint64_t ns){
        if(!ns)return {};
        const auto target=(ns/1000000000)*48000+(ns%1000000000)*48000/1000000000+1;
        if(target<=count)return {};
        std::vector<Stereo> out(target-count);
        route(0,62500,0.15f,out);route(1,31250,0.35f,out);
        count=target;
        for(unsigned d=0;d<2;++d){auto& s=sources[d];const auto rate=d?31250:62500;
            const auto keep=int64_t(count*rate/48000)-16;
            while(!s.data.empty()&&int64_t(s.base)<keep){s.data.pop_front();++s.base;}}
        return out;
    }
    static int16_t pcm(float value){return int16_t(std::clamp(int(value*32768),-32768,32767));}
private:
    struct Source {uint64_t base{};std::deque<Stereo> data;};
    std::array<Source,2> sources;uint64_t count{};
    inline static const auto table=[](){
        std::array<std::array<float,4097>,2> t{};
        for(unsigned i=1;i!=4096;++i){float p=i/4096.0;t[0][i]=(p-p*p*p)/6;}
        for(unsigned i=1;i!=2049;++i){float p=i/4096.0;t[1][i]=p+(p*p-p*p*p)/2;}
        for(unsigned i=2049;i!=4096;++i)t[1][i]=1.0+t[0][i]+t[0][4096-i]-t[1][4096-i];
        t[1][4096]=1.0;return t;
    }();
    float get(unsigned d,int64_t index,unsigned ch)const{
        if(index<0)return 0;
        const auto& s=sources[d];
        if(uint64_t(index)<s.base||uint64_t(index)>=s.base+s.data.size())throw std::runtime_error("missing speaker source history");
        return s.data[index-s.base][ch];
    }
    void route(unsigned d,unsigned rate,float gain,std::vector<Stereo>& out){
        const unsigned divide=rate<=48000?1:1+rate/48000;
        const unsigned step=uint64_t(rate)*0x1000000/48000/divide;
        const auto ssamp=(count%48000)*rate*0x1000/48000;
        int64_t index=(ssamp>>12)+(count/48000)*rate;
        unsigned phase=ssamp&0xfff;
        if(divide>1){unsigned delta=index%divide;phase=(phase|(delta<<12))/divide;index-=delta;}
        index-=4*divide;phase<<=12;
        for(unsigned ch=0;ch<2;++ch){auto cursor=index;auto p=phase;
            auto reader=[&](){float v=0;for(unsigned i=0;i<divide;++i)v+=get(d,cursor++,ch);return v/divide;};
            float s0=reader(),s1=reader(),s2=reader(),s3=reader();
            for(auto& v:out){unsigned c=p>>12;
                v[ch]+=gain*(-s0*table[0][4096-c]+s1*table[1][4096-c]+s2*table[1][c]-s3*table[0][c]);
                p+=step;if(p&0x1000000){p&=0xffffff;s0=s1;s1=s2;s2=s3;s3=reader();}}
        }
    }
};
