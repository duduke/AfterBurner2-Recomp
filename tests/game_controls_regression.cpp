#include "engine/game_controls.hpp"
#include <iostream>
#include <limits>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() try {
    GameControls controls;
    auto input = arcade_inputs(controls);
    require(input.buttons == 0xff && input.analog == std::array<uint8_t,8>{0x80,0x80,0x80,0x80,0x80,0x10,0x10,0x10}, "neutral physical pins changed");
    controls.stick_x = -1; controls.stick_y = -1; controls.throttle = -1;
    input = arcade_inputs(controls);
    require(input.analog[0] == 0x20 && input.analog[1] == 0x40 && input.analog[2] == 0, "low physical endpoints changed");
    controls.stick_x = 1; controls.stick_y = 1; controls.throttle = 1;
    input = arcade_inputs(controls);
    require(input.analog[0] == 0xe0 && input.analog[1] == 0xc0 && input.analog[2] == 0xff, "high physical endpoints changed");
    controls.stick_x = -2; controls.stick_y = 3;
    input = arcade_inputs(controls);
    require(input.analog[0] == 0x20 && input.analog[1] == 0xc0, "out-of-range device axes must saturate");
    for (unsigned buttons = 0; buttons < 16; ++buttons) {
        controls = {}; controls.cannon = buttons & 1; controls.missile = buttons & 2;
        controls.coin = buttons & 4; controls.start = buttons & 8;
        constexpr uint8_t masks[] = {0x10, 0x20, 0x40, 0x08};
        const auto pins = arcade_inputs(controls).buttons;
        for (unsigned b = 0; b < 4; ++b) require(bool(pins & masks[b]) == !(buttons & (1u << b)), "active-low button combination changed");
        require((pins & 0x87) == 0x87, "unmapped physical pins changed");
    }
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        controls = {}; controls.stick_x = invalid;
        bool rejected = false;
        try { arcade_inputs(controls); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "invalid device axis must fail before board input");
    }
    std::cout << "Game controls passed: neutral, physical endpoints, saturation, 16 button combinations and invalid axes.\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
