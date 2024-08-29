#include "intel_blit.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>

#include <drm_fourcc.h>
#include <xf86drm.h>
#include <log/log.h>
#include <cstdint>
#include "i915_prelim.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

#define MI_NOOP (0)
#define MI_FLUSH_DW (0x26 << 23)
#define MI_BATCH_BUFFER_END (0x0a << 23)

#define XY_SRC_COPY_BLT_CMD ((0x2 << 29) | (0x53 << 22) | 8)  // gen >> 8
#define XY_SRC_COPY_BLT_WRITE_ALPHA (1 << 21)
#define XY_SRC_COPY_BLT_WRITE_RGB (1 << 20)
#define XY_SRC_COPY_BLT_SRC_TILED (1 << 15)
#define XY_SRC_COPY_BLT_DST_TILED (1 << 11)

#define XY_TILE_LINEAR                           0
#define XY_TILE_X                                1
#define XY_TILE_4                                2
#define XY_TILE_64                               3

// GEN 125
#define HALIGN_16                                0
#define HALIGN_32                                1
#define HALIGN_64                                2
#define HALIGN_128                               3
#define VALIGN_4                                 1
#define VALIGN_8                                 2
#define VALIGN_16                                3

// See https://gfxspecs.intel.com/Predator/Home/Index/21523
#define XY_BLOCK_COPY_BLT_CMD ((0x2 << 29) | (0x41 << 22) | (0x14))

static void batch_reset(struct intel_info *info) {
  info->cur = info->vaddr;
}

static int batch_create(struct intel_info *info) {
  struct drm_i915_gem_create create;
  struct drm_i915_gem_mmap mmap_arg;
  int ret = 0;
  memset(&create, 0, sizeof(create));
  memset(&mmap_arg, 0, sizeof(mmap_arg));
  create.size = info->size;
  ret = ioctl(info->fd, DRM_IOCTL_I915_GEM_CREATE, &create);
  if (ret < 0) {
    ALOGE("failed to create buffer\n");
    info->batch_handle = 0;
    return ret;
  }
  info->batch_handle = create.handle;
  mmap_arg.handle = create.handle;
  mmap_arg.size = info->size;
  ret = ioctl(info->fd, DRM_IOCTL_I915_GEM_MMAP, &mmap_arg);
  if (ret < 0) {
    drmCloseBufferHandle(info->fd, info->batch_handle);
    info->batch_handle = 0;
    ALOGE("buffer map failure\n");
    return ret;
  }
  info->vaddr = (uint32_t *)mmap_arg.addr_ptr;
  batch_reset(info);
  return ret;
}

__attribute__((unused))
static int batch_count(struct intel_info *info) {
  return info->cur - info->vaddr;
}

static void batch_dword(struct intel_info *info, uint32_t dword) {
  *info->cur++ = dword;
}

static void batch_destroy(struct intel_info *info) {
  if (info->batch_handle) {
    drmCloseBufferHandle(info->fd, info->batch_handle);
    info->batch_handle = 0;
  }
}

static int batch_init(struct intel_info *info) {
  int ret;
  info->size = 4096;
  info->fd = intel_i915_fd();
  ret = batch_create(info);
  return ret;
}

static int batch_submit(struct intel_info *info, uint32_t src, uint32_t dst,
                        uint64_t src_offset, uint64_t dst_offset,
                        uint32_t in_fence_handle, uint32_t *out_fence_handle) {
  int ret;
  batch_dword(info, MI_BATCH_BUFFER_END);
  struct drm_i915_gem_exec_object2 obj[3];
  struct drm_i915_gem_execbuffer2 execbuf;
  struct drm_i915_gem_exec_fence fence_array[2] = {
    {
      .handle = *out_fence_handle,
      .flags = I915_EXEC_FENCE_SIGNAL,
    },
    {
      .handle = in_fence_handle,
      .flags = I915_EXEC_FENCE_WAIT,
    },
  };
  memset(obj, 0, sizeof(obj));
  obj[0].handle = dst;
  obj[0].offset = dst_offset;
  obj[0].flags = EXEC_OBJECT_PINNED | EXEC_OBJECT_WRITE;

  obj[1].handle = src;
  obj[1].offset = src_offset;
  obj[1].flags = EXEC_OBJECT_PINNED;
  obj[2].handle = info->batch_handle;
  obj[2].offset = 0;
  obj[2].flags = EXEC_OBJECT_PINNED;
  memset(&execbuf, 0, sizeof(execbuf));
  execbuf.buffers_ptr = (__u64)&obj;
  execbuf.buffer_count = 3;
  execbuf.flags = I915_EXEC_BLT;
  execbuf.flags |= I915_EXEC_NO_RELOC;
  execbuf.flags |= I915_EXEC_FENCE_ARRAY;
  execbuf.cliprects_ptr = (__u64)(fence_array);
  execbuf.num_cliprects = ARRAY_SIZE(fence_array) - (in_fence_handle == 0 ? 1 : 0);
  ret = ioctl(info->fd, DRM_IOCTL_I915_GEM_EXECBUFFER2_WR, &execbuf);
  if (ret < 0) {
    ALOGE("submit batchbuffer failure, ret:%d\n", ret);
    return -1;
  }
  struct drm_i915_gem_wait wait;
  memset(&wait, 0, sizeof(wait));
  wait.bo_handle = info->batch_handle;
  wait.timeout_ns = 1000 * 1000 * 1000;
  ioctl(info->fd, DRM_IOCTL_I915_GEM_WAIT, &wait);

  batch_reset(info);
  return 0;
}

__attribute__((unused))
static int emit_src_blit_commands(struct intel_info *info,
                                  uint32_t stride, uint32_t bpp,
                                  uint32_t tiling,
                                  uint16_t width, uint16_t height,
                                  uint64_t src_offset, uint64_t dst_offset) {
  uint32_t cmd, br13, pitch;
  if (!info->init) {
    ALOGE("Blitter is not initialized\n");
    return -1;
  }

  cmd = XY_SRC_COPY_BLT_CMD;
  br13 = 0xcc << 16;
  pitch = stride;
  switch (bpp) {
    case 1:
      break;
    case 2:
      br13 |= (1 << 24);
      break;
    case 4:
      br13 |= (1 << 24) | (1 << 25);
      cmd |= XY_SRC_COPY_BLT_WRITE_ALPHA | XY_SRC_COPY_BLT_WRITE_RGB;
      break;
    default:
      ALOGE("unknown bpp (%u)\n", bpp);
      return -1;
  }
  if (tiling != I915_TILING_NONE) {
    pitch >>= 3;
    cmd |= XY_SRC_COPY_BLT_DST_TILED;
    cmd |= XY_SRC_COPY_BLT_SRC_TILED;
  }
  batch_dword(info, cmd);
  batch_dword(info, br13 | (pitch & 0xffff));
  batch_dword(info, 0);
  batch_dword(info, (height << 16) | width);
  batch_dword(info, dst_offset);
  batch_dword(info, dst_offset >> 32);

  batch_dword(info, 0);
  batch_dword(info, (pitch & 0xffff));
  batch_dword(info, src_offset);
  batch_dword(info, src_offset >> 32);

  batch_dword(info, MI_FLUSH_DW | 2);
  batch_dword(info, 0);
  batch_dword(info, 0);
  batch_dword(info, 0);

  return 0;
}

static uint32_t tiling_to_xy_block_tiling(uint32_t tiling) {
  switch (tiling) {
  case I915_TILING_4:
    return XY_TILE_4;
  case I915_TILING_X:
    return XY_TILE_X;
  case I915_TILING_NONE:
    return XY_TILE_LINEAR;
  default:
    ALOGE("Invalid tiling for XY_BLOCK_COPY_BLT");
  }
  return XY_TILE_LINEAR;
}

// For some reson unknown to me, BLOCK_BLIT command is much slower than
// SRC_BLIT. So we prefer the latter one in spite of the fact that SRC_BLIT
// will be remvoed in GPUs in future generations.
__attribute__((unused))
static int emit_block_blit_commands(struct intel_info *info,
                                    uint32_t stride, uint32_t bpp,
                                    uint32_t tiling,
                                    uint16_t width, uint16_t height,
                                    uint64_t src_offset, uint64_t dst_offset) {
  uint32_t cmd, pitch;
  uint32_t color_depth;
  if (!info->init) {
    return -1;
  }

  switch (bpp) {
  case 1:
    color_depth = 0b00;
    break;
  case 2:
    color_depth = 0b01;
    break;
  case 4:
    color_depth = 0b10;
    break;
  case 8:
    color_depth = 0b11;
    break;
  default:
    ALOGE("unknown bpp (%u)\n", bpp);
    return -1;
  }
  cmd = XY_BLOCK_COPY_BLT_CMD | (color_depth << 19);
  pitch = stride;
  if (tiling != I915_TILING_NONE) {
    pitch >>= 2;
  }
  batch_dword(info, cmd);
  batch_dword(info, (tiling_to_xy_block_tiling(tiling) << 30) | (info->mocs.blitter_dst << 21) | (pitch & 0xffff));
  batch_dword(info, 0); // dst y1 (top) x1 (left)
  batch_dword(info, (height << 16) | width); // dst y2 (bottom) x2 (right)
  // 4
  batch_dword(info, dst_offset);
  batch_dword(info, dst_offset >> 32);
  batch_dword(info, (0x1 << 31)); // system memory
  batch_dword(info, 0); // src y1 (top) x1 (left)
  // 8
  batch_dword(info, (tiling_to_xy_block_tiling(tiling) << 30) | (info->mocs.blitter_src << 21) | (pitch & 0xffff));
  batch_dword(info, src_offset);
  batch_dword(info, src_offset >> 32);
  batch_dword(info, (0x0 << 31)); // local memory
  // 12
  batch_dword(info, 0);
  batch_dword(info, 0);
  batch_dword(info, 0);
  batch_dword(info, 0);
  // 16
  batch_dword(info, (0x1 << 29) | ((width - 1) << 14) | (height - 1));
  batch_dword(info, pitch << 4); // Q Pitch can be zero?
  batch_dword(info, (VALIGN_4 << 3) | (HALIGN_32));
  batch_dword(info, (0x1 << 29) | ((width - 1) << 14) | (height - 1));
  // 20
  batch_dword(info, pitch << 4); // Q Pitch can be zero?
  batch_dword(info, (VALIGN_4 << 3) | (HALIGN_32));

  batch_dword(info, MI_FLUSH_DW | 2);
  batch_dword(info, 0);
  batch_dword(info, 0);
  batch_dword(info, 0);

  return 0;
}

int intel_blit(struct intel_info *info, uint32_t dst, uint32_t src,
               uint32_t stride, uint32_t bpp, uint32_t tiling, uint16_t width,
               uint16_t height, int in_fence, int *out_fence) {
  uint32_t in_fence_handle = 0;
  uint32_t out_fence_handle = 0;
  const uint64_t kSrcOffset = 16 * 1024 * 1024;
  const uint64_t kDstOffset = 256 * 1024 * 1024;
  int ret;

  ret = drmSyncobjCreate(info->fd, 0, &out_fence_handle);
  if (ret) {
    ALOGE("failed to create sync object\n");
    goto out;
  }

  if (in_fence >= 0) {
    ret = drmSyncobjCreate(info->fd, 0, &in_fence_handle);
    if (ret) {
      ALOGE("%s:%u: failed to create syncobj\n", __func__, __LINE__);
      goto out;
    }
    ret = drmSyncobjImportSyncFile(info->fd, in_fence_handle, in_fence);
    if (ret) {
      ALOGE("failed to import syncobj (fd=%d)\n", in_fence);
      goto out;
    }
  }

  ret = emit_src_blit_commands(info, stride, bpp, tiling, width, height, kSrcOffset, kDstOffset);
  if (ret) {
    ALOGE("failed to fill commands\n");
    goto out;
  }

  ret = batch_submit(info, src, dst, kSrcOffset, kDstOffset, in_fence_handle, &out_fence_handle);
  if (ret) {
    ALOGE("failed to submit batch\n");
    goto out;
  }
  ret = drmSyncobjExportSyncFile(info->fd, out_fence_handle, out_fence);
  if (ret) {
    ALOGE("failed to export syncobj (handle=%u)\n", out_fence_handle);
    goto out;
  }

out:
  if (in_fence_handle) {
    drmSyncobjDestroy(info->fd, in_fence_handle);
  }
  if (out_fence_handle) {
    drmSyncobjDestroy(info->fd, out_fence_handle);
  }
  return 0;
}

int intel_blit_destroy(struct intel_info *info) {
  if (info->init) {
    batch_destroy(info);
    info->init = 0;
  }
  return 0;
}

int intel_blit_init(struct intel_info *info) {
  memset(info, 0, sizeof(*info));
  batch_init(info);
  info->init = 1;
  info->mocs.blitter_dst = 2 << 1;
  info->mocs.blitter_src = 2 << 1;
  ALOGV("gpubilit init success\n");
  return 0;
}

#define ALIGN(value, alignment) ((value + alignment - 1) & ~(alignment - 1))

struct iris_memregion {
  struct drm_i915_gem_memory_class_instance region;
  uint64_t size;
};

struct i915_device {
  bool initialized;
  bool has_local_mem;
  struct iris_memregion vram, sys;
};

static struct i915_device dev = {
  .initialized = false,
};

static inline void
intel_gem_add_ext(__u64 *ptr, uint32_t ext_name, struct i915_user_extension *ext) {
  __u64 *iter = ptr;
  while (*iter != 0) {
    iter = (__u64 *) &((struct i915_user_extension *)(uintptr_t)*iter)->next_extension;
  }
  ext->name = ext_name;
  *iter = (uintptr_t) ext;
}


static void prelim_i915_bo_update_meminfo(struct i915_device *i915_dev,
    const struct prelim_drm_i915_query_memory_regions *meminfo) {
  i915_dev->has_local_mem = false;
  for (uint32_t i = 0; i < meminfo->num_regions; i++) {
    const struct prelim_drm_i915_memory_region_info *mem = &meminfo->regions[i];
    switch (mem->region.memory_class) {
    case I915_MEMORY_CLASS_SYSTEM:
      i915_dev->sys.region = mem->region;
      i915_dev->sys.size = mem->probed_size;
      break;
    case I915_MEMORY_CLASS_DEVICE:
      i915_dev->vram.region = mem->region;
      i915_dev->vram.size = mem->probed_size;
      i915_dev->has_local_mem = i915_dev->vram.size > 0;
      break;
    default:
      break;
    }
  }
}

static int intel_update_meminfo(int fd) {
  if (dev.initialized) {
    return 0;
  }

  struct prelim_drm_i915_query_memory_regions *meminfo = nullptr;

  struct drm_i915_query_item item = {
    .query_id = PRELIM_DRM_I915_QUERY_MEMORY_REGIONS,
  };

  struct drm_i915_query query = {
    .num_items = 1,
    .items_ptr = (uintptr_t)&item,
  };
  int ret = drmIoctl(fd, DRM_IOCTL_I915_QUERY, &query);
  if (ret < 0) {
    ALOGE("Failed to query PRELIM_DRM_I915_QUERY_MEMORY_REGIONS\n");
    return -1;
  }
  if (item.length <= 0) {
    return -1;
  }

  meminfo = static_cast<struct prelim_drm_i915_query_memory_regions *>(calloc(1, item.length));
  if (!meminfo) {
    ALOGE("Out of memory\n");
    return -1;
  }
  item.data_ptr = (uintptr_t)meminfo;
  ret = drmIoctl(fd, DRM_IOCTL_I915_QUERY, &query);
  if (ret < 0 || item.length <= 0) {
    free(meminfo);
    ALOGE("%s:%d DRM_IOCTL_I915_QUERY error\n", __FUNCTION__, __LINE__);
    return -1;
  }
  prelim_i915_bo_update_meminfo(&dev, meminfo);
  dev.initialized = true;
  free(meminfo);
  return 0;
}

int intel_i915_fd() {
  static int temp, fd = -1;
  char device_path[32];
  if (fd >= 0)
    return fd;
  for (int i = 0; i < 64; ++i) {
    sprintf(device_path, "/dev/dri/renderD%u", 128 + i);
    temp = open(device_path, O_RDWR | O_CLOEXEC);
    if (temp < 0) {
      return temp;
    }
    drmVersionPtr version = drmGetVersion(temp);
    if (strncmp(version->name, "i915", version->name_len)) {
      continue;
    }
    intel_update_meminfo(temp);
    if (dev.has_local_mem) {
      fd = temp;
      break;
    }
  }
  return fd;
}

int intel_create_buffer(uint32_t width, uint32_t height,
                        __attribute__((unused)) uint32_t format,
                        uint64_t modifier, uint32_t *out_handle) {
  assert(out_handle != nullptr);
  int fd = intel_i915_fd();
  uint32_t total_size;
  uint32_t tiling = I915_TILING_NONE;
  uint32_t horizontal_alignment = 64;
  uint32_t vertical_alignment = 4;
  const uint32_t bpp = 4;
  uint32_t aligned_height, stride = width * bpp;
  switch (modifier) {
  case DRM_FORMAT_MOD_LINEAR:
    tiling = I915_TILING_NONE;
    break;
  case I915_FORMAT_MOD_X_TILED:
    tiling = I915_TILING_X;
    break;
  case I915_FORMAT_MOD_Y_TILED:
  case I915_FORMAT_MOD_Y_TILED_CCS:
  case I915_FORMAT_MOD_Yf_TILED:
  case I915_FORMAT_MOD_Yf_TILED_CCS:
    tiling = I915_TILING_Y;
    break;
  case I915_FORMAT_MOD_4_TILED:
    tiling = I915_TILING_4;
    break;
  }
  switch (tiling) {
  default:
  case I915_TILING_NONE:
    /*
     * The Intel GPU doesn't need any alignment in linear mode,
     * but libva requires the allocation stride to be aligned to
     * 16 bytes and height to 4 rows. Further, we round up the
     * horizontal alignment so that row start on a cache line (64
     * bytes).
     */
    horizontal_alignment = 64;
    vertical_alignment = 4;
    break;

  case I915_TILING_X:
    horizontal_alignment = 512;
    vertical_alignment = 8;
    break;

  case I915_TILING_Y:
    horizontal_alignment = 128;
    vertical_alignment = 32;
    break;

  case I915_TILING_4:
    horizontal_alignment = 128;
    vertical_alignment = 32;
    break;
  }
  aligned_height = ALIGN(height, vertical_alignment);
  stride = ALIGN(stride, horizontal_alignment);
  total_size = aligned_height * stride;

  struct drm_i915_gem_create_ext gem_create_ext = {
    .size = ALIGN(total_size, 0x10000),
  };
  struct drm_i915_gem_memory_class_instance regions[2];
  struct drm_i915_gem_create_ext_memory_regions ext_regions = {
    .base = {.name = I915_GEM_CREATE_EXT_MEMORY_REGIONS},
    .num_regions = 0,
    .regions = (uintptr_t)regions,
  };
  regions[ext_regions.num_regions++] = dev.sys.region;
  intel_gem_add_ext(&gem_create_ext.extensions,
                    I915_GEM_CREATE_EXT_MEMORY_REGIONS,
                    &ext_regions.base);
  int ret = drmIoctl(fd, DRM_IOCTL_I915_GEM_CREATE_EXT, &gem_create_ext);
  if (ret) {
    ALOGE("drv: DRM_IOCTL_I915_GEM_CREATE_EXT failed (size=%llu)\n",
      gem_create_ext.size);
    return -errno;
  }
  *out_handle = gem_create_ext.handle;
  return 0;
}
