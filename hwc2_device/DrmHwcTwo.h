/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <hardware/hwcomposer2.h>

#include "drm/DrmHwc.h"

namespace android {

class DrmHwcTwo : public DrmHwc {
 public:
  DrmHwcTwo() = default;
  ~DrmHwcTwo() override = default;

  HWC2::Error RegisterCallback(int32_t descriptor, hwc2_callback_data_t data,
                               hwc2_function_pointer_t function);

  // DrmHwc
  void SendVsyncEventToClient(hwc2_display_t displayid, int64_t timestamp,
                              uint32_t vsync_period) const override;
  void SendVsyncPeriodTimingChangedEventToClient(
      hwc2_display_t displayid, int64_t timestamp) const override;
  void SendRefreshEventToClient(uint64_t displayid) override;
  void SendHotplugEventToClient(hwc2_display_t displayid,
                                DisplayStatus display_status) override;

 private:
  std::pair<HWC2_PFN_HOTPLUG, hwc2_callback_data_t> hotplug_callback_{};
  std::pair<HWC2_PFN_VSYNC, hwc2_callback_data_t> vsync_callback_{};
#if __ANDROID_API__ > 29
  std::pair<HWC2_PFN_VSYNC_2_4, hwc2_callback_data_t> vsync_2_4_callback_{};
  std::pair<HWC2_PFN_VSYNC_PERIOD_TIMING_CHANGED, hwc2_callback_data_t>
      period_timing_changed_callback_{};
#endif
  std::pair<HWC2_PFN_REFRESH, hwc2_callback_data_t> refresh_callback_{};
};
}  // namespace android
