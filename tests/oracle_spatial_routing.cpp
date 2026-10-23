#include "engine/xboard_game.hpp"
#include <fstream>
#include <iostream>
#include <set>
uint64_t number(std::ifstream& in,unsigned n){uint64_t x=0;while(n--){auto b=in.get();if(b==EOF)throw std::runtime_error("truncated spatial oracle");x=(x<<8)|unsigned(b);}return x;}
void tag(std::ifstream& in,const char* wanted){char b[8];if(!in.read(b,8)||std::string_view(b,8)!=std::string_view(wanted,8))throw std::runtime_error("invalid spatial oracle");}
// These are presentation decisions, independently asserted against the actual
// MAME allocation records below. They do not supply execution or device state.
struct RoutingAudit {
    using Group=CockpitRouting::Group;
    std::span<const uint8_t> rom;
    std::map<uint16_t,Group> descriptors;
    std::array<Group,39> owners{};
    Group direct=CockpitRouting::neutral;
    uint8_t ym_address{};
    std::set<uint16_t> seen_descriptors,seen_direct;
    uint64_t checked_pcm{},checked_fm{};
    unsigned root(unsigned command)const{const auto a=0x735+2*(command-0x40);return rom[a]|unsigned(rom[a+1])<<8;}
    explicit RoutingAudit(std::span<const uint8_t> program):rom(program){
        // Propulsion; passing the player's depth; enemy weapon launch; credit,
        // timer and reload cues. See the main producer addresses in the router.
        for(auto command:{0x40,0x41,0x42,0x43,0x6e,0x6f,0xda,0xf6})descriptors[root(command)]=CockpitRouting::rear;
        for(auto command:{0xd8,0x9b})descriptors[root(command)]=CockpitRouting::front;
        for(auto command:{0x7e,0x9a,0x91,0x92,0x93,0x94,0x95,0x96})descriptors[root(command)]=CockpitRouting::neutral;
    }
    static Group direct_policy(unsigned sample){
        switch(sample){
        case 0x11:case 0x18:return CockpitRouting::rear; // impact/debris table
        case 0x1a:return CockpitRouting::front; // cannon
        case 0x06:case 0x26:case 0x28:case 0x46:return CockpitRouting::neutral;
        default:throw std::runtime_error("unreviewed direct sample in spatial oracle: "+std::to_string(sample));
        }
    }
    void observe(const XBoardSoundCpu::Event& e,const XBoardSoundCpu::Registers& r,const CockpitRouting& routing){
        using K=XBoardSoundCpu::Kind;
        if(e.kind==K::MemoryRead&&e.pc==0xc76&&e.address!=e.pc){
            const auto it=descriptors.find(e.address);
            if(it==descriptors.end())throw std::runtime_error("unreviewed descriptor in spatial oracle: "+std::to_string(e.address));
            if(r.de<0xf820||r.de>0xfe82||(r.de-0xf820)%43)throw std::runtime_error("invalid recorded descriptor owner");
            seen_descriptors.insert(e.address);
            for(unsigned i=0;i<rom[e.address];++i){const auto owner=(r.de-0xf820)/43+i;if(owner>=owners.size())throw std::runtime_error("recorded descriptor exceeds owners");owners[owner]=it->second;}
        }
        if(e.kind==K::MemoryRead&&e.pc==0x17a2&&e.address==e.pc){direct=direct_policy(r.bc);seen_direct.insert(r.bc);}
        const auto group=r.ix==0xf808?direct:r.ix>=0xf820&&r.ix<=0xfe82&&(r.ix-0xf820)%43==0?owners[(r.ix-0xf820)/43]:CockpitRouting::neutral;
        if(e.kind==K::MemoryWrite&&e.address>=0xf000&&e.address<0xf100&&(e.address&0x87)==0x86&&!(e.data&1)){
            const unsigned channel=(e.address&0x7f)/8;
            if(routing.pcm[channel]!=group)throw std::runtime_error("spatial PCM policy mismatch: channel="+std::to_string(channel)+" IX="+std::to_string(r.ix)+" expected="+std::to_string(group)+" actual="+std::to_string(routing.pcm[channel]));
            ++checked_pcm;
        }
        if(e.kind==K::IoWrite&&e.address==0)ym_address=e.data;
        if(e.kind==K::IoWrite&&e.address==1){
            int channel=-1;if(ym_address>=0x20)channel=ym_address&7;if(ym_address==8&&(e.data&0x78))channel=e.data&7;
            if(channel>=0){if(routing.fm[channel]!=group)throw std::runtime_error("spatial FM policy mismatch");++checked_fm;}
        }
    }
    void directed(std::span<const uint8_t> main){
        // The natural 1,800-frame capture does not contain a passing sound.
        // Exercise both ROM descriptors without claiming natural MAME coverage.
        for(auto command:{0x6e,0x6f}){
            CockpitRouting routing(rom,main);XBoardSoundCpu::Registers r{};r.de=r.ix=0xfba7;
            routing.observe({XBoardSoundCpu::Kind::MemoryRead,0,0xc76,uint16_t(root(command)),rom[root(command)]},r);
            routing.observe({XBoardSoundCpu::Kind::MemoryWrite,0,0x17ff,0xf086,0},r);
            if(routing.pcm[0]!=CockpitRouting::rear)throw std::runtime_error("passing sound policy mismatch");
        }
    }
};
int main(int argc,char** argv)try{
    const std::string path=argc>1?argv[1]:"build/oracle/spatial-audio/routing.bin";
    if(!roms.load_aburner2())throw std::runtime_error("ROMs required");
    std::ifstream data(path,std::ios::binary);tag(data,"AB2SPA01");
    struct Row{unsigned k;uint64_t ns;uint16_t pc,a;uint8_t value;uint16_t ix,de,bc;};std::vector<Row> rows;uint64_t end=0;
    while(data.peek()!=EOF){const auto k=number(data,1);if(k==255){end=number(data,8);break;}rows.push_back({unsigned(k),number(data,8),uint16_t(number(data,2)),uint16_t(number(data,2)),uint8_t(number(data,1)),uint16_t(number(data,2)),uint16_t(number(data,2)),uint16_t(number(data,2))});}
    if(!end||data.peek()!=EOF)throw std::runtime_error("incomplete spatial oracle");
    HWPalette palette{8192};HWRoad road{0x1700,0x1720,0x1780,-166};HWSprites sprites;HWTiles tiles;
    road.init(roms.road.rom,roms.road.length);sprites.init(roms.sprites.rom,roms.sprites.length);tiles.init_gfx(roms.tiles.rom,roms.tiles.length);
    XBoardSoundBoard sound({roms.soundcpu.rom,roms.soundcpu.length},{roms.pcm.rom,roms.pcm.length});XBoardGame::Host host(roms,palette,road,sprites,tiles,sound);
    sound.enable_music_presentation({roms.soundcpu.rom,roms.soundcpu.length});sound.enable_cockpit({roms.soundcpu.rom,roms.soundcpu.length},{roms.maincpu.rom,roms.maincpu.length});
    RoutingAudit audit({roms.soundcpu.rom,roms.soundcpu.length});audit.directed({roms.maincpu.rom,roms.maincpu.length});
    std::ifstream inputs(path+".inputs",std::ios::binary);tag(inputs,"AB2MIP01");for(auto& v:host.switches)v=number(inputs,1);
    while(inputs.peek()!=EOF){XBoardGame::Host::Input v;v.ns=number(inputs,8);v.buttons=number(inputs,1);for(auto& a:v.analog)a=number(inputs,1);host.input(v);}
    CockpitRouting::Block latest;uint64_t pcm_samples=0;
    const auto keep_spatial=sound.audio.spatial;const auto keep_stream=sound.audio.stream;
    sound.audio.spatial=[&](const auto& b){latest=b;keep_spatial(b);};
    sound.audio.stream=[&](unsigned chip,uint64_t ns,uint64_t first,const auto& samples){
        if(chip==1){if(latest.chip!=chip||latest.first!=first||latest.music.size()!=samples.size())throw std::runtime_error("PCM tap frontier mismatch");
            for(size_t i=0;i<samples.size();++i){auto sum=latest.music[i];for(const auto& group:latest.groups){sum.left+=group[i].left;sum.right+=group[i].right;}if(sum.left!=samples[i].left||sum.right!=samples[i].right)throw std::runtime_error("PCM voice tap lost or duplicated samples");++pcm_samples;}}
        keep_stream(chip,ns,first,samples);
    };
    size_t count=0;uint64_t descriptor=0,direct=0,fm=0,pcm=0;std::set<uint16_t> owners;
    sound.observer=[&](const auto& e,uint64_t ns){if(ns>=end)return;const auto k=unsigned(e.kind);
        if(!((k==0&&(e.pc==0xc76||(e.pc==0x17a2&&e.address==e.pc)))||(k==1&&e.address>=0xf000&&e.address<0xf100)||(k==3&&e.address<=1)))return;
        if(count>=rows.size())throw std::runtime_error("extra spatial transaction");const auto& row=rows[count++];const auto r=sound.cpu.registers();
        if(k!=row.k||ns!=row.ns||e.pc!=row.pc||e.address!=row.a||e.data!=row.value||r.ix!=row.ix||r.de!=row.de||r.bc!=row.bc)throw std::runtime_error("spatial MAME transaction/IX/DE/BC mismatch at row "+std::to_string(count));
        audit.observe(e,r,*sound.cockpit);
        if(k==0&&e.pc==0xc76&&e.address!=e.pc)++descriptor;
        if(k==0&&e.pc==0x17a2)++direct;
        if(k==1){++pcm;owners.insert(r.ix);}if(k==3)++fm;
    };
    while(host.time()<end){host.step();host.deliver();sound.spatial_blocks={};}
    if(count!=rows.size()||!descriptor||!direct||owners.size()<8)throw std::runtime_error("spatial oracle coverage incomplete");
    if(audit.seen_descriptors.size()!=9||audit.seen_direct.size()!=7||!audit.checked_pcm||!audit.checked_fm)throw std::runtime_error("reviewed spatial catalog coverage incomplete");
    std::cout<<"Spatial ownership MAME: "<<count<<" of "<<rows.size()<<" identical transactions/IX/DE/BC, 0 skipped; descriptors="<<descriptor<<" direct samples="<<direct<<" PCM writes="<<pcm<<" FM writes="<<fm<<" observed owners="<<owners.size()<<" exact PCM sums="<<pcm_samples<<'\n';
    std::cout<<"Reviewed spatial policy: "<<audit.seen_descriptors.size()<<" descriptor roots, "<<audit.seen_direct.size()<<" direct sample IDs; "<<audit.checked_pcm<<" PCM enables and "<<audit.checked_fm<<" FM writes; both passing descriptors checked separately (no natural passing event in this capture)\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
