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

#if HAS_LIBDISPLAY_INFO

#include "utils/EdidWrapper.h"
#include "utils/log.h"

namespace android {

auto LibdisplayEdidWrapper::Create(DrmModePropertyBlobUnique blob)
    -> std::unique_ptr<LibdisplayEdidWrapper> {
  if (!blob)
    return nullptr;

  auto *info = di_info_parse_edid(blob->data, blob->length);
  if (!info) {
    ALOGW("Failed to parse edid blob.");
    return nullptr;
  }

  return std::unique_ptr<LibdisplayEdidWrapper>(
      new LibdisplayEdidWrapper(std::move(info)));
}

void LibdisplayEdidWrapper::GetSupportedHdrTypes(std::vector<ui::Hdr> &types) {
  types.clear();

  const auto *hdr_static_meta = di_info_get_hdr_static_metadata(info_);
  const auto *colorimetries = di_info_get_supported_signal_colorimetry(info_);
  if (colorimetries->bt2020_cycc || colorimetries->bt2020_ycc ||
      colorimetries->bt2020_rgb) {
    if (hdr_static_meta->pq)
      types.emplace_back(ui::Hdr::HDR10);
    if (hdr_static_meta->hlg)
      types.emplace_back(ui::Hdr::HLG);
  }
}

void LibdisplayEdidWrapper::GetHdrCapabilities(
    std::vector<ui::Hdr> &types, const float *max_luminance,
    const float *max_average_luminance, const float *min_luminance) {
  GetSupportedHdrTypes(types);

  const auto *hdr_static_meta = di_info_get_hdr_static_metadata(info_);
  max_luminance = &hdr_static_meta->desired_content_max_luminance;
  max_average_luminance = &hdr_static_meta
                               ->desired_content_max_frame_avg_luminance;
  min_luminance = &hdr_static_meta->desired_content_min_luminance;
}

void LibdisplayEdidWrapper::GetColorModes(std::vector<Colormode> &color_modes) {
  color_modes.clear();
  color_modes.emplace_back(Colormode::kNative);

  const auto *hdr_static_meta = di_info_get_hdr_static_metadata(info_);
  const auto *colorimetries = di_info_get_supported_signal_colorimetry(info_);

  /* Rec. ITU-R BT.2020 constant luminance YCbCr */
  /* Rec. ITU-R BT.2020 non-constant luminance YCbCr */
  if (colorimetries->bt2020_cycc || colorimetries->bt2020_ycc)
    color_modes.emplace_back(Colormode::kBt2020);

  /* Rec. ITU-R BT.2020 RGB */
  if (colorimetries->bt2020_rgb)
    color_modes.emplace_back(Colormode::kDisplayBt2020);

  /* SMPTE ST 2113 RGB: P3D65 and P3DCI */
  if (colorimetries->st2113_rgb) {
    color_modes.emplace_back(Colormode::kDciP3);
    color_modes.emplace_back(Colormode::kDisplayP3);
  }

  /* Rec. ITU-R BT.2100 ICtCp HDR (with PQ and/or HLG) */
  if (colorimetries->ictcp) {
    if (hdr_static_meta->pq)
      color_modes.emplace_back(Colormode::kBt2100Pq);
    if (hdr_static_meta->hlg)
      color_modes.emplace_back(Colormode::kBt2100Hlg);
  }
}

}  // namespace android
#endif
