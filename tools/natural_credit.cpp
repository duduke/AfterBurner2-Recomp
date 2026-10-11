// Cold-boot discovery/replay only: physical pins in, passive observations out.
#include "engine/xboard_game.hpp"
#include "engine/physical_recording.hpp"
#include <fstream>
#include <iostream>
#include <string>
#include <array>
#include <set>
#include <charconv>

using Host = XBoardGame::Host;
static uint16_t peek(const Host& h, uint32_t address) {
    const auto offset = MainBus::memory_offset(address);
    if (offset == MainBus::UNMAPPED) throw std::runtime_error("Unmapped observation");
    return uint16_t(h.memory.bytes.at(offset)) * 256 + h.memory.bytes.at(offset + 1);
}
int main(int argc, char** argv) try {
    if (argc != 3) throw std::runtime_error("Usage: natural_credit physical-inputs.txt observations.csv");
    std::ifstream input(argv[1]); std::string magic;
    if (!(input >> magic) || magic != "AB2PHYS1") throw std::runtime_error("Invalid physical recording version");
    unsigned frames, a, b, c;
    if (!(input >> frames >> a >> b >> c) || !frames || a>255 || b>255 || c>255)
        throw std::runtime_error("Invalid frame count/DIP metadata");
    if (!roms.load_aburner2()) throw std::runtime_error("ROMs required");
    XBoardGame game(roms); auto& h = game.board();
    h.switches = {uint8_t(a), uint8_t(b), uint8_t(c)};
    PhysicalRecording::write_metadata(std::string(argv[2])+".json",roms,h.switches);
    std::ofstream observations(argv[2]);
    if (!observations) throw std::runtime_error("Cannot open observations");
    observations << "frame,time_ns,mode,stage_index,lives,death_state,score,credits_bcd,carrier_state,refuel_state\n";
    std::array<uint64_t,8> previous{}; unsigned completed = 0;
    uint32_t last_pc[2]{};
    std::ofstream events(std::string(argv[2])+".events.csv");
    if (!events) throw std::runtime_error("Cannot open execution observations");
    events << "completed_frames,time_ns,stage_index,pc\n";
    std::set<std::pair<unsigned,uint32_t>> seen;
    h.phase = [&](const Host::Phase& p) {
        if (p.entry && p.cpu < 2) last_pc[p.cpu] = p.entry->state.pc;
        if (p.cpu == 0 && p.entry && p.entry->kind == 1) {
            const auto pc=p.entry->state.pc;
            // First observed controller/producer entry per stage. This is
            // evidence of entry, not proof of a completed sequence/action.
            if (pc==0x10348 || pc==0xe224 || pc==0x16236 || pc==0xdfc4 || pc==0xec78 || pc==0x9f7c || pc==0x9ed8) {
                const unsigned stage=peek(h,0x29c060);
                if (seen.emplace(stage,pc).second)
                    events << h.frames << ',' << p.ns << ',' << stage << ',' << pc << '\n';
            }
        }
    };
    try {
        for (unsigned f=0; f<frames; ++f) {
            unsigned index, value; Host::Input pins;
            if (!(input >> index >> value) || index != f || value > 255)
                throw std::runtime_error("Missing/noncontiguous input frame");
            pins.buttons = value;
            for (auto& axis : pins.analog) {
                if (!(input >> value) || value > 255) throw std::runtime_error("Invalid physical ADC value");
                axis = value;
            }
            game.tick(pins); game.take_audio(); ++completed;
            // $13DBA/$13E06 stage index, $CF46 mode/lives and $EC70
            // damage controller state. Read backing RAM; never issue bus reads.
            // $17610 reads score $8884 for the ROM ranking comparison;
            // $E1BA reads packed credit digits $8052 for scaled lives.
            // $103F2/$1624E dispatch on the carrier/refuel selectors.
            const std::array<uint64_t,8> state{peek(h,0x29c09c),peek(h,0x29c060),peek(h,0xffff88e0),peek(h,0xffff9124),
                (uint32_t(peek(h,0xffff8884))<<16)|peek(h,0xffff8886),
                h.memory.bytes.at(MainBus::memory_offset(0xffff8052)),peek(h,0xffff90b0),peek(h,0xffff90e0)};
            observations << h.frames << ',' << h.time();
            for (auto v : state) observations << ',' << v;
            observations << '\n';
            if (!f || !std::equal(state.begin(),state.begin()+4,previous.begin())) {
                std::cout << "TRANSITION frame=" << h.frames << " ns=" << h.time()
                          << " mode=" << state[0] << " stage_index=" << state[1]
                          << " lives=" << state[2] << " death_state=" << state[3] << std::endl;
            }
            previous = state;
        }
        input >> std::ws;
        if (input.peek() != EOF) throw std::runtime_error("Trailing physical input records");
    } catch (const std::exception& error) {
        std::cerr << "STOP completed_frames=" << completed << " attempted_frame=" << completed
                  << " board_frame=" << h.frames << " time_ns=" << h.time()
                  << " last_main_pc=" << std::hex << last_pc[0] << " last_sub_pc=" << last_pc[1]
                  << std::dec;
        const std::string message=error.what();
        if (const auto at=message.rfind(" at PC "); at!=std::string::npos) {
            uint32_t pc=0; const auto* start=message.data()+at+7;
            const auto parsed=std::from_chars(start,message.data()+message.size(),pc);
            if (parsed.ec==std::errc{} && parsed.ptr==message.data()+message.size())
                std::cerr << " stopped_pc=0x" << std::hex << pc << std::dec;
        }
        std::cerr << " error=" << message << '\n';
        return 1;
    }
    std::cout << "RESULT completed_frames=" << completed << " status=window-complete; ending acceptance not inferred\n";
    return 0;
} catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
