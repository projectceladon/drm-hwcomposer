
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

#define LOG_TAG "drmhwc"
#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include "Utils.h"

#include <hardware/hwcomposer2.h>

#include "utils/log.h"

namespace aidl::android::hardware::graphics::composer3 {

hwc3::Error Hwc2toHwc3Error(HWC2::Error error) {
  switch (error) {
    case HWC2::Error::None:
      return hwc3::Error::kNone;
    case HWC2::Error::BadConfig:
      return hwc3::Error::kBadConfig;
    case HWC2::Error::BadDisplay:
      return hwc3::Error::kBadDisplay;
    case HWC2::Error::BadLayer:
      return hwc3::Error::kBadLayer;
    case HWC2::Error::BadParameter:
      return hwc3::Error::kBadParameter;
    case HWC2::Error::NoResources:
      return hwc3::Error::kNoResources;
    case HWC2::Error::NotValidated:
      return hwc3::Error::kNotValidated;
    case HWC2::Error::Unsupported:
      return hwc3::Error::kUnsupported;
    case HWC2::Error::SeamlessNotAllowed:
      return hwc3::Error::kSeamlessNotAllowed;
    case HWC2::Error::SeamlessNotPossible:
      return hwc3::Error::kSeamlessNotPossible;
    default:
      ALOGE("Unknown HWC2 error. Could not translate to HWC3 error: %d",
            static_cast<int32_t>(error));
      return hwc3::Error::kUnsupported;
  }
}

};  // namespace aidl::android::hardware::graphics::composer3