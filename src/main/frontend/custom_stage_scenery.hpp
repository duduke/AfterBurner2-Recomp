#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace Frontend {
// Authored coverage for custom stages, independent of native sprite slots.
// Cells stay fixed in world space as the camera moves; only offscreen cells
// are culled. Density follows the visible ground, including both wide edges.
struct CustomSceneryField {
    struct Cell{double x,z;uint32_t variation;};
    static constexpr int band_height=3; // 392px scene / original 160px crop.
    template<class Camera>
    static std::vector<Cell> visible(const Camera& camera,double near,double runway_width,bool water,double ground){
        const double dx=water?20:24,dz=water?14:32;
        const double nearest=std::max(-camera.behind+3,(camera.height+20-ground)*camera.focal/(470-camera.horizon)-camera.behind);
        const int first=int(std::floor((nearest-near)/dz));
        std::vector<Cell> out;
        for(int row=first;;++row){const double z=near+row*dz;if(z>1800)break;if(z<nearest-32)continue;
            const double extent=470*(z+camera.behind)/camera.focal+30;
            const int left=int(std::floor((camera.x-extent)/dx)),right=int(std::ceil((camera.x+extent)/dx));
            for(int col=left;col<=right;++col){uint32_t h=uint32_t(col)*0x9e3779b9u ^ uint32_t(row)*0x85ebca6bu;h^=h>>16;
                const double x=col*dx+(int(h%11)-5),zz=z+(int((h>>8)%9)-4);
                if(!water && std::abs(x)<runway_width/2+14)continue;
                out.push_back({x,zz,h});
            }
        }
        return out;
    }
};
}
