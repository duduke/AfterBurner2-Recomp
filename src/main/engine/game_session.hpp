#pragma once

#include "engine/game_controls.hpp"
#include "engine/xboard_game.hpp"
#include "hwvideo/sprite_scene.hpp"
#include "engine/native_stage_skip.hpp"
#include <functional>
#include <span>

// Application-facing boundary. The literal board remains the gameplay owner;
// extensions observe completed output or process a separate PCM output copy.
class GameSession {
public:
    static constexpr unsigned width = 320, height = 224, sample_rate = 48000;
    static constexpr double frame_seconds = XBoardTimeline::FRAME_NS / 1.0e9;

    struct FrameInfo { uint64_t number{}, time_ns{}; };
    struct FrameView {
        FrameInfo info;
        unsigned width{}, height{};
        std::span<const RGB888> pixels;
    };
    struct SpritePresentation {
        // Premultiplied ARGB8888, native dimensions. Background is opaque;
        // sprites are transparent except where the native priority test wins.
        std::span<const uint32_t> background, sprites;
        std::span<const uint8_t> protected_pixels;
        bool ready{};
    };
    struct SpriteScene {
        std::span<const uint32_t> heads;
        std::span<const Presentation::Fragment> fragments;
        bool ready{};
    };
    struct Extensions {
        // Views last only for the callback. Copy data needed asynchronously.
        std::function<void(FrameView)> frame_complete;
        // Interleaved signed 16-bit stereo, 48 kHz. Does not alter chip state.
        std::function<void(std::span<int16_t>, FrameInfo)> process_audio;
        // Clear extension-owned playback/history when starting a fresh board.
        std::function<void()> restarted;
    };

    explicit GameSession(Roms& rom, Extensions extensions = {})
        : game(rom), rom(rom), extensions(std::move(extensions)) {}

    // Opt-in debug startup, after the normal physical coin/start boot.
    void debug_skip_to_stage(unsigned stage) {
        NativeStageSkip::enter(game.board(), rom, stage, [&] {
            advance({}); take_audio();
        });
    }

    // One physical frame per advance, independent of display refresh. Pausing
    // admits no inputs and advances no CPU, audio device or gameplay timer.
    bool advance(const GameControls& controls) {
        if (paused_) return false;
        const auto physical = arcade_inputs(controls);
        XBoardGame::Host::Input input;
        input.buttons = physical.buttons;
        input.analog = physical.analog;
        scene_ready=false;
        game.tick(input);
        auto samples = game.take_audio();
        if (extensions.process_audio && !samples.empty()) extensions.process_audio(samples, info());
        if (audio.empty()) audio = std::move(samples);
        else audio.insert(audio.end(), samples.begin(), samples.end());
        if (capture_sprites) presentation_ready = true;
        if (extensions.frame_complete) extensions.frame_complete(frame());
        return true;
    }

    void set_paused(bool paused) { paused_ = paused; }
    bool paused() const { return paused_; }
    void restart() {
        game.restart();
        presentation_ready = false;
        if (capture_sprites) bind_sprite_capture();
        audio.clear();
        paused_ = false;
        if (extensions.restarted) extensions.restarted();
    }
    FrameInfo info() const {
        const auto& board = game.board();
        return {board.frames, board.time()};
    }
    FrameView frame() const { return {info(), width, height, game.pixels()}; }
    // Optional presentation copies. No bus transactions, device writes or
    // replacement native pixels. A newly enabled capture becomes ready after
    // the next completed frame, including when enabled while paused.
    void capture_sprite_presentation(bool enabled) {
        if (enabled == capture_sprites) return;
        capture_sprites = enabled; presentation_ready = false;scene_ready=false;
        if(!enabled)capture_scene=false;
        if (enabled) bind_sprite_capture();
        else { game.board().layers = {}; game.board().rendered = {}; }
    }
    SpritePresentation sprite_presentation() const {
        return {background, sprites, protected_pixels, capture_sprites && presentation_ready};
    }
    void capture_sprite_scene(bool enabled) {
        if(enabled==capture_scene)return;
        capture_scene=enabled;scene_ready=false;
        if(enabled){for(auto& row:scene_rows)row.clear();for(auto& heads:row_heads)heads.fill(UINT32_MAX);presentation_ready=false;assets=std::make_unique<Presentation::EffectAssets>(std::span(rom.maincpu.rom,rom.maincpu.length),std::span(rom.subcpu.rom,rom.subcpu.length));capture_sprite_presentation(true);}
    }
    SpriteScene sprite_scene() {
        if(!capture_scene || !presentation_ready)return {};
        if(!scene_ready){scene_heads.assign(width*height,UINT32_MAX);scene_fragments.clear();
            for(unsigned y=0;y<height;++y){const auto base=uint32_t(scene_fragments.size());
                for(auto f:scene_rows[y]){if(f.previous!=UINT32_MAX)f.previous+=base;scene_fragments.push_back(f);}
                for(unsigned x=0;x<width;++x)if(row_heads[y][x]!=UINT32_MAX)scene_heads[y*width+x]=base+row_heads[y][x];
            }scene_ready=true;
        }
        return {scene_heads,scene_fragments,true};
    }
    void capture_music(bool enabled){game.capture_music(enabled);}
    std::vector<XBoardGame::AudioPresentation> take_music(){return game.take_music();}
    std::vector<int16_t> take_audio() {
        auto result = std::move(audio);
        audio.clear();
        return result;
    }

private:
    XBoardGame game;
    Roms& rom;
    Extensions extensions;
    bool paused_ = false;
    std::vector<int16_t> audio;
    bool capture_sprites{}, presentation_ready{},capture_scene{},scene_ready{};
    std::unique_ptr<Presentation::EffectAssets> assets;
    std::array<std::vector<Presentation::Fragment>,height> scene_rows;
    std::array<std::array<uint32_t,width>,height> row_heads{};
    std::vector<Presentation::Fragment> scene_fragments;
    std::vector<uint32_t> scene_heads;
    std::vector<uint32_t> background, sprites;
    std::vector<uint8_t> protected_pixels;
    static uint32_t argb(RGB888 p) {
        return 0xff000000u | uint32_t(p.r)<<16 | uint32_t(p.g)<<8 | p.b;
    }
    void bind_sprite_capture() {
        for(auto& row:scene_rows)row.clear();for(auto& heads:row_heads)heads.fill(UINT32_MAX);scene_ready=false;
        background.assign(width*height, 0xff000000u);
        sprites.assign(width*height, 0);
        protected_pixels.assign(width*height, 0);
        game.board().layers = [this](unsigned top, unsigned bottom, auto indices, auto priority, const HWPalette& palette) {
            if(capture_scene){
                for(unsigned y=top;y<=bottom;++y){scene_rows[y].clear();row_heads[y].fill(UINT32_MAX);}
                Presentation::decode(game.board().memory.sprite_buffer,std::span(rom.sprites.rom,rom.sprites.length),top,bottom,*assets,
                    [&](unsigned x,unsigned y,uint16_t raw,Presentation::Effect kind){const unsigned i=y*width+x;
                        const bool visible=(1<<((raw>>12)&3))>priority[i];
                        const auto pen=!visible?indices[i]:(raw&0x400f)==0x400a?indices[i]+palette.num_entries():raw&0xfff;
                        auto& row=scene_rows[y];row.push_back({argb(palette.pen(pen)),row_heads[y][x],uint32_t(!visible?Presentation::Effect::background:(raw&0x400f)==0x400a?Presentation::Effect::shadow:kind),raw});
                        row_heads[y][x]=uint32_t(row.size()-1);
                    });
            }
            const auto words = game.board().sprite_words();
            for (unsigned i=top*width; i<(bottom+1)*width; ++i) {
                background[i] = argb(palette.pen(indices[i]));
                const auto pixel = words[i];
                const bool visible=pixel!=0xffff && (1<<((pixel>>12)&3))>priority[i];
                sprites[i] = !visible ? 0u : (pixel&0x400f)==0x400a ? 2u : 1u;
                // Foreground tile pixels and hardware shadows retain their
                // exact native coverage; filtered edges cannot cover them.
                protected_pixels[i] = (!visible && priority[i]!=0) || sprites[i]==2;
            }
        };
        game.board().rendered = [this](uint64_t, unsigned top, unsigned bottom, bool display) {
            const auto& output = game.pixels();
            for (unsigned i=top*width; i<(bottom+1)*width; ++i) {
                if (!display) { if(capture_scene){scene_rows[i/width].clear();row_heads[i/width].fill(UINT32_MAX);}background[i]=0xff000000u; sprites[i]=0; protected_pixels[i]=0; }
                else if (sprites[i]==2) { background[i]=argb(output[i]); sprites[i]=0; }
                else if (sprites[i]) sprites[i]=argb(output[i]);
            }
        };
    }
};
