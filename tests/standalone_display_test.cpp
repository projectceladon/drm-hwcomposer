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

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
// #define LOG_NDEBUG 0 // Uncomment to see HWC2 API calls in logcat

#define LOG_TAG "standalone_display_test"

#include <cutils/properties.h>
#include <string.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include <algorithm>
#include <cinttypes>

#include "DrmHwcTwo.h"
#include "HwcLayer.h"
#include "backend/Backend.h"
#include "utils/log.h"

namespace android {
class DrmHwcTwoTest : public DrmHwcTwo {
 public:
  DrmHwcTwoTest() {
    GetResMan().Init();
  }
  ~DrmHwcTwoTest() override = default;
};
struct Drmhwc2DeviceTest : hwc2_device {
  DrmHwcTwoTest drmhwctwotest;
};
}  // namespace android

typedef struct buffer_object {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t handle;
  uint32_t size;
  uint8_t* vaddr;
  uint32_t fb_id;

} buffer_object_t;
/// fb buffer
struct buffer_object bo_[2];
static uint32_t fb_num_ = 2;

int modeset_create_fb(int fd, buffer_object_t* bo);
void modeset_destory_fb(int fd, buffer_object_t* bo);
void fill_buffer(int fd, buffer_object_t* bo, uint8_t index);
void init_display(android::HwcDisplay* hwc_display, hwc2_layer_t& layer_id);
void wait_blank(int fd);

int main(int argc, const char** argv) {
  ALOGI("usage: %s [fb num]:2 \n", argv[0]);

  auto ctx = std::make_unique<android::Drmhwc2DeviceTest>();
  if (!ctx) {
    ALOGE("Failed to allocate DrmHwcTwo\n");
    return -ENOMEM;
  }

  android::DrmHwcTwoTest* hwc2 = &ctx->drmhwctwotest;
  auto hwc_display = hwc2->GetDisplay(hwc2_display_t(0));
  if (hwc_display == nullptr) {
    ALOGE("Failed to get display\n");
    return -ENOMEM;
  }
  hwc2_layer_t layer_id = 0;
  init_display(hwc_display, layer_id);

  for (uint8_t i = 0; i < fb_num_; i++) {
    modeset_create_fb(hwc_display->GetPipe().device->GetFd(), &bo_[i]);
    fill_buffer(hwc_display->GetPipe().device->GetFd(), &bo_[i], i);
  }

  int swap = 0;
  int32_t out_fence = 0;
  while (true) {
    // swap buffer
    android::HwcLayer* layer = hwc_display->get_layer(layer_id);
    layer->SetLayerFBid(bo_[swap].fb_id);
    swap ^= 1;

    wait_blank(hwc_display->GetPipe().device->GetFd());
    if (hwc_display->PresentDisplay(&out_fence) != HWC2::Error::None) {
      ALOGI("Failed to present display\n");
      break;
    }
    close(out_fence);
    out_fence = 0;
  }

  for (uint8_t i = 0; i < fb_num_; i++) {
    modeset_destory_fb(hwc_display->GetPipe().device->GetFd(), &bo_[i]);
  }
  return 0;
}

void wait_blank(int fd) {
  drmVBlank vblank;
  vblank.request.type = DRM_VBLANK_RELATIVE;
  vblank.request.sequence = 1;
  vblank.request.signal = 0;
  drmWaitVBlank(fd, &vblank);
}

void init_display(android::HwcDisplay* hwc_display, hwc2_layer_t& layer_id) {
  int32_t width = 0;
  int32_t height = 0;
  hwc2_config_t config = 0;
  hwc_display->GetActiveConfig(&config);
  hwc_display->GetDisplayAttribute(config, static_cast<int32_t>(HWC2::Attribute::Width),
                                   &width);
  hwc_display->GetDisplayAttribute(config, static_cast<int32_t>(HWC2::Attribute::Height),
                                   &height);
  bo_[0].width = width, bo_[0].height = height;
  bo_[1].width = width, bo_[1].height = height;
  /// create and set layer info
  hwc_display->CreateLayer(&layer_id);
  android::HwcLayer* layer = hwc_display->get_layer(layer_id);
  layer->SetLayerBlendMode((int32_t)HWC2::BlendMode::Premultiplied);
  layer->SetValidatedType(HWC2::Composition::Device);
  layer->AcceptTypeChange();
  hwc_rect_t display_frame = {0, 0, width, height};
  layer->SetLayerDisplayFrame(display_frame);
  hwc_frect_t source_crop = {0.0, 0.0, static_cast<float>(width), static_cast<float>(height)};
  layer->SetLayerSourceCrop(source_crop);
}

int modeset_create_fb(int fd, buffer_object_t* bo) {
  int ret;
  /// 1 create dumb buffer
  struct drm_mode_create_dumb create = {};
  memset(&create, 0, sizeof(create));
  create.width = bo->width;
  create.height = bo->height;
  create.bpp = 32;
  create.handle = 0;
  ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
  if (ret < 0) {
    fprintf(stderr, "cannot create dumb buffer (%d): %m\n", errno);
    return -errno;
  }
  bo->stride = create.pitch;
  bo->size = create.size;
  bo->handle = create.handle;

  // 2 ADDFB2
  struct drm_mode_fb_cmd2 f;
  memset(&f, 0, sizeof(f));
  f.width = bo->width;
  f.height = bo->height;
  f.pixel_format = DRM_FORMAT_ARGB8888;
  f.flags = 0;
  f.handles[0] = bo->handle;
  f.modifier[0] = 0;
  f.pitches[0] = bo->stride;
  f.offsets[0] = 0;

  ret = drmIoctl(fd, DRM_IOCTL_MODE_ADDFB2, &f);
  if (ret < 0) {
    fprintf(stderr, "cannot addfb2 buffer (%d): %m\n", errno);
    return -errno;
  }
  bo->fb_id = f.fb_id;
  return ret;
}

void modeset_destory_fb(int fd, buffer_object_t* bo) {
  struct drm_mode_destroy_dumb destory = {};
  drmModeRmFB(fd, bo->fb_id);
  destory.handle = bo->handle;
  drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destory);
}

void fill_buffer(int fd, buffer_object_t* bo, uint8_t index) {
  struct drm_mode_map_dumb map = {};
  memset(&map, 0, sizeof(map));
  map.handle = bo->handle;
  int ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
  if (ret)
    fprintf(stderr, "cannot map dumb buffer (%d): %m\n", errno);

  bo->vaddr = (uint8_t*)mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                             fd, map.offset);
  if (bo->vaddr == MAP_FAILED)
    fprintf(stderr, "cannot mmap dumb buffer (%d): %m\n", errno);

  static uint8_t colorinfo[2][4] = {
      {0x32, 0x80, 0x00, 0},
      {0x50, 0, 0x40, 0},
  };
  memset(bo->vaddr, 0, bo->size);
  uint8_t A = colorinfo[index][0];
  uint8_t R = colorinfo[index][1];
  uint8_t G = colorinfo[index][2];
  uint8_t B = colorinfo[index][3];
  uint32_t* pintbuffer = (uint32_t*)bo->vaddr;
  uint32_t value = (A << 24) | (R << 16) | (G << 8) | B;
  for (int i = 0; i < bo->width * bo->height; i++) {
    pintbuffer[i] = value;
  }

  munmap(bo->vaddr, bo->size);
}