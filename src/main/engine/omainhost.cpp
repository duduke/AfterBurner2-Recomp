#include "engine/omainhost.hpp"
#include "engine/omaininput.hpp"
#include "engine/omainplayer.hpp"
#include "engine/omaindamage.hpp"
#include "engine/omainsequence.hpp"
#include "engine/omainweapons.hpp"
#include "engine/omainworld.hpp"
#include "engine/omainsprites.hpp"
#include <algorithm>
#include <initializer_list>
#include <stdexcept>

namespace {
bool contains(uint32_t entry,std::initializer_list<uint32_t> entries) {
    return std::find(entries.begin(),entries.end(),entry)!=entries.end();
}
}

bool OMainHost::execute(MainBus& bus, Registers& r, uint32_t entry, const Call& boundary) {
    // Route only the independently checked routine entry contracts. Internal
    // branch labels are owned by each port; unknown game-flow entries cannot
    // be silently treated as a handler or replaced with native behaviour.
    if(entry==0x1488e) OMainObjects::initialize(bus,r);
    else if(entry==0x16892) OMainObjects::player_initialize(bus,r);
    else if(entry==0x16976) OMainObjects::player_prepare(bus,r);
    else if(entry==0x169b2) OMainObjects::player_hide(bus,r);
    else if(entry==0xc5a8) OMainObjects::random(bus,r);
    else if(entry==0xb628) OMainPlayer::append(bus,r);
    else if(entry==0xb676) {
        // $00B676 BGT yields at the actual wait; never replay publication.
        // A supplied boundary may service the pending interrupt, then retry.
        if(!OMainSprites::publish(bus,r)) {
            if(!boundary) throw std::runtime_error("main sprite publication waiting for IRQ");
            boundary(entry,r);
            if(!OMainSprites::publish(bus,r)) throw std::runtime_error("main sprite publication still waiting for IRQ");
        }
    }
    else if(entry==0xb0ac) OMainInput::update(bus,r);
    else if(entry==0x14976) OMainInput::buttons(bus,r);
    else if(contains(entry,{
        0xe662, 0xe8d0, 0xe94e, 0xf3e2, 0xf43c, 0xf4ba,
        0xf528, 0xf5c0, 0xe744, 0xe7e0, 0xe7da, 0xebd2,
        0xebf6, 0xec1a, 0xec3e, 0xec62, 0xecac, 0xf58e,
        0xf640, 0xf766, 0xf85a, 0xf878, 0xf93e, 0xf95e,
        0xf966, 0xf99e, 0xf9ce, 0xf9e2, 0xfaa0, 0xfb08,
        0xfb16, 0xfb9c, 0xfbc4, 0xfbd4, 0xfbda, 0xfcb4,
        0xfda6, 0xfdfa, 0xff08, 0xffbe, 0xffe6, 0x1003e,
        0x100dc, 0x100f4, 0x10148, 0x101e6, 0x101fe, 0x10252,
        0x102f2, 0x10310
    })) OMainDamage::run(bus,r,entry);
    else if(contains(entry,{
        0x10348, 0x103c6, 0x103ea, 0x1069c, 0x106ba, 0x106d8,
        0x106f6, 0x1071c, 0x10782, 0x107b6, 0x10822, 0x10848,
        0x108ac, 0x108d6, 0x10936, 0x10946, 0x109de, 0x10a46,
        0x10b02, 0x10b82, 0x10ba8, 0x10bde, 0x10c04, 0x10ca2,
        0x10cac, 0x10d40, 0x10d46, 0x10df4, 0x10e2a, 0x10e3e,
        0x10e4c, 0x10e6a, 0x10eb0, 0x10ed8, 0x10fbc, 0x161b8,
        0x163c2, 0x163ec, 0x1641c, 0x164da, 0x169d8
    })) OMainSequence::run(bus,r,entry);
    else if(entry==0x9810) OMainPlayer::initialize(bus,r);
    else if(entry==0x98c0) OMainPlayer::update(bus,r);
    else if(entry==0x9c34) OMainPlayer::shadow_initialize(bus,r);
    else if(entry==0x9c9e) OMainPlayer::shadow_update(bus,r);
    else if(entry==0xae8c || entry==0xaee4 || entry==0xaf08) OMainPlayer::exhaust_initialize(bus,r,entry);
    else if(entry==0xaf3c || entry==0xafcc || entry==0xb05c) OMainPlayer::exhaust_update(bus,r,entry);
    else if(entry==0x1653a || entry==0x1656a || entry==0x16572 || entry==0x1657a) OMainPlayer::sequence_update(bus,r,entry);
    else if(entry==0x1678e) OMainPlayer::sequence_project(bus,r);
    else if(entry==0x16c8a) OMainWeapons::reload(bus,r);
    else if(entry==0xcfc4) OMainWeapons::missile_hud(bus,r);
    else if(entry==0xd062) OMainWeapons::decimal(r);
    else if(entry==0x9d2c) OMainWeapons::initialize(bus,r);
    else if(entry==0x9d34) OMainWeapons::indicator_initialize(bus,r);
    else if(entry==0x9d56) OMainWeapons::update(bus,r);
    else if(entry==0x9df8) OMainWeapons::missile_fire(bus,r);
    else if(entry==0x9e50) OMainWeapons::vulcan_fire(bus,r);
    else if(entry==0x9e82) OMainWeapons::missile_allocate(bus,r);
    else if(entry==0x9ed8) OMainWeapons::vulcan_allocate(bus,r);
    else if(entry==0x9f08 || entry==0x9f22 || entry==0x9f44 || entry==0x9f5e)
        OMainWeapons::pool(bus,r,entry==0x9f44 || entry==0x9f5e,entry==0x9f08 || entry==0x9f44);
    else if(entry==0x9626 || entry==0x964c) OMainPlayer::recover(bus,r,entry);
    else if(entry==0x8946 || entry==0x8c1c || entry==0x9046 || entry==0x90ac || entry==0x9366 || entry==0x93a0 || entry==0x94b8)
        OMainWorld::run(bus,r,entry,boundary);
    else if(entry==0x88c2) OMainWorld::initialize(bus,r);
    else if(entry==0x8af2) OMainWorld::update(bus,r);
    else if(entry==0x91c0) OMainWorld::explosion(bus,r);
    else if(entry==0x8d44 || entry==0x8d58) OMainWorld::effect_initialize(bus,r,entry==0x8d44);
    else if(entry==0x8e18 || entry==0x8ece) OMainWorld::effect_update(bus,r,entry==0x8e18);
    else if(entry==0x8cc8) OMainWorld::player_collision(bus,r);
    else if(entry==0x9556) OMainWorld::release(bus,r);
    else if(entry==0x9f7c) OMainWeapons::missile_initialize(bus,r);
    else if(entry==0xa030) OMainWeapons::vulcan_initialize(bus,r);
    else if(entry==0xac4c) OMainWeapons::vulcan_update(bus,r);
    else if(entry==0xae1c || entry==0xae2e || entry==0xae50) OMainWeapons::remove(bus,r,entry!=0xae50,entry==0xae1c);
    else if(entry==0xa234 || entry==0xa382 || entry==0xa53a) OMainWeapons::missile_update(bus,r,entry);
    else if(entry==0xa9f2) OMainWeapons::trail_allocate(bus,r);
    else if(entry==0xaa32 || entry==0xaaee) OMainWeapons::trail(bus,r,entry==0xaa32);
    else if(entry==0xae32) OMainWeapons::remove(bus,r,true,false,false);
    else if(contains(entry,{
        0x14dc4, 0x14df0, 0x14e14, 0x14e1e, 0x14ff2, 0x14ff4,
        0x150c0, 0x1518a, 0x151b8, 0x15200, 0x1528a, 0x152a8,
        0x153e0, 0x154b8, 0x1552c, 0x15582, 0x15638, 0x156ce,
        0x1578c, 0x157ba, 0x157e8, 0x157f2, 0x1581a, 0x1583e,
        0x15842, 0x14ec8, 0x14ee2, 0x1500c, 0x15026, 0x1586c,
        0x158a8, 0x1592a, 0x15968, 0x15a6a, 0x15ab6, 0x15b0a,
        0x15b6e, 0x15bfe, 0x15c62, 0x15c7a, 0x15c94, 0x15c9a,
        0x15ca2, 0x15caa, 0x15cb0, 0x15d26, 0x15d36, 0x15d9e,
        0x15e6c, 0x15f48, 0x15f74, 0x16038
    })) OMainWeapons::enemy(bus,r,entry);
    else return false;
    return true;
}

void OMainHost::call(MainBus& bus, Registers& r, uint32_t entry, const Call& boundary) {
    if(execute(bus,r,entry,boundary)) return;
    if(!boundary) throw std::runtime_error("unported main host caller: " + std::to_string(entry));
    boundary(entry,r);
}

void OMainHost::dispatch(MainBus& bus, Registers& r, const Call& boundary, const Observe& observe) {
    // $014594 invokes live pointers through $0145AC/$0145AE. Preserve the
    // handler's register results and writes for the next slot, including
    // activation/removal of later records during this same traversal.
    OMainObjects::dispatch(bus,r,[&](uint32_t entry,Registers& state) {
        if(observe) observe(true,entry,state);
        call(bus,state,entry,boundary);
        if(observe) observe(false,entry,state);
    });
}
