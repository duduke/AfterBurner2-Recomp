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
	src/main/engine/oroad.cpp \
	src/main/engine/opalette.cpp \
	src/main/engine/olevelobjs.cpp \
	src/main/engine/oengine.cpp

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

ORACLE_SRCS = src/main/romloader.cpp src/main/roms.cpp src/main/engine/subbus.cpp
VIDEO_SRCS = src/main/romloader.cpp src/main/roms.cpp $(filter src/main/hwvideo/%,$(SRCS)) src/main/engine/osprites.cpp

oracle-test:
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_subsprites.cpp src/main/engine/osubsprites.cpp $(ORACLE_SRCS) -o build/oracle_subsprites
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_subroad.cpp src/main/engine/osubroad.cpp $(ORACLE_SRCS) -o build/oracle_subroad
	$(CXX) $(CXXFLAGS) -Isrc/main tests/oracle_video.cpp $(VIDEO_SRCS) -o build/oracle_video
	./build/oracle_subsprites
	./build/oracle_subroad
	./build/oracle_video build/oracle/frames.bin
	./build/oracle_video build/oracle/frames_nosprites.bin

.PHONY: oracle-capture oracle-test
