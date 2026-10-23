// Continuous arranged-ROM evidence. Inputs include only ROM data and physical
// pins; expected observations must come from independently executing MAME.
#include "engine/game_session.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
Shuffle::Plan declared_plan() {
    // Source 23 is the ROM finale. Both windows use the same fixed-final
    // arrangement; --complete extends the input window and acceptance gate.
    return {0x53485546, {8,5,17,1,2,3,4,6,7,9,10,11,12,13,14,15,16,18,19,20,21,22,23}};
}
unsigned positive(const std::string& value) {
    unsigned result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    require(parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size() && result,
            "Frame count must be a positive integer");
    return result;
}
struct Physical {
    uint8_t buttons{};
    std::array<uint8_t, 8> analog{};
    bool operator==(const Physical&) const = default;
};
Physical profile(unsigned frame) {
    Physical result{255, {128,128,128,128,128,16,16,16}};
    // Exact tools/natural_inputs.py --scenario ground-awards profile.
    if (frame >= 360 && frame < 2760 && frame % 24 < 12) result.buttons &= ~64;
    if (frame >= 2820 && frame < 2832) result.buttons &= ~8;
    if (frame >= 2880) {
        const auto phase = ((frame - 2880) / 120) % 4;
        result.analog[0] = std::array<uint8_t,4>{128,64,192,128}[phase];
        result.analog[1] = std::array<uint8_t,4>{64,96,128,160}[phase];
        result.analog[2] = 255;
        result.buttons &= ~16;
        if (frame % 90 < 12) result.buttons &= ~32;
    }
    return result;
}
struct Input {
    explicit Input(const std::string& path) : stream(path) {
        std::string magic;
        unsigned a, b, c;
        require(bool(stream >> magic >> frames >> a >> b >> c) && magic == "AB2PHYS1"
                && frames && a <= 255 && b <= 255 && c <= 255,
                "Invalid physical input header: " + path);
        switches = {uint8_t(a), uint8_t(b), uint8_t(c)};
    }
    Physical next(unsigned frame) {
        unsigned index, value;
        Physical result;
        require(bool(stream >> index >> value) && index == frame && value <= 255,
                "Missing/noncontiguous physical input frame " + std::to_string(frame));
        result.buttons = uint8_t(value);
        for (auto& axis : result.analog) {
            require(bool(stream >> value) && value <= 255, "Invalid physical ADC");
            axis = uint8_t(value);
        }
        return result;
    }
    void finish() { stream >> std::ws; require(stream.peek() == EOF, "Trailing physical input rows"); }
    std::ifstream stream;
    unsigned frames{};
    std::array<uint8_t,3> switches{};
};
GameControls controls(const Physical& pins) {
    const auto axis = [](unsigned value, unsigned low, unsigned high) {
        require(value >= low && value <= high, "Physical ADC outside application control range");
        return (float(value) - 128.f) / float(value < 128 ? 128 - low : high - 128);
    };
    GameControls result;
    result.stick_x = axis(pins.analog[0], 32, 224);
    result.stick_y = axis(pins.analog[1], 64, 192);
    result.throttle = axis(pins.analog[2], 0, 255);
    result.coin = !(pins.buttons & 64); result.start = !(pins.buttons & 8);
    result.cannon = !(pins.buttons & 16); result.missile = !(pins.buttons & 32);
    const auto roundtrip = arcade_inputs(result);
    require(roundtrip.buttons == pins.buttons && roundtrip.analog == pins.analog,
            "Physical input cannot round-trip through GameControls");
    return result;
}
void export_inputs(const std::string& prefix, unsigned frames) {
    const auto plan = declared_plan();
    require(StageCatalog::count(roms) == plan.stages.size(), "Declared Shuffle ROM census changed");
    Shuffle::RomView arranged(roms, plan);
    const auto parent = std::filesystem::path(prefix).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    std::ofstream image(prefix + ".rom", std::ios::binary), order(prefix + ".order"), input(prefix + ".inputs");
    require(bool(image) && bool(order) && bool(input), "Cannot create arranged Shuffle inputs");
    image.write(reinterpret_cast<const char*>(arranged.rom().maincpu.rom), arranged.rom().maincpu.length);
    order << "AB2SHUF1\n" << plan.seed << ' ' << plan.stages.size() << '\n';
    for (const auto stage : plan.stages) order << stage << ' ';
    order << '\n';
    input << "AB2PHYS1\n" << frames << " 255 255 141\n";
    for (unsigned frame = 0; frame < frames; ++frame) {
        const auto pins = profile(frame);
        input << frame << ' ' << unsigned(pins.buttons);
        for (const auto value : pins.analog) input << ' ' << unsigned(value);
        input << '\n';
    }
    require(bool(image) && bool(order) && bool(input), "Arranged Shuffle input write failed");
    std::cout << "Exported declared Shuffle ROM data/order and " << frames
              << " ground-awards physical-input frames; no expected state exported.\n";
}
Shuffle::Plan read_plan(const std::string& prefix) {
    std::ifstream order(prefix + ".order");
    std::string magic;
    unsigned count;
    Shuffle::Plan plan;
    require(bool(order >> magic >> plan.seed >> count) && magic == "AB2SHUF1" && count <= 64,
            "Invalid declared Shuffle order");
    plan.stages.resize(count);
    for (auto& stage : plan.stages) require(bool(order >> stage), "Truncated Shuffle order");
    order >> std::ws; require(order.peek() == EOF, "Trailing Shuffle order data");
    Shuffle::validate(plan, StageCatalog::count(roms));
    require(plan.seed == declared_plan().seed && plan.stages == declared_plan().stages,
            "Shuffle execution fixture differs from its declared order");
    Shuffle::RomView arranged(roms, plan);
    std::ifstream image(prefix + ".rom", std::ios::binary);
    for (unsigned i = 0; i < arranged.rom().maincpu.length; ++i)
        require(image.get() == arranged.rom().maincpu.rom[i], "Declared arranged ROM changed; recapture required");
    require(image.peek() == EOF, "Trailing arranged ROM bytes");
    return plan;
}
}

int main(int argc, char** argv) try {
    require(roms.load_aburner2(), "ROMs required");
    if (argc >= 3 && std::string_view(argv[1]) == "--export") {
        unsigned frames = 0;
        bool complete = false;
        for (int i = 3; i < argc; ++i) {
            const std::string_view option = argv[i];
            if (option == "--complete" && !complete) complete = true;
            else if (option == "--frames" && !frames && i + 1 < argc) frames = positive(argv[++i]);
            else throw std::runtime_error("Usage: oracle_shuffle_run --export prefix [--complete] [--frames count]");
        }
        export_inputs(argv[2], frames ? frames : complete ? 100000 : 12000);
        return 0;
    }
    require(argc >= 3, "Usage: oracle_shuffle_run prefix observations.csv [--complete] [--bad-reference] [--check-coverage-only] [--bad-ending-coverage]");
    bool bad_reference = false, complete = false, coverage_only = false, bad_ending_coverage = false;
    for (int i = 3; i < argc; ++i) {
        const std::string_view option = argv[i];
        if (option == "--complete" && !complete) complete = true;
        else if (option == "--bad-reference" && !bad_reference) bad_reference = true;
        else if (option == "--check-coverage-only" && !coverage_only) coverage_only = true;
        else if (option == "--bad-ending-coverage" && !bad_ending_coverage) bad_ending_coverage = true;
        else throw std::runtime_error("Usage: oracle_shuffle_run prefix observations.csv [--complete] [--bad-reference] [--check-coverage-only] [--bad-ending-coverage]");
    }
    require((!coverage_only && !bad_ending_coverage) || complete,
            "Ending coverage checks require --complete");
    require(!coverage_only || !bad_reference, "Reference field mutation requires a native replay");
    const std::string prefix = argv[1], capture = argv[2];
    const auto plan = read_plan(prefix);
    Input declared(prefix + ".inputs"), echoed(capture + ".inputs");
    require(declared.frames == echoed.frames && declared.switches == echoed.switches,
            "MAME physical input echo metadata differs");
    std::unique_ptr<GameSession> game;
    if (!coverage_only) {
        game = std::make_unique<GameSession>(roms);
        game->start_shuffle(plan);
        game->set_dip_switches(echoed.switches);
    }
    std::ifstream reference(capture);
    std::string line;
    require(bool(std::getline(reference, line)) && line == "frame,time_ns,mode,position,lives,score",
            "Invalid Shuffle observation header");
    std::set<unsigned> flight_positions;
    std::set<unsigned> modes;
    std::vector<unsigned> lifecycle_modes;
    bool flight_seen = false, returned_to_attract = false;
    Physical previous_request{};
    for (unsigned frame = 0; frame < echoed.frames; ++frame) {
        const auto intended = declared.next(frame), actual = echoed.next(frame);
        // MAME's cached digital port updates one request later; ADC field
        // overrides are immediately readable. Replay the observed pins, as
        // required by the retained natural-credit capture contract.
        const auto expected_buttons = frame ? previous_request.buttons : intended.buttons;
        require(actual.buttons == expected_buttons && actual.analog == intended.analog,
                "MAME physical input delivery differs at frame " + std::to_string(frame));
        previous_request = intended;
        require(bool(std::getline(reference, line)), "Missing Shuffle observation row");
        std::replace(line.begin(), line.end(), ',', ' ');
        std::istringstream row(line);
        uint64_t number, ns, mode, position, lives, score;
        require(bool(row >> number >> ns >> mode >> position >> lives >> score), "Malformed Shuffle observation row");
        row >> std::ws; require(row.peek() == EOF, "Extra Shuffle observation fields");
        const auto physical_controls = controls(actual);
        if (coverage_only) {
            require(number == uint64_t(frame) + 1 && ns == number * XBoardTimeline::FRAME_NS,
                    "Complete Shuffle fixture frame/timestamp continuity differs");
        } else {
            const auto previous_info = game->info();
            const auto previous_status = game->run_status();
            const auto previous_position = game->shuffle_position();
            try {
                require(game->advance(physical_controls), "Shuffle unexpectedly paused");
            } catch (const std::exception& error) {
                const auto failed_status = game->run_status();
                throw std::runtime_error("Shuffle native advance failed at observation frame "
                    + std::to_string(number) + " (physical input " + std::to_string(frame)
                    + "): previous=" + std::to_string(previous_info.number) + ","
                    + std::to_string(previous_info.time_ns) + "," + std::to_string(previous_status.mode)
                    + "," + std::to_string(previous_position) + "," + std::to_string(previous_status.lives)
                    + "," + std::to_string(previous_status.score) + " expected=" + std::to_string(number)
                    + "," + std::to_string(ns) + "," + std::to_string(mode) + ","
                    + std::to_string(position) + "," + std::to_string(lives) + ","
                    + std::to_string(score) + " failed_state=" + std::to_string(failed_status.mode)
                    + "," + std::to_string(game->shuffle_position()) + ","
                    + std::to_string(failed_status.lives) + "," + std::to_string(failed_status.score)
                    + "; " + error.what());
            }
            game->take_audio(); game->take_music(); game->take_feedback();
            const auto info = game->info();
            const auto status = game->run_status();
            if (bad_reference && frame == 0) ns ^= 1;
            if (number != info.number || ns != info.time_ns || mode != status.mode
                || position != game->shuffle_position() || lives != status.lives || score != status.score)
                throw std::runtime_error("Shuffle execution differs from independent MAME at frame "
                    + std::to_string(frame + 1) + ": native=" + std::to_string(info.number) + ","
                    + std::to_string(info.time_ns) + "," + std::to_string(status.mode) + ","
                    + std::to_string(game->shuffle_position()) + "," + std::to_string(status.lives) + ","
                    + std::to_string(status.score) + " expected=" + std::to_string(number) + ","
                    + std::to_string(ns) + "," + std::to_string(mode) + "," + std::to_string(position)
                    + "," + std::to_string(lives) + "," + std::to_string(score));
        }
        if (mode == 16 && position) flight_positions.insert(unsigned(position));
        modes.insert(unsigned(mode));
        flight_seen |= mode == 16;
        if (flight_seen && !returned_to_attract) {
            if (lifecycle_modes.empty() || lifecycle_modes.back() != mode)
                lifecycle_modes.push_back(unsigned(mode));
            if (mode == 0 || mode == 1) returned_to_attract = true;
        }
        if (!coverage_only && (frame + 1) % 2000 == 0)
            std::cout << "Shuffle independent replay: " << frame + 1 << '/' << echoed.frames
                      << " frames, " << flight_positions.size() << " flight positions" << std::endl;
    }
    declared.finish(); echoed.finish();
    reference >> std::ws; require(reference.peek() == EOF, "Trailing Shuffle observation rows");
    if (bad_ending_coverage) flight_positions.erase(unsigned(plan.stages.size()));
    if (complete) {
        require(flight_positions.size() == plan.stages.size(), "Complete Shuffle did not observe all 22 gameplay positions and the native finale");
        for (unsigned position = 1; position <= plan.stages.size(); ++position)
            require(flight_positions.count(position), "Complete Shuffle missed a flight position");
        const std::vector<unsigned> ending{16,17,18,19,20,21,22,25,26};
        require(returned_to_attract && lifecycle_modes.size() == ending.size() + 1
                && std::equal(ending.begin(), ending.end(), lifecycle_modes.begin())
                && (lifecycle_modes.back() == 0 || lifecycle_modes.back() == 1),
                "Complete Shuffle did not observe ordered ending 17..22, ranking 25/26 and return to attract 0/1");
    } else require(flight_positions.size() >= 3 && flight_positions.count(1) && flight_positions.count(2)
                   && flight_positions.count(3), "Shuffle execution did not observe source stages 8, 5 and 17 in flight");
    if (coverage_only) {
        std::cout << "Shuffle complete MAME fixture coverage: " << echoed.frames
                  << " contiguous physical frames; all 22 gameplay positions and native finale, ending 17..22, ranking 25/26 "
                     "and return to attract observed; native field equivalence requires the full replay.\n";
        return 0;
    }
    std::cout << "Shuffle continuous execution: " << echoed.frames << " of " << echoed.frames
              << " independent arranged-MAME observations identical, 0 skipped; physical pins/DIPs echoed exactly; "
              << flight_positions.size() << " flight positions, " << modes.size()
              << " modes; " << (complete ? "all 22 gameplay positions, native finale 23, ending/ranking/attract observed."
                                          : "first sources 8, 5 and 17 observed.")
              << " CPU/device/RGB/audio equivalence is not inferred.\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
