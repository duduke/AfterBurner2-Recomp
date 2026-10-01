cmake_minimum_required(VERSION 3.20)
project(afterburner2-recomp CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

find_package(SDL2 REQUIRED)

add_executable(afterburner2
    src/main/main.cpp
    src/main/romloader.cpp
    src/main/roms.cpp
    src/main/hwvideo/hwroad.cpp
    src/main/hwvideo/hwsprites.cpp
    src/main/hwvideo/hwtiles.cpp
    src/main/hwvideo/hwpalette.cpp
    src/main/hwvideo/screen.cpp
    src/main/hwaudio/arcade_audio.cpp
    src/main/hwaudio/soundchip.cpp
    src/main/hwaudio/segapcm.cpp
    src/main/hwaudio/ym2151.cpp
    src/main/engine/ohud.cpp
    src/main/engine/osprites.cpp
    src/main/engine/oroad.cpp
    src/main/engine/opalette.cpp
    src/main/engine/olevelobjs.cpp
    src/main/engine/oengine.cpp
)

target_include_directories(afterburner2 PRIVATE src/main ${SDL2_INCLUDE_DIRS})
target_link_libraries(afterburner2 PRIVATE ${SDL2_LIBRARIES})

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(afterburner2 PRIVATE -Wall -Wextra)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(engine_regression
        tests/engine_regression.cpp
        src/main/engine/ohud.cpp
        src/main/engine/osprites.cpp
        src/main/engine/oroad.cpp
        src/main/engine/opalette.cpp
        src/main/engine/olevelobjs.cpp
        src/main/engine/oengine.cpp
        src/main/hwvideo/hwroad.cpp
        src/main/hwvideo/hwsprites.cpp
        src/main/hwvideo/hwtiles.cpp
        src/main/hwvideo/hwpalette.cpp
        src/main/hwvideo/screen.cpp
    )
    target_include_directories(engine_regression PRIVATE src/main)
    add_test(NAME engine_regression COMMAND engine_regression)
endif()

option(ORACLE_TESTS "Enable tests against MAME captures in build/oracle (see make oracle-capture)" OFF)
if(BUILD_TESTING AND ORACLE_TESTS)
    add_executable(oracle_subsprites tests/oracle_subsprites.cpp
        src/main/romloader.cpp src/main/roms.cpp
        src/main/engine/subbus.cpp src/main/engine/osubsprites.cpp)
    target_include_directories(oracle_subsprites PRIVATE src/main)
    add_test(NAME oracle_subsprites COMMAND oracle_subsprites)
    set_tests_properties(oracle_subsprites PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_subroad tests/oracle_subroad.cpp
        src/main/romloader.cpp src/main/roms.cpp
        src/main/engine/subbus.cpp src/main/engine/osubroad.cpp)
    target_include_directories(oracle_subroad PRIVATE src/main)
    add_test(NAME oracle_subroad COMMAND oracle_subroad)
    set_tests_properties(oracle_subroad PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    add_executable(oracle_video tests/oracle_video.cpp
        src/main/romloader.cpp src/main/roms.cpp
        src/main/hwvideo/hwroad.cpp src/main/hwvideo/hwsprites.cpp src/main/hwvideo/hwtiles.cpp
        src/main/hwvideo/hwpalette.cpp src/main/hwvideo/screen.cpp src/main/engine/osprites.cpp)
    target_include_directories(oracle_video PRIVATE src/main)
    add_test(NAME oracle_video COMMAND oracle_video build/oracle/frames.bin)
    add_test(NAME oracle_video_nosprites COMMAND oracle_video build/oracle/frames_nosprites.bin)
    set_tests_properties(oracle_video oracle_video_nosprites PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()

option(STAGE1_ROM_TESTS "Enable tests requiring the user's Stage 1 ROM set" OFF)
if(BUILD_TESTING AND STAGE1_ROM_TESTS)
    get_target_property(STAGE1_SOURCES afterburner2 SOURCES)
    list(REMOVE_ITEM STAGE1_SOURCES src/main/main.cpp)
    add_executable(stage1_integration tests/stage1_integration.cpp ${STAGE1_SOURCES})
    target_include_directories(stage1_integration PRIVATE src/main)
    add_test(NAME stage1_integration COMMAND stage1_integration)
    set_tests_properties(stage1_integration PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
endif()
