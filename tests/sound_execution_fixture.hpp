#include "hwaudio/xboard_speaker.hpp"
#include <bit>
#pragma once
#include "hwaudio/xboard_sound_cpu.hpp"
#include "hwaudio/xboard_pcm.hpp"
#include "hwaudio/xboard_ym_timers.hpp"
#include "hwaudio/xboard_audio_deadlines.hpp"
#include "hwmath/io_expander.hpp"
#include "hwmath/compare_timer.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
#include <deque>
#include <cstring>
#include <sstream>
#include <memory>

// Capture fields are assertions. Only the explicitly selected boot/main-host
// provider records drive this fixture; coupled mode replaces them with native
// main bus outputs. CPU state and device read results are never supplied.
class SoundExecutionFixture {
    static uint64_t number(std::istream& in,unsigned n){uint64_t v=0;while(n--){int c=in.get();if(c==EOF)throw std::runtime_error("truncated sound execution capture");v=(v<<8)|c;}return v;}
    static std::vector<uint8_t> load_program(){std::ifstream in("roms/epr-11112.17",std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};}
    static std::vector<uint8_t> load_samples(){
        std::vector<uint8_t> result(0x80000,0xff);
        const char* files[]={"mpr-10931.11","mpr-10930.12","epr-11102.13"};
        for(unsigned n=0;n<3;++n){std::ifstream in(std::string("roms/")+files[n],std::ios::binary);if(!in.read(reinterpret_cast<char*>(result.data()+n*0x20000),0x20000))throw std::runtime_error("missing PCM ROM");}
        return result;
    }
    std::vector<uint8_t> program=load_program(),samples=load_samples();
public:
    struct Record {unsigned kind;uint64_t grant,ns;uint16_t pc,address;uint8_t data;};
    XBoardSoundCpu cpu{program.data(),program.size()};
    XBoardPcm pcm{samples};XBoardYmTimers ym;
    std::unique_ptr<XBoardAudioDeadlines> audio;
    CompareTimer5250 bootstrap_latch;IOExpander bootstrap_io;
    std::function<void(const XBoardSoundCpu::Event&,uint64_t)> observer;
    uint64_t end{},transactions{},grants{},commands{},resets{},native_host{},supplied_host{};
    explicit SoundExecutionFixture(const char* path){
        std::ifstream input(path,std::ios::binary);char magic[8];
        if(!input.read(magic,8)||(std::memcmp(magic,"AB2ZEX02",8)&&std::memcmp(magic,"AB2ZEX03",8)))throw std::runtime_error("invalid sound execution capture");
        if(!std::memcmp(magic,"AB2ZEX03",8)){
            const auto diagnostic=number(input,1);if(diagnostic!=1&&diagnostic!=2)throw std::runtime_error("unknown synthetic sound program");
            std::fill(program.begin(),program.begin()+0xf000,0);
            std::ifstream code(diagnostic==1?"tools/oracle/sound_irq_program.txt":"tools/oracle/sound_timer_b_program.txt");if(!code)throw std::runtime_error("missing synthetic sound program");
            for(std::string line;std::getline(code,line);){if(line.empty()||line[0]=='#')continue;
                std::istringstream row(line);unsigned a,d;row>>std::hex>>a;while(row>>d)program.at(a++)=d;
            }
            irq_assertions.open(std::string(path)+".irq");if(!irq_assertions)throw std::runtime_error("missing read-only MAME IRQ assertions");
        }
        std::array<uint8_t,0x800> initial{};
        if(!input.read(reinterpret_cast<char*>(initial.data()),initial.size())||initial!=cpu.ram)throw std::runtime_error("unexpected power-on sound RAM");
        while(input.peek()!=EOF){const unsigned kind=number(input,1);
            if(kind==255){end=number(input,8);break;}if(kind>5)throw std::runtime_error("invalid sound execution event");
            const auto grant=number(input,8),ns=number(input,8);
            Record r{kind,grant,ns,uint16_t(number(input,2)),uint16_t(number(input,2)),uint8_t(number(input,1))};
            (kind<4?records:host).push_back(r);
        }
        if(!end||input.peek()!=EOF||records.empty()||records.front().pc||records.front().address)throw std::runtime_error("incomplete reset execution corpus");
        cpu.pcm_read=[&](uint8_t a){if(audio)return audio->pcm_read(a,now());update_pcm();return pcm.read(a);};
        cpu.pcm_write=[&](uint8_t a,uint8_t d){if(audio)audio->pcm_write(a,d,now());else{update_pcm();pcm.write(a,d);}};
        cpu.ym_read=[&](uint8_t a){return audio?audio->ym_read(a,now()):ym.read(a,now());};
        cpu.ym_write=[&](uint8_t a,uint8_t d){if(audio)audio->ym_write(a,d,now());else ym.write(a,d,now());};
        ym.synchronize_mode=[&](uint64_t ns,uint8_t d){modes.emplace_back(ns,d);cpu.abort_grant();};
        ym.irq_line=[&](bool asserted){cpu.irq_line(asserted);};
        bind_latch(bootstrap_latch);
        cpu.latch_read=[&](){const auto v=latch->sound_read();cpu.abort_grant();return v;};
        bootstrap_io.output=[&](unsigned p,uint8_t d,uint8_t){if(p==2)queue_reset(provider_ns,!(d&1));};
        cpu.trace=[&](const XBoardSoundCpu::Event& e){
            if(e.kind==XBoardSoundCpu::Kind::InterruptAck){
                uint64_t ns;unsigned pc,ret,sp,af,bc,de,hl,vector;
                if(!(irq_assertions>>ns>>pc>>ret>>sp>>af>>bc>>de>>hl>>vector))throw std::runtime_error("unexpected native IRQ acknowledge");
                const auto r=cpu.registers();
                if(ns!=now()||pc!=e.pc||r.pc!=ret||r.sp!=sp||r.af!=af||r.bc!=bc||r.de!=de||r.hl!=hl||e.data!=vector)
                    {std::cerr<<"IRQ ns="<<now()<<'/'<<ns<<" pc="<<e.pc<<'/'<<pc<<" ret="<<r.pc<<'/'<<ret<<" sp="<<r.sp<<'/'<<sp<<" af="<<r.af<<'/'<<af<<" bc="<<r.bc<<'/'<<bc<<" de="<<r.de<<'/'<<de<<" hl="<<r.hl<<'/'<<hl<<'\n';throw std::runtime_error("native IRQ timestamp or retained caller differs");}
                ++irq_checks;return;
            }
            if(cursor>=records.size()){std::cerr<<"Extra sound grant="<<current_grant<<" ns="<<now()<<" capture end="<<end<<'\n';throw std::runtime_error("extra native sound transaction");}
            const auto& r=records[cursor];const auto ns=now();
            if(unsigned(e.kind)!=r.kind||current_grant!=r.grant||ns!=r.ns||e.pc!=r.pc||e.address!=r.address||e.data!=r.data){
                std::cerr<<"Sound execution record "<<cursor<<" grant="<<current_grant<<'/'<<r.grant<<" ns="<<ns<<'/'<<r.ns<<" kind="<<unsigned(e.kind)<<'/'<<r.kind<<" pc="<<std::hex<<e.pc<<'/'<<r.pc<<" address="<<e.address<<'/'<<r.address<<" data="<<unsigned(e.data)<<'/'<<unsigned(r.data)<<std::dec<<'\n';
                throw std::runtime_error("native continuous sound differs");
            }++cursor;++transactions;if(observer)observer(e,ns);
        };
    }
    uint64_t now() const{return grant_ns+(cpu.clocks()-grant_clock-1)*250;}
    void enable_audio(const std::string& prefix){
        for(unsigned i=0;i<2;++i){audio_assertions[i].open(prefix+(i?".pcm":".fm"),std::ios::binary);char tag[8];
            if(!audio_assertions[i].read(tag,8)||memcmp(tag,"AB2AST01",8))throw std::runtime_error("missing native audio stream assertions");}
        audio=std::make_unique<XBoardAudioDeadlines>(pcm);
        audio->irq_line=[&](bool asserted){cpu.irq_line(asserted);};
        audio->synchronize=[&]{if(running)cpu.abort_grant();};
        audio->stream=[&](unsigned device,uint64_t ns,uint64_t first,const XBoardAudioDeadlines::Samples& out){
            auto& in=audio_assertions[device];
            const auto at=number(in,8),start=number(in,8),count=number(in,4);
            if(at!=ns||start!=first||count!=out.size()){
                std::cerr<<"Audio device="<<device<<" batch="<<audio_batches[device]<<" ns="<<ns<<'/'<<at<<" first="<<first<<'/'<<start<<" count="<<out.size()<<'/'<<count<<'\n';
                throw std::runtime_error("native audio batch deadline/index differs");}
            for(const auto& s:out){const auto l=int32_t(number(in,4)),r=int32_t(number(in,4));
                if(s.left!=l||s.right!=r)throw std::runtime_error("native raw audio samples differ");}
            if(speaker_assertions.is_open())speaker.append(device,first,out);
            audio_samples[device]+=out.size();++audio_batches[device];
        };
    }
    void enable_speaker(const std::string& prefix){
        speaker_assertions.open(prefix+".speaker",std::ios::binary);char tag[8];
        if(!speaker_assertions.read(tag,8)||memcmp(tag,"AB2SPK01",8))throw std::runtime_error("missing speaker assertions");
    }
    void update_speaker(uint64_t ns){
        audio->update(ns);
        if(!speaker_assertions.is_open())return;
        const auto at=number(speaker_assertions,8),first=number(speaker_assertions,8),count=number(speaker_assertions,4);
        if(at!=ns||first!=speaker.samples())throw std::runtime_error("native speaker timestamp/index differs");
        auto out=speaker.update(ns);if(count!=out.size())throw std::runtime_error("native speaker batch count differs");
        for(auto v:out)for(float ch:v)if(std::bit_cast<uint32_t>(ch)!=number(speaker_assertions,4))throw std::runtime_error("native mixed speaker float differs");
        ++speaker_batches;
    }
    unsigned run(uint64_t group,uint64_t begin,unsigned budget){
        current_grant=group;grant_ns=begin;grant_clock=cpu.clocks();running=true;
        const auto result=cpu.run(budget);running=false;++grants;return result;
    }
    void bind_latch(CompareTimer5250& value){
        latch=&value;
        latch->nmi_line=[&](bool asserted){nmi.emplace_back(running?now():provider_ns,asserted);if(running)cpu.abort_grant();};
    }
    void external_time(uint64_t ns){provider_ns=ns;}
    uint64_t deadline() const {
        auto result=audio?audio->deadline():ym.deadline();
        if(!reset.empty())result=std::min(result,reset.front().first);
        if(!nmi.empty())result=std::min(result,nmi.front().first);
        if(!modes.empty())result=std::min(result,modes.front().first);
        return result;
    }
    void queue_reset(uint64_t ns,bool asserted){reset.emplace_back(ns,asserted);}
    void supply_boot(uint64_t group){
        while(host_cursor<host.size()&&host[host_cursor].grant==group){pending.push_back(host[host_cursor++]);++supplied_host;}
    }
    // Assertions on native main transactions; no captured value is applied.
    void check_native_host(uint64_t group,uint64_t ns,unsigned kind,unsigned address,uint8_t data){
        if(host_cursor>=host.size())throw std::runtime_error("extra native sound host input");
        const auto& r=host[host_cursor++];
        if(r.grant!=group||r.ns!=ns||r.kind!=kind||r.address!=address||r.data!=data)throw std::runtime_error("native sound host input differs");
        ++native_host;
    }
    void advance(uint64_t ns){
        while(!pending.empty()&&pending.front().ns<=ns){const auto r=pending.front();pending.pop_front();provider_ns=r.ns;
            if(r.kind==4){bootstrap_latch.deliver_sound(r.data);++commands;}else bootstrap_io.write(r.address,r.data);
        }
        while(!reset.empty()&&reset.front().first<=ns){auto [at,d]=reset.front();reset.pop_front();cpu.reset_line(d);++resets;}
        while(!nmi.empty()&&nmi.front().first<=ns){auto [at,d]=nmi.front();nmi.pop_front();cpu.nmi_line(d);}
        while(!modes.empty()&&modes.front().first<=ns){auto [at,d]=modes.front();modes.pop_front();ym.mode(d,at);}
        if(audio)audio->advance(ns);else ym.advance(ns);
    }
    void finish(uint64_t last_group){
        if(speaker_assertions.is_open()&&speaker_assertions.peek()!=EOF)throw std::runtime_error("missing native speaker batches");
        if(audio)for(auto& in:audio_assertions)if(in.peek()!=EOF)throw std::runtime_error("missing native audio batches");
        if(irq_assertions.is_open()){irq_assertions>>std::ws;if(irq_assertions.peek()!=EOF)throw std::runtime_error("missing IRQ acknowledgements");if(!irq_checks||!cpu.coverage().reti)throw std::runtime_error("directed IRQ/RETI not exercised");}
        if((cursor<records.size()&&records[cursor].grant<=last_group)||(host_cursor<host.size()&&host[host_cursor].grant<=last_group))throw std::runtime_error("missing native sound transactions or host inputs");
    }
    void report() const{
        if(speaker_assertions.is_open())std::cout<<"Native mixed speaker: "<<speaker.samples()<<" stereo frames, "<<speaker_batches<<" batches; float bits identical, 0 differences/skips\n";
        const auto c=cpu.coverage();
        std::cout<<"Continuous native sound: "<<transactions<<'/'<<transactions<<" transactions and timestamps identical, 0 differences/skips; "<<grants<<" computed grants; "<<c.nmi_entries<<" NMI entries, "<<c.retn<<" RETN, "<<c.irq_entries<<" IRQ entries, "<<c.reti<<" RETI; "<<native_host<<" native and "<<supplied_host<<" supplied host writes\n";
        if(audio)for(unsigned i=0;i<2;++i)std::cout<<"Native "<<(i?"PCM":"FM")<<" stream: "<<audio_samples[i]<<" raw stereo samples and "<<audio_batches[i]<<" computed batch deadlines/indexes identical, 0 differences/skips\n";
        if(audio)std::cout<<"Native YM callbacks: "<<audio->mode_callbacks<<" modes ("<<audio->late_modes<<" late), "<<audio->timer_callbacks[0]<<" timer A, "<<audio->timer_callbacks[1]<<" timer B\n";
    }
private:
    std::vector<Record> records,host;size_t cursor{},host_cursor{};
    std::ifstream irq_assertions;unsigned irq_checks{};
    std::array<std::ifstream,2> audio_assertions;
    std::ifstream speaker_assertions;XBoardSpeaker speaker;uint64_t speaker_batches{};
    std::array<uint64_t,2> audio_samples{},audio_batches{};
    std::deque<Record> pending;
    std::deque<std::pair<uint64_t,uint8_t>> modes;
    std::deque<std::pair<uint64_t,bool>> reset,nmi;
    uint64_t pcm_samples{},grant_ns{},grant_clock{},current_grant{},provider_ns{};
    bool running{};CompareTimer5250* latch{};
    void update_pcm(){const auto target=now()/32000;while(pcm_samples<target){pcm.tick();++pcm_samples;}}
};
