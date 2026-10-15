#pragma once

#include "engine/game_controls.hpp"
#include "engine/feedback_cues.hpp"
#include "engine/xboard_game.hpp"
#include "hwvideo/sprite_scene.hpp"
#include "hwvideo/ground_horizon.hpp"
#include "hwvideo/scene_snapshot.hpp"
#include "hwvideo/wide_visibility.hpp"
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
        scene_ready=false;if(capture_wide)wide_clips.clear();
        game.tick(input);
        if(capture_feedback_)feedback_frame=feedback.take(info().number,info().time_ns,feedback_word(0x29c09c),feedback_word(0x29c0d2));
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
        if(visibility)visibility->reset();
        feedback.clear();feedback_frame={};if(capture_feedback_)bind_feedback_capture();
        presentation_ready = false;
        wide_clips.clear();wide_pending.reset();if(capture_wide){bind_wide_capture();bind_feedback_capture();}
        if (capture_sprites) bind_sprite_capture();
        audio.clear();
        paused_ = false;
        if (extensions.restarted) extensions.restarted();
    }
    // Physical DIP pins may be configured only before cold-board execution.
    void set_dip_switches(std::array<uint8_t,3> switches) {
        if (info().number) throw std::runtime_error("DIP switches require a fresh board");
        game.board().switches = switches;
    }
    std::array<uint8_t,3> dip_switches() const { return game.board().switches; }
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
        else { game.board().layers = {}; game.board().rendered = {}; if(capture_wide)bind_wide_capture(); }
    }
    SpritePresentation sprite_presentation() const {
        return {background, sprites, protected_pixels, capture_sprites && presentation_ready};
    }
    void capture_ground_horizon(bool enabled) {
        if(capture_ground==enabled)return;
        capture_ground=enabled;
        if(enabled){ground=std::make_unique<Presentation::GroundHorizon>(std::span(rom.maincpu.rom,rom.maincpu.length),std::span(rom.subcpu.rom,rom.subcpu.length));
            if(!assets)assets=std::make_unique<Presentation::EffectAssets>(std::span(rom.maincpu.rom,rom.maincpu.length),std::span(rom.subcpu.rom,rom.subcpu.length));}
        else ground.reset();
        if(enabled && !capture_sprites)capture_sprite_presentation(true);
        else if(capture_sprites){presentation_ready=false;bind_sprite_capture();}
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
    struct ScenePresentation {FrameInfo info;std::span<const Presentation::SceneClip> clips;bool ready{};};
    void capture_wide_scene(bool enabled){
        if(enabled==capture_wide)return;capture_wide=enabled;wide_clips.clear();wide_pending.reset();
        if(enabled){if(!visibility)visibility=std::make_unique<Presentation::WideVisibility>(rom);bind_wide_capture();}else {game.board().rendering_started={};if(!capture_sprites)game.board().rendered={};if(visibility)visibility->reset();}
        bind_feedback_capture();
    }
    ScenePresentation wide_scene()const{return {info(),wide_clips,capture_wide&&!wide_clips.empty()};}
    void capture_feedback(bool enabled) {
        if(enabled==capture_feedback_)return;
        capture_feedback_=enabled;feedback.clear();feedback_frame={};
        bind_feedback_capture();
    }
    Feedback::Frame take_feedback(){auto f=std::move(feedback_frame);feedback_frame={};return f;}
    void capture_music(bool enabled){game.capture_music(enabled);}
    void capture_cockpit(bool enabled){game.capture_cockpit(enabled);}
    std::vector<XBoardGame::AudioPresentation> take_music(){return game.take_music();}
    std::vector<int16_t> take_audio() {
        auto result = std::move(audio);
        audio.clear();
        return result;
    }

private:
    std::unique_ptr<Presentation::WideVisibility> visibility;
    bool capture_wide{};
    std::vector<Presentation::SceneClip> wide_clips;
    std::unique_ptr<Presentation::SceneClip> wide_pending;
    void publish_wide(uint64_t ns,unsigned top,unsigned bottom,bool display){
        if(!capture_wide || !wide_pending)return;
        wide_pending->ns=ns;wide_pending->top=top;wide_pending->bottom=bottom;wide_pending->display=display;
        wide_clips.push_back(std::move(*wide_pending));wide_pending.reset();
    }
    void bind_wide_capture(){
        game.board().rendering_started=[this]{
            auto p=std::make_unique<Presentation::SceneClip>();const auto& m=game.board().memory;const auto bytes=std::span(m.bytes);
            Presentation::SceneClip::words(p->tiles,bytes.subspan(MainBus::TILE,0x10000));
            Presentation::SceneClip::words(p->text,bytes.subspan(MainBus::TEXT,0x1000));
            Presentation::SceneClip::words(p->palette,bytes.subspan(MainBus::PALETTE,0x4000));
            p->road=m.road_buffer;p->sprites=m.sprite_buffer;p->road_control=m.road_control;p->registers=game.video_registers();
            if(visibility)p->extended_sprites=visibility->descriptors();
            p->mode=feedback_word(0x29c09c);p->throttle=feedback_word(0x29c0d2);p->roll=feedback_word(0x29c09e);
            wide_pending=std::move(p);
        };
        if(!capture_sprites)game.board().rendered=[this](auto ns,auto top,auto bottom,auto display){publish_wide(ns,top,bottom,display);};
    }
    Feedback::Capture feedback;Feedback::Frame feedback_frame;bool capture_feedback_{};
    uint16_t feedback_word(uint32_t a) const {const auto o=MainBus::memory_offset(a);const auto& m=game.board().memory.bytes;return uint16_t(m[o]<<8|m[o+1]);}
    void bind_feedback_capture(){if(!capture_feedback_&&!capture_wide){game.board().phase={};return;}game.board().phase=[this](const auto& p){if(capture_wide&&visibility)visibility->observe(p,game.board());if(capture_feedback_&&!p.cpu && p.write && (p.pc==0x9ef6 || p.pc==0x9ebc || p.pc==0x9c20))feedback.observe(p,feedback_word(0x29c09c)==0x10);};}
    XBoardGame game;
    Roms& rom;
    Extensions extensions;
    bool paused_ = false;
    std::vector<int16_t> audio;
    bool capture_sprites{}, presentation_ready{},capture_scene{},scene_ready{},capture_ground{};
    std::unique_ptr<Presentation::GroundHorizon> ground;
    std::vector<uint16_t> ground_indices;
    std::vector<uint32_t> ground_shadows;
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
        if(capture_ground)ground_shadows.assign(width*height,0);else ground_shadows.clear();
        game.board().layers = [this](unsigned top, unsigned bottom, auto indices, auto priority, const HWPalette& palette) {
            if(capture_ground){
                ground_indices.assign(indices.begin(),indices.end());
                const auto peek=[&](uint32_t a){const auto o=MainBus::memory_offset(a);return uint16_t(game.board().memory.bytes[o]<<8|game.board().memory.bytes[o+1]);};
                if(peek(0x29c09c)==0x10)ground->apply(ground_indices,priority,game.board().memory.road_buffer,game.board().memory.road_control,
                    game.board().memory.sprite_buffer,std::span(rom.sprites.rom,rom.sprites.length),top,bottom,uint8_t(peek(0x29c09e)),*assets);
                indices=std::span<const uint16_t>(ground_indices);
            }
            if(capture_ground)for(unsigned i=top*width;i<(bottom+1)*width;++i)ground_shadows[i]=argb(palette.pen(indices[i]+palette.num_entries()));
            if(capture_scene){
                // Passive RAM observation; no CPU/bus/device transactions.
                const auto peek=[&](uint32_t address){const auto offset=MainBus::memory_offset(address);return uint16_t(game.board().memory.bytes[offset]<<8|game.board().memory.bytes[offset+1]);};
                const bool full_throttle=Presentation::full_throttle_flame(peek(0x29c09c),peek(0x29c0d2));
                for(unsigned y=top;y<=bottom;++y){scene_rows[y].clear();row_heads[y].fill(UINT32_MAX);}
                Presentation::decode(game.board().memory.sprite_buffer,std::span(rom.sprites.rom,rom.sprites.length),top,bottom,*assets,
                    [&](unsigned x,unsigned y,uint16_t raw,Presentation::Effect kind){const unsigned i=y*width+x;
                        if(kind==Presentation::Effect::afterburner && !full_throttle)kind=Presentation::Effect::opaque;
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
        game.board().rendered = [this](uint64_t ns, unsigned top, unsigned bottom, bool display) {
            publish_wide(ns,top,bottom,display);
            const auto& output = game.pixels();
            for (unsigned i=top*width; i<(bottom+1)*width; ++i) {
                if (!display) { if(capture_scene){scene_rows[i/width].clear();row_heads[i/width].fill(UINT32_MAX);}background[i]=0xff000000u; sprites[i]=0; protected_pixels[i]=0; }
                else if (sprites[i]==2) { background[i]=capture_ground?ground_shadows[i]:argb(output[i]); sprites[i]=0; }
                else if (sprites[i]) sprites[i]=argb(output[i]);
            }
        };
    }
};
