/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "drm/ResourceManager.h"
#include "hwc2_device/HwcDisplay.h"

namespace android {

class DrmHwc : public PipelineToFrontendBindingInterface {
 public:
  DrmHwc();
  ~DrmHwc() override = default;

  // Client Callback functions.:
  virtual void SendVsyncEventToClient(hwc2_display_t displayid,
                                      int64_t timestamp,
                                      uint32_t vsync_period) const = 0;
  virtual void SendVsyncPeriodTimingChangedEventToClient(
      hwc2_display_t displayid, int64_t timestamp) const = 0;
  virtual void SendRefreshEventToClient(uint64_t displayid) = 0;
  virtual void SendHotplugEventToClient(hwc2_display_t displayid,
                                        bool connected) = 0;

  // Device functions
  HWC2::Error CreateVirtualDisplay(uint32_t width, uint32_t height,
                                   int32_t *format, hwc2_display_t *display);
  HWC2::Error DestroyVirtualDisplay(hwc2_display_t display);
  void Dump(uint32_t *out_size, char *out_buffer);
  uint32_t GetMaxVirtualDisplayCount();

  auto GetDisplay(hwc2_display_t display_handle) {
    return displays_.count(display_handle) != 0
               ? displays_[display_handle].get()
               : nullptr;
  }

  auto &GetResMan() {
    return resource_manager_;
  }

  void ScheduleHotplugEvent(hwc2_display_t displayid, bool connected) {
    deferred_hotplug_events_[displayid] = connected;
  }

  // PipelineToFrontendBindingInterface
  bool BindDisplay(std::shared_ptr<DrmDisplayPipeline> pipeline) override;
  bool UnbindDisplay(std::shared_ptr<DrmDisplayPipeline> pipeline) override;
  void FinalizeDisplayBinding() override;

 protected:
  auto& Displays() { return displays_; }

 private:
  ResourceManager resource_manager_;
  std::map<hwc2_display_t, std::unique_ptr<HwcDisplay>> displays_;
  std::map<std::shared_ptr<DrmDisplayPipeline>, hwc2_display_t>
      display_handles_;

  std::string dump_string_;

  std::map<hwc2_display_t, bool> deferred_hotplug_events_;
  std::vector<hwc2_display_t> displays_for_removal_list_;

  uint32_t last_display_handle_ = kPrimaryDisplay;
};
}  // namespace android