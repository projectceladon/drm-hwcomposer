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

#ifndef OS_ANDROID_Gralloc1BufferHandler_H_
#define OS_ANDROID_Gralloc1BufferHandler_H_
#include <hardware/gralloc1.h>

namespace android {

class Gralloc1BufferHandler {
 public:
  explicit Gralloc1BufferHandler();
  ~Gralloc1BufferHandler();
  bool Init();
  bool CreateBuffer(uint32_t w, uint32_t h, buffer_handle_t *handle);
 private:
  const hw_module_t *gralloc_;
  hw_device_t *device_;
  GRALLOC1_PFN_CREATE_DESCRIPTOR create_descriptor_;
  GRALLOC1_PFN_SET_CONSUMER_USAGE set_consumer_usage_;
  GRALLOC1_PFN_SET_DIMENSIONS set_dimensions_;
  GRALLOC1_PFN_SET_FORMAT set_format_;
  GRALLOC1_PFN_SET_PRODUCER_USAGE set_producer_usage_;
  GRALLOC1_PFN_ALLOCATE allocate_;
};

}  // namespace android
#endif  // OS_ANDROID_Gralloc1BufferHandler_H_
