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
service_dependencies = common
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

# Optional names rerun changed fixtures without rebuilding unaffected mutations.
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
    if source.endswith(".hpp"):
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
        result = subprocess.run([str(binary), "build/oracle/" + capture + ".bin"],
                                stdout=log, stderr=subprocess.STDOUT)
    summary = [line for line in log_path.read_text().splitlines() if " identical, " in line and " different," in line]
    differences = [line for line in summary if ", 0 different," not in line]
    if result.returncode != 1 or not differences:
        raise RuntimeError(f"{name}: mutation did not produce a differential failure; see {log_path}")
    print(f"{name}: rejected — {differences[-1]}", flush=True)
