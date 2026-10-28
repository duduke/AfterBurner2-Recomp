// Standalone authored stages. Native board execution is used only at asset
// loading to resolve ROM road palettes; it never controls this simulation.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "frontend/custom_stage_view.hpp"
#include "frontend/custom_runway_art.hpp"
#include "frontend/custom_stage_audio.hpp"
#include "frontend/custom_stage_variants.hpp"
#include "frontend/native_input.hpp"
#include "frontend/controller_database.hpp"
#include "engine/native_stage_skip.hpp"
#include <charconv>
#include <filesystem>
#include <iostream>
#include <memory>

namespace {
const char* names[]={"refuel","landing","runway","runway-jungle","runway-takeoff","carrier-takeoff","jungle-takeoff"};
const char* definitions[]={"refuel","carrier-landing","runway-landing","jungle-landing","runway-takeoff","carrier-takeoff","jungle-takeoff"};
using Stage=Frontend::CustomStage;using Definition=Frontend::CustomStageDefinition;
using Texture=std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)>;
}
int main(int argc,char** argv)try{
    unsigned selected=0,frames=0;bool demo=false,hold=false,no_sprites=false,no_scenery=false,mute=false;std::string source,screenshot,file,audio_file;
    uint64_t seed=0;bool seeded=false;
    for(int i=1;i<argc;++i){std::string_view arg=argv[i];
        if(arg=="--mute"){mute=true;continue;}
        if(arg=="--no-scenery"){no_scenery=true;continue;}
        if(arg=="--demo"){demo=true;continue;}if(arg=="--demo-hold"){demo=hold=true;continue;}if(arg=="--no-sprites"){no_sprites=true;continue;}
        if(i+1==argc)throw std::runtime_error("Missing option value");std::string value=argv[++i];
        if(arg=="--stage"){unsigned j=0;for(;j<7 && value!=names[j];++j){}if(j==7)throw std::runtime_error("Unknown custom stage");selected=j;}
        else if(arg=="--stage-file")file=value;else if(arg=="--roms")source=value;else if(arg=="--screenshot")screenshot=value;else if(arg=="--audio-wav")audio_file=value;
        else if(arg=="--seed"){auto result=std::from_chars(value.data(),value.data()+value.size(),seed);if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size())throw std::runtime_error("Seed must be an unsigned integer");seeded=true;}
        else if(arg=="--frames"){auto result=std::from_chars(value.data(),value.data()+value.size(),frames);if(result.ec!=std::errc{} || result.ptr!=value.data()+value.size() || !frames)throw std::runtime_error("Frames must be positive");}
        else throw std::runtime_error("Unknown option: "+std::string(arg));
    }
    if(!audio_file.empty()&&!frames)throw std::runtime_error("Audio capture requires --frames");
    if(!screenshot.empty()&&!frames)throw std::runtime_error("Screenshot requires --frames");
    if(no_sprites&&!source.empty())throw std::runtime_error("Choose ROM art or --no-sprites");
    auto definition=[&]{return Definition::load(file.empty()?std::string("assets/custom_stages/")+definitions[selected]+".json":file);};
    Stage stage(definition());
    if(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO|SDL_INIT_GAMEPAD|SDL_INIT_JOYSTICK))throw std::runtime_error(SDL_GetError());
    struct Quit{~Quit(){SDL_Quit();}} quit;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("After Burner / Custom stages",1152,720,SDL_WINDOW_RESIZABLE),SDL_DestroyWindow);
    if(!window)throw std::runtime_error(SDL_GetError());
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> renderer(SDL_CreateRenderer(window.get(),nullptr),SDL_DestroyRenderer);
    if(!renderer)throw std::runtime_error(SDL_GetError());
    SDL_SetRenderLogicalPresentation(renderer.get(),960,600,SDL_LOGICAL_PRESENTATION_LETTERBOX);SDL_SetRenderDrawBlendMode(renderer.get(),SDL_BLENDMODE_BLEND);
    Frontend::load_controller_database();Frontend::NativeInput input("");
    std::unique_ptr<Roms> rom;std::unique_ptr<Frontend::InteractiveStageSprites> sprites;
    if(!no_sprites && (!source.empty() || std::filesystem::exists("roms/epr-11107.58") || std::filesystem::exists("roms/aburner2.zip"))){
        RomLoader::set_source(source);rom=std::make_unique<Roms>();if(!rom->load_aburner2())throw std::runtime_error("Cannot load ROM set; use --roms or --no-sprites");
        sprites=std::make_unique<Frontend::InteractiveStageSprites>(renderer.get(),*rom,true);
    }
    if(!seeded){std::random_device entropy;seed=(uint64_t(entropy())<<32)^entropy();}
    Frontend::CustomStageVariantPicker variant_picker(seed);
    const auto tanker_variants=rom?Frontend::native_tanker_variants(*rom):std::vector<unsigned>{};
    unsigned tanker_variant=0;
    std::array<Texture,4> materials={Texture(nullptr,SDL_DestroyTexture),Texture(nullptr,SDL_DestroyTexture),Texture(nullptr,SDL_DestroyTexture),Texture(nullptr,SDL_DestroyTexture)};
    std::array<Texture,4> runways={Texture(nullptr,SDL_DestroyTexture),Texture(nullptr,SDL_DestroyTexture),Texture(nullptr,SDL_DestroyTexture),Texture(nullptr,SDL_DestroyTexture)};
    std::array<SDL_Color,4> sky_tops{},sky_edges{};
    Frontend::CustomStageScene scene(renderer.get());
    Frontend::CustomStageView view{renderer.get(),sprites.get(),&scene};view.show_scenery=!no_scenery;
    auto prepare=[&]{
        const auto env=stage.definition.environment;const auto i=unsigned(env);view.stage_number=file.empty()?selected+1:1;
        tanker_variant=0;
        if(stage.refuel() && stage.definition.rom_environment && rom){
            tanker_variant=variant_picker.pick(tanker_variants,stage.definition.tanker_variant);
            // This is a complete native environment, not mixed assets from the
            // preceding attempt. Clear both its material and scenery together.
            view.material=nullptr;materials[3].reset();sprites->reset_tanker_scenery();
        }
        scene.load(stage.definition);
        if(rom && stage.definition.rom_environment && !materials[i]){
            view.draw(stage,false);view.text(278,444,"LOADING ROM TERRAIN MATERIALS",Frontend::CustomStageView::amber);SDL_RenderPresent(renderer.get());
            // Prepare the ordinary aerial environment, then retain only its
            // sprite/text-free hardware road image. No sequence movie is used.
            auto bootstrap=std::make_unique<XBoardGame>(*rom);
            bool collect=false;std::vector<uint32_t> carrier_material;
            const HWPalette* refuel_palette=nullptr;
            std::vector<uint32_t> background_column(224),carrier_column;
            std::array<uint32_t,16> horizon_colors{},carrier_horizon_colors{};
            // Column 16 is outside native HUD text. The pre-sprite background
            // supplies sky fill; the horizon sprite palette supplies the gradient.
            bootstrap->board().layers=[&](unsigned top,unsigned bottom,auto indices,auto,const HWPalette& palette){
                refuel_palette=&palette;
                // Main $10E6E / GroundHorizon: palette E8 is the native
                // horizon SPRITE gradient (pen 1 sky, pen 14 ground).
                for(unsigned pen=1;pen<15;++pen){const auto c=palette.pen(0xe80+pen);
                    horizon_colors[pen]=0xff000000u|(unsigned(c.r)<<16)|(unsigned(c.g)<<8)|c.b;}
                for(unsigned y=top;y<=bottom;++y){const auto c=palette.pen(indices[y*320+16]&0x1fff);
                    background_column[y]=0xff000000u|(unsigned(c.r)<<16)|(unsigned(c.g)<<8)|c.b;}};
            auto harvest=[&]{if(!collect)return;
                for(unsigned slot=0;slot<256;++slot){const auto p=0xffffa000u+slot*64;
                    const auto flags=NativeStageSkip::peek16(bootstrap->board(),p);if(!(flags&0x8000))continue;
                    const auto pc=NativeStageSkip::peek32(bootstrap->board(),p+16);
                    if(pc!=0x8af2 && pc!=0x7650)continue;
                    const auto root=NativeStageSkip::peek32(bootstrap->board(),p+12);
                    const auto palette=NativeStageSkip::peek16(bootstrap->board(),p+2)&255;
                    const float x=int16_t(NativeStageSkip::peek16(bootstrap->board(),p+42));
                    const float z=int16_t(NativeStageSkip::peek16(bootstrap->board(),p+36));
                    sprites->cache_scenery(i,root,palette,x,z,(flags&0x2000)!=0,pc==0x7650);
                }
            };
            auto step=[&](GameControls c={}){auto pins=arcade_inputs(c);XBoardGame::Host::Input p;p.buttons=pins.buttons;p.analog=pins.analog;bootstrap->tick(p);bootstrap->take_audio();harvest();SDL_PumpEvents();};
            for(unsigned f=0;f<600;++f){GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;step(c);
                if(env==Definition::Environment::carrier && f%30==0 && NativeStageSkip::peek16(bootstrap->board(),0x29c09c)==15 && NativeStageSkip::peek16(bootstrap->board(),0xffff90b0)==0){carrier_material=bootstrap->terrain_material();carrier_column=background_column;carrier_horizon_colors=horizon_colors;}}
            NativeStageSkip::enter(bootstrap->board(),*rom,env==Definition::Environment::runway?5:env==Definition::Environment::jungle?13:env==Definition::Environment::sea?tanker_variant:1,[&]{step();});
            // Original flyby aircraft observed under Main $7650. Sea stages
            // keep airborne scenery; land objects are never placed on water.
            if(env==Definition::Environment::carrier || env==Definition::Environment::sea)
                sprites->cache_scenery(i,0x1e7e0,0xcc,-768,4096,false,true);
            collect=true;harvest();
            for(unsigned f=0;f<30;++f)step();
            if(env==Definition::Environment::sea){
                bool settled=NativeStageSkip::peek16(bootstrap->board(),0xffff90e0)==3;
                for(unsigned f=0;f<700 && !settled;++f){step();settled=NativeStageSkip::peek16(bootstrap->board(),0xffff90e0)==3;}
                if(!settled)throw std::runtime_error("Original refueling palette frontier not reached");
                if(refuel_palette)sprites->refuel_colors(*refuel_palette);
            }
            auto pixels=carrier_material.empty()?bootstrap->terrain_material():std::move(carrier_material);
            if(env==Definition::Environment::runway || env==Definition::Environment::jungle){
                // The aerial view may contain only sky in the road layer.
                // Resolve the ground palette at the ROM's parked frontier,
                // keeping the aerial sky material sampled above.
                bool parked=false;
                for(unsigned f=0;f<1800 && !parked;++f){step();
                    for(unsigned slot=0;slot<256;++slot){const auto p=0xffffa000u+slot*64;
                        if(NativeStageSkip::peek32(bootstrap->board(),p+16)==0x1122e && NativeStageSkip::peek16(bootstrap->board(),p+40)==8){parked=true;break;}}
                }
                if(!parked)throw std::runtime_error("ROM runway material frontier not reached");
                sprites->begin_native_layout(i);
                for(unsigned slot=0;slot<256;++slot){const auto p=0xffffa000u+slot*64;
                    const auto flags=NativeStageSkip::peek16(bootstrap->board(),p);if(!(flags&0x8000) || NativeStageSkip::peek32(bootstrap->board(),p+16)!=0x8af2)continue;
                    sprites->native_prop(i,NativeStageSkip::peek32(bootstrap->board(),p+12),NativeStageSkip::peek16(bootstrap->board(),p+2)&255,
                        int16_t(NativeStageSkip::peek16(bootstrap->board(),p+42)),int16_t(NativeStageSkip::peek16(bootstrap->board(),p+36)),(flags&0x2000)!=0);
                }
                const auto ground=bootstrap->terrain_material();
                const auto runway=Frontend::CustomRunwayArt::extract(ground);
                runways[i].reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,Frontend::CustomRunwayArt::width,Frontend::CustomRunwayArt::height));
                if(!runways[i] || !SDL_UpdateTexture(runways[i].get(),nullptr,runway.data(),Frontend::CustomRunwayArt::width*4))throw std::runtime_error(SDL_GetError());
                SDL_SetTextureScaleMode(runways[i].get(),SDL_SCALEMODE_NEAREST);
                pixels=ground;
            }
            if(sprites->hud() && refuel_palette){std::array<uint16_t,40*28> native_hud{};
                for(unsigned y=0;y<28;++y)for(unsigned x=0;x<40;++x)native_hud[y*40+x]=NativeStageSkip::peek16(bootstrap->board(),0x0d0000+2*(y*64+x+24));
                sprites->hud()->native(native_hud,*refuel_palette);
            }
            auto rgb=[](uint32_t v){return SDL_Color{Uint8(v>>16),Uint8(v>>8),Uint8(v),255};};
            const auto& sky_column=carrier_column.empty()?background_column:carrier_column;
            sky_tops[i]=rgb(sky_column[0]);unsigned brightest=0;
            const auto& gradient=carrier_column.empty()?horizon_colors:carrier_horizon_colors;
            for(unsigned pen=1;pen<15;++pen){const auto c=rgb(gradient[pen]);const unsigned light=unsigned(c.r)+c.g+c.b;
                if(light>brightest){brightest=light;sky_edges[i]=c;}}
            if(env==Definition::Environment::sea)for(unsigned y=0;y<224;++y)
                std::fill(pixels.begin()+y*320,pixels.begin()+(y+1)*320,y>=160?horizon_colors[14]:background_column[y]);
            // Pure road output retains sky below the sprite horizon. Start
            // water at our horizon using the native ground-facing pen instead.
            if(env==Definition::Environment::carrier)for(unsigned y=106;y<224;++y)
                std::fill(pixels.begin()+y*320,pixels.begin()+(y+1)*320,carrier_horizon_colors[14]);
            materials[i].reset(SDL_CreateTexture(renderer.get(),SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,320,224));
            if(!materials[i] || !SDL_UpdateTexture(materials[i].get(),nullptr,pixels.data(),320*4))throw std::runtime_error(SDL_GetError());
            SDL_SetTextureScaleMode(materials[i].get(),SDL_SCALEMODE_NEAREST);
        }
        view.material=materials[i].get();view.runway_art=runways[i].get();
        if(materials[i]){view.sky_top=sky_tops[i];view.sky_edge=sky_edges[i];}
    };
    prepare();
    std::unique_ptr<Frontend::CustomStageAudio> audio;
    if(!frames || !audio_file.empty())audio=std::make_unique<Frontend::CustomStageAudio>(rom.get(),!frames,audio_file,mute);
    if(demo)stage.start();bool running=true,paused=false;unsigned count=0;double previous=SDL_GetTicksNS()/1.e9,debt=0;
    while(running){SDL_Event event;
        while(SDL_PollEvent(&event)){input.event(event);SDL_Keycode key=0;
            if(event.type==SDL_EVENT_QUIT)running=false;
            if(event.type==SDL_EVENT_WINDOW_FOCUS_LOST && !frames){paused=true;input.block();if(audio)audio->clear();}
            if(event.type==SDL_EVENT_KEY_DOWN && !event.key.repeat)key=event.key.key;
            if(event.type==SDL_EVENT_GAMEPAD_BUTTON_DOWN){switch(event.gbutton.button){
                case SDL_GAMEPAD_BUTTON_START:key=stage.phase==Stage::Phase::briefing || stage.finished()?SDLK_RETURN:SDLK_P;break;
                case SDL_GAMEPAD_BUTTON_SOUTH:key=SDLK_RETURN;break;case SDL_GAMEPAD_BUTTON_NORTH:key=SDLK_TAB;break;
                case SDL_GAMEPAD_BUTTON_WEST:key=SDLK_H;break;case SDL_GAMEPAD_BUTTON_BACK:key=SDLK_ESCAPE;break;default:break;}}
            if(key==SDLK_ESCAPE)running=false;if(key==SDLK_H)view.assistance=!view.assistance;
            if(key==SDLK_M && audio)audio->mute();
            if(key==SDLK_P){paused=!paused;input.block();if(audio)audio->clear();}
            if(key==SDLK_TAB){selected=(selected+1)%7;file.clear();stage=Stage(definition());prepare();paused=false;input.block();debt=0;previous=SDL_GetTicksNS()/1.e9;}
            if(key==SDLK_R || key==SDLK_F5){stage=Stage(definition());prepare();paused=false;input.block();debt=0;previous=SDL_GetTicksNS()/1.e9;}
            if(key==SDLK_RETURN){if(paused)paused=false;else if(stage.finished()){stage=Stage(definition());prepare();debt=0;previous=SDL_GetTicksNS()/1.e9;}else stage.start();input.block();}
        }
        if(!running)break;
        const double now=SDL_GetTicksNS()/1.e9;
        debt=frames?Stage::dt:paused?0:std::min(.1,debt+std::min(.1,now-previous));previous=now;
        const auto control=[&]{auto c=input.read(paused);c.stick_y=-c.stick_y;return c;}();
        while(debt+1.e-10>=Stage::dt){if(!paused){stage.tick(demo && !(hold && stage.phase==Stage::Phase::ready)?stage.demo():control);if(audio)audio->tick(stage);}debt-=Stage::dt;}
        if(!frames || count+1>=frames)view.draw(stage,paused);
        if(frames && ++count>=frames){
            if(!screenshot.empty()){std::unique_ptr<SDL_Surface,decltype(&SDL_DestroySurface)> image(SDL_RenderReadPixels(renderer.get(),nullptr),SDL_DestroySurface);
                if(!image || !SDL_SaveBMP(image.get(),screenshot.c_str()))throw std::runtime_error(SDL_GetError());}
            std::cout<<"Custom stage: "<<stage.definition.name<<", phase="<<int(stage.phase)<<", ticks="<<stage.ticks<<", range="<<stage.range<<", height="<<stage.height<<", speed="<<stage.speed<<", fuel="<<stage.fuel<<", scene_seed="<<seed<<", tanker_variant="<<tanker_variant<<", scenery_sprites="<<(sprites?sprites->scenery_count(unsigned(stage.definition.environment)):0)<<", trees_drawn="<<view.trees_drawn<<", waves_drawn="<<view.waves_drawn<<", native_props="<<(sprites?sprites->native_prop_count(unsigned(stage.definition.environment)):0)<<", hud_tiles="<<view.hud_tiles<<", game_font="<<view.game_font<<", clouds_drawn="<<view.clouds_drawn<<", sky_aircraft="<<view.sky_aircraft<<", coupled="<<stage.coupled<<", probe_error_px="<<view.probe_error_px<<", missiles="<<stage.missiles<<", flame_error_px="<<(sprites?sprites->last_flame_error:0)<<", audio_energy="<<(audio?audio->energy:0)<<", rom_audio_energy="<<(audio?audio->rom_energy:0)<<", reload_sounds="<<(audio?audio->reload_events:0)<<", launch_sounds="<<(audio?audio->launch_events:0)<<", touchdown_sounds="<<(audio?audio->touchdown_events:0)<<", custom_layers="<<stage.definition.layers.size()<<", custom_drawn="<<scene.drawn<<", custom_tiles="<<scene.tiles_drawn<<", player_shadow="<<view.shadow_drawn<<", smoke_puffs="<<view.smoke_puffs<<", band_height="<<Frontend::CustomSceneryField::band_height<<", scenery_drawn="<<view.scenery_drawn<<", original_runway="<<view.runway_rows<<", carrier_art="<<view.carrier_drawn<<", burner="<<stage.burner()<<", "<<stage.message<<'\n';running=false;
        }
        if(!frames || !running)SDL_RenderPresent(renderer.get());if(!frames)SDL_Delay(4);
    }
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
