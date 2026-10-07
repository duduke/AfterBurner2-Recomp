#!/usr/bin/env python3
"""Prove the Phase 3c/3d MAME checks reject constants changed in temporary ports.

Run from the repository root after make oracle-capture-frame-sprites and
make oracle-capture-objects. Copies,
executables and logs stay under ignored build/oracle; source is never changed.
"""
import os
from pathlib import Path
import subprocess
import sys

root = Path("build/oracle/mutations")
root.mkdir(parents=True, exist_ok=True)
common = ["src/main/engine/mainbus.cpp", "src/main/engine/subbus.cpp",
          "src/main/roms.cpp", "src/main/romloader.cpp"]
handlers = common + ["src/main/engine/omainsprites.cpp", "src/main/engine/omainhost.cpp", "src/main/engine/omainobjects.cpp", "src/main/engine/omaininput.cpp", "src/main/engine/omainplayer.cpp",
                     "src/main/engine/omainworld.cpp", "src/main/engine/omainweapons.cpp", "src/main/engine/omainsound.cpp",
                     "src/main/engine/omaindamage.cpp", "src/main/engine/omainsequence.cpp"]
def handler_dependencies(source):
    return [p for p in handlers if p != source]
cases = [
    ("worldscale", "src/main/engine/omainworld.cpp", "asr(d[1],d[0]&63)", "asr(d[1],(d[0]+1)&63)",
     "tests/oracle_handlers.cpp", "world_directed", handler_dependencies("src/main/engine/omainworld.cpp")),
    ("explosionage", "src/main/engine/omainworld.cpp", "// $0091C0-$0091FE: previous position, pause gate, age/depth and scale.\n    b.write32(p+8,b.read32(p+4))", "// $0091C0-$0091FE: previous position, pause gate, age/depth and scale.\n    b.write16(p+38,b.read16(p+38)+1); b.write32(p+8,b.read32(p+4))",
     "tests/oracle_handlers.cpp", "world_directed", handler_dependencies("src/main/engine/omainworld.cpp")),
    ("inputdeadband", "src/main/engine/omaininput.cpp", "if(s(d0)<=2) goto pitch", "if(s(d0)<=3) goto pitch",
     "tests/oracle_handlers.cpp", "input_directed", handler_dependencies("src/main/engine/omaininput.cpp")),
    ("playerdepth", "src/main/engine/omainplayer.cpp", "b.write16(p+36,0xc00)", "b.write16(p+36,0xc20)",
     "tests/oracle_handlers.cpp", "player_init", handler_dependencies("src/main/engine/omainplayer.cpp")),
    ("playerprojection", "src/main/engine/omainplayer.cpp", "w(d[7],b.read16(p+30)); d[1]=10", "w(d[7],b.read16(p+30)); d[1]=9",
     "tests/oracle_handlers.cpp", "player_directed", handler_dependencies("src/main/engine/omainplayer.cpp")),
    ("sequenceprojection", "src/main/engine/omainplayer.cpp", "b.write16(m-0x4000,0x208)", "b.write16(m-0x4000,0x209)",
     "tests/oracle_handlers.cpp", "player_parts_directed", handler_dependencies("src/main/engine/omainplayer.cpp")),
    ("exhaustframe", "src/main/engine/omainplayer.cpp", "0x29018 : 0x2939c", "0x2901c : 0x293a0",
     "tests/oracle_handlers.cpp", "player_parts_directed", handler_dependencies("src/main/engine/omainplayer.cpp")),
    ("reloadsound", "src/main/engine/omainweapons.cpp", "w(d0,0x9b)", "w(d0,0x9a)",
     "tests/oracle_handlers.cpp", "reload_directed", handler_dependencies("src/main/engine/omainweapons.cpp")),
    ("missilereserve", "src/main/engine/omainweapons.cpp", "w(r.d[1],r.d[1]+0x18)", "w(r.d[1],r.d[1]+0x17)",
     "tests/oracle_handlers.cpp", "weapon_control_directed", handler_dependencies("src/main/engine/omainweapons.cpp")),
    ("adcduration", "src/main/hwmath/adc0804.hpp", "completion=ns+59200", "completion=ns+60000",
     "tests/oracle_adc_timing.cpp", "adc_timing_directed", []),
    ("objectdispatch", "src/main/engine/omainobjects.cpp", "b.read8(r.a[5])&0x80", "b.read8(r.a[5])&0x40",
     "tests/oracle_objects.cpp", "objects_directed", common + ["src/main/engine/omainsound.cpp"]),
    ("compoundinit", "src/main/engine/omainobjects.cpp", "b.write8(p+3,0x46)", "b.write8(p+3,0x45)",
     "tests/oracle_objects.cpp", "object_calls_directed", common + ["src/main/engine/omainsound.cpp"]),
    ("compoundprepare", "src/main/engine/omainobjects.cpp", "r.a[0]+=0x300", "r.a[0]+=0x340",
     "tests/oracle_objects.cpp", "object_calls_directed", common + ["src/main/engine/omainsound.cpp"]),
    ("compoundhide", "src/main/engine/omainobjects.cpp", "word(r.d[1],3)", "word(r.d[1],2)",
     "tests/oracle_objects.cpp", "object_calls_directed", common + ["src/main/engine/omainsound.cpp"]),
    ("soundenqueue", "src/main/engine/omainsound.cpp", "b.write8(0xffff8906,b.read8(0xffff8906)+1)", "b.write8(0xffff8906,b.read8(0xffff8906)+2)",
     "tests/oracle_objects.cpp", "object_calls_directed", common + ["src/main/engine/omainobjects.cpp"]),
    ("objectinit", "src/main/engine/omainobjects.cpp", "r.a[1]-44", "r.a[1]-43",
     "tests/oracle_objects.cpp", "objects", common + ["src/main/engine/omainsound.cpp"]),
    ("objectrng", "src/main/engine/omainobjects.cpp", "r.d[1]<<=2", "r.d[1]<<=1",
     "tests/oracle_objects.cpp", "object_leaves_directed", common + ["src/main/engine/omainsound.cpp"]),
    ("soundring", "src/main/engine/omainsound.cpp", "word(r.d[1],r.d[1]+1); word(r.d[1],r.d[1]&0x3f);\n        b.write16(0xffff890a", "word(r.d[1],r.d[1]+1); word(r.d[1],r.d[1]&0x1f);\n        b.write16(0xffff890a",
     "tests/oracle_objects.cpp", "object_leaves_directed", common + ["src/main/engine/omainobjects.cpp"]),
    ("mainsprites", "src/main/engine/omainsprites.cpp", ">=0xc0", ">=0xbf",
     "tests/oracle_mainsprites.cpp", "mainsprites_directed", common),
    ("palette", "src/main/engine/omainpalette.cpp", "bank=(bank+1)&7", "bank=(bank+1)&3",
     "tests/oracle_palette.cpp", "palette_directed", common + ["src/main/engine/omainframe.cpp"]),
    ("mainframe", "src/main/engine/omainframe.cpp", "count=previous-1", "count=previous-2",
     "tests/oracle_mainframe.cpp", "mainframe_directed", common),
    ("subsprites", "src/main/engine/osubsprites.cpp", "s(bus.read16(a0)) < 0x1e",
     "s(bus.read16(a0)) < 0x1d", "tests/oracle_subsprites.cpp", "subsprites_directed",
     ["src/main/engine/subbus.cpp", "src/main/roms.cpp", "src/main/romloader.cpp"]),
]
# Host composition must reject transcript replay of verified entries too.
for name,source,old,new,capture,test in [
    ("hostreplay", "omainhost", "call(bus,state,entry,boundary)", "boundary(entry,state)", "host_objects", "tests/oracle_host_objects.cpp"),
    ("hostindicator", "omainhost", "OMainWeapons::indicator_initialize(bus,r)", "OMainWeapons::initialize(bus,r)", "host_entries_directed", "tests/oracle_handlers.cpp"),
    ("hostappend", "omainhost", "OMainPlayer::append(bus,r)", "OMainPlayer::shadow_update(bus,r)", "host_objects", "tests/oracle_host_objects.cpp"),
    ("hostregisters", "omainhost", "call(bus,state,entry,boundary);", "auto saved=state; call(bus,state,entry,boundary); state=saved;", "host_objects", "tests/oracle_host_objects.cpp"),
]:
    source="src/main/engine/"+source+".cpp"
    cases.append((name,source,old,new,test,capture,handler_dependencies(source)))

cases.append(('missilelaunch', 'src/main/engine/omainweapons.cpp', 'if(s(b.read16(p+62))>=8)', 'if(s(b.read16(p+62))>=9)', "tests/oracle_handlers.cpp", 'missiles_directed', handler_dependencies('src/main/engine/omainweapons.cpp')))
cases.append(('missileguidance', 'src/main/engine/omainweapons.cpp', 'r.a[0]=0x34018; r.a[1]=0x34098', 'r.a[0]=0x34019; r.a[1]=0x34098', "tests/oracle_handlers.cpp", 'missiles_directed', handler_dependencies('src/main/engine/omainweapons.cpp')))
cases.append(('missilehit', 'src/main/engine/omainweapons.cpp', 'b.write8(r.a[4]+22,b.read8(r.a[4]+22)|2)', 'b.write8(r.a[4]+22,b.read8(r.a[4]+22)|4)', "tests/oracle_handlers.cpp", 'projectiles_directed', handler_dependencies('src/main/engine/omainweapons.cpp')))
cases.append(('vulcanaim', 'src/main/engine/omainweapons.cpp', 'w(d[7],0x4e)', 'w(d[7],0x4f)', "tests/oracle_handlers.cpp", 'projectiles_directed', handler_dependencies('src/main/engine/omainweapons.cpp')))
cases.append(('trailwidth', 'src/main/engine/omainweapons.cpp', 'w(d[1],d[1]+8);', 'w(d[1],d[1]+9);', "tests/oracle_handlers.cpp", 'missiles_directed', handler_dependencies('src/main/engine/omainweapons.cpp')))
cases.append(('enemyzoom', 'src/main/engine/omainweapons.cpp', 'entry==0x1578c ? 1 : 2', 'entry==0x1578c ? 2 : 2', "tests/oracle_handlers.cpp", 'enemy_weapons_directed', handler_dependencies('src/main/engine/omainweapons.cpp')))
cases.append(('enemysway', 'src/main/engine/omainweapons.cpp', 'd[1]=entry==0x1583e ? 7 : 6', 'd[1]=entry==0x1583e ? 7 : 5', "tests/oracle_handlers.cpp", 'enemy_weapons_directed', handler_dependencies('src/main/engine/omainweapons.cpp')))
cases.append(('enemyshadow', 'src/main/engine/omainweapons.cpp', 'b.write32(p+12,0x19040)', 'b.write32(p+12,0x19090)', "tests/oracle_handlers.cpp", 'enemy_parts_directed', handler_dependencies('src/main/engine/omainweapons.cpp')))
cases.append(('groundnoise', 'src/main/engine/omainworld.cpp', 'r.a[0]=0x345c8', 'r.a[0]=0x345cc', "tests/oracle_handlers.cpp", 'effects_directed', handler_dependencies('src/main/engine/omainworld.cpp')))
cases.append(('collisiondamage', 'src/main/engine/omainworld.cpp', 'b.write8(0xffff8d60,b.read8(0xffff8d60)|1)', 'b.write8(0xffff8d60,b.read8(0xffff8d60)|2)', "tests/oracle_handlers.cpp", 'collision_directed', handler_dependencies('src/main/engine/omainworld.cpp')))
cases.append(('playerrecovery', 'src/main/engine/omainplayer.cpp', 'b.write16(p+36,b.read16(p+36)+0x18)', 'b.write16(p+36,b.read16(p+36)+0x19)', "tests/oracle_handlers.cpp", 'weapon_control_directed', handler_dependencies('src/main/engine/omainplayer.cpp')))

# Remaining carrier, horizon, tracking/debris, world and refuel graph checks.
for name,source,old,new,capture in [
    ("carrierspeed", "omainsequence", "if(s(d[0])>4) d[0]=4", "if(s(d[0])>4) d[0]=5", "sequence_directed"),
    ("carrierattachment", "omainsequence", "b.write8(r.a[0]+60,d[1])", "b.write8(r.a[0]+60,d[1]+1)", "sequence_directed"),
    ("horizonstep", "omainsequence", "d[4]=0x17ffd0", "d[4]=0x18ffd0", "sequence_directed"),
    ("trackingtimer", "omainweapons", "auto old=b.read8(p+58),value=uint8_t(old-1)", "auto old=b.read8(p+58),value=uint8_t(old-2)", "enemy_tracking_directed"),
    ("burstvelocity", "omainweapons", "b.write16(a[1]+40,0x180); w(d[3],d[3]-1)", "b.write16(a[1]+40,0x181); w(d[3],d[3]-1)", "enemy_tracking_directed"),
    ("grounddebris", "omainweapons", "a[0]=0x160ea+s(d[0])", "a[0]=0x160ec+s(d[0])", "enemy_tracking_directed"),
    ("worldhitwidth", "omainworld", "a[1]+=2; asr(d[2],1); w(d[0],b.read16(p+4))", "a[1]+=2; asr(d[2],2); w(d[0],b.read16(p+4))", "world_graph_directed"),
    ("collisionoverflow", "omainworld", "((d[0]&0x8000)!=0)!=overflow || s(d[0])>=0x200", "s(d[0])<0 || s(d[0])>=0x200", "world_graph_directed"),
    ("refuelflight", "omainsequence", "if(s(d[0])>2) d[0]=2", "if(s(d[0])>2) d[0]=3", "refuel_parts_directed"),
    ("refuelattitude", "omainsequence", "w(d[1],6);\n        if(s(d[0])>s(d[1]))", "w(d[1],7);\n        if(s(d[0])>s(d[1]))", "refuel_parts_directed"),
]:
    source="src/main/engine/"+source+".cpp"
    cases.append((name,source,old,new,"tests/oracle_handlers.cpp",capture,handler_dependencies(source)))

# Complete comparator IRQ: acknowledgement, six-lane wrap and sound consumer.
irq_common = ["src/main/engine/mainbus.cpp", "src/main/engine/subbus.cpp"]
for name,source,old,new,extra in [
    ("irqack", "src/main/engine/omainadc.cpp", "bus.write16(0x0e8012, r.d[0])", "bus.write16(0x0e8010, r.d[0])", ["src/main/engine/omainsound.cpp"]),
    ("irqsamplewrap", "src/main/engine/omainadc.cpp", "if (d1 >= 6) d1 = 0", "if (d1 >= 5) d1 = 0", ["src/main/engine/omainsound.cpp"]),
    ("irqsoundring", "src/main/engine/omainsound.cpp", "word(r.d[1],r.d[1]+1); word(r.d[1],r.d[1]&0x3f);\n        b.write16(0xffff890a", "word(r.d[1],r.d[1]+1); word(r.d[1],r.d[1]&0x1f);\n        b.write16(0xffff890a", ["src/main/engine/omainadc.cpp"]),
]:
    cases.append((name,source,old,new,"tests/oracle_mainirq.cpp","mainirq_endpoints",irq_common+extra))

# Physical board schedule: reject incorrect line clock, VBLANK edge and
# ordering of the frame notifier relative to a coincident raster edge.
for name,old,new in [
    ("boardclock", "next_line += LINE_NS", "next_line += LINE_NS + 1"),
    ("boardblank", "if (line == 223) blank = true", "if (line == 222) blank = true"),
    ("boardorder", "advance_to(ns, false)", "advance_to(ns, true)"),
]:
    cases.append((name,"src/main/hwmath/xboard_timeline.hpp",old,new,
                  "tests/oracle_board.cpp","board_endpoints",[]))

# CPU service clocks must change outputs without changing the captured inputs.
service_dependencies = common + ["src/main/engine/omainirqcallee.cpp"]
for name,old,new in [
    ("serviceentry", "idle(6);write16(sp-2,c.return_pc)", "idle(8);write16(sp-2,c.return_pc)"),
    ("servicevpa", "sync=cycles-mod+(mod<7 ? 10 : 20)", "sync=cycles-mod+10"),
    ("serviceadc", "pc=0x14626;idle(8)", "pc=0x14626;idle(6)"),
    ("servicewrap", "else { idle(12);pc=0x14640", "else { idle(10);pc=0x14640"),
    ("servicelatch", "pc=0xb81e;idle(6)", "pc=0xb81e;idle(4)"),
    ("servicetail", "return (size==2 ? 4 : 2)+2*count", "return (size==2 ? 4 : 2)+2*count+2"),
]:
    cases.append((name,"src/main/engine/omainirqservice.cpp",old,new,
                  "tests/oracle_irq_service.cpp","service_directed",service_dependencies))
cases.append(("servicemask","src/main/engine/omainirqservice.hpp",
              "return level>((sr>>8)&7)","return level!=0",
              "tests/oracle_irq_service.cpp","service_directed",service_dependencies+["src/main/engine/omainirqservice.cpp"]))

# IRQ4 and caller clocks: external callee durations never erase drift.
for name,old,new,capture,test in [
    ("irq4bsr", "pc=p;idle(absolute_long?8:2)", "pc=p;idle(absolute_long?8:4)", "irq4_service_edges", "tests/oracle_irq4_service.cpp"),
    ("irq4overflow", "const bool logic=int16_t(previous)<=0", "const bool logic=int16_t(uint16_t(previous-1))<0", "irq4_service_edges", "tests/oracle_irq4_service.cpp"),
    ("irq4delay", "word(r.d[0],r.d[0]-1);idle(14)", "word(r.d[0],r.d[0]-1);idle(12)", "irq4_service_edges", "tests/oracle_irq4_service.cpp"),
    ("irq4blit", "write16(r.a[1],v>>16);write16(r.a[1]+2,v)", "write16(r.a[1]+2,v);write16(r.a[1],v>>16)", "irq4_service_edges", "tests/oracle_irq4_service.cpp"),
    ("irq4redirect", "write16(sp,0x700)", "write16(sp,0x701)", "irq4_service_edges", "tests/oracle_irq4_service.cpp"),
    ("irq4roadcount", "word(r.d[0],0x1f);idle(8)", "word(r.d[0],0x20);idle(8)", "irq4_service_edges", "tests/oracle_irq4_service.cpp"),
    ("irq4roadshift", "pc=0x13bda;word(r.d[0],r.d[0]<<4);idle(14)", "pc=0x13bda;word(r.d[0],r.d[0]<<4);idle(12)", "irq4_service_edges", "tests/oracle_irq4_service.cpp"),
    ("irq4roadstage", "word(r.d[1],r.d[1]+r.d[1]);idle(4)", "word(r.d[1],r.d[1]+r.d[1]+2);idle(4)", "irq4_service_edges", "tests/oracle_irq4_service.cpp"),
    ("irq4cycleshift", "idle(alternate?8:12)", "idle(alternate?8:10)", "irq4_service_cycle_endpoints", "tests/oracle_irq4_service.cpp"),
    ("irq4cyclemask", "r.d[0]&(alternate?0x1e0:0x3e0)", "r.d[0]&(alternate?0x1c0:0x3c0)", "irq4_service_cycle_endpoints", "tests/oracle_irq4_service.cpp"),
    ("irq4cyclebranch", "uint16_t(r.d[0])<6;idle", "uint16_t(r.d[0])<5;idle", "irq4_service_cycle_endpoints", "tests/oracle_irq4_service.cpp"),
    ("waitbranch", "idle(8); // Short conditional", "idle(10); // Short conditional", "logic_wait_directed", "tests/oracle_logic_wait.cpp"),
    ("waitrearm", "write16(0x2a3c00,1)", "write16(0x2a3c00,2)", "logic_wait_directed", "tests/oracle_logic_wait.cpp"),
]:
    cases.append((name,"src/main/engine/omainirqservice.cpp",old,new,test,capture,service_dependencies))

for name,old,new in [
    ("irq4rts14990", "pc=0x14990;const auto ret=read32(sp);idle(8)", "pc=0x14990;const auto ret=read32(sp);idle(6)"),
    ("irq4rtse2da", "pc=0xe2da;const auto ret=read32(sp);idle(8)", "pc=0xe2da;const auto ret=read32(sp);idle(6)"),
]:
    cases.append((name,"src/main/engine/omainirqservice.cpp",old,new,
                  "tests/oracle_irq4_service.cpp","irq4_service_rts_callees",service_dependencies))

# Each newly timed callee has a literal instruction mutation; the shadow
# include leaves the source tree and runtime ROM/capture inputs unchanged.
for name,old,new in [
    ("irq4fadecount", "move(2,{I,0,0x3f},{D,0},false); pc=0x14b14", "move(2,{I,0,0x3e},{D,0},false); pc=0x14b14"),
    ("irq4animateshift", "shift(ASR,2,{I,0,0x4},0); pc=0x1406e", "shift(ASR,2,{I,0,0x3},0); pc=0x1406e"),
    ("irq4reloadsound", "move(2,{I,0,0x9b},{D,0},false); pc=0x16d00", "move(2,{I,0,0x9a},{D,0},false); pc=0x16d00"),
    ("irq4sceneryblank", "move(4,{I,0,0x200020},{D,0},false); pc=0x13a84", "move(4,{I,0,0x210021},{D,0},false); pc=0x13a84"),
    ("irq4transitionend", "alu(CMP,2,{I,0,0x20},{W,0,0xffff8c2a},true,false)", "alu(CMP,2,{I,0,0x21},{W,0,0xffff8c2a},true,false)"),
]:
    cases.append((name,"src/main/engine/omainirqcallee.inc",old,new,
                  "tests/oracle_irq4_service.cpp","irq4_service_callee_endpoints",
                  common+["src/main/engine/omainirqservice.cpp","src/main/engine/omainirqcallee.cpp"]))

# Newly closed game-flow graphs: root selection, decimal credits, lamp and
# high-score data/timing must each reject their own literal mutation.
for name,old,new in [
    ("gameflowstage", "alu(CMP,2,{I,0,3},{L,0,0x29c09c},true,false); pc=0x1714e", "alu(CMP,2,{I,0,4},{L,0,0x29c09c},true,false); pc=0x1714e"),
    ("gameflowcoin", "moveq({I,0,6},1); pc=0xc726", "moveq({I,0,5},1); pc=0xc726"),
    ("gameflowrank", "alu(CMP,4,{M,0},{D,0},false,false); pc=0x17616", "alu(CMP,4,{O,0,2},{D,0},false,false); pc=0x17616"),
    ("gameflowlamp", "move(1,{I,0,6},{O,1,2},false); pc=0xc956", "move(1,{I,0,5},{O,1,2},false); pc=0xc956"),
]:
    cases.append((name,"src/main/engine/omainirqgameflow.inc",old,new,
                  "tests/oracle_irq4_service.cpp","irq4_service_gameflow_endpoints",service_dependencies+["src/main/engine/omainirqservice.cpp"]))
for name,old,new in [
    ("gameflowbcd", "if(low>9)result+=6", "if(low>9)result+=5"),
    ("gameflowshift", "idle((size==4?8:6)+2*count)", "idle((size==4?8:6)+2*count+2)"),
]:
    cases.append((name,"src/main/engine/omainirqcallee.cpp",old,new,
                  "tests/oracle_irq4_service.cpp","irq4_service_gameflow_endpoints",common+["src/main/engine/omainirqservice.cpp"]))

cases.append(("gameflowextend","src/main/engine/omainirqservice.cpp",
              "callee_x=value==0", "callee_x=false",
              "tests/oracle_irq4_service.cpp","irq4_service_gameflow_actions",service_dependencies))

# Natural IRQ, linked output and continuous-ring acceptance sensitivity.
acceptance = handlers + ["src/main/engine/omainadc.cpp", "src/main/engine/omainframe.cpp", "src/main/engine/omainpalette.cpp", "src/main/engine/oscenery.cpp"]
video = common + ["src/main/engine/omainsprites.cpp", "src/main/engine/osubsprites.cpp"] + ["src/main/hwvideo/"+p+".cpp" for p in ["hwroad", "hwsprites", "hwtiles", "hwpalette", "screen"]]
for name,source,old,new,test,capture,deps in [
    ("irq4normalphase", "src/main/engine/omainframe.cpp", "b.write16(0x3f8816,0x100)", "b.write16(0x3f8816,0x101)", "tests/oracle_host_interleave.cpp", "paired_objects", acceptance),
    ("sharedhistory", "src/main/engine/oscenery.cpp", "d1=b.read16(0x29c620+s(d1))&0xfc", "d1=(b.read16(0x29c620+s(d1))&0xfc)+4", "tests/oracle_frame_host.cpp", "paired_frame", acceptance),
    ("publicationcount", "src/main/engine/omainsprites.cpp", "b.write16(dst,b.read16(0x2a3cf0))", "b.write16(dst,b.read16(0x2a3cf0)+1)", "tests/oracle_host_interleave.cpp", "host_publication", acceptance),
    ("drawendmarker", "src/main/engine/mainbus.cpp", "m.bytes[SPRITE]=0xff;m.bytes[SPRITE+1]=0xff", "m.bytes[SPRITE]=0;m.bytes[SPRITE+1]=0", "tests/oracle_frame_host.cpp", "paired_frame", acceptance),
    ("framewordorder", "src/main/engine/omainframe.cpp", "b.write16(0x3f8814,value);b.write16(0x3f8812,value>>16)", "b.write16(0x3f8812,value>>16);b.write16(0x3f8814,value)", "tests/oracle_frame_host.cpp", "paired_frame", acceptance),
    ("publicationregister", "src/main/engine/omainsprites.cpp", "r.a[0]=b.read32(0x2a3c0c)", "r.a[0]=b.read32(0x2a3c0c)+2", "tests/oracle_host_objects.cpp", "host_objects", handlers),
    ("interleavedsample", "src/main/engine/omainadc.cpp", "if (d1 >= 6) d1 = 0", "if (d1 >= 5) d1 = 0", "tests/oracle_host_interleave.cpp", "host_prefix", acceptance),
    ("pairedproducer", "src/main/engine/omainplayer.cpp", "b.write8(r.a[0],b.read8(r.a[5]+20))", "b.write8(r.a[0],b.read8(r.a[5]+21))", "tests/oracle_host_interleave.cpp", "paired_objects", acceptance),
    ("pairedblit", "src/main/engine/omainsprites.cpp", "(interpolated?0xc00:0)", "(interpolated?0xc10:0)", "tests/oracle_video.cpp", "paired", video),
    ("pairedframe", "src/main/engine/omainsprites.cpp", "(interpolated?0xc00:0)", "(interpolated?0xc10:0)", "tests/oracle_frame_host.cpp", "paired_frame", acceptance),
    ("streamsounds", "src/main/engine/omainsound.cpp", "b.write8(0xffff8906,b.read8(0xffff8906)-1)", "b.write8(0xffff8906,b.read8(0xffff8906)-2)", "tests/oracle_sound_stream.cpp", "paired_sound", ["src/main/engine/omainsound.cpp", "src/main/engine/mainbus.cpp", "src/main/engine/subbus.cpp"]),
]:
    cases.append((name,source,old,new,test,capture,[p for p in deps if p!=source]))

for name,old,new in [
    ("callerrearm", "constexpr uint16_t armed=1", "constexpr uint16_t armed=2"),
    ("callerreturn", "write16(sp,1);write16(sp+2,0x4594)", "write16(sp,0);write16(sp+2,0x4594)"),
    ("callerpoll", "auto level=poll(ns)", "auto level=poll(ns+CYCLE_NS*4)"),
    ("callercarry", "const bool carry=subtract?old<1:old==0xffff", "const bool carry=false"),
]:
    cases.append((name, "src/main/engine/omainirqservice.cpp", old, new,
                  "tests/oracle_caller.cpp", "caller_directed",
                  ["src/main/engine/omainirqcallee.cpp", *common]))

for name,old,new in [
    ("visibilityearly", "std::max(board.now(), std::min(ns, end))", "std::max(board.now(), ns)"),
    ("visibilityphase", "std::max(board.now(), std::min(ns, end))", "std::max(board.now(), std::min(ns, end + 80))"),
]:
    cases.append((name,"src/main/hwmath/xboard_cpu_visibility.hpp",old,new,
                  "tests/oracle_cpu_visibility.cpp","caller_directed",irq_common))

cases.append(("visibilitytail","src/main/hwmath/m68000_completion.hpp",
              "return (size==2 ? 4 : 2)+2*count", "return 0",
              "tests/oracle_cpu_visibility.cpp","caller",irq_common))

# Optional names rerun changed fixtures without rebuilding unaffected mutations.
cases.append(("callerresumesr", "src/main/engine/omaincallerschedule.hpp",
              "current={result,selected,selection};entered=false;return result;",
              "result.sr^=1;current={result,selected,selection};entered=false;return result;",
              "tests/oracle_caller.cpp", "caller_service_edges",
              ["src/main/engine/omainirqservice.cpp", "src/main/engine/omainirqcallee.cpp",
               "src/main/engine/mainbus.cpp", "src/main/engine/subbus.cpp",
               "src/main/roms.cpp", "src/main/romloader.cpp"]))

cases.append(("callerresumeusp", "src/main/engine/omaincallerschedule.hpp",
              "result.sp=user_sp;", "result.sp=user_sp+2;",
              "tests/oracle_caller.cpp", "caller_service_edges",
              ["src/main/engine/omainirqservice.cpp", "src/main/engine/omainirqcallee.cpp",
               "src/main/engine/mainbus.cpp", "src/main/engine/subbus.cpp",
               "src/main/roms.cpp", "src/main/romloader.cpp"]))

for name,old,new in [
    ("rtemask", "eligible(level,sr)?level:0", "eligible(level,current.state.sr)?level:0"),
    ("rtefrontier", "selection=ns;auto level=physical(ns)", "selection=ns+80;auto level=physical(ns)"),
    ("rtecurpc", "completed_pc=service.current_pc()", "completed_pc=result.pc"),
]:
    cases.append((name,"src/main/engine/omaincallerschedule.hpp",old,new,
                  "tests/oracle_caller.cpp","caller_return_edges",
                  ["src/main/engine/omainirqservice.cpp", "src/main/engine/omainirqcallee.cpp", *common]))

# Continuous production input producers must affect the linked CPU sampler.
for name,source,old,new in [
    ("calleradclane", "src/main/engine/omainio.hpp", "inputs[(mux>>2)&7]", "inputs[((mux>>2)+1)&7]"),
    ("calleradcclock", "src/main/engine/omainio.hpp", "transaction_ns.value_or(board.now())", "transaction_ns.value_or(board.now())+1000000000"),
]:
    cases.append((name,source,old,new,"tests/oracle_caller.cpp","caller_return_edges",
                  ["src/main/engine/omainirqservice.cpp", "src/main/engine/omainirqcallee.cpp", *common]))

cases.append(("hostsequenceinit", "src/main/engine/omainhost.cpp",
              "OMainObjects::player_initialize(bus,r)", "OMainObjects::player_prepare(bus,r)",
              "tests/oracle_objects.cpp", "object_calls_directed", handler_dependencies("src/main/engine/omainhost.cpp")))
cases.append(("pairedroadcenter", "src/main/engine/osubroad.cpp",
              "ROAD0_SCROLL_CENTER = 0x0ec4e0", "ROAD0_SCROLL_CENTER = 0x0ec4e2",
              "tests/oracle_video.cpp", "paired_road",
              ["src/main/romloader.cpp", "src/main/roms.cpp", "src/main/engine/subbus.cpp",
               "src/main/engine/mainbus.cpp", "src/main/engine/omainsprites.cpp", "src/main/engine/osubsprites.cpp",
               "src/main/hwvideo/hwroad.cpp", "src/main/hwvideo/hwsprites.cpp", "src/main/hwvideo/hwtiles.cpp",
               "src/main/hwvideo/hwpalette.cpp", "src/main/hwvideo/screen.cpp"]))
for name,source,old,new,test,capture in [
    ("closureramprefetch", "src/main/engine/omainirqservice.cpp",
     "if((returnpc&0x3fffff)>=0x80000)read16(returnpc);else idle(4)", "idle(4)", "tests/oracle_caller.cpp", "caller_closure_directed"),
    ("closuredepth", "src/main/engine/omainirqservice.cpp",
     "const auto value=read16(r.a[5]+36)", "const auto value=read16(r.a[5]+38)", "tests/oracle_caller.cpp", "caller_closure"),
    ("closureshift", "src/main/engine/omainirqservice.cpp",
     "for(unsigned i=0;i<4;++i) {\n            carry=value&0x8000", "for(unsigned i=0;i<3;++i) {\n            carry=value&0x8000", "tests/oracle_caller.cpp", "caller_closure"),
    ("closurepoll", "src/main/engine/omainirqservice.cpp",
     "latch();idle(2);next=pc+2;break;\n    }\n    // $8B06", "idle(2);latch();next=pc+2;break;\n    }\n    // $8B06", "tests/oracle_caller.cpp", "caller_closure"),
    ("closurecarry", "src/main/engine/omainirqservice.cpp",
     "subtract?old<source:uint64_t(old)+source>0xffffffff", "false", "tests/oracle_caller.cpp", "caller_closure"),
    ("closureprojection", "src/main/engine/omainirqservice.cpp",
     "pc==0x8b64?0x29c092:0x29c090", "pc==0x8b64?0x29c090:0x29c090", "tests/oracle_caller.cpp", "caller_closure"),
    ("closureclip", "src/main/engine/omainirqservice.cpp",
     "case 0x8bb0:branch(!(sr&4)&&bool(sr&8)==bool(sr&2),0x9556,4)", "case 0x8bb0:branch(!(sr&4)&&bool(sr&8)==bool(sr&2),0x955a,4)", "tests/oracle_caller.cpp", "caller_closure"),
    ("closurerelease", "src/main/engine/omainirqservice.cpp",
     "write8(r.a[5],old&~0x40)", "write8(r.a[5],old&~0x80)", "tests/oracle_caller.cpp", "caller_closure"),
    ("readybit", "src/main/engine/omainirqservice.cpp",
     "read8(0x29c200);sr=(sr&~4)|(value&1?0:4)", "read8(0x29c200);sr=(sr&~4)|(value&2?0:4)", "tests/oracle_caller.cpp", "caller_ready_directed"),
    ("readyreturn", "src/main/engine/omainirqservice.cpp",
     "write16(sp+2,0xc098)", "write16(sp+2,0xc09a)", "tests/oracle_caller.cpp", "caller_ready_directed"),
    ("initprojection", "src/main/engine/omainirqservice.cpp",
     "pc==0x88de?0x29c092:0x29c090", "pc==0x88de?0x29c090:0x29c090", "tests/oracle_caller.cpp", "caller_worldinit_directed"),
    ("initshift", "src/main/engine/omainirqservice.cpp",
     "for(unsigned i=0;i<count;++i){carry=value&0x80000000", "for(unsigned i=0;i<count-1;++i){carry=value&0x80000000", "tests/oracle_caller.cpp", "caller_worldinit_directed"),
    ("producercounterreturn", "src/main/engine/omainirqservice.cpp",
     "old=read16(0xffff8b5a);\n        const auto v=addsub_word(old,1,false,true)", "old=read16(0xffff8b5a);\n        const auto v=addsub_word(old,2,false,true)", "tests/oracle_caller.cpp", "caller_soundproducer_directed"),
    ("producerwrap", "src/main/engine/omainirqservice.cpp",
     "word(r.d[1],r.d[1]&0x3f);nz(r.d[1])", "word(r.d[1],r.d[1]&0x1f);nz(r.d[1])", "tests/oracle_caller.cpp", "caller_soundproducer_directed"),
    ("producerstage", "src/main/engine/omainirqservice.cpp",
     "addsub_word(read16(0x29c09c),1,true,false)", "addsub_word(read16(0x29c09c),2,true,false)", "tests/oracle_caller.cpp", "caller_soundproducer_directed"),
    ("producercount", "src/main/engine/omainirqservice.cpp",
     "old=read8(0xffff8906),v=uint8_t(old+1)", "old=read8(0xffff8906),v=uint8_t(old+2)", "tests/oracle_caller.cpp", "caller_soundproducer_directed"),
    ("traildrift", "src/main/engine/omainirqservice.cpp",
     "read16(0x29c0c4)", "read16(0x29c0c6)", "tests/oracle_caller.cpp", "caller_trail_directed"),
    ("trailpool", "src/main/engine/omainirqservice.cpp",
     "r.a[0]=0xffff8e7f", "r.a[0]=0xffff8e80", "tests/oracle_caller.cpp", "caller_trail_directed"),
    ("clearstride", "src/main/engine/omainirqservice.cpp",
     "((pc-base+2)/4)*16", "((pc-base+2)/4)*8", "tests/oracle_caller.cpp", "caller_allirq_trail"),
    ("clearloop", "src/main/engine/omainirqservice.cpp",
     "pc==0xb6b4||pc==0xb6ea?0xff:0x1f", "pc==0xb6b4||pc==0xb6ea?0xfe:0x1f", "tests/oracle_caller.cpp", "caller_allirq_trail"),
    ("linked4return", "src/main/engine/omainirqservice.cpp",
     "if((ret&0x3fffff)>=0x80000)read16(ret);else idle(4);", "idle(2);if((ret&0x3fffff)>=0x80000)read16(ret);else idle(4);", "tests/oracle_caller.cpp", "caller_allirq_trail"),
    ("publicationinstall", "src/main/engine/omainirqservice.cpp",
     "write16(r.a[5]+18,0xb676)", "write16(r.a[5]+18,0xb67c)", "tests/oracle_caller.cpp", "caller_ready"),
    ("followerstage", "src/main/engine/omainirqservice.cpp",
     "pc==0x7bce?7:1", "pc==0x7bce?6:1", "tests/oracle_caller.cpp", "caller_follower_directed"),
    ("followerage", "src/main/engine/omainirqservice.cpp",
     "read8(r.a[5]+21),value=uint8_t(old-4)", "read8(r.a[5]+21),value=uint8_t(old-3)", "tests/oracle_caller.cpp", "caller_follower_directed"),
    ("successorsuboperand", "src/main/engine/omainirqservice.cpp",
     "const auto old=read16(r.a[5]+36)", "const auto old=read16(r.a[5]+38)", "tests/oracle_caller.cpp", "caller_successors"),
    ("successorbranch", "src/main/engine/omainirqservice.cpp",
     "case 0xb632:branch(bool(sr&8)==bool(sr&2),0xb654,4)",
     "case 0xb632:branch(bool(sr&8)==bool(sr&2),0xb656,4)", "tests/oracle_caller.cpp", "caller_successors_directed"),
    ("returnedaltitude", "src/main/engine/omainirqservice.cpp",
     "const auto value=read16(0x29c0aa)", "const auto value=read16(0x29c0a8)", "tests/oracle_caller.cpp", "caller_words"),
    ("returnedcompare", "src/main/engine/omainirqservice.cpp",
     "constexpr uint16_t limit=0xc0", "constexpr uint16_t limit=0", "tests/oracle_caller.cpp", "caller_words"),
    ("returnedcopy", "src/main/engine/omainirqservice.cpp",
     "write16(r.a[5]+8,value>>16);write16(r.a[5]+10,value)",
     "write16(r.a[5]+12,value>>16);write16(r.a[5]+14,value)", "tests/oracle_caller.cpp", "caller_steps"),
    ("returnedbsr", "src/main/engine/omainirqservice.cpp",
     "write16(sp+2,0x5c02)", "write16(sp+2,0x5c04)", "tests/oracle_caller.cpp", "caller_steps"),
    ("irq4returnusp", "src/main/engine/omaincallerschedule.hpp",
     "result.sp=user_sp;", "result.sp=user_sp+2;", "tests/oracle_irq4_service.cpp", "irq4_return_edges"),
    ("irq4returnmask", "src/main/engine/omaincallerschedule.hpp",
     "eligible(level,sr)?level:0", "eligible(level,current.state.sr)?level:0", "tests/oracle_irq4_service.cpp", "irq4_return_edges"),
    ("irq4returnphase", "src/main/engine/omaincallerschedule.hpp",
     "selection=ns;auto level=physical(ns)", "selection=ns;auto level=physical(ns+80)", "tests/oracle_irq4_service.cpp", "irq4_return_edges"),
]:
    deps=["src/main/engine/omainirqcallee.cpp", *common]
    if source.endswith(".hpp"):deps+=["src/main/engine/omainirqservice.cpp"]
    cases.append((name,source,old,new,test,capture,deps))
# Generated input cases are compiled through the same production caller.
for name,old,new in [
    ("inputtimeddeadband", "case 0xb116: {\n    read16(0xb11a);\n    const uint32_t operand=0x2;", "case 0xb116: {\n    read16(0xb11a);\n    const uint32_t operand=0x3;"),
    ("inputtimedresync", "const auto value=old&~0x4;", "const auto value=old&~0x2;"),
    ("inputtimeddivisor", "const uint32_t value=0x71;", "const uint32_t value=0x72;"),
    ("inputtimedbuttons", "const auto value=uint8_t(~old);", "const auto value=uint8_t(old);"),
]:
    cases.append((name,"src/main/engine/omaininputschedule.inc",old,new,
                  "tests/oracle_caller.cpp","caller_input_directed",
                  ["src/main/engine/omainirqservice.cpp","src/main/engine/omainirqcallee.cpp",*common]))
cases.append(("inputsamplemux","src/main/engine/omainio.hpp",
              "uint8_t selected_mux() const { return mux; }",
              "uint8_t selected_mux() const { return mux^4; }",
              "tests/oracle_caller.cpp","caller_input_short",
              ["src/main/engine/omainirqservice.cpp","src/main/engine/omainirqcallee.cpp",*common]))
# Timed flight arithmetic, chip operands, attachment orientation and phases.
for name,old,new in [
    ("flighttimedslew", "case 0x9938: {\n    read16(0x993c);\n    const uint32_t operand=0x4;", "case 0x9938: {\n    read16(0x993c);\n    const uint32_t operand=0x5;"),
    ("flighttimeddivisor", "case 0x99da: {\n    read16(0x99de);\n    const uint32_t value=0x3;", "case 0x99da: {\n    read16(0x99de);\n    const uint32_t value=0x4;"),
    ("flighttimedproduct", "asr_long(0,7);", "asr_long(0,6);"),
    ("flighttimedflip", "r.d[2]=(r.d[2]<<16)|(r.d[2]>>16);", "r.d[2]=r.d[2];"),
    ("flighttimedshift", "case 0x9b7a: {\n    asl_long(0,(r.d[1]&63));", "case 0x9b7a: {\n    asl_long(0,((r.d[1]+1)&63));"),
]:
    cases.append((name,"src/main/engine/omainflightschedule.inc",old,new,
                  "tests/oracle_caller.cpp","caller_flight_directed",
                  ["src/main/engine/omainirqservice.cpp","src/main/engine/omainirqcallee.cpp",*common]))
for name,old,new in [
    ("flightreturnflag", "old&0x2?0:4", "old&0x2?4:0"),
    ("flightreturnoffset", "read8(r.a[5]+(-2))", "read8(r.a[5]+(-4))"),
    ("flightreturncopy", "read16(0x60ce);", "read16(0x60d0);"),
    ("flightreturnworld", "read16(0x7654);", "read16(0x7656);"),
    ("flightreturndip", "read8(0xffff8070)", "read8(0xffff8071)"),
]:
    cases.append((name,"src/main/engine/omainflightschedule.inc",old,new,
                  "tests/oracle_caller.cpp","caller_flight_directed",
                  ["src/main/engine/omainirqservice.cpp","src/main/engine/omainirqcallee.cpp",*common]))
# Timed shadow/exhaust: arithmetic width, strict bank zero, ROM table lane,
# sprite scale bits, projection shift and prefetch placement.
for name,old,new in [
    ("companiontimedbyte", "case 0xaf64: {\n    const uint32_t operand=1;\n    const auto old=uint8_t(r.d[0]);\n    const auto value=addsub_byte", "case 0xaf64: {\n    const uint32_t operand=1;\n    const auto old=uint8_t(r.d[0]);\n    const auto value=addsub_word"),
    ("companiontimedzero", "branch(!(sr&4)&&bool(sr&8)==bool(sr&2),0xb00e,4);", "branch(bool(sr&8)==bool(sr&2),0xb00e,4);"),
    ("companiontimedtable", "read32(r.a[1]+(4)+int16_t(r.d[0]))", "read32(r.a[1]+(0)+int16_t(r.d[0]))"),
    ("companiontimedscale", "case 0xaec4: {\n    read16(0xaec8);\n    const uint32_t operand=0x7f;", "case 0xaec4: {\n    read16(0xaec8);\n    const uint32_t operand=0x7e;"),
    ("companiontimedshadow", "asr_word(0,8);", "asr_word(0,7);"),
    ("companiontimedreturn", "case 0x77ac: {\n    next=read32(sp);sp+=4;read16(next);final(next+2);", "case 0x77ac: {\n    next=read32(sp);sp+=2;read16(next);final(next+2);"),
    ("companiontimedprefetch", "case 0xafa6: {\n    idle(2);\n    read16(0xafaa);", "case 0xafa6: {\n    idle(2);\n    read16(0xafac);"),
]:
    cases.append((name,"src/main/engine/omaincompanionschedule.inc",old,new,
                  "tests/oracle_caller.cpp","caller_companion_directed",
                  ["src/main/engine/omainirqservice.cpp","src/main/engine/omainirqcallee.cpp",*common]))
# Sub wait timing and computed cross-CPU publication signal values.
for name,old,new in [
    ("subpubzero", "case 0x55a:branch(bool(s.sr&8)==bool(s.sr&2),0x556);", "case 0x55a:branch(!(s.sr&4)&&bool(s.sr&8)==bool(s.sr&2),0x556);"),
    ("subpubrelease", "v|0x200,0xff00", "v|0x400,0xff00"),
    ("subpubprefetch", "case 0x452:case 0x556:read(pc+4);", "case 0x452:case 0x556:read(pc+6);"),
    ("subpubreturn", "s.sp+=4;s.pc=", "s.sp+=2;s.pc="),
    ("subpubcounter", "operand-1", "operand-2"),
    ("subpubrearm", "b.write16(0x2a3c00,1);", "b.write16(0x2a3c00,2);"),
]:
    cases.append((name,"src/main/engine/osubpublication.cpp",old,new,
                  "tests/oracle_subpublication.cpp","subpublication_directed",common))
# Object initializer checks now execute through the full production router.
cases = [(name,source,old,new,test,capture,handler_dependencies(source) if test=="tests/oracle_objects.cpp" else dependencies)
         for name,source,old,new,test,capture,dependencies in cases]

if len(sys.argv)>1:
    requested=set(sys.argv[1:]); known={c[0] for c in cases}
    if requested-known: raise RuntimeError("unknown mutations: "+", ".join(sorted(requested-known)))
    cases=[c for c in cases if c[0] in requested]

for name, source, old, new, test, capture, dependencies in cases:
    text = Path(source).read_text()
    if text.count(old) != 1:
        raise RuntimeError(f"{name}: mutation anchor changed; inspect the port")
    variant, binary, log_path = root / (name + ".cpp"), root / name, root / (name + ".log")
    variant.write_text(text.replace(old, new))
    if source.endswith((".hpp", ".inc")):
        include = root / name / Path(source).parent.name
        # Keep the executable separate from the shadow include directory.
        binary = root / (name + "_test")
        include.mkdir(parents=True, exist_ok=True)
        (include / Path(source).name).write_text(text.replace(old, new))
        command = ["-I" + str(include.parent), "-Isrc/main", test, *dependencies]
    else:
        command = ["-Isrc/main", test, str(variant), *dependencies]
    subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-O2",
                    *command, "-o", str(binary)], check=True)
    with log_path.open("w") as log:
        args = [str(binary), "build/oracle/" + capture + (".bin.visibility" if name.startswith("visibility") else ".bin")]
        if capture == "caller_successors_directed": args += ["--require-taken"]
        if name.startswith("irq4return"): args += ["build/oracle/" + capture + ".bin.returns"]
        if test == "tests/oracle_video.cpp": args += ["0", "build/oracle/paired_road_requests.bin"]
        result = subprocess.run(args,
                                stdout=log, stderr=subprocess.STDOUT)
    summary = [line for line in log_path.read_text().splitlines() if " identical, " in line and " different," in line]
    differences = [line for line in summary if ", 0 different," not in line]
    # A changed branch or completion time can leave the bounded transcript
    # before aggregate summaries are printed. Keep the explicit failed bus/
    # state assertions as differential evidence, never a bare generic error.
    assertion_differences = [line for line in log_path.read_text().splitlines()
                            if line.startswith(("Caller pc=", "Caller record pc=", "Caller chain pc=", "Linked service mismatch", "Linked IRQ4 bus pc=", "Linked IRQ4 return mismatch", "IRQ4 bus pc=", "Leaf "))]
    schedule_errors = [line for line in log_path.read_text().splitlines()
                       if any(message in line for message in [
                           "unrecorded IRQ inside sampling gap", "missing scheduled poll",
                           "scheduled wait overshot caller boundary", "wait left bounded graph",
                           "RTE selection mismatch", "missing physical bus observation at PC",
                           "missing RTE physical selection phase", "missing IRQ4 RTE visibility", "missing internal completion poll", "caller has no selected IRQ"])]
    if result.returncode != 1 or not (differences or assertion_differences or schedule_errors):
        raise RuntimeError(f"{name}: mutation did not produce a differential failure; see {log_path}")
    print(f"{name}: rejected — {(differences or assertion_differences or schedule_errors)[-1]}", flush=True)
