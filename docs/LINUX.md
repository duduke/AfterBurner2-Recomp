# Linux x86-64 / ARM64 previews

[Preview 7 downloads](https://github.com/duduke/AfterBurner2-Recomp/releases/tag/v0.1.0-preview.7)
include both game architectures with the Vulkan Slang runtime already bundled,
runtime covered-source/notice archives, and SHA-256 files. Full project source companions remain local
and in the private development release, following the existing public-download
policy.

The x86-64 Linux packaging workflow targets Valve's Steam Linux Runtime 3 **sniper**
SDK, not the build host's Ubuntu libraries. The pinned SDK is
`registry.gitlab.steamos.cloud/steamrt/sniper/sdk:3.0.20261021.266025`.
The resolved image digest is
`sha256:bb30053d3cd19d35c76ae69b2790dbaa1430448006f8240cb6d7e723f2d1940e`.
SDL3 3.4.16 and OpenAL Soft 1.25.2 source archives are checksum verified by
`tools/build_linux.sh`. The SDK's GCC 14 toolchain supplies C++20 support
(the default GCC 10 lacks the required atomic wait headers). The package checks every bundled ELF for a maximum
glibc requirement of 2.31. Graphics/window-system/audio drivers come from the
runtime or host; this is not a fully static executable.

This is a preview workflow. A successful SDK build or Ubuntu launch does not
establish physical Steam Deck performance, input, audio or suspend acceptance.
Linux uses the retained worker-thread CPU continuation backend. RetroArch Slang
shaders use the native SDL Vulkan renderer and GPU sprite composition in CMake
builds. The release bundles the runtime; shader packs remain external downloads.
No engine changes are required by these build and packaging tools.

## RetroArch Slang / native Vulkan composition

Both architectures use the same Vulkan compositor as Windows when a `.slangp`
preset is active. The ordinary frontend continues to use SDL GPU. Shader mode
shares SDL's Vulkan device/queue with librashader and the generated sprite/scene
compute kernels, including ScaleFX, priority protection and effect opacity.
Menus draw after the shader. Paused redraws reuse output; switching presets or
renderers preserves the game session and rolls back a failed load.

Install Vulkan headers in the build SDK (`libvulkan-dev` on Debian), then build
with CMake. The portable Make frontend still uses the stub shader host.
To build the optional Vulkan-only runtime, install/select Rust **1.99.1** with a
minimal rustup profile inside the matching SDK, put its Cargo tools on `PATH`,
and run:

```sh
sh tools/deps/build_librashader_linux.sh
```

The helper verifies the pinned librashader 0.12.0 source archive, uses its locked
Cargo dependencies and records the runtime hash/toolchain in
`build/deps/librashader/build-manifest.json`. It needs a C/C++ compiler, CMake,
Python, curl and network access for Cargo dependencies. Build in sniper for
x86-64 or the Debian 12 ARM64 SDK to preserve the corresponding glibc floor.
Do not use an Ubuntu-host-built runtime in a sniper release.

```sh
./build/linux/afterburner2 --shader-runtime "$PWD/build/deps/librashader/liblibrashader_capi.so" \
  --shader-preset /absolute/path/to/shaders_slang/crt/crt-geom.slangp
```

The **20261103.1 Preview 7 game archives already bundle librashader** in `lib/`.
No separate runtime installation or runtime path setting is needed.

1. Download [shaders_slang.zip](https://buildbot.libretro.com/assets/frontend/shaders_slang.zip) from the
   [official Libretro frontend assets directory](https://buildbot.libretro.com/assets/frontend/).
2. Extract the entire ZIP to a folder you can keep. Preserve all subfolders,
   shader includes and lookup textures; individual presets depend on them.
3. Open **Settings → CRT and Shaders → Shader Preset**, then select a `.slangp`
   preset from the extracted pack. Successful selection activates RetroArch mode.

The retained `librashader-0.12.0-Linux-{x86_64,arm64}-Vulkan.tar.gz` downloads
provide corresponding upstream/Cargo sources and complete notices for the bundled
library. End users do not need to install these archives. The game keeps the
library replaceable in `lib/`; source builds may still use an absolute
`--shader-runtime` path or `build/deps/librashader/`. ABI 2 / API 5 is required.
HDR, legacy GLSL/Cg and first-input mipmap generation remain unsupported.
See the [runtime notice](../src/third_party/librashader/NOTICE.md).

The affected checks are `verify-native-slang`, `verify-native-slang-mutations`,
`verify-native-vulkan-sprite-filters`, `verify-native-vulkan-sprite-mutations`
and `verify-integrated-shader-main` in a CMake build configured with
`BUILD_TESTING=ON` and `STAGE1_ROM_TESTS=ON`. Run them on an unlocked desktop
with the runtime installed; the integrated main checks additionally need private
ROMs. On slow VMs set `AB2_SHADER_MAIN_TIMEOUT_SECONDS=600` (default 90,
maximum 900) to retain the physical cold boot and every live assertion within a
bounded longer run. Retain all fixtures and mutation logs. The mutation tools
accept `--compiler`, `--sdl-prefix` and `--vulkan-include` when the SDK driver
cannot present and a host compiler must build probes against the SDK-built SDL.
Record that compiler separately from the release compiler.

## Build on an x86-64 Linux host

Install Podman (or use equivalent Docker commands), then pull the pinned image.
Record `podman image inspect` output alongside the build logs to retain the
resolved image digest. The image can also run under x86-64 emulation, but such
results are not native hardware performance evidence.

```sh
AB2_SDK=registry.gitlab.steamos.cloud/steamrt/sniper/sdk@sha256:bb30053d3cd19d35c76ae69b2790dbaa1430448006f8240cb6d7e723f2d1940e
podman pull "$AB2_SDK"
python3 tools/snapshot_linux.py
```

The snapshot captures tracked working files plus the Linux tools and this
document. Its JSON manifest records the base commit, working-tree status and
every file hash, so an uncommitted preview is explicitly identified. Keep the
archive and manifest outside the extracted build source. It excludes ignored
ROMs/captures/build products. For a public release, commit the intended source
first. Transfer the archive, manifest and pinned dependency source archives to
the host, then extract into a new workspace. Put dependency archives in
`build/deps/`. Supply private ROMs in `roms/` for the main checks; do not put
them into the source snapshot or release.

From that extracted source directory:

```sh
podman run --rm --userns=keep-id -v "$PWD:/work" -w /work \
  "$AB2_SDK" \
  sh tools/build_linux.sh > build-linux.log 2>&1
```

The script builds shared dependencies, a Release/LTO executable without
`-march=native` or fast-math, the complete `main` profile, and the focused CPU
continuation regression and ROM ZIP startup checks. `AB2_JOBS` defaults to 2 to limit peak compiler memory.
Keep the logs and all generated fixtures. Use the [validation cadence](ROADMAP.md#working-on-the-remaining-tasks-read-this-first)
for additional changed functionality. A main pass is not whole-host fidelity.

## Native ARM64 build on Debian

The ARM64 workflow uses Debian 12 / glibc 2.36, Clang/LLD 14 with the GCC 12
standard library, and the same pinned
SDL3/OpenAL source archives. It is a separate ARM64 executable, not a Steam
Deck binary. `tools/linux-arm64.Containerfile` pins the ARM64 Debian base image
and Debian package repositories at `20261103T000000Z`; keep the recipe, resolved
image ID and package inventory with the build evidence.
The application and checks use Clang/LLD 14; OpenAL uses GCC 12 because Clang 14
cannot instantiate its C++20 ranges code with GCC 12 headers. Both share the
same C++ runtime, and OpenAL's public boundary is C. No upstream source patch
or engine change is involved.

On an ARM64 Linux host with Podman, use an empty SDK build context so ignored
ROMs, captures and build outputs are never sent as container build inputs:

```sh
mkdir -p build/linux-arm64-sdk-context
podman build -f "$PWD/tools/linux-arm64.Containerfile" \
  -t localhost/ab2-arm64-sdk:bookworm-20261103-clang14 build/linux-arm64-sdk-context
python3 tools/snapshot_linux.py --output build/linux-arm64-source.tar.gz
```

Extract the snapshot into a new source workspace, supply dependency archives
under `build/deps/` and private ROMs under `roms/`, and run:

```sh
podman run --rm --userns=keep-id -v "$PWD:/work" -w /work \
  -e AB2_LINUX_PLATFORM=debian12-arm64 \
  localhost/ab2-arm64-sdk:bookworm-20261103-clang14 \
  sh tools/build_linux.sh > build-linux.log 2>&1
```

Record `podman image inspect`, the recipe SHA-256, and
`/usr/local/share/ab2-sdk-packages.txt` from the SDK in an environment JSON
manifest. The ARM64 packager requires `recipe_sha256` and `image_id` and
retains the supplied manifest in its runtime metadata and source companion:

```sh
LD_LIBRARY_PATH="$PWD/build/deps/linux/lib:$PWD/build/deps/linux/lib64" \
  python3 tools/package_linux.py v0.1.0-preview.7-20261103.1 \
  --librashader-bundle build/releases/librashader-0.12.0-Linux-arm64-Vulkan.tar.gz \
  --platform debian12-arm64 --environment /inputs/arm64-environment.json \
  --snapshot /inputs/linux-arm64-source.tar.gz \
  --manifest /inputs/linux-arm64-source.json
```

Run packaging inside the same ARM64 SDK with `/inputs` mounted read-only.
Every bundled ELF must be AArch64-compatible and require no newer glibc than
2.36. ARM64 first-run preferences use a window, Original viewport and nearest
scaling. Existing settings are preserved. Fullscreen and widescreen remain
available in the Display menu. Linux currently uses the worker-thread backend
on ARM64 as well; phone/macOS cooperative-backend performance does not establish
Linux ARM64 performance. Run native main, continuation, ROM startup and package
checks, and independent retained MAME replays, before claiming finite acceptance.

## Package and verify

Run the packager inside the same SDK, with the dependency library directory in
`LD_LIBRARY_PATH`, and pass the archive/manifest from the source snapshot:

```sh
LD_LIBRARY_PATH="$PWD/build/deps/linux/lib:$PWD/build/deps/linux/lib64" \
  python3 tools/package_linux.py v0.1.0-preview.7-20261103.1 \
  --librashader-bundle build/releases/librashader-0.12.0-Linux-x86_64-Vulkan.tar.gz \
  --snapshot /inputs/linux-source.tar.gz --manifest /inputs/linux-source.json
```

Mount `/inputs` read-only into the container. Packaging refuses changed source
hashes and refuses to replace existing output directories. It writes runtime
and source-companion `.tar.gz` files and SHA-256 sidecars in `build/releases/`.
Pass the matching ARM64 runtime/source archive for ARM64 packaging. The packager
requires the checksum-pinned runtime/source bundle, verifies its manifest and
notices, copies the library into `lib/` and checks its ELF architecture/glibc
requirements with every other bundled library. The companion contains the exact
project snapshot and pinned dependency/runtime-source archives. Runtime `BUILD.json` retains source identity and glibc requirements;
`SHA256SUMS` inventories its files. Keep shared libraries replaceable.

Before distribution, extract into a separate directory and verify the archive
checksum and internal `SHA256SUMS`, executable permissions, library resolution,
notices and absence of ROMs/captures/preferences. Launch from an unrelated
working directory with temporary `XDG_DATA_HOME` and explicit private `--roms`.
Check headless output in the SDK and on the host, then use an active desktop to
check X11/Wayland, audio and fullscreen behavior. Record performance scope and
remaining physical acceptance explicitly.

## Steam Deck installation

Extract the runtime in Desktop Mode. Add `Launch.sh` as a Non-Steam Game and
select a Gamepad controller template. Launch natively in Gaming Mode with no
Proton override. Keep `lib/` beside the executable. New settings start in
fullscreen, 16:10, linear scaling and corrected ground; saved settings survive.

Put your own `aburner2.zip` in `roms/`, select it with the initial picker in
Desktop Mode, or supply `--roms /absolute/path/aburner2.zip`. See the
[ROM instructions](../roms/README.md). Preferences and controller bindings use
`$XDG_DATA_HOME/AfterBurner2-Recomp/AfterBurner2`, defaulting to
`~/.local/share/AfterBurner2-Recomp/AfterBurner2`.

If using Steam's runtime compatibility option, select Steam Linux Runtime 3
(sniper). Physical Deck controller feel, reconnect, suspend/resume, frame pacing
and audio stability remain separate hardware checks.

## Recorded x86-64 validation (2026-11-03)

The Ubuntu 26.04.1 VMware host built the local preview and passed the complete
`main` profile, worker-thread continuation regression, ROM ZIP startup checks,
and the retained independent MAME cold replay: **121 of 121 frames identical,
0 skipped**. Extracted SDK/Ubuntu software images match byte-for-byte; native
X11 and Wayland/Vulkan launches pass. Runtime and source-companion archive and
internal checksums were verified after retrieval to the Mac.

A quiet 180-frame Stage 7 Wayland sample measures **11.7 FPS** on this VM,
with audio underruns. This is a functional preview, not real-time VM playback
or physical Steam Deck performance acceptance. See the
[exact scope, timings and evidence](ENGINE_NOTES.md#linux-x86-64--steam-deck-packaging-2026-11-03).

## Recorded ARM64 validation (2026-11-03)

The Debian forky/sid ARM64 QEMU guest built the local Debian 12-targeted preview
and passed the complete `main` profile, worker-thread continuation regression,
ROM ZIP startup checks and the retained independent MAME cold replay:
**121 of 121 frames identical, 0 skipped**. Extracted Debian 12/host software
images match byte-for-byte. Unlocked X11 and Wayland/Vulkan launches pass;
archive/internal/source hashes were verified after retrieval to the Mac.

A quiet 180-frame Stage 7 sample in windowed Original view measures **5.76 FPS**
on this VM, with playback underruns. It is a functional preview, not real-time
VM playback or physical ARM64 acceptance. The package requires **glibc 2.36**
or newer. See the [ARM64 scope and evidence](ENGINE_NOTES.md#linux-arm64-preview-2026-11-03).
