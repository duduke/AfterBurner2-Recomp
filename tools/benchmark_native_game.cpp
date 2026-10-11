// Unpaced native board timings, including rendering and audio generation.
// No ROM-derived data is saved. Does not measure window/GPU presentation.
#include "engine/xboard_game.hpp"
#include "engine/game_controls.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>
#include <string_view>

int main(int argc, char** argv) try {
    const bool extended=argc==2 && std::string_view(argv[1])=="--extended";
    if(argc!=1 && !extended) throw std::runtime_error("Usage: native_game_benchmark [--extended]");
    if (!roms.load_aburner2()) throw std::runtime_error("ROMs required");
    XBoardGame game(roms);
    using Clock = std::chrono::steady_clock;
    Clock::time_point render_start;
    double render_ms = 0;
    unsigned clips = 0;
    game.board().rendering_started = [&] { render_start = Clock::now(); };
    game.board().rendered = [&](uint64_t,unsigned,unsigned,bool) {
        render_ms += std::chrono::duration<double,std::milli>(Clock::now()-render_start).count();
        ++clips;
    };
    std::vector<double> times;
    double gameplay_render = 0;
    unsigned gameplay_clips = 0;
    for (unsigned f=0; f<(extended?1560u:781u); ++f) {
        GameControls input;
        input.coin = f>=360 && f<372;
        input.start = f>=420 && f<432;
        if (f>=600) {
            if(extended) {
                // Retained live smoke's eight 120-frame control segments.
                constexpr float x[]={0.4,-0.4,0,0.25}, y[]={-0.3,0.2,0,0.1};
                const unsigned segment=((f-600)/120)%4;
                input.stick_x=x[segment]; input.stick_y=y[segment];
            } else {
                input.stick_x = ((f-600)/30)%2 ? -0.4 : 0.4;
                input.stick_y = ((f-600)/45)%2 ? -0.3 : 0.3;
            }
            input.throttle = 0.5;
            input.cannon = true;
            input.missile = true;
        }
        render_ms = 0; clips = 0;
        const auto start = Clock::now();
        const auto pins=arcade_inputs(input);
        XBoardGame::Host::Input physical;
        physical.buttons=pins.buttons; physical.analog=pins.analog;
        game.tick(physical);
        game.take_audio();
        const double ms = std::chrono::duration<double,std::milli>(Clock::now()-start).count();
        if (f>=600) { times.push_back(ms); gameplay_render+=render_ms; gameplay_clips+=clips; }
    }
    const double mean=std::accumulate(times.begin(),times.end(),0.0)/times.size();
    std::sort(times.begin(),times.end());
    std::cout << "Native gameplay benchmark: " << times.size() << " flight/weapon frames\n"
              << "mean_ms=" << mean << " p95_ms=" << times[(times.size()-1)*95/100]
              << " max_ms=" << times.back() << " unpaced_fps=" << 1000/mean << '\n'
              << "render_mean_ms=" << gameplay_render/times.size()
              << " remaining_board_mean_ms=" << mean-gameplay_render/times.size()
              << " clips=" << gameplay_clips << " board_budget_ms=16.768\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
