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

#define LOG_TAG "drmhwc"

#include "ResourceManager.h"

#include <sys/stat.h>
#include <dlfcn.h>

#include <ctime>
#include <sstream>

#include "bufferinfo/BufferInfoGetter.h"
#include "drm/DrmAtomicStateManager.h"
#include "drm/DrmDevice.h"
#include "drm/DrmDisplayPipeline.h"
#include "drm/DrmPlane.h"
#include "hwc2_device/DrmHwcTwo.h"
#include "utils/log.h"
#include "utils/properties.h"
#include "hwc2_device/hwcservice_lib.h"
namespace android {

ResourceManager::ResourceManager(
    PipelineToFrontendBindingInterface *p2f_bind_interface)
    : frontend_interface_(p2f_bind_interface) {
  uevent_listener_ = UEventListener::CreateInstance();
}

ResourceManager::~ResourceManager() {
  uevent_listener_->StopThread();
}

static int FindVirtioGpuCard(ResourceManager *res_man, char* path_pattern,
                              int start, int end) {
  bool find = false;
  int i = 0;
  for (i = start; i <= end; i++) {
    std::ostringstream path;
    path << path_pattern << i;
    auto dev = DrmDevice::CreateInstance(path.str(), res_man, i);
    if (dev != nullptr) {
      if (dev->GetName() == "virtio_gpu") {
        find = true;
        break;
      }
    }
  }
  if (find) {
    return i;
  } else {
    return -1;
  }
}

void ResourceManager::ReloadNode() {
  char path_pattern[PROPERTY_VALUE_MAX];
  int path_len = property_get("vendor.hwc.drm.device", path_pattern,
                              "/dev/dri/card%");
  path_pattern[path_len - 1] = '\0';
  for (int idx = card_num_;; ++idx) {
    std::ostringstream path;
    path << path_pattern << idx;

    struct stat buf {};
    if (stat(path.str().c_str(), &buf) != 0)
      break;

    auto dev = DrmDevice::CreateInstance(path.str(), this, idx);
    if (dev && DrmDevice::IsIvshmDev(*(dev->GetFd()))) {
      ALOGD("create ivshmem node card%d, the fd of dev is %x\n", idx, *(dev->GetFd()));
      drms_.emplace_back(std::move(dev));
      reloaded_  = true;
      break;
    }
  }
}

void ResourceManager::Init() {
  if (initialized_) {
    ALOGE("Already initialized");
    return;
  }
  reloaded_ = false;
  char path_pattern[PROPERTY_VALUE_MAX];
  // Could be a valid path or it can have at the end of it the wildcard %
  // which means that it will try open all devices until an error is met.
  auto path_len = property_get("vendor.hwc.drm.device", path_pattern,
                               "/dev/dri/card%");
  if (path_pattern[path_len - 1] != '%') {
    auto dev = DrmDevice::CreateInstance(path_pattern, this, 0);
    if (dev) {
      drms_.emplace_back(std::move(dev));
    }
  } else {
    int node_num = 0;
    path_pattern[path_len - 1] = '\0';
    for (int idx = 0;; ++idx) {
      std::ostringstream path;
      path << path_pattern << idx;

      struct stat buf {};
      if (stat(path.str().c_str(), &buf) != 0)
        break;

      node_num++;
    }
    card_num_ = node_num;
    // only have card0, is BM/GVT-d/Virtio
    if (node_num == 1) {
      std::ostringstream path;
      path << path_pattern << 0;
      auto dev = DrmDevice::CreateInstance(path.str(), this, 0);
      if (dev) {
        drms_.emplace_back(std::move(dev));
      }
    }
    // is SR-IOV or iGPU + dGPU
    // if not find virtio_gpu, we choose igpu
    if (node_num == 2) {
      int card_id = FindVirtioGpuCard(this, path_pattern, 0, 1);
      if (card_id < 0) {
         card_id = 0;
      }
      std::ostringstream path;
      path << path_pattern << card_id;
      auto dev = DrmDevice::CreateInstance(path.str(), this, 1);
      if (dev) {
        drms_.emplace_back(std::move(dev));
      }
    }
    // is SRI-IOV + dGPU, use virtio-gpu for display
    // if not find virtio_gpu, we choose igpu
    if (node_num == 3) {
      int card_id = FindVirtioGpuCard(this, path_pattern, 0, 2);
      if (card_id < 0) {
         card_id = 0;
      }
      std::ostringstream path;
      path << path_pattern << card_id;
      auto dev = DrmDevice::CreateInstance(path.str(), this, 2);
      if (dev) {
        drms_.emplace_back(std::move(dev));
      }
    }
  }

  scale_with_gpu_ = Properties::ScaleWithGpu();

  char proptext[PROPERTY_VALUE_MAX];
  constexpr char kDrmOrGpu[] = "DRM_OR_GPU";
  constexpr char kDrmOrIgnore[] = "DRM_OR_IGNORE";
  property_get("vendor.hwc.drm.ctm", proptext, kDrmOrGpu);
  if (strncmp(proptext, kDrmOrGpu, sizeof(kDrmOrGpu)) == 0) {
    ctm_handling_ = CtmHandling::kDrmOrGpu;
  } else if (strncmp(proptext, kDrmOrIgnore, sizeof(kDrmOrIgnore)) == 0) {
    ctm_handling_ = CtmHandling::kDrmOrIgnore;
  } else {
    ALOGE("Invalid value for vendor.hwc.drm.ctm: %s", proptext);
    ctm_handling_ = CtmHandling::kDrmOrGpu;
  }

  if (BufferInfoGetter::GetInstance() == nullptr) {
    ALOGE("Failed to initialize BufferInfoGetter");
    return;
  }

  uevent_listener_->RegisterHotplugHandler([this] {
    const std::unique_lock lock(GetMainLock());
    UpdateFrontendDisplays();
  });

  UpdateFrontendDisplays();
  pt_ = std::thread(&ResourceManager::HwcServiceThread, this);
  initialized_ = true;
}

void ResourceManager::HwcServiceThread() {
  typedef void (*StartHwcInfoService)(DrmHwcTwo*);
  void *handle = dlopen("/vendor/lib64/hw/libhwcservicelib.so", RTLD_NOW);
  if (!handle) {
    ALOGE("dlopen /vendor/lib64/hw/libhwcservicelib.so fail");
    return;
  }
  StartHwcInfoService func = (StartHwcInfoService)dlsym(handle, "StartHwcInfoService");
  if (!func) {
    ALOGE("dlsym(StartHwcInfoService) fail ");
    return;
  }
  func((DrmHwcTwo*)frontend_interface_);
}

void ResourceManager::DeInit() {
  if (!initialized_) {
    ALOGE("Not initialized");
    return;
  }

  uevent_listener_->RegisterHotplugHandler({});

  DetachAllFrontendDisplays();
  drms_.clear();

  initialized_ = false;
}

auto ResourceManager::GetTimeMonotonicNs() -> int64_t {
  struct timespec ts {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  constexpr int64_t kNsInSec = 1000000000LL;
  return (int64_t(ts.tv_sec) * kNsInSec) + int64_t(ts.tv_nsec);
}

void ResourceManager::UpdateFrontendDisplays() {
  if (!reloaded_)
    ReloadNode();
  auto ordered_connectors = GetOrderedConnectors();

  for (auto *conn : ordered_connectors) {
    conn->UpdateModes();
    auto connected = conn->IsConnected();
    auto attached = attached_pipelines_.count(conn) != 0;

    if (connected != attached) {
      ALOGI("%s connector %s", connected ? "Attaching" : "Detaching",
            conn->GetName().c_str());

      if (connected) {
        std::shared_ptr<DrmDisplayPipeline>
            pipeline = DrmDisplayPipeline::CreatePipeline(*conn);

        if (pipeline) {
          frontend_interface_->BindDisplay(pipeline);
          attached_pipelines_[conn] = std::move(pipeline);
        }
      } else {
        auto &pipeline = attached_pipelines_[conn];
        pipeline->AtomicDisablePipeline();
        frontend_interface_->UnbindDisplay(pipeline);
        attached_pipelines_.erase(conn);
      }
    }
    if (connected) {
      if (!conn->IsLinkStatusGood())
        frontend_interface_->NotifyDisplayLinkStatus(attached_pipelines_[conn]);
    }
  }
  frontend_interface_->FinalizeDisplayBinding();
}

void ResourceManager::DetachAllFrontendDisplays() {
  for (auto &p : attached_pipelines_) {
    frontend_interface_->UnbindDisplay(p.second);
  }
  attached_pipelines_.clear();
  frontend_interface_->FinalizeDisplayBinding();
}

auto ResourceManager::GetOrderedConnectors() -> std::vector<DrmConnector *> {
  /* Put internal displays first then external to
   * ensure Internal will take Primary slot
   */

  std::vector<DrmConnector *> ordered_connectors;

  for (auto &drm : drms_) {
    for (const auto &conn : drm->GetConnectors()) {
      if (conn->IsInternal()) {
        ordered_connectors.emplace_back(conn.get());
      }
    }
  }

  for (auto &drm : drms_) {
    for (const auto &conn : drm->GetConnectors()) {
      if (conn->IsExternal()) {
        ordered_connectors.emplace_back(conn.get());
      }
    }
  }

  return ordered_connectors;
}

auto ResourceManager::GetVirtualDisplayPipeline()
    -> std::shared_ptr<DrmDisplayPipeline> {
  for (auto &drm : drms_) {
    for (const auto &conn : drm->GetWritebackConnectors()) {
      auto pipeline = DrmDisplayPipeline::CreatePipeline(*conn);
      if (!pipeline) {
        ALOGE("Failed to create pipeline for writeback connector %s",
              conn->GetName().c_str());
      }
      if (pipeline) {
        return pipeline;
      }
    }
  }
  return {};
}

auto ResourceManager::GetWritebackConnectorsCount() -> uint32_t {
  uint32_t count = 0;
  for (auto &drm : drms_) {
    count += drm->GetWritebackConnectors().size();
  }
  return count;
}

}  // namespace android
