#!/bin/sh
# Optional dependency only; all fetched source/binaries remain ignored.
set -eu
version=1.25.2
root=$(CDPATH= cd -- "$(dirname "$0")/../.." && pwd)
cd "$root"
mkdir -p build/deps
archive="build/deps/openal-soft-$version.tar.gz"
if [ ! -f "$archive" ]; then
    curl -L --fail "https://codeload.github.com/kcat/openal-soft/tar.gz/refs/tags/$version" -o "$archive"
fi
# Record the upstream tarball before building; no ROM/assets are fetched.
expected=fb27e5839aa11f0e5b9d33756965291fad5d6909ab928ea1f796f4a1a6877894
printf '%s  %s\n' "$expected" "$archive" | shasum -a 256 -c -
tar -xzf "$archive" -C build/deps
set --
# Clang 21/CoreAudio callback annotations currently fail upstream's optional
# function-effects warning gate. Disable only that feature probe on Darwin.
if [ "$(uname -s)" = Darwin ]; then set -- -DHAVE_WFUNCTION_EFFECTS=FALSE; fi
cmake -S "build/deps/openal-soft-$version" -B build/deps/openal-build \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$root/build/deps/openal" \
    -DALSOFT_EXAMPLES=OFF -DALSOFT_UTILS=OFF -DALSOFT_TESTS=OFF -DALSOFT_INSTALL=ON "$@"
cmake --build build/deps/openal-build --parallel 4
cmake --install build/deps/openal-build

notices="$root/build/deps/openal/share/licenses/openal-soft"
mkdir -p "$notices"
cp "build/deps/openal-soft-$version/COPYING" "$notices/LGPL.txt"
cp "build/deps/openal-soft-$version/BSD-3Clause" "$notices/BSD-3Clause.txt"
cp "build/deps/openal-soft-$version/LICENSE-pffft" "$notices/pffft.txt"
cp "build/deps/openal-soft-$version/fmt-11.2.0/LICENSE" "$notices/fmt.txt"
cp "build/deps/openal-soft-$version/gsl/LICENSE" "$notices/gsl.txt"
cp docs/THIRD-PARTY-NOTICES.md "$notices/project-notices.md"
