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

#define LOG_TAG "standalone_display_stress_test"

#include <xf86drm.h>
#include <algorithm>
#include <cinttypes>
#include "DrmHwcTwo.h"
#include "HwcLayer.h"
#include "backend/Backend.h"
#include "utils/log.h"
#include <cutils/properties.h>
#include <string.h>
#include <sys/mman.h>
#include <vector>

#define MAX_DISPLAY_NUM 4
#define MAX_LAYER_NUM 7
#define MAX_SCALING_NUM 2

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

typedef struct layer_info {
  int w;
  int h;
  int x;
  int y;
  float alpha;  // this is the alpha for whole plane
  uint8_t A;    // this is the alpha in ARGB pixel value
  uint8_t R;
  uint8_t G;
  uint8_t B;
  uint8_t scaling;
} layer_info_t;

layer_info_t layer_infos_[MAX_DISPLAY_NUM][MAX_LAYER_NUM] = {
  {
    {1920 * 2, 1080 * 2, 0, 0, 0.5, 0x32, 0x80, 0x00, 0, 1},
    {(1920 - 50 * 2) * 1, (1080 - 50 * 2) * 1, 50, 50, 0.5, 0x50, 0, 0,0x20, 0},
    {(1920 - 100 * 2) * 1, (1080 - 100 * 2) * 1, 100, 100, 0.5, 0x50, 0x20, 0, 0, 0},
    {1920 - 150 * 2, 1080 - 150 * 2, 150, 150, 0.5, 0x50, 0x34, 0x23, 0, 0},
    {1920 - 200 * 2, 1080 - 200 * 2, 200, 200, 0.5, 0x50, 0, 0x43, 0x80, 0},
    {1920 - 250 * 2, 1080 - 250 * 2, 250, 250, 0.5, 0x50, 0x40, 0, 0, 0},
    {1920 - 300 * 2, 1080 - 300 * 2, 300, 300, 0.5, 0xff, 0, 0x40, 0, 0},
  },
  {
    {1920 * 2, 1080 * 2, 0, 0, 0.5, 0x32, 0x80, 0x00, 0, 1},
    {(1920 - 50 * 2) * 1, (1080 - 50 * 2) * 1, 50, 50, 0.5, 0x50, 0, 0, 0x20, 0},
    {(1920 - 100 * 2) * 1, (1080 - 100 * 2) * 1, 100, 100, 0.5, 0x50, 0x20, 0, 0, 0},
    {1920 - 150 * 2, 1080 - 150 * 2, 150, 150, 0.5, 0x50, 0x34, 0x23, 0, 0},
    {1920 - 200 * 2, 1080 - 200 * 2, 200, 200, 0.5, 0x50, 0, 0x43, 0x80, 0},
    {1920 - 250 * 2, 1080 - 250 * 2, 250, 250, 0.5, 0x50, 0x40, 0, 0, 0},
    {1920 - 300 * 2, 1080 - 300 * 2, 300, 300, 0.5, 0xff, 0, 0x40, 0, 0},
  },
  {
    {1920 * 2, 1080 * 2, 0, 0, 0.5, 0x32, 0x80, 0x00, 0, 1},
    {(1920 - 50 * 2) * 1, (1080 - 50 * 2) * 1, 50, 50, 0.5, 0x50, 0, 0, 0x20, 0},
    {(1920 - 100 * 2) * 1, (1080 - 100 * 2) * 1, 100, 100, 0.5, 0x50, 0x20, 0, 0, 0},
    {1920 - 150 * 2, 1080 - 150 * 2, 150, 150, 0.5, 0x50, 0x34, 0x23, 0, 0},
    {1920 - 200 * 2, 1080 - 200 * 2, 200, 200, 0.5, 0x50, 0, 0x43, 0x80, 0},
    {1920 - 250 * 2, 1080 - 250 * 2, 250, 250, 0.5, 0x50, 0x40, 0, 0, 0},
    {1920 - 300 * 2, 1080 - 300 * 2, 300, 300, 0.5, 0xff, 0, 0x40, 0, 0},
  },
  {
    {1920 * 2, 1080 * 2, 0, 0, 0.5, 0x32, 0x80, 0x00, 0, 1},
    {(1920 - 50 * 2) * 1, (1080 - 50 * 2) * 1, 50, 50, 0.5, 0x50, 0, 0, 0x20, 0},
    {(1920 - 100 * 2) * 1, (1080 - 100 * 2) * 1, 100, 100, 0.5, 0x50, 0x20, 0, 0, 0},
    {1920 - 150 * 2, 1080 - 150 * 2, 150, 150, 0.5, 0x50, 0x34, 0x23, 0, 0},
    {1920 - 200 * 2, 1080 - 200 * 2, 200, 200, 0.5, 0x50, 0, 0x43, 0x80, 0},
    {1920 - 250 * 2, 1080 - 250 * 2, 250, 250, 0.5, 0x50, 0x40, 0, 0, 0},
    {1920 - 300 * 2, 1080 - 300 * 2, 300, 300, 0.5, 0xff, 0, 0x40, 0, 0},
  }
};

typedef struct buffer_object {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t handle;
  uint32_t size;
  uint8_t* vaddr;
  uint32_t fb_id;
  uint32_t fd;
  layer_info_t* layer_info;
  android::HwcLayer* layer;

} buffer_object_t;

static int layer_num_ = MAX_LAYER_NUM;
static int scaling_num_ = MAX_SCALING_NUM;

void init_layer_info(uint8_t pipe, uint8_t layer, int width, int height,
                     uint8_t scaling);
int modeset_create_fb(buffer_object_t* bo);
void modeset_destory_fb(buffer_object_t* bo);
void fill_buffer(buffer_object_t* bo);
void wait_blank(int fd);
void init_display(android::HwcDisplay* hwc_display, hwc2_display_t display_id,
                  std::vector<buffer_object_t>& buf_objs);

int main(int argc, const char** argv) {
  ALOGI("usage: %s [layer num] [scaling num]\n", argv[0]);
  if (argc != 3) {
    return 0;
  }
  layer_num_ = std::min(atoi(argv[1]), MAX_LAYER_NUM);
  scaling_num_ = std::min(atoi(argv[2]), MAX_SCALING_NUM);
  property_set("vendor.hwcomposer.planes.enabling", "1");
  auto ctx = std::make_unique<android::Drmhwc2DeviceTest>();
  if (!ctx) {
    ALOGE("Failed to allocate DrmHwcTwo");
    return -ENOMEM;
  }

  android::DrmHwcTwoTest* hwc2 = &ctx->drmhwctwotest;
  hwc2_display_t display_id = 0;
  std::vector<android::HwcDisplay*> hwc_displays;
  std::vector<buffer_object_t> buf_objs;
  while (true) {
    auto hwc_display = hwc2->GetDisplay(display_id);
    if (hwc_display == nullptr)
      break;
    init_display(hwc_display, display_id, buf_objs);
    hwc_displays.push_back(hwc_display);
    display_id++;
  }

  for (uint8_t i = 0; i < buf_objs.size(); i++) {
    modeset_create_fb(&buf_objs[i]);
    fill_buffer(&buf_objs[i]);
    buf_objs[i].layer->SetLayerFBid(buf_objs[i].fb_id);
  }

  time_t t0 = time(NULL);
  int32_t total_frame = 0;
  while (true) {
    for (uint8_t i = 0; i < hwc_displays.size(); i++) {
      auto hwc_display = hwc_displays[i];
      wait_blank(hwc_display->GetPipe().device->GetFd());
      int32_t out_fence = 0;
      if (hwc_display->PresentDisplay(&out_fence) != HWC2::Error::None) {
        ALOGI("Retry PresentDisplay again\n");
        wait_blank(hwc_display->GetPipe().device->GetFd());
        hwc_display->PresentDisplay(&out_fence);
      }
      close(out_fence);
    }
    ++total_frame;
    if (total_frame % 300 == 0) {
      time_t t1 = time(NULL);
      ALOGI("Total %lu secs for 300 frames, fps is %ld\n", t1 - t0,
            (300) / (t1 - t0));
      t0 = time(NULL);
    }
  }

  for (uint8_t i = 0; i < buf_objs.size(); i++) {
    modeset_destory_fb(&buf_objs[i]);
  }
  return 0;
}

void init_display(android::HwcDisplay* hwc_display, hwc2_display_t display_id,
                  std::vector<buffer_object_t>& buf_objs) {
  auto planes = hwc_display->GetPipe().GetUsablePlanes();
  int32_t width = 0;
  int32_t height = 0;
  hwc2_config_t config = 0;
  hwc_display->GetActiveConfig(&config);
  hwc_display->GetDisplayAttribute(config,
                                   static_cast<int32_t>(HWC2::Attribute::Width),
                                   &width);
  hwc_display->GetDisplayAttribute(config,
                                   static_cast<int32_t>(HWC2::Attribute::Height),
                                   &height);

  hwc2_layer_t layer_id = 0;
  for (uint8_t i = 0; i < layer_num_; i++) {
    hwc_display->CreateLayer(&layer_id);
    init_layer_info(display_id, i, width, height, i < scaling_num_);
    layer_info_t* layerinfo = &layer_infos_[display_id][layer_id];
    buffer_object_t bo;
    bo.width = layerinfo->w;
    bo.height = layerinfo->h;
    bo.layer_info = layerinfo;
    bo.fd = hwc_display->GetPipe().device->GetFd();
    android::HwcLayer* layer = hwc_display->get_layer(layer_id);
    bo.layer = layer;
    buf_objs.push_back(bo);
    layer->SetLayerZOrder(layer_id);
    layer->SetLayerBlendMode((int32_t)HWC2::BlendMode::Premultiplied);
    layer->SetLayerPlaneAlpha(layerinfo->A);
    layer->SetValidatedType(HWC2::Composition::Device);
    layer->AcceptTypeChange();
    hwc_rect_t display_frame = {layerinfo->x, layerinfo->y,
                                layerinfo->w + layerinfo->x,
                                layerinfo->h + layerinfo->y};
    if (layerinfo->scaling) {  // scaling
      display_frame = {layerinfo->x, layerinfo->y, width - layerinfo->x,
                       height - layerinfo->y};
    }
    layer->SetLayerDisplayFrame(display_frame);
    hwc_frect_t source_crop = {0.0, 0.0, static_cast<float>(layerinfo->w),
                               static_cast<float>(layerinfo->h)};
    layer->SetLayerSourceCrop(source_crop);
  }
}

void wait_blank(int fd) {
  drmVBlank vblank;
  vblank.request.type = DRM_VBLANK_RELATIVE;
  vblank.request.sequence = 1;
  vblank.request.signal = 0;
  drmWaitVBlank(fd, &vblank);
}

void init_layer_info(uint8_t pipe, uint8_t layer, int width, int height,
                     uint8_t scaling) {
  layer_infos_[pipe][layer].w = scaling ? (width - layer_infos_[pipe][layer].x * 2) * 2
                                        : (width - layer_infos_[pipe][layer].x * 2);
  layer_infos_[pipe][layer].h = scaling ? (height - layer_infos_[pipe][layer].y * 2) * 2
                                        : (height - layer_infos_[pipe][layer].y * 2);
  layer_infos_[pipe][layer].scaling = scaling;
}

void fill_buffer(buffer_object_t* bo) {
  struct drm_mode_map_dumb map = {};
  memset(&map, 0, sizeof(map));
  map.handle = bo->handle;
  int ret = drmIoctl(bo->fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
  if (ret)
    fprintf(stderr, "cannot map dumb buffer (%d): %m\n", errno);

  bo->vaddr = (uint8_t*)mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                             bo->fd, map.offset);
  if (bo->vaddr == MAP_FAILED)
    fprintf(stderr, "cannot mmap dumb buffer (%d): %m\n", errno);

  memset(bo->vaddr, 0, bo->size);
  uint8_t A = bo->layer_info->A;
  uint8_t R = bo->layer_info->R;
  uint8_t G = bo->layer_info->G;
  uint8_t B = bo->layer_info->B;
  uint32_t* pintbuffer = (uint32_t*)bo->vaddr;
  uint32_t value = (A << 24) | (R << 16) | (G << 8) | B;
  for (int i = 0; i < bo->width * bo->height; i++) {
    pintbuffer[i] = value;
  }

  munmap(bo->vaddr, bo->size);
}

int modeset_create_fb(buffer_object_t* bo) {
  struct drm_mode_create_dumb create = {};
  int ret;
  memset(&create, 0, sizeof(create));
  create.width = bo->width;
  create.height = bo->height;
  create.bpp = 32;
  create.handle = 0;
  ret = drmIoctl(bo->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
  if (ret < 0) {
    fprintf(stderr, "cannot create dumb buffer (%d): %m\n", errno);
    return -errno;
  }
  bo->stride = create.pitch;
  bo->size = create.size;
  bo->handle = create.handle;

  // ADDFB2
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

  ret = drmIoctl(bo->fd, DRM_IOCTL_MODE_ADDFB2, &f);
  if (ret < 0) {
    fprintf(stderr, "cannot addfb2 buffer (%d): %m\n", errno);
    return -errno;
  }
  bo->fb_id = f.fb_id;
  return ret;
}

void modeset_destory_fb(buffer_object_t* bo) {
  struct drm_mode_destroy_dumb destory = {};
  drmModeRmFB(bo->fd, bo->fb_id);
  destory.handle = bo->handle;
  drmIoctl(bo->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destory);
}