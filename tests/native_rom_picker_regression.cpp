#include <SDL3/SDL.h>
#include <iostream>
#include <string>

namespace {
int response{};
SDL_DialogFileCallback pending_callback{};
void* pending_userdata{};
void fake_dialog(SDL_DialogFileCallback callback, void* userdata, SDL_Window*,
                 const SDL_DialogFileFilter*, int, const char*, bool) {
    const char* chosen[]{"/ROMs/日本語/aburner2.zip", nullptr};
    const char* cancelled[]{nullptr};
    if (response == 0) callback(userdata, chosen, 0);
    if (response == 1) callback(userdata, cancelled, 0);
    if (response == 2) { SDL_SetError("test dialog failure"); callback(userdata, nullptr, 0); }
    if (response == 3) {
        pending_callback = callback; pending_userdata = userdata;
        SDL_Event event{}; event.type = SDL_EVENT_QUIT; SDL_PushEvent(&event);
    }
}
}
#define SDL_ShowOpenFileDialog fake_dialog
#include "frontend/native_rom_picker.hpp"
#undef SDL_ShowOpenFileDialog

int main() {
    if (!SDL_Init(SDL_INIT_EVENTS)) return 1;
    auto reply = Frontend::pick_rom_zip();
    if (reply.path != "/ROMs/日本語/aburner2.zip" || reply.cancelled || !reply.error.empty()) return 1;
    response = 1; reply = Frontend::pick_rom_zip();
    if (!reply.cancelled || !reply.path.empty()) return 1;
    response = 2; reply = Frontend::pick_rom_zip();
    if (reply.error != "test dialog failure") return 1;
    response = 3; reply = Frontend::pick_rom_zip();
    if (!reply.cancelled) return 1;
    // Simulate a callback delivered after startup stopped owning its state.
    const char* cancelled[]{nullptr}; pending_callback(pending_userdata, cancelled, 0);
    SDL_Quit();
    std::cout << "ROM picker: selection, cancellation, error, quit and late callback passed\n";
}
