#pragma once
#include "frontend/custom_stage_json.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>

namespace Frontend {
// Modern scene data: dynamic vectors, RGBA8 colors, arbitrary image dimensions.
// No palette banks, sprite slots, tile codes, or hardware layer registers.
struct CustomStageElement {
    enum class Type{rectangle,image};Type type=Type::rectangle;
    std::string asset;double x{},y{},z{},width=1,height=1,rotation{};
    size_t columns=1,rows=1;std::array<uint8_t,4> color{255,255,255,255};
};
struct CustomStageLayer {
    std::string name;double order=250;bool world=false;std::vector<CustomStageElement> elements;
};
inline std::vector<CustomStageLayer> load_stage_layers(const StageJson& value){
    if(value.kind!=StageJson::Kind::array)throw std::runtime_error("layers needs a JSON array");
    std::vector<CustomStageLayer> result;
    for(const auto& node:value.array){node.keys({"name","order","space","elements"});CustomStageLayer layer;
        if(auto v=node.find("name"))layer.name=v->text();if(auto v=node.find("order"))layer.order=v->numeric();
        if(auto v=node.find("space")){const auto s=v->text();if(s!="screen" && s!="world")throw std::runtime_error("Layer space needs screen or world");layer.world=s=="world";}
        auto elements=node.find("elements");if(!elements || elements->kind!=StageJson::Kind::array)throw std::runtime_error("Layer needs an elements array");
        for(const auto& item:elements->array){item.keys({"type","asset","x","y","z","width","height","rotation","color","repeat"});CustomStageElement e;
            const auto type=item.find("type");if(!type)throw std::runtime_error("Element needs a type");const auto name=type->text();
            if(name=="image")e.type=CustomStageElement::Type::image;else if(name!="rectangle")throw std::runtime_error("Unknown custom element type");
            if(auto v=item.find("asset")){e.asset=v->text();if(e.asset.empty() || e.asset.find('\0')!=std::string::npos)throw std::runtime_error("Invalid asset path");}
            if(e.type==CustomStageElement::Type::image && e.asset.empty())throw std::runtime_error("Image needs an asset path");
            if(e.type==CustomStageElement::Type::rectangle && !e.asset.empty())throw std::runtime_error("Rectangle cannot have an image asset");
            for(auto [key,dest]:{std::pair{"x",&e.x},{"y",&e.y},{"z",&e.z},{"width",&e.width},{"height",&e.height},{"rotation",&e.rotation}})if(auto v=item.find(key))*dest=v->numeric();
            if(e.width<=0 || e.height<=0)throw std::runtime_error("Element dimensions must be positive");
            if(auto v=item.find("color")){if(v->kind!=StageJson::Kind::array || v->array.size()!=4)throw std::runtime_error("Color needs four RGBA8 channels");
                for(unsigned c=0;c<4;++c){double n=v->array[c].numeric();if(n<0 || n>255 || std::floor(n)!=n)throw std::runtime_error("Invalid RGBA8 channel");e.color[c]=uint8_t(n);}}
            if(auto v=item.find("repeat")){if(v->kind!=StageJson::Kind::array || v->array.size()!=2)throw std::runtime_error("repeat needs columns and rows");
                size_t* counts[]={&e.columns,&e.rows};for(unsigned i=0;i<2;++i){double n=v->array[i].numeric();if(n<1 || n>=double(std::numeric_limits<size_t>::max()) || std::floor(n)!=n)throw std::runtime_error("Invalid tile repetition count");*counts[i]=size_t(n);}}
            // Refuse unrepresentable extents, rather than imposing arcade caps.
            if(!std::isfinite(e.width*e.columns) || !std::isfinite(e.height*e.rows))throw std::runtime_error("Tile extent exceeds numeric range");
            layer.elements.push_back(std::move(e));
        }
        result.push_back(std::move(layer));
    }
    std::stable_sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.order<b.order;});return result;
}
}
