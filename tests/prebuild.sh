#!/bin/bash
if [ ! -d igt-gpu-tools ]; then
    git clone https://gitlab.freedesktop.org/drm/igt-gpu-tools.git
    cd igt-gpu-tools
    meson build
    ninja -C build
    cd ..
    DIFFDIR=../../vendor/intel/external/drm-hwcomposer/tests/diff
    cd ../../../../../external/elfutils
    git apply ${DIFFDIR}/elfutils.diff
    cd ../igt-gpu-tools
    git apply ${DIFFDIR}/igt-gpu-tools.diff
    cd ../kmod
    git apply ${DIFFDIR}/kmod.diff
    cd ../../vendor/intel/external/drm-hwcomposer/tests
    git apply ./diff/Android.bp.diff
fi



