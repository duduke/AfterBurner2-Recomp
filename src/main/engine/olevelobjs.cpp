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

    // Slots 32..47: Ground Palm Tree Groves & Island Scenery
    for (size_t i = 32; i <= 47; ++i)
    {
        objects[i].set_active(true);
        int idx = static_cast<int>(i - 32);
        objects[i].world_z = static_cast<int16_t>(200 + idx * 80);
        int side = (idx & 1) ? 1 : -1;
        objects[i].world_x = static_cast<int16_t>(side * (55 + ((idx * 29) % 75)));
        objects[i].z = objects[i].world_z;
    }
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

    // Periodically spawn incoming enemy aircraft (slots 16..23)
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
                objects[i].attr = static_cast<uint8_t>(seed % 3); // 0=MIG, 1=Delta Bomber, 2=Helicopter
                objects[i].timer = 0;
                break;
            }
        }
    }

    // Authentic arcade enemy MIG frames (Bank 7)
    struct AircraftFrame {
        uint16_t offset;
        uint8_t pitch;
        uint8_t height;
    };
    static constexpr AircraftFrame MIG_FRAMES[4] = {
        { 0xBE71, 14, 39 }, // Level flight
        { 0xC170, 14, 45 }, // Light bank
        { 0xC4CF, 13, 56 }, // Medium bank
        { 0xC8B2, 11, 78 }  // Steep bank
    };

    // Stealth Delta Bomber frames (Bank 7)
    static constexpr AircraftFrame BOMBER_FRAMES[3] = {
        { 0xA1A7, 16, 29 }, // Level flight
        { 0xA422, 15, 46 }, // Medium bank
        { 0xA7D9, 14, 64 }  // Steep bank
    };

    // Attack Helicopter frames (Bank 4)
    static constexpr AircraftFrame HELI_FRAMES[4] = {
        { 0x0000, 9, 31 }, // Level
        { 0x0491, 9, 37 }, // Light bank
        { 0x0668, 8, 41 }, // Medium bank
        { 0x0836, 8, 41 }  // Steep bank
    };

    // Update active enemy aircraft
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

        uint16_t zoom = static_cast<uint16_t>(std::clamp(0x200 + (enemy.z + 100) * 0x300 / 1300, 0x200, 0x580));
        OEntry spr = enemy;
        if (enemy.vx < 0)
            spr.control |= 0x2000; // Flip left
        else
            spr.control &= ~0x2000;

        uint8_t enemy_type = enemy.attr & 3;
        if (enemy_type == 0)
        {
            // MIG Interceptor: Bank 7, Palette 7 (interceptor grey) or 15 (crimson ace)
            int e_tilt = std::min<int>(3, std::abs(enemy.vx));
            const auto& e_frame = MIG_FRAMES[e_tilt];
            int screen_w = (e_frame.pitch * 8 * 512) / zoom;
            int screen_h = (e_frame.height * 512) / zoom;
            spr.x = enemy.x - static_cast<int16_t>(screen_w / 2);
            spr.y = enemy.y - static_cast<int16_t>(screen_h / 2);
            uint8_t pal = (enemy.attr & 4) ? 15 : 7;
            sprites.append_sprite(spr, 7, e_frame.offset, e_frame.pitch, zoom, zoom, e_frame.height, pal, 2);
        }
        else if (enemy_type == 1)
        {
            // Heavy Stealth Delta Bomber: Bank 7, Palette 4 (desert camo)
            int b_tilt = std::min<int>(2, std::abs(enemy.vx));
            const auto& b_frame = BOMBER_FRAMES[b_tilt];
            int screen_w = (b_frame.pitch * 8 * 512) / zoom;
            int screen_h = (b_frame.height * 512) / zoom;
            spr.x = enemy.x - static_cast<int16_t>(screen_w / 2);
            spr.y = enemy.y - static_cast<int16_t>(screen_h / 2);
            sprites.append_sprite(spr, 7, b_frame.offset, b_frame.pitch, zoom, zoom, b_frame.height, 4, 2);
        }
        else
        {
            // Attack Helicopter: Bank 4, Palette 0 (military olive) with spinning rotor overlay
            int h_tilt = std::min<int>(3, std::abs(enemy.vx));
            const auto& h_frame = HELI_FRAMES[h_tilt];
            int screen_w = (h_frame.pitch * 8 * 512) / zoom;
            int screen_h = (h_frame.height * 512) / zoom;
            spr.x = enemy.x - static_cast<int16_t>(screen_w / 2);
            spr.y = enemy.y - static_cast<int16_t>(screen_h / 2);
            sprites.append_sprite(spr, 4, h_frame.offset, h_frame.pitch, zoom, zoom, h_frame.height, 0, 2);

            // Spinning rotor blade overlay
            uint16_t rotor_offset = (frame_counter & 2) ? 0x0191 : 0x02EC;
            sprites.append_sprite(spr, 4, rotor_offset, 9, zoom, zoom, 30, 0, 2);
        }
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

void OLevelObjs::update_scenery(OSprites& sprites, const ORoad& road)
{
    (void)road;
    // Ground palm trees and tropical scenery clusters (slots 32..47)
    // Dynamic perspective projection with banking tilt responses matching arcade hardware
    int horizon = 112 + (player_pitch / 4);
    int curve = -player_roll / 2;

    struct TreeGrove {
        uint16_t offset;
        uint8_t pitch;
        uint8_t height;
    };
    static constexpr TreeGrove GROVE_FRAMES[4] = {
        { 0x1ACC, 18, 109 }, // Tilt 0: Level ground (144x109)
        { 0x2531, 17, 128 }, // Tilt 1: Light tilt (136x128)
        { 0x30DD, 14, 131 }, // Tilt 2: Medium tilt (112x131)
        { 0x3A8F, 11, 121 }  // Tilt 3: Steep tilt (88x121)
    };

    int roll_mag = std::abs(player_roll);
    int tilt_idx = 0;
    if (roll_mag >= 32) tilt_idx = 3;
    else if (roll_mag >= 20) tilt_idx = 2;
    else if (roll_mag >= 8)  tilt_idx = 1;

    const auto& grove = GROVE_FRAMES[tilt_idx];

    for (size_t i = 32; i <= 47; ++i)
    {
        auto& tree = objects[i];
        if (!tree.is_active())
            continue;

        // Ground scenery speed scales with player throttle
        tree.world_z -= static_cast<int16_t>(12 + player_throttle * 6);
        if (tree.world_z < 120)
        {
            tree.world_z += 1280;
            // Alternate horizontal span leaving open flight corridor
            int idx = static_cast<int>(i - 32);
            int side = (idx & 1) ? 1 : -1;
            tree.world_x = static_cast<int16_t>(side * (55 + ((frame_counter * 17 + idx * 31) % 85)));
        }

        tree.z = tree.world_z;

        // Perspective zoom and position
        uint16_t zoom = static_cast<uint16_t>(std::clamp(0x200 + (tree.z - 120) * 0x340 / 1280, 0x180, 0x680));
        int screen_w = (grove.pitch * 8 * 512) / zoom;
        int screen_h = (grove.height * 512) / zoom;

        // Perspective ground projection matching Stage 1/25 arcade layout:
        // Ground contact line runs from horizon (112) down to screen bottom (224)
        int ground_y = horizon + ((224 - horizon) * (1300 - std::clamp<int>(tree.z, 150, 1300))) / (1300 - 150);
        int curve_offset = (curve * (1400 - tree.z)) / 1400;
        int proj_x = 160 + (tree.world_x * 400 / tree.z) + curve_offset - (player_roll * (1400 - tree.z) / 2800);

        OEntry spr = tree;
        spr.x = static_cast<int16_t>(proj_x - screen_w / 2);
        spr.y = static_cast<int16_t>(ground_y - (screen_h * 3 / 4));

        if (player_roll < 0)
            spr.control |= 0x2000; // Flip horizontal when banking left
        else
            spr.control &= ~0x2000;

        // Palette:
        // Left side: Palette 0 (lush green Stage 1 tropical palm fronds)
        // Right side: Palette 4 (authentic Stage 25 desert sand/tan palms)
        uint8_t pal = (tree.world_x < 0) ? 0 : 4;

        // Priority 1: In front of road/horizon, behind player and combat targets
        sprites.append_sprite(spr, 2, grove.offset, grove.pitch, zoom, zoom, grove.height, pal, 1);
    }
}

void OLevelObjs::update_hud(OSprites& sprites)
{
    // Slot 48: Authentic Center Crosshair [+] (Bank 1, 0x7DF0, Palette 6, Priority 3)
    OEntry crosshair;
    crosshair.z = -200; // In front of all 3D world entities
    crosshair.x = static_cast<int16_t>(160 - 12 + (player_roll / 8));
    crosshair.y = static_cast<int16_t>(112 - 9 + (player_pitch / 8));
    sprites.append_sprite(crosshair, 1, 0x7DF0, 3, 0x200, 0x200, 18, 6, 3);

    // Slot 49: Dynamic Target Lock-On Tracker (Bank 1, 0x7E29..0x7F7F)
    // Scan active enemies to locate closest target in front of crosshair
    int best_dist = 9999;
    const OEntry* best_enemy = nullptr;

    for (size_t i = 16; i <= 23; ++i)
    {
        const auto& e = objects[i];
        if (e.is_active() && e.z > 0 && e.z < 1000)
        {
            int dx = (e.x - 160);
            int dy = (e.y - 112);
            int dist = dx * dx + dy * dy;
            if (dist < 70 * 70 && dist < best_dist)
            {
                best_dist = dist;
                best_enemy = &e;
            }
        }
    }

    if (best_enemy)
    {
        auto& lockbox = objects[49];
        lockbox.set_active(true);
        lockbox.z = -199;
        // Animated lock-on cycling 7 bracket frames
        static constexpr uint16_t LOCK_FRAMES[7] = {
            0x7E29, 0x7E62, 0x7E9B, 0x7ED4, 0x7F0D, 0x7F46, 0x7F7F
        };
        int lock_frame = (frame_counter / 3) % 7;
        uint16_t lock_offset = LOCK_FRAMES[lock_frame];

        // Center lockbox around enemy target
        lockbox.x = best_enemy->x - 12;
        lockbox.y = best_enemy->y - 9;
        sprites.append_sprite(lockbox, 1, lock_offset, 3, 0x200, 0x200, 18, 6, 3);
    }
    else
    {
        objects[49].set_active(false);
    }
}

void OLevelObjs::update_tanker(OSprites& sprites)
{
    // Slot 50: KC-10 Extender Refueling Tanker (Bank 7, Offset 0x1673, Palette 1, Priority 2)
    // Slot 51: Refueling Boom (Bank 7, Offset 0x25EB, Palette 1, Priority 2)
    // Flies in as friendly formation leader during flybys (periodic cutscene cadence)
    uint32_t cycle = frame_counter % 800;
    if (cycle >= 100 && cycle <= 400)
    {
        auto& tanker = objects[50];
        tanker.set_active(true);
        tanker.z = static_cast<int16_t>(600 - (cycle - 100)); // Cruising forward ahead of player
        tanker.x = static_cast<int16_t>(160 - (19 * 4) + (player_roll / 12));
        tanker.y = static_cast<int16_t>(60 + (player_pitch / 12));

        uint16_t zoom = static_cast<uint16_t>(std::clamp(0x200 + (tanker.z - 200) * 0x180 / 400, 0x200, 0x380));
        sprites.append_sprite(tanker, 7, 0x1673, 19, zoom, zoom, 95, 1, 2);

        // Lower refueling boom when in formation
        if (cycle >= 200 && cycle <= 360)
        {
            auto& boom = objects[51];
            boom.set_active(true);
            boom.z = tanker.z - 10;
            boom.x = tanker.x + 19 * 4 - 16;
            boom.y = tanker.y + 70;
            sprites.append_sprite(boom, 7, 0x25EB, 4, zoom, zoom, 51, 1, 2);
        }
    }
    else
    {
        objects[50].set_active(false);
        objects[51].set_active(false);
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

    // 3. Update ground obstacles, combat entities, HUD reticles, and tanker
    update_scenery(sprites, road);
    update_weapons(sprites);
    update_enemies(sprites);
    update_hud(sprites);
    update_tanker(sprites);
}

