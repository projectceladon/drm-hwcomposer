# GitHub Copilot Instructions

## Copilot Scope And Priority

This file is the GitHub Copilot entry point for `vendor/intel/external/drm-hwcomposer`. Paths are relative to this repo unless explicitly noted.

Instruction priority:

- Follow explicit user instructions first.
- Follow this file for Copilot guidance inside this repo.
- Use `AGENT.md` in this repo for OpenCode/general-agent companion guidance.
- Use the workspace root `.github/copilot-instructions.md` for full Android/Celadon architecture and cross-repo rules.
- Preserve existing Android, AOSP, Intel, and upstream style when local guidance is absent.

Do not edit generated files, prebuilts, release snapshots, or source/build/test files when the task is documentation-only. Do not commit unless explicitly requested.


## Repo Identity

- Name: `icssp.os.android.drm-hwcomposer`.
- Path: `vendor/intel/external/drm-hwcomposer`.
- Remote: `intel-sandbox`.
- Revision: `local/android/16/main`.
- Source manifest: `.repo/manifests/include/local/bsp-oss.xml`.
- Manifest line: `<project name="icssp.os.android.drm-hwcomposer" path="vendor/intel/external/drm-hwcomposer" remote="intel-sandbox" revision="local/android/16/main" />`.

## Mission

- This repo contains the DRM/KMS hardware composer implementation used by Intel Android products.
- It provides DRM composition planning, buffer import, HWC2 support code, the HWC3 composer service, APEX packaging, and tests.
- Android consumes the HWC3 service through VINTF and init as `android.hardware.composer.hwc3-service.drm`.
- The repo also keeps upstream-style Meson and Makefile support, but Android integration is Soong-based.

## Local Map

- `Android.bp`: Android modules, filegroups, HWC3 service, APEX, rc, VINTF, and support libraries.
- `README.md`: upstream contribution and formatting notes.
- `meson.build`: non-Android Meson build entry.
- `Makefile`: non-Android make helper.
- `.clang-format`: formatting style.
- `.clang-tidy`: lint configuration.
- `backend/`: backend selection and client composition logic.
- `bufferinfo/`: buffer metadata import helpers.
- `bufferinfo/legacy/`: legacy gralloc platform buffer import implementations.
- `compositor/`: KMS composition plan and flattening control.
- `drm/`: DRM device, connector, CRTC, encoder, plane, property, mode, pipeline, vsync, uevent, HDR, and resource management.
- `hwc2_device/`: HWC2 display, config, layer, service, and device glue.
- `hwc3/`: HWC3 AIDL composer service, client, utilities, rc, VINTF XML, APEX manifest, and file contexts.
- `libhwcservice/`: service support library and tests.
- `tests/Android.bp`: build test and `hwc-drm-uevent-print` utility.
- `utils/`: file descriptor, property, EDID, and Intel blit helpers.

## Build And Module Inventory

- `drm_hwcomposer_headers`: exported vendor headers.
- `hwcomposer.drm_defaults`: shared defaults for DRM HWC modules.
- `drm_hwcomposer_common`: filegroup for common DRM/backend/HWC2 sources.
- `drm_hwcomposer_hwc3`: filegroup for HWC3 service sources.
- `drm_hwcomposer_service`: service entry source filegroup.
- `android.hardware.composer.hwc3-service.drm`: main vendor HWC3 service binary installed under `hw`.
- `drm_hwcomposer_hwc3_apex_vintf`: VINTF XML prebuilt for APEX packaging.
- `drm_hwcomposer_hwc3_apex_init_rc`: generated APEX init rc prebuilt.
- `gen-drm_hwcomposer_hwc3_apex_init_rc`: rewrites `/vendor/bin/` to APEX binary path.
- `com.android.hardware.graphics.composer.drm_hwcomposer`: vendor APEX containing the HWC3 service.
- `libhwcservice`: HWC service support library under `libhwcservice/`.
- `libhwcservicelib`: shared library including common HWC sources and service glue.
- `hwcomposer.filegroups_build_test`: test filegroup build check.
- `hwc-drm-uevent-print`: uevent test/diagnostic utility.
- Platform legacy filegroups include `drm_hwcomposer_platformimagination`, `drm_hwcomposer_platformhisi`, `drm_hwcomposer_platformmeson`, and `drm_hwcomposer_platformmediatek`.

## Edit Workflow

- Start in `drm/` for KMS object handling, connectors, modes, planes, CRTCs, properties, vsync, and uevents.
- Start in `compositor/` for composition planning and flattening behavior.
- Start in `backend/` for client/backend selection issues.
- Start in `bufferinfo/` for gralloc metadata, mapper, and import failures.
- Start in `hwc3/` for AIDL service, composer client, VINTF, rc, or APEX packaging changes.
- Start in `hwc2_device/` for HWC2 compatibility/service behavior.
- Check `Android.bp` filegroups before adding/removing sources; many modules reference shared filegroups.
- Treat generated `hwc3-drm.apex.rc` as build output from `gen-drm_hwcomposer_hwc3_apex_init_rc`; edit `hwc3/hwc3-drm.rc` instead.
- Use repository `.clang-format`; README suggests `git diff | clang-format-diff-19 -p 1 -style=file`.
- Keep RAII ownership for file descriptors, fences, DRM objects, and temporary resources.
- Do not change VINTF service names, APEX paths, or rc service names without validating product manifests.
- Avoid slow I/O or long locks in hot composition, present, and vsync paths.

## Cross-Product Safety

- DRM properties and plane capabilities vary by GPU, kernel, display output, and firmware.
- Headless, single-display, multi-display, hotplug, and mode-switch products exercise different code paths.
- Android Soong and Meson builds do not cover exactly the same sources.
- Mapper metadata behavior depends on gralloc/minigbm versions.
- HWC3 service registration depends on binary path, rc, VINTF, and APEX packaging matching exactly.
- Legacy bufferinfo filegroups may still matter for non-mainline variants.
- Display regressions can break boot animation, Settings, camera preview, media playback, and CTS graphics tests.

## Validation Matrix

- Build-only service: `source build/envsetup.sh && lunch <intel-product> && m android.hardware.composer.hwc3-service.drm`.
- Build-only support: `m libhwcservice libhwcservicelib` when service support code changes.
- Build-only APEX: `m com.android.hardware.graphics.composer.drm_hwcomposer` for packaging, rc, VINTF, or file-context changes.
- Build-only tests: `m hwcomposer.filegroups_build_test hwc-drm-uevent-print` for filegroup or uevent changes.
- Integration: build product image and confirm the HWC service is selected by product configuration.
- Runtime service: `adb shell dumpsys SurfaceFlinger` and verify composer service state.
- Runtime VINTF: `adb shell lshal` or service manager queries for composer3 default instance.
- Runtime display: boot animation, launcher, rotation, resolution switch, suspend/resume, and hotplug.
- Runtime media: video playback and camera preview to exercise buffers and fences.
- Runtime multi-display: attach/detach external display when hardware supports it.
- Targeted diagnostics: run `hwc-drm-uevent-print` if uevent handling changed.
- Targeted regression: run HWC/graphics VTS and CTS display tests for service contract changes.

## Debugging Tips

- Use `adb shell dumpsys SurfaceFlinger` for HWC service, composition, display, and layer state.
- Use `adb logcat | grep -iE 'hwcomposer|drm|composer|surfaceflinger|kms'` for framework and HWC logs.
- Use `adb shell ps -A | grep composer` to verify the service process.
- Verify vendor binary packaging with `adb shell ls -l /vendor/bin/hw/android.hardware.composer.hwc3-service.drm`.
- Verify APEX packaging with `adb shell ls -l /apex/com.android.hardware.graphics.composer/bin/hw/` when the APEX is used.
- Inspect DRM topology with `adb shell ls -R /sys/class/drm`.
- Inspect device nodes with `adb shell ls -l /dev/dri`.
- Use `adb shell dmesg | grep -iE 'drm|i915|display|hdmi|dp'` for kernel display errors.
- Use `adb shell getprop | grep -iE 'hwc|composer|display|drm'` for runtime properties.
- Black frames and jank often point to fence ownership, buffer import, or plane assignment issues.

## Safety, Security, And Release Risks

- HWC processes graphics buffers and sync fences from other processes; validate handles and metadata.
- DRM object lifetimes and fd ownership must be exact to avoid leaks, use-after-close, or display hangs.
- Incorrect plane assignment can expose stale content or produce black frames.
- APEX, VINTF, and rc mismatches can leave the device without hardware composition.
- Display hotplug and suspend/resume bugs are high-visibility release blockers.
- Excessive logging in present/vsync paths can cause jank.
- Do not broaden service permissions or file contexts without security review.

## Common Failure Modes

- Service builds but never starts because rc/VINTF/APEX paths disagree.
- Device boots with black screen because SurfaceFlinger cannot connect to composer.
- Video playback shows black frames due to buffer import or modifier mismatch.
- Rotation or mode switch fails because plane constraints were not re-evaluated.
- Hotplug works once but not twice because connector state was cached incorrectly.
- Jank appears after fence ownership or vsync scheduling changes.
- Meson build passes but Android Soong build fails, or the reverse, because source lists diverge.

## Related Repos

- `vendor/intel/mediasdk_c2`: media codec output path depends on display/HWC behavior.
- `vendor/intel/external/project-celadon/camera-vhal`: camera preview consumes display buffers through this stack.
- `vendor/intel/hardware/interfaces`: HAL service, VINTF, and APEX integration patterns.
- `hardware/intel/external/minigbm-intel`: gralloc/minigbm provider affecting buffer metadata.
- Kernel/display driver repos: provide DRM/KMS objects and properties consumed at runtime.
- Device/product repos under `device/intel/`: select the composer service, package APEX/vendor modules, and own SELinux.
