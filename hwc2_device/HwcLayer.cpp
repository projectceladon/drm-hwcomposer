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

#define LOG_TAG "drmhwc"

#include "HwcLayer.h"

#include "HwcDisplay.h"
#include "bufferinfo/BufferInfoGetter.h"
#include "utils/log.h"

namespace android {

void HwcLayer::SetLayerProperties(const LayerProperties& layer_properties) {
  if (layer_properties.buffer) {
    layer_data_.acquire_fence = layer_properties.buffer->acquire_fence;
    buffer_handle_ = layer_properties.buffer->buffer_handle;
    buffer_handle_updated_ = true;
  }
  if (layer_properties.blend_mode) {
    blend_mode_ = layer_properties.blend_mode.value();
  }
  if (layer_properties.color_space) {
    color_space_ = layer_properties.color_space.value();
  }
  if (layer_properties.sample_range) {
    sample_range_ = layer_properties.sample_range.value();
  }
  if (layer_properties.composition_type) {
    sf_type_ = layer_properties.composition_type.value();
  }
  if (layer_properties.display_frame) {
    layer_data_.pi.display_frame = layer_properties.display_frame.value();
  }
  if (layer_properties.alpha) {
    layer_data_.pi.alpha = layer_properties.alpha.value();
  }
  if (layer_properties.source_crop) {
    layer_data_.pi.source_crop = layer_properties.source_crop.value();
  }
  if (layer_properties.transform) {
    layer_data_.pi.transform = layer_properties.transform.value();
  }
  if (layer_properties.z_order) {
    z_order_ = layer_properties.z_order.value();
  }
}

void HwcLayer::ImportFb() {
  if (!IsLayerUsableAsDevice() || !buffer_handle_updated_) {
    return;
  }
  buffer_handle_updated_ = false;

  layer_data_.fb = {};

  auto unique_id = BufferInfoGetter::GetInstance()->GetUniqueId(buffer_handle_);
  if (unique_id && SwChainGetBufferFromCache(*unique_id)) {
    return;
  }

  layer_data_.bi = BufferInfoGetter::GetInstance()->GetBoInfo(buffer_handle_);
  if (!layer_data_.bi) {
    ALOGW("Unable to get buffer information (0x%p)", buffer_handle_);
    bi_get_failed_ = true;
    return;
  }

  layer_data_
      .fb = parent_->GetPipe().device->GetDrmFbImporter().GetOrCreateFbId(
      &layer_data_.bi.value());

  if (!layer_data_.fb) {
    ALOGV("Unable to create framebuffer object for buffer 0x%p",
          buffer_handle_);
    fb_import_failed_ = true;
    return;
  }

  if (unique_id) {
    SwChainAddCurrentBuffer(*unique_id);
  }
}

void HwcLayer::PopulateLayerData() {
  ImportFb();

  if (!layer_data_.bi) {
    ALOGE("%s: Invalid state", __func__);
    return;
  }

  if (blend_mode_ != BufferBlendMode::kUndefined) {
    layer_data_.bi->blend_mode = blend_mode_;
  }
  if (color_space_ != BufferColorSpace::kUndefined) {
    layer_data_.bi->color_space = color_space_;
  }
  if (sample_range_ != BufferSampleRange::kUndefined) {
    layer_data_.bi->sample_range = sample_range_;
  }
}

/* SwapChain Cache */

bool HwcLayer::SwChainGetBufferFromCache(BufferUniqueId unique_id) {
  if (swchain_lookup_table_.count(unique_id) == 0) {
    return false;
  }

  auto seq = swchain_lookup_table_[unique_id];

  if (swchain_cache_.count(seq) == 0) {
    return false;
  }

  auto& el = swchain_cache_[seq];
  if (!el.bi) {
    return false;
  }

  layer_data_.bi = el.bi;
  layer_data_.fb = el.fb;

  return true;
}

void HwcLayer::SwChainReassemble(BufferUniqueId unique_id) {
  if (swchain_lookup_table_.count(unique_id) != 0) {
    if (swchain_lookup_table_[unique_id] ==
        int(swchain_lookup_table_.size()) - 1) {
      /* Skip same buffer */
      return;
    }
    if (swchain_lookup_table_[unique_id] == 0) {
      swchain_reassembled_ = true;
      return;
    }
    /* Tracking error */
    SwChainClearCache();
    return;
  }

  swchain_lookup_table_[unique_id] = int(swchain_lookup_table_.size());
}

void HwcLayer::SwChainAddCurrentBuffer(BufferUniqueId unique_id) {
  if (!swchain_reassembled_) {
    SwChainReassemble(unique_id);
  }

  if (swchain_reassembled_) {
    if (swchain_lookup_table_.count(unique_id) == 0) {
      SwChainClearCache();
      return;
    }

    auto seq = swchain_lookup_table_[unique_id];

    if (swchain_cache_.count(seq) == 0) {
      swchain_cache_[seq] = {};
    }

    swchain_cache_[seq].bi = layer_data_.bi;
    swchain_cache_[seq].fb = layer_data_.fb;
  }
}

void HwcLayer::SwChainClearCache() {
  swchain_cache_.clear();
  swchain_lookup_table_.clear();
  swchain_reassembled_ = false;
}

}  // namespace android