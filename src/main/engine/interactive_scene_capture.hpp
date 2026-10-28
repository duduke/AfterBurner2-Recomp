#pragma once
#include "engine/xboard_game.hpp"
#include "engine/sprite_frame.hpp"
#include <algorithm>
#include <array>
#include <set>

// Read-only practice presentation. Render COPIES of the published sprite bank
// with the existing decoder; native descriptors, scaler feedback and RGB are
// never changed. Road/tile colors come from their actual partial frontier.
class InteractiveSceneCapture {
public:
    struct Shot {
        std::vector<uint32_t> scenery,aircraft;
        uint64_t frame{};
        int left=320,top=224,right=-1,bottom=-1;
        bool has_aircraft()const{return right>=left && bottom>=top;}
    };
    InteractiveSceneCapture(XBoardGame& source_game,const Roms& rom,bool omit_tanker)
        :game(source_game),hide_tanker(omit_tanker),background(320*224),aircraft(320*224),scene_pens(320*224),actor_pens(320*224) {
        decoder.init(rom.sprites.rom,rom.sprites.length);
        std::set<unsigned> players{0x100a0,0x100f0,0x10140,0x10230},tankers{0x1d770,0x1d780};
        // Flight and refuel select different pitch/bank tables. Include every
        // ROM-selected player pose so a scripted roll cannot leak a duplicate
        // aircraft into the detached scenery.
        for(unsigned pitch=0;pitch<5;++pitch)for(unsigned kind=0;kind<2;++kind){
            const unsigned table=rom.maincpu.read32((kind?0x16b9e:0x28c94)+pitch*4),stride=kind?4:16;
            if(table<5*stride || table+5*stride+4>rom.maincpu.length)throw std::runtime_error("Invalid practice pose table");
            for(int bank=-5;bank<=5;++bank)players.insert(rom.maincpu.read32(table+bank*int(stride)));
        }
        // $16594/$1686A use nineteen eight-byte hose records, pointer at +2.
        for(unsigned entry=0;entry<19;++entry)tankers.insert(rom.maincpu.read32(0x166ce + entry*8+2));
        std::set<unsigned> roots=players;roots.insert(tankers.begin(),tankers.end());
        for(unsigned root:roots){
            if(root<0x10000 || root+16>rom.subcpu.length)continue;
            for(unsigned zoom=0;zoom<64;++zoom){const int delta=zoom?int16_t(rom.subcpu.read16(0x9e4+zoom*8+4)):0;
                const auto f=read_sprite_frame(rom.subcpu.rom,rom.subcpu.length,root+delta);
                families.push_back({f.bank,f.offset,unsigned(f.offset)+f.pitch*f.height,f.pitch,tankers.count(root)!=0});}
        }
        game.board().layers=[this](unsigned top,unsigned bottom,auto indices,auto priority,const HWPalette& palette){
            // Observe the actual published exhaust descriptors before the
            // native compositor draws them. $122BA assigns palette $80;
            // instruction-only producer probes miss register-only phases.
            const auto& published=game.board().memory.sprite_buffer;
            for(unsigned off=0;off<0x1000;off+=16){
                const unsigned flags=unsigned(published[off])<<8|published[off+1];
                if(flags&0x8000)break;
                if(!(flags&0x5000) && published[off+13]==0x80){
                    const int y=int(flags&511)-256;
                    const unsigned rows=((unsigned(published[off+10])<<8|published[off+11])&4095)+1;
                    const int end=y+((published[off+8]&0x80)?1:-1)*int(rows-1);
                    if(std::max(y,end)>=int(top) && std::min(y,end)<=int(bottom))flame_drawn=true;
                }
            }
            if(!enabled)return;
            auto scene=game.board().memory.sprite_buffer,actor=scene;
            for(unsigned slot=0;slot<256;++slot){const auto off=slot*16;
                auto word=[&](unsigned i){return unsigned(scene[off+i*2])<<8|scene[off+i*2+1];};
                if(word(0)&0x8000)break;
                const auto color=word(6)&255,bank=(word(0)>>9)&7,start=word(1);
                const unsigned pitch=unsigned(std::abs(int16_t((word(2)>>1)|((word(4)&0x1000)<<3))>>8));
                bool body=false,tanker=false;
                for(const auto& f:families)if(f.bank==bank && f.pitch==pitch && start>=f.begin && start<f.end){
                    if(f.tanker)tanker=true;else body=true;
                }
                body &= color==0xf2 || color==0x13 || color==6 || (color>=0xb3 && color<=0xb6);
                // Native $122BA and flight $AE8C/$AEE4 own palette $80.
                // Keep scenery and all unrelated palette/frame combinations.
                const bool flame=color==0x80;
                const bool remove=body || flame || (hide_tanker && tanker && (color==0x46 || color==0x70 || color==0x8c));
                if(remove)scene[off]|=0x40;
                if(!body || color==0x13)actor[off]|=0x40;
            }
            std::fill(scene_pens.begin()+top*320,scene_pens.begin()+(bottom+1)*320,0xffff);
            std::fill(actor_pens.begin()+top*320,actor_pens.begin()+(bottom+1)*320,0xffff);
            decoder.render_bank(scene.data(),scene_pens.data(),320,224,top,bottom);
            decoder.render_bank(actor.data(),actor_pens.data(),320,224,top,bottom);
            for(unsigned i=top*320;i<(bottom+1)*320;++i){
                auto raw=scene_pens[i];unsigned pen=indices[i];
                if(raw!=0xffff && (1<<((raw>>12)&3))>priority[i])pen=(raw&0x400f)==0x400a?pen+palette.num_entries():raw&0xfff;
                background[i]=argb(palette.pen(pen));
                raw=actor_pens[i];
                aircraft[i]=raw!=0xffff && (1<<((raw>>12)&3))>priority[i] && (raw&0x400f)!=0x400a?argb(palette.pen(raw&0xfff)):0;
            }
        };
        game.board().rendered=[this](uint64_t,unsigned top,unsigned bottom,bool display){
            if(!enabled)return;
            if(!display){std::fill(background.begin()+top*320,background.begin()+(bottom+1)*320,0xff000000);std::fill(aircraft.begin()+top*320,aircraft.begin()+(bottom+1)*320,0);}
        };
    }
    void enable(bool value){enabled=value;}
    void begin_frame(){flame_drawn=false;}
    bool burner_seen()const{return flame_drawn;}
    Shot shot()const{
        Shot result;result.scenery=background;result.aircraft=aircraft;result.frame=game.board().frames;
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x)if(aircraft[y*320+x]){
            result.left=std::min(result.left,int(x));result.right=std::max(result.right,int(x));result.top=std::min(result.top,int(y));result.bottom=std::max(result.bottom,int(y));}
        return result;
    }
private:
    struct Family{unsigned bank,begin,end,pitch;bool tanker;};
    XBoardGame& game;bool hide_tanker,enabled=false,flame_drawn=false;
    HWSprites decoder;
    std::vector<Family> families;
    std::vector<uint32_t> background,aircraft;
    std::vector<uint16_t> scene_pens,actor_pens;
    static uint32_t argb(RGB888 p){return 0xff000000u|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b;}
};
