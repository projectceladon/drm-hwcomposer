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

#if HAS_LIBDISPLAY_INFO
extern "C" {
#include <libdisplay-info/edid.h>
#include <libdisplay-info/info.h>
}
#endif

#include <ui/GraphicTypes.h>

#include "compositor/DisplayInfo.h"
#include "drm/DrmUnique.h"

namespace android {

// Stub wrapper class for edid parsing
class EdidWrapper {
 public:
  EdidWrapper() = default;
  EdidWrapper(const EdidWrapper &) = delete;
  virtual ~EdidWrapper() = default;

  virtual void GetSupportedHdrTypes(std::vector<ui::Hdr> &types) {
    types.clear();
  };
  virtual void GetHdrCapabilities(std::vector<ui::Hdr> &types,
                                  const float * /*max_luminance*/,
                                  const float * /*max_average_luminance*/,
                                  const float * /*min_luminance*/) {
    GetSupportedHdrTypes(types);
  };
  virtual void GetColorModes(std::vector<Colormode> &color_modes) {
    color_modes.clear();
  };
  virtual int GetDpiX() {
    return -1;
  }
  virtual int GetDpiY() {
    return -1;
  }

  virtual auto GetBoundsMm() -> std::pair<int32_t, int32_t> {
    return {-1, -1};
  }
};

#if HAS_LIBDISPLAY_INFO
// Wrapper class for that uses libdisplay-info to parse edids
class LibdisplayEdidWrapper final : public EdidWrapper {
 public:
  LibdisplayEdidWrapper() = delete;
  ~LibdisplayEdidWrapper() override {
    di_info_destroy(info_);
  }
  static auto Create(DrmModePropertyBlobUnique blob)
      -> std::unique_ptr<LibdisplayEdidWrapper>;

  void GetSupportedHdrTypes(std::vector<ui::Hdr> &types) override;

  void GetHdrCapabilities(std::vector<ui::Hdr> &types,
                          const float *max_luminance,
                          const float *max_average_luminance,
                          const float *min_luminance) override;

  void GetColorModes(std::vector<Colormode> &color_modes) override;

  auto GetDpiX() -> int override;
  auto GetDpiY() -> int override;

  auto GetBoundsMm() -> std::pair<int32_t, int32_t> override;

 private:
  LibdisplayEdidWrapper(di_info *info) : info_(std::move(info)) {
  }

  std::pair<int32_t, int32_t> GetDpi();

  di_info *info_{};
};
#endif

}  // namespace android
