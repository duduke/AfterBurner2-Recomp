#pragma once

#include <SDL3/SDL.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace Frontend {
struct RomPickerReply { std::string path, error; bool cancelled{}; };
struct RomPickerState {
    std::mutex mutex;
    std::optional<RomPickerReply> reply;
};
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
            if (state->reply) return std::move(*state->reply);
        }
        SDL_Delay(10);
    }
}
}
