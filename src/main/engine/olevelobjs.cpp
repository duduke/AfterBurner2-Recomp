#include "engine/olevelobjs.hpp"
#include <algorithm>

OLevelObjs::OLevelObjs()
{
    init();
}

void OLevelObjs::init()
{
    for (auto& obj : objects)
        obj.reset();

    player_x = 160;
    player_y = 160;
    player_roll = 0;
    player_pitch = 0;
    player_throttle = 1;
    target_roll = 0;
    target_pitch = 0;
    missile_ammo = 50;

    // Slot 0: Player F-14 Tomcat Fuselage ($A000, init at $016892)
    objects[0].set_active(true);
    objects[0].frame = 0x46;
    objects[0].x = player_x;
    objects[0].y = player_y;
    objects[0].z = -120; // Fixed near plane for player jet

    // Slot 1: Left Exhaust Flame ($A040, init at $0168C4)
    objects[1].set_active(true);
    objects[1].frame = 0x70;
    objects[1].x = player_x - 12;
    objects[1].y = player_y + 16;
    objects[1].z = -120;

    // Slot 2: Right Exhaust Flame ($A080, init at $0168FE)
    objects[2].set_active(true);
    objects[2].control |= 0x2000; // Horizontal flip
    objects[2].frame = 0x70;
    objects[2].x = player_x + 12;
    objects[2].y = player_y + 16;
    objects[2].z = -120;

    // Slot 3: Vulcan Cannon / Pod ($A0C0, init at $01693C)
    objects[3].set_active(true);
    objects[3].frame = 0x8C;
    objects[3].x = player_x;
    objects[3].y = player_y + 4;
    objects[3].z = -118;
}

void OLevelObjs::handle_input(int stick_x, int stick_y, bool fire_vulcan, bool fire_missile, int throttle)
{
    // Flight stick X maps to roll target
    target_roll = static_cast<int16_t>(stick_x * 48);

    // Flight stick Y maps to pitch target
    target_pitch = static_cast<int16_t>(stick_y * 32);

    player_throttle = static_cast<int16_t>(throttle);

    if (fire_vulcan)
    {
        // Vulcan firing state
        if (vulcan_timer == 0)
            vulcan_timer = 2; // Fire every 2 frames
    }

    (void)fire_missile;
}

void OLevelObjs::update_player(OSprites& sprites)
{
    // Damped flight model interpolation (matching arcade ASR filtering at $016792)
    player_roll += (target_roll - player_roll) / 4;
    player_pitch += (target_pitch - player_pitch) / 4;

    // Move player position based on stick input
    player_x += player_roll / 16;
    player_y += player_pitch / 16;

    player_x = std::clamp<int16_t>(player_x, 60, 260);
    player_y = std::clamp<int16_t>(player_y, 80, 190);

    // Select banking sprite frame code (0..8 bank tilt levels from $0169F0)
    int tilt = std::abs(player_roll) / 12;
    tilt = std::clamp(tilt, 0, 8);
    uint8_t frame_code = static_cast<uint8_t>(0x46 + tilt);

    // Slot 0: Fuselage
    objects[0].frame = frame_code;
    objects[0].x = player_x;
    objects[0].y = player_y;
    if (player_roll < 0)
        objects[0].control |= 0x2000; // Flip left
    else
        objects[0].control &= ~0x2000;

    // Emit fuselage sprite
    // Args: obj, rom_bank, rom_offset, pitch, zoom_x, zoom_y, height, palette, priority
    sprites.append_sprite(objects[0], 0, static_cast<uint16_t>(0x0192 + frame_code * 8), 8, 0x200, 0x200, 32, 0, 2);

    // Slot 1 & 2: Afterburner Exhaust Flames (adjusted for roll)
    objects[1].x = player_x - 12;
    objects[1].y = player_y + 14;
    objects[2].x = player_x + 12;
    objects[2].y = player_y + 14;

    uint16_t flame_zoom = (player_throttle > 1) ? 0x280 : 0x1E0;
    sprites.append_sprite(objects[1], 0, 0x0509, 4, flame_zoom, flame_zoom, 24, 1, 1);
    sprites.append_sprite(objects[2], 0, 0x0509, 4, flame_zoom, flame_zoom, 24, 1, 1);

    // Slot 3: Vulcan Cannon muzzle flash when firing
    if (vulcan_timer > 0)
    {
        vulcan_timer--;
        objects[3].x = player_x + (vulcan_timer ? -6 : 6);
        objects[3].y = player_y + 8;
        sprites.append_sprite(objects[3], 0, 0x08F0, 4, 0x180, 0x180, 16, 2, 3);
    }
}

void OLevelObjs::update_weapons(OSprites& /*sprites*/)
{
    // Weapon update logic
}

void OLevelObjs::update_enemies(OSprites& /*sprites*/)
{
    // Enemy update logic
}

void OLevelObjs::update(OSprites& sprites, ORoad& road)
{
    // 1. Update player entity and emit player sprites
    update_player(sprites);

    // 2. Feed camera roll and pitch into the road generator
    road.set_roll(player_roll);
    road.set_horizon(static_cast<int16_t>(112 + (player_pitch / 4)));
    road.set_curve(static_cast<int16_t>(-player_roll / 2));

    // 3. Update weapons and enemies
    update_weapons(sprites);
    update_enemies(sprites);
}
