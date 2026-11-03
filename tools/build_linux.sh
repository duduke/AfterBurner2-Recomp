#!/bin/sh
# Run inside the documented platform SDK; no host-native CPU flags.
set -eu
cd "$(dirname "$0")/.."
jobs=${AB2_JOBS:-2}
case "${AB2_LINUX_PLATFORM:-sniper-x86_64}" in
  sniper-x86_64) export CC=gcc-14 CXX=g++-14 ;;
  debian12-arm64)
    test "$(uname -m)" = aarch64
    export CC=clang-14 CXX=clang++-14
    export LDFLAGS="-fuse-ld=lld${LDFLAGS:+ $LDFLAGS}"
    export CXXFLAGS="-std=c++20 -Wall -Wextra -O2 -fuse-ld=lld" ;;
  *) echo 'Unknown AB2_LINUX_PLATFORM' >&2; exit 1 ;;
esac
deps="$PWD/build/deps"
prefix="$deps/linux"
mkdir -p "$deps"
printf '%s\n' \
  '7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68  build/deps/SDL3-3.4.16.tar.gz' \
  'fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894  build/deps/openal-soft-1.25.2.tar.gz' | sha256sum -c -
test -d "$deps/SDL3-3.4.16" || tar -xf "$deps/SDL3-3.4.16.tar.gz" -C "$deps"
test -d "$deps/openal-soft-1.25.2" || tar -xf "$deps/openal-soft-1.25.2.tar.gz" -C "$deps"
cmake -S "$deps/SDL3-3.4.16" -B build/linux-sdl -G Ninja \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF
cmake --build build/linux-sdl -j "$jobs"
cmake --install build/linux-sdl
al_cc="$CC"; al_cxx="$CXX"
al_cxxflags=${CXXFLAGS-}; al_ldflags=${LDFLAGS-}
if [ "${AB2_LINUX_PLATFORM:-sniper-x86_64}" = debian12-arm64 ]; then
  # Clang 14 cannot instantiate OpenAL's ranges/iota use with GCC 12 headers.
  # OpenAL exposes a C API and uses the same GCC 12 runtime as the application.
  al_cc=gcc-12; al_cxx=g++-12
  al_cxxflags='-std=c++20 -O2'; al_ldflags=''
fi
env CXXFLAGS="$al_cxxflags" LDFLAGS="$al_ldflags" \
  cmake -S "$deps/openal-soft-1.25.2" -B build/linux-openal -G Ninja \
  -DCMAKE_C_COMPILER="$al_cc" -DCMAKE_CXX_COMPILER="$al_cxx" \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DALSOFT_UTILS=OFF -DALSOFT_EXAMPLES=OFF -DALSOFT_TESTS=OFF \
  -DALSOFT_INSTALL_CONFIG=OFF -DALSOFT_INSTALL_HRTF_DATA=ON \
  -DALSOFT_EMBED_HRTF_DATA=ON
cmake --build build/linux-openal -j "$jobs"
cmake --install build/linux-openal
cmake -S . -B build/linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER="$CC" -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_PREFIX_PATH="$prefix" -DBUILD_TESTING=OFF \
  -DAB2_ENABLE_OPENAL=ON -DAB2_REQUIRE_IPO=ON \
  '-DCMAKE_INSTALL_RPATH=$ORIGIN/lib' -DCMAKE_BUILD_WITH_INSTALL_RPATH=ON
cmake --build build/linux -j "$jobs"
export PKG_CONFIG_PATH="$prefix/lib/pkgconfig:$prefix/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export LD_LIBRARY_PATH="$prefix/lib:$prefix/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export SDL_VIDEO_DRIVER=dummy SDL_AUDIO_DRIVER=dummy ALSOFT_DRIVERS=null
python3 tools/workflow.py run main -j "$jobs"
python3 tools/workflow.py run build/cpu_continuation_regression -j "$jobs"
./build/cpu_continuation_regression
python3 tests/test_native_rom_startup.py build/linux/afterburner2
