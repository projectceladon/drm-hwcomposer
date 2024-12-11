/*
 * Copyright (C) 2025 The Android Open Source Project
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

#define LOG_TAG "drmhwc"

#if HAS_LIBDISPLAY_INFO
extern "C" {
#include <libdisplay-info/info.h>
}
#endif

#include "drm/DrmUnique.h"
#include "utils/log.h"

namespace android {

// Stub wrapper class for edid parsing
class EdidWrapper {
 public:
  EdidWrapper() = default;
  EdidWrapper(const EdidWrapper &) = delete;
  virtual ~EdidWrapper() = default;
};

#if HAS_LIBDISPLAY_INFO
// Wrapper class for that uses libdisplay-info to parse edids
class LibdisplayEdidWrapper final : public EdidWrapper {
 public:
  LibdisplayEdidWrapper() = delete;
  LibdisplayEdidWrapper(di_info *info) : info_(info) {
  }
  ~LibdisplayEdidWrapper() override {
    di_info_destroy(info_);
  }
  static auto Create(DrmModePropertyBlobUnique blob)
      -> std::unique_ptr<LibdisplayEdidWrapper> {
    if (!blob)
      return nullptr;

    auto *info = di_info_parse_edid(blob->data, blob->length);
    if (!info) {
      ALOGW("Failed to parse edid blob.");
      return nullptr;
    }

    return std::make_unique<LibdisplayEdidWrapper>(std::move(info));
  }

 private:
  di_info *info_{};
};
#endif

}  // namespace android
