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
	src/main/hwaudio/soundchip.cpp \
	src/main/hwaudio/segapcm.cpp \
	src/main/hwaudio/ym2151.cpp \
	src/main/engine/ohud.cpp \
	src/main/engine/osprites.cpp \
	src/main/engine/olevelobjs.cpp \
	src/main/engine/oengine.cpp

# Verified road/scenery and shared bus; ADC awaits the flight/input port.
SRCS += src/main/engine/omaininput.cpp src/main/engine/omainplayer.cpp src/main/engine/omainworld.cpp src/main/engine/omainweapons.cpp src/main/engine/omainobjects.cpp src/main/engine/omainsound.cpp src/main/engine/omainpalette.cpp src/main/engine/omainframe.cpp src/main/engine/omainsprites.cpp src/main/engine/osubsprites.cpp src/main/engine/subbus.cpp src/main/engine/mainbus.cpp src/main/engine/omainadc.cpp src/main/engine/oscenery.cpp src/main/engine/ostagesetup.cpp src/main/engine/osubroad.cpp

OBJS = $(SRCS:.cpp=.o)
DEPS = $(OBJS:.o=.d)
-include $(DEPS)
TARGET = afterburner2

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $(OBJS) -o $(TARGET) $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

.PHONY: all clean

# ROM-free checks exercise the real descriptor/scaler and engine code.
TEST_SRCS = tests/engine_regression.cpp $(filter src/main/engine/% src/main/hwvideo/%,$(SRCS))

test:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TEST_SRCS) -o build/engine_regression
	./build/engine_regression
	$(CXX) $(CXXFLAGS) -Isrc/main tests/mainbus_regression.cpp src/main/engine/mainbus.cpp src/main/engine/subbus.cpp -o build/mainbus_regression
	./build/mainbus_regression

.PHONY: test

# Optional visual check; requires the user's ROMs in roms/.
render-scene:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tools/render_scene.cpp src/main/romloader.cpp src/main/roms.cpp $(filter src/main/engine/% src/main/hwvideo/%,$(SRCS)) -o build/render_scene

.PHONY: render-scene

# Optional integration suite: reads the user's CRC-verified ROMs.
verify-stage1:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/stage1_integration.cpp $(filter-out src/main/main.cpp,$(SRCS)) -o build/stage1_integration
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
	$(MAKE) oracle-capture-scenery
	$(MAKE) oracle-capture-road-setup

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
HANDLER_SRCS = src/main/engine/omainobjects.cpp src/main/engine/omaininput.cpp src/main/engine/omainplayer.cpp src/main/engine/omainworld.cpp src/main/engine/omainweapons.cpp src/main/engine/omainsound.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS)
HANDLER_CAPTURES = input input_gameplay input_directed player_init player player_directed player_parts player_parts_gameplay player_parts_directed reload reload_directed weapon_control weapon_control_directed world world_directed buttons buttons_directed projectiles projectiles_directed missiles missiles_directed effects_directed collision_directed enemy_weapons_directed enemy_parts_directed enemy_effects enemy_effects_gameplay

oracle-capture-handlers:
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

oracle-handlers:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_handlers.cpp $(HANDLER_SRCS) -o build/oracle_handlers
	@for capture in $(HANDLER_CAPTURES); do ./build/oracle_handlers build/oracle/$$capture.bin || exit 1; done
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_adc_timing.cpp -o build/oracle_adc_timing
	./build/oracle_adc_timing build/oracle/adc_timing.bin
	./build/oracle_adc_timing build/oracle/adc_timing_directed.bin

ORACLE_SRCS = src/main/romloader.cpp src/main/roms.cpp src/main/engine/subbus.cpp
VIDEO_SRCS = src/main/romloader.cpp src/main/roms.cpp $(filter src/main/hwvideo/%,$(SRCS)) src/main/engine/mainbus.cpp src/main/engine/subbus.cpp src/main/engine/omainsprites.cpp src/main/engine/osubsprites.cpp

oracle-test: oracle-handlers
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_objects.cpp src/main/engine/omainobjects.cpp src/main/engine/omainsound.cpp src/main/engine/mainbus.cpp $(ORACLE_SRCS) -o build/oracle_objects
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
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_main.cpp src/main/engine/mainbus.cpp src/main/engine/omainadc.cpp $(ORACLE_SRCS) -o build/oracle_main
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

.PHONY: oracle-capture-handlers oracle-handlers
