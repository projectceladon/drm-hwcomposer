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
  if (layer_properties.slot_buffer) {
    auto slot_id = layer_properties.slot_buffer->slot_id;
    if (!layer_properties.slot_buffer->bi) {
      slots_.erase(slot_id);
    } else {
      slots_[slot_id] = {
          .bi = layer_properties.slot_buffer->bi.value(),
          .fb = {},
      };
    }
  }
  if (layer_properties.active_slot) {
    active_slot_id_ = layer_properties.active_slot->slot_id;
    layer_data_.acquire_fence = layer_properties.active_slot->fence;
    buffer_updated_ = true;
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
  if (!IsLayerUsableAsDevice() || !buffer_updated_ ||
      !active_slot_id_.has_value()) {
    return;
  }
  buffer_updated_ = false;

  if (slots_[*active_slot_id_].fb) {
    return;
  }

  auto& fb_importer = parent_->GetPipe().device->GetDrmFbImporter();
  auto fb = fb_importer.GetOrCreateFbId(&slots_[*active_slot_id_].bi);

  if (!fb) {
    ALOGE("Unable to create framebuffer object for layer %p", this);
    fb_import_failed_ = true;
    return;
  }

  slots_[*active_slot_id_].fb = fb;
}

void HwcLayer::PopulateLayerData() {
  ImportFb();

  if (!active_slot_id_.has_value()) {
    ALOGE("Internal error: populate layer data called without active slot");
    return;
  }

  if (slots_.count(*active_slot_id_) == 0) {
    return;
  }

  layer_data_.bi = slots_[*active_slot_id_].bi;
  layer_data_.fb = slots_[*active_slot_id_].fb;

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

void HwcLayer::ClearSlots() {
  slots_.clear();
  active_slot_id_.reset();
}

}  // namespace android