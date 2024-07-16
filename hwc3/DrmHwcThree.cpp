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

#include "DrmHwcThree.h"

#include <cinttypes>

namespace aidl::android::hardware::graphics::composer3::impl {

void DrmHwcThree::Init(std::shared_ptr<IComposerCallback> callback) {
  composer_callback_ = std::move(callback);
  GetResMan().Init();
}

void DrmHwcThree::SendVsyncPeriodTimingChangedEventToClient(
    uint64_t display_id, int64_t timestamp) const {
  VsyncPeriodChangeTimeline timeline;
  timeline.newVsyncAppliedTimeNanos = timestamp;
  timeline.refreshRequired = false;
  timeline.refreshTimeNanos = 0;

  composer_callback_->onVsyncPeriodTimingChanged(static_cast<int64_t>(
                                                     display_id),
                                                 timeline);
}

void DrmHwcThree::SendRefreshEventToClient(uint64_t display_id) {
  composer_callback_->onRefresh(static_cast<int64_t>(display_id));
}

void DrmHwcThree::SendVsyncEventToClient(uint64_t display_id, int64_t timestamp,
                                         uint32_t vsync_period) const {
  composer_callback_->onVsync(static_cast<int64_t>(display_id), timestamp,
                              static_cast<int32_t>(vsync_period));
}

void DrmHwcThree::SendHotplugEventToClient(hwc2_display_t display_id,
                                           bool connected) {
  composer_callback_->onHotplug(static_cast<int64_t>(display_id), connected);
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
