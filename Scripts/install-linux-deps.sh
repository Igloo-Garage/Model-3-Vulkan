#!/usr/bin/env bash
# Installs the packages a Linux build of Supermodel needs. Run with the password on stdin:
#   echo 'PW' | sudo -S bash Scripts/install-linux-deps.sh
set -u
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq build-essential pkg-config cmake ninja-build \
    g++-14 libstdc++-14-dev \
    libsdl2-dev libsdl2-net-dev libglu1-mesa-dev zlib1g-dev libwebp-dev unzip curl \
    libvulkan1 mesa-vulkan-drivers fonts-wqy-microhei \
    libudev-dev libdecor-0-dev \
    libx11-dev libxext-dev libxrandr-dev libxi-dev libxcursor-dev \
    libxfixes-dev libwayland-dev libxkbcommon-dev libegl1-mesa-dev \
    libasound2-dev libpulse-dev
# g++-14 is required by the SDL3/Vulkan executable: its startup layer uses <print>, which the GCC 13
# that ships as the default on Ubuntu 24.04 does not have. When configuring by hand, pass
# -DCMAKE_C_COMPILER=gcc-14 -DCMAKE_CXX_COMPILER=g++-14, otherwise CMake picks /usr/bin/g++.
#
# libdecor-0-dev is what gives the settings window a title bar on GNOME/Wayland. SDL3 builds its own
# Wayland decorations only when libdecor is present at configure time; without it the window has no
# title bar at all and cannot be dragged or closed with the mouse.
#
# fonts-wqy-microhei is a fallback for old packages. The release ships the same face in
# Data/Assets/Fonts, so the interface no longer depends on any distribution font.
#
# The optional .zst host core is published on the target OS and needs the .NET
# 11 SDK plus clang (Native AOT):
#   sudo apt install clang zlib1g-dev
#   cmake --build build-cmake --target hostcore
echo "--- versions ---"
g++ --version | head -1
sdl2-config --version
pkg-config --modversion zlib
