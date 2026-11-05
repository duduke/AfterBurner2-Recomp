#pragma once
#include "hwaudio/xboard_sound_cpu.hpp"
#include "hwaudio/object_audio_rendering.hpp"
#include <algorithm>
#include <array>
#include <compare>
#include <cstdint>
#include <deque>
#include <map>
#include <span>
#include <vector>

// Passive ownership metadata. Native sound commands, voices and samples are
// untouched. Extended rendering uses individually verified producer/coordinate
// contracts; unknown sources retain the existing category/stereo fallback.
namespace ObjectAudio {
struct Id {
    uint64_t epoch{},generation{};uint32_t record{};
    auto operator<=>(const Id&)const=default;
    explicit operator bool()const{return generation!=0;}
};
struct Position {
    // Raw native 16.16 X/Y and signed depth; not an OpenAL bearing or distance.
    int32_t x{},y{};int16_t depth{},player_x{},player_y{},player_depth{};
    bool operator==(const Position&)const=default;
};
struct Source {Id id;uint64_t ns{};uint32_t handler{};bool position_known{};Position position;};
struct Voice {Id id;uint16_t sequencer{};uint8_t command{};bool operator==(const Voice&)const=default;};
enum class Kind { born, retired, queued, delivered, sequencer, fm, pcm, reset };
struct Event {uint64_t ns{};Kind kind{};Id id;unsigned channel{};uint8_t command{};};
struct Frame {uint64_t number{},ns{},epoch{};std::vector<Source> sources;std::array<Voice,8> fm;std::array<Voice,16> pcm;};
class Tracking {
public:
    void configure(std::span<const uint8_t> sound,std::span<const uint8_t> main={}){rom=sound;main_rom=main;reset(0);}
    void reset(uint64_t ns){++epoch;slots={};lives.clear();trajectories.clear();clear_sound();events.clear();event(ns,Kind::reset,{});reset_asserted=false;}
    template<class Read> void prime(Read read){
        for(unsigned i=0;i<slots.size();++i){const auto p=0x3fa000+i*64;auto& s=slots[i];s.active=bool(read(p)&0x8000);s.generation=s.active?1:0;s.handler=longword(read,p+16);s.family=s.active&&s.handler==0x156ce;
            if(s.family)ensure(id(i),0);}
    }
    void context(uint32_t pc,uint32_t a5,uint32_t sp,uint32_t d1){ctx={pc,a5&0x3fffff,sp&0x3fffff,d1&0xffff};}
    template<class Phase,class Read> void observe_main(const Phase& p,uint16_t mode,Read read){
        if(p.cpu||p.space)return;
        if(p.entry&&p.entry->kind==1){const auto& s=p.entry->state;
            if(s.pc==0xb868||s.pc==0xb81e)context(s.pc,s.registers.a[5],s.sp,s.registers.d[1]);}
        if(p.write)main_write(p.ns,p.pc,p.address,p.data,p.mask,mode,read);
    }
    template<class Read> void main_write(uint64_t ns,uint32_t pc,uint32_t address,uint16_t data,uint16_t mask,uint16_t mode,Read read){
        const auto a=address&0x3fffff;
        if(a==0x140004&&(mask&255)){
            const bool asserted=!(data&1);
            if(asserted&&!reset_asserted){++epoch;for(auto& [key,life]:lives)if(life.dead==never)life.dead=ns;
                clear_sound();event(ns,Kind::reset,{});
                for(unsigned i=0;i<slots.size();++i)if(slots[i].active&&slots[i].family){slots[i].born=ns;ensure(id(i),ns);}}
            reset_asserted=asserted;
        }
        if(a>=0x3fa000&&a<0x3fe000){
            const unsigned i=(a-0x3fa000)/64,o=a&63;auto& s=slots[i];
            if(o==0&&(mask&0xff00)){
                const bool active=bool(data&0x8000);
                if(active&&!s.active){++s.generation;s.born=ns;s.family=false;}
                if(!active&&s.active){retire(id(i),ns);s.family=false;}
                s.active=active;
            }
            if(o==16||o==18){
                const unsigned shift=o==16?16:0;const uint32_t lanes=uint32_t(mask)<<shift;
                s.handler=(s.handler&~lanes)|(uint32_t(data&mask)<<shift);
                if(s.family&&s.handler!=0x156ce){retire(id(i),ns);s.family=false;}
                if(o==18&&mask==0xffff&&s.active&&s.handler==0x156ce){s.family=true;ensure(id(i),s.born);}
            }
        }
        // $B868 writes the exact main-CPU ring slot. A validated return address
        // $1571C identifies the $15716 flyby producer, including interrupted calls.
        for(unsigned lane=0;lane<2;++lane){const unsigned bits=lane?255:0xff00;if(!(mask&bits))continue;
            const auto byte_address=a+lane;const auto command=uint8_t(lane?data:data>>8);
            if(byte_address>=0x3f890c&&byte_address<0x3f894c){
                Tag tag{};
                if(pc==0xb868&&ctx.pc==pc&&mode==16&&pool(ctx.a5)&&ctx.sp>=0x3f8000&&ctx.sp<=0x3ffffc){
                    const auto i=(ctx.a5-0x3fa000)/64;const auto key=id(i);
                    const auto caller=longword(read,ctx.sp);
                    const bool flyby=(command==0x6e||command==0x6f)&&caller==0x1571c&&slots[i].family&&longword(read,ctx.a5+16)==0x156ce;
                    // $14D9A queues $D8 from the firing aircraft in A5, after
                    // allocating weapon/shadow A1/A2. No coordinate contract is
                    // assumed for the shooter; its identity is still verifiable.
                    const bool launch=command==0xd8&&caller==0x14da0;
                    const bool missile=!main_rom.empty()&&caller==0xa026&&table_command(0xa716,16,command);
                    const bool explosion=!main_rom.empty()&&caller==0x91ac&&table_command(0x91b8,8,command);
                    if(slots[i].active&&(read(ctx.a5)&0x8000)&&(flyby||launch||missile||explosion)){
                        if(!main_rom.empty())slots[i].coordinates=flyby?1:launch||missile?2:3;
                        ensure(key,slots[i].born);tag={key,command};event(ns,Kind::queued,key,unsigned(byte_address-0x3f890c),command);
                    }
                }
                // $7B68/$7B6E activates the adjacent lock marker; $7B78
                // copies the aircraft's projected location. Attribute its native
                // FM lock cue to that live parent, never to the reused marker.
                if(!main_rom.empty()&&pc==0xb868&&ctx.pc==pc&&mode==16&&command==0xe0&&pool(ctx.a5)&&pool(ctx.a5-64)&&ctx.sp>=0x3f8000&&ctx.sp<=0x3ffffc&&longword(read,ctx.sp)==0x7bac&&longword(read,ctx.a5+16)==0x7bb6){
                    const unsigned parent=(ctx.a5-64-0x3fa000)/64;
                    if(slots[parent].active&&(read(ctx.a5-64)&0x8000)){
                        slots[parent].coordinates=2;const auto key=id(parent);ensure(key,slots[parent].born);tag={key,command};event(ns,Kind::queued,key,unsigned(byte_address-0x3f890c),command);
                    }
                }
                ring[byte_address-0x3f890c]=tag;
            }
            if(byte_address==0x3f8906&&!command)ring={};
        }
        if((a&~1u)==0xe8016&&(mask&255)){
            Tag tag{};const auto command=uint8_t(data);
            if(pc==0xb81e&&ctx.pc==pc&&ctx.d1<64){tag=ring[ctx.d1];ring[ctx.d1]={};if(tag.command!=command)tag={};}
            if(latches.size()<128)latches.push_back({ns,tag,command});
            else {latches.clear();latch={};} // Unknown ownership never guesses a source.
            if(tag.id)event(ns,Kind::delivered,tag.id,0,command);
        }
    }
    static bool interesting(const XBoardSoundCpu::Event& e){
        const unsigned k=unsigned(e.kind),a=e.address;
        return (k==2&&a>=0x40&&a<0x80) || (k==3&&a<=1) ||
            (k<2&&((a>=0xf800&&a<0xf818)||(k==0&&(e.pc==0xc76||e.pc==0x17a2))||
                (k==1&&a>=0xf820&&a<=0xfe82&&(a-0xf820)%43==0)||
                (k==1&&a>=0xf000&&a<0xf100&&(a&0x87)==0x86)));
    }
    void observe_sound(const XBoardSoundCpu::Event& e,const XBoardSoundCpu::Registers& r,uint64_t ns){
        const auto k=e.kind;const unsigned a=e.address;using K=XBoardSoundCpu::Kind;
        if(k==K::IoRead&&a>=0x40&&a<0x80){
            while(!latches.empty()&&latches.front().ns<=ns){latch=latches.front().tag;latched_command=latches.front().command;latches.pop_front();}
            nmi=(e.pc==0x7a&&latched_command==e.data)?latch:Tag{};
        }
        if(k==K::MemoryRead&&a>=0xf800&&a<0xf818){
            const auto tag=ram[a-0xf800];
            if((e.pc==0x6eb||e.pc==0x702)&&e.data!=0x80)current=tag.command==e.data?tag:Tag{};
            if(e.pc==0x6f7||e.pc==0x70c)transfer={e.pc,e.data,tag};
        }
        if(k==K::MemoryWrite&&a>=0xf800&&a<0xf818){
            Tag tag{};
            if(e.pc==0x7c&&nmi.command==e.data)tag=nmi;
            if((e.pc==0x6f7||e.pc==0x70c)&&transfer.pc==e.pc&&transfer.byte==e.data)tag=transfer.tag;
            ram[a-0xf800]=tag;
        }
        // $C76 initializes count $2B-byte sequencer records from the ROM root.
        if(k==K::MemoryRead&&e.pc==0xc76&&a!=e.pc){
            Tag tag=verified(current,ns);
            if(!tag.id||a!=root(tag.command))tag={};
            if(record(r.de)>=0&&e.data<=39-unsigned(record(r.de)))for(unsigned i=0;i<e.data;++i){
                records[unsigned(record(r.de))+i]=tag;
                if(tag.id)event(ns,Kind::sequencer,tag.id,unsigned(record(r.de))+i,tag.command);
            }
        }
        if(k==K::MemoryRead&&e.pc==0x17a2&&a==e.pc){
            direct=verified(current,ns);
            if(!direct.id||root(direct.command)!=r.bc)direct={};
        }
        if(k==K::MemoryWrite&&record(a)>=0&&!(e.data&0x80))records[unsigned(record(a))]={};
        const Tag tag=verified(r.ix==0xf808?direct:record(r.ix)>=0?records[unsigned(record(r.ix))]:Tag{},ns);
        if(k==K::MemoryWrite&&a>=0xf000&&a<0xf100&&(a&0x87)==0x86){
            const unsigned channel=(a&0x7f)/8;
            assign(pcm[channel],(e.data&1)?Tag{}:tag,r.ix,ns,Kind::pcm,channel);
        }
        if(k==K::IoWrite&&a==0)ym_address=e.data;
        if(k==K::IoWrite&&a==1){
            int channel=ym_address>=0x20?ym_address&7:ym_address==8?e.data&7:-1;
            // Key-off retains the owner of the release tail. A new physical
            // write/owner or source retirement invalidates it; no sound is replayed.
            if(channel>=0 && !(ym_address==8&&!(e.data&0x78)))assign(fm[unsigned(channel)],tag,r.ix,ns,Kind::fm,unsigned(channel));
        }
    }
    bool alive(Id key,uint64_t ns)const{const auto it=lives.find(key);return bool(key)&&it!=lives.end()&&it->second.born<=ns&&ns<it->second.dead;}
    std::array<Voice,8> fm{};std::array<Voice,16> pcm{};
    template<class Read> Frame frame(uint64_t number,uint64_t ns,Read read){
        prune(ns);
        Frame f{number,ns,epoch,{},fm,pcm};
        for(unsigned i=0;i<slots.size();++i)if(slots[i].active&&alive(id(i),ns)){
            const auto p=0x3fa000+64*i;const auto handler=longword(read,p+16);
            if(!(read(p)&0x8000))continue;
            Source source{id(i),ns,handler,slots[i].family&&handler==0x156ce,{}};
            if(source.position_known)source.position={int32_t(longword(read,p+24)),int32_t(longword(read,p+30)),int16_t(read(p+36)),int16_t(read(0xffff8d14)),int16_t(read(0xffff8d16)),int16_t(read(0xffff8d18))};
            f.sources.push_back(source);
            if(!main_rom.empty()){
                const unsigned kind=slots[i].family?1:slots[i].coordinates;
                float x=0,y=0,z=int16_t(read(p+36))-int16_t(read(0xffff8d18));
                if(kind==1){x=float(int32_t(longword(read,p+24)))/65536-int16_t(read(0xffff8d14));y=float(int32_t(longword(read,p+30)))/65536-int16_t(read(0xffff8d16));}
                if(kind==2){x=int16_t(read(p+42))-int16_t(read(0xffff8d14));y=int16_t(read(p+44))-int16_t(read(0xffff8d16));}
                if(kind==3){const auto bearing=world_bearing(read,p);if(bearing)remember(source.id,ns,*bearing);continue;}
                if(kind==1||kind==2){
                    // $15200/$A2CA use the same signed ROM sine/cosine
                    // frame. Positive native X is not screen-right at roll 0.
                    const unsigned angle=read(0x29c09e)&255;
                    const float sine=int8_t(main_rom[0x33e98+angle]),cosine=int8_t(main_rom[0x33e58+angle]);
                    const float view_x=(cosine*x+sine*y)/128;
                    y=-(sine*x-cosine*y)/128;x=view_x;
                }
                if(kind)remember(source.id,ns,{x,y,-z});
            }
        }
        for(auto& v:f.fm)if(!alive(v.id,ns))v={};for(auto& v:f.pcm)if(!alive(v.id,ns))v={};return f;
    }
    std::vector<Event> take_events(uint64_t through=never){
        std::stable_sort(events.begin(),events.end(),[](const auto& a,const auto& b){return a.ns<b.ns;});
        const auto end=std::upper_bound(events.begin(),events.end(),through,[](uint64_t ns,const auto& e){return ns<e.ns;});
        std::vector<Event> out(events.begin(),end);events.erase(events.begin(),end);
        if(through!=never)prune(through);
        return out;
    }
    uint64_t current_epoch()const{return epoch;}
    bool render(Id key,uint64_t ns,std::array<float,directions>& out,bool smooth=true)const{
        if(!alive(key,ns))return false;
        const auto it=trajectories.find(key);if(it==trajectories.end())return false;
        const auto& path=it->second;auto end=std::upper_bound(path.begin(),path.end(),ns,[](uint64_t t,const Point& p){return t<p.ns;});
        if(end==path.begin())return false;
        const auto& p=*--end;auto bearing=p.bearing;
        if(smooth&&end!=path.begin()){const auto& previous=*(end-1);const float t=std::min(1.f,float(ns-p.ns)/16000000.f);for(unsigned axis=0;axis<3;++axis)bearing[axis]=previous.bearing[axis]+(p.bearing[axis]-previous.bearing[axis])*t;}
        out=weights(bearing[0],bearing[1],bearing[2]);return true;
    }
private:
    static constexpr uint64_t never=UINT64_MAX;
    struct Slot {uint64_t generation{},born{};uint32_t handler{};bool active{},family{};unsigned coordinates{};};
    struct Life {uint64_t born{},dead=never;};
    struct Tag {Id id;uint8_t command{};};
    struct Latch {uint64_t ns;Tag tag;uint8_t command;};
    struct Context {uint32_t pc{},a5{},sp{},d1{};}ctx;
    struct Transfer {unsigned pc{},byte{};Tag tag;}transfer;
    std::span<const uint8_t> rom,main_rom;
    struct Point {uint64_t ns;std::array<float,3> bearing;};
    std::map<Id,std::deque<Point>> trajectories;uint64_t epoch{};bool reset_asserted{};uint8_t ym_address{},latched_command{};
    std::array<Slot,256> slots{};std::map<Id,Life> lives;
    std::array<Tag,64> ring{};std::array<Tag,24> ram{};std::array<Tag,39> records{};
    Tag latch{},nmi{},current{},direct{};std::deque<Latch> latches;std::vector<Event> events;
    template<class Read> static uint32_t longword(Read read,uint32_t a){return uint32_t(read(a))<<16|read(a+2);}
    static bool pool(uint32_t a){return a>=0x3fa000&&a<0x3fe000&&!(a&63);}
    static int record(unsigned a){return a>=0xf820&&a<=0xfe82&&(a-0xf820)%43==0?int((a-0xf820)/43):-1;}
    Id id(unsigned i)const{return {epoch,slots[i].generation,0x3fa000+64*i};}
    unsigned root(unsigned command)const{const auto a=0x735+2*(command-0x40);return command>=0x40&&a+1<rom.size()?rom[a]|unsigned(rom[a+1])<<8:UINT32_MAX;}
    bool table_command(unsigned a,unsigned n,uint8_t c)const{return a+n<=main_rom.size()&&std::find(main_rom.begin()+a,main_rom.begin()+a+n,c)!=main_rom.begin()+a+n;}
    void remember(Id key,uint64_t ns,std::array<float,3> bearing){auto& path=trajectories[key];path.push_back({ns,bearing});while(path.size()>128)path.pop_front();}
    void ensure(Id key,uint64_t ns){if(!lives.contains(key)){lives[key]={ns,never};event(ns,Kind::born,key);}}
    void retire(Id key,uint64_t ns){auto it=lives.find(key);if(it!=lives.end()&&it->second.dead==never){it->second.dead=ns;event(ns,Kind::retired,key);}}
    Tag verified(Tag tag,uint64_t ns)const{return alive(tag.id,ns)?tag:Tag{};}
    void assign(Voice& voice,Tag tag,uint16_t seq,uint64_t ns,Kind kind,unsigned channel){
        const Voice next{tag.id,tag.id?seq:uint16_t(0),tag.command};
        if(voice==next)return;voice=next;event(ns,kind,next.id,channel,next.command);
    }
    void clear_sound(){ring={};ram={};records={};latch=nmi=current=direct={};latches.clear();fm={};pcm={};ctx={};transfer={};ym_address=latched_command=0;}
    void prune(uint64_t ns){std::erase_if(trajectories,[&](const auto& item){return !lives.contains(item.first);});std::erase_if(lives,[&](const auto& item){return item.second.dead!=never&&ns>item.second.dead&&ns-item.second.dead>1000000000;});}
    void event(uint64_t ns,Kind kind,Id key,unsigned channel=0,uint8_t command=0){if(events.size()<4096)events.push_back({ns,kind,key,channel,command});}
};
}
