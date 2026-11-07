#pragma once
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
#include "roms.hpp"
#include "engine/game_session.hpp"
#include "frontend/native_settings.hpp"
#include "frontend/native_input.hpp"
#include "frontend/native_music.hpp"
#include "frontend/native_haptics.hpp"

namespace Frontend {
struct Performance {
    double simulation_fps{}, display_fps{}, frame_ms{}, core_ms{}, present_ms{}, filter_ms{};
};
// Frontend-owned asset cache and UI. Reads ROM graphics only; never writes the
// board's RAM, palette, frame pixels, registers or device clocks.
class NativeMenu {
public:
    enum class Action { none, close, start, restart, practice, shuffle, arcade, quit };
    NativeMenu(SDL_Renderer* renderer, const Roms& rom, Settings settings, bool pixel_art)
        : settings(settings), renderer(nullptr), scale_count(pixel_art?4:3), practice_stages(StageCatalog::count(rom)) {
        rebind(renderer,rom);
    }
    // Release renderer-owned resources before a backend switch. Menu/session
    // state remains intact while the frontend recreates its presentation path.
    void rebind(SDL_Renderer* next_renderer,const Roms& rom) {
        if(!next_renderer){SDL_DestroyTexture(atlas);atlas=nullptr;renderer=nullptr;return;}
        auto* surface=SDL_CreateSurface(128,256,SDL_PIXELFORMAT_ARGB8888);
        if (!surface) throw std::runtime_error(SDL_GetError());
        const auto third=rom.tiles.length/3;
        for (unsigned tile=0;tile<512;++tile) for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
            const auto offset=tile*8+y;
            const unsigned value=(((rom.tiles.rom[offset]>>(7-x))&1)) |
                (((rom.tiles.rom[third+offset]>>(7-x))&1)<<1) |
                (((rom.tiles.rom[2*third+offset]>>(7-x))&1)<<2);
            auto* row=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(surface->pixels)+(tile/16*8+y)*surface->pitch);
            // Font pen 7 is the dark outline/letter counter, not foreground.
            const bool ink=value!=0 && value!=7;
            row[tile%16*8+x]=ink?0xffffffffu:0;
        }
        auto* replacement=SDL_CreateTextureFromSurface(next_renderer,surface); SDL_DestroySurface(surface);
        if (!replacement) throw std::runtime_error(SDL_GetError());
        if(!SDL_SetTextureScaleMode(replacement,SDL_SCALEMODE_NEAREST) || !SDL_SetTextureBlendMode(replacement,SDL_BLENDMODE_BLEND)) {
            SDL_DestroyTexture(replacement);throw std::runtime_error(SDL_GetError());
        }
        SDL_DestroyTexture(atlas);atlas=replacement;renderer=next_renderer;
    }
    ~NativeMenu() { SDL_DestroyTexture(atlas); }
    NativeMenu(const NativeMenu&)=delete;
    Settings settings;
    std::string notice;
    // The frontend owns file dialogs and asynchronous callback lifetime. This
    // callback is invoked only by an explicit selection on the menu thread.
    std::function<void()> choose_shader,import_music,import_shaders;
    std::function<void(const std::string&)> select_imported_shader;
    std::vector<std::string> shader_library;
    std::string import_status;
    void open_shader_library(){visit(Page::shader_library);}
    std::string shader_status;
    NativeInput* input{};
    NativeMusic* music{};AudioOutput* audio{};
    NativeHaptics* feedback{};
    bool custom_hud_available{};
    // A menu edit is only a candidate; F5 keeps the last launched stage.
    unsigned practice_stage{1},active_practice_stage{};
    bool practice_allowed{true};
    bool shuffle_active{},shuffle_allowed{true};
    std::function<bool(const Settings&)> apply;
    enum class Page { root, modes, settings, display, graphics, effects, crt, diagnostics,
        controls, bindings, analog, monitor, feedback, cues, feedback_tests,
        audio, music, volumes, output, arcade, practice, shuffle, shader_library };
    Page page() const {return current;}
    int row_count() const {return int(rows().size());}
    std::string row_label(int row) const {return rows().at(row).label;}
    void open(GameSession& game) {
        if(opened)return;
        if(feedback)feedback->stop();
        was_paused=game.paused();initial=game.info().number==0;
        game.set_paused(true);opened=true;current=Page::root;selected=0;history.clear();scroll=0;
    }
    void close(GameSession& game) {
        if(!opened)return;
        if(feedback)feedback->stop();if(audio)audio->clear();
        if(music)music->stop_preview();if(input)input->cancel();
        game.set_paused(was_paused);opened=false;
    }
    bool is_open() const {return opened;}
    bool in_video() const {return current==Page::display || current==Page::graphics || current==Page::effects || current==Page::crt;}
    bool in_dips() const {return current==Page::arcade;}
    bool in_controls() const {return current==Page::controls || current==Page::bindings || current==Page::analog;}
    bool in_calibration() const {return current==Page::monitor;}
    bool in_options() const {return current==Page::settings || in_video() || current==Page::diagnostics;}
    bool in_practice() const {return current==Page::practice;}
    bool in_shuffle() const {return current==Page::shuffle;}
    int selection() const {return selected;}
    bool fullscreen_toggle() {auto changed=settings;changed.fullscreen=!changed.fullscreen;return change(changed);}
    bool stats_toggle() {auto changed=settings;changed.stats=static_cast<Stats>((int(changed.stats)+1)%3);return change(changed);}
    Action key(SDL_Keycode key) {
        const bool enter=key==SDLK_RETURN || key==SDLK_SPACE || key==SDLK_KP_ENTER;
        if(key==SDLK_ESCAPE || key==SDLK_F1)return back();
        if(input && current==Page::bindings && input->waiting())return Action::none;
        if(current==Page::monitor){if(enter)return back();return Action::none;}
        auto entries=rows();
        if(key==SDLK_UP || key==SDLK_DOWN){
            if(feedback)feedback->stop();notice.clear();
            selected=(selected+int(entries.size())+(key==SDLK_UP?-1:1))%int(entries.size());
            return Action::none;
        }
        const auto row=entries.at(selected);
        if(key==SDLK_BACKSPACE && current==Page::bindings && row.id>=4 && row.id<15){input->clear(row.id-4);return Action::none;}
        if(current==Page::music && key==SDLK_SPACE && row.id<6){
            if(music->previewing() && music->previewing_track()==row.id)music->stop_preview();else music->preview(row.id);
            return Action::none;
        }
        if(!enter && key!=SDLK_LEFT && key!=SDLK_RIGHT)return Action::none;
        if(!row.enabled){notice=row.help;return Action::none;}
        if(row.kind==Kind::back)return enter?back():Action::none;
        if(row.kind==Kind::page){if(enter)visit(row.destination);return Action::none;}
        if(row.kind==Kind::action)return enter?row.action:Action::none;
        const int direction=key==SDLK_LEFT?-1:1;
        if(in_video() || current==Page::diagnostics){
            if(row.id==14){if(enter){if(choose_shader)choose_shader();else notice="SHADER PICKER UNAVAILABLE";}}
            else adjust(row.id,direction);
        } else if(current==Page::shader_library){
            if(enter && row.id==0 && import_shaders)import_shaders();
            else if(enter && row.id>0 && select_imported_shader)select_imported_shader(shader_library.at(row.id-1));
        } else if(current==Page::arcade){
            auto changed=settings;
            if(row.id==0)changed.lives^=1;
            if(row.id==1)changed.difficulty=(changed.difficulty+(direction<0?3:1))%4;
            if(row.id==2)changed.lives^=2;
            change(changed);
        } else if(current==Page::practice){
            practice_stage=(practice_stage-1+practice_stages+direction)%practice_stages+1;
        } else if(current==Page::controls || current==Page::analog || current==Page::bindings){
            if(row.id==0)input->cycle(direction);
            if(row.id==1)input->deadzone(direction);
            if(row.id==2)input->sensitivity(direction);
            if(row.id==3)input->throttle_mode();
            if(enter && row.id>=4 && row.id<15)input->begin(row.id-4);
            if(enter && row.id==15)input->reset();
        } else if(current==Page::feedback || current==Page::cues || current==Page::feedback_tests){
            if(row.id<9 || (row.id>=15&&row.id<=17))feedback->adjust(row.id,direction);
            else if(enter && row.id>=10 && row.id<=13)feedback->test(row.id-10);
            else if(enter && row.id==14)feedback->stop();
        } else if(current==Page::audio){
            if(row.id<2)music->volume(row.id==1,direction);
            else if(enter)music->defaults();
        } else if(current==Page::music || current==Page::volumes){
            if(row.id<6){if(current==Page::music)music->cycle(row.id,direction);else music->track_volume(row.id,direction);}
            else if(enter && row.id==7 && import_music)import_music();
            else if(enter){if(current==Page::music)music->originals();else {music->settings.track_volume.fill(100);music->track_volume(0,0);}}
        } else if(current==Page::output){
            if(row.id==0)audio->cycle_profile(direction);
            if(row.id==1)audio->cycle_device(direction);
            if(row.id==2)audio->cycle_resampler(direction);
            if(enter && row.id==3)audio->speaker_test();
        }
        return Action::none;
    }
    Action pointer(float x,float y,bool click) {
        if(input && current==Page::bindings && input->waiting())return Action::none;
        if(current==Page::monitor)return click?back():Action::none;
        const int first=first_row();
        for(int i=first;i<std::min(row_count(),first+capacity());++i){
            const float top=row_top(i);
            if(x>=24 && x<=304 && y>=top-3 && y<top+spacing()-3){
                if(selected!=i){if(feedback)feedback->stop();notice.clear();}selected=i;return click?key(SDLK_RETURN):Action::none;
            }
        }
        return Action::none;
    }
    void draw() {
        SDL_SetRenderDrawColor(renderer,0,0,0,255);SDL_RenderClear(renderer);
        if(current==Page::monitor){draw_monitor();return;}
        const bool root=current==Page::root;
        if(root && initial){title("AFTER",16,{240,244,255,255});title("BURNER II",51,{240,244,255,255});}
        else {centered(page_title(),16,{255,255,255,255});centered(page_context(),36,{125,153,180,255});}
        const auto entries=rows();
        for(int i=first_row();i<std::min(int(entries.size()),first_row()+capacity());++i){
            const auto& row=entries[i];const float y=row_top(i);
            const SDL_Color color=!row.enabled?SDL_Color{85,99,113,255}:i==selected?SDL_Color{255,255,255,255}:SDL_Color{135,157,181,255};
            text(row.label,32,y,color);
            std::string value=row.value;
            std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return std::toupper(c);});
            text(value.substr(0,14),184,y,row.enabled?SDL_Color{54,184,255,255}:color);
            if(row.kind==Kind::page)text(">",296,y,color);
            if(i==selected)cursor(8,y);
        }
        scrollbar(first_row(),int(entries.size()),capacity(),row_top(first_row()),spacing()*capacity());
        auto help=notice.empty()?entries.at(selected).help:notice;
        if(help.empty())help=page_help();
        // Full device/preset names remain readable without widening the native UI.
        auto detail=entries.at(selected).value;
        std::transform(detail.begin(),detail.end(),detail.begin(),[](unsigned char c){return std::toupper(c);});
        if(detail.size()>14){
            if(detail.size()>38){detail+="    ";const auto offset=(SDL_GetTicks()/250)%detail.size();detail=detail.substr(offset)+detail.substr(0,offset);}
            centered(detail.substr(0,38),184,{54,184,255,255});
        }
        centered(help.substr(0,38),198,{125,153,180,255});
        centered(current==Page::music?"L/R CHANGE  SPACE PREVIEW  ESC BACK":"ENTER SELECT  L/R CHANGE  ESC BACK",214,{73,152,210,255});
    }
    void draw_practice_status() {
        if(!active_practice_stage && !shuffle_active)return;
        const std::string label=shuffle_active?"SHUFFLE":"PRACTICE FROM "+std::to_string(active_practice_stage);
        // Stay below the score strip and above the native bottom instruments.
        const float top=settings.stats==Stats::detailed?74.f:settings.stats==Stats::fps?36.f:28.f;
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer,0,0,0,160);
        SDL_FRect panel{4,top,float(label.size()*8+8),12};SDL_RenderFillRect(renderer,&panel);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
        text(label,8,top+2,{195,219,237,255});
    }
    void draw_stats(const Performance& p,bool paused) {
        if(settings.stats==Stats::off) return;
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer,0,0,0,210);
        SDL_FRect panel{4,4,152,settings.stats==Stats::detailed?65.f:25.f};SDL_RenderFillRect(renderer,&panel);
        SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_NONE);
        char line[48];
        std::snprintf(line,sizeof(line),"SIM %5.1f FPS%s",p.simulation_fps,paused?" P":"");text(line,8,8,{255,255,255,255});
        std::snprintf(line,sizeof(line),"DRAW %5.1f FPS",p.display_fps);text(line,8,18,{55,187,255,255});
        if(settings.stats==Stats::detailed) {
            std::snprintf(line,sizeof(line),"FRAME %5.1f MS",p.frame_ms);text(line,8,28,{255,255,255,255});
            std::snprintf(line,sizeof(line),"CORE  %5.1f MS",p.core_ms);text(line,8,38,{255,255,255,255});
            std::snprintf(line,sizeof(line),"FILTER %5.1f MS",p.filter_ms);text(line,8,48,{255,255,255,255});
            std::snprintf(line,sizeof(line),"PRESENT %4.1f MS",p.present_ms);text(line,8,58,{255,255,255,255});
        }
    }
private:
    // Passive position indicator in native menu coordinates. Its thumb covers
    // the visible rows, including a short final page, and reaches both ends.
    void scrollbar(int first,int rows,int capacity,float top,float height) {
        if(rows<=capacity)return;
        const int shown=std::min(capacity,rows-first);
        const float thumb_height=std::max(8.f,std::round(height*shown/rows));
        const float offset=std::round((height-thumb_height)*first/(rows-shown));
        SDL_FRect track{312,top,3,height},thumb{312,top+offset,3,thumb_height};
        SDL_SetRenderDrawColor(renderer,41,65,90,255);SDL_RenderFillRect(renderer,&track);
        SDL_SetRenderDrawColor(renderer,54,184,255,255);SDL_RenderFillRect(renderer,&thumb);
    }
    enum class Kind {setting,page,action,back};
    struct Row {
        std::string label,value,help;int id{};bool enabled=true;Kind kind=Kind::setting;
        Page destination=Page::root;Action action=Action::none;
    };
    struct Location {Page page;int selected,scroll;};
    Page current=Page::root;
    std::vector<Location> history;
    SDL_Renderer* renderer;
    SDL_Texture* atlas{};
    int scale_count,selected{};
    const unsigned practice_stages;
    bool opened{},was_paused{},initial{};
    mutable int scroll{};
    void visit(Page page){history.push_back({current,selected,scroll});current=page;selected=0;scroll=0;notice.clear();if(page==Page::music)music->refresh();}
    Action back(){
        if(current==Page::root)return Action::close;
        if(feedback)feedback->stop();if(current==Page::output && audio)audio->clear();
        if(current==Page::music && music)music->stop_preview();if(current==Page::bindings && input)input->cancel();
        const auto previous=history.back();history.pop_back();current=previous.page;selected=previous.selected;scroll=previous.scroll;notice.clear();
        return Action::none;
    }
    int capacity()const{return current==Page::root?5:current==Page::settings?8:7;}
    float spacing()const{return current==Page::settings?16.f:17.f;}
    int first_row()const {
        scroll=std::clamp(scroll,0,std::max(0,row_count()-capacity()));
        if(selected<scroll)scroll=selected;
        if(selected>=scroll+capacity())scroll=selected-capacity()+1;
        return scroll;
    }
    float row_top(int row)const{return (current==Page::root?106.f:60.f)+(row-first_row())*spacing();}
    std::string page_title()const {
        switch(current){
        case Page::root:return active_practice_stage?"PAUSED - PRACTICE "+std::to_string(active_practice_stage):shuffle_active?"PAUSED - SHUFFLE":"PAUSED - ARCADE";
        case Page::modes:return "CHANGE MODE";case Page::settings:return "SETTINGS";
        case Page::display:return "DISPLAY";case Page::graphics:return "GRAPHICS";case Page::effects:return "TRANSPARENCY EFFECTS";
        case Page::shader_library:return "IMPORTED SHADER PRESETS";
        case Page::crt:return "CRT AND SHADERS";case Page::diagnostics:return "DIAGNOSTICS";
        case Page::controls:return "CONTROLS";case Page::bindings:return "BUTTON BINDINGS";case Page::analog:return "ANALOG SETTINGS";
        case Page::monitor:return "INPUT MONITOR";case Page::feedback:return "CONTROLLER FEEDBACK";
        case Page::cues:return "FEEDBACK CUES";case Page::feedback_tests:return "FEEDBACK TESTS";
        case Page::audio:return "AUDIO";case Page::music:return "MUSIC REPLACEMENTS";case Page::volumes:return "TRACK VOLUMES";
        case Page::output:return "AUDIO OUTPUT";case Page::arcade:return "ARCADE SETTINGS";
        case Page::practice:return "STAGE SELECT / PRACTICE";case Page::shuffle:return "SHUFFLE MODE";
        }
        return {};
    }
    std::string page_context()const {
        if(current==Page::settings)return "CHOOSE A CATEGORY";
        if(current==Page::practice)return "STARTS A FRESH SESSION";
        if(current==Page::shuffle)return std::to_string(practice_stages-1)+" STAGES THEN FINALE";
        if(current==Page::modes)return "STARTING A MODE ENDS THIS RUN";
        if(current==Page::arcade)return "CHANGES REQUIRE A RESTART";
        if(current==Page::root)return "CURRENT RUN IS PAUSED";
        return "SETTINGS / "+page_title();
    }
    std::string page_help()const {
        if(current==Page::shader_library)return import_status;
        if(current==Page::practice)return "F5 RETRIES ACTIVE PRACTICE STAGE";
        if(current==Page::shuffle)return "F5 RESTARTS THE SAME ORDER";
        if(current==Page::bindings)return input->status.empty()?"ENTER REMAP - BACKSPACE CLEAR":input->status;
        if(current==Page::feedback || current==Page::feedback_tests)return feedback->notice;
        if(current==Page::output)return audio->status().text;
        if(current==Page::audio || current==Page::music || current==Page::volumes)return music->notice;
        return {};
    }
    std::vector<Row> rows()const {
        std::vector<Row> result;
        auto setting=[&](int id,std::string label,std::string value="",std::string help="",bool enabled=true){result.push_back({label,value,help,id,enabled});};
        auto link=[&](std::string label,Page page,std::string help="",bool enabled=true){Row row{label,"",help,0,enabled,Kind::page};row.destination=page;result.push_back(row);};
        auto action=[&](std::string label,Action action,std::string help="",bool enabled=true){Row row{label,"",help,0,enabled,Kind::action};row.action=action;result.push_back(row);};
        auto back=[&]{result.push_back({"BACK","","",0,true,Kind::back});};
        const auto recording="UNAVAILABLE WHILE RECORDING";
        switch(current){
        case Page::root:
            action(initial?"ARCADE":"RESUME",initial?Action::start:Action::close,initial?"INSERT COIN / START IN ARCADE":"");
            if(initial){link("PRACTICE",Page::practice,practice_allowed?"START AT A SELECTED STAGE":recording,practice_allowed);link("SHUFFLE",Page::shuffle,shuffle_allowed?"PLAY STAGES IN RANDOM ORDER":recording,shuffle_allowed);}
            else {action("RESTART RUN",Action::restart,"ENDS AND RESTARTS THE CURRENT RUN");link("CHANGE MODE...",Page::modes,"STARTING A MODE ENDS THIS RUN");}
            link("SETTINGS",Page::settings);action("QUIT",Action::quit);break;
        case Page::modes:
            action("ARCADE",Action::arcade,practice_allowed&&shuffle_allowed?"START A FRESH ARCADE SESSION":recording,practice_allowed&&shuffle_allowed);
            link("PRACTICE",Page::practice,practice_allowed?"START AT A SELECTED STAGE":recording,practice_allowed);
            link("SHUFFLE",Page::shuffle,shuffle_allowed?"PLAY STAGES IN RANDOM ORDER":recording,shuffle_allowed);back();break;
        case Page::settings:
            link("DISPLAY",Page::display);link("GRAPHICS",Page::graphics);link("CRT AND SHADERS",Page::crt);
            if(input)link("CONTROLS",Page::controls);if(music)link("AUDIO",Page::audio);
            link("ARCADE SETTINGS",Page::arcade,"LIVES AND DIFFICULTY REQUIRE RESTART");link("DIAGNOSTICS",Page::diagnostics);back();break;
        case Page::display: {
            const char* scales[]{"NEAREST","SMOOTH","INTEGER","PIXEL ART"};
#ifdef SDL_PLATFORM_ANDROID
            setting(6,"DISPLAY MODE","FULLSCREEN","ANDROID USES THE FULL DISPLAY",false);
#else
            setting(6,"DISPLAY MODE",settings.fullscreen?"FULLSCREEN":"WINDOWED");
#endif
            setting(9,"ASPECT RATIO",viewport_labels[unsigned(settings.viewport)],"WIDE VIEWS ARE EXPERIMENTAL");
            setting(0,"OUTPUT SCALING",scales[int(settings.scaling)]);back();break;
        }
        case Page::graphics:
            setting(1,"SPRITE FILTER",sprite_filter_labels[int(settings.sprites)]);
            setting(10,"HUD STYLE",hud_labels[unsigned(settings.hud)],"TEXT PLANE - NATIVE PLACEMENT");
            setting(8,"GROUND",settings.ground?"CORRECTED":"ORIGINAL");link("TRANSPARENCY",Page::effects);back();break;
        case Page::effects:
            setting(2,"CLOUDS",settings.clouds?"TRANSLUCENT":"OPAQUE");setting(3,"TRAILS",settings.trails?"TRANSLUCENT":"OPAQUE");
            setting(4,"EXPLOSIONS",settings.explosions?"TRANSLUCENT":"OPAQUE");setting(5,"AFTERBURNER",settings.afterburner?"TRANSLUCENT":"OPAQUE","FLAME: FULL THROTTLE ONLY");back();break;
        case Page::crt: {
            const bool retro=settings.crt==Crt::retroarch;
            setting(11,"CRT MODE",crt_labels[unsigned(settings.crt)],shader_status.empty()?"PRESETS RESET BOTH STRENGTHS":shader_status);
            setting(12,"SCANLINES",retro?"IN PRESET":std::to_string(settings.crt_scanlines)+"%",retro?"CONTROLLED BY THE SHADER PRESET":"",!retro);
            setting(13,"RGB MASK",retro?"IN PRESET":std::to_string(settings.crt_mask)+"%",retro?"CONTROLLED BY THE SHADER PRESET":"",!retro);
            setting(14,"SHADER PRESET",settings.shader_preset.empty()?"CHOOSE...":std::filesystem::path(settings.shader_preset).filename().string(),import_shaders?"BROWSE IMPORTED SHADER PACKS":"ENTER CHOOSES A .SLANGP PRESET");
            if(import_shaders)link("SHADER PACKS",Page::shader_library);back();break;
        }
        case Page::diagnostics:{const char* stats[]{"OFF","FPS","DETAILED"};setting(7,"PERFORMANCE",stats[int(settings.stats)],"F3 CYCLES THIS DURING PLAY");back();break;}
        case Page::controls:
            setting(0,"INPUT DEVICE",input->device_name());link("BUTTON BINDINGS",Page::bindings);link("ANALOG SETTINGS",Page::analog);
            link("INPUT MONITOR",Page::monitor,input->controller_selected()?"LIVE RAW AND MAPPED AXES":"SELECT A CONTROLLER FIRST",input->controller_selected());
            if(feedback)link("FEEDBACK",Page::feedback);back();break;
        case Page::analog:
            setting(1,"DEAD ZONE",std::to_string(input->profile().deadzone)+"%");setting(2,"SENSITIVITY",std::to_string(input->profile().sensitivity)+"%");
            setting(3,"THROTTLE",input->raw_device()?(input->profile().full_throttle?"FULL AXIS":"HALF AXES"):"BUTTONS/TRIGGERS");back();break;
        case Page::bindings:
            for(int i=0;i<11;++i)setting(i+4,NativeInput::labels[i],input->binding_name(i));
            setting(15,"RESET CONTROLS","","ENTER RESTORES DEVICE DEFAULTS");back();break;
        case Page::feedback:
            setting(0,"ENABLED",feedback->settings.enabled?"ON":"OFF");setting(1,"OUTPUT DEVICE",feedback->device_name());
            setting(2,"BODY STRENGTH",std::to_string(feedback->settings.body)+"%");setting(3,"TRIGGER STRENGTH",std::to_string(feedback->settings.triggers)+"%");
            link("CUES",Page::cues);link("TEST FEEDBACK",Page::feedback_tests,feedback->capabilities());back();break;
        case Page::cues:{auto& s=feedback->settings;
            const char* labels[]{"CANNON","MISSILES","PLAYER IMPACT","AFTERBURNER","SWAP TRIGGERS"};const int levels[]{s.cannon,s.missile,s.impact,s.boost};
            for(int i=0;i<4;++i)setting(i+4,labels[i],FeedbackSettings::level(levels[i]));setting(15,"LANDING",FeedbackSettings::level(s.landing));setting(16,"TAKEOFF",FeedbackSettings::level(s.takeoff));setting(17,"REFUELING",FeedbackSettings::level(s.refueling));setting(8,labels[4],s.swap?"ON":"OFF");back();break;}
        case Page::feedback_tests:
            setting(10,"TEST LOW BODY");setting(11,"TEST HIGH BODY");setting(12,"TEST LEFT TRIGGER");setting(13,"TEST RIGHT TRIGGER");setting(14,"STOP ALL","",feedback->capabilities());back();break;
        case Page::audio:
            setting(0,"MUSIC VOLUME",std::to_string(music->settings.music)+"%");setting(1,"EFFECTS VOLUME",std::to_string(music->settings.effects)+"%");
            if(audio)link("OUTPUT SETUP",Page::output);link("MUSIC TRACKS",Page::music);link("TRACK VOLUMES",Page::volumes);setting(4,"RESET VOLUMES");back();break;
        case Page::music:case Page::volumes:
            for(int i=0;i<6;++i)setting(i,music_titles[i],current==Page::music?music->selection(i):std::to_string(music->settings.track_volume[i])+"%");
            setting(6,current==Page::music?"ALL ORIGINAL":"RESET VOLUMES");
            if(current==Page::music && import_music)setting(7,"IMPORT WAV / FLAC");back();break;
        case Page::shader_library:
            setting(0,"IMPORT SHADER PACK ZIP");
            for(unsigned i=0;i<shader_library.size();++i) {
                auto marker=shader_library[i].find("/shaders/");
                auto name=marker==std::string::npos?std::filesystem::path(shader_library[i]).filename().string():shader_library[i].substr(marker+9);
                // Use the existing uppercase value field and scrolling detail;
                // arbitrary provider names must not become ROM-font labels.
                setting(int(i)+1,"PRESET "+std::to_string(i+1),name);
            }
            back();break;
        case Page::output: {
            setting(0,"PROFILE",audio_profiles[int(audio->settings.profile)]);
            auto device=audio->settings.profile==AudioProfile::original?"SDL DEFAULT":audio->settings.device.empty()?"DEFAULT":audio->settings.device;
            if(device.starts_with("OpenAL Soft on "))device.erase(0,15);
            setting(1,"DEVICE",device);setting(2,"INTERPOLATION",audio->settings.profile==AudioProfile::original?"NATIVE":audio->settings.resampler.empty()?"DEFAULT":audio->settings.resampler);
            setting(3,"SPEAKER TEST");back();break;
        }
        case Page::arcade:
            setting(0,"LIVES/CREDIT",lives_labels[settings.lives%2]);setting(1,"DIFFICULTY",difficulty_labels[settings.difficulty]);setting(2,"SHIP INCREASE",settings.lives>=2?"YES":"NO");
            action("APPLY AND RESTART",Action::restart,"RESTART ENDS THE CURRENT GAME");back();break;
        case Page::practice:
            setting(0,"STAGE",std::to_string(practice_stage)+" / "+std::to_string(practice_stages),practice_allowed?"NORMAL LIVES / DAMAGE":recording,practice_allowed);
            action("START PRACTICE",Action::practice,practice_allowed?"STARTS A FRESH SESSION":recording,practice_allowed);
            action("RETURN TO ARCADE",Action::arcade,practice_allowed?"STARTS A FRESH ARCADE SESSION":recording,practice_allowed&&(active_practice_stage||shuffle_active));back();break;
        case Page::shuffle:
            action(shuffle_active?"NEW SHUFFLE":"START SHUFFLE",Action::shuffle,shuffle_allowed?"NEW SHUFFLE STARTS A FRESH RUN":recording,shuffle_allowed);
            action("RETURN TO ARCADE",Action::arcade,shuffle_allowed?"STARTS A FRESH ARCADE SESSION":recording,shuffle_allowed&&shuffle_active);back();break;
        case Page::monitor:back();break;
        }
        return result;
    }
    void draw_monitor(){
        centered("INPUT MONITOR",16,{255,255,255,255});
        if(!input->controller_selected()){centered("CONTROLLER DISCONNECTED",96,{255,255,255,255});centered("ENTER OR ESC TO RETURN",208,{125,153,180,255});return;}
        auto name=input->device_name();std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return std::toupper(c);});centered(name.substr(0,38),38,{54,184,255,255});
        const auto sample=input->calibration();const char* labels[]{"STICK X","STICK Y","THROTTLE"};
        const float raw[]{sample.raw.stick_x,sample.raw.stick_y,sample.raw.throttle},mapped[]{sample.mapped.stick_x,sample.mapped.stick_y,sample.mapped.throttle};
        text("RAW",151,59,{135,157,181,255});text("MAPPED",216,59,{54,184,255,255});
        for(int i=0;i<3;++i){const int y=82+i*35;text(labels[i],24,y,{255,255,255,255});text(std::to_string(int(std::round(raw[i]*100))),151,y,{135,157,181,255});text(std::to_string(int(std::round(mapped[i]*100))),224,y,{54,184,255,255});
            SDL_SetRenderDrawColor(renderer,65,75,88,255);SDL_RenderLine(renderer,24,y+17,296,y+17);SDL_SetRenderDrawColor(renderer,54,184,255,255);SDL_RenderLine(renderer,160,y+17,160+int(std::round(std::clamp(mapped[i],-1.f,1.f)*136)),y+17);}
        centered("DEAD ZONE "+std::to_string(input->profile().deadzone)+"%  SENSITIVITY "+std::to_string(input->profile().sensitivity)+"%",192,{125,153,180,255});centered("ENTER OR ESC TO RETURN",208,{125,153,180,255});
    }
    bool change(const Settings& changed) {
        if (apply && !apply(changed)) {notice="SETTING UNAVAILABLE";return false;}
        settings=changed;return true;
    }
    // Presentation IDs keep the existing setting adjustments independent of page order.
    void adjust(int id,int direction) {
        auto changed=settings;
        if(id==0) changed.scaling=static_cast<Scaling>((static_cast<int>(settings.scaling)+scale_count+direction)%scale_count);
        if(id==1) changed.sprites=static_cast<SpriteFilter>((static_cast<int>(settings.sprites)+sprite_filter_count+direction)%sprite_filter_count);
        if(id==2) changed.clouds=!settings.clouds;
        if(id==3) changed.trails=!settings.trails;
        if(id==4) changed.explosions=!settings.explosions;
        if(id==5) changed.afterburner=!settings.afterburner;
        if(id==6) changed.fullscreen=!settings.fullscreen;
        if(id==7) changed.stats=static_cast<Stats>((static_cast<int>(settings.stats)+3+direction)%3);
        if(id==8) changed.ground=!settings.ground;
        if(id==9)changed.viewport=static_cast<Viewport>((static_cast<int>(settings.viewport)+viewport_count+direction)%viewport_count);
        if(id==10){const unsigned count=custom_hud_available?hud_count:2;
            changed.hud=static_cast<Hud>((static_cast<unsigned>(settings.hud)+count+direction)%count);}
        if(id==11){
            changed.crt=static_cast<Crt>((static_cast<int>(settings.crt)+int(crt_count)+direction)%int(crt_count));
            if(changed.crt==Crt::subtle || changed.crt==Crt::arcade){changed.crt_scanlines=changed.crt==Crt::arcade?50:25;changed.crt_mask=changed.crt==Crt::arcade?35:15;}
        }
        if(id==12 || id==13){
            if(changed.crt==Crt::retroarch)return;
            auto& strength=id==12?changed.crt_scanlines:changed.crt_mask;
            strength=unsigned(std::clamp(int(strength)+direction*5,0,100));
            if(changed.crt==Crt::off)changed.crt=Crt::subtle;
        }
        if(id<14) change(changed);
    }
    void tile(unsigned code,float x,float y,SDL_Color color,float scale=1) {
        SDL_SetTextureColorMod(atlas,color.r,color.g,color.b);
        SDL_FRect source{float(code%16*8),float(code/16*8),8,8},dest{x,y,8*scale,8*scale};
        SDL_RenderTexture(renderer,atlas,&source,&dest);
    }
    void text(const std::string& value,float x,float y,SDL_Color color,float scale=1) {
        for(unsigned char ch:value) { if(ch!=' ')tile(ch,x,y,color,scale);x+=8*scale; }
    }
    void centered(const std::string& value,float y,SDL_Color color) {text(value,160-value.size()*4,y,color);}
    void title(const std::string& value,float y,SDL_Color color) {
        float x=160-value.size()*12;
        unsigned index=0;
        for(unsigned char ch:value) {
            const auto ink=value=="BURNER II" && index>=7?SDL_Color{255,62,48,255}:color;
            if(ch!=' ') tile(ch,x,y,ink,3);
            x+=24;
            ++index;
        }
    }
    void cursor(float x,float y) {tile('*',x+8,y,{65,183,255,255});}

};
inline bool pixel_art_available() {
#if SDL_VERSION_ATLEAST(3,4,0)
    return SDL_GetVersion()>=SDL_VERSIONNUM(3,4,0);
#else
    return false;
#endif
}
inline bool apply_texture_settings(SDL_Texture* texture,const Settings& settings) {
    SDL_ScaleMode scale=settings.scaling==Scaling::linear?SDL_SCALEMODE_LINEAR:SDL_SCALEMODE_NEAREST;
#if SDL_VERSION_ATLEAST(3,4,0)
    if(settings.scaling==Scaling::pixel_art) scale=SDL_SCALEMODE_PIXELART;
#endif
    return SDL_SetTextureScaleMode(texture,scale);
}
inline bool apply_settings(SDL_Window* window,SDL_Renderer* renderer,SDL_Texture* texture,
                           const Settings& settings) {
#ifdef SDL_PLATFORM_ANDROID
    constexpr bool fullscreen=true;
#else
    const bool fullscreen=settings.fullscreen;
#endif
    return apply_texture_settings(texture,settings) &&
        SDL_SetRenderLogicalPresentation(renderer,viewport_width(settings),224,settings.scaling==Scaling::integer?
            SDL_LOGICAL_PRESENTATION_INTEGER_SCALE:SDL_LOGICAL_PRESENTATION_LETTERBOX) &&
        (((SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN)!=0)==fullscreen ||
         (SDL_SetWindowFullscreen(window,fullscreen) && SDL_SyncWindow(window)));
}
}
