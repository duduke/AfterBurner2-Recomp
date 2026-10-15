#pragma once
#include "hwvideo/sprite_scene.hpp"


namespace Presentation {
// Optional presentation correction, not a replacement for the native raster.
// $10FBC selects horizon art from main $11000; $10ED8 projects the fifteen
// segments with the signed sine/cosine table at $33E98. Read asset/LOD records
// at runtime, just as the passive sprite scene does, rather than guessing colors.
class GroundHorizon {
    struct Range {unsigned bank,begin,end,pitch;bool operator==(const Range&)const=default;};
    std::vector<Range> ranges;
    std::span<const uint8_t> main;
public:
    GroundHorizon(std::span<const uint8_t> m,std::span<const uint8_t> sub):main(m) {
        const auto word=[&](uint32_t a){if(a+1>=sub.size())throw std::runtime_error("Horizon frame outside ROM");return unsigned(sub[a])<<8|sub[a+1];};
        if(main.size()<0x33f98)throw std::runtime_error("Horizon table outside ROM");
        for(unsigned angle=0;angle<64;++angle){uint32_t frame=0;for(unsigned b=0;b<4;++b)frame=frame<<8|main[0x11000+angle*4+b];
            for(unsigned lod=0;lod<64;++lod){const auto a=(frame&0xffffff)+int16_t(word(0x9e4+lod*8+4));
                const unsigned rows=word(a+2)+1,bank=(word(a+4)>>9)&7,start=word(a+6),pitch=word(a+8);
                if(!pitch || rows>4096 || start+pitch*rows>65536)throw std::runtime_error("Invalid horizon asset extent");
                const Range range{bank,start,start+pitch*rows,pitch};
                if(std::find(ranges.begin(),ranges.end(),range)==ranges.end())ranges.push_back(range);
            }
        }
    }
    std::array<int,2> normal(uint8_t angle)const {
        // Main $10F18/$10F1E use sin(angle), sin(angle-64). The latter
        // is NEGATIVE cosine: at level flight it is -128. Ground-facing
        // normal must point down at angle zero, not into the sky.
        return {int8_t(main[0x33e98+angle]),-int(int8_t(main[0x33e58+angle]))};
    }
    bool matches(const std::array<uint16_t,8>& d)const {
        // $10E6E initializes every horizon segment with palette $E8.
        if((d[6]&255)!=0xe8)return false;
        const unsigned bank=(d[0]>>9)&7;
        const int pitch=int16_t((d[2]>>1)|((d[4]&0x1000)<<3))>>8;
        for(const auto& r:ranges)if(bank==r.bank && d[1]>=r.begin && d[1]<r.end && unsigned(std::abs(pitch))==r.pitch)return true;
        return false;
    }
    struct Profile {
        std::vector<int> upper,lower;
        std::array<int,224> left,right;
        struct Ends {int sky_min=10000,sky_max=-1,ground_min=10000,ground_max=-1;
            void pixel(int coordinate,unsigned pen){if(pen==1){sky_min=std::min(sky_min,coordinate);sky_max=std::max(sky_max,coordinate);}if(pen==14){ground_min=std::min(ground_min,coordinate);ground_max=std::max(ground_max,coordinate);}}
            int direction()const {if(sky_max<0 || ground_max<0)return 0;return sky_max<ground_min?1:ground_max<sky_min?-1:0;}
        };
        std::vector<Ends> columns;std::array<Ends,224> rows{};bool measured{};
        explicit Profile(unsigned width=320):upper(width,-1),lower(width,-1),columns(width){left.fill(-1);right.fill(-1);}

        void pixel(unsigned x,unsigned y,unsigned pen=0){
            columns[x].pixel(int(y),pen);rows[y].pixel(int(x),pen);
            if(upper[x]<0 || int(y)<upper[x])upper[x]=int(y);
            lower[x]=std::max(lower[x],int(y));
            if(left[y]<0 || int(x)<left[y])left[y]=int(x);
            right[y]=std::max(right[y],int(x));
        }
        bool sky(unsigned x,unsigned y,int nx,int ny,bool fallback=false)const {
            // Require agreement wherever both silhouettes exist. This fills
            // visible sky beyond a clipped strip without cutting into terrain.
            if(measured){const int rx=rows[y].direction(),cy=columns[x].direction();nx=rx?rx:fallback?nx:0;ny=cy?cy:fallback?ny:0;}
            const int xedge=nx>0?left[y]:right[y],yedge=ny>0?upper[x]:lower[x];
            const bool known_x=nx && xedge>=0,known_y=ny && yedge>=0;
            return (known_x||known_y) && (!known_x||(nx>0?int(x)<xedge:int(x)>xedge)) &&
                (!known_y||(ny>0?int(y)<yedge:int(y)>yedge));
        }
        bool ground(unsigned x,unsigned y,int nx,int ny)const {
            if(measured){nx=rows[y].direction();ny=columns[x].direction();}
            const int xedge=nx>0?right[y]:left[y],yedge=ny>0?lower[x]:upper[x];
            const bool known_x=nx&&xedge>=0,known_y=ny&&yedge>=0;
            return (known_x||known_y)&&(!known_x||(nx>0?int(x)>xedge:int(x)<xedge))&&(!known_y||(ny>0?int(y)>yedge:int(y)<yedge));
        }
    };
    // Correct plain road-0 sky fill beyond the sky-facing edge. New side bands
    // may also inherit observed native ground fill beyond the ground-facing edge.
    static unsigned correct(std::span<uint16_t> indices,std::span<const uint8_t> priority,
                            std::span<const uint8_t> road,unsigned top,unsigned bottom,
                            int nx,int ny,const Profile& profile,int native_left=0) {
        const unsigned width=profile.upper.size();
        if(indices.size()!=width*224 || priority.size()!=indices.size() || road.size()!=0x1000 || top>bottom || bottom>=224)throw std::invalid_argument("Invalid ground correction clip");
        if(!nx && !ny)return 0;
        const bool extend_plane=native_left<0||native_left+int(width)>320;
        // Project the observed strip envelope onto its ROM-derived normal.
        // Rows/columns with no strip intersection still belong to this plane.
        int plane_min=INT32_MAX,plane_max=INT32_MIN;
        if(extend_plane&&profile.measured)for(unsigned y=0;y<224;++y)if(profile.left[y]>=0){
            for(int x:{profile.left[y],profile.right[y]}){const int v=nx*x+ny*int(y);plane_min=std::min(plane_min,v);plane_max=std::max(plane_max,v);}}
        const auto ground_side=[&](unsigned x,unsigned y){return profile.ground(x,y,nx,ny)||(plane_max!=INT32_MIN&&nx*int(x)+ny*int(y)>plane_max);};
        unsigned changed=0;
        for(unsigned y=top;y<=bottom;++y){
            const auto word=[&](unsigned i){return unsigned(road[2*i])<<8|road[2*i+1];};
            const auto data=word(y);if(data&0xa00)continue;
            const uint16_t sky=0x1720^((word(0x600+y)>>8)&15);
            // Extend the wide ground plane using an observed native-row index.
            // Include native edge holes exposed by translucent art; the
            // original 320-wide presentation keeps its existing policy.
            const int first=std::max(0,-native_left),last=std::min(int(width),320-native_left);
            int left_ground=-1,right_ground=-1;
            if(extend_plane)for(int x=first;x<last;++x){const unsigned i=y*width+x;
                if(!priority[i]&&indices[i]>=0x1700&&indices[i]<0x1708&&ground_side(x,y)){if(left_ground<0)left_ground=indices[i];right_ground=indices[i];}}
            for(unsigned x=0;x<width;++x){const auto i=y*width+x;const int original=int(x)+native_left;
                // A clipped strip can lose one marker beyond the screen. Use
                // its silhouette/ROM normal across the enhanced wide plane,
                // including central holes exposed during inversion.
                if(!priority[i] && indices[i]>=0x1700 && indices[i]<0x1708 && (profile.sky(x,y,nx,ny,extend_plane)||(extend_plane&&plane_min!=INT32_MAX&&nx*int(x)+ny*int(y)<plane_min))){indices[i]=sky;++changed;}
                else if(extend_plane&&!priority[i]&&indices[i]>=0x1720&&indices[i]<0x1730&&ground_side(x,y)){
                    const int ground=original<0?left_ground:right_ground;if(ground>=0){indices[i]=ground;++changed;}}
            }
        }
        return changed;
    }
    unsigned apply(std::span<uint16_t> indices,std::span<const uint8_t> priority,
                   std::span<const uint8_t> road,uint8_t control,std::span<const uint8_t> bank,
                   std::span<const uint8_t> sprites,unsigned top,unsigned bottom,uint8_t angle,
                   const EffectAssets& assets,unsigned width=320,int native_left=0,std::span<const int> anchors={})const {
        if(bank.empty()||bank.size()%16||bank.size()>0x10000)throw std::invalid_argument("Invalid horizon sprite bank");
        if(control!=4)return 0;
        // This is a copy: the hardware's descriptor/word-7 feedback is untouched.
        std::vector<uint8_t> horizon(bank.begin(),bank.end());
        unsigned selected=0;
        for(unsigned slot=0;slot<bank.size()/16;++slot){std::array<uint16_t,8>d{};for(unsigned w=0;w<8;++w)d[w]=uint16_t(bank[slot*16+2*w])<<8|bank[slot*16+2*w+1];
            if(d[0]&0x8000)break;
            if(!(d[0]&0x5000) && matches(d)){++selected;continue;}
            horizon[slot*16]|=0x50;
        }
        if(!selected)return 0;
        auto [nx,ny]=normal(angle);
        Profile profile(width);std::vector<uint8_t> pens(width*224);
        decode(horizon,sprites,0,223,assets,[&](int x,unsigned y,uint16_t raw,Effect){pens[y*width+unsigned(x-native_left)]=raw&15;},native_left,native_left+int(width)-1,anchors);
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<width;++x)if(pens[y*width+x])profile.pixel(x,y,pens[y*width+x]);
        profile.measured=true;
        return correct(indices,priority,road,top,bottom,nx,ny,profile,native_left);
    }
};
}
