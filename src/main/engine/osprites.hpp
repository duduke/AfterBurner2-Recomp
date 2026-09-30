#pragma once
#include <cstdint>
#include <vector>
#include "engine/oentry.hpp"
#include "hwvideo/hwsprites.hpp"

// Replaces the original dual-CPU counting-sort pipeline.
// Collects sprite records from game objects during the frame tick,
// sorts them by Z-depth, formats them into the X-Board 8-word descriptor format,
// and blits them to HWSprites RAM before triggering the draw.
class OSprites
{
public:
    struct SpriteDesc
    {
        int16_t z{0};
        uint16_t words[8]{0};
    };

    OSprites();

    // Clear the active preparation list for a new frame tick.
    void begin_frame();

    // Emits a sprite into the frame queue. Height is in source ROM rows;
    // append_raw instead accepts a hardware descriptor with destination height.
    void append_sprite(const OEntry& obj, uint16_t rom_bank, uint16_t rom_offset,
                      uint16_t pitch, uint16_t zoom_x, uint16_t zoom_y,
                      uint16_t height, uint8_t palette, uint8_t priority);

    // Alternative direct 8-word descriptor emission matching arcade append_sprite.
    void append_raw(int16_t z, const uint16_t words[8]);

    // Sort by Z distance, write into HWSprites RAM, and trigger the hardware scaler.
    void flush_to_hardware(HWSprites& hw_sprites);

    [[nodiscard]] size_t get_active_count() const { return sprite_queue.size(); }

private:
    static constexpr size_t MAX_SPRITES = 192; // Arcade limit ($C0 entries)
    std::vector<SpriteDesc> sprite_queue;
};
