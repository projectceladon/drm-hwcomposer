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

#define LOG_TAG "drmhwc"

#include "DrmHwcTwo.h"

#include <cinttypes>

#include "backend/Backend.h"
#include "utils/log.h"

namespace android {

HWC2::Error DrmHwcTwo::RegisterCallback(int32_t descriptor,
                                        hwc2_callback_data_t data,
                                        hwc2_function_pointer_t function) {
  switch (static_cast<HWC2::Callback>(descriptor)) {
    case HWC2::Callback::Hotplug: {
      hotplug_callback_ = std::make_pair(HWC2_PFN_HOTPLUG(function), data);
      if (function != nullptr) {
        GetResMan().Init();
      } else {
        GetResMan().DeInit();
        /* Headless display may still be here. Remove it! */
        if (Displays().count(kPrimaryDisplay) != 0) {
          Displays()[kPrimaryDisplay]->Deinit();
          Displays().erase(kPrimaryDisplay);
        }
      }
      break;
    }
    case HWC2::Callback::Refresh: {
      refresh_callback_ = std::make_pair(HWC2_PFN_REFRESH(function), data);
      break;
    }
    case HWC2::Callback::Vsync: {
      vsync_callback_ = std::make_pair(HWC2_PFN_VSYNC(function), data);
      break;
    }
#if __ANDROID_API__ > 29
    case HWC2::Callback::Vsync_2_4: {
      vsync_2_4_callback_ = std::make_pair(HWC2_PFN_VSYNC_2_4(function), data);
      break;
    }
    case HWC2::Callback::VsyncPeriodTimingChanged: {
      period_timing_changed_callback_ = std::
          make_pair(HWC2_PFN_VSYNC_PERIOD_TIMING_CHANGED(function), data);
      break;
    }
#endif
    default:
      break;
  }
  return HWC2::Error::None;
}

void DrmHwcTwo::SendHotplugEventToClient(hwc2_display_t displayid,
                                         DisplayStatus display_status) {
  auto hc = hotplug_callback_;

  if (hc.first != nullptr && hc.second != nullptr) {
    /* For some reason HWC Service will call HWC2 API in hotplug callback
     * handler. This is the reason we're using recursive mutex.
     */
    hc.first(hc.second, displayid,
             display_status ? HWC2_CONNECTION_CONNECTED
                            : HWC2_CONNECTION_DISCONNECTED);
  }
}

void DrmHwcTwo::SendVsyncEventToClient(
    hwc2_display_t displayid, int64_t timestamp,
    [[maybe_unused]] uint32_t vsync_period) const {
  /* vsync callback */
#if __ANDROID_API__ > 29
  if (vsync_2_4_callback_.first != nullptr &&
      vsync_2_4_callback_.second != nullptr) {
    vsync_2_4_callback_.first(vsync_2_4_callback_.second, displayid, timestamp,
                              vsync_period);
  } else
#endif
      if (vsync_callback_.first != nullptr &&
          vsync_callback_.second != nullptr) {
    vsync_callback_.first(vsync_callback_.second, displayid, timestamp);
  }
}

void DrmHwcTwo::SendVsyncPeriodTimingChangedEventToClient(
    [[maybe_unused]] hwc2_display_t displayid,
    [[maybe_unused]] int64_t timestamp) const {
#if __ANDROID_API__ > 29
  hwc_vsync_period_change_timeline_t timeline = {
      .newVsyncAppliedTimeNanos = timestamp,
      .refreshRequired = false,
      .refreshTimeNanos = 0,
  };
  if (period_timing_changed_callback_.first != nullptr &&
      period_timing_changed_callback_.second != nullptr) {
    period_timing_changed_callback_
        .first(period_timing_changed_callback_.second, displayid, &timeline);
  }
#endif
}

void DrmHwcTwo::SendRefreshEventToClient(hwc2_display_t displayid) {
  if (refresh_callback_.first != nullptr &&
      refresh_callback_.second != nullptr) {
    refresh_callback_.first(refresh_callback_.second, displayid);
  }
}

void DrmHwcTwo::EnableHDCPSessionForDisplay(uint32_t connector,
                                          EHwcsContentType content_type) {
   HWCContentType type = kCONTENT_TYPE0;
 
   if (content_type == HWCS_CP_CONTENT_TYPE1) {
     type = kCONTENT_TYPE1;
   }
 
   size_t size = displays_.size();
   for (size_t i = 0; i < size; i++) {
     if (displays_[i]-> GetPipe().connector->Get()->GetId()== connector) {
       displays_[i]->GetPipe().atomic_state_manager->SetHDCPState(HWCContentProtection::kDesired, type);
     }
   }
 }
 
 void DrmHwcTwo::EnableHDCPSessionForAllDisplays(EHwcsContentType content_type) {
   HWCContentType type = kCONTENT_TYPE0;
 
   if (content_type == HWCS_CP_CONTENT_TYPE1) {
     type = kCONTENT_TYPE1;
   }
 
   size_t size = displays_.size();
   for (size_t i = 0; i < size; i++) {
     displays_[i]->GetPipe().atomic_state_manager->SetHDCPState(HWCContentProtection::kDesired, type);
   }
 }
 
 void DrmHwcTwo::DisableHDCPSessionForDisplay(uint32_t connector) {
   size_t size = displays_.size();
   for (size_t i = 0; i < size; i++) {
     if (displays_[i]->GetPipe().connector->Get()->GetId() == connector) {
       displays_[i]->GetPipe().atomic_state_manager->SetHDCPState(HWCContentProtection::kUnDesired,
                                     HWCContentType::kInvalid);
     }
   }
 }
 
 void DrmHwcTwo::DisableHDCPSessionForAllDisplays() {
   size_t size = displays_.size();
   for (size_t i = 0; i < size; i++) {
     displays_[i]->GetPipe().atomic_state_manager->SetHDCPState(HWCContentProtection::kUnDesired,
                                   HWCContentType::kInvalid);
   }
 }
}  // namespace android
