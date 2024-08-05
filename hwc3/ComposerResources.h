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

#include <memory>

#include "aidl/android/hardware/graphics/composer3/IComposerClient.h"
#include "composer-resources/2.2/ComposerResources.h"
#include "cutils/native_handle.h"
#include "hwc3/Utils.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class ComposerResourceReleaser {
 public:
  explicit ComposerResourceReleaser(bool is_buffer)
      : replaced_handle_(is_buffer) {
  }
  virtual ~ComposerResourceReleaser() = default;

  ::android::hardware::graphics::composer::V2_2::hal::ComposerResources::
      ReplacedHandle*
      GetReplacedHandle() {
    return &replaced_handle_;
  }

 private:
  ::android::hardware::graphics::composer::V2_2::hal::ComposerResources::
      ReplacedHandle replaced_handle_;
};

class ComposerResources {
 public:
  static std::unique_ptr<ComposerResources> Create();
  ~ComposerResources() = default;

  hwc3::Error GetLayerBuffer(uint64_t display_id, int64_t layer_id,
                             const Buffer& buffer,
                             buffer_handle_t* out_buffer_handle,
                             ComposerResourceReleaser* releaser);
  hwc3::Error GetLayerSidebandStream(
      uint64_t display_id, int64_t layer_id,
      const aidl::android::hardware::common::NativeHandle& handle,
      buffer_handle_t* out_handle, ComposerResourceReleaser* releaser);

  hwc3::Error AddLayer(uint64_t display, int64_t layer,
                       uint32_t buffer_cache_size);
  hwc3::Error RemoveLayer(uint64_t display, int64_t layer);

  bool HasDisplay(uint64_t display);
  hwc3::Error AddPhysicalDisplay(uint64_t display);
  hwc3::Error AddVirtualDisplay(uint64_t display,
                                uint32_t output_buffer_cache_size);
  hwc3::Error RemoveDisplay(uint64_t display);

  void SetDisplayMustValidateState(uint64_t display_id, bool must_validate);
  bool MustValidateDisplay(uint64_t display_id);

  hwc3::Error GetDisplayClientTarget(uint64_t display_id, const Buffer& buffer,
                                     buffer_handle_t* out_handle,
                                     ComposerResourceReleaser* releaser);

  hwc3::Error SetDisplayClientTargetCacheSize(
      uint64_t display_id, uint32_t client_target_cache_size);
  hwc3::Error GetDisplayClientTargetCacheSize(uint64_t display_id,
                                              size_t* out_cache_size);
  hwc3::Error GetDisplayOutputBufferCacheSize(uint64_t display,
                                              size_t* out_cache_size);
  hwc3::Error GetDisplayOutputBuffer(uint64_t display_id, const Buffer& buffer,
                                     buffer_handle_t* out_handle,
                                     ComposerResourceReleaser* releaser);

  static std::unique_ptr<ComposerResourceReleaser> CreateResourceReleaser(
      bool is_buffer);

 private:
  ComposerResources() = default;

  std::unique_ptr<
      ::android::hardware::graphics::composer::V2_2::hal::ComposerResources>
      resources_ = ::android::hardware::graphics::composer::V2_2::hal::
          ComposerResources::create();
};

}  // namespace aidl::android::hardware::graphics::composer3::impl