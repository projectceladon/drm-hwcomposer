/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include "GrallocBufferHandler.h"

#include <cutils/native_handle.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#include <ui/GraphicBuffer.h>
namespace android {

Gralloc1BufferHandler::Gralloc1BufferHandler()
    : gralloc_(nullptr),
      device_(nullptr),
      create_descriptor_(nullptr),
      set_consumer_usage_(nullptr),
      set_dimensions_(nullptr),
      set_format_(nullptr),
      set_producer_usage_(nullptr),
      allocate_(nullptr) {
}

Gralloc1BufferHandler::~Gralloc1BufferHandler() {
  gralloc1_device_t *gralloc1_dvc =
      reinterpret_cast<gralloc1_device_t *>(device_);
  gralloc1_dvc->common.close(device_);
}

bool Gralloc1BufferHandler::Init() {
  int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                          (const hw_module_t **)&gralloc_);
  if (ret) {
    ALOGE("Failed to get gralloc module");
    return false;
  }

  ret = gralloc_->methods->open(gralloc_, GRALLOC_HARDWARE_MODULE_ID, &device_);
  if (ret) {
    ALOGE("Failed to open gralloc module");
    return false;
  }

  gralloc1_device_t *gralloc1_dvc = reinterpret_cast<gralloc1_device_t *>(device_);

  create_descriptor_ = reinterpret_cast<GRALLOC1_PFN_CREATE_DESCRIPTOR>(
      gralloc1_dvc->getFunction(gralloc1_dvc,
                                GRALLOC1_FUNCTION_CREATE_DESCRIPTOR));
  set_consumer_usage_ = reinterpret_cast<GRALLOC1_PFN_SET_CONSUMER_USAGE>(
      gralloc1_dvc->getFunction(gralloc1_dvc,
                                GRALLOC1_FUNCTION_SET_CONSUMER_USAGE));
  set_dimensions_ =
      reinterpret_cast<GRALLOC1_PFN_SET_DIMENSIONS>(gralloc1_dvc->getFunction(
          gralloc1_dvc, GRALLOC1_FUNCTION_SET_DIMENSIONS));
  set_format_ = reinterpret_cast<GRALLOC1_PFN_SET_FORMAT>(
      gralloc1_dvc->getFunction(gralloc1_dvc, GRALLOC1_FUNCTION_SET_FORMAT));
  set_producer_usage_ = reinterpret_cast<GRALLOC1_PFN_SET_PRODUCER_USAGE>(
      gralloc1_dvc->getFunction(gralloc1_dvc,
                                GRALLOC1_FUNCTION_SET_PRODUCER_USAGE));
  allocate_ = reinterpret_cast<GRALLOC1_PFN_ALLOCATE>(
      gralloc1_dvc->getFunction(gralloc1_dvc, GRALLOC1_FUNCTION_ALLOCATE));
  return true;
}

bool Gralloc1BufferHandler::CreateBuffer(uint32_t w, uint32_t h,buffer_handle_t *handle) {
  uint64_t gralloc1_buffer_descriptor_t;
  gralloc1_device_t *gralloc1_dvc =
      reinterpret_cast<gralloc1_device_t *>(device_);

  create_descriptor_(gralloc1_dvc, &gralloc1_buffer_descriptor_t);
  uint32_t usage = 0;
  set_format_(gralloc1_dvc, gralloc1_buffer_descriptor_t, HAL_PIXEL_FORMAT_RGBA_8888);

  usage |= GRALLOC1_CONSUMER_USAGE_HWCOMPOSER |
            GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET |
            GRALLOC1_CONSUMER_USAGE_GPU_TEXTURE;


  set_consumer_usage_(gralloc1_dvc, gralloc1_buffer_descriptor_t, usage);
  set_producer_usage_(gralloc1_dvc, gralloc1_buffer_descriptor_t, usage);
  set_dimensions_(gralloc1_dvc, gralloc1_buffer_descriptor_t, w, h);
  allocate_(gralloc1_dvc, 1, &gralloc1_buffer_descriptor_t, handle);

  return true;
}
}  // namespace android
