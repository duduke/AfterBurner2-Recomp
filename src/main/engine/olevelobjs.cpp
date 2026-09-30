#include "engine/olevelobjs.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>

OLevelObjs::OLevelObjs() { init(); }
void OLevelObjs::init()
{
    for (auto& object : objects) object.reset();
    player_x = 160; player_y = 118; player_roll = player_pitch = 0;
    player_throttle = 1; target_roll = target_pitch = 0;
    missile_ammo = 50; missile_cooldown = missile_fire_side = 0;
    vulcan_timer = vulcan_fire_side = 0; frame_counter = travel = score = 0;
    hits = invulnerability = 0; completed = false;
    audio_commands = {0x7e, 0xd8, 0x92, 0x91};
    objects[0].set_active(true); objects[0].z = -120;
}

void OLevelObjs::handle_input(int stick_x, int stick_y, bool cannon, bool missile, int throttle)
{
    target_roll = static_cast<int16_t>(std::clamp(stick_x, -1, 1) * 48);
    target_pitch = static_cast<int16_t>(std::clamp(stick_y, -1, 1) * 32);
    player_throttle = static_cast<int16_t>(std::clamp(throttle, 0, 2));
    if (completed) return;
    if (cannon && vulcan_timer == 0)
        for (size_t i = 4; i <= 11; ++i)
            if (!objects[i].is_active())
            {
                auto& bullet = objects[i]; bullet.reset(); bullet.set_active(true);
                bullet.x = player_x + ((vulcan_fire_side++ & 1) ? -10 : 10);
                bullet.y = player_y; bullet.z = -100; bullet.timer = 25;
                vulcan_timer = 4;
                audio_commands.push_back(0x90); // Original Vulcan sound command.
                break;
            }
    if (missile && missile_ammo && missile_cooldown == 0)
        for (size_t i = 12; i <= 15; ++i)
            if (!objects[i].is_active())
            {
                auto& shot = objects[i]; shot.reset(); shot.set_active(true);
                shot.x = player_x + ((missile_fire_side++ & 1) ? -24 : 24);
                shot.y = player_y + 10; shot.z = -100; shot.timer = 60;
                --missile_ammo; missile_cooldown = 16;
                audio_commands.push_back(0x40); // Original missile launch command.
                break;
            }
}

void OLevelObjs::project(OEntry& object) const
{
    const double angle = -player_roll * 3.14159265358979323846 / 180;
    const double dx = object.x - 160.0, dy = object.y - 112.0;
    object.x = static_cast<int16_t>(160 + std::cos(angle) * dx - std::sin(angle) * dy);
    object.y = static_cast<int16_t>(112 + std::sin(angle) * dx + std::cos(angle) * dy + player_pitch * 0.7);
}

void OLevelObjs::update_player(OSprites& sprites)
{
    const auto approach = [](int16_t current, int16_t target) {
        const int difference = target - current;
        return static_cast<int16_t>(current + (std::abs(difference) < 4 ? difference : difference / 4));
    };
    player_roll = approach(player_roll, target_roll);
    player_pitch = approach(player_pitch, target_pitch);
    player_x = std::clamp<int16_t>(player_x + player_roll / 24, 80, 240);
    player_y = std::clamp<int16_t>(player_y + player_pitch / 24, 78, 164);
    const int tilt = std::clamp(std::abs(player_roll) / 10, 0, 4);
    // The normal rear-camera attitude is row 3 ($0AB2), observed in original play.
    const int pitch_level = player_pitch < -22 ? 0 : player_pitch < -12 ? 1 :
                            player_pitch < -3 ? 2 : player_pitch < 12 ? 3 : 4;
    const auto jet = frame(0x10000 + tilt * 0x190 + pitch_level * 0x50);
    auto& player = objects[0];
    player.x = static_cast<int16_t>(player_x - jet.width / 2);
    player.y = static_cast<int16_t>(player_y - jet.height / 2);
    player.control = player_roll > 0 ? 0x2000 : 0;
    if (!invulnerability || (frame_counter & 4))
        sprites.append_sprite(player, jet.bank, jet.offset, jet.pitch, 0x200, 0x200, jet.height, 0xf2, 2);
    if (vulcan_timer) --vulcan_timer;
    if (missile_cooldown) --missile_cooldown;
    if (invulnerability) --invulnerability;
}

void OLevelObjs::hit_enemy(OEntry& enemy)
{
    enemy.set_active(false);
    spawn_explosion(enemy.x, enemy.y, enemy.z);
    score += 1000;
    audio_commands.push_back(0x4c);
}

void OLevelObjs::update_weapons(OSprites& sprites)
{
    for (size_t i = 4; i <= 15; ++i)
    {
        auto& shot = objects[i];
        if (!shot.is_active()) continue;
        const bool missile = i >= 12;
        shot.z += missile ? 45 : 65;
        --shot.timer;
        if (!shot.timer || shot.z > 1800) { shot.set_active(false); continue; }
        const OEntry* target = nullptr;
        int nearest = 100000;
        if (missile)
            for (size_t j = 16; j <= 31; ++j)
            {
                const auto& enemy = objects[j];
                if (!enemy.is_active() || enemy.z < 0) continue;
                const int dx = enemy.x - 160, dy = enemy.y - 82;
                const int range = dx * dx + dy * dy;
                if (range < 90 * 90 && range < nearest) { nearest = range; target = &enemy; }
            }
        const int aim_x = target ? target->x : 160 + (player_x - 160) / 2;
        const int aim_y = target ? target->y : 82 + (player_y - 118) / 3;
        shot.x += static_cast<int16_t>((aim_x - shot.x) / (missile ? 4 : 6));
        shot.y += static_cast<int16_t>((aim_y - shot.y) / (missile ? 4 : 6));
        for (size_t j = 16; j <= 31; ++j)
        {
            auto& enemy = objects[j];
            if (enemy.is_active() && std::abs(enemy.z - shot.z) < 110 &&
                std::abs(enemy.x - shot.x) < (missile ? 22 : 12) && std::abs(enemy.y - shot.y) < 16)
            {
                shot.set_active(false); hit_enemy(enemy); break;
            }
        }
        if (!shot.is_active()) continue;
        const uint16_t zoom = static_cast<uint16_t>(std::clamp(0x200 + shot.z / 2, 0x200, 0x700));
        OEntry sprite = shot;
        const int sprite_width = missile ? 32 : 8, sprite_height = missile ? 25 : 2;
        sprite.x -= static_cast<int16_t>(sprite_width * 256 / zoom);
        sprite.y -= static_cast<int16_t>(sprite_height * 256 / zoom);
        sprites.append_sprite(sprite, missile ? 6 : 0, missile ? 0x9a49 : 0x396,
                              missile ? 4 : 1, zoom, zoom, sprite_height, missile ? 6 : 0, 3);
    }
}

void OLevelObjs::update_enemies(OSprites& sprites)
{
    // Stage 1 fighter formations: mirrored sweeps, climbing pass, then head-on pairs.
    // Explicit native sequencing rather than the old mixed-aircraft/tanker demo.
    if (!completed && frame_counter > 60 && frame_counter % 90 == 0)
    {
        const unsigned wave = frame_counter / 90;
        for (int wing = 0; wing < 4; ++wing)
            for (size_t i = 16; i <= 31; ++i)
                if (!objects[i].is_active())
                {
                    auto& enemy = objects[i]; enemy.reset(); enemy.set_active(true);
                    enemy.world_x = static_cast<int16_t>((wing - 2) * 85 + ((wave & 1) ? 100 : -100));
                    enemy.world_y = static_cast<int16_t>(-80 + wing * 15);
                    enemy.z = static_cast<int16_t>(1800 + wing * 100);
                    enemy.vx = (wave & 1) ? -1 : 1;
                    enemy.attr = static_cast<uint8_t>(wave % 3); break;
                }
    }
    for (size_t i = 16; i <= 31; ++i)
    {
        auto& enemy = objects[i];
        if (!enemy.is_active()) continue;
        ++enemy.timer;
        enemy.z -= static_cast<int16_t>(14 + player_throttle * 6);
        enemy.world_x += enemy.vx;
        enemy.world_y = static_cast<int16_t>(-65 + std::sin(enemy.timer / 22.0 + i) * 35);
        if (enemy.z < -100) { enemy.set_active(false); continue; }
        enemy.x = static_cast<int16_t>(160 + enemy.world_x * 300 / std::max(250, enemy.z + 250));
        enemy.y = static_cast<int16_t>(112 + enemy.world_y * 200 / std::max(300, enemy.z + 400));
        project(enemy);
        if (!completed && enemy.z > 300 && enemy.z < 900 && (enemy.timer % 30) == 0)
            for (size_t j = 64; j < 72; ++j)
                if (!objects[j].is_active())
                {
                    auto& threat = objects[j]; threat.reset(); threat.set_active(true);
                    threat.x = enemy.x; threat.y = enemy.y; threat.z = enemy.z; threat.timer = 55;
                    break;
                }
        const int lod = enemy.z > 1200 ? 3 : enemy.z > 700 ? 2 : enemy.z > 350 ? 1 : 0;
        const auto jet = frame(0x11a30 + lod * 16);
        const uint16_t zoom = static_cast<uint16_t>(std::clamp(0x200 + std::max<int>(0, enemy.z) / 3, 0x200, 0x380));
        OEntry sprite = enemy;
        sprite.control = enemy.vx < 0 ? 0x2000 : 0;
        sprite.x -= static_cast<int16_t>(jet.width * 256 / zoom);
        sprite.y -= static_cast<int16_t>(jet.height * 256 / zoom);
        if (sprite.x > -170 && sprite.x < 320 && sprite.y > -150 && sprite.y < 224)
            sprites.append_sprite(sprite, jet.bank, jet.offset, jet.pitch, zoom, zoom, jet.height, 0xcb, 2);
    }
}

void OLevelObjs::update_scenery(OSprites& sprites, const ORoad&)
{
    // Bank 3 canopy/rock LODs verified from Stage 1 sprite RAM and Sub CPU tables.
    // A moving water channel exposes the shore as rows approach the camera.
    if (frame_counter < 120) return;
    const double phase = (travel % 640) / 640.0;
    for (int row = 0; row < 8; ++row)
    {
        const double depth = (row + phase + 1) / 8.0;
        const int ground = static_cast<int>(124 + depth * depth * 175);
        const int lod = row < 3 ? 1 : 0;
        const auto canopy = frame(0x165f0 + lod * 16);
        const uint16_t zoom = static_cast<uint16_t>(std::clamp<int>(static_cast<int>((lod ? 950 : 1450) - depth * 1000), 0x160, 0x500));
        const int sw = canopy.width * 512 / zoom, sh = canopy.height * 512 / zoom;
        const int spacing = std::max(22, sw * 2 / 3);
        const double channel = std::sin(travel / 1700.0 + row * 0.25) * 140;
        for (int x = -150; x < 380; x += spacing)
        {
            const int center = x + sw / 2;
            if (std::abs(center - (160 + channel)) < 38 + row * 3) continue;
            OEntry patch; patch.control = ((x + row) & 1) ? 0x2000 : 0;
            patch.z = static_cast<int16_t>(2000 - row * 180 - phase * 180);
            patch.x = static_cast<int16_t>(x);
            patch.y = static_cast<int16_t>(ground - sh + std::sin(x * 0.05 + row) * 9);
            if (patch.x > 320 || patch.y > 250) continue;
            sprites.append_sprite(patch, canopy.bank, canopy.offset, canopy.pitch, zoom, zoom, canopy.height,
                                  ((x / spacing + row) & 3) == 0 ? 0x41 : 0, 1);
            if (std::abs(center - (160 + channel)) < sw + 55 && row > 2)
            {
                const auto rock = frame(0x16af0 + lod * 16);
                patch.y = static_cast<int16_t>(ground - rock.height * 512 / zoom + 6);
                patch.z += 1; // The canopy covers the top edge of the shore rock.
                sprites.append_sprite(patch, rock.bank, rock.offset, rock.pitch, zoom, zoom, rock.height, 0x10, 1);
            }
        }
    }
}

void OLevelObjs::update_hud(OSprites& sprites)
{
    const int aim_x = 160 + (player_x - 160) / 2;
    const int aim_y = 82 + (player_y - 118) / 3;
    OEntry crosshair; crosshair.z = -200;
    crosshair.x = aim_x - 12; crosshair.y = aim_y - 9;
    sprites.append_sprite(crosshair, 1, 0x7e29, 3, 0x200, 0x200, 19, 0xf1, 3);
    int best = 100000; const OEntry* target = nullptr;
    for (size_t i = 16; i <= 31; ++i)
    {
        const auto& enemy = objects[i];
        if (!enemy.is_active() || enemy.z <= 0 || enemy.z > 1400) continue;
        const int dx = enemy.x - aim_x, dy = enemy.y - aim_y, range = dx * dx + dy * dy;
        if (range < 70 * 70 && range < best) { best = range; target = &enemy; }
    }
    if (target)
    {
        OEntry tracker; tracker.z = -199; tracker.x = target->x - 12; tracker.y = target->y - 9;
        sprites.append_sprite(tracker, 1, static_cast<uint16_t>(0x7e29 + ((frame_counter / 3) % 7) * 0x39),
                              3, 0x200, 0x200, 19, 0xf1, 3);
        if (!completed && frame_counter % 90 == 0) { audio_commands.push_back(0xe0); audio_commands.push_back(0xc1); }
    }
    OEntry radar; radar.z = -201; radar.x = 254; radar.y = 33;
    sprites.append_sprite(radar, 0, 0x7dec, 2, 0x200, 0x200, 8, 0x1d, 3);
}

void OLevelObjs::spawn_explosion(int16_t x, int16_t y, int16_t z)
{
    for (size_t i = 32; i <= 39; ++i)
        if (!objects[i].is_active())
        {
            auto& blast = objects[i]; blast.reset(); blast.set_active(true);
            blast.x = x; blast.y = y; blast.z = z; break;
        }
}

void OLevelObjs::update_threats(OSprites& sprites)
{
    for (size_t i = 64; i < 72; ++i)
    {
        auto& threat = objects[i]; if (!threat.is_active()) continue;
        threat.z -= 42; --threat.timer;
        threat.x += static_cast<int16_t>((player_x - threat.x) / 20);
        threat.y += static_cast<int16_t>((player_y - threat.y) / 20);
        if (threat.z < 0 || !threat.timer)
        {
            threat.set_active(false);
            if (!completed && !invulnerability && std::abs(threat.x - player_x) < 24 && std::abs(threat.y - player_y) < 24)
            {
                ++hits; invulnerability = 90;
                spawn_explosion(player_x, player_y, -121); audio_commands.push_back(0x4e);
            }
            continue;
        }
        OEntry sprite = threat;
        const uint16_t zoom = static_cast<uint16_t>(std::clamp(0x180 + threat.z / 2, 0x180, 0x600));
        sprite.x -= 8; sprite.y -= 6;
        sprites.append_sprite(sprite, 6, 0x9a49, 4, zoom, zoom, 25, 6, 3);
    }
    constexpr uint16_t offsets[6] = {0x0bfb,0x1441,0x2d01,0x3745,0x1e8e,0x5118};
    constexpr uint16_t heights[6] = {99,124,122,116,145,125};
    for (size_t i = 32; i <= 39; ++i)
    {
        auto& blast = objects[i]; if (!blast.is_active()) continue;
        if (++blast.timer >= 24) { blast.set_active(false); continue; }
        const int index = blast.timer / 4;
        const int stride = index == 4 ? 19 : index == 5 ? 15 : 16;
        const uint16_t zoom = static_cast<uint16_t>(std::clamp(0x400 + blast.z / 3 - blast.timer * 6, 0x280, 0x700));
        OEntry sprite = blast;
        sprite.x -= static_cast<int16_t>(stride * 8 * 256 / zoom);
        sprite.y -= static_cast<int16_t>(heights[index] * 256 / zoom);
        sprites.append_sprite(sprite, 5, offsets[index], stride, zoom, zoom, heights[index], 7, 3);
    }
}

void OLevelObjs::update(OSprites& sprites, ORoad& road)
{
    ++frame_counter;
    if (!completed)
    {
        travel += static_cast<uint32_t>(12 + player_throttle * 6);
        if (frame_counter >= 3600)
        {
            completed = true;
            for (size_t i = 4; i < 72; ++i) objects[i].set_active(false);
            audio_commands.push_back(0xef);
        }
        if (frame_counter % 30 == 0) { audio_commands.push_back(0xf1); audio_commands.push_back(0x20 + player_throttle * 6); }
    }
    update_player(sprites);
    road.set_roll(player_roll); road.set_horizon(static_cast<int16_t>(124 + player_pitch));
    update_scenery(sprites, road);
    update_enemies(sprites);
    update_weapons(sprites);
    update_threats(sprites);
    update_hud(sprites);
}
