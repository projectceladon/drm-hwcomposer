/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifndef ANDROID_HWC3_H
#define ANDROID_HWC3_H

#include <inttypes.h>
#include <hardware/hwcomposer2.h>
#include <string>
#include <aidl/android/hardware/graphics/composer3/IComposerClient.h>
#include <android-base/logging.h>
#include <log/log.h>
#include <utils/Trace.h>
using namespace aidl::android::hardware::graphics::composer3;
namespace HWC3 {
enum class Error : int32_t {
  None = 0,
  BadConfig = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_BAD_CONFIG,
  BadDisplay = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_BAD_DISPLAY,
  BadLayer = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_BAD_LAYER,
  BadParameter = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_BAD_PARAMETER,
  NoResources = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_NO_RESOURCES,
  NotValidated = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_NOT_VALIDATED,
  Unsupported = aidl::android::hardware::graphics::composer3::IComposerClient::
      EX_UNSUPPORTED,
  SeamlessNotAllowed = aidl::android::hardware::graphics::composer3::
      IComposerClient::EX_SEAMLESS_NOT_ALLOWED,
};

typedef enum {
    HWC3_FUNCTION_SET_EXPECTED_PRESENT_TIME = HWC2_FUNCTION_GET_LAYER_GENERIC_METADATA_KEY + 1,
}hwc3_function_descriptor_t;

typedef int32_t /*hwc_error_t*/ (*HWC3_PFN_SET_EXPECTED_PRESENT_TIME)(hwc2_device_t* device,
        hwc2_display_t display, const std::optional<ClockMonotonicTimestamp>& expectedPresentTime);
}  // namespace HWC3

#endif

