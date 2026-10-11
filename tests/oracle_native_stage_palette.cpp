#include "engine/native_stage_skip.hpp"
#include "engine/game_controls.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char** argv) try {
    if (argc != 2) throw std::runtime_error("Usage: oracle_native_stage_palette capture");
    std::ifstream in(argv[1], std::ios::binary);
    char header[8];
    if (!in.read(header, 8) || std::string_view(header, 8) != "AB2NSP01")
        throw std::runtime_error("Invalid stage palette capture");
    auto word = [&]() -> uint16_t {
        unsigned char data[2];
        if (!in.read(reinterpret_cast<char*>(data), 2)) throw std::runtime_error("Truncated palette capture");
        return (uint16_t(data[0]) << 8) | data[1];
    };
    const auto stage = word();
    if (!roms.load_aburner2()) throw std::runtime_error("ROMs required");
    XBoardGame game(roms);
    auto tick = [&](const GameControls& controls = {}) {
        const auto pins = arcade_inputs(controls);
        XBoardGame::Host::Input input; input.buttons = pins.buttons; input.analog = pins.analog;
        game.tick(input); game.take_audio();
    };
    for (unsigned f = 0; f < 600; ++f) {
        GameControls c; c.coin = f >= 360 && f < 372; c.start = f >= 420 && f < 432;
        tick(c);
    }
    NativeStageSkip::enter(game.board(), roms, stage, [&] { tick(); });
    unsigned differences = 0;
    for (unsigned i = 0; i < 14; ++i)
        differences += NativeStageSkip::peek16(game.board(), 0x122f02 + i * 2) != word();
    if (in.peek() != std::char_traits<char>::eof()) throw std::runtime_error("Trailing palette capture data");
    if (NativeStageSkip::peek16(game.board(), 0x29c060) != stage - 1)
        throw std::runtime_error("Selected stage not reached");
    if (differences) throw std::runtime_error(std::to_string(differences) + " alternate road palette words differ from MAME");
    std::cout << "Native stage " << stage << " palette: 14/14 original-MAME gradient words identical, 0 skipped.\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
