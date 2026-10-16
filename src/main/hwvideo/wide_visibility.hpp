#pragma once
#include "hwvideo/wide_projection.hpp"
#include "hwvideo/scene_snapshot.hpp"
#include "hwvideo/wide_scenery.hpp"
#include "hwvideo/wide_request.hpp"
#include "engine/xboard_game.hpp"
#include <map>
#include <vector>

namespace Presentation {
// Passive native-producer observer. Detached copies own extended visibility;
// no emulated read, write, math endpoint, CPU clock or callback is invoked.
class WideVisibility {
    using Host=XBoardGame::Host;
    using Request=std::array<uint8_t,16>;
    struct Ghost {uint32_t object{};std::array<uint8_t,64> data{};unsigned size{};bool fresh=true;};
    using Extra=std::pair<Request,unsigned>;
    struct Submission {Request request;unsigned index;bool rejected;uint32_t object;std::array<uint8_t,64> data;};
    struct Bank {std::vector<WideDescriptor> full,half;std::vector<Extra> extras;std::vector<Submission> submitted;};
    struct WaterPosition {uint16_t x,y,depth;};
    std::map<uint32_t,WaterPosition> water_history;
    std::vector<uint32_t> water_frames;
    std::vector<Submission> collecting;
    std::map<uint32_t,std::vector<Submission>> submitted;
    std::map<uint32_t,Ghost> ghosts;
    std::map<uint32_t,std::vector<Extra>> pending;
    std::map<uint32_t,Bank> banks;
    std::vector<Request> horizon;
    uint32_t horizon_object{};
    std::array<uint32_t,4> previous_horizon{};uint32_t horizon_scene{};bool horizon_history{};
    std::vector<WideDescriptor> current;
    uint32_t serial{},selected{};bool interpolated{};uint16_t stage=0xffff,scene_mode=0xffff;
    Roms& rom;unsigned margin;
    WideSpriteProjection projector;
    MainBus::Memory scratch_memory{};
    MainBus scratch_bus;
    static uint16_t word(std::span<const uint8_t> b,unsigned i){return uint16_t(b[i]<<8|b[i+1]);}
    static uint32_t longword(std::span<const uint8_t> b,unsigned i){return uint32_t(word(b,i))<<16|word(b,i+2);}
    static void put(Request& b,unsigned i,uint16_t v){b[i]=v>>8;b[i+1]=v;}
    static std::span<const uint8_t> region(const Host& h,uint32_t a,unsigned n){auto o=MainBus::memory_offset(a);if(o==MainBus::UNMAPPED||o+n>h.memory.bytes.size())throw std::runtime_error("Wide observer address outside RAM");return {h.memory.bytes.data()+o,n};}
    static uint16_t read(const Host& h,uint32_t a){return word(region(h,a,2),0);}
    static uint32_t readlong(const Host& h,uint32_t a){return longword(region(h,a,4),0);}
    void submit(const Host& h,const Host::Entry& e){
        // $B628..$B64A copies A5[0..15], replacing byte 12 with A5[20].
        // Observe before the signed $C0 rejection. Never alter its count/bank.
        const auto object=e.state.registers.a[5];auto q=wide_request(region(h,object,21));
        const auto count=read(h,0x2a3cf0);
        std::array<uint8_t,64> data;std::copy_n(region(h,object,64).begin(),64,data.begin());
        collecting.push_back({q,count,native_request_rejected(count),MainBus::memory_offset(object),data});
    }
    Request request(MainBus& b,uint32_t p){Request r;for(unsigned i=0;i<16;++i)r[i]=b.read8(p+i);r[12]=b.read8(p+20);return r;}
    void sync_scene(const Host& h){
        const auto mode=read(h,0x29c09c),next_stage=read(h,0x29c060);
        if((mode!=1&&mode!=15&&mode!=16)||stage!=next_stage||scene_mode!=mode){ghosts.clear();water_history.clear();}
        stage=next_stage;scene_mode=mode;
    }
    void sync_scratch(const Host& h){
        std::copy_n(h.memory.bytes.begin(),0x10000,scratch_memory.bytes.begin());
    }
    void restore_scratch(const Host& h,uint32_t addr,unsigned count){
        const auto o=MainBus::memory_offset(addr);
        if(o!=MainBus::UNMAPPED&&o+count<=0x10000)std::copy_n(h.memory.bytes.begin()+o,count,scratch_memory.bytes.begin()+o);
    }
    void recover(const Host& h,const Host::Entry& e){
        sync_scene(h);
        auto regs=e.state.registers;const auto p=regs.a[5];
        const int half=int16_t(read(h,0x2a3800+unsigned(region(h,p+20,1)[0])*4))>>1;
        if(int16_t(read(h,p+4))>half+160+int(margin)||int16_t(read(h,p+4))<=-half-160-int(margin))return;
        restore_scratch(h,p,64);restore_scratch(h,0x29c080,0x100);
        if(!SceneryProjection::finish(scratch_bus,regs))return;
        Ghost g;g.object=p;g.size=unsigned(half*2);
        for(unsigned i=0;i<64;++i)g.data[i]=scratch_bus.read8(p+i);
        ghosts.emplace(++serial,g);
    }
    void extend_water(const Host& h,std::vector<Extra>& out,const std::vector<Submission>& generation,bool scratch_synced){
        if(scene_mode!=15)return;
        // $884E..$8860 distributes water over sixteen columns, 3*64 world
        // units apart. Repeat that native pattern in adjacent world cells;
        // retain its art, depth, camera projection and animation phase.
        constexpr int period=16*3*64;constexpr uint32_t scratch=0x3fc400;
        if(!scratch_synced)sync_scratch(h);
        auto& bus=scratch_bus;
        std::map<uint32_t,WaterPosition> next;
        for(const auto& s:generation){const auto frame=longword(s.request,12)&0xffffff;
            if(std::find(water_frames.begin(),water_frames.end(),frame)==water_frames.end())continue;
            for(int side:{-1,1}){
                for(unsigned n=0;n<64;++n)bus.write8(scratch+n,s.data[n]);
                bus.write16(scratch+42,word(s.data,42)+side*period);
                OMainObjects::Registers r;r.a[5]=scratch;r.a[6]=0x2e4000;
                auto& d=r.d;using namespace SceneryProjection;
                // $8AFE..$8B20: scale at the already-updated native depth.
                w(d[1],bus.read16(scratch+36));asr(d[1],5);if(SceneryProjection::s(d[1])<0)continue;
                w(d[1],d[1]+0x20);bus.write32(r.a[6],0x7fe0);bus.write16(r.a[6]+20,d[1]);w(d[7],bus.read16(r.a[6]+8));
                const auto pair=bus.read32(scratch+54);d[4]=bus.read32(pair);d[5]=bus.read32(pair+4);
                horizontal(bus,r);vertical(bus,r);
                auto q=request(bus,scratch);const uint32_t key=s.object*2+(side>0);
                WaterPosition position{word(q,4),word(q,6),word(s.data,36)};auto previous=water_history.find(key);
                if(previous!=water_history.end()&&position.depth<=previous->second.depth){put(q,8,previous->second.x);put(q,10,previous->second.y);}
                else{q[0]|=12;put(q,8,position.x);put(q,10,position.y);}
                next[key]=position;out.push_back({q,6});
            }
        }
        water_history=std::move(next);
    }
    void publish(const Host& h){
        sync_scene(h);
        auto& out=pending[readlong(h,0x2a3c04)&0xfffff];out.clear();
        submitted[readlong(h,0x2a3c04)&0xfffff]=std::move(collecting);collecting.clear();
        const bool need_scratch=!ghosts.empty()||scene_mode==15;
        if(need_scratch)sync_scratch(h);
        auto& bus=scratch_bus;
        for(auto i=ghosts.begin();i!=ghosts.end();){auto& g=i->second;
            const uint32_t width_addr=0x2a3800+unsigned(g.data[20])*4;
            for(unsigned n=0;n<64;++n)bus.write8(g.object+n,g.data[n]);
            // The pointed-to pair is shared camera/roll state, not object-owned
            // storage. Keep the current native pair when advancing the copy.
            bus.write16(width_addr,g.size);
            OMainObjects::Registers regs;regs.a[5]=g.object;regs.a[6]=0x2e4000;
            if(!g.fresh&&!SceneryProjection::update(bus,regs,margin)){
                restore_scratch(h,g.object,64);restore_scratch(h,width_addr,2);
                i=ghosts.erase(i);continue;
            }
            g.fresh=false;
            auto r=request(bus,g.object);out.push_back({r,2});
            auto projected=projector.project(r,std::span(scratch_memory.bytes).subspan(MainBus::SHARED,0x8000),{rom.subcpu.rom,rom.subcpu.length},margin);g.size=projected.width;
            for(unsigned n=0;n<64;++n)g.data[n]=bus.read8(g.object+n);
            restore_scratch(h,g.object,64);restore_scratch(h,width_addr,2);
            ++i;
        }
        extend_water(h,out,submitted[readlong(h,0x2a3c04)&0xfffff],need_scratch);
        // A controller can update fifteen positions without publishing its
        // strip (notably the canyon). Only extend actual producer submissions.
        const auto& generation=submitted[readlong(h,0x2a3c04)&0xfffff];
        const bool horizon_published=std::any_of(generation.begin(),generation.end(),[&](const auto& s){return s.object>=horizon_object&&s.object<horizon_object+15*64&&(s.object-horizon_object)%64==0;});
        if(horizon_published)for(auto& q:horizon)out.push_back({q,3});
        // Consume only this publication's controller output. Attract and
        // carrier sequences publish the same strip outside flight mode.
        horizon.clear();
    }
    void extend_horizon(const Host& h,const Host::Entry& e){
        const auto& r=e.state.registers;const auto p=r.a[5];horizon.clear();
        const uint32_t scene=uint32_t(read(h,0x29c09c))<<16|read(h,0x29c060);
        if(scene!=horizon_scene)horizon_history=false;horizon_scene=scene;
        horizon_object=MainBus::memory_offset(p);
        // $10ED8's exact 16.16 increments after its fifteen native segments.
        const uint32_t dx=r.d[1],dy=r.d[5],sx=r.d[0]-15*dx,sy=r.d[4]-15*dy;
        const unsigned extra=(margin+23)/24+1;
        Request base;std::copy_n(region(h,p,16).begin(),16,base.begin());base[12]=region(h,p+20,1)[0];
        for(int i=-int(extra);i<15+int(extra);++i){if(i>=0&&i<15)continue;auto q=base;
            put(q,4,uint16_t((sx+uint32_t(i)*dx)>>16));put(q,6,uint16_t((sy+uint32_t(i)*dy)>>16));
            if(horizon_history){put(q,8,uint16_t((previous_horizon[0]+uint32_t(i)*previous_horizon[2])>>16));put(q,10,uint16_t((previous_horizon[1]+uint32_t(i)*previous_horizon[3])>>16));}
            else{q[0]|=12;put(q,8,word(q,4));put(q,10,word(q,6));}
            horizon.push_back(q);
        }
        previous_horizon={sx,sy,dx,dy};horizon_history=true;
    }
    void begin_sub(const Host& h){
        const auto shared=std::span(h.memory.bytes).subspan(MainBus::SHARED,0x8000);
        const auto sorted=longword(shared,0x7c18)&0xfffff,requests=longword(shared,0x7c08)&0xfffff;
        auto& b=banks[sorted];
        if(shared[0x3a]&1){
            b.extras=pending[requests];
            b.submitted=submitted[requests];
            for(unsigned i=0;i<b.full.size();++i){auto& full=b.full[i];const unsigned offset=word(full.bytes,14);auto q=region(h,(requests|0x200000)+offset,16);
                if(!full.recovered){b.half[i]={};continue;}
                WideSpriteProjection::Result value{full.bytes,full.x,0};auto half=WideSpriteProjection::halfway(value,q);b.half[i]={half.descriptor,half.x,full.depth,true,1};}
            return;
        }
        b={};b.extras=pending[requests];b.submitted=submitted[requests];
    }
    void project_native(const Host& h,const Host::Entry& e){
        const auto& r=e.state.registers;auto shared=std::span(h.memory.bytes).subspan(MainBus::SHARED,0x8000);
        const auto sorted=longword(shared,0x7c18)&0xfffff;
        const unsigned slot=((r.a[4]&0xfffff)-sorted)/16;if(slot>=256)return;
        auto q=region(h,r.a[5]|0x200000,16);auto result=projector.project(q,shared,{rom.subcpu.rom,rom.subcpu.length},margin);
        result.descriptor[14]=region(h,r.a[4]|0x200000,16)[14];result.descriptor[15]=region(h,r.a[4]|0x200000,16)[15];
        auto& b=banks[sorted];b.full.resize(slot+1);b.half.resize(slot+1);
        b.full[slot]={result.descriptor,result.x,q[2],true,1};auto half=WideSpriteProjection::halfway(result,q);b.half[slot]={half.descriptor,half.x,q[2],true,1};
    }
    void latch(const Host& h){
        auto found=banks.find(selected);current.clear();if(found==banks.end())return;
        const auto& bank=found->second;const auto& projected=interpolated?bank.half:bank.full;
        std::array<bool,256> represented{};
        // Preserve the hardware's actual depth order and untouched palette metadata;
        // extended projection recovers hidden entries and far-left clipped columns.
        for(unsigned i=0;i<256;++i){auto native=std::span(h.memory.sprite_buffer).subspan(i*16,16);if(native[0]&0x80)break;
            if(i<bank.full.size()&&bank.full[i].recovered){
                const unsigned index=word(bank.full[i].bytes,14)/16;
                if(index<represented.size()){if(represented[index])continue;represented[index]=true;}
            }
            WideDescriptor d;std::copy(native.begin(),native.end(),d.bytes.begin());d.depth=i<projected.size()&&projected[i].recovered?projected[i].depth:0;
            if(i<projected.size()&&projected[i].recovered&&!(projected[i].bytes[0]&0x50)){d=projected[i];d.bytes[12]=native[12];if(!(native[0]&0x40)){d.recovered=false;d.kind=0;}}current.push_back(d);
        }
        const auto shared=std::span(h.memory.bytes).subspan(MainBus::SHARED,0x8000);
        auto extras=bank.extras;
        for(const auto& s:bank.submitted){
            // Accepted requests absent from the sorted bank were dropped by
            // the native 30-entry depth bucket. Keep native order untouched;
            // add missing requests in stable depth order in presentation only.
            const bool present=s.index<represented.size()&&represented[s.index];
            if(s.rejected||!present)extras.push_back({s.request,s.rejected?4u:5u});
        }
        for(auto& extra:extras){auto& q=extra.first;auto p=projector.project(q,shared,{rom.subcpu.rom,rom.subcpu.length},margin);if(interpolated)p=WideSpriteProjection::halfway(p,q);if(p.descriptor[0]&0x50)continue;
            WideDescriptor d{p.descriptor,p.x,q[2],true,extra.second};auto where=std::find_if(current.begin(),current.end(),[&](auto& x){return x.depth>d.depth;});current.insert(where,d);}
    }
public:
    explicit WideVisibility(Roms& r,unsigned m=39):rom(r),margin(m),scratch_bus(r.maincpu.rom,r.maincpu.length,r.subcpu.rom,r.subcpu.length,scratch_memory){
        const auto main=std::span(r.maincpu.rom,r.maincpu.length);
        // Native water animation header and seven frame pointers.
        for(unsigned i=0;i<main[0x3f918];++i)water_frames.push_back(longword(main,0x3f91c+i*4)&0xffffff);
    }
    void reset(){ghosts.clear();water_history.clear();pending.clear();banks.clear();submitted.clear();collecting.clear();horizon.clear();current.clear();horizon_history=false;stage=scene_mode=0xffff;selected=0;interpolated=false;}
    const std::vector<WideDescriptor>& descriptors()const{return current;}
    void observe(const Host::Phase& p,const Host& h){
        if(p.cpu==0&&p.write&&p.address>=0x110000&&p.address<0x120000){latch(h);return;}
        if(!p.entry)return;const auto pc=p.entry->state.pc;
        if(p.cpu==1){if(pc==0x648)begin_sub(h);if(pc==0x7b8)project_native(h,*p.entry);return;}
        if(pc==0xb628)submit(h,*p.entry);
        if(pc==0x8bb0||pc==0x8bb8){const auto sr=p.entry->state.sr;const bool less=bool(sr&8)!=bool(sr&2),equal=sr&4;
            if(pc==0x8bb0?(!equal&&!less):(equal||less))recover(h,*p.entry);}
        if(pc==0xb684)publish(h);
        if(pc==0x10fba)extend_horizon(h,*p.entry);
        if(pc==0xb7c2||pc==0xb7d2){selected=readlong(h,0x2a3c14)&0xfffff;interpolated=pc==0xb7d2;}
    }
};
}
