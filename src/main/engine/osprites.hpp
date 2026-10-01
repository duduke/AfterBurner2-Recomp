#pragma once
#include <array>
#include <cstdint>
#include "engine/oentry.hpp"
#include "engine/omainsprites.hpp"
// Temporary native-object adapter, not an arcade object port. It emits real
// ROM frame pointers and request records through $00B628; it never sorts or
// writes hardware descriptors. Remove with OLevelObjs in Phase 3d.
class OSprites {
public:
    void bind(MainBus&,const uint8_t* sub_rom,uint32_t length);
    void begin_frame();
    void append_sprite(const OEntry&,uint16_t bank,uint16_t offset,uint16_t pitch,
                       uint16_t zoom_x,uint16_t zoom_y,uint16_t height,uint8_t palette,uint8_t priority);
    [[nodiscard]] size_t get_active_count() const { return count; }
    [[nodiscard]] size_t unsupported_frames() const { return unsupported; }
private:
    MainBus* bus{};
    const uint8_t* rom{}; uint32_t length{};
    size_t count{},unsupported{};
    std::array<int16_t,192> previous_x{},previous_y{};
    OMainSprites main;
};
