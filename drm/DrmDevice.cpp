/*
 * Copyright (C) 2015 The Android Open Source Project
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

#define LOG_TAG "hwc-drm-device"

#include "DrmDevice.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cinttypes>
#include <cstdint>
#include <string>

#include "drm/DrmAtomicStateManager.h"
#include "drm/DrmPlane.h"
#include "drm/ResourceManager.h"
#include "drm/DrmVirtgpu.h"
#include "utils/log.h"
#include "utils/properties.h"

namespace android {

auto DrmDevice::CreateInstance(std::string const &path,
                               ResourceManager *res_man)
    -> std::unique_ptr<DrmDevice> {
  if (!IsKMSDev(path.c_str())) {
    return {};
  }

  auto device = std::unique_ptr<DrmDevice>(new DrmDevice(res_man));

  if (device->Init(path.c_str()) != 0) {
    return {};
  }

  return device;
}

DrmDevice::DrmDevice(ResourceManager *res_man) : res_man_(res_man) {
  drm_fb_importer_ = std::make_unique<DrmFbImporter>(*this);
}

auto DrmDevice::Init(const char *path) -> int {
  /* TODO: Use drmOpenControl here instead */
  fd_ = UniqueFd(open(path, O_RDWR | O_CLOEXEC));
  if (!fd_) {
    // NOLINTNEXTLINE(concurrency-mt-unsafe): Fixme
    ALOGE("Failed to open dri %s: %s", path, strerror(errno));
    return -ENODEV;
  }

  int ret = drmSetClientCap(GetFd(), DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  if (ret != 0) {
    ALOGE("Failed to set universal plane cap %d", ret);
    return ret;
  }

  ret = drmSetClientCap(GetFd(), DRM_CLIENT_CAP_ATOMIC, 1);
  if (ret != 0) {
    ALOGE("Failed to set atomic cap %d", ret);
    return ret;
  }

#ifdef DRM_CLIENT_CAP_WRITEBACK_CONNECTORS
  ret = drmSetClientCap(GetFd(), DRM_CLIENT_CAP_WRITEBACK_CONNECTORS, 1);
  if (ret != 0) {
    ALOGI("Failed to set writeback cap %d", ret);
  }
#endif

  uint64_t cap_value = 0;
  if (drmGetCap(GetFd(), DRM_CAP_ADDFB2_MODIFIERS, &cap_value) != 0) {
    ALOGW("drmGetCap failed. Fallback to no modifier support.");
    cap_value = 0;
  }
  HasAddFb2ModifiersSupport_ = cap_value != 0;

  drmSetMaster(GetFd());
  if (drmIsMaster(GetFd()) == 0) {
    ALOGE("DRM/KMS master access required");
    return -EACCES;
  }

  auto res = MakeDrmModeResUnique(GetFd());
  if (!res) {
    ALOGE("Failed to get DrmDevice resources");
    return -ENODEV;
  }

  char property[PROPERTY_VALUE_MAX];
  memset(property, 0 , PROPERTY_VALUE_MAX);
  property_get("vendor.hwcomposer.preferred.mode.limit", property, "1");
  preferred_mode_limit_ = atoi(property) != 0 ? true : false;
  ALOGD("The property 'vendor.hwcomposer.preferred.mode.limit' value is %d", preferred_mode_limit_);

  memset(property, 0 , PROPERTY_VALUE_MAX);
  property_get("vendor.hwcomposer.planes.enabling", property, "1");
  planes_enabling_ = atoi(property) != 0 ? true : false;
  ALOGD("The property 'vendor.hwcomposer.planes.enabling' value is %d, %s",
    planes_enabling_, planes_enabling_ ? "support all planes":"only support primary plane");

  min_resolution_ = std::pair<uint32_t, uint32_t>(res->min_width,
                                                  res->min_height);
  max_resolution_ = std::pair<uint32_t, uint32_t>(res->max_width,
                                                  res->max_height);

  color_adjustment_enabling_ = false;
  memset(property, 0 , PROPERTY_VALUE_MAX);
  property_get("vendor.hwcomposer.color.adjustment.enabling", property, "0");
  color_adjustment_enabling_ = atoi(property) != 0 ? true : false;
  ALOGD("COLOR_ The property 'vendor.hwcomposer.color.adjustment.enabling' value is %d", color_adjustment_enabling_);

  for (int i = 0; i < res->count_crtcs; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto crtc = DrmCrtc::CreateInstance(*this, res->crtcs[i], i);
    if (crtc) {
      crtcs_.emplace_back(std::move(crtc));
    }
  }

  for (int i = 0; i < res->count_encoders; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto enc = DrmEncoder::CreateInstance(*this, res->encoders[i], i);
    if (enc) {
      encoders_.emplace_back(std::move(enc));
    }
  }

  for (int i = 0; i < res->count_connectors; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto conn = DrmConnector::CreateInstance(*this, res->connectors[i], i);

    if (!conn) {
      continue;
    }

    if (conn->IsWriteback()) {
      writeback_connectors_.emplace_back(std::move(conn));
    } else {
      connectors_.emplace_back(std::move(conn));
    }
  }

  auto plane_res = MakeDrmModePlaneResUnique(GetFd());
  if (!plane_res) {
    ALOGE("Failed to get plane resources");
    return -ENOENT;
  }
  constexpr char PLANES_MAX_NUM_STRING[] = "99";
  planes_num_ = atoi(PLANES_MAX_NUM_STRING);
  memset(property, 0, PROPERTY_VALUE_MAX);
  property_get("vendor.hwcomposer.planes.num", property, PLANES_MAX_NUM_STRING);
  planes_num_ = atoi(property) <= 0 ? atoi(PLANES_MAX_NUM_STRING)
                                    : atoi(property);
  ALOGD("The property 'vendor.hwcomposer.planes.num' value is %s", property);
  for (uint32_t i = 0; i < plane_res->count_planes; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto plane = DrmPlane::CreateInstance(*this, plane_res->planes[i]);

    if (!planes_enabling_) {
      if (plane->GetType() == DRM_PLANE_TYPE_PRIMARY)
        planes_.emplace_back(std::move(plane));
    } else {
      planes_.emplace_back(std::move(plane));
    }
  }

  IsIvshmDev_ = IsIvshmDev(GetFd());
  return 0;
}

auto DrmDevice::RegisterUserPropertyBlob(void *data, size_t length) const
    -> DrmModeUserPropertyBlobUnique {
  struct drm_mode_create_blob create_blob {};
  create_blob.length = length;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-cstyle-cast)
  create_blob.data = (__u64)data;

  int ret = drmIoctl(GetFd(), DRM_IOCTL_MODE_CREATEPROPBLOB, &create_blob);
  if (ret != 0) {
    ALOGE("Failed to create mode property blob %d", ret);
    return {};
  }

  return DrmModeUserPropertyBlobUnique(
      new uint32_t(create_blob.blob_id), [this](const uint32_t *it) {
        struct drm_mode_destroy_blob destroy_blob {};
        destroy_blob.blob_id = (__u32)*it;
        int err = drmIoctl(GetFd(), DRM_IOCTL_MODE_DESTROYPROPBLOB,
                           &destroy_blob);
        if (err != 0) {
          ALOGE("Failed to destroy mode property blob %" PRIu32 "/%d", *it,
                err);
        }
        // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
        delete it;
      });
}

uint32_t DrmDevice::GetNextModeId() {
  return ++mode_id_;
}

void DrmDevice::ResetModeId() {
  mode_id_ = 0;
}

int DrmDevice::GetProperty(uint32_t obj_id, uint32_t obj_type,
                           const char *prop_name, DrmProperty *property) const {
  drmModeObjectPropertiesPtr props = nullptr;

  props = drmModeObjectGetProperties(GetFd(), obj_id, obj_type);
  if (props == nullptr) {
    ALOGE("Failed to get properties for %d/%x", obj_id, obj_type);
    return -ENODEV;
  }

  bool found = false;
  for (int i = 0; !found && (size_t)i < props->count_props; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    drmModePropertyPtr p = drmModeGetProperty(GetFd(), props->props[i]);
    if (strcmp(p->name, prop_name) == 0) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      property->Init(obj_id, p, props->prop_values[i]);
      found = true;
    }
    drmModeFreeProperty(p);
  }

  drmModeFreeObjectProperties(props);
  return found ? 0 : -ENOENT;
}

auto DrmDevice::IsIvshmDev(int fd) -> bool {
  uint64_t dev_feature = 0;
  for (uint32_t i = 0; i < ARRAY_SIZE(params); i++) {
    struct drm_virtgpu_getparam get_param = {.param = 0, .value = 0};

    get_param.param = params[i].param;
    get_param.value = (uint64_t)(uintptr_t)&params[i].value;
    int ret = drmIoctl(fd, DRM_IOCTL_VIRTGPU_GETPARAM, &get_param);
    if (ret == 0) {
      if ((strcmp(params[i].name, "VIRTGPU_PARAM_QUERY_DEV") == 0) && (params[i].value != 1))
        dev_feature |= VIRTGPU_PARAM_QUERY_DEV_BIT;
      if ((strcmp(params[i].name, "VIRTGPU_PARAM_RESOURCE_BLOB") == 0) && (params[i].value == 1))
        dev_feature |= VIRTGPU_PARAM_RESOURCE_BLOB_BIT;
    }
  }
  return dev_feature & VIRTGPU_PARAM_QUERY_DEV_BIT && dev_feature & VIRTGPU_PARAM_RESOURCE_BLOB_BIT;
}

std::string DrmDevice::GetName() const {
  auto *ver = drmGetVersion(GetFd());
  if (ver == nullptr) {
    ALOGW("Failed to get drm version for fd=%d", GetFd());
    return "generic";
  }

  std::string name(ver->name);
  drmFreeVersion(ver);
  return name;
}

bool DrmDevice::IsHdrSupportedDevice() {
  if (!hdr_device_checked_) {
    hdr_device_checked_ = true;
    auto *ver = drmGetVersion(GetFd());
    if (ver == nullptr) {
      ALOGW("Failed to get drm version for fd=%d", GetFd());
      is_hdr_supported_ = false;;
    } else {
      std::string name(ver->name);
      ALOGD("drm device name is : %s\n", name.c_str());
      if (name == "i915") {
        is_hdr_supported_ = true;
      } else {
        is_hdr_supported_ = false;
      }
    }
    drmFreeVersion(ver);
  }

  return is_hdr_supported_;
}

auto DrmDevice::IsKMSDev(const char *path) -> bool {
  auto fd = UniqueFd(open(path, O_RDWR | O_CLOEXEC));
  if (!fd) {
    return false;
  }

  auto res = MakeDrmModeResUnique(fd.Get());
  if (!res) {
    return false;
  }

  bool is_kms = res->count_crtcs > 0 && res->count_connectors > 0 &&
                res->count_encoders > 0;

  return is_kms;
}

auto DrmDevice::GetConnectors()
    -> const std::vector<std::unique_ptr<DrmConnector>> & {
  return connectors_;
}

auto DrmDevice::GetPlanes() -> const std::vector<std::unique_ptr<DrmPlane>> & {
  return planes_;
}

auto DrmDevice::GetCrtcs() -> const std::vector<std::unique_ptr<DrmCrtc>> & {
  return crtcs_;
}

auto DrmDevice::GetEncoders()
    -> const std::vector<std::unique_ptr<DrmEncoder>> & {
  return encoders_;
}

}  // namespace android
