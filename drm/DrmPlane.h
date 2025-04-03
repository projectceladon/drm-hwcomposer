/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include <xf86drmMode.h>

#include <cstdint>
#include <vector>

#include "DrmCrtc.h"
#include "DrmProperty.h"
#include "compositor/LayerData.h"
#include <hardware/hwcomposer2.h>
namespace android {

class DrmDevice;
struct LayerData;

// NOLINTNEXTLINE(readability-identifier-naming)
struct drm_plane_size_hint_local {
  __u16 width;
  __u16 height;
};

class DrmPlane : public PipelineBindable<DrmPlane> {
 public:
  DrmPlane(const DrmPlane &) = delete;
  DrmPlane &operator=(const DrmPlane &) = delete;

  static auto CreateInstance(DrmDevice &dev, uint32_t plane_id)
      -> std::unique_ptr<DrmPlane>;
  bool IsPixBlendModeSupported() { return blend_property_ ? true : false;}
  bool IsCrtcSupported(const DrmCrtc &crtc) const;
  bool IsValidForLayer(LayerData *layer);

  auto GetType() const {
    return type_;
  }

  bool IsFormatSupported(uint32_t format) const;
  bool HasNonRgbFormat() const;
  bool IsResolutionSupported(hwc_rect_t display_frame);
  auto AtomicSetState(drmModeAtomicReq &pset, LayerData &layer, uint32_t zpos,
                      uint32_t crtc_id, DstRectInfo &whole_display_rect) -> int;
  auto AtomicDisablePlane(drmModeAtomicReq &pset) -> int;
  auto &GetZPosProperty() const {
    return zpos_property_;
  }

  auto GetId() const {
    return plane_->plane_id;
  }

  bool HasCursorSizeConstraints() const;

 private:
  DrmPlane(DrmDevice &dev, DrmModePlaneUnique plane)
      : drm_(&dev), plane_(std::move(plane)){};
  DrmDevice *const drm_;
  DrmModePlaneUnique plane_;

  enum class Presence { kOptional, kMandatory };

  auto Init() -> int;
  auto GetPlaneProperty(const char *prop_name, DrmProperty &property,
                        Presence presence = Presence::kMandatory) -> bool;
  bool IsBufferValidForCursorPlane(const BufferInfo &bi) const;

  uint32_t type_{};

  std::vector<uint32_t> formats_;

  DrmProperty crtc_property_;
  DrmProperty fb_property_;
  DrmProperty crtc_x_property_;
  DrmProperty crtc_y_property_;
  DrmProperty crtc_w_property_;
  DrmProperty crtc_h_property_;
  DrmProperty src_x_property_;
  DrmProperty src_y_property_;
  DrmProperty src_w_property_;
  DrmProperty src_h_property_;
  DrmProperty zpos_property_;
  DrmProperty rotation_property_;
  DrmProperty alpha_property_;
  DrmProperty blend_property_;
  DrmProperty in_fence_fd_property_;
  DrmProperty color_encoding_property_;
  DrmProperty color_range_property_;
  DrmProperty size_hints_property_;

  std::map<BufferBlendMode, uint64_t> blending_enum_map_;
  std::map<BufferColorSpace, uint64_t> color_encoding_enum_map_;
  std::map<BufferSampleRange, uint64_t> color_range_enum_map_;
  uint64_t transform_enum_mask_ = DRM_MODE_ROTATE_0;
  std::vector<drm_plane_size_hint_local> size_hints_;
};
}  // namespace android
