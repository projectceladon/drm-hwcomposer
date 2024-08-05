
src_hwc3 = files(
    'ComposerClient.cpp',
    'Composer.cpp',
    'DrmHwcThree.cpp',
    'service.cpp',
    'ComposerResources.cpp',
    'Utils.cpp',
)

executable(
    'android.hardware.composer.hwc3-service.drm',
    src_hwc3,
    cpp_args : common_cpp_flags + hwc2_cpp_flags,
    dependencies : deps,
    install : true,
    link_with: [drmhwc_common, drmhwc_hwc2_common],
    install_dir : get_option('bindir') / 'hw',
    include_directories: inc_include,
)

configure_file(
  input: 'hwc3-drm.rc',
  output: '@PLAINNAME@',
  copy: true,
  install_dir: get_option('sysconfdir') / 'init',
)

configure_file(
  input: 'hwc3-drm.xml',
  output: '@PLAINNAME@',
  copy: true,
  install_dir: get_option('sysconfdir') / 'vintf' / 'manifest',
)
