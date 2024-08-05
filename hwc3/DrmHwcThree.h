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
#include "hwc3/ComposerResources.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class DrmHwcThree : public ::android::DrmHwc {
 public:
  explicit DrmHwcThree(ComposerResources* composer_resources)
      : composer_resources_(composer_resources) {
  }
  ~DrmHwcThree() override;

  void Init(std::shared_ptr<IComposerCallback> callback);

  // DrmHwcInterface
  void SendVsyncEventToClient(hwc2_display_t display_id, int64_t timestamp,
                              uint32_t vsync_period) const override;
  void SendVsyncPeriodTimingChangedEventToClient(
      hwc2_display_t display_id, int64_t timestamp) const override;
  void SendRefreshEventToClient(uint64_t display_id) override;
  void SendHotplugEventToClient(hwc2_display_t display_id,
                                bool connected) override;

 private:
  void CleanDisplayResources(uint64_t display_id);
  void HandleDisplayHotplugEvent(uint64_t display_id, bool connected);

  std::shared_ptr<IComposerCallback> composer_callback_;
  ComposerResources* composer_resources_;
};
}  // namespace aidl::android::hardware::graphics::composer3::impl
