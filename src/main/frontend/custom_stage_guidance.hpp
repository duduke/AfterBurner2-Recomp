#pragma once
#include <algorithm>
#include <cmath>

namespace Frontend {
struct StagePoint{double x{},y{};};
inline StagePoint rotate_stage_point(StagePoint p,double degrees){const double a=degrees*3.141592653589793/180;return {p.x*std::cos(a)-p.y*std::sin(a),p.x*std::sin(a)+p.y*std::cos(a)};}
inline StagePoint anchored_stage_image(StagePoint anchor,StagePoint local_root,double scale,double degrees,bool flip){if(flip)local_root.x=-local_root.x;const auto d=rotate_stage_point({local_root.x*scale,local_root.y*scale},degrees);return {anchor.x-d.x,anchor.y-d.y};}
struct CustomStageGuidance {
    static unsigned pitch_pose(double pitch){return unsigned(std::clamp(std::lround(2+std::clamp(pitch,-1.,1.)*2),0l,4l));}
    static constexpr double probe_forward=12,probe_right=1.8,probe_up=.3,capture_radius=.45;
    static double contact_distance(double x,double height,double range){return std::sqrt(x*x+height*height+(range-probe_forward)*(range-probe_forward));}
    static double closure(double range,double initial){if(range<=probe_forward)return 1;return std::clamp((initial-range)/std::max(1.,initial-probe_forward),0.,1.);}
    struct Landing{StagePoint cross,velocity;double lateral_angle,glide_angle;
        bool localizer_centered()const{return std::abs(cross.x-ils_reference.x)<=3;}
        bool glideslope_centered()const{return std::abs(cross.y-ils_reference.y)<=3;}
    };
    struct Tanker{StagePoint offset;double radius;bool locked;};
    static Tanker tanker(double x,double height,double range,double initial,bool locked,bool acquiring=false){
        locked=locked || acquiring;
        // Let the inner diamond grow almost to the outer one on approach.
        // Exact alignment and the bold lock share the physical capture predicate.
        return {locked?StagePoint{}:StagePoint{x*2,-height*2},locked?84:std::min(83.,10+74*closure(range,initial)),locked};
    }
    static constexpr StagePoint ils_reference{480,300};
    static Landing landing(double x,double height,double range,double lateral,double vertical,double speed,double slope,double horizon,double focal){
        const double ahead=std::max(45.,range),safe_speed=std::max(1.,speed);
        const double lateral_angle=std::atan2(-x,ahead);
        const double flare_slope=height<4?1.7/safe_speed:slope;
        const double glide_angle=std::atan2(height-2,ahead)-std::atan(flare_slope);
        return {{ils_reference.x+std::clamp(focal*lateral_angle,-100.,100.),ils_reference.y+std::clamp(focal*glide_angle,-90.,90.)},
                {480+std::clamp(focal*lateral/safe_speed,-190.,190.),horizon-std::clamp(focal*vertical/safe_speed,-160.,160.)},lateral_angle,glide_angle};
    }
};
}
