#include "frontend/native_practice.hpp"
#include <algorithm>
#include <array>
#include <iostream>
#include <set>
#include <string_view>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static constexpr std::array<uint8_t, 3> stock_switches{0xff, 0xff, 0x8d};
static constexpr std::array region_members{&Roms::maincpu, &Roms::subcpu, &Roms::tiles,
    &Roms::sprites, &Roms::road, &Roms::soundcpu, &Roms::pcm};

struct CanonicalSnapshot {
    std::array<std::vector<uint8_t>, region_members.size()> bytes;
    explicit CanonicalSnapshot(const Roms& rom) {
        for (unsigned i = 0; i < bytes.size(); ++i) {
            const auto& region = rom.*region_members[i];
            bytes[i].assign(region.rom, region.rom + region.length);
        }
    }
    void check(const Roms& rom) const {
        for (unsigned i = 0; i < bytes.size(); ++i) {
            const auto& region = rom.*region_members[i];
            require(region.length == bytes[i].size() &&
                    std::equal(bytes[i].begin(), bytes[i].end(), region.rom),
                    "Shuffle modified canonical ROM storage");
        }
    }
};

static Shuffle::Plan test_plan(unsigned count) {
    require(count >= 17, "Shuffle test requires canonical stages 8, 5 and 17");
    Shuffle::Plan plan{0x53485546u, {8, 5, 17}};
    for (unsigned stage = 1; stage <= count; ++stage)
        if (stage != 8 && stage != 5 && stage != 17) plan.stages.push_back(stage);
    return plan;
}

static GameControls boot_input(unsigned frame) {
    GameControls input;
    input.coin = frame >= 360 && frame < 372;
    input.start = frame >= 420 && frame < 432;
    return input;
}

// Existing tools/natural_inputs.py ground-awards physical scenario. Its Easy /
// 3x Credits pins plus 100 coin pulses cap the bank at 99 credits: 297 starting
// lives. This exercises continuous progression; it is not standard-credit play
// or an assertion that the requested weapons successfully allocate a handler.
static GameControls natural_input(unsigned frame) {
    GameControls input;
    input.coin = frame >= 360 && frame < 2760 && frame % 24 < 12;
    input.start = frame >= 2820 && frame < 2832;
    if (frame >= 2880) {
        const unsigned phase = ((frame - 2880) / 120) % 4;
        constexpr float x[]{0, -2.f / 3, 2.f / 3, 0};
        constexpr float y[]{-1, -0.5f, 0, 0.5f};
        input.stick_x = x[phase]; input.stick_y = y[phase];
        input.throttle = 1;
        input.cannon = true;
        input.missile = frame % 90 < 12;
    }
    return input;
}

struct Observer {
    unsigned restarts{};
    uint64_t frames{}, audio_callbacks{}, total_frames{};
    std::vector<int16_t> last_audio;
    GameSession::Extensions extensions() {
        GameSession::Extensions callbacks;
        callbacks.restarted = [this] { ++restarts; frames = audio_callbacks = 0; last_audio.clear(); };
        callbacks.process_audio = [this](auto samples, auto info) {
            require(info.number == frames + 1, "Shuffle audio extension frontier changed");
            ++audio_callbacks; last_audio.assign(samples.begin(), samples.end());
        };
        callbacks.frame_complete = [this](auto view) {
            require(view.info.number == ++frames, "Shuffle frame extension frontier changed");
            require(view.pixels.size() == 320 * 224, "Shuffle extension lost native pixels");
            ++total_frames;
        };
        return callbacks;
    }
};

template<class Reference>
static void same_frame(GameSession& session, Reference& reference, GameSession::FrameInfo expected) {
    const auto actual = session.frame();
    const auto pixels = reference.frame().pixels;
    require(actual.info.number == expected.number && actual.info.time_ns == expected.time_ns,
            "Shuffle changed frame/time frontier");
    require(actual.pixels.size() == pixels.size() &&
            std::equal(actual.pixels.begin(), actual.pixels.end(), pixels.begin(),
                [](auto a, auto b) { return a.r == b.r && a.g == b.g && a.b == b.b; }),
            "Shuffle presentation observer changed native RGB");
}

static void drain(GameSession& game) {
    game.take_audio(); game.take_music(); game.take_feedback();
}

static void lifecycle(const Shuffle::Plan& plan, const CanonicalSnapshot& canonical) {
    Observer observer;
    GameSession session(roms, observer.extensions());
    session.capture_music(true); session.capture_cockpit(true); session.capture_feedback(true);
    session.capture_wide_scene(true); session.capture_ground_horizon(true); session.capture_sprite_scene(true);
    session.set_dip_switches(stock_switches);
    for (unsigned frame = 0; frame < 24; ++frame) { session.advance({}); drain(session); }
    session.advance({}); session.set_paused(true);
    session.start_shuffle(plan);
    require(session.shuffled() && session.shuffle_plan()->stages == plan.stages &&
            session.shuffle_plan()->seed == plan.seed && session.shuffle_position() == 0 &&
            session.shuffled_stage() == 0 && session.info().number == 0 && !session.paused() &&
            session.dip_switches() == stock_switches && observer.restarts == 1,
            "Shuffle start failed to preserve DIPs and clear the paused board");
    require(session.take_audio().empty() && session.take_music().empty() && !session.take_feedback().number &&
            !session.sprite_presentation().ready && !session.sprite_scene().ready && !session.wide_scene().ready,
            "Shuffle start retained output or presentation history");
    canonical.check(roms);

    bool music_started = false;
    for (unsigned frame = 0; frame < 4; ++frame) {
        session.advance({});
        session.take_audio(); session.take_feedback();
        music_started |= !session.take_music().empty();
    }
    require(music_started, "Shuffle fixture did not reach the first music export");
    session.advance({}); session.set_paused(true);
    const auto before = session.info();
    const auto expect_invalid = [&](Shuffle::Plan invalid) {
        bool rejected = false;
        try { session.start_shuffle(invalid); } catch (const std::exception&) { rejected = true; }
        require(rejected && session.paused() && observer.restarts == 1 &&
                session.info().number == before.number && session.info().time_ns == before.time_ns &&
                session.dip_switches() == stock_switches && session.shuffle_plan()->stages == plan.stages,
                "Invalid Shuffle plan mutated the active run");
    };
    auto invalid = plan; invalid.stages.front() = invalid.stages.back(); expect_invalid(invalid);
    invalid = plan; invalid.stages.pop_back(); expect_invalid(invalid);
    invalid = plan; invalid.stages.front() = 0; expect_invalid(invalid);
    invalid = plan; std::swap(invalid.stages.front(), invalid.stages.back()); expect_invalid(invalid);
    bool direct_rejected = false;
    try { session.enter_practice_stage(1); } catch (const std::exception&) { direct_rejected = true; }
    require(direct_rejected && session.shuffled() && session.paused() && observer.restarts == 1 &&
            session.info().number == before.number && session.info().time_ns == before.time_ns &&
            session.shuffle_plan()->stages == plan.stages,
            "Direct Practice entry mutated a bound Shuffle run");
    require(session.take_audio() == observer.last_audio && !session.take_music().empty() &&
            session.take_feedback().number == before.number,
            "Invalid Shuffle plan cleared pending output");
    session.retry_shuffle();
    require(observer.restarts == 2 && !session.paused() && session.dip_switches() == stock_switches &&
            session.shuffle_plan()->stages == plan.stages && session.info().number == 0,
            "Shuffle retry changed its order, DIPs or paused state");

    // Capture-enabled and capture-free sessions independently bind the same
    // plan. This checks deterministic lifecycle/observer neutrality, not MAME
    // equivalence for the arranged content policy itself.
    GameSession plain(roms); plain.capture_music(true); plain.set_dip_switches(stock_switches);
    plain.start_shuffle(plan);
    uint64_t music_chunks = 0, spatial_samples = 0;
    for (unsigned frame = 0; frame < 660; ++frame) {
        auto input = boot_input(frame);
        if (frame >= 600) { input.throttle = 1; input.cannon = true; input.missile = frame % 30 < 3; }
        require(session.advance(input) && plain.advance(input), "Shuffle deterministic boot paused");
        same_frame(session, plain, plain.info());
        require(session.take_audio() == plain.take_audio(), "Shuffle presentation capture changed native PCM");
        auto chunks = session.take_music(); music_chunks += chunks.size();
        for (const auto& chunk : chunks)
            for (const auto& block : chunk.spatial) spatial_samples += block.music.size();
        plain.take_music(); session.take_feedback();
    }
    require(session.shuffle_position() == 1 && session.shuffled_stage() == 8 &&
            observer.frames == 660 && observer.audio_callbacks && music_chunks && spatial_samples,
            "Shuffle first-stage setup or preserved music/cockpit capture was not observed");
    const auto paused = session.info(); session.set_paused(true);
    require(!session.advance({}) && session.info().number == paused.number && session.info().time_ns == paused.time_ns,
            "Paused Shuffle advanced");
    session.retry_shuffle();
    require(observer.restarts == 3 && session.info().number == 0 && !session.paused() &&
            session.dip_switches() == stock_switches && !session.shuffle_position(),
            "Shuffle flight retry retained state");

    Frontend::NativePractice practice;
    practice.start(session, 1, stock_switches);
    require(!session.shuffled() && !session.shuffle_plan() && !session.shuffle_position() &&
            !session.shuffled_stage() && practice.stage() == 1 && observer.restarts == 4,
            "Shuffle to Practice retained an arranged ROM binding");
    // This is the same explicit transition used by the frontend: clear its
    // Practice target, then bind the requested continuous Shuffle run.
    practice.arcade(session, stock_switches);
    session.start_shuffle(plan);
    require(practice.stage() == 0 && session.shuffled() && observer.restarts == 6,
            "Practice to Shuffle retained the Practice retry target");
    session.advance({}); session.set_paused(true); session.restart();
    require(!session.shuffled() && !session.shuffle_plan() && !session.paused() && session.info().number == 0 &&
            session.dip_switches() == std::array<uint8_t, 3>{0xff, 0xff, 0xdd} && observer.restarts == 7 &&
            session.take_audio().empty() && session.take_music().empty() && !session.take_feedback().number,
            "Canonical restart retained Shuffle ROM, DIP pins or output");

    // A bare original host provides a separate reference for restored Arcade.
    XBoardGame original(roms);
    for (unsigned frame = 0; frame < 16; ++frame) {
        const auto pins = arcade_inputs({}); XBoardGame::Host::Input input;
        input.buttons = pins.buttons; input.analog = pins.analog;
        original.tick(input); session.advance({});
        const auto actual = session.frame(); const auto& expected = original.pixels();
        require(actual.info.number == original.board().frames && actual.info.time_ns == original.board().time() &&
                std::equal(actual.pixels.begin(), actual.pixels.end(), expected.begin(),
                    [](auto a, auto b) { return a.r == b.r && a.g == b.g && a.b == b.b; }) &&
                session.take_audio() == original.take_audio(),
                "Returning to Arcade changed original host RGB/PCM/time");
        session.take_music(); session.take_feedback();
    }
    const auto unchanged = session.info(); bool rejected = false;
    try { session.retry_shuffle(); } catch (const std::exception&) { rejected = true; }
    require(rejected && session.info().number == unchanged.number && session.info().time_ns == unchanged.time_ns &&
            observer.restarts == 7, "Inactive Shuffle retry mutated Arcade");
    canonical.check(roms);
    std::cout << "Shuffle lifecycle passed: 660 capture-neutral RGB/PCM/time frames, 16 original-host restoration frames; invalid plans, DIPs, pause/retry, music/cockpit and Arcade/Practice mode switches\n";
}

static void natural_run(const Shuffle::Plan& plan, const CanonicalSnapshot& canonical, bool complete) {
    Observer observer;
    GameSession session(roms, observer.extensions());
    session.set_dip_switches(stock_switches);
    session.capture_feedback(true);
    session.start_shuffle(plan);
    const unsigned expected_restarts = observer.restarts;
    const unsigned limit = complete ? 100000 : 24000;
    unsigned reached = 0, final_position_frames = 0;
    bool flight = false, finished = false, ending = false;
    std::set<unsigned> ending_modes;
    unsigned previous_ending_mode = 0;
    uint16_t starting_lives = 0;
    uint32_t previous_entry_score = 0;
    std::array<uint64_t, 3> cues{};
    GameSession::FrameInfo previous{};
    for (unsigned frame = 0; frame < limit; ++frame) {
        try { require(session.advance(natural_input(frame)), "Continuous Shuffle paused unexpectedly"); }
        catch (const std::exception&) {
            const auto stopped = session.info();
            const auto state = session.run_status();
            std::cerr << "Shuffle STOP attempted_input=" << frame << " board_frame=" << stopped.number
                      << " time_ns=" << stopped.time_ns << " position=" << session.shuffle_position()
                      << " source=" << session.shuffled_stage() << " mode=" << state.mode << std::endl;
            throw;
        }
        const auto now = session.info();
        require(now.number == uint64_t(frame) + 1 && now.number == previous.number + 1 && now.time_ns > previous.time_ns &&
                observer.frames == now.number && observer.restarts == expected_restarts,
                "Continuous Shuffle reset or skipped physical frames");
        previous = now;
        session.take_audio();
        const auto feedback = session.take_feedback();
        for (auto cue : feedback.events) ++cues.at(static_cast<unsigned>(cue.cue));
        const auto state = session.run_status();
        const unsigned position = session.shuffle_position();
        if (state.mode == 16 && position) {
            if (!flight) {
                flight = true; starting_lives = state.lives;
                require(starting_lives == 297, "Easy / 99-credit fixture did not begin with 297 lives");
            }
            require(position <= plan.stages.size() && session.shuffled_stage() == plan.stages[position - 1],
                    "Continuous Shuffle source identity differs from its plan");
            if (position != reached) {
                require(position == reached + 1, "Continuous Shuffle omitted or repeated a stage position");
                require(state.score >= previous_entry_score, "Continuous Shuffle reset its score between stage entries");
                previous_entry_score = state.score;
                reached = position;
                std::cout << "Shuffle natural position=" << reached << " source=" << session.shuffled_stage()
                          << " frame=" << now.number << " lives=" << state.lives << " score=" << state.score << std::endl;
            }
            if (!complete && reached == 3 && ++final_position_frames >= 120) { finished = true; break; }
        }
        if (complete && state.mode == 17) {
            require(reached == plan.stages.size(), "Shuffle ending started before all stage positions completed");
            ending = true;
        }
        if (ending) {
            ending_modes.insert(state.mode);
            if (state.mode != previous_ending_mode) {
                previous_ending_mode = state.mode;
                std::cout << "Shuffle ending mode=" << state.mode << " frame=" << now.number
                          << " lives=" << state.lives << " score=" << state.score << std::endl;
            }
            if (ending_modes.contains(26) && (state.mode == 0 || state.mode == 1)) { finished = true; break; }
        }
        if (frame % 3000 == 2999)
            std::cout << "Shuffle natural progress: frames=" << now.number << " mode=" << state.mode
                      << " position=" << position << " lives=" << state.lives << std::endl;
    }
    canonical.check(roms);
    require(finished && reached == (complete ? plan.stages.size() : 3) && flight &&
            observer.restarts == expected_restarts && cues[0] && cues[1],
            "Continuous Shuffle did not finish the declared natural window with observed cannon/missile allocations");
    if (complete) {
        for (unsigned mode : {17u, 18u, 19u, 20u, 21u, 22u, 25u, 26u})
            require(ending_modes.contains(mode), "Continuous Shuffle missed a required ending/ranking mode");
        require(ending_modes.contains(0) || ending_modes.contains(1), "Continuous Shuffle did not return to attract");
    }
    std::cout << "Shuffle natural " << (complete ? "completion" : "three-position window") << " passed: "
              << session.info().number << " physical frames, " << reached << " consecutive initialized positions, "
              << starting_lives << " starting lives, cannon=" << cues[0] << " missile=" << cues[1] << " impact=" << cues[2]
              << "; no stage skips, extra board restarts or native frame/time resets"
              << (complete ? "; ending17..22, ranking25/26 and attract return observed" : "") << std::endl;
}

int main(int argc, char** argv) try {
    const bool complete = argc == 2 && std::string_view(argv[1]) == "--complete";
    require(argc == 1 || complete, "Usage: shuffle_session_integration [--complete]");
    require(roms.load_aburner2(), "ROMs required");
    const CanonicalSnapshot canonical(roms);
    const auto plan = test_plan(StageCatalog::count(roms));
    lifecycle(plan, canonical);
    natural_run(plan, canonical, complete);
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
