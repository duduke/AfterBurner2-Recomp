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
    src/main/hwaudio/soundchip.cpp
    src/main/hwaudio/segapcm.cpp
    src/main/hwaudio/ym2151.cpp
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
