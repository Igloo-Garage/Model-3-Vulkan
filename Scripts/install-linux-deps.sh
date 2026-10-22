#!/usr/bin/env bash
# Installs the packages a Linux build of Supermodel needs. Run with the password on stdin:
#   echo 'PW' | sudo -S bash Scripts/install-linux-deps.sh
set -u
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq build-essential pkg-config \
    libsdl2-dev libsdl2-net-dev libglu1-mesa-dev zlib1g-dev libwebp-dev unzip curl
echo "--- versions ---"
g++ --version | head -1
sdl2-config --version
pkg-config --modversion zlib
