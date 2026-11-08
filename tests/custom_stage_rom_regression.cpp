#include "engine/custom_stage_rom.hpp"
#include "engine/custom_stage_interactive.hpp"
#include "engine/game_session.hpp"
#include "engine/omainirqservice.hpp"
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <set>
#include <string>
#include <vector>

namespace {

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Function>
void rejects(Function function, const char* message = "Invalid CustomStage input was accepted") {
    try {
        function();
    } catch (const std::exception&) {
        return;
    }
    throw std::runtime_error(message);
}

constexpr auto regions = std::array{
    &Roms::maincpu, &Roms::subcpu, &Roms::tiles,
    &Roms::sprites, &Roms::road, &Roms::soundcpu, &Roms::pcm
};

constexpr std::array<std::pair<uint32_t, unsigned>, 12> tables{{
    {0x32196, 2}, {0x32324, 2}, {0x32452, 2}, {0x30d54, 2}, {0x2b604, 1},
    {0x2b6ac, 8}, {0x140da, 4}, {0x1413a, 4}, {0x14196, 4}, {0x141f2, 4},
    {0x1424e, 4}, {0x2ea16, 32}
}};

constexpr std::array<std::pair<uint32_t, unsigned>, 5> palettes{{
    {0x140da, 64}, {0x1413a, 256}, {0x14196, 256}, {0x141f2, 256}, {0x1424e, 256}
}};

void write32_be(MainBus& bus, uint32_t addr, uint32_t val) {
    bus.write16(addr, uint16_t(val >> 16));
    bus.write16(addr + 2u, uint16_t(val));
}

uint32_t read32_be(MainBus& bus, uint32_t addr) {
    return (uint32_t(bus.read16(addr)) << 16) | uint32_t(bus.read16(addr + 2u));
}

void build_synthetic_rom(Roms& rom) {
    rom.maincpu.init(0x40000);
    for (unsigned i = 1; i < regions.size(); ++i) {
        auto& region = rom.*regions[i];
        region.init(32 + i);
        for (unsigned byte = 0; byte < region.length; ++byte)
            region.rom[byte] = uint8_t(i * 17 + byte);
        region.loaded = (i & 1) != 0;
    }
    auto fill = [&](uint32_t address, unsigned stride, unsigned bank = 0) {
        for (unsigned stage = 0; stage < 23; ++stage)
            for (unsigned byte = 0; byte < stride; ++byte)
                rom.maincpu.rom[address + stage * stride + byte] =
                    uint8_t(stage * 7 + bank * 29 + byte * 3);
    };
    for (const auto& [address, stride] : tables) fill(address, stride);
    for (const uint32_t base : {0x32352u, 0x32224u})
        for (unsigned bank = 0; bank < 4; ++bank) fill(base + bank * 0x40, 2, bank);

    // Stage 1..3 have explicit music commands; stage 4 (pos 3) has 0x80 (continue predecessor).
    rom.maincpu.rom[0x2b604 + 0] = 0x81;
    rom.maincpu.rom[0x2b604 + 1] = 0x82;
    rom.maincpu.rom[0x2b604 + 2] = 0x83;
    rom.maincpu.rom[0x2b604 + 3] = 0x80;

    // Minimal valid $0137E2 text descriptors at $2B8BA and $3253E for command 12 ($DFC4)
    for (const uint32_t text_addr : {0x2b8bau, 0x3253eu}) {
        rom.maincpu.rom[text_addr + 0] = 0;
        rom.maincpu.rom[text_addr + 1] = 0;
        rom.maincpu.rom[text_addr + 2] = 0;
        rom.maincpu.rom[text_addr + 3] = 0;
        rom.maincpu.rom[text_addr + 4] = 0x20;
    }

    // Populate $3F000 enemy formation table (entries 0..31 -> $3E000 + i*16) for $8122..$82FA
    for (unsigned i = 0; i < 32; ++i) {
        const uint32_t form_data = 0x3e000u + i * 16u;
        for (unsigned b = 0; b < 16; ++b)
            rom.maincpu.rom[form_data + b] = uint8_t(i + b + 1u);
        Shuffle::Detail::write32(rom, 0x3f000u + i * 8u, form_data);
        rom.maincpu.rom[0x3f000u + i * 8u + 4u] = uint8_t(i + 1u);
        rom.maincpu.rom[0x3f000u + i * 8u + 5u] = uint8_t(i + 2u);
        rom.maincpu.rom[0x3f000u + i * 8u + 6u] = uint8_t(i + 3u);
        rom.maincpu.rom[0x3f000u + i * 8u + 7u] = uint8_t(i + 4u);
    }

    Shuffle::Detail::write16(rom, 0x3218a, 100); // missile max
    Shuffle::Detail::write16(rom, 0x32480, 23);
    uint32_t palette_cursor = 0x18000;
    for (const auto& [base, size] : palettes)
        for (unsigned stage = 0; stage < 23; ++stage) {
            Shuffle::Detail::write32(rom, base + stage * 4, palette_cursor);
            for (unsigned byte = 0; byte < size; ++byte)
                rom.maincpu.rom[palette_cursor + byte] = uint8_t(stage * 13 + byte * 7);
            palette_cursor += size;
        }
    for (unsigned stage = 0; stage < 23; ++stage) {
        const auto descriptor = 0x35000 + stage * 20;
        const auto events = 0x36000 + stage * 32;
        const auto alternate = 0x37000 + stage * 24;
        const auto primary = 0x37400 + stage * 8;
        const auto waves = 0x38000 + stage * 24;
        const auto scenery = 0x38400 + stage * 24;
        rom.maincpu.rom[0x32482 + stage] = uint8_t(stage);
        for (unsigned byte = 0; byte < 20; ++byte)
            rom.maincpu.rom[descriptor + byte] = uint8_t(stage * 11 + byte);
        Shuffle::Detail::write32(rom, 0x3249a + stage * 4, descriptor);
        Shuffle::Detail::write32(rom, descriptor + 0, primary);
        Shuffle::Detail::write32(rom, descriptor + 4, alternate);
        Shuffle::Detail::write32(rom, descriptor + 8, waves);
        Shuffle::Detail::write32(rom, descriptor + 12, scenery);
        Shuffle::Detail::write32(rom, descriptor + 16, events);
        for (unsigned byte = 0; byte < 24; ++byte)
            rom.maincpu.rom[alternate + byte] = uint8_t(stage * 5 + byte * 3);
        for (unsigned byte = 0; byte < 8; ++byte)
            rom.maincpu.rom[primary + byte] = uint8_t(stage * 3 + byte);
        for (unsigned record = 0; record < 3; ++record) {
            for (unsigned byte = 0; byte < 8; ++byte)
                rom.maincpu.rom[events + record * 8 + byte] = uint8_t(stage + record + byte);
            Shuffle::Detail::write16(rom, events + record * 8, uint16_t((record + 1) * 10));
        }
        rom.maincpu.rom[events + 2] = (stage == 2 || stage == 8 || stage == 10 || stage == 15 || stage == 18 || stage == 20) ? 14 : 17;
        rom.maincpu.rom[events + 10] = 18; // command 18 must be stripped
        rom.maincpu.rom[events + 18] = 12; // terminal command 12
        Shuffle::Detail::write16(rom, events + 24, 0xffff);

        // Populate donor enemy wave stream (2 records + 0xFFFF sentinel)
        Shuffle::Detail::write16(rom, waves + 0, 12);
        rom.maincpu.rom[waves + 2] = 0;
        rom.maincpu.rom[waves + 3] = uint8_t((stage % 8) + 1);
        Shuffle::Detail::write16(rom, waves + 8, 28);
        rom.maincpu.rom[waves + 10] = 1;
        rom.maincpu.rom[waves + 11] = uint8_t((stage % 8) + 2);
        Shuffle::Detail::write16(rom, waves + 12, 0x0010);
        Shuffle::Detail::write16(rom, waves + 16, 0xffff);

        // Populate donor scenery stream (1 record + 0xFFFF sentinel)
        Shuffle::Detail::write16(rom, scenery + 0, 16);
        rom.maincpu.rom[scenery + 2] = 0;
        Shuffle::Detail::write16(rom, scenery + 8, 0xffff);
    }
    rom.maincpu.loaded = true;
}

// Step the real translated 68000 stage control event dispatcher ($00DE68..$00DE8E)
// across every record in the compiled event stream and verify hardware side effects.
void execute_control_event_stream_68k(const Roms& active, unsigned slot) {
    MainBus::Memory memory{};
    MainBus bus(active.maincpu.rom, active.maincpu.length,
                active.subcpu.rom, active.subcpu.length, memory);
    const auto entry = StageCatalog::get(active, slot);
    write32_be(bus, 0xffff8a08u, entry.events);
    bus.write16(0x29c060u, uint16_t(slot - 1u));
    bus.write16(0x29c00eu, 0u);

    uint32_t cursor = entry.events;
    unsigned executed_batches = 0;
    while (true) {
        const uint16_t tick = StageCatalog::Detail::word(active, cursor);
        if (tick == 0xffffu) break;

        // Collect all consecutive records sharing this tick
        std::vector<CustomStage::EventRecord> batch;
        while (StageCatalog::Detail::word(active, cursor) == tick) {
            CustomStage::EventRecord rec;
            rec.tick = tick;
            rec.command = active.maincpu.rom[cursor + 2u];
            for (size_t i = 0; i < 5u; ++i)
                rec.params[i] = active.maincpu.rom[cursor + 3u + i];
            batch.push_back(rec);
            cursor += 8u;
        }

        write32_be(bus, 0x29c006u, uint32_t(tick) << 4);
        OMainIrqService irq_service;
        OMainIrqService::Context ctx{};
        ctx.boundary_ns = 0;
        ctx.interrupted_pc = 0x00de68u;
        ctx.return_pc = 0x00de68u;
        ctx.supervisor_sp = 0xfffffc00u;
        ctx.sr = 0x2000u;
        write32_be(bus, ctx.supervisor_sp, 0x0000de8eu);

        unsigned steps = 0;
        while (ctx.supervisor_sp <= 0xfffffc00u) {
            const uint32_t pc_before = ctx.return_pc;
            try {
                const auto step = irq_service.caller_step(
                    bus, ctx, [](uint64_t) {}, [](uint64_t) { return 0u; });
                ctx.registers = step.state.registers;
                ctx.return_pc = step.state.pc;
                ctx.interrupted_pc = step.state.pc;
                ctx.supervisor_sp = step.state.sp;
                ctx.sr = step.state.sr;
                ctx.boundary_ns = step.state.ns;
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    "68000 stage control event execution failed at PC " +
                    std::to_string(pc_before) + ": " + e.what());
            }
            require(++steps <= 4096u, "68000 stage control event handler exceeded step budget");
        }

        // Verify $FFFF8A08 advanced to cursor
        require(read32_be(bus, 0xffff8a08u) == cursor,
                "68000 $00DE68 dispatcher did not advance $FFFF8A08 past executed tick batch");

        // Verify hardware side effect of the final command in this tick batch
        const auto& last = batch.back();
        switch (last.command) {
        case 1u:
        case 3u:
            require((bus.read8(0x29c201u) & 0x01u) == 0u &&
                    (bus.read8(0xffff8d36u) & 0x01u) == 0u,
                    "68000 clear_horizon_flags (cmd 1/3) did not clear bit 0 of $29C201 and $FFFF8D36");
            break;
        case 4u:
            require((bus.read8(0x29c201u) & 0x01u) != 0u,
                    "68000 set_horizon_flag (cmd 4) did not set bit 0 of $29C201");
            break;
        case 7u:
            require(bus.read16(0x29c07cu) == last.word4(),
                    "68000 set_horizon_target (cmd 7) did not write word 4(A0) to $29C07C");
            break;
        case 8u:
            require((bus.read8(0xffff8b6fu) & 0x02u) != 0u,
                    "68000 set_ground_flag (cmd 8) did not set bit 1 of $FFFF8B6F");
            break;
        case 9u:
            require((bus.read8(0xffff8b6fu) & 0x02u) == 0u,
                    "68000 clear_ground_flag (cmd 9) did not clear bit 1 of $FFFF8B6F");
            break;
        case 10u:
            require(bus.read8(0x29c0a1u) == last.params[0],
                    "68000 set_scroll_state (cmd 10) did not write byte 3(A0) to $29C0A1");
            break;
        case 11u:
            require(bus.read8(0xffff8d6au) == last.params[0],
                    "68000 set_cloud_control (cmd 11) did not write byte 3(A0) to $FFFF8D6A");
            break;
        case 13u:
            require(bus.read8(0x29c0efu) == last.params[0] &&
                    bus.read16(0x29c0e6u) == last.word4() &&
                    bus.read16(0x29c0e8u) == last.word6(),
                    "68000 set_camera_bounds (cmd 13) did not write 3(A0)/4(A0)/6(A0) to $29C0EF/$29C0E6/$29C0E8");
            break;
        case 14u:
            require(bus.read16(0x29c0ccu) == 0u,
                    "68000 refuel (cmd 14) did not clear $29C0CC");
            break;
        case 15u:
            require(bus.read16(0x29c0ccu) == last.word4(),
                    "68000 set_stage_mode/landing (cmd 15) did not write word 4(A0) to $29C0CC");
            break;
        case 16u:
            require(bus.read16(0x29c320u) == 0x0402u &&
                    (bus.read8(0x29c200u) & 0x20u) != 0u,
                    "68000 special_road_exit (cmd 16) did not set $29C320=$0402 and bit 5 of $29C200");
            break;
        case 17u:
            require(bus.read16(0x29c320u) == 0x0480u &&
                    (bus.read8(0x29c200u) & 0x20u) == 0u,
                    "68000 special_road_enter (cmd 17) did not set $29C320=$0480 and clear bit 5 of $29C200");
            break;
        case 19u:
            require(bus.read8(0xffff8d7eu) == last.params[0],
                    "68000 set_bank_drift_flag (cmd 19) did not write byte 3(A0) to $FFFF8D7E");
            break;
        case 20u:
            require((bus.read8(0x29c201u) & 0x01u) != 0u &&
                    (bus.read8(0xffff8d36u) & 0x01u) != 0u &&
                    bus.read16(0xffff8d38u) == last.word4(),
                    "68000 set_horizon_scroll (cmd 20) did not set horizon flags and write word 4(A0) to $FFFF8D38");
            break;
        default:
            break;
        }
        ++executed_batches;
    }
    require(executed_batches >= 1u, "Compiled stage control stream had no executable events");
}

// Step the real translated 68000 enemy wave dispatcher ($008122..$0082FA)
// across every record in the stage's descriptor+8 enemy wave stream.
void execute_enemy_wave_stream_68k(const Roms& active, unsigned slot) {
    MainBus::Memory memory{};
    MainBus bus(active.maincpu.rom, active.maincpu.length,
                active.subcpu.rom, active.subcpu.length, memory);
    const auto entry = StageCatalog::get(active, slot);
    const uint32_t waves_ptr = StageCatalog::Detail::pointer(active, entry.descriptor + 8u);
    write32_be(bus, 0xffff8a00u, waves_ptr);

    uint32_t cursor = waves_ptr;
    while (true) {
        const uint16_t tick = StageCatalog::Detail::word(active, cursor);
        if (tick == 0xffffu) break;
        while (StageCatalog::Detail::word(active, cursor) == tick) {
            cursor += 8u;
        }

        write32_be(bus, 0x29c006u, uint32_t(tick) << 4);
        OMainIrqService irq_service;
        OMainIrqService::Context ctx{};
        ctx.registers.a[5] = 0xffffa000u;
        ctx.boundary_ns = 0;
        ctx.interrupted_pc = 0x008122u;
        ctx.return_pc = 0x008122u;
        ctx.supervisor_sp = 0xfffffc00u;
        ctx.sr = 0x2000u;

        unsigned steps = 0;
        while (ctx.return_pc != 0x0082fcu) {
            const uint32_t pc_before = ctx.return_pc;
            try {
                const auto step = irq_service.caller_step(
                    bus, ctx, [](uint64_t) {}, [](uint64_t) { return 0u; });
                ctx.registers = step.state.registers;
                ctx.return_pc = step.state.pc;
                ctx.interrupted_pc = step.state.pc;
                ctx.supervisor_sp = step.state.sp;
                ctx.sr = step.state.sr;
                ctx.boundary_ns = step.state.ns;
            } catch (const std::exception& e) {
                throw std::runtime_error(
                    "68000 enemy wave execution failed at PC " +
                    std::to_string(pc_before) + ": " + e.what());
            }
            require(++steps <= 1024u, "68000 enemy wave handler exceeded step budget");
        }
        require(read32_be(bus, 0xffff8a00u) == cursor,
                "68000 $008122 enemy wave dispatcher did not advance $FFFF8A00 to expected record");
    }
}

void test_rom_view_compilation(const Roms& canonical) {
    const std::vector<uint8_t> before(canonical.maincpu.rom, canonical.maincpu.rom + canonical.maincpu.length);

    CustomStage::Definition def = CustomStage::Definition::preset_by_name("canyon-combat");
    def.name = "CRIMSON CANYON";
    def.slot = 1;
    def.source_stage = 4; // 1-based stage 4 (pos 3) has 0x80 music -> inherits from predecessor when music_cmd is unset
    def.enemy_stage = 2;
    def.scenery_stage = 3;
    def.music_cmd.reset();
    def.road_setup = 5;
    def.sky_setup = 3;
    def.curve_setup = 4;
    def.custom_ocean = true;
    def.ocean.sky = {240, 64, 32};
    def.ocean.haze = {255, 180, 96};
    def.ocean.water = {48, 24, 16};
    def.replace_events = false;
    def.custom_events = {
        {15, 11, {2, 0, 0, 0, 0}},
        {5,  17, {0, 0, 0, 0, 0}}
    };
    def.replace_enemy_waves = false;
    def.custom_enemy_waves = {
        {8, 0, {1, 0, 0, 0, 0}},
        {22, 1, {2, 0, 24, 0, 0}}
    };

    CustomStage::RomView view(canonical, def);
    const Roms& active = view.rom();

    // Canonical ROM must remain completely untouched
    require(!std::memcmp(before.data(), canonical.maincpu.rom, before.size()),
            "CustomStage::RomView modified canonical maincpu ROM");
    require(active.maincpu.rom != canonical.maincpu.rom && active.maincpu.length == 0x80000u,
            "CustomStage::RomView did not allocate an isolated 0x80000 maincpu buffer");
    for (unsigned i = 1; i < regions.size(); ++i) {
        const auto& copy = active.*regions[i];
        const auto& src = canonical.*regions[i];
        require(copy.rom != src.rom && copy.length == src.length && copy.loaded == src.loaded &&
                !std::memcmp(copy.rom, src.rom, src.length),
                "CustomStage::RomView did not deep-clone non-maincpu ROM regions");
    }

    // Check HUD title ($2B6AC) and stage banner ($2EA16) for slot 1 (pos 0)
    require(!std::memcmp(active.maincpu.rom + 0x2b6ac, "CRIMSON ", 8),
            "CustomStage::RomView did not write 8-byte HUD title at $2B6AC");
    require(!std::memcmp(active.maincpu.rom + 0x2ea16, "CRIMSON CANYON                  ", 32),
            "CustomStage::RomView did not write 32-byte banner at $2EA16");

    // Check inherited music resolution at $2B604 for pos 0 from donor stage 4 (pos 3)
    uint8_t expected_music = canonical.maincpu.rom[0x2b604 + 3];
    if (expected_music == 0x80) {
        for (int s = 3; s >= 0; --s) {
            if (canonical.maincpu.rom[0x2b604 + s] != 0x80) {
                expected_music = canonical.maincpu.rom[0x2b604 + s];
                break;
            }
        }
    }
    require(active.maincpu.rom[0x2b604 + 0] == expected_music,
            "CustomStage::RomView did not resolve inherited music command for 0x80 stage");

    // Check explicit music override
    auto def_explicit_music = def;
    def_explicit_music.music_cmd = 0x84u;
    CustomStage::RomView view_music(canonical, def_explicit_music);
    require(view_music.rom().maincpu.rom[0x2b604 + 0] == 0x84u,
            "CustomStage::RomView did not apply explicit music_cmd override");

    // Check road/sky/curve overrides at $32196, $32324, $32452
    require(StageCatalog::Detail::word(active, 0x32196 + 0) == 5,
            "CustomStage::RomView did not apply road_setup override at $32196");
    require(StageCatalog::Detail::word(active, 0x32324 + 0) == 3,
            "CustomStage::RomView did not apply sky_setup override at $32324");
    require(StageCatalog::Detail::word(active, 0x32452 + 0) == 4,
            "CustomStage::RomView did not apply curve_setup override at $32452");

    // Check relocated palette blocks and custom ocean RGB555 gradient
    const uint32_t pal0 = StageCatalog::Detail::pointer(active, 0x140da + 0);
    require(pal0 >= 0x40000, "CustomStage::RomView did not relocate palette block 0 into upper ROM");
    require(StageCatalog::Detail::word(active, pal0 + 34) == def.ocean.sky.rgb555(),
            "CustomStage::RomView did not compile sky RGB555 into relocated palette");
    require(StageCatalog::Detail::word(active, pal0 + 34 + 13 * 2) == def.ocean.water.rgb555(),
            "CustomStage::RomView did not compile water RGB555 into relocated palette");

    // Cold-start palette alias at $2BC60 must match relocated pal0 + 34 when slot == 1
    require(!std::memcmp(active.maincpu.rom + 0x2bc60, active.maincpu.rom + pal0 + 34, 28),
            "CustomStage::RomView did not sync cold-start palette alias at $2BC60");

    // Check relocated stage descriptor, compiled event stream, enemy_stage, and scenery_stage
    const auto entry = StageCatalog::get(active, 1);
    require(entry.descriptor >= 0x40000 && entry.events >= 0x40000,
            "CustomStage::RomView did not relocate stage descriptor and event list to upper ROM");
    const auto scenery_donor_entry = StageCatalog::get(canonical, 3);
    require(StageCatalog::Detail::pointer(active, entry.descriptor + 12u) ==
            StageCatalog::Detail::pointer(canonical, scenery_donor_entry.descriptor + 12u),
            "CustomStage::RomView did not wire descriptor+12 scenery stream from scenery_stage donor");
    const uint32_t compiled_waves = StageCatalog::Detail::pointer(active, entry.descriptor + 8u);
    require(compiled_waves >= 0x40000u,
            "CustomStage::RomView did not relocate customized enemy wave stream at descriptor+8");

    // Verify event stream ordering, command-18 removal, custom event splicing, and 0xFFFF sentinel
    std::vector<std::pair<uint16_t, uint8_t>> compiled_events;
    for (uint32_t cursor = entry.events;; cursor += 8) {
        const uint16_t t = StageCatalog::Detail::word(active, cursor);
        if (t == 0xffff) break;
        const uint8_t cmd = active.maincpu.rom[cursor + 2];
        require(cmd != 18, "CustomStage::RomView retained old-adjacency command 18 in event stream");
        if (!compiled_events.empty()) {
            require(t >= compiled_events.back().first,
                    "CustomStage::RomView event stream is not sorted by timestamp");
        }
        compiled_events.emplace_back(t, cmd);
    }
    require(compiled_events.size() >= 4, "CustomStage::RomView lost spliced custom events");
    require(compiled_events.front().first == 5 && compiled_events.front().second == 17,
            "CustomStage::RomView did not splice custom event (tick=5, cmd=17) at start");
    require(compiled_events.back().second == 12,
            "CustomStage::RomView event stream does not end with stage-completion command 12");

    // Execute both compiled streams through the real 68000 instruction port
    execute_control_event_stream_68k(active, 1);
    execute_enemy_wave_stream_68k(active, 1);

    // Check replace_events mode automatically appends terminal command 12 if omitted
    auto def_replace = def;
    def_replace.replace_events = true;
    def_replace.custom_events = {{10, 17, {0, 0, 0, 0, 0}}, {25, 11, {2, 0, 0, 0, 0}}};
    CustomStage::RomView view_replace(canonical, def_replace);
    const auto rep_entry = StageCatalog::get(view_replace.rom(), 1);
    require(StageCatalog::Detail::word(view_replace.rom(), rep_entry.events + 0) == 10,
            "replace_events first event timestamp mismatch");
    require(StageCatalog::Detail::word(view_replace.rom(), rep_entry.events + 8) == 25,
            "replace_events second event timestamp mismatch");
    require(view_replace.rom().maincpu.rom[rep_entry.events + 16 + 2] == 12,
            "replace_events did not append terminal command 12");
    require(StageCatalog::Detail::word(view_replace.rom(), rep_entry.events + 24) == 0xffff,
            "replace_events did not append 0xFFFF sentinel");
    execute_control_event_stream_68k(view_replace.rom(), 1);

    // Exercise all 19 valid $00DE90 stage control commands through 68000 caller_step
    {
        auto def_all_cmds = def;
        def_all_cmds.replace_events = true;
        def_all_cmds.custom_events = {
            {2,  0,  {0, 0, 0, 0, 0}},       // music_transition sub 0
            {4,  0,  {0, 0, 4, 0, 0}},       // music_transition sub 4
            {6,  0,  {0, 0, 8, 0, 0}},       // music_transition sub 8
            {8,  0,  {0, 0, 12, 0, 0}},      // music_transition sub 12
            {10, 4,  {0, 0, 0, 0, 0}},       // set_horizon_flag
            {12, 1,  {0, 0, 0, 0, 0}},       // clear_horizon_flags
            {14, 20, {0, 1, 2, 0, 0}},       // set_horizon_scroll ($0102)
            {16, 3,  {0, 0, 0, 0, 0}},       // clear_horizon_flags_alt
            {18, 5,  {0, 0, 0, 0, 0}},       // nop_5
            {20, 6,  {0, 0, 0, 0, 0}},       // nop_6
            {22, 7,  {0, 0, 64, 0, 0}},      // set_horizon_target ($0040)
            {24, 8,  {0, 0, 0, 0, 0}},       // set_ground_flag
            {26, 9,  {0, 0, 0, 0, 0}},       // clear_ground_flag
            {28, 10, {3, 0, 0, 0, 0}},       // set_scroll_state (3)
            {30, 11, {5, 0, 0, 0, 0}},       // set_cloud_control (5)
            {32, 13, {2, 0, 32, 0, 48}},     // set_camera_bounds (2, $0020, $0030)
            {34, 15, {0, 0, 2, 0, 0}},       // set_stage_mode / landing ($0002)
            {36, 14, {0, 0, 0, 0, 0}},       // refuel (clears $29C0CC)
            {38, 17, {0, 0, 0, 0, 0}},       // special_road_enter ($0480, BCLR #5)
            {40, 16, {0, 0, 0, 0, 0}},       // special_road_exit ($0402, BSET #5)
            {42, 19, {7, 0, 0, 0, 0}}        // set_bank_drift_flag (7)
        };
        CustomStage::RomView view_all(canonical, def_all_cmds);
        execute_control_event_stream_68k(view_all.rom(), 1);
    }

    // Verify Shuffle::prepare_alternate_palette works seamlessly with CustomStage::RomView
    MainBus::Memory memory{};
    MainBus bus(view.rom().maincpu.rom, view.rom().maincpu.length,
                view.rom().subcpu.rom, view.rom().subcpu.length, memory);
    Shuffle::prepare_alternate_palette(bus, view.rom(), 0);
    require(!std::memcmp(memory.bytes.data() + MainBus::memory_offset(0x122f02),
                         view.rom().maincpu.rom + pal0 + 34, 28),
            "Shuffle::prepare_alternate_palette did not copy custom ocean gradient to palette RAM");
}

void test_variant_picker(const Roms& canonical) {
    const auto variants = CustomStage::VariantPicker::discover_tanker_stages(canonical);
    require(variants.size() == 6, "discover_tanker_stages did not find 6 tanker stages");

    CustomStage::VariantPicker picker_a(12345u);
    CustomStage::VariantPicker picker_b(12345u);

    std::vector<unsigned> seq_a, seq_b;
    for (unsigned i = 0; i < 18; ++i) {
        seq_a.push_back(picker_a.pick(variants));
        seq_b.push_back(picker_b.pick(variants));
    }
    require(seq_a == seq_b, "VariantPicker is not deterministic for identical seeds");

    for (size_t i = 1; i < seq_a.size(); ++i) {
        require(seq_a[i] != seq_a[i - 1], "VariantPicker produced adjacent duplicate tanker stages");
    }
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
        std::set<unsigned> visited(seq_a.begin() + cycle * 6, seq_a.begin() + (cycle + 1) * 6);
        require(visited.size() == 6, "VariantPicker cycle did not visit all 6 canonical tanker stages");
    }
    require(picker_a.pick(variants, variants[2]) == variants[2], "VariantPicker pinned selection failed");
    rejects([&] { picker_a.pick(variants, 99u); }, "VariantPicker accepted invalid pinned stage");
}

void test_interactive_controller_and_board_sync(const Roms& canonical) {
    // Verify all 7 interactive presets complete cleanly under demo() autopilot
    for (const char* name : {"refuel", "carrier-landing", "runway-landing", "jungle-landing",
                             "runway-takeoff", "carrier-takeoff", "jungle-takeoff"}) {
        const auto def = CustomStage::Definition::preset_by_name(name);
        CustomStage::InteractiveController ctrl(def);
        ctrl.start();
        for (unsigned step = 0; step < 3600 && !ctrl.finished(); ++step) {
            ctrl.tick(ctrl.demo());
        }
        require(ctrl.phase == CustomStage::InteractiveController::Phase::success,
                "Interactive preset failed to complete under demo() autopilot");
    }

    // Verify full-throttle gate on takeoff stage
    {
        const auto def = CustomStage::Definition::preset_by_name("carrier-takeoff");
        CustomStage::InteractiveController ctrl(def);
        ctrl.start();
        require(ctrl.phase == CustomStage::InteractiveController::Phase::ready,
                "Takeoff stage must start in ready phase");
        GameControls partial{};
        partial.throttle = 0.5f;
        for (unsigned i = 0; i < 30; ++i) ctrl.tick(partial);
        require(ctrl.phase == CustomStage::InteractiveController::Phase::ready && ctrl.speed == 0.0,
                "Takeoff stage advanced before full-throttle gate");

        GameControls zero{};
        ctrl.tick(zero); // Arm throttle (< 0.25)
        GameControls full{};
        full.throttle = 1.0f;
        ctrl.tick(full);
        require(ctrl.phase == CustomStage::InteractiveController::Phase::accelerating,
                "Takeoff stage did not enter accelerating phase on full throttle");
    }

    // Verify seamless landing -> rollout -> service -> ready -> accelerating -> climb -> success cycle
    {
        const auto def = CustomStage::Definition::preset_by_name("runway-landing");
        CustomStage::InteractiveController ctrl(def);
        ctrl.start();
        bool saw_rollout = false, saw_service = false, saw_ready = false, saw_accelerating = false, saw_climb = false;
        for (unsigned i = 0; i < 3600 && !ctrl.finished(); ++i) {
            ctrl.tick(ctrl.demo());
            saw_rollout |= (ctrl.phase == CustomStage::InteractiveController::Phase::rollout);
            saw_service |= (ctrl.phase == CustomStage::InteractiveController::Phase::service);
            saw_ready |= (ctrl.phase == CustomStage::InteractiveController::Phase::ready);
            saw_accelerating |= (ctrl.phase == CustomStage::InteractiveController::Phase::accelerating);
            saw_climb |= (ctrl.phase == CustomStage::InteractiveController::Phase::climb);
        }
        require(saw_rollout && saw_service && saw_ready && saw_accelerating && saw_climb &&
                ctrl.phase == CustomStage::InteractiveController::Phase::success,
                "Landing stage did not transition through rollout -> service -> ready -> accelerating -> climb -> success");
    }

    // Verify live X-Board RAM synchronization for carrier state 2 and airbase state 8
    {
        struct DummyHost {
            MainBus::Memory memory{};
        } host;

        // 1. Carrier state 2 ($29C09C == 15) hold & release
        const auto carrier_def = CustomStage::Definition::preset_by_name("carrier-takeoff");
        CustomStage::InteractiveController carrier_ctrl(carrier_def);
        host.memory.bytes.fill(0);
        CustomStage::InteractiveController::seed16(host.memory, 0x29c09cu, 15u);
        CustomStage::InteractiveController::seed16(host.memory, 0xffff90b0u, 2u);
        CustomStage::InteractiveController::seed16(host.memory, 0xffff90aeu, 18u);

        GameControls idle{};
        carrier_ctrl.sync_board(host, canonical, idle);
        require(carrier_ctrl.board_gate_held &&
                CustomStage::InteractiveController::peek16(host.memory, 0xffff90aeu) == 0u,
                "sync_board did not hold carrier state 2 timer ($FFFF90AE) at 0 while waiting for throttle");

        GameControls full{};
        full.throttle = 1.0f;
        carrier_ctrl.sync_board(host, canonical, full);
        require(carrier_ctrl.phase == CustomStage::InteractiveController::Phase::accelerating &&
                CustomStage::InteractiveController::peek16(host.memory, 0xffff90aeu) == 19u,
                "sync_board did not release carrier state 2 timer ($FFFF90AE) to 19 on full throttle");

        // 2. Airbase runway ($1122E state 8) hold & release
        const auto runway_def = CustomStage::Definition::preset_by_name("runway-takeoff");
        CustomStage::InteractiveController runway_ctrl(runway_def);
        host.memory.bytes.fill(0);
        CustomStage::InteractiveController::seed16(host.memory, 0x29c09cu, 16u);
        const uint32_t obj = 0xffffa000u;
        host.memory.bytes.at(MainBus::memory_offset(obj)) = 0x80u;
        CustomStage::InteractiveController::seed16(host.memory, obj + 16u, 0x0001u);
        CustomStage::InteractiveController::seed16(host.memory, obj + 18u, 0x122eu);
        CustomStage::InteractiveController::seed16(host.memory, obj + 40u, 8u);
        CustomStage::InteractiveController::seed16(host.memory, obj + 42u, 1u);
        CustomStage::InteractiveController::seed16(host.memory, 0xffff9100u, 0u);
        host.memory.bytes.at(MainBus::memory_offset(0xffff90ffu)) = 0u;

        runway_ctrl.sync_board(host, canonical, idle);
        require(runway_ctrl.board_gate_held &&
                CustomStage::InteractiveController::peek16(host.memory, obj + 42u) == 30u,
                "sync_board did not hold airbase $1122E state 8 timer (+42) at 30 while waiting for throttle");
    }
}

void test_guidance_and_shadow_math() {
    const auto ld = CustomStage::Guidance::landing(0.0, 74.0, 1200.0, 0.0, -4.8, 80.0, 0.06, 198.0, 480.0);
    require(ld.localizer_centered() && ld.glideslope_centered(),
            "Aligned landing approach was not centered on ILS localizer/glideslope");

    const auto tk_far = CustomStage::Guidance::tanker(10.0, -5.0, 220.0, 220.0, false);
    const auto tk_near = CustomStage::Guidance::tanker(1.0, -1.0, 30.0, 220.0, false);
    const auto tk_lock = CustomStage::Guidance::tanker(0.1, 0.1, 12.0, 220.0, true);
    require(!tk_far.locked && tk_near.radius > tk_far.radius && tk_lock.locked && tk_lock.radius == 84.0,
            "Refueling tanker guidance diamond progression incorrect");

    const auto s_low = CustomStage::ShadowState::at_height(5.0, 0.0);
    const auto s_high = CustomStage::ShadowState::at_height(80.0, 0.6);
    require(s_low.scale > s_high.scale && s_low.opacity > s_high.opacity && s_high.offset.x > s_low.offset.x,
            "ShadowState altitude/bank scaling incorrect");

    std::vector<CustomStage::ShadowState::Vertex> poly{
        {{-50.0, 100.0}, {0.0, 0.0}},
        {{ 50.0, 100.0}, {1.0, 0.0}},
        {{ 50.0, 200.0}, {1.0, 1.0}},
        {{-50.0, 200.0}, {0.0, 1.0}}
    };
    const std::array<CustomStage::Point, 4> quad{{
        {0.0, 120.0}, {30.0, 120.0}, {30.0, 180.0}, {0.0, 180.0}
    }};
    const auto clipped = CustomStage::ShadowState::clip(poly, quad);
    require(clipped.size() == 4, "Sutherland-Hodgman quad clipping failed");
    for (const auto& v : clipped) {
        require(v.point.x >= -0.01 && v.point.x <= 30.01 && v.point.y >= 119.99 && v.point.y <= 180.01,
                "Clipped shadow vertex fell outside deck quad");
    }
}

void test_json_assets_and_validation(const Roms& canonical) {
    const std::vector<std::string> files{
        "assets/custom_stages/refuel.json",
        "assets/custom_stages/carrier-landing.json",
        "assets/custom_stages/runway-landing.json",
        "assets/custom_stages/jungle-landing.json",
        "assets/custom_stages/runway-takeoff.json",
        "assets/custom_stages/carrier-takeoff.json",
        "assets/custom_stages/jungle-takeoff.json",
        "assets/custom_stages/layered-runway.json",
        "assets/custom_stages/canyon-combat.json",
        "assets/custom_stages/twilight-trench-run.json"
    };
    for (const auto& path : files) {
        const auto def = CustomStage::Definition::load(path);
        CustomStage::RomView view(canonical, def);
        require(view.rom().maincpu.loaded, "Compiled asset ROM not marked loaded");
        execute_control_event_stream_68k(view.rom(), def.slot);
        execute_enemy_wave_stream_68k(view.rom(), def.slot);
        CustomStage::InteractiveController ctrl(def);
        ctrl.start();
        for (unsigned step = 0; step < 3600 && !ctrl.finished(); ++step) {
            ctrl.tick(ctrl.demo());
        }
        if (!ctrl.combat()) {
            require(ctrl.phase == CustomStage::InteractiveController::Phase::success,
                    "Asset interactive stage failed demo() completion");
        }
    }

    // Verify symbolic command mapping and round-trip against the $00DE90 ROM dispatch table
    const std::pair<const char*, uint8_t> expected_control_names[] = {
        {"music_transition", 0},
        {"clear_horizon_flags", 1},
        {"clear_horizon_flags_alt", 3},
        {"set_horizon_flag", 4},
        {"nop_5", 5},
        {"nop_6", 6},
        {"set_horizon_target", 7},
        {"set_ground_flag", 8},
        {"clear_ground_flag", 9},
        {"set_scroll_state", 10},
        {"set_cloud_control", 11},
        {"stage_advance", 12},
        {"set_camera_bounds", 13},
        {"refuel", 14},
        {"set_stage_mode", 15},
        {"special_road_exit", 16},
        {"special_road_enter", 17},
        {"set_bank_drift_flag", 19},
        {"set_horizon_scroll", 20}
    };
    for (const auto& [name, cmd] : expected_control_names) {
        require(CustomStage::EventRecord::parse_control_command(name) == cmd,
                "Symbolic control command mapped to wrong $00DE90 index");
        require(CustomStage::EventRecord::parse_control_command(
                    std::string(CustomStage::EventRecord::control_command_name(cmd))) == cmd,
                "Control command name round-trip mismatch");
    }

    const auto trench = CustomStage::Definition::load("assets/custom_stages/twilight-trench-run.json");
    require(trench.custom_events.size() == 4 &&
            trench.custom_events[0].command == 11 &&
            trench.custom_events[1].command == 20 &&
            trench.custom_events[2].command == 10 &&
            trench.custom_events[3].command == 1,
            "twilight-trench-run.json symbolic events did not map to [11, 20, 10, 1]");

    const auto canyon = CustomStage::Definition::load("assets/custom_stages/canyon-combat.json");
    require(canyon.custom_events.size() == 4 &&
            canyon.custom_events[0].command == 17 &&
            canyon.custom_events[1].command == 11 &&
            canyon.custom_events[2].command == 20 &&
            canyon.custom_events[3].command == 16,
            "canyon-combat.json symbolic events did not map to [17, 11, 20, 16]");

    rejects([] { CustomStage::Json::parse("{bad json"); }, "Accepted malformed JSON");
    rejects([] {
        CustomStage::Definition::from_json(CustomStage::Json::parse(
            R"({"version":1,"name":"X","objective":"landing","environment":"carrier","unknown_field":1})"));
    }, "Accepted unknown top-level JSON field");
    rejects([] {
        CustomStage::Definition::from_json(CustomStage::Json::parse(
            R"({"version":1,"name":"X","objective":"orbit","environment":"carrier"})"));
    }, "Accepted invalid objective");
    rejects([] {
        CustomStage::Definition::from_json(CustomStage::Json::parse(
            R"({"version":1,"name":"X","objective":"landing","environment":"mars"})"));
    }, "Accepted invalid environment");
    rejects([] {
        CustomStage::Definition::from_json(CustomStage::Json::parse(
            R"({"version":1,"name":"X","objective":"landing","environment":"carrier","presentation":{"ocean":{"sky":[300,0,0]}}})"));
    }, "Accepted out-of-range RGB component");
    rejects([] {
        CustomStage::Definition::from_json(CustomStage::Json::parse(
            R"({"version":1,"name":"X","objective":"combat","environment":"canyon","events":[{"tick":10,"command":2}]})"));
    }, "Accepted unported event command 2 ($DE98)");
    rejects([] {
        CustomStage::Definition::from_json(CustomStage::Json::parse(
            R"({"version":1,"name":"X","objective":"combat","environment":"canyon","events":[{"tick":10,"command":18}]})"));
    }, "Accepted reserved event command 18");
    rejects([] {
        CustomStage::Definition::from_json(CustomStage::Json::parse(
            R"({"version":1,"name":"X","objective":"combat","environment":"canyon","events":[{"tick":10,"command":21}]})"));
    }, "Accepted out-of-range event command 21");
    rejects([] {
        CustomStage::Definition::from_json(CustomStage::Json::parse(
            R"({"version":1,"name":"X","objective":"combat","environment":"canyon","events":[{"tick":10,"command":"music_transition","params":[0,0,2,0,0]}]})"));
    }, "Accepted invalid music_transition subcommand word 2");
    rejects([] {
        CustomStage::Definition::from_json(CustomStage::Json::parse(
            R"({"version":1,"name":"X","objective":"combat","environment":"canyon","enemy_waves":[{"tick":10,"command":10}]})"));
    }, "Accepted out-of-range enemy_waves command 10");
}

void test_real_game_session(Roms& real_roms) {
    for (const char* path : {"assets/custom_stages/twilight-trench-run.json",
                             "assets/custom_stages/canyon-combat.json"}) {
        const auto def = CustomStage::Definition::load(path);
        GameSession session(real_roms);
        session.start_custom_stage(def);
        require(session.custom_stage_active() && session.custom_stage_controller() != nullptr,
                "GameSession::start_custom_stage did not activate custom stage controller");

        for (unsigned f = 0; f < 960; ++f) {
            GameControls c = session.custom_stage_controller()->demo();
            c.coin = (f >= 360 && f < 372);
            c.start = (f >= 420 && f < 432);
            require(session.advance(c), "CustomStage GameSession failed to advance frame");
        }
        require(session.info().number == 960u,
                "Real GameSession did not complete 960 frames of custom stage execution");
    }
}

void inspect_custom_stage(const Roms& canonical, const std::string& arg) {
    std::error_code ec;
    const auto def = std::filesystem::exists(arg, ec)
        ? CustomStage::Definition::load(arg)
        : CustomStage::Definition::preset_by_name(arg);
    CustomStage::RomView view(canonical, def);
    const Roms& active = view.rom();
    const unsigned pos = def.slot - 1u;
    const auto entry = StageCatalog::get(active, def.slot);
    const uint32_t pal0 = StageCatalog::Detail::pointer(active, 0x140da + pos * 4u);
    const uint32_t waves = StageCatalog::Detail::pointer(active, entry.descriptor + 8u);

    const std::string title8(reinterpret_cast<const char*>(active.maincpu.rom + 0x2b6ac + pos * 8u), 8);
    const std::string banner32(reinterpret_cast<const char*>(active.maincpu.rom + 0x2ea16 + pos * 32u), 32);

    std::cout << "=== Compiled CustomStage::RomView ===\n"
              << "  Stage Name   : " << def.name << "\n"
              << "  Target Slot  : " << def.slot << " (donor stage " << def.donor_stage()
              << ", enemy donor " << def.enemy_stage.value_or(def.donor_stage())
              << ", scenery donor " << def.scenery_stage.value_or(def.donor_stage()) << ")\n"
              << "  HUD Title    : [" << title8 << "] @ $2B6AC\n"
              << "  Stage Banner : [" << banner32 << "] @ $2EA16\n"
              << "  Music Byte   : 0x" << std::hex << unsigned(active.maincpu.rom[0x2b604 + pos]) << std::dec << " @ $2B604\n"
              << "  Descriptor   : 0x" << std::hex << entry.descriptor
              << " (events @ 0x" << entry.events
              << ", waves @ 0x" << waves
              << ", palette0 @ 0x" << pal0 << ")\n"
              << "  Ocean RGB555 : sky=0x" << StageCatalog::Detail::word(active, pal0 + 34)
              << " horizon=0x" << StageCatalog::Detail::word(active, pal0 + 34 + 6 * 2)
              << " water=0x" << StageCatalog::Detail::word(active, pal0 + 34 + 13 * 2) << std::dec << "\n"
              << "  Scene Layers : " << def.layers.size() << "\n"
              << "  Control Events ($DE68 -> $DE90) :\n";

    for (uint32_t cur = entry.events;; cur += 8u) {
        const uint16_t t = StageCatalog::Detail::word(active, cur);
        if (t == 0xffffu) {
            std::cout << "    [0x" << std::hex << cur << "] SENTINEL 0xFFFF" << std::dec << "\n";
            break;
        }
        const uint8_t cmd = active.maincpu.rom[cur + 2u];
        std::cout << "    [0x" << std::hex << cur << std::dec << "] tick=" << t
                  << " cmd=" << unsigned(cmd) << " (" << CustomStage::EventRecord::control_command_name(cmd) << ")"
                  << " params=[" << unsigned(active.maincpu.rom[cur + 3u])
                  << "," << unsigned(active.maincpu.rom[cur + 4u])
                  << "," << unsigned(active.maincpu.rom[cur + 5u])
                  << "," << unsigned(active.maincpu.rom[cur + 6u])
                  << "," << unsigned(active.maincpu.rom[cur + 7u]) << "]\n";
    }

    execute_control_event_stream_68k(active, def.slot);
    execute_enemy_wave_stream_68k(active, def.slot);
    std::cout << "  68000 Stream Execution : verified ($00DE68 control + $008122 enemy waves)\n";

    CustomStage::InteractiveController ctrl(def);
    ctrl.start();
    auto last_phase = ctrl.phase;
    std::cout << "  Autopilot Simulation :\n"
              << "    frame 0: phase=" << int(ctrl.phase) << " (" << ctrl.message << ")\n";
    for (unsigned step = 1; step <= 3600 && !ctrl.finished(); ++step) {
        ctrl.tick(ctrl.demo());
        if (ctrl.phase != last_phase) {
            last_phase = ctrl.phase;
            std::cout << "    frame " << step << ": phase=" << int(ctrl.phase)
                      << " range=" << int(ctrl.range) << "m height=" << int(ctrl.height)
                      << "m speed=" << int(ctrl.speed) << " (" << ctrl.message << ")\n";
        }
    }
    std::cout << "    result: " << ctrl.message << " (missiles=" << ctrl.missiles << "/" << ctrl.missile_max << ")\n";
}

} // namespace

int main(int argc, char** argv) try {
    Roms synthetic;
    build_synthetic_rom(synthetic);
    test_rom_view_compilation(synthetic);
    test_variant_picker(synthetic);
    test_interactive_controller_and_board_sync(synthetic);
    test_guidance_and_shadow_math();
    test_json_assets_and_validation(synthetic);

    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--inspect" && i + 1 < argc) {
            inspect_custom_stage(synthetic, argv[++i]);
            continue;
        }
        if (std::string_view(argv[i]) == "--roms") {
            if (i + 1 < argc) RomLoader::set_source(argv[++i]);
            Roms real_roms;
            require(real_roms.load_aburner2(), "Failed to load real ROM set for custom stage verification");
            test_rom_view_compilation(real_roms);
            test_variant_picker(real_roms);
            test_json_assets_and_validation(real_roms);
            test_real_game_session(real_roms);
        }
    }

    std::cout << "Custom stage ROM & interactive regression passed." << std::endl;
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Custom stage regression failed: " << error.what() << std::endl;
    return 1;
}
