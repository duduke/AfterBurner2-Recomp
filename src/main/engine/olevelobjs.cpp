#include "engine/olevelobjs.hpp"
#include <algorithm>
#include <cstdlib>

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
    missile_cooldown = 0;
    missile_fire_side = 0;
    vulcan_timer = 0;
    vulcan_fire_side = 0;
    frame_counter = 0;

    // Slot 0: Player F-14 Tomcat Fuselage ($A000, init at $016892)
    objects[0].set_active(true);
    objects[0].frame = 0x46;
    objects[0].x = player_x - 32;
    objects[0].y = player_y - 9;
    objects[0].z = -120; // Fixed near plane for player jet

    // Slot 1: Afterburner Exhaust Flame ($A040, init at $0168C4)
    objects[1].set_active(true);
    objects[1].frame = 0x70;
    objects[1].x = player_x - 32;
    objects[1].y = player_y + 10;
    objects[1].z = -120;

    // Slot 2: Spare
    objects[2].reset();

    // Slot 3: Vulcan Cannon / Muzzle flash ($A0C0, init at $01693C)
    objects[3].set_active(true);
    objects[3].frame = 0x8C;
    objects[3].x = player_x;
    objects[3].y = player_y + 2;
    objects[3].z = -118;
}

void OLevelObjs::handle_input(int stick_x, int stick_y, bool fire_vulcan, bool fire_missile, int throttle)
{
    // Flight stick X maps to roll target (-48 .. +48)
    target_roll = static_cast<int16_t>(stick_x * 48);

    // Flight stick Y maps to pitch target (-32 .. +32)
    target_pitch = static_cast<int16_t>(stick_y * 32);

    player_throttle = static_cast<int16_t>(throttle);

    if (fire_vulcan)
    {
        if (vulcan_timer == 0)
        {
            vulcan_timer = 2; // Fire muzzle flash
            // Spawn bullet projectile into slots 4..11
            for (size_t i = 4; i <= 11; ++i)
            {
                if (!objects[i].is_active())
                {
                    objects[i].set_active(true);
                    objects[i].x = player_x + ((vulcan_fire_side++) & 1 ? -10 : 10);
                    objects[i].y = player_y + 4;
                    objects[i].z = -100;
                    objects[i].vx = static_cast<int16_t>((160 - objects[i].x) / 16);
                    objects[i].vy = static_cast<int16_t>((112 - objects[i].y) / 16);
                    objects[i].timer = 25;
                    break;
                }
            }
        }
    }

    if (fire_missile && missile_ammo > 0 && missile_cooldown == 0)
    {
        missile_ammo--;
        missile_cooldown = 16;
        for (size_t i = 12; i <= 15; ++i)
        {
            if (!objects[i].is_active())
            {
                objects[i].set_active(true);
                objects[i].x = player_x + ((missile_fire_side++) & 1 ? -24 : 24);
                objects[i].y = player_y + 10;
                objects[i].z = -100;
                objects[i].vx = 0;
                objects[i].vy = -1;
                objects[i].timer = 60;
                break;
            }
        }
    }
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

    // 1. Select bank tilt level (0..4) based on roll angle
    int tilt = std::abs(player_roll) / 10;
    tilt = std::clamp(tilt, 0, 4);

    // 2. Select pitch level (0..4) based on pitch angle:
    // 0 = Nose Down (diving), 1 = Neutral, 2 = Nose Up 1, 3 = Nose Up 2, 4 = Nose Up 3 (steep climb)
    int p_level = 1;
    if (player_pitch <= -8)
        p_level = 0;
    else if (player_pitch >= 28)
        p_level = 4;
    else if (player_pitch >= 18)
        p_level = 3;
    else if (player_pitch >= 8)
        p_level = 2;

    struct JetFrame {
        uint16_t offset;
        uint8_t pitch;
        uint8_t height;
    };

    // Authentic Sega X-Board F-14 Tomcat pitch x roll matrix (Bank 0, from Sub CPU sprite table $10000..$10780)
    static constexpr JetFrame JET_MATRIX[5][5] = {
        // Tilt 0: Level flight
        {
            { 0x0000, 15, 46 }, // Pitch 0: Nose Down
            { 0x0398, 15, 46 }, // Pitch 1: Neutral
            { 0x0730, 15, 46 }, // Pitch 2: Nose Up 1
            { 0x0AB2, 15, 46 }, // Pitch 3: Nose Up 2
            { 0x0E1E, 15, 46 }  // Pitch 4: Nose Up 3
        },
        // Tilt 1: Light bank (~12°)
        {
            { 0x11C0, 15, 46 },
            { 0x155F, 15, 46 },
            { 0x1915, 15, 46 },
            { 0x1C90, 15, 46 },
            { 0x2011, 15, 46 }
        },
        // Tilt 2: Medium bank (~25°)
        {
            { 0x23B3, 14, 50 },
            { 0x2754, 14, 50 },
            { 0x2AF9, 14, 46 },
            { 0x2E69, 14, 46 },
            { 0x31C0, 15, 52 }
        },
        // Tilt 3: Steep bank (~40°)
        {
            { 0x35DC, 14, 54 },
            { 0x39CC, 14, 54 },
            { 0x3DC3, 14, 52 },
            { 0x4198, 14, 52 },
            { 0x455A, 14, 54 }
        },
        // Tilt 4: Heavy bank (~55°)
        {
            { 0x4963, 13, 59 },
            { 0x4D75, 13, 61 },
            { 0x519D, 13, 63 },
            { 0x55E5, 13, 63 },
            { 0x5A1F, 13, 57 }
        }
    };

    const auto& frame = JET_MATRIX[tilt][p_level];
    uint16_t frame_offset = frame.offset;
    uint8_t frame_pitch = frame.pitch;
    uint8_t frame_height = frame.height;

    // Slot 0: Fuselage
    // Palette 7: Authentic F-14 white/grey fuselage with glowing orange/red afterburner rings
    objects[0].frame = static_cast<uint8_t>(0x46 + tilt);
    objects[0].x = player_x - static_cast<int16_t>(frame_pitch * 4);
    objects[0].y = player_y - static_cast<int16_t>(frame_height / 2);
    if (player_roll < 0)
        objects[0].control |= 0x2000; // Flip left
    else
        objects[0].control &= ~0x2000;

    // Bank 0, palette 7, priority 2
    sprites.append_sprite(objects[0], 0, frame_offset, frame_pitch, 0x200, 0x200, frame_height, 7, 2);

    // Slot 3: Vulcan Cannon muzzle flash when firing
    if (vulcan_timer > 0)
    {
        vulcan_timer--;
        objects[3].x = player_x + (vulcan_timer ? -8 : 8) - 4;
        objects[3].y = player_y + 2;
        sprites.append_sprite(objects[3], 0, 0x0396, 1, 0x200, 0x200, 1, 0, 3);
    }
}

void OLevelObjs::update_weapons(OSprites& sprites)
{
    if (missile_cooldown > 0)
        missile_cooldown--;

    // 1. Update vulcan bullet tracers (slots 4..11)
    for (size_t i = 4; i <= 11; ++i)
    {
        auto& bullet = objects[i];
        if (!bullet.is_active())
            continue;

        bullet.z += 45;
        bullet.x += bullet.vx;
        bullet.y += bullet.vy;
        bullet.timer--;

        if (bullet.timer == 0 || bullet.z > 800)
        {
            bullet.set_active(false);
            continue;
        }

        uint16_t zoom = static_cast<uint16_t>(std::clamp(0x200 + (bullet.z + 100) * 0x200 / 900, 0x200, 0x400));
        int screen_w = (1 * 8 * 512) / zoom;
        int screen_h = (1 * 512) / zoom;
        OEntry spr = bullet;
        spr.x = bullet.x - static_cast<int16_t>(screen_w / 2);
        spr.y = bullet.y - static_cast<int16_t>(screen_h / 2);
        sprites.append_sprite(spr, 0, 0x0396, 1, zoom, zoom, 1, 0, 3);

        // Check collision against enemies (slots 16..23)
        for (size_t e = 16; e <= 23; ++e)
        {
            auto& enemy = objects[e];
            if (enemy.is_active() && std::abs(enemy.z - bullet.z) < 100)
            {
                if (std::abs(enemy.x - bullet.x) < 28 && std::abs(enemy.y - bullet.y) < 24)
                {
                    bullet.set_active(false);
                    enemy.set_active(false);
                    spawn_explosion(enemy.x, enemy.y, enemy.z);
                    break;
                }
            }
        }
    }

    // 2. Update player missiles (slots 12..15)
    for (size_t i = 12; i <= 15; ++i)
    {
        auto& m = objects[i];
        if (!m.is_active())
            continue;

        m.z += 30;
        m.x += (160 - m.x) / 18;
        m.y += (112 - m.y) / 18;
        m.timer--;

        if (m.timer == 0 || m.z > 1000)
        {
            m.set_active(false);
            continue;
        }

        uint16_t zoom = static_cast<uint16_t>(std::clamp(0x200 + (m.z + 100) * 0x280 / 1100, 0x200, 0x480));
        int screen_w = (4 * 8 * 512) / zoom;
        int screen_h = (25 * 512) / zoom;
        OEntry spr = m;
        spr.x = m.x - static_cast<int16_t>(screen_w / 2);
        spr.y = m.y - static_cast<int16_t>(screen_h / 2);
        // Bank 6, offset 0x9A49, pitch 4, height 25, Palette 6 (authentic white missile with black band/fins)
        sprites.append_sprite(spr, 6, 0x9A49, 4, zoom, zoom, 25, 6, 2);

        // Check collision against enemies
        for (size_t e = 16; e <= 23; ++e)
        {
            auto& enemy = objects[e];
            if (enemy.is_active() && std::abs(enemy.z - m.z) < 100)
            {
                if (std::abs(enemy.x - m.x) < 32 && std::abs(enemy.y - m.y) < 28)
                {
                    m.set_active(false);
                    enemy.set_active(false);
                    spawn_explosion(enemy.x, enemy.y, enemy.z);
                    break;
                }
            }
        }
    }
}

void OLevelObjs::update_enemies(OSprites& sprites)
{
    frame_counter++;

    // Periodically spawn incoming enemy fighter jets (slots 16..23)
    if ((frame_counter % 90) == 0)
    {
        for (size_t i = 16; i <= 23; ++i)
        {
            if (!objects[i].is_active())
            {
                objects[i].set_active(true);
                uint32_t seed = frame_counter / 90;
                objects[i].x = static_cast<int16_t>(100 + ((seed * 53) % 120));
                objects[i].y = static_cast<int16_t>(80 + ((seed * 37) % 36));
                objects[i].z = 1200; // Far distance
                objects[i].vx = static_cast<int16_t>(((seed & 1) ? 1 : -1) * (1 + (seed % 2)));
                objects[i].timer = 0;
                break;
            }
        }
    }

    // Authentic arcade enemy MIG banking progression (Bank 7, Palette 7)
    struct EnemyFrame {
        uint16_t offset;
        uint8_t pitch;
        uint8_t height;
    };
    static constexpr EnemyFrame ENEMY_FRAMES[4] = {
        { 0xBE71, 14, 39 }, // Level flight
        { 0xC170, 14, 45 }, // Light bank
        { 0xC4CF, 13, 56 }, // Medium bank
        { 0xC8B2, 11, 78 }  // Steep bank
    };

    // Update active enemy fighters
    for (size_t i = 16; i <= 23; ++i)
    {
        auto& enemy = objects[i];
        if (!enemy.is_active())
            continue;

        enemy.z -= static_cast<int16_t>(16 + player_throttle * 6);
        enemy.x += enemy.vx;

        if (enemy.z < -140)
        {
            enemy.set_active(false);
            continue;
        }

        // Hardware zoom: 0x200 is 1.0x (near), higher zoom is smaller (far)
        uint16_t zoom = static_cast<uint16_t>(std::clamp(0x200 + (enemy.z + 100) * 0x300 / 1300, 0x200, 0x580));

        // Select banking frame based on horizontal velocity
        int e_tilt = std::min<int>(3, std::abs(enemy.vx));
        const auto& e_frame = ENEMY_FRAMES[e_tilt];

        int screen_w = (e_frame.pitch * 8 * 512) / zoom;
        int screen_h = (e_frame.height * 512) / zoom;
        OEntry spr = enemy;
        spr.x = enemy.x - static_cast<int16_t>(screen_w / 2);
        spr.y = enemy.y - static_cast<int16_t>(screen_h / 2);
        if (enemy.vx < 0)
            spr.control |= 0x2000; // Flip left
        else
            spr.control &= ~0x2000;

        // Bank 7, Palette 7, Priority 2
        sprites.append_sprite(spr, 7, e_frame.offset, e_frame.pitch, zoom, zoom, e_frame.height, 7, 2);
    }

    // Explosion animation progression (Bank 5, Palette 7)
    struct ExpFrame {
        uint16_t offset;
        uint8_t pitch;
        uint8_t height;
    };
    static constexpr ExpFrame EXP_FRAMES[6] = {
        { 0x0BFB, 16,  98 },
        { 0x1441, 16, 123 },
        { 0x2D01, 16, 121 },
        { 0x3745, 16, 115 },
        { 0x1E8E, 19, 144 },
        { 0x5118, 15, 124 }
    };

    // Update explosions (slots 24..31)
    for (size_t i = 24; i <= 31; ++i)
    {
        auto& exp = objects[i];
        if (!exp.is_active())
            continue;

        exp.timer++;
        if (exp.timer >= 24)
        {
            exp.set_active(false);
            continue;
        }

        int frame_idx = std::clamp<int>(exp.timer / 4, 0, 5);
        const auto& exp_f = EXP_FRAMES[frame_idx];

        int base_zoom = std::clamp(0x200 + (exp.z + 100) * 0x280 / 1100, 0x200, 0x500);
        uint16_t zoom = static_cast<uint16_t>(std::max(0x180, base_zoom - static_cast<int>(exp.timer * 6)));

        int screen_w = (exp_f.pitch * 8 * 512) / zoom;
        int screen_h = (exp_f.height * 512) / zoom;
        OEntry spr = exp;
        spr.x = exp.x - static_cast<int16_t>(screen_w / 2);
        spr.y = exp.y - static_cast<int16_t>(screen_h / 2);
        // Bank 5, Palette 7, Priority 3
        sprites.append_sprite(spr, 5, exp_f.offset, exp_f.pitch, zoom, zoom, exp_f.height, 7, 3);
    }
}

void OLevelObjs::spawn_explosion(int16_t x, int16_t y, int16_t z)
{
    for (size_t i = 24; i <= 31; ++i)
    {
        if (!objects[i].is_active())
        {
            objects[i].set_active(true);
            objects[i].x = x;
            objects[i].y = y;
            objects[i].z = z;
            objects[i].timer = 0;
            break;
        }
    }
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
