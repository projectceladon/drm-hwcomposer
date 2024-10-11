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

#define LOG_TAG "hwc-resource-manager"

#include "ResourceManager.h"

#include <sys/stat.h>

#include <ctime>
#include <sstream>
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>

#include "bufferinfo/BufferInfoGetter.h"
#include "drm/DrmAtomicStateManager.h"
#include "drm/DrmDevice.h"
#include "drm/DrmDisplayPipeline.h"
#include "drm/DrmPlane.h"
#include "utils/log.h"
#include "utils/properties.h"
#include "hwc2_device/DrmHwcTwo.h"
namespace android {

ResourceManager::ResourceManager(
    PipelineToFrontendBindingInterface *p2f_bind_interface)
    : frontend_interface_(p2f_bind_interface) {
  if (uevent_listener_.Init() != 0) {
    ALOGE("Can't initialize event listener");
  }
}

ResourceManager::~ResourceManager() {
  uevent_listener_.Exit();
}

static int find_virtio_gpu_card(ResourceManager *res_man, char* path_pattern, int start, int end) {
  bool find = false;
  int i = 0;
  for(i = start; i <= end; i++) {
    std::ostringstream path;
    path << path_pattern << i;
    auto dev = DrmDevice::CreateInstance(path.str(), res_man);
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

    auto dev = DrmDevice::CreateInstance(path.str(), this);
    if (dev && DrmDevice::IsIvshmDev(dev->GetFd())) {
      ALOGD("create ivshmem node card%d, the fd of dev is %x\n", idx, dev->GetFd());
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
  int path_len = property_get("vendor.hwc.drm.device", path_pattern,
                              "/dev/dri/card%");
  if (path_pattern[path_len - 1] != '%') {
    auto dev = DrmDevice::CreateInstance(path_pattern, this);
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
      auto dev = DrmDevice::CreateInstance(path.str(), this);
      if (dev) {
        drms_.emplace_back(std::move(dev));
      }
    }
    // is SR-IOV or iGPU + dGPU
    // if not find virtio_gpu, we choose igpu
    if (node_num == 2) {
      int card_id = find_virtio_gpu_card(this, path_pattern, 0, 1);
      if (card_id < 0) {
         card_id = 0;
      }
      std::ostringstream path;
      path << path_pattern << card_id;
      auto dev = DrmDevice::CreateInstance(path.str(), this);
      if (dev) {
        drms_.emplace_back(std::move(dev));
      }
    }
    // is SRI-IOV + dGPU, use virtio-gpu for display
    // if not find virtio_gpu, we choose igpu
    if (node_num == 3) {
      int card_id = find_virtio_gpu_card(this, path_pattern, 0, 2);
      if (card_id < 0) {
         card_id = 0;
      }
      std::ostringstream path;
      path << path_pattern << card_id;
      auto dev = DrmDevice::CreateInstance(path.str(), this);
      if (dev) {
        drms_.emplace_back(std::move(dev));
      }
    }
  }

  char scale_with_gpu[PROPERTY_VALUE_MAX];
  property_get("vendor.hwc.drm.scale_with_gpu", scale_with_gpu, "0");
  scale_with_gpu_ = bool(strncmp(scale_with_gpu, "0", 1));

  if (BufferInfoGetter::GetInstance() == nullptr) {
    ALOGE("Failed to initialize BufferInfoGetter");
    return;
  }

  uevent_listener_.RegisterHotplugHandler([this] {
    const std::lock_guard<std::mutex> lock(GetMainLock());
    UpdateFrontendDisplays();
  });

  UpdateFrontendDisplays();
  pt_ = std::thread(&ResourceManager::HwcServiceThread, this);
  initialized_ = true;
}

void ResourceManager::HwcServiceThread() {
  this->hwcService_.Start((DrmHwcTwo*)frontend_interface_);
  sp<ProcessState> proc(ProcessState::self());
  if (!proc.get())
  {
      ALOGE("Error: Fail to new ProcessState.");
      return ;
  }
  proc->startThreadPool();
  IPCThreadState::self()->joinThreadPool();
}

void ResourceManager::DeInit() {
  if (!initialized_) {
    ALOGE("Not initialized");
    return;
  }

  uevent_listener_.RegisterHotplugHandler([] {});

  DetachAllFrontendDisplays();
  drms_.clear();

  initialized_ = false;
}

auto ResourceManager::GetTimeMonotonicNs() -> int64_t {
  struct timespec ts {};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  constexpr int64_t kNsInSec = 1000000000LL;
  return int64_t(ts.tv_sec) * kNsInSec + int64_t(ts.tv_nsec);
}

#define DRM_MODE_LINK_STATUS_GOOD       0
#define DRM_MODE_LINK_STATUS_BAD        1
void ResourceManager::UpdateFrontendDisplays() {
  if (!reloaded_)
    ReloadNode();
  auto ordered_connectors = GetOrderedConnectors();

  for (auto *conn : ordered_connectors) {
    conn->UpdateModes();
    bool connected = conn->IsConnected();
    bool attached = attached_pipelines_.count(conn) != 0;

    if (connected != attached) {
      ALOGI("%s connector %s", connected ? "Attaching" : "Detaching",
            conn->GetName().c_str());

      if (connected) {
        auto pipeline = DrmDisplayPipeline::CreatePipeline(*conn);
        if (pipeline) {
          frontend_interface_->BindDisplay(pipeline.get());
          attached_pipelines_[conn] = std::move(pipeline);
        }
      } else {
        auto &pipeline = attached_pipelines_[conn];
        pipeline->AtomicDisablePipeline();
        frontend_interface_->UnbindDisplay(pipeline.get());
        attached_pipelines_.erase(conn);
      }
    } else {
      if (connected) {
        uint64_t link_status = 0;
        int ret = 0;
        auto &pipeline = attached_pipelines_[conn];
        conn->UpdateLinkStatusProperty();
        std::tie(ret, link_status) = conn->link_status_property().value();
        if (ret) {
          ALOGE("Connector %u get link status value error %d", conn->GetId(),
                ret);
          continue;
        }
        if (link_status != DRM_MODE_LINK_STATUS_GOOD) {
          ALOGW("Connector %u link status bad", conn->GetId());
          HwcDisplay *display = frontend_interface_->GetDisplay(pipeline.get());
          if (display) {
            display->SetPowerMode(static_cast<int32_t>(HWC2::PowerMode::Off));
            display->ChosePreferredConfig();
            display->SetPowerMode(static_cast<int32_t>(HWC2::PowerMode::On));
            ALOGD("Connector %u link status bad handling done", conn->GetId());
          }
        } else {
          ALOGD("Connector %u link status good. Do nothing", conn->GetId());
        }
      }
    }
  }
  frontend_interface_->FinalizeDisplayBinding();
}

void ResourceManager::DetachAllFrontendDisplays() {
  for (auto &p : attached_pipelines_) {
    frontend_interface_->UnbindDisplay(p.second.get());
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
}  // namespace android
