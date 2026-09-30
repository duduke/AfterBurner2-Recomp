#pragma once
#include <cstdint>
#include <array>
#include "engine/oentry.hpp"
#include "engine/osprites.hpp"
#include "engine/oroad.hpp"

// Replaces the arcade object/task dispatch system ($014594 - $0145B6).
// Manages the 254 active game object slots ($9FC0 - $DF40),
// the player F-14 Tomcat flight dynamics, weapons, and enemy entities.
class OLevelObjs
{
public:
    static constexpr size_t NUM_OBJECTS = 254;

    OLevelObjs();

    // Spawns initial game objects (player jet compound slots 0..3)
    void init();

    // Input handlers from platform layer (joystick / keyboard)
    void handle_input(int stick_x, int stick_y, bool fire_vulcan, bool fire_missile, int throttle);

    // Per-frame game logic tick
    void update(OSprites& sprites, ORoad& road);

    [[nodiscard]] const OEntry& get_player() const { return objects[0]; }
    [[nodiscard]] int16_t get_roll() const { return player_roll; }
    [[nodiscard]] int16_t get_pitch() const { return player_pitch; }

private:
    std::array<OEntry, NUM_OBJECTS> objects;

    // Player flight model state
    int16_t player_x{160};       // Screen center
    int16_t player_y{160};       // Flight altitude
    int16_t player_roll{0};      // Bank angle
    int16_t player_pitch{0};     // Pitch angle
    int16_t player_throttle{1};  // Throttle level
    int16_t target_roll{0};
    int16_t target_pitch{0};

    uint16_t missile_ammo{50};
    uint8_t missile_cooldown{0};
    uint8_t missile_fire_side{0};
    uint8_t vulcan_timer{0};
    uint8_t vulcan_fire_side{0};
    uint32_t frame_counter{0};

    void update_player(OSprites& sprites);
    void update_weapons(OSprites& sprites);
    void update_enemies(OSprites& sprites);
    void spawn_explosion(int16_t x, int16_t y, int16_t z);
};
