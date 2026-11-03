FROM docker.io/library/debian@sha256:7ccb8b5b60c7654f5b795d94ee7d751cd0811500803f0b09cefcd555e755a55a
# Frozen Debian 12 package inputs; native ARM64 compiler, generic ARMv8 target.
RUN rm /etc/apt/sources.list.d/debian.sources && \
    printf '%s\n' \
      'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian/20261103T000000Z bookworm main' \
      'deb [check-valid-until=no] http://snapshot.debian.org/archive/debian-security/20261103T000000Z bookworm-security main' \
      > /etc/apt/sources.list && \
    apt-get -o Acquire::Check-Valid-Until=false update && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      build-essential clang-14 lld-14 llvm-14 cmake ninja-build pkg-config python3 binutils git \
      ca-certificates curl libasound2-dev libpulse-dev libpipewire-0.3-dev \
      libdbus-1-dev libudev-dev libwayland-dev libxkbcommon-dev wayland-protocols \
      libdecor-0-dev libx11-dev libxext-dev libxrandr-dev libxcursor-dev \
      libxfixes-dev libxi-dev libxss-dev libxtst-dev libxrender-dev libdrm-dev \
      libgbm-dev libegl1-mesa-dev libgl1-mesa-dev libgles2-mesa-dev \
      libvulkan-dev mesa-vulkan-drivers && \
    dpkg-query -W > /usr/local/share/ab2-sdk-packages.txt && \
    rm -rf /var/lib/apt/lists/*
