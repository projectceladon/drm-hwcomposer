
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
#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include "ComposerResources.h"

#include <aidlcommonsupport/NativeHandle.h>

#include "hardware/hwcomposer2.h"
#include "hwc3/Utils.h"

namespace {
using Hwc2Display = ::android::hardware::graphics::composer::V2_1::Display;
using Hwc2Layer = ::android::hardware::graphics::composer::V2_1::Layer;

auto ToHwc2Display(uint64_t display_id) -> Hwc2Display {
  return static_cast<Hwc2Display>(display_id);
}

auto ToHwc2Layer(int64_t layer_id) -> Hwc2Layer {
  return static_cast<Hwc2Layer>(layer_id);
}
}  // namespace

namespace aidl::android::hardware::graphics::composer3::impl {

std::unique_ptr<ComposerResourceReleaser>
ComposerResources::CreateResourceReleaser(bool is_buffer) {
  return std::make_unique<ComposerResourceReleaser>(is_buffer);
}

std::unique_ptr<ComposerResources> ComposerResources::Create() {
  auto instance = std::unique_ptr<ComposerResources>(new ComposerResources);
  if (instance->resources_ == nullptr) {
    ALOGE("%s: Failed to initialise ComposerResources", __func__);
    return nullptr;
  }

  return instance;
}

hwc3::Error ComposerResources::GetLayerBuffer(
    uint64_t display_id, int64_t layer_id, const Buffer& buffer,
    buffer_handle_t* out_buffer_handle,
    ComposerResourceReleaser* buf_releaser) {
  auto display = ToHwc2Display(display_id);
  auto layer = ToHwc2Layer(layer_id);

  const bool use_cache = !buffer.handle.has_value();
  buffer_handle_t buffer_handle = nullptr;
  if (buffer.handle.has_value()) {
    buffer_handle = ::android::makeFromAidl(*buffer.handle);
  }

  auto err = resources_->getLayerBuffer(display, layer, buffer.slot, use_cache,
                                        buffer_handle, out_buffer_handle,
                                        buf_releaser->GetReplacedHandle());

  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}

hwc3::Error ComposerResources::GetLayerSidebandStream(
    uint64_t display_id, int64_t layer_id,
    const aidl::android::hardware::common::NativeHandle& handle,
    buffer_handle_t* out_handle, ComposerResourceReleaser* releaser) {
  auto display = ToHwc2Display(display_id);
  auto layer = ToHwc2Layer(layer_id);

  auto err = resources_->getLayerSidebandStream(display, layer,
                                                ::android::makeFromAidl(handle),
                                                out_handle,
                                                releaser->GetReplacedHandle());

  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}

hwc3::Error ComposerResources::AddLayer(uint64_t display_id, int64_t layer_id,
                                        uint32_t buffer_cache_size) {
  auto display = ToHwc2Display(display_id);
  auto layer = ToHwc2Layer(layer_id);

  auto err = resources_->addLayer(display, layer, buffer_cache_size);
  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}

hwc3::Error ComposerResources::RemoveLayer(uint64_t display_id,
                                           int64_t layer_id) {
  auto display = ToHwc2Display(display_id);
  auto layer = ToHwc2Layer(layer_id);

  auto err = resources_->removeLayer(display, layer);
  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}

bool ComposerResources::HasDisplay(uint64_t display_id) {
  auto display = ToHwc2Display(display_id);
  return resources_->hasDisplay(display);
}

hwc3::Error ComposerResources::AddPhysicalDisplay(uint64_t display_id) {
  auto display = ToHwc2Display(display_id);
  auto err = resources_->addPhysicalDisplay(display);
  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}

hwc3::Error ComposerResources::AddVirtualDisplay(
    uint64_t display, uint32_t output_buffer_cache_size) {
  auto err = resources_->addVirtualDisplay(display, output_buffer_cache_size);
  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}

hwc3::Error ComposerResources::RemoveDisplay(uint64_t display_id) {
  auto display = ToHwc2Display(display_id);
  auto err = resources_->removeDisplay(display);
  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}

void ComposerResources::SetDisplayMustValidateState(uint64_t display_id,
                                                    bool must_validate) {
  auto display = ToHwc2Display(display_id);
  resources_->setDisplayMustValidateState(display, must_validate);
}

bool ComposerResources::MustValidateDisplay(uint64_t display_id) {
  auto display = ToHwc2Display(display_id);
  return resources_->mustValidateDisplay(display);
}

hwc3::Error ComposerResources::GetDisplayClientTarget(
    uint64_t display_id, const Buffer& buffer, buffer_handle_t* out_handle,
    ComposerResourceReleaser* releaser) {
  auto display = ToHwc2Display(display_id);

  const bool use_cache = !buffer.handle.has_value();
  buffer_handle_t buffer_handle = nullptr;
  if (buffer.handle.has_value()) {
    buffer_handle = ::android::makeFromAidl(*buffer.handle);
  }

  auto err = resources_->getDisplayClientTarget(display, buffer.slot, use_cache,
                                                buffer_handle, out_handle,
                                                releaser->GetReplacedHandle());
  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}

hwc3::Error ComposerResources::SetDisplayClientTargetCacheSize(
    uint64_t display_id, uint32_t client_target_cache_size) {
  auto display = ToHwc2Display(display_id);
  auto err = resources_
                 ->setDisplayClientTargetCacheSize(display,
                                                   client_target_cache_size);
  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}

hwc3::Error ComposerResources::GetDisplayClientTargetCacheSize(
    uint64_t display_id, size_t* out_cache_size) {
  auto display = ToHwc2Display(display_id);
  auto err = resources_->getDisplayClientTargetCacheSize(display,
                                                         out_cache_size);
  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}

hwc3::Error ComposerResources::GetDisplayOutputBufferCacheSize(
    uint64_t display_id, size_t* out_cache_size) {
  auto display = ToHwc2Display(display_id);
  auto err = resources_->getDisplayOutputBufferCacheSize(display,
                                                         out_cache_size);
  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}

hwc3::Error ComposerResources::GetDisplayOutputBuffer(
    uint64_t display_id, const Buffer& buffer, buffer_handle_t* out_handle,
    ComposerResourceReleaser* releaser) {
  auto display = ToHwc2Display(display_id);
  const bool use_cache = !buffer.handle.has_value();

  buffer_handle_t buffer_handle = nullptr;
  if (buffer.handle.has_value()) {
    buffer_handle = ::android::makeFromAidl(*buffer.handle);
  }

  auto err = resources_->getDisplayOutputBuffer(display, buffer.slot, use_cache,
                                                buffer_handle, out_handle,
                                                releaser->GetReplacedHandle());
  return Hwc2toHwc3Error(static_cast<HWC2::Error>(err));
}
}  // namespace aidl::android::hardware::graphics::composer3::impl