#pragma once
#include "hwvideo/ground_horizon.hpp"

// Retained pre-optimization correction. Keep its evaluation order unchanged;
// this baseline checks complete indices and changed counts, not fingerprints.
namespace TestReference {
inline unsigned ground_correction(std::span<uint16_t> indices,std::span<const uint8_t> priority,
                            std::span<const uint8_t> road,unsigned top,unsigned bottom,
                            int nx,int ny,const Presentation::GroundHorizon::Profile& profile,int native_left=0,std::span<const uint16_t> palette={},std::vector<uint8_t>* geometry=nullptr) {
        const unsigned width=profile.upper.size();
        if(indices.size()!=width*224 || priority.size()!=indices.size() || road.size()!=0x1000 || top>bottom || bottom>=224)throw std::invalid_argument("Invalid ground correction clip");
        if(!palette.empty()&&palette.size()!=8192)throw std::invalid_argument("Invalid ground palette");
        if(!nx && !ny)return 0;
        const bool extend_plane=native_left<0||native_left+int(width)>320;
        // The displayed horizon may still be in the previous quadrant while
        // the current roll has crossed an axis. Its ordered raster markers
        // own the fallback direction for clipped rows/columns.
        if(profile.measured&&extend_plane){
            int dx=0,dy=0;
            for(const auto& row:profile.rows)dx+=row.direction();
            for(const auto& column:profile.columns)dy+=column.direction();
            if(dx)nx=dx>0?std::max(1,std::abs(nx)):-std::max(1,std::abs(nx));
            if(dy)ny=dy>0?std::max(1,std::abs(ny)):-std::max(1,std::abs(ny));
        }
        // Project the observed strip envelope onto the fallback normal.
        // Rows/columns with no strip intersection still belong to this plane.
        int plane_min=INT32_MAX,plane_max=INT32_MIN;
        if(extend_plane&&profile.measured)for(unsigned y=0;y<224;++y)if(profile.left[y]>=0){
            for(int x:{profile.left[y],profile.right[y]}){const int v=nx*x+ny*int(y);plane_min=std::min(plane_min,v);plane_max=std::max(plane_max,v);}}
        const auto observed_sky=[&](unsigned x,unsigned y){return profile.sky(x,y,nx,ny,extend_plane);};
        const auto classify_sky=[&](unsigned x,unsigned y){return observed_sky(x,y)||(!profile.ground(x,y,nx,ny)&&extend_plane&&plane_min!=INT32_MAX&&nx*int(x)+ny*int(y)<plane_min);};
        const auto classify_ground=[&](unsigned x,unsigned y){return profile.ground(x,y,nx,ny)||(!observed_sky(x,y)&&plane_max!=INT32_MIN&&nx*int(x)+ny*int(y)>plane_max);};
        // Classification depends only on the horizon raster, viewport and ROM
        // normal. Palette, road fills and priority remain live clip inputs.
        const auto sky_side=[&](unsigned x,unsigned y){
            if(!geometry)return classify_sky(x,y);
            auto& bits=(*geometry)[y*width+x];
            if(!(bits&4))bits|=4|(classify_sky(x,y)?1:0);
            return bool(bits&1);
        };
        const auto ground_side=[&](unsigned x,unsigned y){
            if(!geometry)return classify_ground(x,y);
            auto& bits=(*geometry)[y*width+x];
            if(!(bits&8))bits|=8|(classify_ground(x,y)?2:0);
            return bool(bits&2);
        };
        const auto word=[&](unsigned i){return unsigned(road[2*i])<<8|road[2*i+1];};
        // A flat-color field can lose every native ground pixel on a row as
        // the road and horizon rotate at different frontiers. Borrow only an
        // observed uniform ground pen with the identical road color control,
        // within this palette/priority clip. Mixed fields retain row sampling.
        struct Fill {unsigned color;int pen;};std::vector<Fill> fields;
        if(extend_plane)for(unsigned y=top;y<bottom+1;++y){
            if(word(y)&0xa00)continue;
            const unsigned color=word(0x600+y);
            int sample=-1;bool mixed=false;
            for(int x=std::max(0,-native_left);x<std::min(int(width),320-native_left);++x){
                const auto i=y*width+x;
                if(priority[i]||indices[i]<0x1700||indices[i]>=0x1708)continue;
                if(sample<0)sample=indices[i];
                else if(sample!=indices[i]&&(palette.empty()||palette[sample]!=palette[indices[i]]))mixed=true;
            }
            if(sample<0)continue;
            auto found=std::find_if(fields.begin(),fields.end(),[&](const Fill& f){return f.color==color;});
            if(found==fields.end())fields.push_back({color,mixed?-1:sample});
            else if(mixed||found->pen<0||(found->pen!=sample&&(palette.empty()||palette[found->pen]!=palette[sample])))found->pen=-1;
        }
        unsigned changed=0;
        for(unsigned y=top;y<=bottom;++y){
            const auto data=word(y);if(data&0xa00)continue;
            const uint16_t sky=0x1720^((word(0x600+y)>>8)&15);
            // Extend the wide ground plane using an observed native-row index.
            // Include native edge holes exposed by translucent art; the
            // original 320-wide presentation keeps its existing policy.
            const int first=std::max(0,-native_left),last=std::min(int(width),320-native_left);
            int left_ground=-1,right_ground=-1;
            if(extend_plane){
                const auto donor=[&](int x){const unsigned i=y*width+x;
                    return !priority[i]&&indices[i]>=0x1700&&indices[i]<0x1708&&ground_side(x,y);};
                for(int x=first;x<last;++x)if(donor(x)){left_ground=indices[y*width+x];break;}
                for(int x=last-1;x>=first;--x)if(donor(x)){right_ground=indices[y*width+x];break;}
            }
            if(extend_plane&&(left_ground<0||right_ground<0)){
                const auto fill=std::find_if(fields.begin(),fields.end(),[&](const Fill& f){return f.color==word(0x600+y);});
                if(fill!=fields.end()&&fill->pen>=0){if(left_ground<0)left_ground=fill->pen;if(right_ground<0)right_ground=fill->pen;}
            }
            for(unsigned x=0;x<width;++x){const auto i=y*width+x;const int original=int(x)+native_left;
                // A clipped strip can lose one marker beyond the screen. Use
                // its silhouette/ROM normal across the enhanced wide plane,
                // including central holes exposed during inversion.
                if(!priority[i] && indices[i]>=0x1700 && indices[i]<0x1708 && sky_side(x,y)){indices[i]=sky;++changed;}
                else if(extend_plane&&!priority[i]&&indices[i]>=0x1720&&indices[i]<0x1730&&ground_side(x,y)){
                    const int ground=original<0?left_ground:right_ground;if(ground>=0){indices[i]=ground;++changed;}}
            }
        }
        return changed;
    }
}
