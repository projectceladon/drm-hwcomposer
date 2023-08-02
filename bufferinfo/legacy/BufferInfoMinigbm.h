/*
 * Copyright (C) 2018 The Android Open Source Project
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

#ifndef BUFFERINFOMINIGBM_H_
#define BUFFERINFOMINIGBM_H_

#include <hardware/gralloc.h>
#include <hardware/gralloc1.h>

#include "bufferinfo/BufferInfoGetter.h"

#define DRV_MAX_PLANES 4
#define DRV_MAX_FDS (DRV_MAX_PLANES + 1)

enum INITIALIZE_ERROR{
	INITIALIZE_CALLOC_ERROR = 1,
	INITIALIZE_GET_MODULE_ERROR,
	INITIALIZE_OPEN_DEVICE_ERROR,
	INITIALIZE_NONE = 0,
};

struct dri2_drm_display
{
   int fd;
   const gralloc_module_t *gralloc;
   uint16_t gralloc_version;
   gralloc1_device_t *gralloc1_dvc;
   GRALLOC1_PFN_LOCK pfn_lock;
   GRALLOC1_PFN_GET_FORMAT pfn_getFormat;
   GRALLOC1_PFN_UNLOCK pfn_unlock;
   GRALLOC1_PFN_IMPORT_BUFFER pfn_importBuffer;
   GRALLOC1_PFN_RELEASE pfn_release;
   GRALLOC1_PFN_GET_STRIDE pfn_get_stride;
};

namespace android {

class BufferInfoMinigbm : public LegacyBufferInfoGetter {
 public:
  using LegacyBufferInfoGetter::LegacyBufferInfoGetter;
  auto GetBoInfo(buffer_handle_t handle) -> std::optional<BufferInfo> override;
  int ValidateGralloc() override;
  static void InitializeGralloc1(DrmDevice *drmDevice);
  static void DumpBuffer(DrmDevice *drmDevice, buffer_handle_t handle, BufferInfo buffer_info);
};

}  // namespace android

#endif
