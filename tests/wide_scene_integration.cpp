#include "wide_ground_reference.hpp"
#include "wide_native_reference.hpp"
#include "engine/game_session.hpp"
#include "engine/omainsprites.hpp"
#include "engine/osubsprites.hpp"
#include "hwvideo/wide_scene.hpp"
#include "frontend/sprite_filter.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
void require(bool c,const char* m){if(!c)throw std::runtime_error(m);}
// Explicit presentation fixtures: isolated host memory, no CPU progression.
// They prove retention across original culling and native object-slot reuse.
static void visibility_fixtures(){
    XBoardGame fixture(roms);auto& h=fixture.board();auto& b=h.bus;Presentation::WideVisibility visibility(roms);
    const uint32_t object=0x3fc400;OMainSprites sprites;sprites.initialize(b);
    b.write16(0x29c09c,16);b.write16(0x29c060,0);b.write16(0x29c08e,200);b.write16(0x29c092,127);
    auto event=[&](unsigned cpu,uint32_t pc,OMainObjects::Registers r={},uint16_t sr=0){
        XBoardGame::Host::Entry entry;entry.state.registers=r;entry.state.pc=pc;entry.state.sr=sr;
        XBoardGame::Host::Phase phase;phase.cpu=cpu;phase.pc=pc;phase.entry=&entry;
        const auto before=h.memory.bytes;const auto time=h.time();visibility.observe(phase,h);
        require(h.memory.bytes==before&&h.time()==time,"wide observer changed native memory/time");
    };
    auto latch=[&]{b.write32(0x2a3c08,b.read32(0x2a3c04));b.write32(0x2a3c14,b.read32(0x2a3c18));
        event(1,0x648);event(0,0xb7c2);h.memory.sprite_buffer.fill(0);h.memory.sprite_buffer[0]=0x80;
        XBoardGame::Host::Phase phase;phase.write=true;phase.address=0x110000;const auto before=h.memory.bytes;visibility.observe(phase,h);
        require(h.memory.bytes==before,"wide observer changed native memory/time");
    };
    event(0,0xb684); // establish the flight/stage generation before a cull
    b.write16(object,127);b.write8(object+3,0xe8);b.write16(object+4,200);b.write16(object+36,0x1000);
    b.write32(object+54,0x29c190);b.write32(object+60,0x29c180);b.write32(0x29c180,b.read32(0x11000));b.write16(0x2a3800,32);
    OMainObjects::Registers r;r.a[5]=object;r.a[6]=0x2e4000;r.d[7]=127;
    event(0,0x8bb0,r);event(0,0xb684);latch();
    require(std::any_of(visibility.descriptors().begin(),visibility.descriptors().end(),[](auto& x){return x.kind==2;}),"scenery deleted at native edge");
    for(unsigned n=0;n<64;++n)b.write8(object+n,0xff); // native allocator reuses the record
    event(0,0xb684);latch();
    require(std::any_of(visibility.descriptors().begin(),visibility.descriptors().end(),[](auto& x){return x.kind==2;}),"retained scenery lost after native slot reuse");
    b.write16(0x29c08e,500);event(0,0xb684);latch();
    require(visibility.descriptors().empty(),"scenery persisted beyond wide edge");
    // Retained ground/waves use the live shared rotation pair in every scene
    // that runs this native scenery controller, including attract and takeoff.
    for(unsigned mode:{1u,15u,16u}){
        visibility.reset();sprites.initialize(b);b.write16(0x29c09c,mode);b.write16(0x29c08e,200);
        for(unsigned n=0;n<64;++n)b.write8(object+n,0);
        b.write16(object,127);b.write8(object+3,0xe8);b.write16(object+4,200);b.write16(object+36,0x1000);
        b.write32(object+54,0x29c190);b.write32(object+60,0x29c180);b.write32(0x29c180,b.read32(0x11000));b.write16(0x2a3800,32);
        b.write32(0x29c190,0);b.write32(0x29c194,0);event(0,0xb684);
        event(0,0x8bb0,r);event(0,0xb684);latch();
        require(!visibility.descriptors().empty(),"non-flight scenery retention missing");
        event(0,0xb684);latch(); // settle the detached zoom before comparing camera changes
        const auto before=visibility.descriptors().front().bytes;
        b.write32(0x29c194,0x2000);event(0,0xb684);latch();
        require(!visibility.descriptors().empty(),"live rotated scenery disappeared");
        const auto after=visibility.descriptors().front().bytes;
        require((unsigned(after[0]&1)<<8|after[1])<(unsigned(before[0]&1)<<8|before[1]),"retained scenery froze the shared rotation pair");
        b.write16(0x29c060,1);event(0,0xb684);latch();
        require(visibility.descriptors().empty(),"retained scenery crossed a stage change");b.write16(0x29c060,0);
    }
    visibility.reset();sprites.initialize(b);b.write16(0x29c09c,16);b.write32(0x29c194,0);
    // The ROM's finite water field needs adjacent world columns during takeoff.
    for(unsigned mode:{1u,15u,16u}){
        visibility.reset();sprites.initialize(b);b.write16(0x29c09c,mode);b.write16(0x29c08e,0);
        for(unsigned i=0;i<2;++i){const auto p=object+i*64;for(unsigned n=0;n<64;++n)b.write8(p+n,0);
            b.write16(p,0xa01d);b.write16(p+2,0x2023);b.write16(p+6,140);b.write16(p+36,0x2000);
            b.write16(p+42,i?1400:-1400);b.write32(p+54,0x29c190);b.write32(p+12,b.read32(0x3f91c));
            auto water=r;water.a[5]=p;event(0,0xb628,water);}
        event(0,0xb684);latch();unsigned left_water=0,right_water=0;
        for(const auto& d:visibility.descriptors())if(d.kind==6){left_water+=d.x<0;right_water+=d.x>=320;}
        require(mode==15?(left_water&&right_water):(!left_water&&!right_water),"takeoff water field extension missing or leaked into another mode");
        if(mode==15)for(unsigned i=0;i<2;++i){
            std::array<uint8_t,16> q;for(unsigned n=0;n<16;++n)q[n]=b.read8(object+i*64+n);
            const auto put=[&](unsigned n,uint16_t v){q[n]=v>>8;q[n+1]=v;};
            // Depth $2000 gives $7FE0/($100+32)=113. With cosine 127,
            // adjacent world X +/-1672 projects to +182/-184 before origin.
            q[0]|=12;q[12]=0;put(4,i?-184:182);put(6,112);put(8,i?-184:182);put(10,112);
            Presentation::WideSpriteProjection projector;auto expected=projector.project(q,std::span(h.memory.bytes).subspan(MainBus::SHARED,0x8000),{roms.subcpu.rom,roms.subcpu.length},39);
            require(std::any_of(visibility.descriptors().begin(),visibility.descriptors().end(),[&](const auto& d){return d.kind==6&&d.x==expected.x&&d.bytes==expected.descriptor;}),"takeoff water projection changed native depth or coordinates");
        }
    }
    visibility.reset();sprites.initialize(b);b.write16(0x29c09c,16);b.write16(0x29c08e,200);
    // Extend the ROM's 24-pixel native horizon spacing, not viewport pixels.
    const uint32_t frame=b.read32(0x11000);b.write16(object,127);b.write8(object+3,0xe8);b.write32(object+12,frame);b.write8(object+20,0);
    r.d[0]=192u<<16;r.d[1]=24u<<16;r.d[4]=112u<<16;r.d[5]=0;
    event(0,0x10fba,r);event(0,0xb628,r);event(0,0xb684);latch();unsigned left=0,right=0;
    for(auto& d:visibility.descriptors())if(d.kind==3){left+=d.x<0;right+=d.x>=320;}
    require(left&&right,"horizon extension missing at wide edges");
    event(0,0x10fba,r);event(0,0xb684);latch();require(std::none_of(visibility.descriptors().begin(),visibility.descriptors().end(),[](const auto& d){return d.kind==3;}),"unpublished horizon extended");
    // The same native controller publishes strips during attract and takeoff.
    // Mode changes must not erase a freshly generated, actually submitted strip.
    for(unsigned mode:{1u,15u,16u}){
        r.d[0]+=8u<<16;b.write16(0x29c09c,mode);event(0,0x10fba,r);event(0,0xb628,r);event(0,0xb684);latch();
        require(std::any_of(visibility.descriptors().begin(),visibility.descriptors().end(),[](const auto& d){return d.kind==3;}),"non-flight published horizon missing");
        const auto first_x=std::find_if(visibility.descriptors().begin(),visibility.descriptors().end(),[](const auto& d){return d.kind==3;})->x;
        event(0,0xb7d2);{XBoardGame::Host::Phase phase;phase.write=true;phase.address=0x110000;visibility.observe(phase,h);}
        require(std::find_if(visibility.descriptors().begin(),visibility.descriptors().end(),[](const auto& d){return d.kind==3;})->x==first_x,"horizon interpolation crossed a scene change");
        r.d[0]+=8u<<16;event(0,0x10fba,r);event(0,0xb628,r);event(0,0xb684);latch();
        const auto full_x=std::find_if(visibility.descriptors().begin(),visibility.descriptors().end(),[](const auto& d){return d.kind==3;})->x;
        event(0,0xb7d2);{XBoardGame::Host::Phase phase;phase.write=true;phase.address=0x110000;visibility.observe(phase,h);}
        const auto half=std::find_if(visibility.descriptors().begin(),visibility.descriptors().end(),[](const auto& d){return d.kind==3;});
        require(half!=visibility.descriptors().end()&&half->x==full_x-4,"non-flight horizon interpolation differs");
        event(0,0xb628,r);event(0,0xb684);latch();
        require(std::none_of(visibility.descriptors().begin(),visibility.descriptors().end(),[](const auto& d){return d.kind==3;}),"old horizon reused without a controller update");
    }
    visibility.reset();require(visibility.descriptors().empty(),"wide reset retained detached objects");
    // Exercise the original $C0 rejection without changing native append.
    // More than one hardware bank must survive in detached presentation.
    visibility.reset();OMainSprites native_sprites;native_sprites.initialize(b);
    b.write16(0x2a3cf0,192);b.write16(object,127);b.write16(object+2,1000);
    b.write16(object+4,0);b.write16(object+6,0);b.write32(object+12,frame);b.write8(object+20,0);
    r={};r.a[5]=object;
    for(unsigned i=0;i<512;++i){event(0,0xb628,r);native_sprites.append(b,object);}
    require(b.read16(0x2a3cf0)==192,"wide capture changed native request capacity");
    event(0,0xb684);latch();
    require(visibility.descriptors().size()==512,"capacity overflow requests missing");
    require(std::all_of(visibility.descriptors().begin(),visibility.descriptors().end(),[](const auto& d){return d.kind==4;}),"capacity overflow misclassified");
    const auto overflow_descriptor=visibility.descriptors().front();
    // Generations and reset must not replay a previous frame's rejected list.
    event(0,0xb684);latch();require(visibility.descriptors().empty(),"capacity overflow leaked across publication");
    visibility.reset();b.write16(0x2a3cf0,0);
    event(0,0xb628,r);native_sprites.append(b,object);event(0,0xb684);latch();
    require(visibility.descriptors().size()==1&&visibility.descriptors()[0].kind==5,"missing native bucket request not recovered");
    visibility.reset();latch();require(visibility.descriptors().empty(),"capacity overflow survived reset");
    // Run the real retained Sub sorter: depth 255 has room for only 30.
    // Its stale order entries must not become duplicate enhanced sprites.
    native_sprites.initialize(b);b.write16(0x2a3cf0,0);b.write16(object+2,0xff00);
    const auto requests=b.read32(0x2a3c04);b.write32(0x2a3c08,requests);
    for(unsigned i=0;i<64;++i){event(0,0xb628,r);native_sprites.append(b,object);}
    event(0,0xb684);b.write16(b.read32(0x2a3c10),64);event(1,0x648);
    SubBus sub(roms.subcpu.rom,roms.subcpu.length,h.memory.bytes.data()+MainBus::SHARED);OSubSprites sub_sprites;sub_sprites.pass(sub);
    const auto sorted=b.read32(0x2a3c18);
    for(unsigned i=0;i<64;++i){OMainObjects::Registers regs;regs.a[4]=sorted+i*16;regs.a[5]=(requests&0xfffff)+sub.read8(SubBus::A6+0x7d00+i)*16;event(1,0x7b8,regs);}
    b.write32(0x2a3c14,sorted);event(0,0xb7c2);
    std::copy_n(h.memory.bytes.begin()+MainBus::memory_offset(sorted),65*16,h.memory.sprite_buffer.begin());
    {XBoardGame::Host::Phase phase;phase.cpu=0;phase.write=true;phase.address=0x110000;visibility.observe(phase,h);}
    require(visibility.descriptors().size()==64,"depth bucket overflow lost requests or duplicated stale native order");
    require(std::count_if(visibility.descriptors().begin(),visibility.descriptors().end(),[](const auto& d){return d.kind==5;})==34,"depth bucket overflow recovery census differs");
    visibility.reset();
    // Enabling halfway through a Sub pass cannot treat uncaptured leading
    // slots as request zero or replace them with default descriptors.
    {OMainObjects::Registers regs;regs.a[4]=sorted+63*16;regs.a[5]=(requests&0xfffff)+sub.read8(SubBus::A6+0x7d00+63)*16;event(1,0x7b8,regs);}
    event(0,0xb7c2);{XBoardGame::Host::Phase phase;phase.cpu=0;phase.write=true;phase.address=0x110000;visibility.observe(phase,h);}
    require(visibility.descriptors().size()==64,"mid-pass enable discarded uncaptured native slots");
    require(std::equal(visibility.descriptors()[0].bytes.begin(),visibility.descriptors()[0].bytes.end(),h.memory.sprite_buffer.begin()),"mid-pass enable replaced uncaptured native descriptor");
    b.write8(0x29c03a,1);event(1,0x648);event(0,0xb7d2);
    {XBoardGame::Host::Phase phase;phase.cpu=0;phase.write=true;phase.address=0x110000;visibility.observe(phase,h);}
    require(std::equal(visibility.descriptors()[0].bytes.begin(),visibility.descriptors()[0].bytes.end(),h.memory.sprite_buffer.begin()),"mid-pass interpolation replaced uncaptured native descriptor");
    b.write8(0x29c03a,0);
    visibility.reset();
    Presentation::SceneClip large;large.display=true;large.bottom=223;large.ns=1;
    large.sprites[0]=0x80;large.extended_sprites.resize(5001);
    for(auto& d:large.extended_sprites)d.bytes[0]=0x40;
    large.extended_sprites.back()=overflow_descriptor;
    auto large_owner=std::make_unique<Presentation::WideScene>(roms);auto& large_scene=*large_owner;large_scene.render(std::span(&large,1),1,false);
    require(large_scene.view().ready&&!large_scene.view().fragments.empty(),"dynamic descriptor tail was truncated");
    std::cout<<"Wide visibility fixtures: native cull/slot reuse, wide-edge retirement, horizon endpoints, immutable memory/time; 512 rejected requests, 34 real bucket drops without duplicates, generations/reset and 5001-descriptor tail passed\n";
}
static void attract_edge_scene(){
    auto reference_owner=std::make_unique<GameSession>(roms),captured_owner=std::make_unique<GameSession>(roms);
    auto& reference=*reference_owner;auto& captured=*captured_owner;reference.set_dip_switches({255,255,221});captured.set_dip_switches({255,255,221});captured.capture_wide_scene(true);
    auto plain_owner=std::make_unique<Presentation::WideScene>(roms),fixed_owner=std::make_unique<Presentation::WideScene>(roms);
    auto& plain=*plain_owner;auto& fixed=*fixed_owner;WideNativeReference legacy(roms);
    for(unsigned f=0;f<840;++f){reference.advance({});captured.advance({});auto a=reference.frame(),b=captured.frame();
        require(a.info.time_ns==b.info.time_ns&&std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),[](auto x,auto y){return x.r==y.r&&x.g==y.g&&x.b==y.b;}),"attract edge observer changed native RGB/time");
        require(reference.take_audio()==captured.take_audio(),"attract edge observer changed PCM");auto scene=captured.wide_scene();legacy.check(scene.clips,scene.info.number,a.pixels);
        plain.render(scene.clips,scene.info.number,false);fixed.render(scene.clips,scene.info.number,true);auto p=plain.view(),q=fixed.view();
        for(unsigned i=0;i<p.ground_indices.size();++i)if(p.ground_indices[i]!=q.ground_indices[i])require(valid_wide_ground_change(p,q,i),"attract edge correction overwrote terrain");
        if(f==140){const unsigned i=180*398+390;
            require(p.ground_indices[i]>=0x1720&&p.ground_indices[i]<0x1730,"banked attract corner reproduction lost its gap");
            require(q.ground_indices[i]>=0x1700&&q.ground_indices[i]<0x1708,"banked attract corner retained its color block");
            save_wide_ground_fixture(q,"build/oracle/widescreen/edge-banked-cloud.bin");}
        if(f==780){const auto i=180*398+20;require(p.ground_indices[i]>=0x1720&&p.ground_indices[i]<0x1730,"attract edge reproduction lost original sky gap");
            if(!(q.ground_indices[i]>=0x1700&&q.ground_indices[i]<0x1708))std::cerr<<"attract edge before="<<std::hex<<p.ground_indices[i]<<" after="<<q.ground_indices[i]<<std::dec<<'\n';
            require(q.ground_indices[i]>=0x1700&&q.ground_indices[i]<0x1708,"attract cloud side-band background still clipped");
            for(unsigned x:{39u,40u})require(q.ground_indices[180*398+x]>=0x1700&&q.ground_indices[180*398+x]<0x1708,"attract cloud native-edge seam remains");
            save_wide_ground_fixture(q,"build/oracle/widescreen/edge-cloud.bin");}
    }
    std::cout<<"Attract edge: 840 native RGB/PCM/time frames and legacy images exact; translucent-cloud background witness passed\n";
}
int main(int argc,char** argv)try{
    require(roms.load_aburner2(),"ROMs required");visibility_fixtures();if(argc==2&&std::string_view(argv[1])=="--visibility-fixtures")return 0;
    auto original_owner=std::make_unique<GameSession>(roms),captured_owner=std::make_unique<GameSession>(roms);
    auto& original=*original_owner;auto& captured=*captured_owner;
    std::vector<std::unique_ptr<Presentation::WideScene>> scenes;
    auto make_scene=[&](unsigned width=398)->Presentation::WideScene&{scenes.push_back(std::make_unique<Presentation::WideScene>(roms,width));return *scenes.back();};
    attract_edge_scene();captured.capture_wide_scene(true);auto& wide=make_scene();auto& native=make_scene(320);auto& enhanced=make_scene();auto& corrected=make_scene();Frontend::SpriteScaler scaler,native_scaler;Presentation::GroundHorizon horizon({roms.maincpu.rom,roms.maincpu.length},{roms.subcpu.rom,roms.subcpu.length});
    // Independent side-column reference: decode the original planar tile ROM
    // directly and select deliberately distinct virtual tile/column entries.
    auto synthetic_owner=std::make_unique<Presentation::SceneClip>();auto& synthetic=*synthetic_owner;synthetic.display=true;synthetic.bottom=223;synthetic.ns=1;
    synthetic.sprites[0]=0x80;synthetic.registers[4]=synthetic.registers[5]=0x8000;
    for(unsigned i=0;i<8192;++i)synthetic.palette[i]=uint16_t((i*13)&0x7fff);
    for(unsigned i=0;i<synthetic.tiles.size();++i)synthetic.tiles[i]=uint16_t((i*7+1)&0x1fff);
    for(unsigned i=0;i<0x200;++i){synthetic.road[2*i]=8;synthetic.road[2*i+1]=5;}
    for(unsigned layer=0;layer<2;++layer)for(unsigned col=0;col<32;++col)synthetic.text[0xf16/2+layer*32+col]=col*8;
    const auto before_bank=synthetic.sprites;auto& column_scene=make_scene();column_scene.render(std::span(&synthetic,1),1,false);
    const auto colview=column_scene.view();HWPalette column_palette(8192);for(unsigned i=0;i<8192;++i)column_palette.write(i,synthetic.palette[i]);
    auto colpixels=scaler.compose(colview.background,colview.sprites,398,224,Frontend::SpriteFilter::none,colview.protection);
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<398;++x)if(x<39||x>=359){int nx=int(x)-39;unsigned vx=(nx+192)&1023,col=((nx+8)&511)/16,vy=(y+col*8)&511;
        auto entry=synthetic.tiles[((vy&255)>>3)*64+((vx&511)>>3)];const unsigned code=(entry&8191)%(roms.tiles.length/24),off=code*8+(vy&7),third=roms.tiles.length/3;
        unsigned pen=0;for(unsigned plane=0;plane<3;++plane)pen|=((roms.tiles.rom[plane*third+off]>>(7-(vx&7)))&1)<<plane;
        unsigned index=pen?0x1c00+((entry>>6)&127)*8+pen:0x1785;auto p=column_palette.pen(index);
        require(colpixels[y*398+x]==(0xff000000u|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b),"wide side column/source wrap differs from independent planar-ROM reference");}
    require(synthetic.sprites==before_bank,"wide renderer wrote native sprite feedback");
    auto upper=synthetic,lower=synthetic;upper.bottom=111;lower.top=112;lower.ns=2;
    upper.palette.fill(0x001f);lower.palette.fill(0x7c00);
    auto& partial_scene=make_scene();std::vector<Presentation::SceneClip> parts;parts.reserve(2);parts.push_back(upper);parts.push_back(lower);partial_scene.render(parts,1,false);
    auto partial=partial_scene.view();HWPalette partial_palette(8192);partial_palette.write(0,0x001f);auto red=partial_palette.pen(0);partial_palette.write(0,0x7c00);auto blue=partial_palette.pen(0);
    require(partial.background[100*398]==(0xff000000u|uint32_t(red.r)<<16|uint32_t(red.g)<<8|red.b) &&
        partial.background[120*398]==(0xff000000u|uint32_t(blue.r)<<16|uint32_t(blue.g)<<8|blue.b),"wide renderer lost partial-frontier palette/row ownership");
    std::map<unsigned,uint64_t> corrections_by_mode;
    uint64_t side_corrections[2]{};uint64_t recovered=0,retained_scenery=0,extra_horizon=0,extended_side=0;uint64_t clips=0,side_changes=0,side_sprites=0,ground_changes=0;unsigned left=0,right=0,columns=0,inverted[2]{},inverted_turns[2]{};uint8_t previous_angle=0;
    for(unsigned f=0;f<1560;++f){GameControls c;c.coin=f>=360&&f<372;c.start=f>=420&&f<432;
        if(f>=600){c.stick_x=((f-600)/60)%2?1:-1;c.throttle=1;c.cannon=true;c.missile=f%90<12;}
        original.advance(c);captured.advance(c);
        auto a=original.frame(),b=captured.frame();require(a.info.number==b.info.number&&a.info.time_ns==b.info.time_ns,"wide tap changed time");
        require(std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),[](auto p,auto q){return p.r==q.r&&p.g==q.g&&p.b==q.b;}),"wide tap changed native RGB");require(original.take_audio()==captured.take_audio(),"wide tap changed PCM");
        auto scene=captured.wide_scene();require(scene.ready,"wide scene frontier missing");clips+=scene.clips.size();
        for(auto& clip:scene.clips)for(auto& d:clip.extended_sprites){recovered+=d.recovered;retained_scenery+=d.kind==2;extra_horizon+=d.kind==3;}
        for(auto& clip:scene.clips)columns+=(clip.registers[4]&0x8000)||(clip.registers[5]&0x8000);
        wide.render(scene.clips,scene.info.number,false,false);enhanced.render(scene.clips,scene.info.number,false);native.render(scene.clips,scene.info.number,false);corrected.render(scene.clips,scene.info.number,true);auto w=wide.view(),n=native.view(),q=corrected.view(),e=enhanced.view();
        require(std::equal(e.sprites.begin(),e.sprites.end(),q.sprites.begin())&&std::equal(e.protection.begin(),e.protection.end(),q.protection.begin()),"wide ground correction changed aircraft/layer protection");
        const int roll_normal=horizon.normal(uint8_t(scene.clips.back().roll))[0];
        const auto angle=uint8_t(scene.clips.back().roll);const int turn=int8_t(uint8_t(angle-previous_angle));previous_angle=angle;
        if(scene.clips.back().mode==16&&horizon.normal(angle)[1]<-100&&turn)++inverted_turns[turn>0];
        if(scene.clips.back().mode==16&&horizon.normal(uint8_t(scene.clips.back().roll))[1]<0){
            if(!inverted[c.stick_x>0]){
                if(c.stick_x<0)require(q.ground_indices[230]>=0x1720&&q.ground_indices[230]<0x1730,"inverted wide center retained road fill in the sky");
                save_wide_ground_fixture(q,"build/oracle/widescreen/edge-inverted-"+std::to_string(c.stick_x>0)+".bin");}
            ++inverted[c.stick_x>0];}

        for(unsigned i=0;i<w.ground_indices.size();++i)if(e.ground_indices[i]!=q.ground_indices[i]){++ground_changes;++corrections_by_mode[scene.clips.back().mode];if(roll_normal&&(i%398<39||i%398>=359))++side_corrections[roll_normal>0];require(valid_wide_ground_change(e,q,i),"wide ground correction overwrote terrain/tile indices");}require(w.ready&&n.ready,"wide scene row coverage missing");
        auto output=scaler.compose(w.background,w.sprites,w.width,224,Frontend::SpriteFilter::none,w.protection);
        auto normal=native_scaler.compose(n.background,n.sprites,320,224,Frontend::SpriteFilter::none,n.protection);
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<320;++x){unsigned i=y*320+x;auto p=a.pixels[i];auto expected=0xff000000u|uint32_t(p.r)<<16|uint32_t(p.g)<<8|p.b;
            if(normal[i]!=expected || output[y*w.width+x+w.origin]!=expected){std::cerr<<" normal-bg="<<std::hex<<n.background[i]<<" wide-bg="<<w.background[y*w.width+x+w.origin]<<" normal-sprite="<<n.sprites[i]<<" wide-sprite="<<w.sprites[y*w.width+x+w.origin]<<std::dec<<" ";std::cerr<<"frame="<<f<<" x="<<x<<" y="<<y<<" native="<<std::hex<<expected<<" reconstructed="<<normal[i]<<" wide="<<output[y*w.width+x+w.origin]<<std::dec<<'\n';throw std::runtime_error("wide center differs from native raster");}}
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<w.origin;++x){side_sprites+=w.sprites[y*w.width+x]!=0;side_sprites+=w.sprites[y*w.width+w.width-1-x]!=0;side_changes+=output[y*w.width+x]!=output[y*w.width+w.origin];side_changes+=output[y*w.width+w.width-1-x]!=output[y*w.width+w.width-1-w.origin];}
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<e.origin;++x){extended_side+=e.sprites[y*e.width+x]!=w.sprites[y*w.width+x];extended_side+=e.sprites[y*e.width+e.width-1-x]!=w.sprites[y*w.width+w.width-1-x];}
        if(f>=1080){if(c.stick_x<0)++left;if(c.stick_x>0)++right;}
        if(f==1275){auto w=e;std::filesystem::create_directories("build/oracle/widescreen");std::ofstream file("build/oracle/widescreen/scene.bin",std::ios::binary);file.write("AB2WIDE1",8);uint32_t dims[]{w.width,224,uint32_t(w.fragments.size())};file.write(reinterpret_cast<const char*>(dims),sizeof(dims));
            for(auto data:{w.background,w.sprites,w.heads})file.write(reinterpret_cast<const char*>(data.data()),data.size_bytes());
            file.write(reinterpret_cast<const char*>(w.protection.data()),w.protection.size_bytes());file.write(reinterpret_cast<const char*>(w.fragments.data()),w.fragments.size_bytes());}
        if(f==1275||f==1287){Frontend::SpriteScaler corrected_scaler;auto corrected_output=corrected_scaler.compose(q.background,q.sprites,q.width,224,Frontend::SpriteFilter::none,q.protection);std::ofstream img("build/oracle/widescreen/corrected-"+std::to_string(f)+".ppm",std::ios::binary);img<<"P6\n"<<q.width<<" 224\n255\n";for(auto p:corrected_output)for(unsigned shift:{16u,8u,0u})img.put(char(p>>shift));}
        if(f==1275||f==1287){std::filesystem::create_directories("build/oracle/widescreen");std::ofstream img("build/oracle/widescreen/scene-"+std::to_string(f)+".ppm",std::ios::binary);img<<"P6\n"<<w.width<<" 224\n255\n";for(auto p:output)for(unsigned shift:{16u,8u,0u})img.put(char(p>>shift));}
    }
    // $C0A's target-change threshold requires a direct full-stick reversal.
    // Hold each side for 120 frames so the +/-255 offset has time to decay
    // by six per Sub update before commanding the opposite barrel roll.
    original.debug_skip_to_stage(1);captured.debug_skip_to_stage(1);
    WideNativeReference inversion_legacy(roms);auto& inversion_plain=make_scene();unsigned inversion_frames=0;bool inversion_saved[2]{};
    for(unsigned f=0;f<720;++f){GameControls c;c.throttle=1;c.cannon=true;c.missile=f%90<12;
        if(f>=120)c.stick_x=((f-120)/120)%2?1:-1;
        original.advance(c);captured.advance(c);auto a=original.frame(),b=captured.frame();
        require(a.info.time_ns==b.info.time_ns&&std::equal(a.pixels.begin(),a.pixels.end(),b.pixels.begin(),[](auto p,auto q){return p.r==q.r&&p.g==q.g&&p.b==q.b;}),"inverted roll observer changed native RGB/time");
        require(original.take_audio()==captured.take_audio(),"inverted roll observer changed native PCM");
        auto scene=captured.wide_scene();inversion_legacy.check(scene.clips,scene.info.number,a.pixels);
        corrected.render(scene.clips,scene.info.number,true);inversion_plain.render(scene.clips,scene.info.number,false);auto p=inversion_plain.view(),q=corrected.view();
        for(unsigned i=0;i<p.ground_indices.size();++i)if(p.ground_indices[i]!=q.ground_indices[i])require(valid_wide_ground_change(p,q,i),"inverted roll correction overwrote terrain");
        const auto angle=uint8_t(scene.clips.back().roll);const int turn=int8_t(uint8_t(angle-previous_angle));previous_angle=angle;
        if(f>=120&&scene.clips.back().mode==16&&horizon.normal(angle)[1]<-100&&turn){
            if(!inversion_saved[turn>0]){save_wide_ground_fixture(q,"build/oracle/widescreen/edge-full-inversion-"+std::to_string(turn>0)+".bin");inversion_saved[turn>0]=true;}
            ++inverted_turns[turn>0];}++inversion_frames;
    }
    std::cout<<"Directed inversion tail: "<<inversion_frames<<" native RGB/PCM/time and legacy frames exact; near-full turns="<<inverted_turns[0]<<'/'<<inverted_turns[1]<<'\n';
    require(inverted_turns[0]&&inverted_turns[1],"full inversion missing in one actual roll direction");
    require(side_corrections[0]&&side_corrections[1],"outer sky-fill coverage missing in one roll direction");
    require(corrections_by_mode[1]&&corrections_by_mode[15],"attract/takeoff sky-fill not exercised");
    require(recovered&&extended_side&&extra_horizon,"wide visibility extension missing");
    require(side_changes&&side_sprites&&ground_changes&&left&&right,"wide side/roll witnesses missing");
    captured.set_paused(true);const auto before=captured.info();require(!captured.advance({})&&captured.info().number==before.number,"paused wide capture advanced");
    captured.capture_sprite_scene(true);captured.capture_ground_horizon(true);captured.set_paused(false);for(unsigned i=0;i<8;++i){original.advance({});captured.advance({});require(original.take_audio()==captured.take_audio(),"combined observers changed PCM");require(captured.wide_scene().ready&&captured.sprite_scene().ready,"wide capture replaced effect/ground observers");}
    captured.restart();wide.reset();for(unsigned i=0;i<8;++i){captured.advance({});captured.take_audio();wide.render(captured.wide_scene().clips,captured.info().number,false);}require(wide.view().ready,"wide restart missing rows");
    captured.capture_sprite_presentation(false);captured.advance({});captured.take_audio();require(captured.wide_scene().ready,"disabling sprite capture removed wide observer");captured.capture_wide_scene(false);require(!captured.wide_scene().ready,"wide disable retained snapshots");
    std::cout<<"Near-full inversion by actual roll direction: "<<inverted_turns[0]<<'/'<<inverted_turns[1]<<'\n';
    std::cout<<"Inverted roll frames by input direction: "<<inverted[0]<<'/'<<inverted[1]<<'\n';
    std::cout<<"Wide visibility: "<<recovered<<" recovered/projected descriptors, "<<extended_side<<" side pixel differences; left/right outer sky-fill="<<side_corrections[0]<<'/'<<side_corrections[1]<<"; retained scenery/horizon="<<retained_scenery<<'/'<<extra_horizon<<"\n";
    std::cout<<"Wide scene: 1560 native RGB/PCM/time frames and 320-center reconstructions identical; "<<clips<<" partial clips, "<<side_changes<<" non-repeated side pixels, "<<side_sprites<<" side sprite pixels; "<<ground_changes<<" sky-fill corrections with terrain preserved; independent column wrap passed; both rolls="<<left<<'/'<<right<<", column-scroll clips="<<columns<<"; shared observers, pause/reset/disable passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
