#pragma once
#include <SDL3/SDL.h>
#include <iostream>
namespace Frontend {
inline int load_controller_database() {
    static constexpr char database[]=
#include "controllerdb/database.inc"
    ;
    auto* io=SDL_IOFromConstMem(database,sizeof(database)-1);
    const int loaded=io?SDL_AddGamepadMappingsFromIO(io,true):-1;
    if(loaded<0)std::cerr<<"Controller database unavailable: "<<SDL_GetError()<<'\n';
    return loaded;
}
}
