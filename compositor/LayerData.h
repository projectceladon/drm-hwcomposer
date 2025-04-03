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

#pragma once

#include <cmath>
#include <cstdbool>
#include <cstdint>
#include <optional>
#include <vector>

#include "bufferinfo/BufferInfo.h"
#include "drm/DrmFbImporter.h"
#include "utils/fd.h"
#include "utils/UniqueFd2.h"
#include "log/log.h"
namespace android {

class DrmFbIdHandle;

using ILayerId = int64_t;

/* Rotation is defined in the clockwise direction */
/* The flip is done before rotation */
struct LayerTransform {
  bool hflip;
  bool vflip;
  bool rotate90;
};

struct SrcRectInfo {
  struct FRect {
    float left;
    float top;
    float right;
    float bottom;
  };
  /* nullopt means the whole buffer */
  std::optional<FRect> f_rect;
};

struct DstRectInfo {
  struct IRect {
    int32_t left;
    int32_t top;
    int32_t right;
    int32_t bottom;
  };
  /* nullopt means the whole display */
  std::optional<IRect> i_rect;
};

constexpr float kAlphaOpaque = 1.0F;

struct PresentInfo {
  LayerTransform transform{};
  float alpha = kAlphaOpaque;
  SrcRectInfo source_crop{};
  DstRectInfo display_frame{};

  bool RequireScalingOrPhasing() const {
    if (!source_crop.f_rect || !display_frame.i_rect) {
      return false;
    }

    const auto &src = *source_crop.f_rect;
    const auto &dst = *display_frame.i_rect;

    const float src_width = src.right - src.left;
    const float src_height = src.bottom - src.top;

    auto dest_width = float(dst.right - dst.left);
    auto dest_height = float(dst.bottom - dst.top);

    auto scaling = src_width != dest_width || src_height != dest_height;
    auto phasing = (src.left - std::floor(src.left) != 0) ||
                   (src.top - std::floor(src.top) != 0);
    return scaling || phasing;
  }
};

struct LayerData {
  std::optional<BufferInfo> bi;
  std::shared_ptr<DrmFbIdHandle> fb;
  PresentInfo pi;
  SharedFd acquire_fence;
  UniqueFd2 blit_fence;
};

}  // namespace android
