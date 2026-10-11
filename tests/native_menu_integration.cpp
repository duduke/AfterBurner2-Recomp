#include "frontend/native_menu.hpp"
#include <cstring>
#include <iostream>
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
int main() try {
    require(roms.load_aburner2(),"ROMs required");
    require(SDL_Init(SDL_INIT_VIDEO),SDL_GetError());
    auto* window=SDL_CreateWindow("Menu integration",960,672,0);
    require(window,"window unavailable");
    auto* renderer=SDL_CreateRenderer(window,"software");require(renderer,"renderer unavailable");
    auto* texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,320,224);
    require(texture,"texture unavailable");
    {
        GameSession game(roms);require(game.advance({}),"initial game advance failed");game.take_audio();
        const auto before=game.info();const auto source=game.frame().pixels;
        const std::vector<RGB888> pixels(source.begin(),source.end());
        Frontend::NativeMenu menu(renderer,roms,{},Frontend::pixel_art_available());
        menu.apply=[&](const Frontend::Settings& s){return Frontend::apply_settings(window,renderer,texture,s);};
        menu.open(game);require(game.paused(),"opening menu did not freeze board");
        require(!game.advance({}) && game.info().number==before.number,"menu admitted a physical board frame");
        menu.key(SDLK_DOWN);menu.key(SDLK_RETURN);require(menu.in_options(),"options navigation failed");
        menu.key(SDLK_LEFT);menu.key(SDLK_RIGHT); // wrap filter selection without invalid enum
        menu.key(SDLK_RIGHT);require(menu.settings.scaling==Frontend::Scaling::linear,"smooth scaling selection failed");
        SDL_ScaleMode scale{};require(SDL_GetTextureScaleMode(texture,&scale) && scale==SDL_SCALEMODE_LINEAR,"SDL filter not applied");
        menu.key(SDLK_RIGHT);
        SDL_RendererLogicalPresentation presentation{};int w,h;
        require(SDL_GetRenderLogicalPresentation(renderer,&w,&h,&presentation) &&
            presentation==SDL_LOGICAL_PRESENTATION_INTEGER_SCALE && w==320 && h==224,"integer presentation not applied");
        menu.key(SDLK_DOWN);
        menu.key(SDLK_LEFT);require(menu.settings.sprites==Frontend::SpriteFilter::scalefx,"sprite filter reverse wrap failed");
        menu.key(SDLK_RIGHT);require(menu.settings.sprites==Frontend::SpriteFilter::none,"sprite filter forward wrap failed");
        for(int i=1;i<Frontend::sprite_filter_count;++i) {
            menu.key(SDLK_RIGHT);require(static_cast<int>(menu.settings.sprites)==i,"sprite filter selection failed");
        }
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);
        require(menu.settings.fullscreen && (SDL_GetWindowFlags(window)&SDL_WINDOW_FULLSCREEN),"fullscreen not applied");
        menu.key(SDLK_LEFT);require(!menu.settings.fullscreen,"windowed restoration failed");
        menu.key(SDLK_DOWN);menu.key(SDLK_RIGHT);menu.key(SDLK_RIGHT);
        require(menu.settings.stats==Frontend::Stats::detailed,"stats mode selection failed");
        menu.draw();menu.draw_stats({59.6,59.6,16.8,12,1},true);SDL_RenderPresent(renderer);
        require(game.info().number==before.number && game.info().time_ns==before.time_ns &&
            std::memcmp(pixels.data(),game.frame().pixels.data(),pixels.size()*sizeof(RGB888))==0,
            "UI rendering mutated native frame/time");
        const auto accepted=menu.settings;menu.apply=[](const auto&){return false;};menu.key(SDLK_RIGHT);
        require(menu.settings==accepted,"failed option must retain last accepted settings");
        menu.key(SDLK_ESCAPE);require(!menu.in_options(),"options Back failed");
        require(menu.key(SDLK_ESCAPE)==Frontend::NativeMenu::Action::close,"home Escape failed");
        menu.close(game);require(!game.paused() && game.advance({}),"menu resume failed");
        game.set_paused(true);menu.open(game);menu.close(game);require(game.paused(),"menu lost preexisting pause");
        menu.open(game);menu.key(SDLK_DOWN);menu.key(SDLK_DOWN);
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::quit,"menu quit action failed");menu.close(game);
        GameSession cold(roms);menu.open(cold);
        require(menu.key(SDLK_RETURN)==Frontend::NativeMenu::Action::start,"cold menu Start action failed");menu.close(cold);
    }
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);SDL_Quit();
    std::cout<<"Native menu passed: pause/resume and prior pause, frame/time isolation, navigation, SDL filtering/integer/fullscreen, stats, rollback, Start and Quit.\n";
    return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}
