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

auto LibdisplayEdidWrapper::GetDpiX() -> int {
  return GetDpi().first;
}

auto LibdisplayEdidWrapper::GetDpiY() -> int {
  return GetDpi().second;
}

auto LibdisplayEdidWrapper::GetBoundsMm() -> std::pair<int32_t, int32_t> {
  const auto edid = di_info_get_edid(info_);
  const auto detailed_timing_defs = di_edid_get_detailed_timing_defs(edid);
  const auto dtd = detailed_timing_defs[0];
  if (dtd == nullptr || dtd->horiz_image_mm == 0 || dtd->vert_image_mm == 0) {
    // try to fallback on display size if no dtd.
    // However since edid screen size are vastly unreliable only provide a valid
    // width to avoid invalid dpi computation.
    const auto screen_size = di_edid_get_screen_size(edid);
    return {screen_size->width_cm * 10, -1};
  }

  return {dtd->horiz_image_mm, dtd->vert_image_mm};
}

auto LibdisplayEdidWrapper::GetDpi() -> std::pair<int32_t, int32_t> {
  static const int32_t kUmPerInch = 25400;
  const auto edid = di_info_get_edid(info_);
  const auto detailed_timing_defs = di_edid_get_detailed_timing_defs(edid);
  const auto dtd = detailed_timing_defs[0];
  if (dtd == nullptr || dtd->horiz_image_mm == 0 || dtd->vert_image_mm == 0) {
    // try to fallback on display size if no dtd.
    const auto screen_size = di_edid_get_screen_size(edid);
    const auto standard_timings = di_edid_get_standard_timings(edid);
    if (screen_size->width_cm <= 0 || standard_timings == nullptr) {
      return {-1, -1};
    }

    // display size is more unreliable so use only horizontal dpi.
    int32_t horiz_video = standard_timings[0]->horiz_video;
    int32_t dpi = horiz_video * kUmPerInch / (screen_size->width_cm * 10);
    return {dpi, dpi};
  }

  return {dtd->horiz_video * kUmPerInch / dtd->horiz_image_mm,
          dtd->vert_video * kUmPerInch / dtd->vert_image_mm};
}

}  // namespace android
#endif
