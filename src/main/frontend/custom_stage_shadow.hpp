#pragma once
#include "frontend/custom_stage_guidance.hpp"
#include <array>
#include <vector>

namespace Frontend {
// Authored sunlight/projection parameters, independent of arcade shadow logic.
struct CustomStageShadow {
    StagePoint offset;double scale,opacity,softness;
    static CustomStageShadow at_height(double height,double bank=0){
        const double h=std::max(0.,height);
        return {{h*.45,h*2},55/(55+h*2)*(1-.2*std::abs(std::clamp(bank,-1.,1.))),128*std::exp(-h/35),.7+h*.055};
    }
    struct Vertex{StagePoint point,uv;};
    static std::vector<Vertex> clip(std::vector<Vertex> polygon,const std::array<StagePoint,4>& surface){
        double area=0;for(unsigned i=0;i<4;++i){const auto a=surface[i],b=surface[(i+1)%4];area+=a.x*b.y-b.x*a.y;}
        if(std::abs(area)<1.e-8)return {};
        for(unsigned i=0;i<4 && !polygon.empty();++i){const auto a=surface[i],b=surface[(i+1)%4];
            auto distance=[&](Vertex v){return ((b.x-a.x)*(v.point.y-a.y)-(b.y-a.y)*(v.point.x-a.x))*(area>0?1:-1);};
            std::vector<Vertex> out;auto previous=polygon.back();double dp=distance(previous);
            for(auto current:polygon){const double dc=distance(current);
                if((dp>=0)!=(dc>=0)){const double t=dp/(dp-dc);out.push_back({{previous.point.x+(current.point.x-previous.point.x)*t,previous.point.y+(current.point.y-previous.point.y)*t},{previous.uv.x+(current.uv.x-previous.uv.x)*t,previous.uv.y+(current.uv.y-previous.uv.y)*t}});}
                if(dc>=0)out.push_back(current);previous=current;dp=dc;}
            polygon=std::move(out);
        }
        return polygon;
    }
};
}
