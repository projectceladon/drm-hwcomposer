/*
 * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <aidl/android/hardware/graphics/common/Hdr.h>
#include <aidl/android/hardware/graphics/composer3/IComposerClient.h>
#include <hardware/hwcomposer2.h>
#include <log/log.h>

#include <cstdint>

// NOLINTNEXTLINE
#define DEBUG_FUNC() ALOGV("%s", __func__)

namespace aidl::android::hardware::graphics::composer3 {

namespace hwc3 {
enum class Error : int32_t {
  kNone = 0,
  kBadConfig = IComposerClient::EX_BAD_CONFIG,
  kBadDisplay = IComposerClient::EX_BAD_DISPLAY,
  kBadLayer = IComposerClient::EX_BAD_LAYER,
  kBadParameter = IComposerClient::EX_BAD_PARAMETER,
  kNoResources = IComposerClient::EX_NO_RESOURCES,
  kNotValidated = IComposerClient::EX_NOT_VALIDATED,
  kUnsupported = IComposerClient::EX_UNSUPPORTED,
  kSeamlessNotAllowed = IComposerClient::EX_SEAMLESS_NOT_ALLOWED,
  kSeamlessNotPossible = IComposerClient::EX_SEAMLESS_NOT_POSSIBLE,
};
}  // namespace hwc3

hwc3::Error Hwc2toHwc3Error(HWC2::Error error);

inline ndk::ScopedAStatus ToBinderStatus(hwc3::Error error) {
  if (error != hwc3::Error::kNone) {
    return ndk::ScopedAStatus::fromServiceSpecificError(
        static_cast<int32_t>(error));
  }
  return ndk::ScopedAStatus::ok();
}

inline ndk::ScopedAStatus ToBinderStatus(HWC2::Error error) {
  return ToBinderStatus(Hwc2toHwc3Error(error));
}

inline int64_t Hwc2DisplayToHwc3(hwc2_display_t display) {
  return static_cast<int64_t>(display);
}

inline int32_t Hwc2ConfigIdToHwc3(hwc2_config_t config_id) {
  return static_cast<int32_t>(config_id);
}

inline hwc2_display_t Hwc3DisplayToHwc2(int64_t display) {
  return static_cast<hwc2_display_t>(display);
}

inline hwc2_config_t Hwc3ConfigIdToHwc2(int32_t config_id) {
  return static_cast<hwc2_config_t>(config_id);
}

// Values match up to HWC2_COMPOSITION_SIDEBAND, with HWC2 not supporting
// newer values. static_cast in between shared values.
// https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/graphics/composer/aidl/android/hardware/graphics/composer3/Composition.aidl
// https://cs.android.com/android/platform/superproject/main/+/main:hardware/libhardware/include_all/hardware/hwcomposer2.h;drc=d783cabd4d9bddb4b83f2dd38300b7598bb58b24;l=826
inline Composition Hwc2CompositionTypeToHwc3(int32_t composition_type) {
  if (composition_type < HWC2_COMPOSITION_INVALID ||
      composition_type > HWC2_COMPOSITION_SIDEBAND) {
    return Composition::INVALID;
  }
  return static_cast<Composition>(composition_type);
}

// Values for color modes match across HWC versions, so static cast is safe:
// https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/graphics/composer/aidl/android/hardware/graphics/composer3/ColorMode.aidl
// https://cs.android.com/android/platform/superproject/main/+/main:system/core/libsystem/include/system/graphics-base-v1.0.h;drc=7d940ae4afa450696afa25e07982f3a95e17e9b2;l=118
// https://cs.android.com/android/platform/superproject/main/+/main:system/core/libsystem/include/system/graphics-base-v1.1.h;drc=7d940ae4afa450696afa25e07982f3a95e17e9b2;l=35
inline ColorMode Hwc2ColorModeToHwc3(int32_t color_mode) {
  return static_cast<ColorMode>(color_mode);
}

inline int32_t Hwc3ColorModeToHwc2(ColorMode color_mode) {
  return static_cast<int32_t>(color_mode);
}

// Capabilities match up to DisplayCapability::AUTO_LOW_LATENCY_MODE, with hwc2
// not defining capabilities beyond that.
// https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/graphics/composer/aidl/android/hardware/graphics/composer3/DisplayCapability.aidl#28
// https://cs.android.com/android/platform/superproject/main/+/main:hardware/libhardware/include_all/hardware/hwcomposer2.h;drc=1a0e4a1698c7b080d6763cef9e16592bce75967e;l=418
inline DisplayCapability Hwc2DisplayCapabilityToHwc3(
    uint32_t display_capability) {
  if (display_capability > 8) {
    return DisplayCapability::INVALID;
  }
  return static_cast<DisplayCapability>(display_capability);
}

// Values match between hwc versions, so static cast is safe.
// https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/graphics/composer/aidl/android/hardware/graphics/composer3/DisplayConnectionType.aidl
// https://cs.android.com/android/platform/superproject/main/+/main:hardware/libhardware/include_all/hardware/hwcomposer2.h;l=216;drc=d783cabd4d9bddb4b83f2dd38300b7598bb58b24;bpv=0;bpt=1
inline DisplayConnectionType Hwc2DisplayConnectionTypeToHwc3(uint32_t type) {
  if (type > HWC2_DISPLAY_CONNECTION_TYPE_EXTERNAL) {
    // Arbitrarily return EXTERNAL in this case, which shouldn't happen.
    // TODO: This will be cleaned up once hwc2<->hwc3 conversion is removed.
    ALOGE("Unknown HWC2 connection type. Could not translate: %d", type);
    return DisplayConnectionType::EXTERNAL;
  }
  return static_cast<DisplayConnectionType>(type);
}

// Values match, so static_cast is safe.
// https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/graphics/composer/aidl/android/hardware/graphics/composer3/RenderIntent.aidl
// https://cs.android.com/android/platform/superproject/main/+/main:system/core/libsystem/include/system/graphics-base-v1.1.h;drc=7d940ae4afa450696afa25e07982f3a95e17e9b2;l=37
inline RenderIntent Hwc2RenderIntentToHwc3(int32_t intent) {
  if (intent < HAL_RENDER_INTENT_COLORIMETRIC ||
      intent > HAL_RENDER_INTENT_TONE_MAP_ENHANCE) {
    ALOGE("Unknown HWC2 render intent. Could not translate: %d", intent);
    return RenderIntent::COLORIMETRIC;
  }
  return static_cast<RenderIntent>(intent);
}
inline int32_t Hwc3RenderIntentToHwc2(RenderIntent render_intent) {
  return static_cast<int32_t>(render_intent);
}

// Values match up to DOZE_SUSPEND.
// https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/graphics/composer/aidl/android/hardware/graphics/composer3/PowerMode.aidl
// https://cs.android.com/android/platform/superproject/main/+/main:hardware/libhardware/include_all/hardware/hwcomposer2.h;l=348;drc=d783cabd4d9bddb4b83f2dd38300b7598bb58b24
inline int32_t Hwc3PowerModeToHwc2(PowerMode power_mode) {
  if (power_mode > PowerMode::DOZE_SUSPEND) {
    ALOGE("Unsupported HWC2 power mode. Could not translate: %d", power_mode);
    return HWC2_POWER_MODE_ON;
  }
  return static_cast<int32_t>(power_mode);
}

// Values appear to match.
// https://cs.android.com/android/platform/superproject/main/+/main:hardware/interfaces/graphics/common/aidl/android/hardware/graphics/common/Dataspace.aidl
// https://cs.android.com/android/platform/superproject/main/+/main:system/core/libsystem/include/system/graphics-base-v1.0.h;l=43
// https://cs.android.com/android/platform/superproject/main/+/main:system/core/libsystem/include/system/graphics-base-v1.1.h;l=22;drc=7d940ae4afa450696afa25e07982f3a95e17e9b2
inline int32_t Hwc3DataspaceToHwc2(common::Dataspace dataspace) {
  return static_cast<int32_t>(dataspace);
}

// Values appear to match.
// https://cs.android.com/android/platform/superproject/main/+/main:hardware/interfaces/graphics/common/aidl/android/hardware/graphics/common/Hdr.aidl
// https://cs.android.com/android/platform/superproject/main/+/main:system/core/libsystem/include/system/graphics-base-v1.0.h;l=130;drc=7d940ae4afa450696afa25e07982f3a95e17e9b2
// https://cs.android.com/android/platform/superproject/main/+/main:system/core/libsystem/include/system/graphics-base-v1.2.h;l=12;drc=af7be7616859f8e9e57710b9c37c66cf880a6643
inline common::Hdr Hwc2HdrTypeToHwc3(int32_t hdr_type) {
  return static_cast<common::Hdr>(hdr_type);
}

};  // namespace aidl::android::hardware::graphics::composer3
