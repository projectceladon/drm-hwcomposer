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

#define LOG_TAG "drmhwc"

#include "DrmHwcThree.h"

#include <cinttypes>

#include "Utils.h"
#include "aidl/android/hardware/graphics/common/Dataspace.h"
#if __ANDROID_API__ >= 35
#include "aidl/android/hardware/graphics/common/DisplayHotplugEvent.h"
#endif

namespace aidl::android::hardware::graphics::composer3::impl {

auto DrmHwcThree::GetHwc3Display(::android::HwcDisplay& display)
    -> std::shared_ptr<Hwc3Display> {
  auto frontend_private_data = display.GetFrontendPrivateData();
  if (!frontend_private_data) {
    frontend_private_data = std::make_shared<Hwc3Display>();
    display.SetFrontendPrivateData(frontend_private_data);
  }
  return std::static_pointer_cast<Hwc3Display>(frontend_private_data);
}

DrmHwcThree::~DrmHwcThree() {
  /* Display deinit routine is handled by resource manager */
  GetResMan().DeInit();
}

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
  {
    const std::unique_lock lock(GetResMan().GetMainLock());
    auto* idisplay = GetDisplay(display_id);
    if (idisplay == nullptr) {
      ALOGE("Failed to get display %" PRIu64, display_id);
      return;
    }
    auto hwc3_display = GetHwc3Display(*idisplay);
    hwc3_display->must_validate = true;
  }
  composer_callback_->onRefresh(static_cast<int64_t>(display_id));
}

void DrmHwcThree::SendVsyncEventToClient(uint64_t display_id, int64_t timestamp,
                                         uint32_t vsync_period) const {
  composer_callback_->onVsync(static_cast<int64_t>(display_id), timestamp,
                              static_cast<int32_t>(vsync_period));
}

#if __ANDROID_API__ >= 35

void DrmHwcThree::SendHotplugEventToClient(
    hwc2_display_t display_id, DrmHwc::DisplayStatus display_status) {
  common::DisplayHotplugEvent event = common::DisplayHotplugEvent::DISCONNECTED;
  switch (display_status) {
    case DrmHwc::kDisconnected:
      event = common::DisplayHotplugEvent::DISCONNECTED;
      break;
    case DrmHwc::kConnected:
      event = common::DisplayHotplugEvent::CONNECTED;
      break;
    case DrmHwc::kLinkTrainingFailed:
      event = common::DisplayHotplugEvent::ERROR_INCOMPATIBLE_CABLE;
      break;
  }
  composer_callback_->onHotplugEvent(static_cast<int64_t>(display_id), event);
}

#else

void DrmHwcThree::SendHotplugEventToClient(
    hwc2_display_t display_id, DrmHwc::DisplayStatus display_status) {
  bool connected = display_status != DrmHwc::kDisconnected;
  composer_callback_->onHotplug(static_cast<int64_t>(display_id), connected);
}

#endif

}  // namespace aidl::android::hardware::graphics::composer3::impl
