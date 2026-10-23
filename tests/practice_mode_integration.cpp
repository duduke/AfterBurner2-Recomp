#include "frontend/native_practice.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <string_view>

static void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

static GameControls boot_input(unsigned frame) {
    GameControls input;
    input.coin = frame >= 360 && frame < 372;
    input.start = frame >= 420 && frame < 432;
    return input;
}

static void same_frame(GameSession& game, GameSession& reference, bool bad_reference = false) {
    const auto actual = game.frame(), expected = reference.frame();
    require(actual.info.number == expected.info.number && actual.info.time_ns == expected.info.time_ns,
            "Practice changed native frame/time frontier");
    require(actual.pixels.size() == expected.pixels.size(), "Practice changed native frame dimensions");
    for (size_t i = 0; i < actual.pixels.size(); ++i) {
        const auto a = actual.pixels[i];
        auto b = expected.pixels[i];
        if (bad_reference && i == 0) b.r ^= 1;
        require(a.r == b.r && a.g == b.g && a.b == b.b,
                "Practice RGB differs from directed debug startup");
    }
}

struct Observer {
    unsigned restarted{}, frame_callbacks{}, audio_callbacks{};
    std::vector<int16_t> last_audio;
    GameSession::Extensions extensions() {
        GameSession::Extensions result;
        result.restarted = [&] {
            ++restarted;
            frame_callbacks = audio_callbacks = 0;
            last_audio.clear();
        };
        result.process_audio = [&](std::span<int16_t> samples, GameSession::FrameInfo info) {
            require(info.number == uint64_t(frame_callbacks) + 1,
                    "Practice audio callback has the wrong completed frontier");
            ++audio_callbacks;
            last_audio.assign(samples.begin(), samples.end());
        };
        result.frame_complete = [&](GameSession::FrameView view) {
            require(view.info.number == ++frame_callbacks,
                    "Practice lost or duplicated a completed-frame callback");
            require(view.pixels.size() == GameSession::width * GameSession::height,
                    "Practice callback has incomplete native pixels");
        };
        return result;
    }
};

static void invalid_preserves(GameSession& game, Frontend::NativePractice& practice, unsigned candidate) {
    const auto before = game.info();
    const auto switches = game.dip_switches();
    const auto stage = practice.stage();
    const bool paused = game.paused();
    bool rejected = false;
    try { practice.start(game, candidate, {0, 0, 0}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "Invalid practice stage accepted");
    require(game.info().number == before.number && game.info().time_ns == before.time_ns &&
            game.dip_switches() == switches && game.paused() == paused && practice.stage() == stage,
            "Invalid practice candidate mutated the current run or retry target");
}

int main(int argc, char** argv) try {
    bool bad_reference = false;
    if (argc == 2 && std::string_view(argv[1]) == "--bad-reference") bad_reference = true;
    else require(argc == 1, "Usage: practice_mode_integration [--bad-reference]");
    require(roms.load_aburner2(), "ROMs required");
    const std::array<uint8_t, 3> switches{0xff, 0xff, 0xdd};
    Observer observed, baseline;
    GameSession game(roms, observed.extensions()), reference(roms, baseline.extensions());
    game.capture_music(true);
    game.capture_feedback(true);
    reference.capture_music(true);
    reference.capture_feedback(true);
    Frontend::NativePractice practice;
    require(practice.stage() == 0 && game.stage_count() == NativeStageSkip::stage_count(roms),
            "Practice must begin in arcade mode with the runtime stage range");
    game.set_dip_switches(switches);
    require(game.advance({}), "Initial board did not advance");
    game.set_paused(true);
    invalid_preserves(game, practice, 0);
    invalid_preserves(game, practice, game.stage_count() + 1);
    invalid_preserves(game, practice, std::numeric_limits<unsigned>::max());
    require(observed.restarted == 0, "Invalid stage cleared extension playback");

    unsigned exact_frames = 0, supplied_callbacks = 0;
    uint64_t observed_samples = 0, observed_music = 0, observed_feedback = 0;
    bool nonzero_audio = false;
    // These are lifecycle comparisons with the existing directed debug helper,
    // not independent MAME fidelity evidence or natural stage progression.
    for (unsigned selected : {1u, 8u, 23u}) {
        require(selected <= game.stage_count(), "Representative ROM stage unavailable");
        const auto previous_restarts = observed.restarted;
        unsigned callbacks = 0;
        const auto discard = [&] {
            require(game.info().number == ++callbacks,
                    "Practice after_frame did not run exactly once per hidden frame");
            const auto samples = game.take_audio();
            observed_samples += samples.size();
            nonzero_audio |= std::any_of(samples.begin(), samples.end(), [](auto sample) { return sample != 0; });
            observed_music += game.take_music().size();
            const auto feedback = game.take_feedback();
            require(feedback.number == game.info().number && feedback.ns == game.info().time_ns,
                    "Practice feedback callback has the wrong frontier");
            ++observed_feedback;
        };
        game.set_paused(true);
        if (selected == 1) practice.start(game, selected, switches); // Exercise default discard.
        else practice.start(game, selected, switches, discard);
        require(practice.stage() == selected && !game.paused() && game.dip_switches() == switches &&
                observed.restarted == previous_restarts + 1,
                "Practice failed to cold restart with current DIP settings and retry target");
        require(game.take_audio().empty() && game.take_music().empty() && !game.take_feedback().number,
                "Hidden practice startup retained audio, music or feedback output");
        require(observed.frame_callbacks == game.info().number && observed.audio_callbacks != 0,
                "Practice bypassed extension frame/audio observations");
        if (selected != 1) {
            require(callbacks == game.info().number && callbacks >= 600,
                    "Practice boot or ROM entry bypassed after_frame");
            supplied_callbacks += callbacks;
        }

        reference.restart();
        reference.set_dip_switches(switches);
        for (unsigned frame = 0; frame < 600; ++frame) {
            require(reference.advance(boot_input(frame)), "Reference boot paused");
            reference.take_audio(); reference.take_music(); reference.take_feedback();
        }
        reference.debug_skip_to_stage(selected);
        reference.take_audio(); reference.take_music(); reference.take_feedback();
        same_frame(game, reference, bad_reference);
        require(observed.last_audio == baseline.last_audio,
                "Practice entry PCM differs from directed debug startup");
        ++exact_frames;
        for (unsigned frame = 0; frame < 120; ++frame) {
            GameControls input;
            input.stick_x = frame < 60 ? -0.5f : 0.5f;
            input.stick_y = frame < 40 ? -0.5f : frame < 80 ? 0.5f : 0;
            input.throttle = frame < 60 ? 1 : -1;
            input.cannon = true;
            input.missile = frame % 30 < 3;
            require(game.advance(input) && reference.advance(input), "Post-entry flight paused");
            same_frame(game, reference);
            require(game.take_audio() == reference.take_audio(),
                    "Practice flight PCM differs from directed debug startup");
            game.take_music(); reference.take_music(); game.take_feedback(); reference.take_feedback();
            ++exact_frames;
        }
        game.set_paused(true);
        const auto paused = game.info();
        require(!game.advance({}) && game.info().number == paused.number && game.info().time_ns == paused.time_ns,
                "Practice pause advanced the board");
        invalid_preserves(game, practice, 0);
        invalid_preserves(game, practice, game.stage_count() + 1);
        std::cout << "Practice stage " << selected << ": entry and 120 flight RGB/PCM/time frames match debug startup; pause and invalid bounds preserve target\n";
    }
    require(supplied_callbacks >= 1200 && observed_samples && observed_music && observed_feedback && nonzero_audio,
            "Practice did not exercise captured PCM/music/feedback output");

    // Retry takes the active target. A separately browsed UI candidate cannot
    // change it until a successful start request reaches this controller.
    const unsigned retry_stage = practice.stage();
    practice.start(game, retry_stage, switches);
    require(practice.stage() == retry_stage && !game.paused() && game.take_audio().empty() &&
            game.take_music().empty() && !game.take_feedback().number,
            "Practice retry failed to reset paused state and discard hidden output");

    const std::array<uint8_t, 3> arcade_switches{0xff, 0xff, 0xfc};
    const auto previous_restarts = observed.restarted;
    game.advance({}); // Queue application output before returning to arcade.
    game.set_paused(true);
    practice.arcade(game, arcade_switches);
    require(practice.stage() == 0 && game.info().number == 0 && game.info().time_ns == 0 &&
            !game.paused() && game.dip_switches() == arcade_switches &&
            observed.restarted == previous_restarts + 1 && observed.frame_callbacks == 0 &&
            game.take_audio().empty() && game.take_music().empty() && !game.take_feedback().number,
            "Return to arcade failed to clear practice, board, outputs or extension playback");
    reference.restart(); reference.set_dip_switches(arcade_switches);
    for (unsigned frame = 0; frame < 12; ++frame) {
        require(game.advance({}) && reference.advance({}), "Returned arcade boot paused");
        same_frame(game, reference);
        require(game.take_audio() == reference.take_audio(), "Returned arcade PCM differs from cold boot");
        game.take_music(); reference.take_music(); game.take_feedback(); reference.take_feedback();
        ++exact_frames;
    }
    require(observed.restarted == 5, "Practice start/retry/arcade restart callback count changed");
    std::cout << "Practice lifecycle: " << exact_frames << " exact native RGB/PCM/time comparisons, "
              << supplied_callbacks << " supplied hidden-frame callbacks; cold restart/DIPs, default and supplied output discard, retry, pause and arcade return passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
