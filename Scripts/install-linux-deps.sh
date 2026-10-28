#!/usr/bin/env bash
# Installs the packages a Linux build of Supermodel needs. Run with the password on stdin:
#   echo 'PW' | sudo -S bash Scripts/install-linux-deps.sh
set -u
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq build-essential pkg-config \
    libsdl2-dev libsdl2-net-dev libglu1-mesa-dev zlib1g-dev libwebp-dev unzip curl \
    libvulkan1 mesa-vulkan-drivers fonts-wqy-microhei
# The CMake Vulkan build (Model3-Vulkan) additionally uses the bundled SDL3 and
# Vulkan headers.  On a minimal install the SDL3 build may also need its usual
# X11/Wayland/audio development headers:
#   sudo apt install libx11-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev \
#       libxfixes-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev \
#       libasound2-dev libpulse-dev
# The optional .zst host core is published on the target OS and needs the .NET
# 11 SDK plus clang (Native AOT):
#   sudo apt install clang zlib1g-dev
#   cmake --build build-cmake --target hostcore
echo "--- versions ---"
g++ --version | head -1
sdl2-config --version
pkg-config --modversion zlib
