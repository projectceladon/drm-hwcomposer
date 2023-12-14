src_hwc2_device = files(
    'hwc2_device.cpp',
    'DrmHwcTwo.cpp',
    'HwcDisplayConfigs.cpp',
    'HwcDisplay.cpp',
    'HwcLayer.cpp',
)

drmhwc_hwc2_common = static_library(
    'drm_hwc2',
    src_hwc2_device,
# TODO remove hwc2 flags from common code (backends needs rework)
    cpp_args : common_cpp_flags + hwc2_cpp_flags,
    dependencies : deps,
    link_with: drmhwc_common,
    include_directories: inc_include,
)

shared_library(
    'hwcomposer.drm',
    name_prefix : '',
    cpp_args : common_cpp_flags + hwc2_cpp_flags,
    dependencies : deps,
    install : true,
    link_whole: [drmhwc_common, drmhwc_hwc2_common],
    install_dir : get_option('libdir') / 'hw',
    include_directories: inc_include,
)