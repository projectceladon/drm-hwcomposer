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

#define LOG_TAG "hwc-bufferinfo-minigbm"

#include "BufferInfoMinigbm.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cerrno>
#include <cstring>

#include "utils/log.h"
namespace android {

LEGACY_BUFFER_INFO_GETTER(BufferInfoMinigbm);

constexpr int CROS_GRALLOC_DRM_GET_FORMAT = 1;
constexpr int CROS_GRALLOC_DRM_GET_DIMENSIONS = 2;
constexpr int CROS_GRALLOC_DRM_GET_BUFFER_INFO = 4;
constexpr int CROS_GRALLOC_DRM_GET_USAGE = 5;

struct cros_gralloc0_buffer_info {
  uint32_t drm_fourcc;
  int num_fds;
  int fds[4];
  uint64_t modifier;
  int offset[4];
  int stride[4];
};

auto BufferInfoMinigbm::GetBoInfo(buffer_handle_t handle)
    -> std::optional<BufferInfo> {
  if (handle == nullptr) {
    return {};
  }

  BufferInfo bi{};

  uint32_t width{};
  uint32_t height{};
  if (gralloc_->perform(gralloc_, CROS_GRALLOC_DRM_GET_DIMENSIONS, handle,
                        &width, &height) != 0) {
    ALOGE(
        "CROS_GRALLOC_DRM_GET_DIMENSIONS operation has failed. "
        "Please ensure you are using the latest minigbm.");
    return {};
  }

  int32_t droid_format{};
  if (gralloc_->perform(gralloc_, CROS_GRALLOC_DRM_GET_FORMAT, handle,
                        &droid_format) != 0) {
    ALOGE(
        "CROS_GRALLOC_DRM_GET_FORMAT operation has failed. "
        "Please ensure you are using the latest minigbm.");
    return {};
  }

  uint32_t usage{};
  if (gralloc_->perform(gralloc_, CROS_GRALLOC_DRM_GET_USAGE, handle, &usage) !=
      0) {
    ALOGE(
        "CROS_GRALLOC_DRM_GET_USAGE operation has failed. "
        "Please ensure you are using the latest minigbm.");
    return {};
  }

  struct cros_gralloc0_buffer_info info {};
  if (gralloc_->perform(gralloc_, CROS_GRALLOC_DRM_GET_BUFFER_INFO, handle,
                        &info) != 0) {
    ALOGE(
        "CROS_GRALLOC_DRM_GET_BUFFER_INFO operation has failed. "
        "Please ensure you are using the latest minigbm.");
    return {};
  }

  bi.width = width;
  bi.height = height;

  bi.format = info.drm_fourcc;

  for (int i = 0; i < info.num_fds; i++) {
    bi.modifiers[i] = info.modifier;
    bi.prime_fds[i] = info.fds[i];
    bi.pitches[i] = info.stride[i];
    bi.offsets[i] = info.offset[i];
  }

  return bi;
}

constexpr char cros_gralloc_module_name[] = "CrOS Gralloc";

int BufferInfoMinigbm::ValidateGralloc() {
  if (strcmp(gralloc_->common.name, cros_gralloc_module_name) != 0) {
    ALOGE("Gralloc name isn't valid: Expected: \"%s\", Actual: \"%s\"",
          cros_gralloc_module_name, gralloc_->common.name);
    return -EINVAL;
  }

  if (gralloc_->perform == nullptr) {
    ALOGE(
        "CrOS gralloc has no perform call implemented. Please upgrade your "
        "minigbm.");
    return -EINVAL;
  }

  return 0;
}

void BufferInfoMinigbm::InitializeGralloc1(DrmDevice *drmDevice) {
  hw_device_t *device;

  struct dri2_drm_display *dri_drm = (struct dri2_drm_display *)calloc(1, sizeof(*dri_drm));
  if (!dri_drm)
    return;

  dri_drm->fd = -1;
  int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                      (const hw_module_t **)&dri_drm->gralloc);
  if (ret) {
    return;
  }

  dri_drm->gralloc_version = dri_drm->gralloc->common.module_api_version;
  if (dri_drm->gralloc_version == HARDWARE_MODULE_API_VERSION(1, 0)) {
    ret = dri_drm->gralloc->common.methods->open(&dri_drm->gralloc->common, GRALLOC_HARDWARE_MODULE_ID, &device);
    if (ret) {
      ALOGE("Failed to open device");
      return;
    } else {
      ALOGE("success to open device, Initialize");
      dri_drm->gralloc1_dvc = (gralloc1_device_t *)device;
      dri_drm->pfn_lock = (GRALLOC1_PFN_LOCK)dri_drm->gralloc1_dvc->getFunction(dri_drm->gralloc1_dvc, GRALLOC1_FUNCTION_LOCK);
      dri_drm->pfn_importBuffer = (GRALLOC1_PFN_IMPORT_BUFFER)dri_drm->gralloc1_dvc->getFunction(dri_drm->gralloc1_dvc, GRALLOC1_FUNCTION_IMPORT_BUFFER);
      dri_drm->pfn_release = (GRALLOC1_PFN_RELEASE)dri_drm->gralloc1_dvc->getFunction(dri_drm->gralloc1_dvc, GRALLOC1_FUNCTION_RELEASE);
      dri_drm->pfn_unlock = (GRALLOC1_PFN_UNLOCK)dri_drm->gralloc1_dvc->getFunction(dri_drm->gralloc1_dvc, GRALLOC1_FUNCTION_UNLOCK);
      dri_drm->pfn_get_stride = (GRALLOC1_PFN_GET_STRIDE)dri_drm->gralloc1_dvc->getFunction(dri_drm->gralloc1_dvc, GRALLOC1_FUNCTION_GET_STRIDE);
      drmDevice->dri_drm_ = (void *)dri_drm;
      }
    }
  return;
}

void BufferInfoMinigbm::DumpBuffer(DrmDevice *drmDevice, buffer_handle_t handle, BufferInfo buffer_info) {
  if (NULL == handle)
    return;
  char dump_file[256] = {0};
  buffer_handle_t handle_copy;
  uint8_t* pixels = nullptr;
  gralloc1_rect_t accessRegion = {0, 0, (int32_t)buffer_info.width, (int32_t)buffer_info.height};;

  struct dri2_drm_display *dri_drm = (struct dri2_drm_display *)drmDevice->dri_drm_;

  assert (dri_drm == nullptr ||
          dri_drm->pfn_importBuffer  == nullptr ||
          dri_drm->pfn_lock  == nullptr ||
          dri_drm->pfn_unlock  == nullptr ||
          dri_drm->pfn_release  == nullptr ||
          dri_drm->pfn_get_stride);

  int ret = dri_drm->pfn_importBuffer(dri_drm->gralloc1_dvc, handle, &handle_copy);
  if (ret) {
    ALOGE("Gralloc importBuffer failed");
    return;
  }

  ret = dri_drm->pfn_lock(dri_drm->gralloc1_dvc, handle_copy,
                          GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN, GRALLOC1_PRODUCER_USAGE_CPU_WRITE_NEVER,
                          &accessRegion, reinterpret_cast<void**>(&pixels), 0);
  if (ret) {
    ALOGE("gralloc->lock failed: %d", ret);
    return;
  } else {
    char ctime[32];
    time_t t = time(0);
    static int i = 0;
    if (i >= 1000) {
      i = 0;
    }
    strftime(ctime, sizeof(ctime), "%Y-%m-%d", localtime(&t));
    sprintf(dump_file, "/data/local/traces/dump_%dx%d_0x%x_%s_%d", buffer_info.width, buffer_info.height, buffer_info.format, ctime,i);
    int file_fd = 0;
    file_fd = open(dump_file, O_RDWR|O_CREAT, 0666);
    if (file_fd == -1) {
      ALOGE("Failed to open %s while dumping", dump_file);
    } else {
      uint32_t bytes = 64;
      size_t size = buffer_info.width * buffer_info.height * bytes;
      ALOGE("write file buffer_info.size = %zu", size);
      write(file_fd, pixels, size);
      close(file_fd);
    }
    int outReleaseFence = 0;
    dri_drm->pfn_unlock(dri_drm->gralloc1_dvc, handle_copy, &outReleaseFence);
    dri_drm->pfn_release(dri_drm->gralloc1_dvc, handle_copy);
  }
}

}  // namespace android
