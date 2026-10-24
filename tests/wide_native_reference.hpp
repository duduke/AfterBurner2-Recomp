#pragma once
#include "hwvideo/wide_scene.hpp"
#include "frontend/sprite_filter.hpp"
#include <stdexcept>
#include <string>
#include <memory>
// The legacy 320-wide reconstruction must remain pixel exact in every tested
// stage/mode. Extended visibility is allowed to add center-overlapping objects.
class WideNativeReference {
    std::unique_ptr<Presentation::WideScene> native;
    Frontend::SpriteScaler scaler;
public:
    explicit WideNativeReference(const Roms& rom):native(std::make_unique<Presentation::WideScene>(rom,320)){}
    template<class Pixels>void check(std::span<const Presentation::SceneClip> clips,uint64_t frame,const Pixels& pixels){
        native->render(clips,frame,false,false);const auto view=native->view();
        if(!view.ready||pixels.size()!=320*224)throw std::runtime_error("Native wide reconstruction missing rows");
        const auto output=scaler.compose(view.background,view.sprites,320,224,Frontend::SpriteFilter::none,view.protection);
        for(unsigned i=0;i<output.size();++i){const auto p=pixels[i];const auto expected=0xff000000u|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b;
            if(output[i]!=expected)throw std::runtime_error("Native wide reconstruction differs at frame "+std::to_string(frame)+" x="+std::to_string(i%320)+" y="+std::to_string(i/320)+" expected="+std::to_string(expected)+" actual="+std::to_string(output[i]));}
    }
};
