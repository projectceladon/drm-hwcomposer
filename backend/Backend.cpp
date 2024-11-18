/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include "Backend.h"

#include <climits>

#include <aidl/android/hardware/graphics/composer3/Composition.h>
#include "BackendManager.h"
#include "bufferinfo/BufferInfoGetter.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "hwc-backend"

namespace android {

HWC2::Error Backend::ValidateDisplay(HwcDisplay *display, uint32_t *num_types,
                                     uint32_t *num_requests) {
  *num_types = 0;
  *num_requests = 0;

  auto layers = display->GetOrderLayersByZPos();

  for (auto l : layers) {
    if ((uint32_t)l->GetSfType() == (uint32_t)aidl::android::hardware::graphics::composer3::Composition::DISPLAY_DECORATION)
      return HWC2::Error::Unsupported;
  }
  int client_start = -1;
  size_t client_size = 0;

  if (display->ProcessClientFlatteningState(layers.size() <= 1)) {
    display->total_stats().frames_flattened_++;
    client_start = 0;
    client_size = layers.size();
    MarkValidated(layers, client_start, client_size);
  } else {
    std::tie(client_start, client_size) = GetClientLayers(display, layers);

    MarkValidated(layers, client_start, client_size);
  }

  *num_types = client_size;

  display->total_stats().gpu_pixops_ += CalcPixOps(layers, client_start,
                                                   client_size);
  display->total_stats().total_pixops_ += CalcPixOps(layers, 0, layers.size());

  return *num_types != 0 ? HWC2::Error::HasChanges : HWC2::Error::None;
}

std::tuple<int, size_t> Backend::GetClientLayers(
    HwcDisplay *display, std::vector<HwcLayer *> &layers) {
  int client_start = -1;
  size_t client_size = 0;

  for (size_t z_order = 0; z_order < layers.size(); ++z_order) {
    if (IsClientLayer(display, layers[z_order])) {
      if (client_start < 0)
        client_start = (int)z_order;
      client_size = (z_order - client_start) + 1;
    }
  }

  return std::make_tuple(client_start, client_size);
}

bool Backend::IsClientLayer(HwcDisplay *display, HwcLayer *layer) {
  return !HardwareSupportsLayerType(layer->GetSfType()) ||
         !layer->IsLayerUsableAsDevice() ||
         display->color_transform_hint() != HAL_COLOR_TRANSFORM_IDENTITY ||
         (layer->GetLayerData().pi.RequireScalingOrPhasing() &&
          display->GetHwc2()->GetResMan().ForcedScalingWithGpu());
}

bool Backend::IsVideoLayer(HwcLayer *layer) {
  std::optional<BufferInfo> bi;
  if (layer->GetBufferHandle())
    bi = BufferInfoGetter::GetInstance()->GetBoInfo(layer->GetBufferHandle());
  return bi && bi->usage & GRALLOC_USAGE_HW_VIDEO_ENCODER;
}

bool Backend::HardwareSupportsLayerType(HWC2::Composition comp_type) {
  return comp_type == HWC2::Composition::Device ||
         comp_type == HWC2::Composition::Cursor;
}

uint32_t Backend::CalcPixOps(const std::vector<HwcLayer *> &layers,
                             size_t first_z, size_t size) {
  uint32_t pixops = 0;
  for (size_t z_order = 0; z_order < layers.size(); ++z_order) {
    if (z_order >= first_z && z_order < first_z + size) {
      hwc_rect_t &df = layers[z_order]->GetLayerData().pi.display_frame;
      pixops += (df.right - df.left) * (df.bottom - df.top);
    }
  }
  return pixops;
}

void Backend::MarkValidated(std::vector<HwcLayer *> &layers,
                            size_t client_first_z, size_t client_size) {
  for (size_t z_order = 0; z_order < layers.size(); ++z_order) {
    if (z_order >= client_first_z && z_order < client_first_z + client_size) {
      layers[z_order]->SetValidatedType(HWC2::Composition::Client);
      layers[z_order]->SetUseVPPCompose(false);
    } else {
      layers[z_order]->SetValidatedType(HWC2::Composition::Device);
      layers[z_order]->SetUseVPPCompose(true);
    }
  }
}

std::tuple<int, int> Backend::GetExtraClientRange(
    HwcDisplay *display, const std::vector<HwcLayer *> &layers,
    int client_start, size_t client_size) {
  auto planes = display->GetPipe().GetUsablePlanes();
  size_t avail_planes = planes.size();

  /*
   * If more layers then planes, save one plane
   * for client composited layers
   */
  if (avail_planes < display->layers().size())
    avail_planes--;

  int extra_client = int(layers.size() - client_size) - int(avail_planes);

  if (extra_client > 0) {
    int start = 0;
    size_t steps = 0;
    if (client_size != 0) {
      int prepend = std::min(client_start, extra_client);
      int append = std::min(int(layers.size()) -
                                int(client_start + client_size),
                            extra_client);
      start = client_start - (int)prepend;
      client_size += extra_client;
      steps = 1 + std::min(std::min(append, prepend),
                           int(layers.size()) - int(start + client_size));
    } else {
      client_size = extra_client;
      steps = 1 + layers.size() - extra_client;
    }

    uint32_t gpu_pixops = UINT32_MAX;
    for (size_t i = 0; i < steps; i++) {
      uint32_t po = CalcPixOps(layers, start + i, client_size);
      if (po < gpu_pixops) {
        gpu_pixops = po;
        client_start = start + int(i);
      }
    }
  }

  return std::make_tuple(client_start, client_size);
}

std::tuple<int, int> Backend::GetExtraClientRange2(
    HwcDisplay *display, const std::vector<HwcLayer *> &layers,
    int client_start, size_t client_size, int device_start, size_t device_size) {
  auto planes = display->GetPipe().GetUsablePlanes();
  size_t avail_planes = planes.size();

  /*
   * Only video layer must to be compositing by device.
   * parameter device_size indicate video layers number.
   * Since video layer has 2 planes, and so need 2 hardware
   * planes to composite. hwc will not assgin 2 planes
   * for video layer directly, instead avail_planes(indicate
   * available planes) decrease 1.
   */
  avail_planes -= device_size;
  /*
   * If more layers then planes, save one plane
   * for client composited layers
   */
  if (avail_planes < display->layers().size())
    avail_planes--;

  if (avail_planes < device_size) {
    ALOGE("too many device video layers(%zd), no enough planes(%zd) to use", device_size, avail_planes);
    return GetExtraClientRange(display, layers, client_start, client_size);
  } else if (avail_planes == device_size) {
    if (device_start != 0 && (device_start +  device_size) != layers.size()) {
      ALOGE("status is abnormal");
      return GetExtraClientRange(display, layers, client_start, client_size);
    }

    if (device_start == 0)
      return std::make_tuple(device_start + device_size, layers.size() - device_size);
    else
      return std::make_tuple(0, layers.size() - device_size);
  } else {
    if (client_start == -1) {
      int extra_device = std::min(layers.size() - device_size - client_size, avail_planes - device_size);
      int prepend = device_start;
      int append = layers.size() - (device_start + device_size);
      if (std::min(prepend, append) > extra_device) {
        ALOGE("status is abnormal");
        return GetExtraClientRange(display, layers, client_start, client_size);
      }

      if (prepend == std::min(prepend, append)) {
        int remain = extra_device - prepend;
        return std::make_tuple(device_start + device_size + remain, layers.size() - extra_device - device_size);
      }
      if (append == std::min(prepend, append)) {
        return std::make_tuple(0, layers.size() - extra_device - device_size);
      }
    } else {
      int extra_device = std::min(layers.size() - device_size - client_size, avail_planes - device_size);
      if (client_start > device_start) {
        int prepend = device_start;
        int midpend = client_start - (device_start + device_size);
        int append = layers.size() - (client_start + client_size);
        if (prepend > extra_device) {
          ALOGE("status is abnormal");
          return GetExtraClientRange(display, layers, client_start, client_size);
        }
        int remain = extra_device - prepend;
        if (remain == 0) {
          return std::make_tuple(device_start + device_size, layers.size() - extra_device - device_size);
        } else {
          midpend = std::min(midpend, remain);
          if (midpend == remain) {
            return std::make_tuple(device_start + device_size + midpend,
                                    layers.size() - prepend - midpend - device_size);
          } else {
            return std::make_tuple(device_start + device_size + midpend,
                                  layers.size() - prepend - midpend - std::min(remain - midpend, append) - device_size);
          }
        }
      } else {
        int prepend = client_start;
        int midpend = device_start - (client_start + client_size);
        int append = layers.size() - (device_start + device_size);
        if (append > extra_device) {
          ALOGE("status is abnormal");
          return GetExtraClientRange(display, layers, client_start, client_size);
        }
        int remain = extra_device - append;
        if (remain == 0)
          return std::make_tuple(0, layers.size() - extra_device - device_size);
        else {
          midpend = std::min(midpend, remain);
          if (midpend == remain) {
            return std::make_tuple(0, layers.size() - append - midpend - device_size);
          } else {
            return std::make_tuple(std::min(remain - midpend, prepend),
                    layers.size() - append - midpend - std::min(remain - midpend, prepend) - device_size);
          }
        }
      }
    }
  }
  return GetExtraClientRange(display, layers, client_start, client_size);
}
// clang-format off
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables, cert-err58-cpp)
REGISTER_BACKEND("generic", Backend);
// clang-format on

}  // namespace android
