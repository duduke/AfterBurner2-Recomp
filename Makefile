.DEFAULT_GOAL := all
CXX ?= clang++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O2
INCLUDES = -Isrc/main $(shell pkg-config --cflags sdl2 2>/dev/null || echo -I/opt/homebrew/include -I/opt/homebrew/include/SDL2)
LIBS = $(shell pkg-config --libs sdl2 2>/dev/null || echo -L/opt/homebrew/lib -lSDL2main -lSDL2 -framework Cocoa)

SRCS = \
	src/main/main.cpp \
	src/main/romloader.cpp \
	src/main/roms.cpp \
	src/main/hwvideo/hwroad.cpp \
	src/main/hwvideo/hwsprites.cpp \
	src/main/hwvideo/hwtiles.cpp \
	src/main/hwvideo/hwpalette.cpp \
	src/main/hwvideo/screen.cpp \
	src/main/hwaudio/arcade_audio.cpp \
	src/third_party/ymfm/ymfm_opm.cpp \
	src/main/hwaudio/xboard_sound_cpu.cpp \
	src/main/hwaudio/soundchip.cpp \
	src/main/hwaudio/segapcm.cpp \
	src/main/hwaudio/ym2151.cpp \
	src/main/engine/ohud.cpp \
	src/main/engine/osprites.cpp \
	src/main/engine/olevelobjs.cpp \
	src/main/engine/oengine.cpp

# Verified road/scenery and shared bus; ADC awaits the flight/input port.
SRCS += src/main/engine/omainhost.cpp src/main/engine/omainirqservice.cpp src/main/engine/omainirqcallee.cpp src/main/engine/omaininput.cpp src/main/engine/omainplayer.cpp src/main/engine/omaindamage.cpp src/main/engine/omainsequence.cpp src/main/engine/omainworld.cpp src/main/engine/omainweapons.cpp src/main/engine/omainobjects.cpp src/main/engine/omainsound.cpp src/main/engine/omainpalette.cpp src/main/engine/omainframe.cpp src/main/engine/omainsprites.cpp src/main/engine/osubsprites.cpp src/main/engine/subbus.cpp src/main/engine/mainbus.cpp src/main/engine/omainadc.cpp src/main/engine/oscenery.cpp src/main/engine/ostagesetup.cpp src/main/engine/osubroad.cpp

SRCS += src/main/engine/osubirq.cpp src/main/engine/osubprogress.cpp src/main/engine/osubpublication.cpp

OBJS = $(SRCS:.cpp=.o)
DEPS = $(OBJS:.o=.d)
-include $(DEPS)
TARGET = afterburner2

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -pthread -o $(TARGET) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

.PHONY: all clean

# ROM-free checks exercise the real descriptor/scaler and engine code.
TEST_SRCS = tests/engine_regression.cpp $(filter src/main/engine/% src/main/hwvideo/%,$(SRCS))

# Dependency files include transitive headers and generated .inc files.
CHECK_SRCS = $(sort $(TEST_SRCS) tests/mainbus_regression.cpp tests/xboard_timeline_regression.cpp tests/xboard_irq_lines_regression.cpp tests/cpu_continuation_regression.cpp tests/sound_cpu_regression.cpp tests/stage1_integration.cpp tests/whole_host_integration.cpp $(filter-out src/main/main.cpp,$(SRCS)))
CHECK_OBJS = $(addprefix build/check_obj/,$(CHECK_SRCS:.cpp=.o))
check_objs = $(addprefix build/check_obj/,$(1:.cpp=.o))
-include $(CHECK_OBJS:.o=.d)

# Invalidate these objects when compiler/options change; preserve stamp mtime
# when they do not, so repeated checks only rerun the executable.
CHECK_CONFIG_CHANGED := $(shell printf '%s\n' '$(CXX)' '$(CXXFLAGS)' '$(INCLUDES)' | cmp -s - build/check_obj/config || echo FORCE_CHECK_REBUILD)
build/check_obj/config: FORCE_CHECK_CONFIG
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(CXX)' '$(CXXFLAGS)' '$(INCLUDES)' > $@.tmp
	@if cmp -s $@.tmp $@; then rm $@.tmp; else rm -f $(CHECK_OBJS); mv $@.tmp $@; fi

# A phony prerequisite also catches option changes within one clock tick on
# older make versions with second-resolution timestamp comparisons.
build/check_obj/%.o: %.cpp build/check_obj/config $(CHECK_CONFIG_CHANGED)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -pthread -MMD -MP -c $< -o $@

build/engine_regression: $(call check_objs,$(TEST_SRCS))
	$(CXX) $(CXXFLAGS) -pthread $^ -o $@
build/mainbus_regression: $(call check_objs,tests/mainbus_regression.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp)
	$(CXX) $(CXXFLAGS) -pthread $^ -o $@
build/xboard_timeline_regression: $(call check_objs,tests/xboard_timeline_regression.cpp)
	$(CXX) $(CXXFLAGS) -pthread $^ -o $@
build/xboard_irq_lines_regression: $(call check_objs,tests/xboard_irq_lines_regression.cpp)
	$(CXX) $(CXXFLAGS) -pthread $^ -o $@
build/cpu_continuation_regression: $(call check_objs,tests/cpu_continuation_regression.cpp)
	$(CXX) $(CXXFLAGS) -pthread $^ -o $@
build/sound_cpu_regression: $(call check_objs,tests/sound_cpu_regression.cpp src/main/hwaudio/xboard_sound_cpu.cpp)
	$(CXX) $(CXXFLAGS) -pthread $^ -o $@

test: build/engine_regression build/mainbus_regression build/xboard_timeline_regression build/xboard_irq_lines_regression build/cpu_continuation_regression build/sound_cpu_regression
	./build/engine_regression
	./build/mainbus_regression
	./build/xboard_timeline_regression
	./build/xboard_irq_lines_regression
	./build/cpu_continuation_regression
	./build/sound_cpu_regression

clean-checks:
	rm -rf build/check_obj
	rm -f build/engine_regression build/mainbus_regression build/xboard_timeline_regression build/xboard_irq_lines_regression build/cpu_continuation_regression build/sound_cpu_regression build/stage1_integration

.PHONY: test clean-checks FORCE_CHECK_CONFIG FORCE_CHECK_REBUILD

# Optional visual check; requires the user's ROMs in roms/.
render-scene:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tools/render_scene.cpp src/main/romloader.cpp src/main/roms.cpp $(filter src/main/engine/% src/main/hwvideo/%,$(SRCS)) -o build/render_scene

.PHONY: render-scene

# Optional integration suite: reads the user's CRC-verified ROMs.
build/stage1_integration: $(call check_objs,tests/stage1_integration.cpp $(filter-out src/main/main.cpp,$(SRCS)))
	$(CXX) $(CXXFLAGS) -pthread $^ -o $@

verify-stage1: build/stage1_integration
	./build/stage1_integration

.PHONY: verify-stage1

# Oracle tests: ported arcade routines vs the real code running in MAME.
# Captures come from your own ROM set and stay local in build/oracle/.
oracle-capture:
	@mkdir -p build/oracle
	tools/oracle/capture.sh tools/oracle/capture_subsprites.lua build/oracle/subsprites.bin 1200 120
	tools/oracle/capture.sh tools/oracle/capture_subroad.lua build/oracle/subroad.bin 1800 120
	tools/oracle/capture.sh tools/oracle/capture_frames.lua build/oracle/frames.bin 300 120
	AB2_ORACLE_NO_SPRITES=1 tools/oracle/capture.sh tools/oracle/capture_frames.lua build/oracle/frames_nosprites.bin 100 60
	$(MAKE) oracle-capture-objects
	$(MAKE) oracle-capture-handlers
	$(MAKE) oracle-capture-frame-sprites
	$(MAKE) oracle-capture-main
	$(MAKE) oracle-capture-mainirq
	$(MAKE) oracle-capture-scenery
	$(MAKE) oracle-capture-road-setup
	$(MAKE) oracle-capture-host-objects
	$(MAKE) oracle-capture-scheduling-returns
	$(MAKE) oracle-capture-caller-words
	$(MAKE) oracle-capture-caller-successors
	$(MAKE) oracle-capture-caller-closure
	$(MAKE) oracle-capture-caller-follower
	$(MAKE) oracle-capture-caller-ready
	$(MAKE) oracle-capture-caller-allirq
	$(MAKE) oracle-capture-caller-soundproducer
	$(MAKE) oracle-capture-caller-input
	$(MAKE) oracle-capture-caller-companion
	$(MAKE) oracle-capture-caller-flight
	$(MAKE) oracle-capture-subpublication-short
	$(MAKE) oracle-capture-subpublication-directed
	$(MAKE) oracle-capture-subpublication-full
	$(MAKE) oracle-capture-subprogress-short
	$(MAKE) oracle-capture-subprogress-directed
	$(MAKE) oracle-capture-subprogress-full

# 3d foundations only; leaf and directed calls do not prove player gameplay.
oracle-capture-objects:
	@mkdir -p build/oracle
	tools/oracle/capture.sh tools/oracle/capture_objects.lua build/oracle/objects.bin 120 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=540 tools/oracle/capture.sh tools/oracle/capture_objects.lua build/oracle/objects_gameplay.bin 240 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_OBJECT_DIRECTED=1 AB2_ORACLE_SKIP=540 tools/oracle/capture.sh tools/oracle/capture_objects.lua build/oracle/objects_directed.bin 120 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_OBJECT_LEAVES=1 tools/oracle/capture.sh tools/oracle/capture_objects.lua build/oracle/object_leaves.bin 600 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_OBJECT_LEAVES=1 AB2_ORACLE_OBJECT_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_objects.lua build/oracle/object_leaves_directed.bin 600 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_OBJECT_LEAVES=1 AB2_ORACLE_OBJECT_DIRECTED=1 AB2_ORACLE_OBJECT_CALLS=1 tools/oracle/capture.sh tools/oracle/capture_objects.lua build/oracle/object_calls_directed.bin 168 30

oracle-capture-frame-sprites:
	@mkdir -p build/oracle
	AB2_ORACLE_SPRITE_INIT=1 AB2_ORACLE_SKIP=500 tools/oracle/capture.sh tools/oracle/capture_mainsprites.lua build/oracle/spriteinit.bin 1 30
	AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_palette.lua build/oracle/palette.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_PALETTE_DIRECTED=1 AB2_ORACLE_SKIP=500 tools/oracle/capture.sh tools/oracle/capture_palette.lua build/oracle/palette_directed.bin 1600 45
	AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_mainsprites.lua build/oracle/mainsprites.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SPRITE_DIRECTED=1 AB2_ORACLE_SKIP=500 tools/oracle/capture.sh tools/oracle/capture_mainsprites.lua build/oracle/mainsprites_directed.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=500 tools/oracle/capture.sh tools/oracle/capture_mainframe.lua build/oracle/mainframe.bin 120 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_FRAME_DIRECTED=1 AB2_ORACLE_SKIP=500 tools/oracle/capture.sh tools/oracle/capture_mainframe.lua build/oracle/mainframe_directed.bin 120 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SPRITES_DIRECTED=1 AB2_ORACLE_SKIP=500 tools/oracle/capture.sh tools/oracle/capture_subsprites_directed.lua build/oracle/subsprites_directed.bin 240 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=540 AB2_ORACLE_STRIDE=1 tools/oracle/capture.sh tools/oracle/capture_pipeline.lua build/oracle/pipeline.bin 120 30

oracle-capture-scenery:
	@mkdir -p build/oracle
	tools/oracle/capture.sh tools/oracle/capture_scenery.lua build/oracle/scenery.bin 2000 90
	AB2_ORACLE_STRIDE=8 tools/oracle/capture.sh tools/oracle/capture_scenery.lua build/oracle/scenery_extended.bin 1200 240
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=420 tools/oracle/capture.sh tools/oracle/capture_scenery.lua build/oracle/scenery_gameplay.bin 1200 60

oracle-capture-road-setup:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_stagesetup.lua build/oracle/stagesetup.bin 0 50
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SETUP_CONTROL=1 tools/oracle/capture.sh tools/oracle/capture_stagesetup.lua build/oracle/stagesetup_control.bin 0 50
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SETUP_ALT=1 tools/oracle/capture.sh tools/oracle/capture_stagesetup.lua build/oracle/stagesetup_alternate.bin 0 50
	AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_roadinit.lua build/oracle/roadinit.bin 0 30
	AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_roadrequests.lua build/oracle/roadrequests.bin 0 30
	AB2_ORACLE_SPECIAL=1 AB2_ORACLE_SKIP=500 tools/oracle/capture.sh tools/oracle/capture_subroad.lua build/oracle/subroad_special.bin 600 45

oracle-capture-main:
	@mkdir -p build/oracle
	tools/oracle/capture.sh tools/oracle/capture_compare.lua build/oracle/compare.bin 300 30
	tools/oracle/capture.sh tools/oracle/capture_main.lua build/oracle/main.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_main.lua build/oracle/main_gameplay.bin 1800 60
	$(MAKE) oracle-capture-gameplay

oracle-capture-gameplay:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=420 tools/oracle/capture.sh tools/oracle/capture_subsprites.lua build/oracle/subsprites_gameplay.bin 1200 90
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=420 tools/oracle/capture.sh tools/oracle/capture_subroad.lua build/oracle/subroad_gameplay.bin 1800 90
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=540 AB2_ORACLE_STRIDE=3 tools/oracle/capture.sh tools/oracle/capture_frames.lua build/oracle/frames_gameplay.bin 300 60


# Independent handler execution, including chip state and ordered RAM writes.
HANDLER_SRCS = src/main/engine/omainsprites.cpp src/main/engine/omainhost.cpp src/main/engine/omainobjects.cpp src/main/engine/omaininput.cpp src/main/engine/omainplayer.cpp src/main/engine/omaindamage.cpp src/main/engine/omainsequence.cpp src/main/engine/omainworld.cpp src/main/engine/omainweapons.cpp src/main/engine/omainsound.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS)
HANDLER_CAPTURES = host_entries_directed input input_gameplay input_directed input_endpoints buttons_endpoints player_init player player_directed player_parts player_parts_gameplay player_parts_directed reload reload_directed weapon_control weapon_control_directed world world_directed buttons buttons_directed projectiles projectiles_directed missiles missiles_directed effects_directed collision_directed enemy_weapons_directed enemy_parts_directed enemy_effects enemy_effects_gameplay damage_directed damage_gameplay companion_directed companion_gameplay sequence_directed sequence_gameplay enemy_tracking_directed enemy_tracking_gameplay world_graph_directed refuel_parts_directed

oracle-capture-handlers: oracle-capture-damage oracle-capture-companion oracle-capture-sequence oracle-capture-enemy-tracking oracle-capture-world-graph oracle-capture-refuel-parts
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/input.bin 240 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=540 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/input_gameplay.bin 360 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=540 AB2_ORACLE_HANDLER_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/input_directed.bin 128 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER=9810 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/player_init.bin 1 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=540 AB2_ORACLE_HANDLER=98c0 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/player.bin 360 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=540 AB2_ORACLE_HANDLER=98c0 AB2_ORACLE_HANDLER_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/player_directed.bin 128 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLERS=9c34,9c9e,ae8c,aee4,af08,af3c,afcc,b05c tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/player_parts.bin 720 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=540 AB2_ORACLE_HANDLERS=af3c,afcc,9c9e tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/player_parts_gameplay.bin 720 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLERS=1653a,1656a,16572,1657a,1678e,9c34,9c9e,ae8c,aee4,af08,af3c,afcc,b05c tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/player_parts_directed.bin 260 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_LEAF=1 AB2_ORACLE_HANDLER=16c8a AB2_ORACLE_SKIP=540 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/reload.bin 240 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLERS=16c8a,cfc4,d062 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/reload_directed.bin 210 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLERS=9d2c,9d56 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/weapon_control.bin 480 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLERS=9d2c,9d56,9df8,9e50,9e82,9ed8,9f08,9f22,9f44,9f5e,9626,964c tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/weapon_control_directed.bin 384 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLERS=14976 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/buttons_directed.bin 64 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_LEAF=1 AB2_ORACLE_HANDLER=14976 AB2_ORACLE_SKIP=540 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/buttons.bin 240 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLERS=88c2,8af2,91c0,9556 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/world.bin 480 90
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLERS=88c2,8af2,91c0,9556 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/world_directed.bin 256 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLERS=9f7c,a030,ac4c tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/projectiles.bin 480 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLER_DELAY=0.002 AB2_ORACLE_HANDLERS=9f7c,a030,ac4c,ae1c,ae2e,ae50 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/projectiles_directed.bin 384 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLERS=a234,a382,a53a,aa32,aaee tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/missiles.bin 720 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLERS=a234,a382,a53a,a9f2,aa32,aaee,ae32 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/missiles_directed.bin 448 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLERS=8d44,8d58,8e18,8ece,8cc8 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/effects_directed.bin 320 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLERS=8cc8 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/collision_directed.bin 64 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLERS=14dc4,14df0,14e14,14e1e,14ff2,14ff4,150c0,1518a,151b8,15200,1528a,152a8,153e0,154b8,1552c,15582,15638,156ce,1578c,157ba,157e8,157f2,1581a,1583e,15842 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/enemy_weapons_directed.bin 1600 90
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLERS=14ec8,14ee2,1500c,15026 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/enemy_parts_directed.bin 256 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLERS=153e0,154b8,1552c,15582,15638,156ce,14ec8,14ee2,1500c,15026,8d44,8d58,8e18,8ece tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/enemy_effects.bin 480 90
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SKIP=540 AB2_ORACLE_HANDLERS=153e0,154b8,1552c,15582,15638,156ce,14ec8,14ee2,1500c,15026,8d44,8d58,8e18,8ece tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/enemy_effects_gameplay.bin 480 90
	tools/oracle/capture.sh tools/oracle/capture_adc_timing.lua build/oracle/adc_timing.bin 900 45
	AB2_ORACLE_ADC_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_adc_timing.lua build/oracle/adc_timing_directed.bin 600 45

# Damage routines plus bounded ordinary gameplay; no host-cadence claim.
oracle-capture-damage:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLER_DELAY=0.002 AB2_ORACLE_HANDLERS=e662,e8d0,e94e,f3e2,f43c,f4ba,f528,f5c0,e744,e7e0,ebd2,ebf6,ec1a,ec3e,ec62,ecac,f58e,f640,f766,f85a,f878,f93e,f95e,f966,f99e,f9ce,f9e2,faa0 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/damage_directed.bin 1792 90
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLERS=e662,e8d0,e94e tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/damage_gameplay.bin 120 120

oracle-capture-companion:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLER_DELAY=0.002 AB2_ORACLE_HANDLERS=e7da,fb08,fb16,fb9c,fbc4,fbd4,fbda,fcb4,fda6,fdfa,ff08,ffbe,ffe6,1003e,100dc,100f4,10148,101e6,101fe,10252,102f2,10310 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/companion_directed.bin 1408 90
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLERS=e7da,fb08,fb16,fb9c,fbc4,fbd4,fbda,fcb4,fda6,fdfa,ff08,ffbe,ffe6,1003e,100dc,100f4,10148,101e6,101fe,10252,102f2,10310 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/companion_gameplay.bin 2400 120

oracle-handlers:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_handlers.cpp $(HANDLER_SRCS) -o build/oracle_handlers
	@for capture in $(HANDLER_CAPTURES); do ./build/oracle_handlers build/oracle/$$capture.bin || exit 1; done
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_adc_timing.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp -o build/oracle_adc_timing
	./build/oracle_adc_timing build/oracle/adc_timing.bin
	./build/oracle_adc_timing build/oracle/adc_timing_directed.bin

ORACLE_SRCS = src/main/romloader.cpp src/main/roms.cpp src/main/engine/subbus.cpp
VIDEO_SRCS = src/main/romloader.cpp src/main/roms.cpp $(filter src/main/hwvideo/%,$(SRCS)) src/main/engine/mainbus.cpp src/main/engine/subbus.cpp src/main/engine/omainsprites.cpp src/main/engine/osubsprites.cpp src/main/engine/osubroad.cpp

oracle-capture-mainirq:
	@mkdir -p build/oracle
	AB2_ORACLE_IRQ=1 tools/oracle/capture.sh tools/oracle/capture_adc_timing.lua build/oracle/mainirq.bin 900 45
	AB2_ORACLE_IRQ=1 AB2_ORACLE_INPUT_ENDPOINTS=1 tools/oracle/capture.sh tools/oracle/capture_adc_timing.lua build/oracle/mainirq_endpoints.bin 900 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_INPUT_ENDPOINTS=1 AB2_ORACLE_SKIP=540 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/input_endpoints.bin 360 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_INPUT_ENDPOINTS=1 AB2_ORACLE_HANDLER_LEAF=1 AB2_ORACLE_HANDLER=14976 AB2_ORACLE_SKIP=540 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/buttons_endpoints.bin 360 45

oracle-mainirq:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_mainirq.cpp src/main/engine/omainadc.cpp src/main/engine/omainsound.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp -o build/oracle_mainirq
	./build/oracle_mainirq build/oracle/mainirq.bin
	./build/oracle_mainirq build/oracle/mainirq_endpoints.bin

oracle-test: oracle-publication-schedule oracle-caller-host oracle-caller-weapon oracle-subirq oracle-bus-arbiter oracle-subprogress oracle-subpublication oracle-caller-companion oracle-caller-flight oracle-caller-input oracle-caller-soundproducer oracle-caller-allirq oracle-caller-ready oracle-caller-follower oracle-caller-closure oracle-caller-successors oracle-caller-words oracle-scheduling-returns oracle-caller-return oracle-caller-service oracle-irq4-service oracle-logic-wait oracle-handlers oracle-mainirq oracle-board oracle-irq-service oracle-host-objects oracle-paired-road
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_objects.cpp $(HANDLER_SRCS) -o build/oracle_objects
	./build/oracle_objects build/oracle/objects.bin
	./build/oracle_objects build/oracle/objects_gameplay.bin
	./build/oracle_objects build/oracle/objects_directed.bin
	./build/oracle_objects build/oracle/object_leaves.bin
	./build/oracle_objects build/oracle/object_leaves_directed.bin
	./build/oracle_objects build/oracle/object_calls_directed.bin
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_palette.cpp src/main/engine/omainpalette.cpp src/main/engine/omainframe.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS) -o build/oracle_palette
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_mainsprites.cpp src/main/engine/omainsprites.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS) -o build/oracle_mainsprites
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_mainframe.cpp src/main/engine/omainframe.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS) -o build/oracle_mainframe
	./build/oracle_palette build/oracle/palette.bin
	./build/oracle_palette build/oracle/palette_directed.bin
	./build/oracle_mainsprites build/oracle/spriteinit.bin
	./build/oracle_mainsprites build/oracle/mainsprites.bin
	./build/oracle_mainsprites build/oracle/mainsprites_directed.bin
	./build/oracle_mainframe build/oracle/mainframe.bin
	./build/oracle_mainframe build/oracle/mainframe_directed.bin
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_subsprites.cpp src/main/engine/osubsprites.cpp $(ORACLE_SRCS) -o build/oracle_subsprites
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_subroad.cpp src/main/engine/osubroad.cpp $(ORACLE_SRCS) -o build/oracle_subroad
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_video.cpp $(VIDEO_SRCS) -o build/oracle_video
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_main.cpp src/main/engine/mainbus.cpp src/main/engine/omainadc.cpp src/main/engine/omainsound.cpp $(ORACLE_SRCS) -o build/oracle_main
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_compare.cpp -o build/oracle_compare
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_scenery.cpp src/main/engine/oscenery.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS) -o build/oracle_scenery
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_stagesetup.cpp src/main/engine/ostagesetup.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS) -o build/oracle_stagesetup
	./build/oracle_stagesetup build/oracle/stagesetup.bin
	./build/oracle_stagesetup build/oracle/stagesetup_alternate.bin
	./build/oracle_stagesetup build/oracle/stagesetup_control.bin
	./build/oracle_subroad build/oracle/roadrequests.bin
	./build/oracle_subroad build/oracle/roadinit.bin
	./build/oracle_subroad build/oracle/subroad_special.bin
	./build/oracle_subsprites build/oracle/subsprites_directed.bin
	./build/oracle_video build/oracle/pipeline.bin
	./build/oracle_subsprites
	./build/oracle_subroad
	./build/oracle_video build/oracle/frames.bin
	./build/oracle_video build/oracle/frames_nosprites.bin
	./build/oracle_compare
	./build/oracle_main
	./build/oracle_main build/oracle/main_gameplay.bin
	./build/oracle_subsprites build/oracle/subsprites_gameplay.bin
	./build/oracle_subroad build/oracle/subroad_gameplay.bin
	./build/oracle_video build/oracle/frames_gameplay.bin
	./build/oracle_scenery
	./build/oracle_scenery build/oracle/scenery_extended.bin
	./build/oracle_scenery build/oracle/scenery_gameplay.bin

.PHONY: oracle-capture-frame-sprites oracle-capture-road-setup oracle-capture oracle-capture-main oracle-capture-gameplay oracle-capture-scenery oracle-test

.PHONY: oracle-capture-objects

.PHONY: oracle-capture-handlers oracle-capture-damage oracle-capture-companion oracle-handlers
.PHONY: oracle-capture-mainirq oracle-mainirq

# Independent carrier controller and paired part graph.
oracle-capture-sequence:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLER_DELAY=0 AB2_ORACLE_HANDLERS=10348,103c6,103ea,1069c,106ba,106d8,106f6,1071c,10782,107b6,10822,10848,108ac,108d6,10936,10946,109de,10a46,10b02,10b82,10ba8,10bde,10c04,10ca2,10cac,10d40,10d46,10df4,10e2a,10e3e,10e4c,10e6a,10eb0,10ed8,10fbc tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/sequence_directed.bin 2240 150
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLERS=103c6,103ea,10822,108ac,108d6,10936,10b02,10c04,10ca2,10cac,10d40 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/sequence_gameplay.bin 2400 120

.PHONY: oracle-capture-sequence

oracle-capture-enemy-tracking:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLER_DELAY=0 AB2_ORACLE_HANDLERS=1586c,158a8,1592a,15968,15a6a,15ab6,15b0a,15b6e,15bfe,15c62,15c7a,15c94,15c9a,15ca2,15caa,15cb0,15d26,15d36,15d9e,15e6c,15f48,15f74,16038 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/enemy_tracking_directed.bin 1472 120
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLERS=1586c,158a8,1592a,15968,15a6a,15ab6,15bfe,15d9e,15e6c tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/enemy_tracking_gameplay.bin 480 120

.PHONY: oracle-capture-enemy-tracking

oracle-capture-world-graph:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLER_BOUNDARIES=1 AB2_ORACLE_HANDLER_DELAY=0 AB2_ORACLE_HANDLERS=8946,8c1c,9046,90ac,9366,93a0,94b8 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/world_graph_directed.bin 448 90

.PHONY: oracle-capture-world-graph

oracle-capture-refuel-parts:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLER_DELAY=0 AB2_ORACLE_HANDLERS=161b8,163c2,163ec,1641c,164da,169d8 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/refuel_parts_directed.bin 384 90

.PHONY: oracle-capture-refuel-parts

# Physical scanline/ADC timeline: reset phase is asserted, edges are generated.
oracle-capture-board:
	@mkdir -p build/oracle
	AB2_ORACLE_BOARD=1 tools/oracle/capture.sh tools/oracle/capture_adc_timing.lua build/oracle/board.bin 900 45
	AB2_ORACLE_BOARD=1 AB2_ORACLE_INPUT_ENDPOINTS=1 tools/oracle/capture.sh tools/oracle/capture_adc_timing.lua build/oracle/board_endpoints.bin 900 45
	AB2_ORACLE_BOARD=1 AB2_ORACLE_ADC_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_adc_timing.lua build/oracle/board_directed.bin 600 45

oracle-board:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_board.cpp -o build/oracle_board
	./build/oracle_board build/oracle/board.bin
	./build/oracle_board build/oracle/board_endpoints.bin
	./build/oracle_board build/oracle/board_directed.bin

.PHONY: oracle-board oracle-capture-board

# IRQ2/IRQ6 service: predict bus and return timestamps from CPU cycles.
oracle-capture-service:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_irq_service.lua build/oracle/service.bin 900 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_INPUT_ENDPOINTS=1 tools/oracle/capture.sh tools/oracle/capture_irq_service.lua build/oracle/service_endpoints.bin 900 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_SERVICE_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_irq_service.lua build/oracle/service_directed.bin 600 30

oracle-irq-service:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_irq_service.cpp src/main/engine/omainirqservice.cpp src/main/engine/omainirqcallee.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS) -o build/oracle_irq_service
	./build/oracle_irq_service build/oracle/service.bin
	./build/oracle_irq_service build/oracle/service_endpoints.bin
	./build/oracle_irq_service build/oracle/service_directed.bin

.PHONY: oracle-irq-service oracle-capture-service

# Directed IPL7 dispatcher + verified handlers on live gameplay records.
# Unknown 3e callers remain external inputs; this is not natural host cadence.
oracle-capture-host-objects:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HANDLER_CALLS=1 AB2_ORACLE_HANDLERS=9d34,b628 tools/oracle/capture.sh tools/oracle/capture_handlers.lua build/oracle/host_entries_directed.bin 128 45
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_HOST=1 AB2_ORACLE_SKIP=540 tools/oracle/capture.sh tools/oracle/capture_objects.lua build/oracle/host_objects.bin 240 60
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_INPUT_ENDPOINTS=1 AB2_ORACLE_HOST=1 AB2_ORACLE_SKIP=540 tools/oracle/capture.sh tools/oracle/capture_objects.lua build/oracle/host_objects_endpoints.bin 240 60

oracle-host-objects:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_host_objects.cpp $(HANDLER_SRCS) -o build/oracle_host_objects
	./build/oracle_host_objects build/oracle/host_objects.bin
	./build/oracle_host_objects build/oracle/host_objects_endpoints.bin

.PHONY: oracle-capture-host-objects oracle-host-objects

# Natural dispatcher IRQ composition; interrupted CPU boundaries remain inputs.
INTERLEAVE_SRCS = $(HANDLER_SRCS) src/main/engine/omainadc.cpp src/main/engine/omainframe.cpp src/main/engine/omainpalette.cpp src/main/engine/oscenery.cpp
oracle-host-interleave:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_host_interleave.cpp $(INTERLEAVE_SRCS) -o build/oracle_host_interleave
	./build/oracle_host_interleave build/oracle/host_prefix.bin build/oracle/host_requests.bin

oracle-capture-host-interleave:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_INTERLEAVE=1 AB2_ORACLE_INTERLEAVE_PREFIX=1 AB2_ORACLE_SKIP=540 tools/oracle/capture.sh tools/oracle/capture_objects.lua build/oracle/host_prefix.bin 120 60

.PHONY: oracle-host-interleave oracle-capture-host-interleave

# Linked natural producers -> sub transform -> both display lists -> RGB.
oracle-capture-paired:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_INTERLEAVE=1 AB2_ORACLE_INTERLEAVE_PREFIX=1 AB2_ORACLE_PUBLICATION=1 AB2_ORACLE_PAIRED=1 AB2_ORACLE_OBJECT_OUT=build/oracle/paired_objects.bin AB2_ORACLE_SOUND_OUT=build/oracle/paired_sound.bin AB2_ORACLE_FRAME_OUT=build/oracle/paired_frame.bin AB2_ORACLE_SKIP=1020 AB2_ORACLE_STRIDE=1 tools/oracle/capture.sh tools/oracle/capture_paired.lua build/oracle/paired.bin 240 60

oracle-paired: oracle-host-interleave oracle-frame-host oracle-sound-stream oracle-host-publication
	./build/oracle_host_interleave build/oracle/paired_objects.bin build/oracle/paired_requests.bin
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_video.cpp $(VIDEO_SRCS) -o build/oracle_video
	./build/oracle_video build/oracle/paired.bin 0 build/oracle/paired_requests.bin

.PHONY: oracle-capture-paired oracle-paired

# Actual road consumer/flip boundaries paired with computed main targets.
oracle-capture-paired-road:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_INTERLEAVE=1 AB2_ORACLE_INTERLEAVE_PREFIX=1 AB2_ORACLE_PUBLICATION=1 AB2_ORACLE_PAIRED=1 AB2_ORACLE_PAIRED_ROAD=1 AB2_ORACLE_OBJECT_OUT=build/oracle/paired_road_objects.bin AB2_ORACLE_SOUND_OUT=build/oracle/paired_road_sound.bin AB2_ORACLE_FRAME_OUT=build/oracle/paired_road_frame.bin AB2_ORACLE_SKIP=1020 AB2_ORACLE_STRIDE=1 tools/oracle/capture.sh tools/oracle/capture_paired.lua build/oracle/paired_road.bin 240 60

oracle-paired-road: oracle-paired
	./build/oracle_host_interleave build/oracle/paired_road_objects.bin build/oracle/paired_road_requests.bin
	./build/oracle_frame_host build/oracle/paired_road_frame.bin
	./build/oracle_sound_stream build/oracle/paired_road_sound.bin
	./build/oracle_video build/oracle/paired_road.bin 0 build/oracle/paired_road_requests.bin

.PHONY: oracle-capture-paired-road oracle-paired-road

oracle-frame-host:
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_frame_host.cpp $(INTERLEAVE_SRCS) -o build/oracle_frame_host
	./build/oracle_frame_host build/oracle/paired_frame.bin

oracle-sound-stream:
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_sound_stream.cpp src/main/engine/omainsound.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp -o build/oracle_sound_stream
	./build/oracle_sound_stream build/oracle/paired_sound.bin

.PHONY: oracle-frame-host oracle-sound-stream

# Natural complete dispatch, including the publication wait and IRQ4 progress.
oracle-capture-host-publication:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_INTERLEAVE=1 AB2_ORACLE_INTERLEAVE_PREFIX=1 AB2_ORACLE_PUBLICATION=1 AB2_ORACLE_SKIP=1020 tools/oracle/capture.sh tools/oracle/capture_objects.lua build/oracle/host_publication.bin 120 60

oracle-host-publication: oracle-host-interleave
	./build/oracle_host_interleave build/oracle/host_publication.bin

.PHONY: oracle-capture-host-publication oracle-host-publication

# IRQ4 envelope, selected timed callees and uninterrupted re-arm steps.
oracle-capture-irq4-service:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_service.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_IRQ4_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_service_directed.bin 900 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_IRQ4_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_service_edges.bin 120 15
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_IRQ4_DIRECTED=1 AB2_ORACLE_IRQ4_ROAD_ENDPOINTS=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_service_road_endpoints.bin 120 15
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_IRQ4_DIRECTED=1 AB2_ORACLE_IRQ4_CYCLE_ENDPOINTS=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_service_cycle_endpoints.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_IRQ4_DIRECTED=1 AB2_ORACLE_IRQ4_CALLEE_ENDPOINTS=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_service_callee_endpoints.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_IRQ4_DIRECTED=1 AB2_ORACLE_IRQ4_SCENERY_ENDPOINTS=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_service_scenery_endpoints.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_IRQ4_DIRECTED=1 AB2_ORACLE_IRQ4_RTS_CALLEES=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_service_rts_callees.bin 120 15
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_IRQ4_DIRECTED=1 AB2_ORACLE_IRQ4_GAMEFLOW_ENDPOINTS=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_service_gameflow_endpoints.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_IRQ4_DIRECTED=1 AB2_ORACLE_IRQ4_GAMEFLOW_ACTIONS=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_service_gameflow_actions.bin 600 30

oracle-irq4-service:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_irq4_service.cpp src/main/engine/omainirqservice.cpp src/main/engine/omainirqcallee.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS) -o build/oracle_irq4_service
	./build/oracle_irq4_service build/oracle/irq4_service.bin
	./build/oracle_irq4_service build/oracle/irq4_service_directed.bin
	./build/oracle_irq4_service build/oracle/irq4_service_edges.bin
	./build/oracle_irq4_service build/oracle/irq4_service_road_endpoints.bin
	./build/oracle_irq4_service build/oracle/irq4_service_cycle_endpoints.bin
	./build/oracle_irq4_service build/oracle/irq4_service_callee_endpoints.bin
	./build/oracle_irq4_service build/oracle/irq4_service_scenery_endpoints.bin
	./build/oracle_irq4_service build/oracle/irq4_service_rts_callees.bin --require-rts
	./build/oracle_irq4_service build/oracle/irq4_service_gameflow_endpoints.bin --require-gameflow
	./build/oracle_irq4_service build/oracle/irq4_service_gameflow_actions.bin --require-actions

oracle-capture-logic-wait:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_logic_wait.lua build/oracle/logic_wait.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_WAIT_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_logic_wait.lua build/oracle/logic_wait_directed.bin 120 15

oracle-logic-wait:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_logic_wait.cpp src/main/engine/omainirqservice.cpp src/main/engine/omainirqcallee.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS) -o build/oracle_logic_wait
	./build/oracle_logic_wait build/oracle/logic_wait.bin
	./build/oracle_logic_wait build/oracle/logic_wait_directed.bin

.PHONY: oracle-capture-irq4-service oracle-irq4-service oracle-capture-logic-wait oracle-logic-wait

# Bounded wait/dispatcher/publication instruction stream and IRQ entry.
oracle-capture-caller:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_directed.bin 600 30

# Incremental caller build: shared objects and generated include dependencies.
CALLER_SRCS = tests/oracle_caller.cpp src/main/engine/omainirqservice.cpp src/main/engine/omainirqcallee.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS)
CALLER_OBJS = $(addprefix build/oracle_obj/,$(CALLER_SRCS:.cpp=.o))
-include $(CALLER_OBJS:.o=.d)
build/oracle_obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Isrc/main -MMD -MP -c $< -o $@

build/oracle_caller: $(CALLER_OBJS)
	$(CXX) $(CXXFLAGS) $(CALLER_OBJS) -o $@

oracle-caller: oracle-cpu-visibility build/oracle_caller
	./build/oracle_caller build/oracle/caller.bin
	./build/oracle_caller build/oracle/caller_directed.bin

.PHONY: oracle-capture-caller oracle-caller

# CPU bus-phase delivery, independent physical IPL/ADC assertions.
oracle-cpu-visibility:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_cpu_visibility.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp -o build/oracle_cpu_visibility
	./build/oracle_cpu_visibility build/oracle/caller.bin.visibility
	./build/oracle_cpu_visibility build/oracle/caller_directed.bin.visibility

.PHONY: oracle-cpu-visibility

# Production ADC/mux endpoints used by OEngine; CPU bus times remain supplied.
oracle-io: oracle-mainirq
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_adc_timing.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp -o build/oracle_adc_timing
	./build/oracle_adc_timing build/oracle/adc_timing.bin
	./build/oracle_adc_timing build/oracle/adc_timing_directed.bin

.PHONY: oracle-io

# Computed caller entry -> bounded IRQ2/IRQ6 -> RTE -> caller resumption.
# IRQ4 and shared input producers remain explicit boundaries.
oracle-capture-caller-service:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_service.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_service_edges.bin 120 15

oracle-caller-service: oracle-caller
	./build/oracle_caller build/oracle/caller_service.bin
	./build/oracle_caller build/oracle/caller_service_edges.bin

.PHONY: oracle-capture-caller-service oracle-caller-service

# RTE mask restoration and physical IPL selection, including immediate entry.
oracle-capture-caller-return:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_return.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_return_edges.bin 120 15

oracle-caller-return: oracle-caller
	./build/oracle_caller build/oracle/caller_return.bin
	./build/oracle_caller build/oracle/caller_return_edges.bin

.PHONY: oracle-capture-caller-return oracle-caller-return

# IRQ4 RTE stack/mask selection and the six natural returned-instruction PCs.
oracle-capture-scheduling-returns:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_steps.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_IRQ4_RETURN=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_return.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_IRQ4_RETURN=1 AB2_ORACLE_IRQ4_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_irq4_service.lua build/oracle/irq4_return_edges.bin 120 15

oracle-scheduling-returns: oracle-caller oracle-irq4-service
	./build/oracle_caller build/oracle/caller_steps.bin
	./build/oracle_irq4_service build/oracle/irq4_return.bin build/oracle/irq4_return.bin.returns
	./build/oracle_irq4_service build/oracle/irq4_return_edges.bin build/oracle/irq4_return_edges.bin.returns

.PHONY: oracle-capture-scheduling-returns oracle-scheduling-returns

# The fourteen CALL5 post-RTE boundaries: $8AF8 nine times, $B62E five.
oracle-capture-caller-words:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_words.bin 600 30

oracle-caller-words: oracle-caller
	./build/oracle_caller build/oracle/caller_words.bin build/oracle/caller_steps.bin

.PHONY: oracle-capture-caller-words oracle-caller-words

# Ten CALL6 successors: $8AFE twice and $B632 eight times; taken BGE fixtures.
oracle-capture-caller-successors:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_successors.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_SUCCESSOR_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_successors_directed.bin 120 15

oracle-caller-successors: oracle-caller
	./build/oracle_caller build/oracle/caller_successors.bin build/oracle/caller_words.bin
	./build/oracle_caller build/oracle/caller_successors_directed.bin --require-taken

.PHONY: oracle-capture-caller-successors oracle-caller-successors

# Close every returned instruction exposed by the expanded projection and
# publication graphs; true instruction snapshots include internal ALU tails.
oracle-capture-caller-closure:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_closure.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_SUCCESSOR_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_closure_directed.bin 120 15

oracle-caller-closure: oracle-caller
	./build/oracle_caller build/oracle/caller_closure.bin build/oracle/caller_successors.bin --require-closed
	./build/oracle_caller build/oracle/caller_closure_directed.bin --require-taken --require-closed

.PHONY: oracle-capture-caller-closure oracle-caller-closure

# Timed compound follower through computed sprite producer and return.
oracle-capture-caller-follower:
	mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_FOLLOWER=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_follower.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_FOLLOWER_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_follower_directed.bin 120 15

oracle-caller-follower: oracle-caller
	./build/oracle_caller build/oracle/caller_follower.bin --require-closed
	./build/oracle_caller build/oracle/caller_follower_directed.bin --require-follower --require-closed

.PHONY: oracle-capture-caller-follower oracle-caller-follower

# Closed ready-bit, world initializer and publication-initializer caller graphs.
oracle-capture-caller-ready:
	mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_ready.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_READY_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_ready_directed.bin 120 15
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_WORLDINIT_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_worldinit_directed.bin 120 15

oracle-caller-ready: oracle-caller
	./build/oracle_caller build/oracle/caller_ready.bin --require-ready-path --require-worldinit --require-publication-init --require-closed
	./build/oracle_caller build/oracle/caller_ready_directed.bin --require-ready --require-closed
	./build/oracle_caller build/oracle/caller_worldinit_directed.bin --require-worldinit --require-closed

.PHONY: oracle-capture-caller-ready oracle-caller-ready

# Linked IRQ2/4/6, including sprite RAM clear/upload caller continuations.
oracle-capture-caller-allirq:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 AB2_ORACLE_CALLER_IRQ4=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_allirq_trail.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_TRAIL_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_trail_directed.bin 120 15

oracle-caller-allirq: oracle-caller
	./build/oracle_caller build/oracle/caller_allirq_trail.bin --require-ready-path --require-worldinit --require-publication-init --require-clear --require-trail-path --require-allirq --require-closed
	./build/oracle_caller build/oracle/caller_trail_directed.bin --require-trail --require-closed

.PHONY: oracle-capture-caller-allirq oracle-caller-allirq

# Timed sound-ring producer; caller entry and hardware frontiers remain explicit.
oracle-capture-caller-soundproducer:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_SOUNDPRODUCER=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 AB2_ORACLE_CALLER_IRQ4=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_soundproducer.bin 600 30
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_SOUNDPRODUCER=1 AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_SOUNDPRODUCER_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_soundproducer_directed.bin 120 15

oracle-caller-soundproducer: oracle-caller
	./build/oracle_caller build/oracle/caller_soundproducer.bin --require-soundproducer-path --require-allirq --require-closed
	./build/oracle_caller build/oracle/caller_soundproducer_directed.bin --require-soundproducer --require-closed

.PHONY: oracle-capture-caller-soundproducer oracle-caller-soundproducer

# Computed input graph. Short/direct fixtures omit unrelated caller snapshots.
oracle-capture-caller-input:
	$(MAKE) oracle-capture-caller-input-short
	$(MAKE) oracle-capture-caller-input-directed
	$(MAKE) oracle-capture-caller-input-full

oracle-capture-caller-input-short:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_INPUT=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_INPUT_ONLY=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_input_short.bin 120 15

oracle-capture-caller-input-directed:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_INPUT=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_INPUT_ONLY=1 AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_INPUT_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_input_directed.bin 200 15

oracle-capture-caller-input-full:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_INPUT=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_CALLER_SOUNDPRODUCER=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_input.bin 600 30

oracle-caller-input: build/oracle_caller
	./build/oracle_caller build/oracle/caller_input_short.bin --require-input-path --require-closed
	./build/oracle_caller build/oracle/caller_input_directed.bin --require-input --require-closed
	./build/oracle_caller build/oracle/caller_input.bin --require-input-path --require-allirq --require-closed

oracle-input-generated-check:
	@mkdir -p build/oracle
	python3 tools/build_rom_image.py maincpu build/oracle/input_main.bin
	python3 tools/generate_input_schedule.py build/oracle/input_main.bin --check

.PHONY: oracle-capture-caller-input oracle-capture-caller-input-short oracle-capture-caller-input-directed oracle-capture-caller-input-full oracle-caller-input oracle-input-generated-check

# Timed live flight and projection, sharing the already timed append graph.
oracle-capture-caller-flight:
	$(MAKE) oracle-capture-caller-flight-short
	$(MAKE) oracle-capture-caller-flight-directed
	$(MAKE) oracle-capture-caller-flight-full

oracle-capture-caller-flight-short:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_FLIGHT=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_FLIGHT_ONLY=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_FOLLOWER=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_flight_short.bin 120 15

oracle-capture-caller-flight-directed:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_FLIGHT=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_FLIGHT_ONLY=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_FLIGHT_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_flight_directed.bin 200 15

oracle-capture-caller-flight-full:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_FLIGHT=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_CALLER_SOUNDPRODUCER=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 AB2_ORACLE_CALLER_INPUT=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_flight.bin 600 30

oracle-caller-flight: build/oracle_caller
	./build/oracle_caller build/oracle/caller_flight_short.bin --require-flight-path --require-closed
	./build/oracle_caller build/oracle/caller_flight_directed.bin --require-flight --require-closed
	./build/oracle_caller build/oracle/caller_flight.bin --require-flight-path --require-input-path --require-allirq --require-closed

oracle-flight-generated-check:
	@mkdir -p build/oracle
	python3 tools/build_rom_image.py maincpu build/oracle/flight_main.bin
	python3 tools/generate_input_schedule.py build/oracle/flight_main.bin --graph flight --check


.PHONY: oracle-capture-caller-flight oracle-capture-caller-flight-short oracle-capture-caller-flight-directed oracle-capture-caller-flight-full oracle-caller-flight oracle-flight-generated-check

# Timed shadow and exhaust, sharing the already timed append graph.
oracle-capture-caller-companion:
	$(MAKE) oracle-capture-caller-companion-short
	$(MAKE) oracle-capture-caller-companion-directed
	$(MAKE) oracle-capture-caller-companion-full

oracle-capture-caller-companion-short:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_COMPANION=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_COMPANION_ONLY=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_FOLLOWER=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_companion_short.bin 120 15

oracle-capture-caller-companion-directed:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_COMPANION=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_COMPANION_ONLY=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_COMPANION_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_companion_directed.bin 320 15

oracle-capture-caller-companion-full:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_COMPANION=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_CALLER_FLIGHT=1 AB2_ORACLE_CALLER_SOUNDPRODUCER=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 AB2_ORACLE_CALLER_INPUT=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_companion.bin 600 30

oracle-caller-companion: build/oracle_caller
	./build/oracle_caller build/oracle/caller_companion_short.bin --require-companion-path --require-closed
	./build/oracle_caller build/oracle/caller_companion_directed.bin --require-companion --require-closed
	./build/oracle_caller build/oracle/caller_companion.bin --require-companion-path --require-flight-path --require-input-path --require-allirq --require-closed

oracle-companion-generated-check:
	@mkdir -p build/oracle
	python3 tools/build_rom_image.py maincpu build/oracle/companion_main.bin
	python3 tools/generate_input_schedule.py build/oracle/companion_main.bin --graph companion --check


.PHONY: oracle-capture-caller-companion oracle-capture-caller-companion-short oracle-capture-caller-companion-directed oracle-capture-caller-companion-full oracle-caller-companion oracle-companion-generated-check

# Bounded sub publication waits and the two-CPU shared signal ledger.
build/oracle_subpublication: tests/oracle_subpublication.cpp src/main/engine/osubpublication.cpp src/main/engine/osubpublication.hpp src/main/engine/subbus.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_subpublication.cpp src/main/engine/osubpublication.cpp $(ORACLE_SRCS) -o $@

oracle-capture-subpublication-short:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 tools/oracle/capture.sh tools/oracle/capture_subpublication.lua build/oracle/subpublication_short.bin 120 15

oracle-capture-subpublication-directed:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_PUBLICATION_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_subpublication.lua build/oracle/subpublication_directed.bin 160 15

oracle-capture-subpublication-full:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 tools/oracle/capture.sh tools/oracle/capture_subpublication.lua build/oracle/subpublication.bin 600 30

oracle-subpublication: build/oracle_subpublication
	./build/oracle_subpublication build/oracle/subpublication_short.bin
	./build/oracle_subpublication build/oracle/subpublication_directed.bin --require-endpoints
	./build/oracle_subpublication build/oracle/subpublication.bin

.PHONY: oracle-capture-subpublication-short oracle-capture-subpublication-directed oracle-capture-subpublication-full oracle-subpublication

# Bounded sub CPU sprite/road progress; external IRQ/producer delivery is explicit.
build/oracle_subprogress: tests/oracle_subprogress.cpp src/main/engine/osubprogress.cpp src/main/engine/osubprogress.hpp src/main/engine/osubprogress.inc src/main/engine/osubprogress_manifest.hpp src/main/engine/osubpublication.cpp src/main/engine/osubpublication.hpp src/main/engine/subbus.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_subprogress.cpp src/main/engine/osubprogress.cpp src/main/engine/osubpublication.cpp $(ORACLE_SRCS) -o $@

oracle-capture-subprogress-short:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 tools/oracle/capture.sh tools/oracle/capture_subprogress.lua build/oracle/subprogress_short.bin 120 15

oracle-capture-subprogress-directed:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_PUBLICATION_DIRECTED=1 AB2_ORACLE_SUB_GRAPH_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_subprogress.lua build/oracle/subprogress_directed.bin 360 15

oracle-capture-subprogress-full:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 tools/oracle/capture.sh tools/oracle/capture_subprogress.lua build/oracle/subprogress.bin 600 30

oracle-subprogress: build/oracle_subprogress
	./build/oracle_subprogress build/oracle/subprogress_short.bin
	./build/oracle_subprogress build/oracle/subprogress_directed.bin --require-endpoints
	./build/oracle_subprogress build/oracle/subprogress.bin

oracle-subprogress-generated-check:
	@mkdir -p build/oracle
	python3 tools/build_rom_image.py subcpu build/oracle/subprogress_rom.bin
	python3 tools/generate_sub_schedule.py build/oracle/subprogress_rom.bin --check

.PHONY: oracle-capture-subprogress-short oracle-capture-subprogress-directed oracle-capture-subprogress-full oracle-subprogress oracle-subprogress-generated-check

# Bounded independent sub interrupt selection, entry, service and resumption.
build/oracle_subirq: tests/oracle_subirq.cpp src/main/engine/osubirq.cpp src/main/engine/osubirq.hpp src/main/engine/osubprogress.cpp src/main/engine/osubprogress.hpp src/main/engine/osubprogress.inc src/main/engine/osubpublication.cpp src/main/hwmath/xboard_sub_irq_selection.hpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_subirq.cpp src/main/engine/osubirq.cpp src/main/engine/osubprogress.cpp src/main/engine/osubpublication.cpp $(ORACLE_SRCS) -o $@

oracle-capture-subirq-short:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 tools/oracle/capture.sh tools/oracle/capture_subirq.lua build/oracle/subirq_short.bin 120 15

oracle-capture-subirq-directed:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_PUBLICATION_DIRECTED=1 AB2_ORACLE_SUB_GRAPH_DIRECTED=1 AB2_ORACLE_SUB_IRQ_DIRECTED=1 tools/oracle/capture.sh tools/oracle/capture_subirq.lua build/oracle/subirq_directed.bin 360 15

oracle-capture-subirq-full:
	@mkdir -p build/oracle
	AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 tools/oracle/capture.sh tools/oracle/capture_subirq.lua build/oracle/subirq.bin 600 30

oracle-subirq: build/oracle_subirq
	./build/oracle_subirq build/oracle/subirq_short.bin
	./build/oracle_subirq build/oracle/subirq_directed.bin --require-endpoints
	./build/oracle_subirq build/oracle/subirq.bin

build/oracle_bus_arbiter: tests/oracle_bus_arbiter.cpp src/main/hwmath/xboard_bus_arbiter.hpp src/main/engine/mainbus.cpp src/main/engine/mainbus.hpp src/main/engine/subbus.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_bus_arbiter.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp -o $@

MAME_SOURCE ?= /Users/dakiva/dev/mame
oracle-scheduler-probe:
	python3 tools/oracle/build_scheduler_probe.py "$(MAME_SOURCE)" build/oracle/scheduler_probe

oracle-capture-bus-arbiter: oracle-scheduler-probe
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/arbiter_schedule.txt" AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_bus_arbiter.lua build/oracle/bus_arbiter_short.bin 120 15

oracle-bus-arbiter: build/oracle_bus_arbiter
	./build/oracle_bus_arbiter build/oracle/bus_arbiter_short.bin build/oracle/arbiter_schedule.txt

oracle-capture-bus-boot: oracle-scheduler-probe
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/arbiter_boot.txt" AB2_ORACLE_START_FRAME=0 AB2_CPU_PROBE=1 tools/oracle/capture.sh tools/oracle/capture_bus_arbiter.lua build/oracle/bus_arbiter_boot.bin 32 15

oracle-bus-boot: build/oracle_bus_arbiter
	./build/oracle_bus_arbiter build/oracle/bus_arbiter_boot.bin build/oracle/arbiter_boot.txt --require-boot

oracle-bus-boot-mutations: oracle-bus-boot
	CXX="$(CXX)" CXXFLAGS="$(CXXFLAGS)" python3 tools/oracle/check_bus_boot_mutations.py

oracle-test: oracle-bus-boot-mutations

.PHONY: oracle-capture-bus-boot oracle-bus-boot oracle-bus-boot-mutations

.PHONY: oracle-capture-subirq-short oracle-capture-subirq-directed oracle-capture-subirq-full oracle-subirq oracle-scheduler-probe oracle-capture-bus-arbiter oracle-bus-arbiter

# Timed weapon controller and fire/allocation callers. HUD remains a labelled callee.
oracle-capture-caller-weapon-short:
	@mkdir -p build/oracle
	AB2_ORACLE_CALLER_WEAPON=1 AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_COMPANION=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_CALLER_FLIGHT=1 AB2_ORACLE_CALLER_SOUNDPRODUCER=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 AB2_ORACLE_CALLER_INPUT=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_weapon_short.bin 120 15

oracle-capture-caller-weapon-directed:
	@mkdir -p build/oracle
	AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_WEAPON_DIRECTED=1 AB2_ORACLE_CALLER_WEAPON=1 AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_COMPANION=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_CALLER_FLIGHT=1 AB2_ORACLE_CALLER_SOUNDPRODUCER=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 AB2_ORACLE_CALLER_INPUT=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_weapon_directed.bin 240 15

oracle-caller-weapon: build/oracle_caller
	./build/oracle_caller build/oracle/caller_weapon_short.bin --require-weapon-path --require-closed
	./build/oracle_caller build/oracle/caller_weapon.bin --require-weapon-path --require-flight-path --require-input-path --require-allirq --require-closed
	./build/oracle_caller build/oracle/caller_weapon_directed.bin --require-weapon --require-closed

oracle-weapon-generated-check:
	@mkdir -p build/oracle
	python3 tools/build_rom_image.py maincpu build/oracle/weapon_main.bin
	python3 tools/generate_input_schedule.py build/oracle/weapon_main.bin --graph weapons --check

.PHONY: oracle-capture-caller-weapon-short oracle-capture-caller-weapon-directed oracle-caller-weapon oracle-weapon-generated-check

oracle-capture-caller-weapon-full:
	@mkdir -p build/oracle
	AB2_ORACLE_CALLER_WEAPON=1 AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_COMPANION=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_CALLER_FLIGHT=1 AB2_ORACLE_CALLER_SOUNDPRODUCER=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 AB2_ORACLE_CALLER_INPUT=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_weapon.bin 600 30

.PHONY: oracle-capture-caller-weapon-full

# Literal flight initialization/recovery and caller HUD/launch-sound graphs.
oracle-capture-caller-host-short:
	@mkdir -p build/oracle
	AB2_ORACLE_CALLER_HOST=1 AB2_ORACLE_CALLER_WEAPON=1 AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_COMPANION=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_CALLER_FLIGHT=1 AB2_ORACLE_CALLER_SOUNDPRODUCER=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 AB2_ORACLE_CALLER_INPUT=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_host_short.bin 120 15

oracle-capture-caller-host-directed:
	@mkdir -p build/oracle
	AB2_ORACLE_CALLER_DIRECTED=1 AB2_ORACLE_HOST_DIRECTED=1 AB2_ORACLE_CALLER_HOST=1 AB2_ORACLE_CALLER_WEAPON=1 AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_COMPANION=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_CALLER_FLIGHT=1 AB2_ORACLE_CALLER_SOUNDPRODUCER=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 AB2_ORACLE_CALLER_INPUT=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_host_directed.bin 360 20

oracle-capture-caller-host-full:
	@mkdir -p build/oracle
	AB2_ORACLE_CALLER_HOST=1 AB2_ORACLE_CALLER_WEAPON=1 AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_CALLER_SERVICE=1 AB2_ORACLE_CALLER_RETURN=1 AB2_ORACLE_CALLER_STEPS=1 AB2_ORACLE_CALLER_WORDS=1 AB2_ORACLE_CALLER_SUCCESSORS=1 AB2_ORACLE_CALLER_CLOSURE=1 AB2_ORACLE_CALLER_SNAPSHOTS=1 AB2_ORACLE_CALLER_COMPANION=1 AB2_ORACLE_CALLER_IRQ4=1 AB2_ORACLE_CALLER_FLIGHT=1 AB2_ORACLE_CALLER_SOUNDPRODUCER=1 AB2_ORACLE_CALLER_FOLLOWER=1 AB2_ORACLE_CALLER_READY=1 AB2_ORACLE_CALLER_WORLDINIT=1 AB2_ORACLE_CALLER_CLEAR=1 AB2_ORACLE_CALLER_TRAIL=1 AB2_ORACLE_CALLER_INPUT=1 tools/oracle/capture.sh tools/oracle/capture_caller.lua build/oracle/caller_host.bin 600 30


oracle-caller-host: build/oracle_caller
	./build/oracle_caller build/oracle/caller_host_short.bin --require-closed
	./build/oracle_caller build/oracle/caller_host.bin --require-host-path --require-weapon-path --require-flight-path --require-input-path --require-allirq --require-closed
	./build/oracle_caller build/oracle/caller_host_directed.bin --require-host --require-closed

oracle-host-generated-check:
	@mkdir -p build/oracle
	python3 tools/build_rom_image.py maincpu build/oracle/host_main.bin
	python3 tools/generate_input_schedule.py build/oracle/host_main.bin --graph host --check

.PHONY: oracle-capture-caller-host-short oracle-capture-caller-host-directed oracle-capture-caller-host-full oracle-caller-host oracle-host-generated-check

# Both native publication streams share computed grants and one RAM instance.
build/oracle_publication_schedule: $(wildcard src/main/hwvideo/*.hpp) $(filter src/main/hwvideo/%,$(SRCS)) tests/oracle_publication_schedule.cpp tests/sound_execution_fixture.hpp src/main/hwaudio/xboard_speaker.hpp src/main/hwaudio/xboard_sound_cpu.cpp src/main/hwaudio/xboard_sound_cpu.hpp src/main/hwaudio/xboard_pcm.hpp src/main/hwaudio/xboard_ym_timers.hpp src/main/hwaudio/xboard_audio_deadlines.hpp src/main/hwmath/xboard_device_deadlines.hpp $(wildcard src/third_party/ymfm/*) src/third_party/z80.h src/main/engine/omainjointmanifest.hpp src/main/engine/omainbootschedule.hpp src/main/hwmath/xboard_cpu_continuation.hpp $(CALLER_OBJS) src/main/engine/osubpublication.cpp src/main/engine/osubpublication.hpp src/main/engine/osubprogress.cpp src/main/engine/osubprogress.inc src/main/engine/osubprogress_manifest.hpp src/main/engine/osubirq.cpp src/main/engine/osubirq.hpp src/main/hwmath/xboard_timeline.hpp src/main/hwvideo/hwsprites.cpp src/main/hwvideo/hwsprites.hpp
	$(CXX) $(CXXFLAGS) -pthread -Isrc/main tests/oracle_publication_schedule.cpp src/main/hwaudio/xboard_sound_cpu.cpp src/third_party/ymfm/ymfm_opm.cpp src/main/engine/osubpublication.cpp src/main/engine/osubprogress.cpp src/main/engine/osubirq.cpp $(filter src/main/hwvideo/%,$(SRCS)) $(filter-out build/oracle_obj/tests/oracle_caller.o,$(CALLER_OBJS)) -o $@

oracle-capture-publication-schedule: oracle-scheduler-probe
	@mkdir -p build/oracle
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/publication_schedule.txt" AB2_ORACLE_GAMEPLAY=1 AB2_CPU_PROBE=1 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/publication_schedule.bin 120 15

oracle-publication-schedule: build/oracle_publication_schedule
	./build/oracle_publication_schedule build/oracle/publication_schedule.bin build/oracle/publication_schedule.txt

.PHONY: oracle-capture-publication-schedule oracle-publication-schedule

# Expanded existing main caller ports under the same main/sub grants.
oracle-joint-main-generated-check:
	@mkdir -p build/oracle
	python3 tools/build_rom_image.py maincpu build/oracle/main_joint_image.bin
	python3 tools/generate_input_schedule.py build/oracle/main_joint_image.bin --graph irqwrapper --check
	python3 tools/generate_input_schedule.py build/oracle/main_joint_image.bin --graph producers --check
	python3 tools/generate_input_schedule.py build/oracle/main_joint_image.bin --graph callees --check
	python3 tools/generate_input_schedule.py build/oracle/main_joint_image.bin --graph remaining --check
	python3 tools/generate_joint_main_manifest.py --check

oracle-capture-joint-main-progress: oracle-scheduler-probe oracle-joint-main-generated-check
	@mkdir -p build/oracle
	python3 tools/generate_joint_main_manifest.py --check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/joint_main_progress.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/joint_main_progress.devices" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/joint_main_progress.bin.screen" AB2_PIXEL_TRACE="$(CURDIR)/build/oracle/joint_main_progress.bin.pixels" AB2_LATCH_TRACE="$(CURDIR)/build/oracle/joint_main_progress.bin.latches" AB2_ORACLE_GAMEPLAY=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/joint_main_progress.bin 120 15

oracle-joint-main-progress: build/oracle_publication_schedule oracle-joint-main-generated-check
	python3 tools/generate_joint_main_manifest.py --check
	./build/oracle_publication_schedule build/oracle/joint_main_progress.bin build/oracle/joint_main_progress.txt --require-main-closed --require-sub-continuity --require-video-frontiers --require-sound-handshake

.PHONY: oracle-capture-joint-main-progress oracle-joint-main-progress oracle-joint-main-generated-check

oracle-capture-joint-main-progress-full: oracle-scheduler-probe oracle-joint-main-generated-check
	@mkdir -p build/oracle
	python3 tools/generate_joint_main_manifest.py --check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/joint_main_progress_full.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/joint_main_progress_full.devices" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/joint_main_progress_full.bin.screen" AB2_PIXEL_TRACE="$(CURDIR)/build/oracle/joint_main_progress_full.bin.pixels" AB2_LATCH_TRACE="$(CURDIR)/build/oracle/joint_main_progress_full.bin.latches" AB2_ORACLE_GAMEPLAY=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/joint_main_progress_full.bin 600 30

oracle-joint-main-progress-full: build/oracle_publication_schedule
	./build/oracle_publication_schedule build/oracle/joint_main_progress_full.bin build/oracle/joint_main_progress_full.txt --require-reset --require-main-closed --require-sub-continuity --require-video-frontiers --require-sound-handshake

oracle-capture-joint-reset-window: oracle-scheduler-probe oracle-joint-main-generated-check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/joint_reset_focused.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/joint_reset_focused.devices" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/joint_reset_focused.bin.screen" AB2_ORACLE_GAMEPLAY=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 AB2_ORACLE_START_FRAME=400 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/joint_reset_focused.bin 440 15

oracle-joint-reset-window: build/oracle_publication_schedule oracle-joint-main-generated-check
	./build/oracle_publication_schedule build/oracle/joint_reset_focused.bin build/oracle/joint_reset_focused.txt --reset-window --physical-main --require-sub-continuity --require-video-frontiers --require-sound-handshake

.PHONY: oracle-capture-joint-reset-window oracle-joint-reset-window

build/oracle_subreset: tests/oracle_subreset.cpp src/main/engine/osubirq.cpp src/main/engine/osubirq.hpp src/main/engine/osubprogress.cpp src/main/engine/osubprogress.hpp src/main/engine/osubprogress.inc src/main/engine/osubpublication.cpp src/main/engine/osubpublication.hpp
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_subreset.cpp src/main/engine/osubirq.cpp src/main/engine/osubprogress.cpp src/main/engine/osubpublication.cpp $(ORACLE_SRCS) -o $@

oracle-capture-subreset: oracle-scheduler-probe
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_CPU_PROBE=1 tools/oracle/capture.sh tools/oracle/capture_subreset.lua build/oracle/subreset.bin 600 30

oracle-subreset: build/oracle_subreset
	./build/oracle_subreset build/oracle/subreset.bin

oracle-test: oracle-subreset

.PHONY: oracle-capture-subreset oracle-subreset

.PHONY: oracle-capture-joint-main-progress-full oracle-joint-main-progress-full

# Bounded native Z80 reset boot, stopping before any device read is supplied.
oracle-capture-sound-boot:
	@mkdir -p build/oracle
	tools/oracle/capture.sh tools/oracle/capture_sound_boot.lua build/oracle/sound_boot.bin 1 5

build/oracle_sound_boot: tests/oracle_sound_boot.cpp src/main/hwaudio/xboard_sound_cpu.cpp src/main/hwaudio/xboard_sound_cpu.hpp src/main/hwaudio/xboard_pcm_startup.hpp src/main/hwmath/compare_timer.hpp src/third_party/z80.h
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_sound_boot.cpp src/main/hwaudio/xboard_sound_cpu.cpp -o $@

oracle-sound-boot: build/oracle_sound_boot
	./build/oracle_sound_boot build/oracle/sound_boot.bin

test-sound-cpu: build/sound_cpu_regression
	./build/sound_cpu_regression

oracle-sound-boot-mutations: oracle-sound-boot
	python3 tools/oracle/check_sound_boot_mutations.py

.PHONY: oracle-capture-sound-boot oracle-sound-boot oracle-sound-boot-mutations test-sound-cpu

oracle-test: oracle-joint-main-progress oracle-sound-boot-mutations

oracle-pcm-probe:
	python3 tools/oracle/build_pcm_probe.py /Users/dakiva/dev/mame build/oracle/pcm_probe

oracle-capture-pcm: oracle-pcm-probe
	MAME="$(CURDIR)/build/oracle/pcm_probe/mame" AB2_PCM_TRACE="$(CURDIR)/build/oracle/pcm.bin" AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_pcm.lua build/oracle/pcm.bin 600 15
	MAME="$(CURDIR)/build/oracle/pcm_probe/mame" AB2_PCM_TRACE="$(CURDIR)/build/oracle/pcm_directed.bin" AB2_PCM_DIRECTED=1 AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_pcm.lua build/oracle/pcm_directed.bin 600 15
	MAME="$(CURDIR)/build/oracle/pcm_probe/mame" AB2_PCM_TRACE="$(CURDIR)/build/oracle/pcm_synthetic.bin" AB2_PCM_SYNTHETIC=1 AB2_PCM_DIRECTED=1 AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_pcm.lua build/oracle/pcm_synthetic.bin 600 15

.PHONY: oracle-pcm-probe oracle-capture-pcm

build/oracle_pcm: tests/oracle_pcm.cpp src/main/hwaudio/xboard_pcm.hpp src/main/romloader.cpp src/main/romloader.hpp
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_pcm.cpp src/main/romloader.cpp -o $@

oracle-pcm: build/oracle_pcm
	./build/oracle_pcm build/oracle/pcm.bin
	./build/oracle_pcm build/oracle/pcm_directed.bin --require-directed
	./build/oracle_pcm build/oracle/pcm_synthetic.bin --require-directed

.PHONY: oracle-pcm

oracle-pcm-mutations: oracle-pcm
	python3 tools/oracle/check_pcm_mutations.py

oracle-test: oracle-pcm-mutations

.PHONY: oracle-pcm-mutations

# Joint tile latches and phase-local RGB use the existing computed main/sub corpus.
oracle-joint-video: build/oracle_publication_schedule
	./build/oracle_publication_schedule build/oracle/joint_main_progress_full.bin build/oracle/joint_main_progress_full.txt --require-reset --require-main-closed --require-sub-continuity --require-video-rgb --require-sound-handshake
.PHONY: oracle-joint-video

oracle-capture-joint-video-directed: oracle-scheduler-probe oracle-joint-main-generated-check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/joint_video_directed.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/joint_video_directed.devices" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/joint_video_directed.bin.screen" AB2_PIXEL_TRACE="$(CURDIR)/build/oracle/joint_video_directed.bin.pixels" AB2_LATCH_TRACE="$(CURDIR)/build/oracle/joint_video_directed.bin.latches" AB2_DISPLAY_FIXTURE="$(CURDIR)/build/oracle/joint_video_directed.bin.fixture" AB2_ORACLE_GAMEPLAY=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/joint_video_directed.bin 120 15
oracle-joint-video-directed: build/oracle_publication_schedule
	./build/oracle_publication_schedule build/oracle/joint_video_directed.bin build/oracle/joint_video_directed.txt --require-video-fixture --require-main-closed --require-sub-continuity --require-video-rgb --require-sound-handshake
.PHONY: oracle-capture-joint-video-directed oracle-joint-video-directed

oracle-joint-video-mutations: oracle-joint-video oracle-joint-video-directed oracle-joint-video-owned
	python3 tools/oracle/check_joint_video_mutations.py
oracle-test: oracle-joint-video-mutations
.PHONY: oracle-joint-video-mutations

# Continuous Z80 execution; captured bus values/times are assertions only.
build/oracle_sound_execution: tests/oracle_sound_execution.cpp tests/sound_execution_fixture.hpp src/main/hwaudio/xboard_speaker.hpp src/main/hwaudio/xboard_sound_cpu.cpp src/main/hwaudio/xboard_sound_cpu.hpp src/main/hwaudio/xboard_ym_timers.hpp src/main/hwaudio/xboard_audio_deadlines.hpp src/main/hwmath/xboard_device_deadlines.hpp $(wildcard src/third_party/ymfm/*) src/main/hwaudio/xboard_pcm.hpp src/third_party/z80.h
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_sound_execution.cpp src/main/hwaudio/xboard_sound_cpu.cpp src/third_party/ymfm/ymfm_opm.cpp -o $@

oracle-capture-sound-execution: oracle-scheduler-probe
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/sound_execution.txt" tools/oracle/capture.sh tools/oracle/capture_sound_execution.lua build/oracle/sound_execution.bin 16 5

oracle-capture-sound-execution-full: oracle-scheduler-probe
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/sound_execution_full.txt" AB2_ORACLE_GAMEPLAY=1 tools/oracle/capture.sh tools/oracle/capture_sound_execution.lua build/oracle/sound_execution_full.bin 600 15

oracle-sound-execution-full: build/oracle_sound_execution
	./build/oracle_sound_execution build/oracle/sound_execution_full.bin build/oracle/sound_execution_full.txt

oracle-capture-sound-irq: oracle-scheduler-probe
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/sound_irq.txt" AB2_Z80_IRQ_TRACE="$(CURDIR)/build/oracle/sound_irq.bin.irq" AB2_SOUND_IRQ_FIXTURE=1 tools/oracle/capture.sh tools/oracle/capture_sound_execution.lua build/oracle/sound_irq.bin 4 5

oracle-sound-execution: build/oracle_sound_execution
	./build/oracle_sound_execution build/oracle/sound_execution.bin build/oracle/sound_execution.txt

oracle-sound-irq: build/oracle_sound_execution
	./build/oracle_sound_execution build/oracle/sound_irq.bin build/oracle/sound_irq.txt

oracle-capture-joint-sound-full: oracle-scheduler-probe oracle-joint-main-generated-check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/joint_sound_full.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/joint_sound_full.devices" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/joint_sound_full.bin.screen" AB2_PIXEL_TRACE="$(CURDIR)/build/oracle/joint_sound_full.bin.pixels" AB2_LATCH_TRACE="$(CURDIR)/build/oracle/joint_sound_full.bin.latches" AB2_ORACLE_GAMEPLAY=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 AB2_JOINT_SOUND_EXECUTION=1 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/joint_sound_full.bin 600 30

oracle-joint-sound-full: build/oracle_publication_schedule oracle-joint-main-generated-check
	./build/oracle_publication_schedule build/oracle/joint_sound_full.bin build/oracle/joint_sound_full.txt --require-reset --require-main-closed --require-sub-continuity --require-video-rgb --require-native-sound

oracle-sound-execution-mutations: oracle-sound-execution oracle-sound-execution-full oracle-sound-irq
	python3 tools/oracle/check_sound_execution_mutations.py

oracle-test: oracle-sound-execution-mutations oracle-joint-sound-full
.PHONY: oracle-capture-sound-execution oracle-capture-sound-irq oracle-sound-execution oracle-sound-irq oracle-capture-joint-sound-full oracle-joint-sound-full oracle-sound-execution-mutations

.PHONY: oracle-capture-sound-execution-full oracle-sound-execution-full

# Host synchronization cost only; no ROMs, captured clocks or acceptance skips.
build/cpu_continuation_benchmark: tools/benchmark_cpu_continuation.cpp src/main/hwmath/xboard_cpu_continuation.hpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -pthread -Isrc/main tools/benchmark_cpu_continuation.cpp -o $@
benchmark-cpu-continuation: build/cpu_continuation_benchmark
	./build/cpu_continuation_benchmark
.PHONY: benchmark-cpu-continuation

# All native device heads and raw audio batches are assertions, not providers.
oracle-capture-joint-audio-full: oracle-scheduler-probe oracle-joint-main-generated-check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/joint_audio_full.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/joint_audio_full.devices" AB2_AUDIO_TRACE="$(CURDIR)/build/oracle/joint_audio_full.bin.audio" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/joint_audio_full.bin.screen" AB2_PIXEL_TRACE="$(CURDIR)/build/oracle/joint_audio_full.bin.pixels" AB2_LATCH_TRACE="$(CURDIR)/build/oracle/joint_audio_full.bin.latches" AB2_ORACLE_GAMEPLAY=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 AB2_JOINT_SOUND_EXECUTION=1 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/joint_audio_full.bin 600 30
oracle-joint-audio-full: build/oracle_publication_schedule oracle-joint-main-generated-check
	./build/oracle_publication_schedule build/oracle/joint_audio_full.bin build/oracle/joint_audio_full.txt --require-reset --require-main-closed --require-sub-continuity --require-video-rgb --require-native-deadlines
.PHONY: oracle-capture-joint-audio-full oracle-joint-audio-full

build/audio_deadline_regression: tests/audio_deadline_regression.cpp src/main/hwaudio/xboard_audio_deadlines.hpp src/main/hwmath/xboard_device_deadlines.hpp src/main/hwaudio/xboard_pcm.hpp $(wildcard src/third_party/ymfm/*)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -Isrc/main tests/audio_deadline_regression.cpp src/third_party/ymfm/ymfm_opm.cpp -o $@
test-audio-deadlines: build/audio_deadline_regression
	./build/audio_deadline_regression
test: test-audio-deadlines
.PHONY: test-audio-deadlines

oracle-capture-sound-audio-irq: oracle-scheduler-probe
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/sound_audio_irq.txt" AB2_AUDIO_TRACE="$(CURDIR)/build/oracle/sound_audio_irq.bin.audio" AB2_Z80_IRQ_TRACE="$(CURDIR)/build/oracle/sound_audio_irq.bin.irq" AB2_SOUND_IRQ_FIXTURE=1 tools/oracle/capture.sh tools/oracle/capture_sound_execution.lua build/oracle/sound_audio_irq.bin 4 5
oracle-capture-sound-audio-timer-b: oracle-scheduler-probe
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/sound_audio_timer_b.txt" AB2_AUDIO_TRACE="$(CURDIR)/build/oracle/sound_audio_timer_b.bin.audio" AB2_Z80_IRQ_TRACE="$(CURDIR)/build/oracle/sound_audio_timer_b.bin.irq" AB2_SOUND_TIMER_B_FIXTURE=1 tools/oracle/capture.sh tools/oracle/capture_sound_execution.lua build/oracle/sound_audio_timer_b.bin 4 5
oracle-sound-audio-irq: build/oracle_sound_execution
	./build/oracle_sound_execution build/oracle/sound_audio_irq.bin build/oracle/sound_audio_irq.txt --native-audio
oracle-sound-audio-timer-b: build/oracle_sound_execution
	./build/oracle_sound_execution build/oracle/sound_audio_timer_b.bin build/oracle/sound_audio_timer_b.txt --native-audio
.PHONY: oracle-capture-sound-audio-irq oracle-capture-sound-audio-timer-b oracle-sound-audio-irq oracle-sound-audio-timer-b

oracle-audio-deadline-mutations: oracle-joint-audio-full oracle-sound-audio-irq oracle-sound-audio-timer-b
	python3 tools/oracle/check_audio_deadline_mutations.py
oracle-test: oracle-audio-deadline-mutations
.PHONY: oracle-audio-deadline-mutations

# Exact MAME default speaker resampling/mix, captures remain local.
build/oracle_speaker: tests/oracle_speaker.cpp src/main/hwaudio/xboard_speaker.hpp
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_speaker.cpp -o $@
oracle-speaker: build/oracle_speaker
	./build/oracle_speaker build/oracle/joint_speaker_full.bin.audio
.PHONY: oracle-speaker

oracle-joint-speaker-full: build/oracle_publication_schedule oracle-joint-main-generated-check
	./build/oracle_publication_schedule build/oracle/joint_speaker_full.bin build/oracle/joint_speaker_full.txt --require-reset --require-main-closed --require-sub-continuity --require-video-rgb --require-native-deadlines --require-speaker
.PHONY: oracle-joint-speaker-full

oracle-speaker-edges: build/oracle_speaker
	./build/oracle_speaker build/oracle/speaker_edges
oracle-capture-speaker-edges: oracle-scheduler-probe
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_AUDIO_TRACE="$(CURDIR)/build/oracle/speaker_edges" tools/oracle/capture.sh tools/oracle/capture_speaker_edges.lua build/oracle/speaker_edges.done 64 5
oracle-capture-joint-speaker-full: oracle-scheduler-probe oracle-joint-main-generated-check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/joint_speaker_full.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/joint_speaker_full.devices" AB2_AUDIO_TRACE="$(CURDIR)/build/oracle/joint_speaker_full.bin.audio" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/joint_speaker_full.bin.screen" AB2_PIXEL_TRACE="$(CURDIR)/build/oracle/joint_speaker_full.bin.pixels" AB2_LATCH_TRACE="$(CURDIR)/build/oracle/joint_speaker_full.bin.latches" AB2_ORACLE_GAMEPLAY=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 AB2_JOINT_SOUND_EXECUTION=1 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/joint_speaker_full.bin 600 30
.PHONY: oracle-speaker-edges oracle-capture-speaker-edges oracle-capture-joint-speaker-full

oracle-speaker-mutations: oracle-speaker oracle-speaker-edges oracle-joint-speaker-full
	python3 tools/oracle/check_speaker_mutations.py
oracle-test: oracle-speaker-mutations
.PHONY: oracle-speaker-mutations

# Coupled directed video acceptance: device heads/sound/speaker are native.
oracle-capture-joint-video-owned-directed: oracle-scheduler-probe oracle-joint-main-generated-check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/joint_video_owned_directed.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/joint_video_owned_directed.devices" AB2_AUDIO_TRACE="$(CURDIR)/build/oracle/joint_video_owned_directed.bin.audio" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/joint_video_owned_directed.bin.screen" AB2_PIXEL_TRACE="$(CURDIR)/build/oracle/joint_video_owned_directed.bin.pixels" AB2_LATCH_TRACE="$(CURDIR)/build/oracle/joint_video_owned_directed.bin.latches" AB2_DISPLAY_FIXTURE="$(CURDIR)/build/oracle/joint_video_owned_directed.bin.fixture" AB2_ORACLE_GAMEPLAY=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 AB2_JOINT_SOUND_EXECUTION=1 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/joint_video_owned_directed.bin 120 15
oracle-joint-video-owned-directed: build/oracle_publication_schedule oracle-joint-main-generated-check
	./build/oracle_publication_schedule build/oracle/joint_video_owned_directed.bin build/oracle/joint_video_owned_directed.txt --require-video-fixture --require-main-closed --require-sub-continuity --require-video-rgb --require-native-deadlines --require-speaker
oracle-joint-video-owned: oracle-joint-speaker-full oracle-joint-video-owned-directed
.PHONY: oracle-capture-joint-video-owned-directed oracle-joint-video-owned-directed oracle-joint-video-owned

# Additional physical gameplay inputs; CPU state is retained after frame 32.
oracle-capture-joint-coverage-flight: oracle-scheduler-probe oracle-joint-main-generated-check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/joint_coverage_flight_expanded.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/joint_coverage_flight_expanded.devices" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/joint_coverage_flight_expanded.bin.screen" AB2_PIXEL_TRACE="$(CURDIR)/build/oracle/joint_coverage_flight_expanded.bin.pixels" AB2_LATCH_TRACE="$(CURDIR)/build/oracle/joint_coverage_flight_expanded.bin.latches" AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_INPUT_ENDPOINTS=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/joint_coverage_flight_expanded.bin 1200 25
oracle-joint-coverage-flight: build/oracle_publication_schedule oracle-joint-main-generated-check
	./build/oracle_publication_schedule build/oracle/joint_coverage_flight_expanded.bin build/oracle/joint_coverage_flight_expanded.txt --require-reset --require-main-closed --require-sub-continuity --require-video-rgb --require-sound-handshake
.PHONY: oracle-capture-joint-coverage-flight oracle-joint-coverage-flight

oracle-capture-cpu-coverage: oracle-scheduler-probe
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_CPU_PROBE=1 tools/oracle/capture.sh tools/oracle/capture_cpu_coverage.lua build/oracle/cpu_coverage.bin 100 4
.PHONY: oracle-capture-cpu-coverage

build/oracle_cpu_coverage: $(wildcard src/main/engine/osub*.hpp src/main/engine/osub*.inc) tests/oracle_cpu_coverage.cpp $(filter-out build/oracle_obj/tests/oracle_caller.o,$(CALLER_OBJS)) src/main/engine/osubirq.cpp src/main/engine/osubprogress.cpp src/main/engine/osubpublication.cpp
	$(CXX) $(CXXFLAGS) -pthread -Isrc/main tests/oracle_cpu_coverage.cpp $(filter-out build/oracle_obj/tests/oracle_caller.o,$(CALLER_OBJS)) src/main/engine/osubirq.cpp src/main/engine/osubprogress.cpp src/main/engine/osubpublication.cpp -o $@
oracle-cpu-coverage: build/oracle_cpu_coverage
	./build/oracle_cpu_coverage build/oracle/cpu_coverage.bin
.PHONY: oracle-cpu-coverage

oracle-cpu-coverage-expanded: build/oracle_cpu_coverage
	./build/oracle_cpu_coverage build/oracle/cpu_coverage_expanded_full.bin
.PHONY: oracle-cpu-coverage-expanded

oracle-capture-cpu-nested-irq: oracle-scheduler-probe
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_CPU_PROBE=1 AB2_CPU_IRQ_FIXTURE=1 tools/oracle/capture.sh tools/oracle/capture_cpu_nested_irq.lua build/oracle/cpu_nested_irq.bin 100 4
.PHONY: oracle-capture-cpu-nested-irq

build/oracle_cpu_nested_irq: tests/oracle_cpu_nested_irq.cpp $(filter-out build/oracle_obj/tests/oracle_caller.o,$(CALLER_OBJS))
	$(CXX) $(CXXFLAGS) -pthread -Isrc/main tests/oracle_cpu_nested_irq.cpp $(filter-out build/oracle_obj/tests/oracle_caller.o,$(CALLER_OBJS)) -o $@
oracle-cpu-nested-irq: build/oracle_cpu_nested_irq
	./build/oracle_cpu_nested_irq build/oracle/cpu_nested_irq.bin
.PHONY: oracle-cpu-nested-irq

oracle-cpu-coverage-plan: oracle-joint-speaker-full
	python3 tools/build_rom_image.py maincpu build/oracle/coverage-main.bin
	python3 tools/build_rom_image.py subcpu build/oracle/coverage-sub.bin
	python3 tools/oracle/plan_cpu_coverage.py build/oracle/joint_speaker_full.bin.coverage.json build/oracle/cpu_coverage_plan.lua --main-image build/oracle/coverage-main.bin --sub-image build/oracle/coverage-sub.bin
oracle-capture-cpu-coverage-expanded: oracle-scheduler-probe oracle-cpu-coverage-plan
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_CPU_PROBE=1 AB2_CPU_COVERAGE_PLAN="$(CURDIR)/build/oracle/cpu_coverage_plan.lua" tools/oracle/capture.sh tools/oracle/capture_cpu_coverage.lua build/oracle/cpu_coverage_expanded_full.bin 700 15
oracle-cpu-coverage-check: oracle-cpu-coverage-expanded
	python3 tools/oracle/check_cpu_coverage.py build/oracle/cpu_coverage_plan.json build/oracle/cpu_coverage_expanded_full.bin.coverage.json
.PHONY: oracle-cpu-coverage-plan oracle-capture-cpu-coverage-expanded oracle-cpu-coverage-check
oracle-cpu-coverage-mutations: oracle-cpu-coverage oracle-cpu-coverage-check oracle-cpu-nested-irq
	python3 tools/oracle/check_cpu_coverage_mutations.py
oracle-main-sub-coverage: oracle-cpu-coverage-mutations oracle-joint-coverage-flight
oracle-test: oracle-main-sub-coverage
.PHONY: oracle-cpu-coverage-mutations oracle-main-sub-coverage
# Preserve the first 4,464-case capture alongside its expanded superset.
oracle-cpu-coverage-legacy: build/oracle_cpu_coverage
	./build/oracle_cpu_coverage build/oracle/cpu_coverage_expanded.bin
oracle-main-sub-coverage: oracle-cpu-coverage-legacy
.PHONY: oracle-cpu-coverage-legacy
oracle-capture-cpu-coverage-legacy: oracle-scheduler-probe oracle-joint-speaker-full
	python3 tools/build_rom_image.py maincpu build/oracle/coverage-main.bin
	python3 tools/build_rom_image.py subcpu build/oracle/coverage-sub.bin
	python3 tools/oracle/plan_cpu_coverage.py build/oracle/joint_speaker_full.bin.coverage.json build/oracle/cpu_coverage_plan_legacy.lua --legacy --main-image build/oracle/coverage-main.bin --sub-image build/oracle/coverage-sub.bin
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_CPU_PROBE=1 AB2_CPU_COVERAGE_PLAN="$(CURDIR)/build/oracle/cpu_coverage_plan_legacy.lua" tools/oracle/capture.sh tools/oracle/capture_cpu_coverage.lua build/oracle/cpu_coverage_expanded.bin 700 15
.PHONY: oracle-capture-cpu-coverage-legacy

# Cold CPU/bank capture, including reset vectors and every instruction snapshot.
oracle-capture-joint-cold: oracle-scheduler-probe oracle-cold-generated-check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/joint_cold.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/joint_cold.devices" AB2_AUDIO_TRACE="$(CURDIR)/build/oracle/joint_cold.bin.audio" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/joint_cold.bin.screen" AB2_PIXEL_TRACE="$(CURDIR)/build/oracle/joint_cold.bin.pixels" AB2_LATCH_TRACE="$(CURDIR)/build/oracle/joint_cold.bin.latches" AB2_ORACLE_GAMEPLAY=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 AB2_JOINT_SOUND_EXECUTION=1 AB2_ORACLE_START_FRAME=0 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/joint_cold.bin 120 15

.PHONY: oracle-capture-joint-cold

oracle-joint-cold: build/oracle_publication_schedule oracle-cold-generated-check
	./build/oracle_publication_schedule build/oracle/joint_cold.bin build/oracle/joint_cold.txt --require-cold --require-main-closed --require-sub-continuity --require-video-rgb --require-native-deadlines --require-speaker

.PHONY: oracle-joint-cold

# Full cold stream extends beyond the old 600-frame terminal grants.
oracle-capture-joint-cold-full: oracle-scheduler-probe oracle-cold-generated-check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/joint_cold_full.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/joint_cold_full.devices" AB2_AUDIO_TRACE="$(CURDIR)/build/oracle/joint_cold_full.bin.audio" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/joint_cold_full.bin.screen" AB2_PIXEL_TRACE="$(CURDIR)/build/oracle/joint_cold_full.bin.pixels" AB2_LATCH_TRACE="$(CURDIR)/build/oracle/joint_cold_full.bin.latches" AB2_ORACLE_GAMEPLAY=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 AB2_JOINT_SOUND_EXECUTION=1 AB2_ORACLE_START_FRAME=0 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/joint_cold_full.bin 602 30


oracle-joint-cold-full: build/oracle_publication_schedule oracle-cold-generated-check
	./build/oracle_publication_schedule build/oracle/joint_cold_full.bin build/oracle/joint_cold_full.txt --require-reset --require-cold --require-main-closed --require-sub-continuity --require-video-rgb --require-native-deadlines --require-speaker

oracle-joint-cold-mutations: oracle-joint-cold
	python3 tools/oracle/check_joint_cold_mutations.py

.PHONY: oracle-capture-joint-cold-full oracle-joint-cold-full oracle-joint-cold-mutations


oracle-joint-cold-boundaries: oracle-joint-cold-full
	python3 tools/oracle/check_joint_cold_boundaries.py build/oracle/joint_cold_full.bin build/oracle/joint_speaker_full.bin

oracle-test: oracle-joint-cold-mutations oracle-joint-cold-boundaries
.PHONY: oracle-joint-cold-boundaries


oracle-cold-generated-check:
	python3 tools/build_rom_image.py maincpu build/oracle/cold_main_image.bin
	python3 tools/generate_input_schedule.py build/oracle/cold_main_image.bin --graph boot --check
.PHONY: oracle-cold-generated-check

oracle-capture-whole-host-expanded: oracle-scheduler-probe oracle-cold-generated-check
	MAME="$(CURDIR)/build/oracle/scheduler_probe/mame" AB2_SCHED_TRACE="$(CURDIR)/build/oracle/whole_host_expanded.txt" AB2_DEVICE_TRACE="$(CURDIR)/build/oracle/whole_host_expanded.devices" AB2_AUDIO_TRACE="$(CURDIR)/build/oracle/whole_host_expanded.bin.audio" AB2_SCREEN_TRACE="$(CURDIR)/build/oracle/whole_host_expanded.bin.screen" AB2_PIXEL_TRACE="$(CURDIR)/build/oracle/whole_host_expanded.bin.pixels" AB2_LATCH_TRACE="$(CURDIR)/build/oracle/whole_host_expanded.bin.latches" AB2_ORACLE_GAMEPLAY=1 AB2_ORACLE_INPUT_ENDPOINTS=1 AB2_CPU_PROBE=1 AB2_JOINT_MAIN_PROGRESS=1 AB2_JOINT_SOUND_EXECUTION=1 AB2_ORACLE_START_FRAME=0 tools/oracle/capture.sh tools/oracle/capture_host_publication_schedule.lua build/oracle/whole_host_expanded.bin 780 35
.PHONY: oracle-capture-whole-host-expanded

build/oracle_whole_host: $(wildcard src/main/hwvideo/*.hpp src/main/hwmath/*.hpp src/main/hwaudio/*.hpp src/main/engine/*.hpp) $(filter src/main/hwvideo/%,$(SRCS)) tests/oracle_whole_host.cpp src/main/engine/xboard_host.hpp src/main/hwaudio/xboard_sound_board.hpp src/main/hwaudio/xboard_speaker.hpp src/main/hwaudio/xboard_sound_cpu.cpp src/main/hwaudio/xboard_sound_cpu.hpp src/main/hwaudio/xboard_pcm.hpp src/main/hwaudio/xboard_ym_timers.hpp src/main/hwaudio/xboard_audio_deadlines.hpp src/main/hwmath/xboard_device_deadlines.hpp $(wildcard src/third_party/ymfm/*) src/third_party/z80.h src/main/engine/omainjointmanifest.hpp src/main/engine/omainbootschedule.hpp src/main/hwmath/xboard_cpu_continuation.hpp $(CALLER_OBJS) src/main/engine/osubpublication.cpp src/main/engine/osubpublication.hpp src/main/engine/osubprogress.cpp src/main/engine/osubprogress.inc src/main/engine/osubprogress_manifest.hpp src/main/engine/osubirq.cpp src/main/engine/osubirq.hpp src/main/hwmath/xboard_timeline.hpp src/main/hwvideo/hwsprites.cpp src/main/hwvideo/hwsprites.hpp
	$(CXX) $(CXXFLAGS) -pthread -Isrc/main tests/oracle_whole_host.cpp src/main/hwaudio/xboard_sound_cpu.cpp src/third_party/ymfm/ymfm_opm.cpp src/main/engine/osubpublication.cpp src/main/engine/osubprogress.cpp src/main/engine/osubirq.cpp $(filter src/main/hwvideo/%,$(SRCS)) $(filter-out build/oracle_obj/tests/oracle_caller.o,$(CALLER_OBJS)) -o $@

oracle-whole-host: build/oracle_whole_host
	./build/oracle_whole_host build/oracle/joint_cold.bin build/oracle/joint_cold.txt

oracle-whole-host-full: build/oracle_whole_host
	./build/oracle_whole_host build/oracle/joint_cold_full.bin build/oracle/joint_cold_full.txt

oracle-whole-host-expanded: build/oracle_whole_host
	./build/oracle_whole_host build/oracle/whole_host_expanded.bin build/oracle/whole_host_expanded.txt
.PHONY: oracle-whole-host oracle-whole-host-full oracle-whole-host-expanded

oracle-whole-host-mutations: oracle-whole-host
	python3 tools/oracle/check_whole_host_mutations.py
.PHONY: oracle-whole-host-mutations

build/whole_host_integration: $(call check_objs,tests/whole_host_integration.cpp $(filter-out src/main/main.cpp,$(SRCS)))
	$(CXX) $(CXXFLAGS) -pthread $^ -o $@

verify-whole-host: build/whole_host_integration
	./build/whole_host_integration
.PHONY: verify-whole-host

oracle-whole-host-speaker-producer: build/oracle_whole_host
	python3 tools/oracle/check_whole_host_mutations.py speaker producer
.PHONY: oracle-whole-host-speaker-producer

oracle-test: oracle-whole-host-mutations oracle-whole-host-full oracle-whole-host-expanded
verify-stage1: verify-whole-host

verify-whole-host-headless: all
	SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./afterburner2 --frames 120
.PHONY: verify-whole-host-headless
