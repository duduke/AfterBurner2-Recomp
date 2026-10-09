#include "engine/game_session.hpp"
#include <iostream>

void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
GameControls controls(unsigned frame) {
    GameControls v;
    v.coin = frame >= 360 && frame < 372;
    v.start = frame >= 420 && frame < 432;
    if (frame >= 480) {
        const auto phase = ((frame - 480) / 24) % 12;
        constexpr float x[] = {0,-1,1,0,0,0,0,-1,1,0,0,0};
        constexpr float y[] = {0,0,0,-1,1,0,0,1,-1,0,0,0};
        v.stick_x = x[phase]; v.stick_y = y[phase];
        v.throttle = phase == 5 ? -1 : phase == 6 ? 1 : 0;
        v.cannon = phase == 7 || phase == 9 || phase == 10;
        v.missile = phase == 8 || phase == 9 || phase == 10;
    }
    return v;
}
void same_frame(GameSession& session, XBoardGame& reference) {
    const auto view = session.frame();
    require(view.width == 320 && view.height == 224 && view.pixels.size() == reference.pixels().size(), "native frame dimensions changed");
    require(view.info.number == reference.board().frames && view.info.time_ns == reference.board().time(), "frame frontier changed");
    for (size_t i = 0; i < view.pixels.size(); ++i) {
        const auto a = view.pixels[i], b = reference.pixels()[i];
        require(a.r == b.r && a.g == b.g && a.b == b.b, "session RGB differs from native host");
    }
}
int main() try {
    require(roms.load_aburner2(), "ROMs required");
    XBoardGame reference(roms);
    unsigned frames = 0, restarts = 0, processed = 0;
    bool mute = false, nonzero_muted_source = false;
    GameSession::Extensions extensions;
    extensions.frame_complete = [&](GameSession::FrameView view) {
        require(view.info.number == ++frames, "duplicate or missing frame notification");
        require(view.pixels.size() == 320 * 224, "incomplete frame notification");
    };
    extensions.process_audio = [&](std::span<int16_t> samples, GameSession::FrameInfo info) {
        require(info.number == frames + 1 && samples.size() % 2 == 0, "audio must belong to the completed physical frame");
        ++processed;
        if (mute) std::fill(samples.begin(), samples.end(), 0);
    };
    extensions.restarted = [&] { frames = 0; ++restarts; };
    GameSession session(roms, extensions);
    for (unsigned f = 0; f < 781; ++f) {
        if (f == 520) {
            const auto before = session.info();
            session.set_paused(true);
            for (unsigned p = 0; p < 5; ++p) require(!session.advance(controls(f)), "paused session advanced");
            require(session.info().number == before.number && session.info().time_ns == before.time_ns && frames == f, "pause moved the board or notified extensions");
            require(session.take_audio().empty(), "pause produced audio");
            session.set_paused(false);
        }
        // Mute a real nonzero section, then restore unchanged audio output.
        mute = f >= 600 && f < 650;
        const auto input = arcade_inputs(controls(f));
        XBoardGame::Host::Input raw; raw.buttons = input.buttons; raw.analog = input.analog;
        reference.tick(raw);
        require(session.advance(controls(f)), "running session did not advance");
        same_frame(session, reference);
        auto expected = reference.take_audio();
        if (mute) {
            for (auto sample : expected) nonzero_muted_source |= sample != 0;
            std::fill(expected.begin(), expected.end(), 0);
        }
        require(session.take_audio() == expected, "audio extension affected native output or replay");
    }
    require(frames == 781 && processed > 0 && nonzero_muted_source, "extensions must exercise gameplay and nonzero audio");
    // Pending application audio is discarded on restart; extension lifecycle
    // survives, while both board and paused state return to a fresh run.
    session.set_paused(true); session.restart(); reference.restart();
    require(restarts == 1 && !session.paused() && session.take_audio().empty(), "restart lifecycle failed");
    for (unsigned f = 0; f < 8; ++f) {
        const auto input = arcade_inputs({});
        XBoardGame::Host::Input raw; raw.buttons = input.buttons; raw.analog = input.analog;
        reference.tick(raw); session.advance({}); same_frame(session, reference);
        require(session.take_audio() == reference.take_audio(), "session restart differs from fresh native audio");
    }
    std::cout << "Game session passed: 781 exact native RGB frames, identity/processed audio, pause/resume and restart lifecycle.\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
