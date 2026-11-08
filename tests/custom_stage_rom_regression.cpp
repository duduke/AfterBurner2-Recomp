#include "engine/custom_stage_rom.hpp"
#include "engine/custom_stage_interactive.hpp"
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
        rom.maincpu.rom[0x32482 + stage] = uint8_t(stage);
        for (unsigned byte = 0; byte < 20; ++byte)
            rom.maincpu.rom[descriptor + byte] = uint8_t(stage * 11 + byte);
        Shuffle::Detail::write32(rom, 0x3249a + stage * 4, descriptor);
        Shuffle::Detail::write32(rom, descriptor + 0, primary);
        Shuffle::Detail::write32(rom, descriptor + 4, alternate);
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
        rom.maincpu.rom[events + 2] = (stage == 2 || stage == 8 || stage == 10 || stage == 15 || stage == 18 || stage == 20) ? 14 : 2;
        rom.maincpu.rom[events + 10] = 18; // command 18 must be stripped
        rom.maincpu.rom[events + 18] = 12; // terminal command 12
        Shuffle::Detail::write16(rom, events + 24, 0xffff);
    }
    rom.maincpu.loaded = true;
}

void test_rom_view_compilation(const Roms& canonical) {
    const std::vector<uint8_t> before(canonical.maincpu.rom, canonical.maincpu.rom + canonical.maincpu.length);

    CustomStage::Definition def = CustomStage::Definition::preset_by_name("canyon-combat");
    def.name = "CRIMSON CANYON";
    def.slot = 1;
    def.source_stage = 4; // 1-based stage 4 (pos 3) has 0x80 music -> inherits from predecessor when music_cmd is unset
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
        {15, 3, {2, 0, 0, 0, 0}},
        {5,  2, {1, 0, 0, 0, 0}}
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

    // Check relocated stage descriptor and compiled event stream
    const auto entry = StageCatalog::get(active, 1);
    require(entry.descriptor >= 0x40000 && entry.events >= 0x40000,
            "CustomStage::RomView did not relocate stage descriptor and event list to upper ROM");

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
    require(compiled_events.front().first == 5 && compiled_events.front().second == 2,
            "CustomStage::RomView did not splice custom event (tick=5, cmd=2) at start");
    require(compiled_events.back().second == 12,
            "CustomStage::RomView event stream does not end with stage-completion command 12");

    // Check replace_events mode automatically appends terminal command 12 if omitted
    auto def_replace = def;
    def_replace.replace_events = true;
    def_replace.custom_events = {{10, 2, {1, 0, 0, 0, 0}}, {25, 3, {2, 0, 0, 0, 0}}};
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
            R"({"version":1,"name":"X","objective":"combat","environment":"canyon","events":[{"tick":10,"command":18}]})"));
    }, "Accepted reserved event command 18");
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

    const std::string title8(reinterpret_cast<const char*>(active.maincpu.rom + 0x2b6ac + pos * 8u), 8);
    const std::string banner32(reinterpret_cast<const char*>(active.maincpu.rom + 0x2ea16 + pos * 32u), 32);

    std::cout << "=== Compiled CustomStage::RomView ===\n"
              << "  Stage Name   : " << def.name << "\n"
              << "  Target Slot  : " << def.slot << " (donor stage " << def.donor_stage() << ")\n"
              << "  HUD Title    : [" << title8 << "] @ $2B6AC\n"
              << "  Stage Banner : [" << banner32 << "] @ $2EA16\n"
              << "  Music Byte   : 0x" << std::hex << unsigned(active.maincpu.rom[0x2b604 + pos]) << std::dec << " @ $2B604\n"
              << "  Descriptor   : 0x" << std::hex << entry.descriptor << " (events @ 0x" << entry.events << ", palette0 @ 0x" << pal0 << ")\n"
              << "  Ocean RGB555 : sky=0x" << StageCatalog::Detail::word(active, pal0 + 34)
              << " horizon=0x" << StageCatalog::Detail::word(active, pal0 + 34 + 6 * 2)
              << " water=0x" << StageCatalog::Detail::word(active, pal0 + 34 + 13 * 2) << std::dec << "\n"
              << "  Scene Layers : " << def.layers.size() << "\n"
              << "  Event Stream :\n";

    for (uint32_t cur = entry.events;; cur += 8u) {
        const uint16_t t = StageCatalog::Detail::word(active, cur);
        if (t == 0xffffu) {
            std::cout << "    [0x" << std::hex << cur << "] SENTINEL 0xFFFF" << std::dec << "\n";
            break;
        }
        const uint8_t cmd = active.maincpu.rom[cur + 2u];
        std::cout << "    [0x" << std::hex << cur << std::dec << "] tick=" << t
                  << " cmd=" << unsigned(cmd)
                  << " params=[" << unsigned(active.maincpu.rom[cur + 3u])
                  << "," << unsigned(active.maincpu.rom[cur + 4u])
                  << "," << unsigned(active.maincpu.rom[cur + 5u])
                  << "," << unsigned(active.maincpu.rom[cur + 6u])
                  << "," << unsigned(active.maincpu.rom[cur + 7u]) << "]\n";
    }

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
        }
    }

    std::cout << "Custom stage ROM & interactive regression passed." << std::endl;
    return 0;
} catch (const std::exception& error) {
    std::cerr << "Custom stage regression failed: " << error.what() << std::endl;
    return 1;
}
