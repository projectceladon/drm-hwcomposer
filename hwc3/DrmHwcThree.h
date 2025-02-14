/*
 * Copyright (C) 2024 The Android Open Source Project
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

#include <aidl/android/hardware/graphics/composer3/IComposerCallback.h>

#include "drm/DrmHwc.h"
#include "hwc2_device/HwcDisplay.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class Hwc3Display : public ::android::FrontendDisplayBase {
 public:
  bool must_validate = false;

  int64_t next_layer_id = 1;
};

class DrmHwcThree : public ::android::DrmHwc {
 public:
  explicit DrmHwcThree() = default;
  ~DrmHwcThree() override;

  void Init(std::shared_ptr<IComposerCallback> callback);

  // DrmHwcInterface
  void SendVsyncEventToClient(hwc2_display_t display_id, int64_t timestamp,
                              uint32_t vsync_period) const override;
  void SendVsyncPeriodTimingChangedEventToClient(
      hwc2_display_t display_id, int64_t timestamp) const override;
  void SendRefreshEventToClient(uint64_t display_id) override;
  void SendHotplugEventToClient(hwc2_display_t display_id,
                                DrmHwc::DisplayStatus display_status) override;

  static auto GetHwc3Display(::android::HwcDisplay& display)
      -> std::shared_ptr<Hwc3Display>;

 private:
  std::shared_ptr<IComposerCallback> composer_callback_;
};
}  // namespace aidl::android::hardware::graphics::composer3::impl
