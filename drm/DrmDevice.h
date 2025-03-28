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

#include <cstdint>
#include <map>
#include <optional>
#include <tuple>

#include "DrmConnector.h"
#include "DrmCrtc.h"
#include "DrmEncoder.h"
#include "bufferinfo/BufferInfo.h"
#include "utils/fd.h"

namespace android {

#define DRM_FORMAT_NV12_INTEL fourcc_code('9', '9', '9', '6')
class DrmFbImporter;
class DrmPlane;
class ResourceManager;

class DrmDevice {
 public:
  ~DrmDevice() = default;

  static auto CreateInstance(std::string const &path, ResourceManager *res_man,
                             uint32_t index) -> std::unique_ptr<DrmDevice>;

  auto &GetFd() const {
    return fd_;
  }

  auto GetIndexInDevArray() const {
    return index_in_dev_array_;
  }

  auto &GetResMan() {
    return *res_man_;
  }

  auto GetConnectors() -> const std::vector<std::unique_ptr<DrmConnector>> &;
  auto GetWritebackConnectors()
      -> const std::vector<std::unique_ptr<DrmConnector>> &;
  auto GetPlanes() -> const std::vector<std::unique_ptr<DrmPlane>> &;
  auto GetCrtcs() -> const std::vector<std::unique_ptr<DrmCrtc>> &;
  auto GetEncoders() -> const std::vector<std::unique_ptr<DrmEncoder>> &;
  static auto IsIvshmDev(int fd) -> bool;
  auto GetMinResolution() const {
    return min_resolution_;
  }

  auto GetMaxResolution() const {
    return max_resolution_;
  }

  uint32_t GetNextModeId();
  void ResetModeId();

  auto GetColorAdjustmentEnabling() const {
    return color_adjustment_enabling_;
  }
  std::string GetName() const;

  auto RegisterUserPropertyBlob(void *data, size_t length) const
      -> DrmModeUserPropertyBlobUnique;

  auto HasAddFb2ModifiersSupport() const {
    return HasAddFb2ModifiersSupport_;
  }

  auto CreateBufferForModeset(uint32_t width, uint32_t height)
      -> std::optional<BufferInfo>;

  auto &GetDrmFbImporter() {
    return *drm_fb_importer_;
  }

  auto FindCrtcById(uint32_t id) const -> DrmCrtc * {
    for (const auto &crtc : crtcs_) {
      if (crtc->GetId() == id) {
        return crtc.get();
      }
    };

    return nullptr;
  }

  auto FindEncoderById(uint32_t id) const -> DrmEncoder * {
    for (const auto &enc : encoders_) {
      if (enc->GetId() == id) {
        return enc.get();
      }
    };

    return nullptr;
  }

  int GetProperty(uint32_t obj_id, uint32_t obj_type, const char *prop_name,
                  DrmProperty *property) const;

  const std::optional<std::pair<uint64_t, uint64_t>> &GetCapCursorSize() const {
    return cap_cursor_size_;
  }
  auto IsIvshmDev() {return IsIvshmDev_;}
 private:
  explicit DrmDevice(ResourceManager *res_man, uint32_t index);
  auto Init(const char *path) -> int;

  static auto IsKMSDev(const char *path) -> bool;

  SharedFd fd_;
  const uint32_t index_in_dev_array_;
  uint32_t mode_id_ = 0;
  std::vector<std::unique_ptr<DrmConnector>> connectors_;
  std::vector<std::unique_ptr<DrmConnector>> writeback_connectors_;
  std::vector<std::unique_ptr<DrmEncoder>> encoders_;
  std::vector<std::unique_ptr<DrmCrtc>> crtcs_;
  std::vector<std::unique_ptr<DrmPlane>> planes_;

  std::pair<uint32_t, uint32_t> min_resolution_;
  std::pair<uint32_t, uint32_t> max_resolution_;
  std::optional<std::pair<uint64_t, uint64_t>> cap_cursor_size_;

  bool HasAddFb2ModifiersSupport_{};

  std::unique_ptr<DrmFbImporter> drm_fb_importer_;

  ResourceManager *const res_man_;
  bool IsIvshmDev_ = false;
public:
  bool preferred_mode_limit_ = false;
  bool planes_enabling_ = false;
  uint32_t planes_num_ = 0;
  bool color_adjustment_enabling_ = false;
};

}  // namespace android
