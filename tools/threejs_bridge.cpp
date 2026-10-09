// Local prototype transport. Gameplay always runs the production native core.
#include "engine/xboard_game.hpp"
#include "engine/game_controls.hpp"
#include "threejs_scene.hpp"
#include <iostream>
#include <sstream>
#include <string>
#include <chrono>
#include <unordered_map>

std::string base64(std::span<const uint8_t> bytes){
    constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;out.reserve((bytes.size()+2)/3*4);
    for(size_t i=0;i<bytes.size();i+=3){const unsigned n=(unsigned(bytes[i])<<16)|(i+1<bytes.size()?unsigned(bytes[i+1])<<8:0)|(i+2<bytes.size()?bytes[i+2]:0);
        out+=alphabet[(n>>18)&63];out+=alphabet[(n>>12)&63];out+=i+1<bytes.size()?alphabet[(n>>6)&63]:'=';out+=i+2<bytes.size()?alphabet[n&63]:'=';
    }return out;
}
struct SceneCapture {
    XBoardGame& game;
    std::vector<std::string> clips;
    struct Image {unsigned width{},height{};std::vector<uint8_t> pixels;};
    std::unordered_map<unsigned,Image> images;
    std::vector<uint8_t> previous_palette;
    void full_scene(){images.clear();previous_palette.clear();}
    bool enabled=true;
    double capture_ms=0,tick_ms=0;
    using Clock=std::chrono::steady_clock;
    explicit SceneCapture(XBoardGame& game):game(game){bind();}
    void bind(){
        game.board().layers=[this](auto top,auto bottom,auto indices,auto priority,const HWPalette& palette){
            if(enabled)capture(top,bottom,indices,priority,palette);
        };
        game.board().rendered=[this](uint64_t,unsigned top,unsigned bottom,bool display){
            if(enabled&&!display)clips.push_back("{\"top\":"+std::to_string(top)+",\"bottom\":"+std::to_string(bottom)+",\"display\":false}");
        };
    }
    void capture(unsigned top,unsigned bottom,std::span<const uint16_t> indices,std::span<const uint8_t> priority,const HWPalette& palette){
        const auto began=Clock::now();
        std::ostringstream out;out<<"{\"top\":"<<top<<",\"bottom\":"<<bottom<<",\"display\":true";
        const auto& m=game.board().memory;
        std::vector<uint8_t> pens; pens.reserve(16384*4);
        for(unsigned i=0;i<16384;++i){const auto p=palette.pen(i);pens.insert(pens.end(),{p.r,p.g,p.b,255});}
        if(pens!=previous_palette){out<<",\"palette\":\""<<base64(pens)<<"\"";previous_palette=std::move(pens);}
        // Indexed scanline strips become actual batched Three.js geometry.
        std::vector<uint8_t> strips;
        auto append=[&](unsigned value){strips.push_back(value&255);strips.push_back(value>>8);};
        for(unsigned y=top;y<=bottom;++y)for(unsigned x=0;x<320;){unsigned end=x+1;const auto i=y*320+x;
            while(end<320&&indices[y*320+end]==indices[i]&&priority[y*320+end]==priority[i])++end;
            append(x);append(end);append(y);append(indices[i]);append(priority[i]);x=end;
        }
        out<<",\"stripsLE\":\""<<base64(strips)<<"\",\"sprites\":[";bool comma=false;
        const auto objects=ThreeScene::sprites(m.sprite_buffer,{roms.sprites.rom,roms.sprites.length},top,bottom);
        for(const auto& s:objects){if(comma)out<<',';comma=true;
            out<<"{\"slot\":"<<s.slot<<",\"x\":"<<s.x<<",\"y\":"<<s.y<<",\"xd\":"<<s.xdelta<<",\"yd\":"<<s.ydelta<<",\"width\":"<<s.width<<",\"height\":"<<s.rows<<",\"sourceWidth\":"<<s.source_width<<",\"hzoom\":"<<s.hzoom<<",\"priority\":"<<s.priority<<",\"color\":"<<s.color;
            auto& image=images[s.slot];
            if(image.width!=s.source_width||image.height!=s.rows||image.pixels!=s.pixels){
                out<<",\"pixels\":\""<<base64(s.pixels)<<"\"";
                image={s.source_width,s.rows,s.pixels};
            }
            out<<'}';
        }
        out<<"]}";clips.push_back(out.str());
        capture_ms+=std::chrono::duration<double,std::milli>(Clock::now()-began).count();
    }
    std::string response(const std::vector<int16_t>& audio,bool verify=true){
        std::ostringstream out;out<<"{\"frame\":"<<game.board().frames<<",\"timeNs\":"<<game.board().time()<<",\"clips\":[";
        for(size_t i=0;i<clips.size();++i){if(i)out<<',';out<<clips[i];}out<<"],\"audio\":\"";
        std::vector<uint8_t> bytes;for(auto v:audio){bytes.push_back(uint16_t(v)&255);bytes.push_back(uint16_t(v)>>8);}out<<base64(bytes)<<"\"";
        if(verify){
            uint32_t hash=2166136261u;for(auto pixel:game.pixels())for(auto value:{pixel.r,pixel.g,pixel.b}){hash^=value;hash*=16777619u;}
            out<<",\"referenceHash\":"<<hash;
        }
        out<<",\"performance\":{\"coreMs\":"<<tick_ms-capture_ms<<",\"captureMs\":"<<capture_ms<<"}}";return out.str();
    }
};
int main()try{
    // Keep ROM-loader diagnostics out of the line-delimited JSON protocol.
    auto* output=std::cout.rdbuf(std::cerr.rdbuf());const bool loaded=roms.load_aburner2();std::cout.rdbuf(output);
    if(!loaded)return 1;
    XBoardGame game(roms);SceneCapture capture(game);
    std::cout<<"{\"ready\":true}"<<std::endl;
    for(std::string line;std::getline(std::cin,line);){
        std::istringstream command(line);std::string action;command>>action;
        if(action=="quit")break;
        if(action=="restart"){capture.clips.clear();capture.full_scene();game.restart();capture.bind();std::cout<<capture.response({})<<std::endl;continue;}
        unsigned count=1;GameControls c;
        if(action=="tick")command>>count>>c.stick_x>>c.stick_y>>c.throttle>>c.cannon>>c.missile>>c.coin>>c.start;
        else if(action=="demo")command>>count;else throw std::runtime_error("unknown bridge command");
        if(!command||!count||count>781)throw std::runtime_error("invalid bridge command");
        bool full_scene=false,verify=true;
        if(action=="tick"){
            if(command>>full_scene){command>>verify;if(full_scene)capture.full_scene();}
        }else command>>verify;
        capture.capture_ms=0;
        std::vector<int16_t> audio;
        for(unsigned i=0;i<count;++i){const auto began=SceneCapture::Clock::now();capture.clips.clear();const unsigned frame=game.board().frames;
            capture.enabled=i+1==count;
            if(action=="demo"){c={};c.coin=frame>=360&&frame<372;c.start=frame>=420&&frame<432;}
            const auto pins=arcade_inputs(c);XBoardGame::Host::Input input;input.buttons=pins.buttons;input.analog=pins.analog;
            game.tick(input);audio=game.take_audio();
            capture.tick_ms=std::chrono::duration<double,std::milli>(SceneCapture::Clock::now()-began).count(); // fast-forward retains only the last frame's output
        }
        std::cout<<capture.response(audio,verify)<<std::endl;
    }return 0;
}catch(const std::exception& e){std::cerr<<"Three.js bridge stopped: "<<e.what()<<'\n';return 1;}
