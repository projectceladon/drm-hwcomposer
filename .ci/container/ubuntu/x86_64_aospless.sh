#!/usr/bin/env bash

# For any changes to this file to take effect, the UBUNTU_HWC_TAG has
# to be bumped to generate a new image.

set -ex

DEPS=(
    clang
    llvm
    clang-19
    clang-tidy-19
    clang-format-19
    ca-certificates
    git
    libdrm-dev
    blueprint-tools
    libgtest-dev
    make
    python3
    wget
    sudo
    rsync
    lld
    pkg-config
    ninja-build
    meson
    python3-mako
    python3-jinja2
    python3-ply
    python3-yaml
    wget
    gnupg
    xz-utils
)

export DEBIAN_FRONTEND=noninteractive

apt-get update
apt-get upgrade -y

apt-get install -y --no-remove --no-install-recommends "${DEPS[@]}"

wget https://gitlab.freedesktop.org/-/project/5/uploads/cafa930dad28acf7ee44d50101d5e8f0/aospless_drm_hwcomposer_arm64.tar.xz

sha256sum aospless_drm_hwcomposer_arm64.tar.xz
if echo f792b1140861112f80c8a3a22e1af8e3eccf4910fe4449705e62d2032b713bf9 aospless_drm_hwcomposer_arm64.tar.xz | sha256sum --check; then
    tar --no-same-owner -xf aospless_drm_hwcomposer_arm64.tar.xz -C /
else
    echo "Tar file check failed"
    exit 1
fi
