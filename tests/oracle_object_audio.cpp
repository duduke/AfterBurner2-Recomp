#include "hwaudio/object_audio_tracking.hpp"
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
#include <stdexcept>
#include <string>
using namespace ObjectAudio;
void require(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
struct Memory {
    std::array<uint8_t,0x8000> m{};
    uint16_t word(uint32_t a)const{a&=0x7fff;return unsigned(m[a])<<8|m[(a+1)&0x7fff];}
    void word(uint32_t a,uint16_t d){a&=0x7fff;m[a]=d>>8;m[(a+1)&0x7fff]=d;}
    void longword(uint32_t a,uint32_t d){word(a,d>>16);word(a+2,d);}
};
// Independent event-provenance audit. Queue copies follow the captured data
// source and DE destination; producer identities come from actual MAME A5,
// active-slot edges and the caller on the original 68000 stack.
struct Audit {
    struct Tag {Id id;uint8_t byte{};};
    uint64_t epoch=1;std::array<uint64_t,256> generation{},birth{};std::array<bool,256> active{};
    std::map<Id,uint64_t> dead;
    std::array<Tag,64> ring{};std::array<Tag,24> ram{};std::array<Tag,39> sequence{};
    std::array<Id,8> fm{};std::array<Id,16> pcm{};
    struct Delivery{uint64_t ns;uint8_t byte;Tag tag;};std::deque<Delivery> delivered;
    Tag latch{},input{},executing{},copied{},direct{};uint8_t latch_byte{},copied_byte{};unsigned copied_destination=0,ym=0;bool reset=false;
    unsigned origins{},assignments{},reuse{};bool expanded{};std::span<const uint8_t> main_rom;
    Id key(unsigned i)const{return {epoch,generation[i],0x3fa000+64*i};}
    bool valid(Id id,uint64_t ns)const{return bool(id)&&dead.contains(id)&&ns<dead.at(id);}
    static int record(unsigned a){return a>=0xf820&&a<=0xfe82&&(a-0xf820)%43==0?int((a-0xf820)/43):-1;}
    void main(uint64_t ns,unsigned pc,unsigned a,unsigned d,unsigned mask,unsigned obj,unsigned d1,unsigned ret,unsigned mode){
        if(a==0x140004&&(mask&255)){
            const bool asserted=!(d&1);
            if(asserted&&!reset){++epoch;for(auto& [id,end]:dead)if(end==UINT64_MAX)end=ns;ring={};ram={};sequence={};fm={};pcm={};delivered.clear();latch=input=executing=copied={};}
            reset=asserted;
        }
        if(a>=0x3fa000&&a<0x3fe000&&(a&63)==0&&(mask&0xff00)){
            const unsigned i=(a-0x3fa000)/64;const bool now=d&0x8000;
            if(now&&!active[i]){reuse+=generation[i]>0;++generation[i];birth[i]=ns;}
            if(!now&&active[i]&&dead.contains(key(i)))dead[key(i)]=ns;
            active[i]=now;
        }
        for(unsigned lane=0;lane<2;++lane)if(mask&(lane?255:0xff00)){
            const auto addr=a+lane;const uint8_t byte=lane?d:d>>8;
            if(addr>=0x3f890c&&addr<0x3f894c){
                Tag tag{};
                if(pc==0xb868&&mode==16&&obj>=0x3fa000&&obj<0x3fe000&&!(obj&63)&&((byte==0xd8&&ret==0x14da0)||(expanded&&((ret==0xa026&&std::find(main_rom.begin()+0xa716,main_rom.begin()+0xa726,byte)!=main_rom.begin()+0xa726)||(ret==0x91ac&&std::find(main_rom.begin()+0x91b8,main_rom.begin()+0x91c0,byte)!=main_rom.begin()+0x91c0))))&&active[(obj-0x3fa000)/64]){
                    tag={key((obj-0x3fa000)/64),byte};dead.try_emplace(tag.id,UINT64_MAX);++origins;
                }
                if(expanded&&pc==0xb868&&mode==16&&ret==0x7bac&&byte==0xe0&&obj>=0x3fa040&&obj<0x3fe000&&active[(obj-64-0x3fa000)/64]){tag={key((obj-64-0x3fa000)/64),byte};dead.try_emplace(tag.id,UINT64_MAX);++origins;}
                ring[addr-0x3f890c]=tag;
            }
            if(addr==0x3f8906&&!byte)ring={};
        }
        if((a&~1u)==0xe8016&&(mask&255)){
            Tag tag{};if(pc==0xb81e&&(d1&65535)<64){tag=ring[d1&65535];ring[d1&65535]={};if(tag.byte!=uint8_t(d))tag={};}
            delivered.push_back({ns,uint8_t(d),tag});
        }
    }
    void sound(unsigned k,unsigned pc,unsigned a,unsigned value,unsigned ix,unsigned de,unsigned bc,uint64_t ns,std::span<const uint8_t> rom){
        if(k==2&&a>=64&&a<128){while(!delivered.empty()&&delivered.front().ns<=ns){latch=delivered.front().tag;latch_byte=delivered.front().byte;delivered.pop_front();}input=pc==0x7a&&latch_byte==value?latch:Tag{};}
        if(k==0&&a>=0xf800&&a<0xf818){
            const auto origin=ram[a-0xf800];
            if((pc==0x702||pc==0x6eb)&&value!=128)executing=origin.byte==value?origin:Tag{};
            copied=origin;copied_byte=value;copied_destination=de;
        }
        if(k==1&&a>=0xf800&&a<0xf818){Tag tag{};
            if(pc==0x7c&&input.byte==value)tag=input;
            else if(a==copied_destination&&value==copied_byte)tag=copied;
            ram[a-0xf800]=tag;
        }
        if(k==0&&pc==0xc76&&a!=pc&&record(de)>=0){
            Tag tag=executing;
            const unsigned p=0x735+2*(unsigned(tag.byte>=0x40?tag.byte:0x40)-0x40),root=rom[p]|unsigned(rom[p+1])<<8;
            if(!valid(tag.id,ns)||a!=root)tag={};
            for(unsigned i=0;i<value&&unsigned(record(de))+i<39;++i)sequence[unsigned(record(de))+i]=tag;
        }
        if(k==1&&record(a)>=0&&!(value&128))sequence[unsigned(record(a))]={};
        if(k==0&&pc==0x17a2&&a==pc){direct=executing;const unsigned p=0x735+2*(unsigned(direct.byte>=0x40?direct.byte:0x40)-0x40);if(!valid(direct.id,ns)||bc!=(rom[p]|unsigned(rom[p+1])<<8))direct={};}
        Id owner=ix==0xf808?direct.id:record(ix)>=0?sequence[unsigned(record(ix))].id:Id{};if(!valid(owner,ns))owner={};
        if(k==1&&a>=0xf000&&a<0xf100&&(a&0x87)==0x86)pcm[(a&127)/8]=(value&1)?Id{}:owner;
        if(k==3&&a==0)ym=value;
        if(k==3&&a==1){int ch=ym>=32?ym&7:ym==8?value&7:-1;if(ch>=0&&!(ym==8&&!(value&0x78)))fm[unsigned(ch)]=owner;}
    }
};
int main(int argc,char** argv)try{
    std::ifstream data(argc>1?argv[1]:"build/oracle/object-audio/weapons-v4.txt");std::string line;require(bool(std::getline(data,line))&&(line=="AB2OBJ02"||line=="AB2OBJ03"),"object oracle version/missing capture");
    std::ifstream sound("roms/epr-11112.17",std::ios::binary);std::vector<uint8_t> rom((std::istreambuf_iterator<char>(sound)),{});require(rom.size()>=0xf000,"sound ROM required");
    const bool expanded=line=="AB2OBJ03";
    std::vector<uint8_t> main_rom(0x40000);if(expanded){for(unsigned lane=0;lane<2;++lane){std::ifstream in(lane?"roms/epr-11108.63":"roms/epr-11107.58",std::ios::binary);std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});require(bytes.size()==0x20000,"main ROM required");for(unsigned i=0;i<bytes.size();++i)main_rom[2*i+lane]=bytes[i];}}
    Tracking tracker;Memory memory;auto read=[&](auto a){return memory.word(a);};tracker.configure(rom,expanded?std::span<const uint8_t>(main_rom):std::span<const uint8_t>{});tracker.prime(read);Audit audit;audit.expanded=expanded;audit.main_rom=main_rom;
    uint64_t rows=0;unsigned frames=0,position_rows=0,owned_fm=0,owned_pcm=0;std::set<Id> seen;
    std::vector<std::pair<unsigned,Position>> positions;unsigned family_positions=0,flyby_predicates=0,flyby_eligible=0;std::vector<std::pair<unsigned,std::array<float,3>>> bearings;
    while(std::getline(data,line)){
        std::istringstream row(line);char kind;row>>kind;++rows;
        if(kind=='M'){
            uint64_t ns;unsigned pc,a,d,mask,obj,sp,d1,ret,mode,handler,flags,x,y,z,px,py,pz;
            require(bool(row>>ns>>pc>>a>>d>>mask>>obj>>sp>>d1>>ret>>mode>>handler>>flags>>x>>y>>z>>px>>py>>pz),"truncated main record");
            if(pc==0xb868&&sp>=0x3f8000)memory.longword(sp,ret);
            if(handler){memory.word(obj,uint16_t(flags<<8));memory.longword(obj+16,handler);memory.longword(obj+24,x);memory.longword(obj+30,y);memory.word(obj+36,z);memory.word(0xffff8d14,px);memory.word(0xffff8d16,py);memory.word(0xffff8d18,pz);}
            tracker.context(pc,obj,sp,d1);tracker.main_write(ns,pc,a,d,mask,mode,read);audit.main(ns,pc,a,d,mask,obj,d1,ret,mode);
            if(a>=0x3f8000&&a<0x400000)memory.word(a,(memory.word(a)&~mask)|(d&mask));
        }else if(kind=='Z'){
            uint64_t ns;unsigned k,pc,a,d,ix,de,bc,af;require(bool(row>>ns>>k>>pc>>a>>d>>ix>>de>>bc>>af),"truncated sound record");
            XBoardSoundCpu::Event e{XBoardSoundCpu::Kind(k),0,uint16_t(pc),uint16_t(a),uint8_t(d)};XBoardSoundCpu::Registers r{};r.ix=ix;r.de=de;r.bc=bc;r.af=af;
            tracker.observe_sound(e,r,ns);audit.sound(k,pc,a,d,ix,de,bc,ns,rom);
            for(unsigned i=0;i<8;++i){require(tracker.fm[i].id==audit.fm[i],"FM ownership differs at row "+std::to_string(rows));if(tracker.fm[i].id){++owned_fm;seen.insert(tracker.fm[i].id);}}
            for(unsigned i=0;i<16;++i){require(tracker.pcm[i].id==audit.pcm[i],"PCM ownership differs at row "+std::to_string(rows));if(tracker.pcm[i].id){++owned_pcm;seen.insert(tracker.pcm[i].id);}}
        }else if(kind=='P'){
            unsigned p,x,y,z,px,py,pz;uint64_t ns;require(bool(row>>p>>x>>y>>z>>px>>py>>pz>>ns),"truncated position record");
            memory.longword(p+24,x);memory.longword(p+30,y);memory.word(p+36,z);memory.word(0xffff8d14,px);memory.word(0xffff8d16,py);memory.word(0xffff8d18,pz);
            positions.push_back({p,{int32_t(x),int32_t(y),int16_t(z),int16_t(px),int16_t(py),int16_t(pz)}});
        }else if(kind=='B'){
            uint64_t ns;unsigned p,depth,player;require(bool(row>>ns>>p>>depth>>player),"truncated flyby predicate");
            require(p>=0x3fa000&&p<0x3fe000&&!(p&63),"flyby predicate source");++flyby_predicates;flyby_eligible+=uint16_t(depth-0x1fe0)<player;
        }else if(kind=='Q'){
            unsigned p,k,x,y,z,px,py,pz,base,bx,by,cos,sin,angle;uint64_t ns;
            require(bool(row>>p>>k>>x>>y>>z>>px>>py>>pz>>base>>bx>>by>>cos>>sin>>angle>>ns),"truncated family position");
            memory.word(p+42,x);memory.word(p+44,y);memory.word(p+36,z);memory.word(0xffff8d14,px);memory.word(0xffff8d16,py);memory.word(0xffff8d18,pz);
            memory.longword(p+54,base);if(k==3&&base>=0x3f8000&&base<=0x3ffff8){memory.longword(base,bx);memory.longword(base+4,by);}memory.word(0x29c092,cos);memory.word(0x29c090,sin);memory.word(0x29c09e,angle);
            float dx=int16_t(x)-int16_t(px),dy=int16_t(y)-int16_t(py);
            if(k==3){dx=(float(int16_t(cos))*int16_t(x)+int32_t(bx))/128;dy=(float(int16_t(sin))*int16_t(x)-int32_t(by))/128;}
            if(k==2){const float sn=int8_t(main_rom[0x33e98+(angle&255)]),cs=int8_t(main_rom[0x33e58+(angle&255)]);const float view=(cs*dx+sn*dy)/128;dy=-(sn*dx-cs*dy)/128;dx=view;}else dy=-dy;
            bearings.push_back({p,{dx,dy,float(int16_t(pz)-int16_t(z))}});
        }else if(kind=='F'){
            unsigned n;uint64_t ns;require(bool(row>>n>>ns)&&n==++frames,"frame gap");const auto f=tracker.frame(n,ns,read);
            unsigned actual=0;for(const auto& s:f.sources)if(s.position_known){++actual;const auto it=std::find_if(positions.begin(),positions.end(),[&](const auto& p){return p.first==s.id.record;});
                require(it!=positions.end()&&it->second==s.position,"raw missile coordinate witness mismatch");require(s.id==audit.key((s.id.record-0x3fa000)/64),"coordinate generation mismatch");}
            for(const auto& [p,b]:bearings){const auto id=audit.key((p-0x3fa000)/64);std::array<float,directions> w{};require(tracker.render(id,ns,w,false),"family trajectory unavailable");const auto expected=weights(b[0],b[1],b[2]);for(unsigned j=0;j<directions;++j)require(std::abs(w[j]-expected[j])<1e-5,"family bearing differs from MAME");++family_positions;}bearings.clear();
            require(actual==positions.size(),"missing/extra missile trajectory source");position_rows+=actual;positions.clear();tracker.take_events(ns);
        }else throw std::runtime_error("unknown object oracle record");
    }
    std::cerr<<"coverage: frames="<<frames<<" positions="<<position_rows<<" origins="<<audit.origins<<" owned FM/PCM="<<owned_fm<<'/'<<owned_pcm<<" identities="<<seen.size()<<" reuse="<<audit.reuse<<'\n';
    require(!expanded||(owned_fm&&family_positions),"natural FM/family position coverage missing");
    require(data.eof()&&positions.empty()&&frames>=8000&&position_rows&&audit.origins&&owned_pcm&&seen.size()>=2&&audit.reuse,"object/audio positive/lifecycle coverage missing");
    std::cout<<"Object audio MAME: "<<rows<<" of "<<rows<<" records identical, 0 skipped; frames="<<frames<<" raw missile positions="<<position_rows<<" launch origins="<<audit.origins<<" owned FM/PCM observations="<<owned_fm<<'/'<<owned_pcm<<" identities="<<seen.size()<<" flyby predicates/eligible="<<flyby_predicates<<'/'<<flyby_eligible<<" family positions="<<family_positions<<" slot reuses="<<audit.reuse<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
