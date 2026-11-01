#pragma once

#include <SDL3/SDL.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <fstream>
#include <filesystem>

namespace Frontend {
struct RomPickerReply { std::string path, error; bool cancelled{}; };
struct RomPickerState {
    std::mutex mutex;
    std::optional<RomPickerReply> reply;
};
// Android document providers return content:// URIs, not filesystem paths.
// Import once through SDL IO into private storage; the ROM loader and its CRC
// validation remain shared with desktop. Bound untrusted provider input.
inline RomPickerReply import_rom_zip(const std::string& source, const std::string& destination) {
    auto* input=SDL_IOFromFile(source.c_str(),"rb");
    if(!input)return {{},SDL_GetError(),false};
    const auto temporary=destination+".tmp";
    std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
    char buffer[32768];size_t total=0;bool ok=bool(output);
    while(ok) {
        const auto count=SDL_ReadIO(input,buffer,sizeof(buffer));
        if(!count){ok=SDL_GetIOStatus(input)==SDL_IO_STATUS_EOF;break;}
        total+=count;
        if(total>64*1024*1024){ok=false;break;}
        output.write(buffer,count);ok=bool(output);
    }
    SDL_CloseIO(input);output.close();ok=ok && bool(output) && total>0;
    std::error_code error;
    if(ok){std::filesystem::rename(temporary,destination,error);ok=!error;}
    if(!ok){std::filesystem::remove(temporary,error);return {{},"Cannot import ROM ZIP (empty, unreadable, over 64 MiB, or storage unavailable)",false};}
    return {destination,{},false};
}
// The dialog may reply on another thread, including after SDL_QUIT. A weak
// reference keeps that callback independent of startup's lifetime.
inline void SDLCALL rom_picker_result(void* userdata, const char* const* files, int) {
    std::unique_ptr<std::weak_ptr<RomPickerState>> request(
        static_cast<std::weak_ptr<RomPickerState>*>(userdata));
    auto state = request->lock();
    if (!state) return;
    RomPickerReply reply;
    if (!files) reply.error = SDL_GetError();
    else if (!files[0]) reply.cancelled = true;
    else reply.path = files[0];
    if (!files && reply.error.empty()) reply.error = "File picker unavailable";
    std::lock_guard lock(state->mutex);
    state->reply = std::move(reply);
}
inline RomPickerReply pick_rom_zip() {
    auto state = std::make_shared<RomPickerState>();
    static const SDL_DialogFileFilter filter{"After Burner II ROM ZIP", "zip"};
    SDL_ShowOpenFileDialog(rom_picker_result, new std::weak_ptr(state),
                          nullptr, &filter, 1, nullptr, false);
    for (;;) {
        SDL_Event event;
        while (SDL_PollEvent(&event))
            if (event.type == SDL_EVENT_QUIT) return {{}, {}, true};
        {
            std::lock_guard lock(state->mutex);
            if (state->reply) {
                auto reply=std::move(*state->reply);
#ifdef SDL_PLATFORM_ANDROID
                if(!reply.path.empty()) {
                    char* path=SDL_GetPrefPath("AfterBurner2-Recomp","AfterBurner2");
                    if(!path)return {{},SDL_GetError(),false};
                    const std::string destination=std::string(path)+"aburner2.zip";SDL_free(path);
                    return import_rom_zip(reply.path,destination);
                }
#endif
                return reply;
            }
        }
        SDL_Delay(10);
    }
}
}
