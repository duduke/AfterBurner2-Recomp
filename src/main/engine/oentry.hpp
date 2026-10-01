#pragma once
#include <cstdint>
#include <cstring>

// Native reconstruction's object state. Its C++ layout is NOT the arcade
// record format; OSprites serializes requests explicitly until 3d replaces it.
struct OEntry
{
    // Common fields decoded from the 64-byte record:
    // Offset +0: Control word
    //   Bit 15: Inactive flag (1 = inactive / free, 0 = active)
    //   Bit 13: Horizontal flip enable
    //   Bits 0-9: Screen Y position
    uint16_t control{0x8000};

    // Offset +2: Palette / bank / priority attribute
    uint8_t attr{0};

    // Offset +3: Current sprite frame code (e.g. 0x46, 0x70, 0x8C)
    uint8_t frame{0};

    // Offset +4: Current screen position
    int16_t x{0};
    int16_t y{0};

    // Offset +8: Previous screen position (for interpolation)
    int16_t old_x{0};
    int16_t old_y{0};

    // Offset +12: Animation / trajectory script pointer
    uint32_t script_ptr{0};

    // Offset +16: Update function pointer / object handler ID
    uint32_t func_ptr{0};

    // Offset +20: Extra sprite descriptor attribute byte
    uint8_t sprite_attr{0};
    uint8_t pad0{0};

    // Offset +22: Extra state / timer
    uint16_t timer{0};

    // Offset +24: Z-depth distance (used for depth sorting) or bank roll
    int16_t z{0};

    // Offset +26: Velocities
    int16_t vx{0};
    int16_t vy{0};

    // Offset +30: 3D world coordinates
    int16_t world_x{0};
    int16_t world_y{0};
    int16_t world_z{0};

    // Offset +36: Bank roll angle
    int16_t roll{0};

    // Offset +38..+63: Extended scratch / attachment offsets
    uint8_t extra[26]{0};

    [[nodiscard]] bool is_active() const
    {
        return (control & 0x8000) == 0;
    }

    void set_active(bool active)
    {
        if (active)
            control &= ~0x8000;
        else
            control |= 0x8000;
    }

    void reset()
    {
        control = 0x8000;
        attr = 0;
        frame = 0;
        x = y = old_x = old_y = 0;
        script_ptr = 0;
        func_ptr = 0;
        sprite_attr = 0;
        pad0 = 0;
        timer = 0;
        z = vx = vy = 0;
        world_x = world_y = world_z = 0;
        roll = 0;
        std::memset(extra, 0, sizeof(extra));
    }
};
