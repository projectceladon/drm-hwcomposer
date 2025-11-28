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

#include <ui/GraphicTypes.h>
#include <ui/ColorSpace.h>

#include "EdidWrapper.h"

namespace android {

// Wrapper class for that uses hack H3Cdisplay-info to parse edids
class H3CdisplayEdidWrapper final : public EdidWrapper {
 public:
  H3CdisplayEdidWrapper() = delete;
  ~H3CdisplayEdidWrapper() override {
  }
  static auto Create(DrmModePropertyBlobUnique blob)
      -> std::unique_ptr<H3CdisplayEdidWrapper>;

  void GetSupportedHdrTypes(std::vector<ui::Hdr> &types) override;

  void GetHdrCapabilities(std::vector<ui::Hdr> &types,
                          float *max_luminance,
                          float *max_average_luminance,
                          float *min_luminance) override;

  void GetColorModes(std::vector<Colormode> &color_modes) override;

  auto GetDpiX() -> int override;
  auto GetDpiY() -> int override;

  auto GetBoundsMm() -> std::pair<int32_t, int32_t> override;

  void GetColorGamut(
    std::array<float2, 3> &primaries, float2 &whitepoint) override;

 private:
  H3CdisplayEdidWrapper(void *date, size_t length);

  std::pair<int32_t, int32_t> GetDpi();

  static constexpr int32_t kWidthPixels = 2880;
  static constexpr int32_t kHeightPixels = 1800;
  static constexpr int32_t kWidthMm = 301;
  static constexpr int32_t kHeightMm = 189;
  static constexpr float kMaxLuminance = 507.5f;
  static constexpr float kMaxAvgLuminance = 507.5f;
  static constexpr float kMinLuminance = 0.004f;
};

}  // namespace android
