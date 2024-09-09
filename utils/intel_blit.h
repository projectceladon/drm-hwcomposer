#ifndef __INTEL_BLIT_H__
#define __INTEL_BLIT_H__

#include <i915_drm.h>
#include <stdint.h>

#define I915_TILING_4 9

struct intel_info {
  int fd;
  uint32_t batch_handle;
  uint32_t *vaddr;
  uint32_t *cur;
  uint64_t size;
  int init;
  struct {
    uint32_t blitter_src;
    uint32_t blitter_dst;
  } mocs;
};

int intel_blit_destroy(struct intel_info *info);
int intel_blit_init(struct intel_info *info);
int intel_blit(struct intel_info *info, uint32_t dst, uint32_t src,
               uint32_t stride, uint32_t bpp, uint32_t tiling, uint16_t width,
               uint16_t height, int in_fence, int *out_fence);
int intel_create_buffer(uint32_t width, uint32_t height, uint32_t format,
                        uint64_t modifier, uint32_t *out_handle);
int intel_i915_fd();
bool virtio_gpu_allow_p2p(int virtgpu_fd);

#endif  // __INTEL_BLIT_H__
