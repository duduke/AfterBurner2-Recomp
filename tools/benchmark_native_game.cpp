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
    const bool full_throttle=argc==2 && std::string_view(argv[1])=="--full-throttle";
    const bool rolls=argc==2 && std::string_view(argv[1])=="--rolls";
    const bool missiles=argc==2 && std::string_view(argv[1])=="--missiles";
    const bool extended=full_throttle || rolls || missiles || (argc==2 && std::string_view(argv[1])=="--extended");
    if(argc!=1 && !extended) throw std::runtime_error("Usage: native_game_benchmark [--extended|--full-throttle|--rolls|--missiles]");
    if (!roms.load_aburner2()) throw std::runtime_error("ROMs required");
    XBoardGame game(roms);
    unsigned orientation_calls=0, effect_calls=0, missile_indicator_calls=0;
    if(rolls || missiles) game.board().phase=[&](const auto& phase) {
        if(phase.cpu!=0 || !phase.entry) return;
        if(phase.entry->state.pc==0x7784) ++orientation_calls;
        if(phase.entry->state.pc==0x6598) ++effect_calls;
        if(phase.entry->state.pc==0x19c78) ++missile_indicator_calls;
    };
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
            if(missiles) {
                input.stick_x=0; input.stick_y=0;
            } else if(rolls) {
                input.stick_x=((f-600)/60)%2 ? 1.0 : -1.0;
            } else if(full_throttle) {
                // Sustained Space input; the exact clamp has directed MAME fixtures.
                input.stick_x=0; input.stick_y=0;
            } else if(extended) {
                // Retained live smoke's eight 120-frame control segments.
                constexpr float x[]={0.4,-0.4,0,0.25}, y[]={-0.3,0.2,0,0.1};
                const unsigned segment=((f-600)/120)%4;
                input.stick_x=x[segment]; input.stick_y=y[segment];
            } else {
                input.stick_x = ((f-600)/30)%2 ? -0.4 : 0.4;
                input.stick_y = ((f-600)/45)%2 ? -0.3 : 0.3;
            }
            // Roll with Space held first, then with neutral throttle.
            input.throttle = rolls ? (f<1080 ? 1.0 : 0.0) : full_throttle ? 1.0 : 0.5;
            input.cannon = !full_throttle || f>=720;
            input.missile = !full_throttle || f>=720;
            if(missiles) {
                input.throttle=f<1080 ? 0.0 : 1.0;
                input.cannon=false;
                input.missile=(f-600)%30<6;
            }
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
    if(rolls) std::cout << "roll_orientation_calls=" << orientation_calls
                       << " roll_effect_calls=" << effect_calls << '\n';
    if(missiles) {
        std::cout << "missile_press_windows=32 missile_indicator_calls="
                  << missile_indicator_calls << '\n';
        if(!missile_indicator_calls)
            throw std::runtime_error("Missile regression did not exercise $19C78");
    }
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
