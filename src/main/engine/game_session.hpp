#pragma once

#include "engine/game_controls.hpp"
#include "engine/feedback_cues.hpp"
#include "engine/xboard_game.hpp"
#include "hwvideo/sprite_scene.hpp"
#include "hwvideo/ground_horizon.hpp"
#include "hwvideo/scene_snapshot.hpp"
#include "hwvideo/wide_visibility.hpp"
#include "engine/native_stage_skip.hpp"
#include "engine/stage_catalog.hpp"
#include "engine/shuffle_rom.hpp"
#include "engine/custom_stage_rom.hpp"
#include "engine/custom_stage_interactive.hpp"
#include "engine/interactive_rearm.hpp"
#include "engine/custom_stage_boss.hpp"
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

    unsigned stage_count() const { return StageCatalog::count(rom); }
    StageCatalog::Stage stage(unsigned number) const { return StageCatalog::get(rom, number); }

    // Explicit directed Practice entry, after fresh physical coin/start boot.
    // This retains first-stage accumulated state, not natural progression.
    // The optional callback drains hidden startup output and pumps the frontend;
    // it must not advance, pause or restart the session.
    void enter_practice_stage(unsigned stage, const std::function<void()>& after_frame = {}) {
        if (shuffled() || custom_stage_active())
            throw std::runtime_error("Practice entry requires a canonical board restart");
        StageCatalog::get(rom, stage); // Reject invalid choices before execution.
        NativeStageSkip::enter(game.board(), rom, stage, [&] {
            advance({});
            if (after_frame) after_frame(); else take_audio();
        });
    }
    // Compatibility for existing directed sweeps and the --stage debug option.
    void debug_skip_to_stage(unsigned stage) { enter_practice_stage(stage); }

    // One physical frame per advance, independent of display refresh. Pausing
    // admits no inputs and advances no CPU, audio device or gameplay timer.
    bool advance(const GameControls& controls) {
        if (paused_) return false;
        const auto physical = arcade_inputs(controls);
        XBoardGame::Host::Input input;
        input.buttons = physical.buttons;
        input.analog = physical.analog;
        scene_ready=false;if(capture_wide)wide_clip_count=0;
        game.tick(input);
        if (custom_stage_view && custom_stage_view->sprite_overrides.pending())
            update_custom_sprites();
        if (custom_stage_controller_ && !entering_custom_stage_)
            custom_stage_controller_->sync_board(game.board(), active_rom(), controls);
        if (!entering_custom_stage_) {
            rearm_.carrier = custom_stage_in_slot() && custom_stage_view->definition().carrier_landing;
            rearm_.sync(game.board().memory, active_rom(), controls);
            if (boss_ && custom_stage_in_slot()) boss_->sync(game.board().memory, custom_stage_tick(), controls);
        }
        if(primary_completed)primary_completed(info().number);
        if(capture_feedback_){
            feedback_frame=feedback.take(info().number,info().time_ns,feedback_word(0x29c09c),feedback_word(0x29c0d2),
                feedback.player_record()?(uint32_t(feedback_word(feedback.player_record()+16))<<16)|feedback_word(feedback.player_record()+18):0,feedback_word(0xffff9124));
            const auto base=feedback.sequences.base_record,finale=feedback.sequences.finale_record;
            feedback_frame.motion=Feedback::SequenceCapture::state(feedback_word(0x29c09c),feedback_word(0xffff90b0),
                feedback_word(0x29c0aa),feedback_word(0xffff90ae),bool(feedback_word(0xffff90fe)&0x100),
                (base&&(feedback_word(base)&0x8000))?(uint32_t(feedback_word(base+16))<<16)|feedback_word(base+18):0,
                base?feedback_word(base+40):0,base&&(feedback_word(base+60)&0x100),base?feedback_word(base+54):0,
                (finale&&(feedback_word(finale)&0x8000))?(uint32_t(feedback_word(finale+16))<<16)|feedback_word(finale+18):0,
                finale?feedback_word(finale+24):0);
            feedback_frame.sequence_events=feedback.sequences.take();
        }
        if(capture_object_audio_)object_audio_frame=object_audio.frame(info().number,info().time_ns,[this](auto a){return feedback_word(a);});
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
    // Ordinary restart restores the canonical ROM data and original cold
    // board defaults. Practice uses this boundary before directed startup.
    void restart() {
        const bool changed_rom = shuffled() || custom_stage_active();
        game.restart(rom); // Drop the bound host before releasing its ROM view.
        shuffle_view.reset();
        shuffle_plan_.reset();
        custom_stage_view.reset();
        boss_.reset();
        custom_stage_controller_.reset();
        restarted(changed_rom);
    }
    void start_shuffle(const Shuffle::Plan& plan) {
        // Validate and allocate the complete next run before mutating the
        // current board, pause state, outputs or successful Shuffle plan.
        auto next_plan = std::make_unique<Shuffle::Plan>(plan);
        auto next_view = std::make_unique<Shuffle::RomView>(rom, *next_plan);
        const auto switches = dip_switches();
        game.restart(next_view->rom());
        shuffle_view = std::move(next_view);
        shuffle_plan_ = std::move(next_plan);
        custom_stage_view.reset();
        boss_.reset();
        custom_stage_controller_.reset();
        game.board().switches = switches;
        restarted(true);
    }
    void start_custom_stage(const CustomStage::Definition& definition) {
        auto next_view = std::make_unique<CustomStage::RomView>(rom, definition);
        auto next_controller = std::make_unique<CustomStage::InteractiveController>(definition);
        next_controller->start();
        const auto switches = dip_switches();
        game.restart(next_view->rom());
        custom_stage_view = std::move(next_view);
        custom_stage_controller_ = std::move(next_controller);
        boss_.reset();
        if (custom_stage_view->definition().boss)
            boss_ = std::make_unique<CustomStage::BossFight>(*custom_stage_view->definition().boss);
        shuffle_view.reset();
        shuffle_plan_.reset();
        game.board().switches = switches;
        restarted(true);
    }
    bool custom_stage_active() const { return bool(custom_stage_view); }
    // Player-flown landings and tanker hook-ups (see interactive_rearm.hpp).
    void set_interactive_rearm(bool on) { if (rearm_.enabled == on) return; rearm_.enabled = on; rearm_.reset(); }
    bool interactive_rearm() const { return rearm_.enabled; }
    const Rearm::Hud& rearm_hud() const { return rearm_.hud(); }
    const CustomStage::BossFight::View* boss_view() const {
        return boss_ && custom_stage_in_slot() && boss_->view().active ? &boss_->view() : nullptr;
    }
    // True while the run is inside the slot the custom stage was compiled into.
    // Completing it continues the run into the following arcade stage.
    bool custom_stage_in_slot() const {
        return custom_stage_view && feedback_word(0x29c060) + 1u == custom_stage_view->definition().slot;
    }
    // Same timer the stage's event stream is compared against ($29C006 >> 4).
    uint16_t custom_stage_tick() const {
        return uint16_t(((uint32_t(feedback_word(0x29c006)) << 16) | feedback_word(0x29c008)) >> 4);
    }
    // Live values a keyed sky needs each frame. Colors follow any palette fade,
    // so the key still matches and the image can dim along with the scene.
    struct SkyState {
        bool active{};
        // While a stage transition blends the old sky toward the key, sky pixels
        // carry the live blended color, so both values count as sky.
        RGB888 key{}, blending_key{}, fill{};
        float brightness{1.f};
        int16_t camera_x{}, camera_y{};
        uint8_t roll{};
    };
    SkyState custom_stage_sky() const {
        SkyState state;
        if (!custom_stage_in_slot() || !custom_stage_view->definition().sky_keyed()) return state;
        const uint16_t expected = CustomStage::RomView::sky_key_color().rgb555();
        uint16_t live = feedback_word(0x122f00);
        for (const uint32_t address : {0x122f00u, 0x122e40u, 0x121d02u, 0x122f02u})
            if (feedback_word(address) == expected) { live = expected; break; }
        const auto weight = [](uint16_t w) {
            return ((w >> 12) & 1) + ((w >> 13) & 1) + ((w >> 14) & 1) +
                   ((w & 15) << 1) + (((w >> 4) & 15) << 1) + (((w >> 8) & 15) << 1);
        };
        HWPalette dac(1);
        dac.write(0, live);
        state.active = true;
        state.key = dac.pen(0);
        dac.write(0, feedback_word(0x122f00));
        state.blending_key = dac.pen(0);
        dac.write(0, custom_stage_view->sky_fill_word());
        state.fill = dac.pen(0);
        state.brightness = std::clamp(float(weight(live)) / float(weight(expected)), 0.f, 1.f);
        state.camera_x = int16_t(feedback_word(0x29c08a));
        state.camera_y = int16_t(feedback_word(0x29c088));
        state.roll = uint8_t(feedback_word(0x29c09e));
        return state;
    }
    const CustomStage::Definition* custom_stage_definition() const {
        return custom_stage_view ? &custom_stage_view->definition() : nullptr;
    }
    CustomStage::InteractiveController* custom_stage_controller() {
        return custom_stage_controller_.get();
    }
    const CustomStage::InteractiveController* custom_stage_controller() const {
        return custom_stage_controller_.get();
    }
    void retry_custom_stage() {
        if (!custom_stage_view)
            throw std::runtime_error("Custom stage retry requires an active custom stage");
        const auto def = custom_stage_view->definition();
        start_custom_stage(def);
    }
    // Advance a freshly started custom stage past the Stage 1 carrier takeoff
    // cutscene ($29C09C == 15) and into the target custom stage slot ($29C09C == 16),
    // or stop at carrier catapult state 2 when the custom stage itself is a
    // Stage 1 carrier-takeoff sequence.
    void enter_custom_stage(const std::function<void()>& after_frame = {}) {
        if (!custom_stage_view)
            throw std::runtime_error("Custom stage entry requires an active custom stage");
        const auto& def = custom_stage_view->definition();
        struct RestoreEntering {
            bool& flag;
            ~RestoreEntering() { flag = false; }
        } restore{entering_custom_stage_};
        entering_custom_stage_ = true;

        auto step_frame = [&] {
            advance({});
            if (after_frame) after_frame(); else take_audio();
        };

        if (def.slot == 1u &&
            def.objective == CustomStage::Definition::Objective::takeoff &&
            def.environment == CustomStage::Definition::Environment::carrier) {
            for (unsigned f = 0; f < 1800u; ++f) {
                if (feedback_word(0x29c09c) == 15u && feedback_word(0xffff90b0) >= 2u)
                    break;
                step_frame();
            }
            if (feedback_word(0x29c09c) != 15u)
                throw std::runtime_error("Carrier takeoff sequence not reached after cold start");
        } else {
            NativeStageSkip::enter(game.board(), active_rom(), def.slot, step_frame);
        }
        if (custom_stage_controller_) {
            custom_stage_controller_->reset();
            custom_stage_controller_->start();
        }
        if (boss_) boss_->reset();
    }
    // ROM data the board is currently running from. Presentation code must read
    // sprite pixels through this, since a custom stage can rewrite its own copy.
    const Roms& current_rom() const {
        return custom_stage_view ? custom_stage_view->rom() : shuffle_view ? shuffle_view->rom() : rom;
    }
    std::span<const uint8_t> published_sprites() const { return game.board().memory.sprite_buffer; }
    // Called whenever a restart changes which ROM view is current.
    std::function<void(const Roms&)> rom_changed;
    bool shuffled() const { return bool(shuffle_view); }
    const Shuffle::Plan* shuffle_plan() const { return shuffle_plan_.get(); }
    unsigned shuffle_position() const {
        if(!shuffle_plan_ || prepared_shuffle_position==UINT32_MAX)return 0;
        const unsigned position=feedback_word(0x29c060);
        return position<shuffle_plan_->stages.size()?position+1:0;
    }
    unsigned shuffled_stage() const {
        const unsigned position=shuffle_position();
        return position?shuffle_plan_->stages[position-1]:0;
    }
    void retry_shuffle() {
        if (!shuffle_plan_) throw std::runtime_error("Shuffle retry requires an active run");
        // start_shuffle copies this plan before replacing the active owner.
        start_shuffle(*shuffle_plan_);
    }
    // Physical DIP pins may be configured only before cold-board execution.
    void set_dip_switches(std::array<uint8_t,3> switches) {
        if (info().number) throw std::runtime_error("DIP switches require a fresh board");
        game.board().switches = switches;
    }
    std::array<uint8_t,3> dip_switches() const { return game.board().switches; }
    struct RunStatus { uint16_t mode{}, lives{}; uint32_t score{}; };
    RunStatus run_status() const {
        // Passive observations already used by the natural-credit harness:
        // $CF46 mode/lives, and $17610's ranking score source.
        return {feedback_word(0x29c09c), feedback_word(0xffff88e0),
                (uint32_t(feedback_word(0xffff8884)) << 16) | feedback_word(0xffff8886)};
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
        else { game.board().layers = {}; game.board().rendered = {}; if(capture_wide)bind_wide_capture(); }
    }
    SpritePresentation sprite_presentation() const {
        return {background, sprites, protected_pixels, capture_sprites && presentation_ready};
    }
    void capture_ground_horizon(bool enabled) {
        if(capture_ground==enabled)return;
        capture_ground=enabled;
        if(enabled){auto& source=active_rom();ground=std::make_unique<Presentation::GroundHorizon>(std::span(source.maincpu.rom,source.maincpu.length),std::span(source.subcpu.rom,source.subcpu.length));
            if(!assets)assets=std::make_unique<Presentation::EffectAssets>(std::span(source.maincpu.rom,source.maincpu.length),std::span(source.subcpu.rom,source.subcpu.length));}
        else ground.reset();
        if(enabled && !capture_sprites)capture_sprite_presentation(true);
        else if(capture_sprites){presentation_ready=false;bind_sprite_capture();}
    }
    void capture_sprite_scene(bool enabled) {
        if(enabled==capture_scene)return;
        capture_scene=enabled;scene_ready=false;
        if(enabled){for(auto& row:scene_rows)row.clear();for(auto& heads:row_heads)heads.fill(UINT32_MAX);presentation_ready=false;auto& source=active_rom();assets=std::make_unique<Presentation::EffectAssets>(std::span(source.maincpu.rom,source.maincpu.length),std::span(source.subcpu.rom,source.subcpu.length));capture_sprite_presentation(true);}
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
    // Synchronous partial-frame consumer. The clip is captured before native
    // feedback; the borrowed primary layers are valid only during the callback.
    // Snapshots can be retained for comparison. In compact mode the tile array
    // is omitted (tile_gen=0), one metadata slot is reused and wide_scene() is
    // unavailable. The optional completion callback runs after the board tick,
    // before frame/audio extensions. Without it, finish after advance() returns.
    using PrimarySceneConsumer=std::function<void(const Presentation::SceneClip&,uint64_t,const Screen::ViewportLayers&)>;
    using PrimaryFrameConsumer=std::function<void(uint64_t)>;
    void capture_wide_scene(bool enabled,unsigned logical_width=398,PrimarySceneConsumer consumer={},bool retain_snapshots=true,PrimaryFrameConsumer completed={}){
        if(logical_width<320||logical_width>640||logical_width%2)throw std::invalid_argument("Unsupported wide capture width");
        if(enabled&&!consumer&&!retain_snapshots)throw std::invalid_argument("Compact capture requires a primary scene consumer");
        if(enabled&&completed&&!consumer)throw std::invalid_argument("Primary frame completion requires a scene consumer");
        if(enabled==capture_wide&&(!enabled||logical_width==wide_width)&&!primary_consumer&&!consumer)return;
        if(enabled&&(!visibility||logical_width!=wide_width))visibility=std::make_unique<Presentation::WideVisibility>(active_rom(),(logical_width-320)/2);
        game.board().integrated_viewport(enabled&&consumer?logical_width:0);
        primary_consumer=enabled?std::move(consumer):PrimarySceneConsumer{};
        primary_completed=enabled?std::move(completed):PrimaryFrameConsumer{};
        retain_wide_snapshots=!enabled||retain_snapshots;
        if(enabled)wide_width=logical_width;
        capture_wide=enabled;wide_clip_count=0;wide_clip_open=false;
        if(enabled){bind_wide_capture();}else {game.board().rendering_started={};if(!capture_sprites)game.board().rendered={};if(visibility)visibility->reset();}
        bind_feedback_capture();
    }
    ScenePresentation wide_scene()const{
        if(!retain_wide_snapshots)return {info(),{},false};
        return {info(),std::span<const Presentation::SceneClip>(wide_clips.data(),wide_clip_count),capture_wide&&wide_clip_count!=0};
    }
    void capture_feedback(bool enabled) {
        if(enabled==capture_feedback_)return;
        capture_feedback_=enabled;feedback.clear();feedback_frame={};
        bind_feedback_capture();
    }
    Feedback::Frame take_feedback(){auto f=std::move(feedback_frame);feedback_frame={};return f;}
    void capture_music(bool enabled){if(!enabled)capture_audio_objects(false);game.capture_music(enabled);}
    void capture_audio_objects(bool enabled){
        if(enabled==capture_object_audio_)return;
        capture_object_audio_=enabled;object_audio_frame={};
        if(enabled){object_audio.configure({active_rom().soundcpu.rom,active_rom().soundcpu.length},{active_rom().maincpu.rom,active_rom().maincpu.length});object_audio.prime([this](auto a){return feedback_word(a);});}
        else object_audio.reset(info().time_ns);
        game.track_audio_objects(enabled?&object_audio:nullptr);bind_feedback_capture();
    }
    void capture_cockpit(bool enabled){game.capture_cockpit(enabled);capture_audio_objects(enabled);}
    ObjectAudio::Frame take_audio_objects(){auto f=std::move(object_audio_frame);object_audio_frame={};return f;}
    std::vector<ObjectAudio::Event> take_audio_object_events(){return object_audio.take_events(info().time_ns);}
    std::vector<XBoardGame::AudioPresentation> take_music(){return game.take_music();}
    std::vector<int16_t> take_audio() {
        auto result = std::move(audio);
        audio.clear();
        return result;
    }

private:
    // Destruction is reverse declaration order: the bound host and all
    // presentation helpers are gone before their owned runtime ROM storage.
    std::unique_ptr<Shuffle::RomView> shuffle_view;
    std::unique_ptr<Shuffle::Plan> shuffle_plan_;
    std::unique_ptr<CustomStage::RomView> custom_stage_view;
    std::unique_ptr<CustomStage::InteractiveController> custom_stage_controller_;
    Rearm::Controller rearm_;
    std::unique_ptr<CustomStage::BossFight> boss_;
    std::unique_ptr<Presentation::WideVisibility> visibility;
    unsigned prepared_shuffle_position{UINT32_MAX};
    bool capture_wide{}, wide_clip_open{};
    PrimarySceneConsumer primary_consumer;
    PrimaryFrameConsumer primary_completed;
    bool retain_wide_snapshots{true};
    unsigned wide_width{398};
    size_t wide_clip_count{};
    std::vector<Presentation::SceneClip> wide_clips;
    void publish_wide(uint64_t ns,unsigned top,unsigned bottom,bool display){
        if(!capture_wide || !wide_clip_open || !wide_clip_count)return;
        auto& clip=wide_clips[wide_clip_count-1];
        clip.ns=ns;clip.top=top;clip.bottom=bottom;clip.display=display;
        wide_clip_open=false;
        if(primary_consumer){
            const auto* layers=game.board().integrated_layers();
            if(!layers)throw std::runtime_error("Primary scene layers unavailable");
            primary_consumer(clip,game.board().frames+1,*layers);
        }
    }
    void bind_wide_capture(){
        // A restart creates a fresh host. Reinstall its optional viewport before
        // binding callbacks; no CPU, scheduler or device clock is advanced here.
        game.board().integrated_viewport(primary_consumer?wide_width:0);
        game.board().rendering_started=[this]{
            const auto idx=retain_wide_snapshots?wide_clip_count:0;
            if(idx==wide_clips.size())wide_clips.emplace_back();
            wide_clip_count=idx+1;
            auto& p=wide_clips[idx];const auto& h=game.board();const auto& m=h.memory;const auto bytes=std::span(m.bytes);
            if(!retain_wide_snapshots)p.tile_gen=0;
            else if(p.tile_gen!=h.tile_gen){
                if(idx>0&&wide_clips[idx-1].tile_gen==h.tile_gen)p.tiles=wide_clips[idx-1].tiles;
                else Presentation::SceneClip::words(p.tiles,bytes.subspan(MainBus::TILE,0x10000));
                p.tile_gen=h.tile_gen;
            }
            Presentation::SceneClip::words(p.text,bytes.subspan(MainBus::TEXT,0x1000));
            if(p.palette_gen!=h.palette_gen){
                if(idx>0&&wide_clips[idx-1].palette_gen==h.palette_gen)p.palette=wide_clips[idx-1].palette;
                else Presentation::SceneClip::words(p.palette,bytes.subspan(MainBus::PALETTE,0x4000));
                p.palette_gen=h.palette_gen;
            }
            p.road=m.road_buffer;p.sprites=m.sprite_buffer;p.road_control=m.road_control;p.registers=game.video_registers();
            if(visibility)p.extended_sprites=visibility->descriptors();else p.extended_sprites.clear();
            p.mode=feedback_word(0x29c09c);p.throttle=feedback_word(0x29c0d2);p.roll=feedback_word(0x29c09e);
            wide_clip_open=true;
        };
        if(!capture_sprites)game.board().rendered=[this](auto ns,auto top,auto bottom,auto display){publish_wide(ns,top,bottom,display);};
    }
    ObjectAudio::Tracking object_audio;ObjectAudio::Frame object_audio_frame;bool capture_object_audio_{};
    Feedback::Capture feedback;Feedback::Frame feedback_frame;bool capture_feedback_{};
    uint16_t feedback_word(uint32_t a) const {const auto o=MainBus::memory_offset(a);const auto& m=game.board().memory.bytes;return uint16_t(m[o]<<8|m[o+1]);}
    Roms& active_rom() {
        return custom_stage_view ? custom_stage_view->rom() : shuffle_view ? shuffle_view->rom() : rom;
    }
    void restarted(bool changed_rom) {
        prepared_shuffle_position = UINT32_MAX;
        rearm_.reset();
        entering_custom_stage_ = false;
        if (changed_rom) {
            auto& source = active_rom();
            if(visibility)visibility=std::make_unique<Presentation::WideVisibility>(source,(wide_width-320)/2);
            if(ground)ground=std::make_unique<Presentation::GroundHorizon>(std::span(source.maincpu.rom,source.maincpu.length),std::span(source.subcpu.rom,source.subcpu.length));
            if(assets)assets=std::make_unique<Presentation::EffectAssets>(std::span(source.maincpu.rom,source.maincpu.length),std::span(source.subcpu.rom,source.subcpu.length));
            if(rom_changed)rom_changed(source);
        } else if(visibility)visibility->reset();
        feedback.clear();feedback_frame={};object_audio_frame={};
        if(capture_object_audio_){object_audio.configure({active_rom().soundcpu.rom,active_rom().soundcpu.length},{active_rom().maincpu.rom,active_rom().maincpu.length});object_audio.prime([this](auto a){return feedback_word(a);});game.track_audio_objects(&object_audio);}
        presentation_ready = scene_ready = false;
        wide_clip_count=0;wide_clip_open=false;
        if(capture_wide)bind_wide_capture();
        bind_feedback_capture();
        if(capture_sprites)bind_sprite_capture();
        audio.clear();
        paused_=false;
        if(extensions.restarted)extensions.restarted();
    }
    // Writes external sprite art the first time the game draws each frame, using
    // the palette that request names. Bus writes reuse the untraced path below.
    void update_custom_sprites() {
        auto& host=game.board();
        CustomStage::SpriteOverrides::PaletteAccess palette;
        palette.read=[this](unsigned index){
            CustomStage::SpriteOverrides::PaletteWords words{};
            for(unsigned i=0;i<16;++i)words[i]=feedback_word(0x120000u+index*32u+i*2u);
            return words;
        };
        palette.write=[&host](unsigned index,const CustomStage::SpriteOverrides::PaletteWords& words){
            struct RestoreTrace {
                MainBus& bus; MainBus::Trace* previous;
                ~RestoreTrace(){bus.trace=previous;}
            } restore{host.bus,host.bus.trace};
            host.bus.trace=nullptr;
            for(unsigned i=1;i<15;++i)host.bus.write16(0x120000u+index*32u+i*2u,words[i]);
            ++host.palette_gen;
        };
        if(custom_stage_view->sprite_overrides.observe(host.memory.sprite_buffer,custom_stage_view->rom(),palette))
            game.refresh_sprite_rom();
    }
    void prepare_shuffle_palette(const XBoardGame::Host::Phase& phase) {
        if((!shuffle_view && !custom_stage_view) || phase.cpu || !phase.entry)return;
        const auto pc=phase.entry->state.pc;
        if(pc!=0x13ce8 && pc!=0x138fe)return;
        // $138FE completes cold/new-credit initialization separately from
        // normal stage advancement. The ROM has reloaded these palette banks
        // even when another credit starts at a previously observed ordinal.
        if(pc==0x138fe)prepared_shuffle_position=UINT32_MAX;
        const unsigned position=feedback_word(0x29c060);
        const unsigned stage_limit=shuffle_plan_?unsigned(shuffle_plan_->stages.size()):stage_count();
        if(position>=stage_limit || position==prepared_shuffle_position)return;
        const bool needed=custom_stage_view?custom_stage_view->needs_alternate_palette(position):shuffle_view->needs_alternate_palette(position);
        const bool custom_primary = custom_stage_view && !needed &&
            position + 1u == custom_stage_view->definition().slot &&
            (custom_stage_view->definition().custom_ocean || custom_stage_view->definition().sky_keyed());
        if(!needed && !custom_primary){
            prepared_shuffle_position=position;
            return;
        }
        // Both audited completion boundaries follow selection of the stage's
        // descriptor, event program and ordinary palette. The typed arranged
        // preparation must add no CPU-owned transactions.
        auto& host=game.board();
        {
            struct RestoreTrace {
                MainBus& bus; MainBus::Trace* previous;
                ~RestoreTrace(){bus.trace=previous;}
            } restore{host.bus,host.bus.trace};
            host.bus.trace=nullptr;
            if(needed)
                Shuffle::prepare_alternate_palette(host.bus,active_rom(),position);
            else
                CustomStage::prepare_primary_ocean_palette(host.bus,active_rom(),position);
        }
        ++host.palette_gen;
        prepared_shuffle_position=position;
    }
    // Sequence cues need carrier/base/nozzle and sound-latch writes excluded by
    // the sprite presentation filter. Use the existing complete passive observer
    // while feedback is enabled; this does not change CPU or device execution.
    void bind_feedback_capture(){if(!capture_feedback_&&!capture_object_audio_&&!capture_wide&&!shuffle_view&&!custom_stage_view){game.board().phase={};game.board().presentation_phase=false;return;}game.board().presentation_phase=!(capture_feedback_||capture_object_audio_);game.board().phase=[this](const auto& p){prepare_shuffle_palette(p);if(capture_object_audio_)object_audio.observe_main(p,feedback_word(0x29c09c),[this](auto a){return feedback_word(a);});if(capture_wide&&visibility)visibility->observe(p,game.board());if(capture_feedback_&&!p.cpu && p.write && (p.pc==0x9ef6 || p.pc==0x9ebc || p.pc==0x9c20))feedback.observe(p,feedback_word(0x29c09c)==0x10,feedback_word(0x29c0b8));if(capture_feedback_&&Feedback::SequenceCapture::interesting(p))feedback.sequences.observe(p,feedback_word(0x29c09c),bool(feedback_word(0xffff90fe)&0x100));};}
    XBoardGame game;
    Roms& rom;
    Extensions extensions;
    bool paused_ = false;
    bool entering_custom_stage_ = false;
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
                    game.board().memory.sprite_buffer,std::span(current_rom().sprites.rom,current_rom().sprites.length),top,bottom,uint8_t(peek(0x29c09e)),*assets);
                indices=std::span<const uint16_t>(ground_indices);
            }
            if(capture_ground)for(unsigned i=top*width;i<(bottom+1)*width;++i)ground_shadows[i]=argb(palette.pen(indices[i]+palette.num_entries()));
            if(capture_scene){
                // Passive RAM observation; no CPU/bus/device transactions.
                const auto peek=[&](uint32_t address){const auto offset=MainBus::memory_offset(address);return uint16_t(game.board().memory.bytes[offset]<<8|game.board().memory.bytes[offset+1]);};
                const bool full_throttle=Presentation::full_throttle_flame(peek(0x29c09c),peek(0x29c0d2));
                for(unsigned y=top;y<=bottom;++y){scene_rows[y].clear();row_heads[y].fill(UINT32_MAX);}
                Presentation::decode(game.board().memory.sprite_buffer,std::span(current_rom().sprites.rom,current_rom().sprites.length),top,bottom,*assets,
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
