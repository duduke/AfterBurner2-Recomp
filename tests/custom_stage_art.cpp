// ROM-backed presentation checks. No native execution or fixture mutation.
#include "frontend/custom_stage_view.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <set>
int main()try{
    assert(SDL_Init(SDL_INIT_VIDEO));
    Roms source;assert(source.load_aburner2());
    auto* screen=SDL_CreateSurface(1152,720,SDL_PIXELFORMAT_RGBA32);assert(screen);
    auto* renderer=SDL_CreateSoftwareRenderer(screen);assert(renderer);
    SDL_SetRenderLogicalPresentation(renderer,960,600,SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderDrawBlendMode(renderer,SDL_BLENDMODE_BLEND);
    const std::filesystem::path out="build/oracle/landing-polish";std::filesystem::create_directories(out);
    auto capture=[&](const std::string& name){auto* image=SDL_RenderReadPixels(renderer,nullptr);assert(image);assert(SDL_SaveBMP(image,(out/name).string().c_str()));
        std::vector<uint8_t> bytes;for(int y=0;y<image->h;++y){auto* p=static_cast<uint8_t*>(image->pixels)+y*image->pitch;bytes.insert(bytes.end(),p,p+image->w*4);}SDL_DestroySurface(image);return bytes;};
    {
        Frontend::InteractiveStageSprites sprites(renderer,source,true);
        Frontend::CustomStageView view{renderer,&sprites};
        sprites.cache_scenery(3,0x1daa0,0x12,0,100,false,false);assert(sprites.scenery_count(3)>0);
        sprites.reset_tanker_scenery();assert(sprites.scenery_count(3)==0);
        sprites.cache_scenery(3,0x140f0,0xd6,0,100,false,false);assert(sprites.scenery_count(3)==1);
        sprites.reset_tanker_scenery();

        // Independently decode a font tile from the ROM, then sample rendered
        // cell centers. This catches accidentally retaining SDL's debug font.
        SDL_SetRenderDrawColor(renderer,7,11,17,255);SDL_RenderClear(renderer);
        sprites.hud()->text(100,100,"a",{255,255,255,255},2);const auto glyph=capture("game-font-a.bmp");
        const auto third=source.tiles.length/3;
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){const unsigned p=unsigned('A')*8+y,bit=7-x;
            const unsigned pen=((source.tiles.rom[p]>>bit)&1)|(((source.tiles.rom[third+p]>>bit)&1)<<1)|(((source.tiles.rom[2*third+p]>>bit)&1)<<2);
            const auto pixel=&glyph[(int(std::floor((100+(y+.5)*2)*1.2))*1152+int(std::floor((100+(x+.5)*2)*1.2)))*4];
            if(!pen)assert(pixel[0]==7 && pixel[1]==11 && pixel[2]==17);
            else if(pen==7)assert(pixel[0]==0 && pixel[1]==0 && pixel[2]==0);
            else assert(pixel[0]==255 && pixel[1]==255 && pixel[2]==255);
        }
        for(unsigned kind=0;kind<7;++kind){Frontend::CustomStage layout(Frontend::CustomStageDefinition::preset(kind));layout.start();
            view.draw(layout,false);assert(view.game_font && view.hud_tiles>40);const auto native_hud=capture("game-hud-"+std::to_string(kind)+".bmp");
            for(int y:{20,580,700})for(int x=0;x<1152;++x){const auto pixel=&native_hud[(y*1152+x)*4];assert(pixel[0]==10 && pixel[1]==23 && pixel[2]==29);}
        }
        Frontend::CustomStage ammo(Frontend::CustomStageDefinition::preset(0));ammo.start();view.draw(ammo,false);const auto low=capture("hud-ammo-50.bmp");
        ammo.missiles=100;view.draw(ammo,false);const auto full=capture("hud-ammo-100.bmp");unsigned ammo_changed=0;
        for(int y=525;y<562;++y)for(int x=975;x<1090;++x)for(unsigned c=0;c<3;++c)ammo_changed+=low[(y*1152+x)*4+c]!=full[(y*1152+x)*4+c];assert(ammo_changed>100);
        std::cout<<"ROM font pixels match original glyph pens; all seven HUDs stay in the playfield and missile symbols track rearming\n";
        for(unsigned kind:{1u,2u,3u}){
            Frontend::CustomStage s(Frontend::CustomStageDefinition::preset(kind));s.start();
            while(s.phase==Frontend::CustomStage::Phase::approach)s.tick(s.demo());assert(s.phase==Frontend::CustomStage::Phase::rollout);
            s.phase_time=3;view.draw(s,false);auto clear=capture(std::to_string(kind)+"-clear.bmp");
            s.phase_time=.35;view.draw(s,false);assert(view.smoke_puffs>0);auto smoke=capture(std::to_string(kind)+"-smoke.bmp");
            unsigned changed=0;for(int y=470;y<540;++y)for(int x=500;x<655;++x)for(unsigned c=0;c<3;++c)changed+=clear[(y*1152+x)*4+c]!=smoke[(y*1152+x)*4+c];
            assert(changed>100);std::cout<<"surface="<<kind<<" ROM tire smoke changed "<<changed<<" wheel-region channels\n";
            s.phase=Frontend::CustomStage::Phase::approach;s.height=10;s.pitch=.8;view.draw(s,false);
            assert(sprites.last_pitch_pose==4);s.pitch=-.8;view.draw(s,false);assert(sprites.last_pitch_pose==0);
        }
        for(unsigned kind:{1u,2u,3u}){
            Frontend::CustomStage s(Frontend::CustomStageDefinition::preset(kind));s.start();s.x=0;s.height=5;s.range=0;s.pitch=s.bank=0;
            s.definition.player_shadow=false;view.draw(s,false);const auto clear=capture(std::to_string(kind)+"-shadow-off.bmp");
            s.definition.player_shadow=true;view.draw(s,false);assert(view.shadow_drawn);const auto shaded=capture(std::to_string(kind)+"-shadow-on.bmp");
            unsigned changed=0;for(int y=400;y<563;++y)for(int x=420;x<760;++x)for(unsigned c=0;c<3;++c){const size_t i=(y*1152+x)*4+c;if(clear[i]!=shaded[i]){assert(shaded[i]<=clear[i]);++changed;}}
            assert(changed>100);std::cout<<"surface="<<kind<<" player shadow darkens "<<changed<<" receiver-region channels\n";
        }
        Frontend::CustomStage airborne(Frontend::CustomStageDefinition::preset(1));airborne.start();view.draw(airborne,false);assert(!view.shadow_drawn);
        Frontend::CustomStage refuel(Frontend::CustomStageDefinition::preset(0));refuel.start();view.draw(refuel,false);assert(!view.shadow_drawn);
        for(unsigned kind=0;kind<7;++kind){
            Frontend::CustomStage sky(Frontend::CustomStageDefinition::preset(kind));sky.start();
            sky.definition.clouds=false;view.draw(sky,false);const auto clear=capture("clouds-off-"+std::to_string(kind)+".bmp");assert(view.clouds_drawn==0);
            sky.definition.clouds=true;view.draw(sky,false);const auto cloudy=capture("clouds-on-"+std::to_string(kind)+".bmp");assert(view.clouds_drawn==4);
            unsigned changes=0;for(int y=100;y<225;++y)for(int x=48;x<1104;++x)for(unsigned c=0;c<3;++c){const size_t i=(y*1152+x)*4+c;if(clear[i]!=cloudy[i]){++changes;}}
            assert(changes>1000);
        }
        SDL_SetRenderDrawColor(renderer,7,11,17,255);SDL_RenderClear(renderer);
        Frontend::CustomStageClouds alpha_clouds;alpha_clouds.draw(renderer,405,0,0,false);auto alpha=capture("cloud-alpha.bmp");
        unsigned soft=0;for(int y=100;y<470;++y)for(int x=48;x<1104;++x){const auto p=&alpha[(y*1152+x)*4];assert(p[0]<200 && p[1]<200 && p[2]<200);soft+=p[0]>10;}assert(soft>1000);
        for(float pitch:{-1.f,0.f,1.f}){refuel.pitch=pitch;refuel.height=-5;view.draw(refuel,false);
            const auto pose=Frontend::CustomStageGuidance::pitch_pose(pitch);assert(sprites.last_pitch_pose==pose);
            assert(sprites.last_player_frame==source.maincpu.read32(source.maincpu.read32(0x28c94+pose*4)));
            SDL_SetRenderDrawColor(renderer,7,11,17,255);SDL_RenderClear(renderer);
            sprites.fighter(480,385,158,0,false,0,true,false,pitch,false);
            const auto point=sprites.refuel_contact(480,385,158,0,pitch);auto aircraft=capture("tanker-pitch-"+std::to_string(pose)+".bmp");
            const auto pixel=&aircraft[(int(std::lround(point.y*1.2))*1152+int(std::lround(point.x*1.2)))*4];
            assert(pixel[0]!=7 || pixel[1]!=11 || pixel[2]!=17); // contact lies on actual opaque aircraft art
        }
        SDL_SetRenderDrawColor(renderer,7,11,17,255);SDL_RenderClear(renderer);sprites.refueling_hose(480,300,2);
        const auto tip=sprites.tanker_tip();const auto nozzle=capture("nozzle-contact-pixel.bmp");
        const auto end=&nozzle[(int(std::floor((300+tip.y*2)*1.2))*1152+int(std::floor((480+tip.x*2)*1.2)))*4];
        assert(end[0]!=7 || end[1]!=11 || end[2]!=17); // hose endpoint itself must also be opaque
        const auto hatch=sprites.hose_offset();assert(hatch.x>=10 && hatch.x<=20 && hatch.y>25 && hatch.y<40);
        refuel.pitch=refuel.x=refuel.height=0;refuel.range=12;refuel.tick({});view.draw(refuel,false);
        assert(refuel.coupled && view.probe_error_px<.01 && view.sky_aircraft==0);capture("tanker-contact-corrected.bmp");
        std::cout<<"All seven skies have translucent clouds; tanker pitch uses ROM poses and nozzle meets the aircraft silhouette at capture\n";
        // A tiny attitude change across a native pose boundary must not
        // teleport the tanker or its hose. Only the player changes ROM frames.
        Frontend::CustomStage stable(Frontend::CustomStageDefinition::preset(0));stable.start();
        stable.definition.rom_environment=stable.definition.clouds=false;stable.range=80;stable.height=stable.x=0;
        view.assistance=false;view.show_scenery=false;
        for(bool bank_boundary:{false,true}){
            stable.pitch=bank_boundary?0:.249;stable.bank=bank_boundary?-.179:0;view.draw(stable,false);auto before=capture(bank_boundary?"bank-before.bmp":"pitch-before.bmp");
            stable.pitch=bank_boundary?0:.251;stable.bank=bank_boundary?-.181:0;view.draw(stable,false);auto after=capture(bank_boundary?"bank-after.bmp":"pitch-after.bmp");
            unsigned changed=0;for(int y=130;y<390;++y)for(int x=300;x<820;++x)for(unsigned c=0;c<3;++c)changed+=before[(y*1152+x)*4+c]!=after[(y*1152+x)*4+c];
            assert(changed<50);
        }
        view.assistance=view.show_scenery=true;
        auto gentle_bank=[&](float bank,const char* name){SDL_SetRenderDrawColor(renderer,7,11,17,255);SDL_RenderClear(renderer);sprites.fighter(480,385,158,bank,false,0,true,false,0,false);return capture(name);};
        const auto gentle_before=gentle_bank(-.179f,"gentle-bank-before.bmp"),gentle_after=gentle_bank(-.181f,"gentle-bank-after.bmp");
        unsigned gentle_changes=0;for(size_t i=0;i<gentle_before.size();++i)gentle_changes+=gentle_before[i]!=gentle_after[i];assert(gentle_changes<200);
        // Hold pose/power fixed: the plume may flicker in brightness, but its
        // shape/root must not jump to a different flight geometry every tick.
        std::vector<bool> fixed_mask;
        for(unsigned tick=0;tick<20;++tick){SDL_SetRenderDrawColor(renderer,7,11,17,255);SDL_RenderClear(renderer);
            sprites.fighter(480,385,158,0,true,tick,false,true,0,true);auto frame=capture("steady-flame-"+std::to_string(tick)+".bmp");
            std::vector<bool> mask;for(int y=470;y<550;++y)for(int x=480;x<670;++x){const auto p=&frame[(y*1152+x)*4];mask.push_back(p[0]!=7 || p[1]!=11 || p[2]!=17);}
            if(tick==0)fixed_mask=mask;else assert(mask==fixed_mask);
        }
        for(float attitude:{-.6f,.6f}){std::vector<bool> fixed_air_mask;
            for(unsigned tick:{0u,4u,8u,12u,16u}){SDL_SetRenderDrawColor(renderer,7,11,17,255);SDL_RenderClear(renderer);
                sprites.fighter(480,385,158,attitude,true,tick,false,false,attitude,false);assert(sprites.last_flame_error<.01);
                auto image=capture("air-flame-"+std::to_string(attitude)+"-"+std::to_string(tick)+".bmp");std::vector<bool> mask;
                for(int y=400;y<550;++y)for(int x=400;x<750;++x){const auto p=&image[(y*1152+x)*4];mask.push_back(p[0]!=7 || p[1]!=11 || p[2]!=17);}
                if(tick==0)fixed_air_mask=mask;else assert(mask==fixed_air_mask);
            }
        }
        std::cout<<"Tanker pose/left-bank boundaries retain world placement and orientation; fixed ground and airborne afterburner poses retain the same plume footprint\n";
        std::set<uint64_t> distinct;
        for(unsigned pose=0;pose<5;++pose){SDL_SetRenderDrawColor(renderer,7,11,17,255);SDL_RenderClear(renderer);
            sprites.fighter(480,385,158,0,false,0,false,true,(int(pose)-2)*.5f,false);
            assert(sprites.last_pitch_pose==pose);
            assert(sprites.last_player_frame==source.maincpu.read32(source.maincpu.read32(0x28c94+pose*4)));
            auto data=capture("pitch-"+std::to_string(pose)+".bmp");uint64_t hash=1469598103934665603ull;for(auto b:data)hash=(hash^b)*1099511628211ull;distinct.insert(hash);
        }
        assert(distinct.size()==5);std::cout<<"Five distinct ROM pitch images; pitch input remains active with landing gear on all surfaces\n";
        SDL_SetRenderDrawColor(renderer,7,11,17,255);SDL_RenderClear(renderer);assert(sprites.brake_smoke(480,385,.35)>0);
        auto smoke=capture("neutral-smoke.bmp");unsigned neutral=0;
        for(int y=470;y<540;++y)for(int x=500;x<655;++x){auto p=&smoke[(y*1152+x)*4];const unsigned high=std::max({p[0],p[1],p[2]}),low=std::min({p[0],p[1],p[2]});
            if(high>50){assert(high-low<=16 && high<255);++neutral;}}
        assert(neutral>100);std::cout<<"Native smoke silhouette uses neutral gray with translucent compositing\n";
        Frontend::CustomStageView hud{renderer,nullptr};hud.show_hud=false;
        auto profile=[&](const std::vector<uint8_t>& data,double x,double y,bool vertical){unsigned green=0;const int cx=std::lround(x*1.2),cy=std::lround(y*1.2);
            for(int i=-8;i<=8;++i){const auto p=&data[((cy+(vertical?0:i))*1152+cx+(vertical?i:0))*4];green+=p[1]>245 && p[0]<220 && p[2]<220;}return green;};
        unsigned bold_v=0,bold_h=0;
        for(unsigned mode=0;mode<3;++mode){Frontend::CustomStage s(Frontend::CustomStageDefinition::preset(1));s.start();s.definition.rom_environment=false;
            s.x=mode==1?20:0;s.height=mode==2?82:62;s.range=1000;s.pitch=s.bank=0;
            const auto aid=Frontend::CustomStageGuidance::landing(s.x,s.height,s.range,0,0,80,.06,198,480);
            hud.draw(s,false);auto data=capture("ils-weight-"+std::to_string(mode)+".bmp");
            const auto v=profile(data,aid.cross.x,270,true),h=profile(data,430,aid.cross.y,false);
            if(mode==0){bold_v=v;bold_h=h;assert(v>=3 && h>=3);}else if(mode==1){assert(v<bold_v && h==bold_h);}else assert(v==bold_v && h<bold_h);
        }
        Frontend::CustomStage tanker(Frontend::CustomStageDefinition::preset(0));tanker.start();tanker.definition.rom_environment=false;
        tanker.x=tanker.height=0;tanker.range=12;tanker.tick({});assert(tanker.coupled && tanker.phase==Frontend::CustomStage::Phase::contact);
        hud.draw(tanker,false);auto locked=capture("tanker-locked.bmp");
        GameControls strong;strong.stick_x=strong.stick_y=strong.throttle=1;
        for(unsigned i=0;i<30;++i)tanker.tick(strong);hud.draw(tanker,false);auto held=capture("tanker-held.bmp");
        for(int y=94;y<563;++y)for(int x=48;x<1104;++x)for(unsigned c=0;c<4;++c)assert(locked[(y*1152+x)*4+c]==held[(y*1152+x)*4+c]);
        std::cout<<"ILS bars independently thicken in centered positions; tanker world/diamonds remain pixel-identical under held input during automatic rearming\n";
        auto normal_plane=[&](const char* name){SDL_SetRenderDrawColor(renderer,7,11,17,255);SDL_RenderClear(renderer);sprites.fighter(480,385,158,0,false,0,false,false,.5,false);return capture(name);};
        const auto normal_before=normal_plane("landing-palette-before.bmp");
        HWPalette different_palette(8192);for(unsigned pen=1;pen<15;++pen)different_palette.write(source.maincpu.rom[0x16b8a]*16+pen,0x000f);
        sprites.refuel_colors(different_palette);const auto normal_after=normal_plane("landing-palette-after.bmp");assert(normal_before==normal_after);
        std::cout<<"Changing tanker live pitch palettes leaves landing aircraft colors unchanged\n";

    }
    SDL_DestroyRenderer(renderer);SDL_DestroySurface(screen);SDL_Quit();return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
