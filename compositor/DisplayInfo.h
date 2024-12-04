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

#include <cstdint>

/*
 * Display colorimetry enums.
 */
// NOLINTBEGIN(readability-identifier-naming)
enum class Colormode : int32_t {
  kNative,
  kBt601_625,
  kBt601_625Unadjusted,
  kBt601_525,
  kBt601_525Unadjusted,
  kBt709,
  kDciP3,
  kSrgb,
  kAdobeRgb,
  kDisplayP3,
  kBt2020,
  kBt2100Pq,
  kBt2100Hlg,
  kDisplayBt2020,
};
// NOLINTEND(readability-identifier-naming)

/**
 * Display panel colorspace property values.
 */
enum class Colorspace : int32_t {
  kDefault,
  kSmpte170MYcc,
  kBt709Ycc,
  kXvycc601,
  kXvycc709,
  kSycc601,
  kOpycc601,
  kOprgb,
  kBt2020Cycc,
  kBt2020Rgb,
  kBt2020Ycc,
  kDciP3RgbD65,
  kDciP3RgbTheater,
  kRgbWideFixed,
  kRgbWideFloat,
  kBt601Ycc,
};

/**
 * Display panel orientation property values.
 */
enum PanelOrientation {
  kModePanelOrientationNormal = 0,
  kModePanelOrientationBottomUp,
  kModePanelOrientationLeftUp,
  kModePanelOrientationRightUp
};

struct QueuedConfigTiming {
  // In order for the new config to be applied, the client must send a new frame
  // at this time.
  int64_t refresh_time_ns;

  // The time when the display will start to refresh at the new vsync period.
  int64_t new_vsync_time_ns;
};
