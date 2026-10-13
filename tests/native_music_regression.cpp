#include "frontend/native_music.hpp"
#include <iostream>
#include <chrono>
#include <cmath>
using namespace Frontend;
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
void wait_file(NativeMusic& music,unsigned track,const std::string& file) {
    for(int i=0;i<2000 && music.settings.files[track]!=file;++i){music.poll();SDL_Delay(2);}
    require(music.settings.files[track]==file,"asynchronous file selection failed");
}
XBoardGame::AudioPresentation chunk(unsigned count,uint64_t first=0,std::vector<MusicRouting::Event> events={}) {
    return {first,std::vector<XBoardSpeaker::Stereo>(count,{.2f,-.2f}),std::vector<XBoardSpeaker::Stereo>(count,{.1f,-.1f}),std::move(events)};
}
int main()try {
    require(SDL_Init(SDL_INIT_AUDIO),SDL_GetError());
    {
        std::vector<uint8_t> samples(0x80000,0xc0);XBoardPcm pcm(samples);
        for(auto [ch,volume]:std::array<std::pair<unsigned,unsigned>,4>{{{0,1},{7,2},{8,4},{15,8}}}) {
            pcm.write(ch*8+2,volume);pcm.write(ch*8+3,volume);pcm.write(ch*8+6,1);pcm.write(ch*8+7,0);
            pcm.write(ch*8+0x84,0);pcm.write(ch*8+0x85,0);pcm.write(ch*8+0x86,0);
        }
        XBoardAudioDeadlines audio(pcm);unsigned points=0;
        audio.stems=[&](unsigned device,uint64_t,uint64_t,const auto& m,const auto& f){if(device!=1)return;for(unsigned i=0;i<m.size();++i){require(m[i].left==192 && m[i].right==192 && f[i].left==768 && f[i].right==768,"PCM music/effects routing");++points;}};
        audio.update(200000);require(points==7,"PCM stem sample coverage");
    }
    const std::filesystem::path root="build/oracle/music-fixtures";
    const auto config=(root/"settings.cfg").string();std::filesystem::remove(config);
    auto files=soundtrack_files(root);require(files==std::vector<std::string>{"A invalid.wav","B tone.WAV","C tone.FLAC"},"folder scan/filter/order");
    MusicSettings chosen;chosen.music=55;chosen.effects=25;chosen.track_volume[1]=80;chosen.files[1]="C tone.FLAC";
    require(save_music_settings(config,chosen) && load_music_settings(config)==chosen,"audio settings persistence");
    require(!music_filename("../outside.wav") && !music_filename("dir/song.wav"),"filename escape accepted");
    {std::ofstream bad(config);bad<<"AB2AUDIO1\nmusic 101\neffects -1\nfinal_take_off 50 \"../x.wav\"\n";}
    require(load_music_settings(config)==MusicSettings{},"malformed settings defaults");
    std::filesystem::remove(config);
    std::string error;auto wav=decode_music((root/"B tone.WAV").string(),error),flac=decode_music((root/"C tone.FLAC").string(),error);
    require(wav && flac && wav->stereo.size()==7680 && wav->stereo.size()==flac->stereo.size(),"WAV/FLAC decode");
    for(unsigned i=0;i<wav->stereo.size();++i)require(std::abs(wav->stereo[i]-flac->stereo[i])<1e-6,"lossless decoded tone mismatch");
    require(!decode_music((root/"A invalid.wav").string(),error),"invalid file accepted");
    auto unknown=decode_music((root/"formats/unknown-count.flac").string(),error);
    require(unknown && unknown->stereo==flac->stereo,"unknown-count FLAC decode");
    auto mono=decode_music((root/"formats/mono24.wav").string(),error);
    require(mono && mono->stereo.size()==9600,"mono24 44100 Hz resampling");
    for(size_t i=0;i<mono->stereo.size();i+=2)require(std::abs(mono->stereo[i]-mono->stereo[i+1])<1e-6,"mono stereo conversion");

    {
        NativeMusic music(config,root);
        std::vector<int16_t> raw(4000,1234);
        require(music.mix(raw,{chunk(2000,0,{{0,MusicRouting::Kind::start,0}})})==raw,"all-original unity changed native PCM");
        music.settings.music=0;
        auto output=music.mix(raw,{chunk(2000,2000)});
        require(output[3998]==XBoardSpeaker::pcm(.1f),"music mute affected effects");
        music.settings.music=100;music.settings.effects=0;
        output=music.mix(raw,{chunk(2000,4000)});require(output[3998]==XBoardSpeaker::pcm(.2f),"effects mute affected music");
        music.settings.music=50;music.settings.effects=100;music.settings.track_volume[0]=80;
        output=music.mix(raw,{chunk(2000,6000)});require(std::abs(output[3998]-XBoardSpeaker::pcm(.18f))<=1,"combined track/global gain");
        music.restarted();music.defaults();
        music.cycle(0,1);for(int i=0;i<100 && music.notice=="LOADING MUSIC FILE";++i){music.poll();SDL_Delay(2);}
        require(music.settings.files[0].empty(),"invalid file replaced valid selection");
        music.cycle(0,1);wait_file(music,0,"B tone.WAV");music.cycle(0,1);wait_file(music,0,"C tone.FLAC");
        raw.assign(12000,0);
        output=music.mix(raw,{chunk(6000,0,{{0,MusicRouting::Kind::start,0}})});
        for(unsigned frame:{0u,1000u,3840u,4567u})require(std::abs(output[2*frame]-XBoardSpeaker::pcm(flac->stereo[(frame%3840)*2]*.25f+.1f))<=1,"replacement/loop/effects mix");
        output=music.mix(std::vector<int16_t>(4000,0),{chunk(2000,6000,{{125000000,MusicRouting::Kind::stop,0}})});
        require(output.back()==XBoardSpeaker::pcm(-.1f),"original music tail leaked after replacement stop");
        music.restarted();
        output=music.mix(std::vector<int16_t>(2000,0),{chunk(1000,0,{{0,MusicRouting::Kind::start,0},{10000000,MusicRouting::Kind::fade,0,.5f}})});
        require(std::abs(output[1600]-XBoardSpeaker::pcm(flac->stereo[1600]*.5f*.25f+.1f))<=1,"replacement fade");
        music.preview(0);require(music.previewing(),"preview stream failed");music.update_preview();music.stop_preview();require(!music.previewing(),"preview shutdown");
        music.originals();require(music.settings.files[0].empty(),"Original restoration");
    }
    {
        NativeMusic music("",root/"formats");
        const std::vector<int16_t> original(4000,1234);
        require(music.mix(original,{chunk(2000,0,{{0,MusicRouting::Kind::start,0}})})==original,"live mapping Original baseline");
        // Scan order: mastered.wav, mono24.wav, unknown-count.flac.
        music.cycle(0,1);wait_file(music,0,"mastered.wav");
        auto output=music.mix(std::vector<int16_t>(4000),{chunk(2000,2000)});
        const float mastered=32767.f/32768;
        require(std::abs(output[3998]-XBoardSpeaker::pcm(mastered*.25f+.1f))<=1,"mastered music lacks mixing headroom");
        require(std::abs(output[3999]-XBoardSpeaker::pcm(-mastered*.25f-.1f))<=1,"mastered stereo balance");
        music.settings.effects=0;
        auto dry=music.mix(std::vector<int16_t>(4000),{chunk(2000,2000)});
        require(std::abs(output[3998]-dry[3998]-3276)<=1,"replacement reduced or clipped effects");
        music.settings.music=50;music.settings.track_volume[0]=80;
        output=music.mix(std::vector<int16_t>(4000),{chunk(2000,4000)});
        require(std::abs(output[3998]-XBoardSpeaker::pcm(mastered*.25f*.4f))<=1,"replacement track/global gains");
        music.settings.music=0;music.settings.effects=100;
        output=music.mix(std::vector<int16_t>(4000),{chunk(2000,6000)});
        require(output[3998]==XBoardSpeaker::pcm(.1f),"replacement mute affected effects");
        music.defaults();music.restarted();
        music.mix(original,{chunk(2000,0,{{0,MusicRouting::Kind::start,0},{10000000,MusicRouting::Kind::fade,0,.5f}})});
        music.cycle(0,1);wait_file(music,0,"mono24.wav");
        output=music.mix(original,{chunk(2000,2000)});
        require(std::abs(output[2000]-XBoardSpeaker::pcm(mono->stereo[2000]*.25f*.5f+.1f))<=1,"live replacement switch or fade preservation");
        music.cycle(1,1);wait_file(music,1,"mastered.wav");
        output=music.mix(original,{chunk(2000,4000)});
        require(std::abs(output[2000]-XBoardSpeaker::pcm(mono->stereo[6000]*.25f*.5f+.1f))<=1,"inactive mapping restarted active music");
        music.cycle(0,-1);wait_file(music,0,"mastered.wav");music.cycle(0,-1);
        require(music.mix(original,{chunk(2000,6000)})==original,"live return to Original");
        music.cycle(0,1);wait_file(music,0,"mastered.wav");
        output=music.mix(original,{chunk(2000,8000)});
        require(std::abs(output[3998]-XBoardSpeaker::pcm(mastered*.25f*.5f+.1f))<=1,"live return to replacement");
        music.originals();require(music.mix(original,{chunk(2000,10000)})==original,"live All Original restoration");
        music.mix(original,{chunk(2000,12000,{{250000000,MusicRouting::Kind::stop,0}})});
        music.cycle(0,1);wait_file(music,0,"mastered.wav");
        require(music.mix(original,{chunk(2000,14000)})==original,"mapping edit restarted stopped music");
    }
    {
        chosen=MusicSettings{};chosen.files[0]="missing.wav";require(save_music_settings(config,chosen),"missing file config");
        NativeMusic fallback(config,root);std::vector<int16_t> raw(4000,4321);
        require(fallback.mix(raw,{chunk(2000,0,{{0,MusicRouting::Kind::start,0}})})==raw,"missing-file Original fallback");
    }
    std::filesystem::remove(config);SDL_Quit();
    std::cout<<"PASS music folder, persistence, WAV/FLAC, asynchronous selection, independent gains/mutes, loops, fades, preview, restart and Original fallback\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';SDL_Quit();return 1;}
