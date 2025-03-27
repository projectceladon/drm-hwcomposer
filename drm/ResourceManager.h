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

#pragma once

#include <cstring>
#include <mutex>

#include "DrmDevice.h"
#include "DrmDisplayPipeline.h"
#include "DrmFbImporter.h"
#include "DrmProperty.h"
#include "UEventListener.h"

namespace android {

enum class CtmHandling {
  kDrmOrGpu,    /* Handled by DRM is possible, otherwise by GPU */
  kDrmOrIgnore, /* Handled by DRM is possible, otherwise displayed as is */
};

class PipelineToFrontendBindingInterface {
 public:
  virtual ~PipelineToFrontendBindingInterface() = default;
  virtual bool BindDisplay(std::shared_ptr<DrmDisplayPipeline>) = 0;
  virtual bool UnbindDisplay(std::shared_ptr<DrmDisplayPipeline>) = 0;
  virtual void FinalizeDisplayBinding() = 0;
  virtual void NotifyDisplayLinkStatus(
      std::shared_ptr<DrmDisplayPipeline> pipeline) = 0;
};

class ResourceManager {
 public:
  explicit ResourceManager(
      PipelineToFrontendBindingInterface *p2f_bind_interface);
  ResourceManager(const ResourceManager &) = delete;
  ResourceManager &operator=(const ResourceManager &) = delete;
  ResourceManager(const ResourceManager &&) = delete;
  ResourceManager &&operator=(const ResourceManager &&) = delete;
  ~ResourceManager();

  void Init();

  void DeInit();

  bool ForcedScalingWithGpu() const {
    return scale_with_gpu_;
  }

  auto &GetCtmHandling() const {
    return ctm_handling_;
  }

  auto &GetMainLock() {
    return main_lock_;
  }

  auto GetVirtualDisplayPipeline() -> std::shared_ptr<DrmDisplayPipeline>;
  auto GetWritebackConnectorsCount() -> uint32_t;

  static auto GetTimeMonotonicNs() -> int64_t;

 private:
  auto GetOrderedConnectors() -> std::vector<DrmConnector *>;
  void UpdateFrontendDisplays();
  void DetachAllFrontendDisplays();
  void ReloadNode();
  std::vector<std::unique_ptr<DrmDevice>> drms_;

  // Android properties:
  bool scale_with_gpu_{};
  CtmHandling ctm_handling_{};

  std::shared_ptr<UEventListener> uevent_listener_;

  std::recursive_mutex main_lock_;

  std::map<DrmConnector *, std::shared_ptr<DrmDisplayPipeline>>
      attached_pipelines_;

  PipelineToFrontendBindingInterface *const frontend_interface_;

  bool initialized_{};
  int card_num_ = 0;
  bool reloaded_{};
};
}  // namespace android
