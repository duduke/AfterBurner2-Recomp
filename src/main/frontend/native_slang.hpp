#pragma once
#include <SDL3/SDL.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace Frontend {
struct ShaderParameter {
    std::string name, description;
    float value{}, minimum{}, maximum{}, step{};
};

// Optional RetroArch Slang presentation. The emulated frame is never modified.
// macOS uses SDL Metal; Windows/Linux use SDL Vulkan. Both load an optional
// librashader runtime. Other platforms retain the built-in CRT modes.
class NativeSlang {
public:
    NativeSlang(SDL_Renderer*, SDL_Window*);
    ~NativeSlang();
    NativeSlang(const NativeSlang&)=delete;
    NativeSlang& operator=(const NativeSlang&)=delete;
    bool load(const std::string& preset, const std::string& runtime_path = {});
    const std::string& error() const;
    bool available() const;
    bool begin(unsigned logical_width, uint64_t frame_number);
    bool end(uint64_t frame_number, double fps, double delta_us, bool clear_history=false);
    void draw_cached();
    bool needs_frame(uint64_t frame_number) const;
    void invalidate();
    const std::vector<ShaderParameter>& parameters() const;
    bool set_parameter(const std::string& name, float value);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
