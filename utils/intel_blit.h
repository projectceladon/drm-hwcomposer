/*
 * Copyright (C) 2024 Intel Corporation
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

#ifndef __INTEL_BLIT_H__
#define __INTEL_BLIT_H__

#include <i915_drm.h>
#include <stdint.h>

#include "UniqueFd.h"

#define I915_TILING_4 9

struct intel_info {
  android::UniqueFd fd;
  uint32_t batch_handle;
  uint32_t *vaddr;
  uint32_t *cur;
  uint64_t size;
  int init;
  struct {
    uint32_t blitter_src;
    uint32_t blitter_dst;
  } mocs;
  int addr_offset;
  uint32_t context_id;
};

int intel_blit_destroy(struct intel_info *info);
int intel_blit_init(struct intel_info *info);
int intel_blit(struct intel_info *info, uint32_t dst, uint32_t src,
               uint32_t stride, uint32_t bpp, uint32_t tiling, uint16_t width,
               uint16_t height, int in_fence, int *out_fence);
int intel_create_buffer(struct intel_info *info,
                        uint32_t width, uint32_t height, uint32_t format,
                        uint64_t modifier, uint32_t *out_handle);
int intel_dgpu_fd();
bool virtio_gpu_allow_p2p(int virtgpu_fd);

class IntelBlitter {
 public:
  IntelBlitter() {
    intel_blit_init(&info);
  }
  ~IntelBlitter() {
    intel_blit_destroy(&info);
  }
  bool Initialized() {
    return info.init;
  }
  int GetFd() {
    return info.fd.Get();
  }
  bool Blit(uint32_t dst, uint32_t src, uint32_t stride, uint32_t bpp,
            uint16_t width, uint16_t height, int in_fence, int *out_fence);
  bool CreateShadowBuffer(uint32_t width, uint32_t height, uint32_t format,
                          uint64_t modifier, uint32_t *out_handle);
 private:
  struct intel_info info;
};

#endif  // __INTEL_BLIT_H__
