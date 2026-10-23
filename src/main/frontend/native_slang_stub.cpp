#include "frontend/native_slang.hpp"
namespace Frontend {
struct NativeSlang::Impl {
    std::string message="RetroArch Slang shaders currently require the native macOS Metal renderer";
    std::vector<ShaderParameter> params;
};
NativeSlang::NativeSlang(SDL_Renderer*,SDL_Window*):impl(std::make_unique<Impl>()){}
NativeSlang::~NativeSlang()=default;
bool NativeSlang::load(const std::string&,const std::string&){return false;}
const std::string& NativeSlang::error()const{return impl->message;}
bool NativeSlang::available()const{return false;}
bool NativeSlang::begin(unsigned,uint64_t){return false;}
bool NativeSlang::end(uint64_t,double,double,bool){return false;}
void NativeSlang::draw_cached(){}
bool NativeSlang::needs_frame(uint64_t)const{return false;}
void NativeSlang::invalidate(){}
const std::vector<ShaderParameter>& NativeSlang::parameters()const{return impl->params;}
bool NativeSlang::set_parameter(const std::string&,float){return false;}
}
