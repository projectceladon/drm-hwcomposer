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

#include <cstddef>
#define LOG_TAG "drmhwc"

#include "utils/EdidWrapper.h"
#include "utils/EdidWrapperH3C.h"
#include "utils/log.h"

namespace android {
auto H3CdisplayEdidWrapper::Create(DrmModePropertyBlobUnique blob)
    -> std::unique_ptr<H3CdisplayEdidWrapper> {
  if (!blob)
      return nullptr;

  void *data = blob->data;
  uint32_t length = blob->length;
  if (length < 256) {
      return nullptr;
  }

  uint8_t *cta_ext_blk = static_cast<uint8_t *>(data) + 0x80;

  if (cta_ext_blk[0x00] == 0x70
          && cta_ext_blk[0x01] == 0x20
          && cta_ext_blk[0x08] == 0x94
          && cta_ext_blk[0x09] == 0x0b
          && cta_ext_blk[0x0a] == 0xd5) {
      ALOGI("kanli create H3C edid wrapper");
      return std::unique_ptr<H3CdisplayEdidWrapper>(
              new H3CdisplayEdidWrapper(data, length));
  }

  return nullptr;
    }

void H3CdisplayEdidWrapper::GetSupportedHdrTypes(std::vector<ui::Hdr> &types) {
    types.clear();
    types.emplace_back(ui::Hdr::HDR10);
    //types.emplace_back(ui::Hdr::HLG);
    //types.emplace_back(ui::Hdr::HDR10_PLUS);
}

void H3CdisplayEdidWrapper::GetHdrCapabilities(
        std::vector<ui::Hdr> &types,
        float *max_luminance,
        float *max_average_luminance,
        float *min_luminance) {
    GetSupportedHdrTypes(types);

    *max_luminance = kMaxLuminance;
    *max_average_luminance = kMaxAvgLuminance;
    *min_luminance = kMinLuminance;
}

void H3CdisplayEdidWrapper::GetColorModes(std::vector<Colormode> &color_modes) {
    color_modes.clear();
    color_modes.emplace_back(Colormode::kNative);
    //color_modes.emplace_back(Colormode::kSrgb);
    //color_modes.emplace_back(Colormode::kDisplayP3);
    color_modes.emplace_back(Colormode::kDisplayBt2020);
}

auto H3CdisplayEdidWrapper::GetDpiX() -> int {
    return GetDpi().first;
}

auto H3CdisplayEdidWrapper::GetDpiY() -> int {
    return GetDpi().second;
}

auto H3CdisplayEdidWrapper::GetBoundsMm() -> std::pair<int32_t, int32_t> {
    return {kWidthMm, kHeightMm};
}

auto H3CdisplayEdidWrapper::GetDpi() -> std::pair<int32_t, int32_t> {
    int32_t kUmPerInch = 25400;
    return {kWidthPixels * kUmPerInch / (kWidthMm * 1000),
        kHeightPixels * kUmPerInch / (kHeightMm * 1000)};
}

void H3CdisplayEdidWrapper::GetColorGamut(
        std::array<float2, 3> &primaries, float2 &whitepoint) {
    int sdr_config = 0;
    if (sdr_config) {
        primaries[0].x = 0.640;
        primaries[0].y = 0.330;
        primaries[1].x = 0.300;
        primaries[1].y = 0.600;
        primaries[2].x = 0.150;
        primaries[2].y = 0.060;

        whitepoint.x = 0.313;
        whitepoint.y = 0.329;
    } else {
        primaries[0].x = 0.684;
        primaries[0].y = 0.316;
        primaries[1].x = 0.245;
        primaries[1].y = 0.730;
        primaries[2].x = 0.139;
        primaries[2].y = 0.042;

        whitepoint.x = 0.313;
        whitepoint.y = 0.329;
    }

    ALOGI("print ColorGamut:");
    ALOGI("    red %f %f"  ,primaries[0].x, primaries[0].y);
    ALOGI("  grean %f %f"  ,primaries[1].x, primaries[1].y);
    ALOGI("   blue %f %f"  ,primaries[2].x, primaries[2].y);
    ALOGI("  write %f %f"  ,whitepoint.x, whitepoint.y);


    return;
}


H3CdisplayEdidWrapper::H3CdisplayEdidWrapper(void *date, size_t length) {

}
}  // namespace android
