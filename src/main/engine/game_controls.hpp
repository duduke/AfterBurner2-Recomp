#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>

// Device-independent controls. Remapping, deadzones and response curves belong
// in the frontend. Positive X is right, positive Y is up, throttle is -1..1.
struct GameControls {
    float stick_x{}, stick_y{}, throttle{};
    bool cannon{}, missile{}, coin{}, start{};
};

struct ArcadeInputs {
    uint8_t buttons = 0xff;
    std::array<uint8_t, 8> analog{};
};

inline ArcadeInputs arcade_inputs(const GameControls& controls) {
    auto axis = [](float value, int low, int center, int high) {
        if (!std::isfinite(value)) throw std::invalid_argument("Control axis must be finite");
        value = std::clamp(value, -1.0f, 1.0f);
        return static_cast<uint8_t>(std::lround(center + value * (value < 0 ? center - low : high - center)));
    };
    ArcadeInputs input;
    input.analog.fill(0x10);
    for (unsigned i = 0; i < 5; ++i) input.analog[i] = 0x80;
    // Physical aburner2 ADC endpoints already used by the native application.
    input.analog[0] = axis(controls.stick_x, 0x20, 0x80, 0xe0);
    input.analog[1] = axis(controls.stick_y, 0x40, 0x80, 0xc0);
    input.analog[2] = axis(controls.throttle, 0x00, 0x80, 0xff);
    if (controls.cannon) input.buttons &= ~0x10;
    if (controls.missile) input.buttons &= ~0x20;
    if (controls.coin) input.buttons &= ~0x40;
    if (controls.start) input.buttons &= ~0x08;
    return input;
}
