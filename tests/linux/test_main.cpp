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

#define LOG_TAG "hwc-drm-planes-test-main"

#include <cinttypes>
#include <xf86drm.h>
#include <algorithm>
#include "DrmHwcTwo.h"
#include "backend/Backend.h"
#include "utils/log.h"
#include "igt_assist.h"
#include "HwcLayer.h"
#include "utils/properties.h"
#include <string.h>
namespace android {
class DrmHwcTwoTest : public DrmHwcTwo {
public:
    DrmHwcTwoTest() {GetResMan().Init();}
    ~DrmHwcTwoTest() override = default;
};
struct Drmhwc2DeviceTest : hwc2_device {
  DrmHwcTwoTest drmhwctwotest;
};

}
void waitBlank(int fd) {
  drmVBlank vblank;
  vblank.request.type = DRM_VBLANK_RELATIVE;
  vblank.request.sequence = 1;
  vblank.request.signal = 0;
  drmWaitVBlank(fd, &vblank);
}

int main(int argc, const char** argv) {
  ALOGI("usage: %s [planes num]", argv[0]);
  if (argc >= 3) {
    return 0;
  }
  int planes_num = MAX_PLANE_NUM;
  if (argc == 2)
    planes_num = std::min(atoi(argv[1]), MAX_PLANE_NUM);
  property_set("vendor.hwcomposer.planes.enabling", "1");
  auto ctx = std::make_unique<android::Drmhwc2DeviceTest>();
  if (!ctx) {
    ALOGE("Failed to allocate DrmHwcTwo");
    return -ENOMEM;
  }

  android::DrmHwcTwoTest* hwc2 = &ctx->drmhwctwotest;
  hwc2_display_t display_id = 0;

  while(true) {
    auto hwc_display = hwc2->GetDisplay(display_id);
    if (hwc_display == nullptr)
      break;
    auto planes = hwc_display->GetPipe().GetUsablePlanes();
    int32_t width = 0;
    int32_t height = 0;
    hwc2_config_t config = 0;
    hwc_display->GetActiveConfig(&config);
    hwc_display->GetDisplayAttribute(config, static_cast<int32_t>(HWC2::Attribute::Width), &width);
    hwc_display->GetDisplayAttribute(config, static_cast<int32_t>(HWC2::Attribute::Height), &height);

    hwc2_layer_t layer_id = 0;
    for (uint8_t i = 0; i < planes_num; i++) {
      hwc_display->CreateLayer(&layer_id);
      create_fb(hwc_display->GetPipe().device->GetFd(), display_id, i);
      android::HwcLayer * layer = hwc_display->get_layer(layer_id);
      LayerInfo* layerinfo = get_layer_info(display_id, layer_id);
      layer->SetLayerZOrder(layer_id);
      layer->SetLayerBlendMode((int32_t)HWC2::BlendMode::Premultiplied);
      layer->SetLayerPlaneAlpha(layerinfo->A);
      layer->SetValidatedType(HWC2::Composition::Device);
      layer->AcceptTypeChange();
      hwc_rect_t display_frame = {layerinfo->x, layerinfo->y, layerinfo->w + layerinfo->x, layerinfo->h + layerinfo->y};
      if (i == 0 || i == 1) {//scaling
        display_frame = {layerinfo->x, layerinfo->y, 1920 - layerinfo->x , 1080 - layerinfo->y};
      }
      layer->SetLayerDisplayFrame(display_frame);
      hwc_frect_t source_crop = {0.0, 0.0,
        static_cast<float>(layerinfo->w), static_cast<float>(layerinfo->h)};
      layer->SetLayerSourceCrop(source_crop);
    }
    display_id++;
  }

  time_t t0 = time(NULL);
  int32_t total_frame = 0;
  if (display_id > 0) {
    display_id = 0;
    bool first_commit = true;
    auto hwc_display = hwc2->GetDisplay(display_id);
    waitBlank(hwc_display->GetPipe().device->GetFd());
    while(true) {
      hwc_display = hwc2->GetDisplay(display_id);
      if (hwc_display == nullptr) {
        ++total_frame;
        if (total_frame % 300 == 0) {
          time_t t1 = time(NULL);
          ALOGI("Total %lu secs for 300 frames, fps is %ld", t1 - t0, (300) / (t1 - t0));
          t0 = time(NULL);
        }
        display_id = 0;
        hwc_display = hwc2->GetDisplay(display_id);
        waitBlank(hwc_display->GetPipe().device->GetFd());
        continue;
      }
      int32_t out_fence = 0;
      if (first_commit) {
        waitBlank(hwc_display->GetPipe().device->GetFd());
        first_commit = false;
      }
      if (hwc_display->PresentDisplay(&out_fence) != HWC2::Error::None) {
        ALOGI("Retry PresentDisplay again");
        waitBlank(hwc_display->GetPipe().device->GetFd());
        hwc_display->PresentDisplay(&out_fence);
      }
      close(out_fence);
      display_id++;
    }
  }
  return 0;
}
