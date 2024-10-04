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

#include "DrmHwc.h"

#include <cinttypes>

#include "backend/Backend.h"
#include "utils/log.h"

namespace android {

DrmHwc::DrmHwc() : resource_manager_(this) {};

/* Must be called after every display attach/detach cycle */
void DrmHwc::FinalizeDisplayBinding() {
  if (displays_.count(kPrimaryDisplay) == 0) {
    /* Primary display MUST always exist */
    ALOGI("No pipelines available. Creating null-display for headless mode");
    displays_[kPrimaryDisplay] = std::make_unique<
        HwcDisplay>(kPrimaryDisplay, HWC2::DisplayType::Physical, this);
    /* Initializes null-display */
    displays_[kPrimaryDisplay]->SetPipeline({});
  }

  if (displays_[kPrimaryDisplay]->IsInHeadlessMode() &&
      !display_handles_.empty()) {
    /* Reattach first secondary display to take place of the primary */
    auto pipe = display_handles_.begin()->first;
    ALOGI("Primary display was disconnected, reattaching '%s' as new primary",
          pipe->connector->Get()->GetName().c_str());
    UnbindDisplay(pipe);
    BindDisplay(pipe);
  }

  // Finally, send hotplug events to the client
  for (auto &dhe : deferred_hotplug_events_) {
    SendHotplugEventToClient(dhe.first, dhe.second);
  }
  deferred_hotplug_events_.clear();

  /* Wait 0.2s before removing the displays to flush pending HWC2 transactions
   */
  auto &mutex = GetResMan().GetMainLock();
  mutex.unlock();
  const int time_for_sf_to_dispose_display_us = 200000;
  usleep(time_for_sf_to_dispose_display_us);
  mutex.lock();
  for (auto handle : displays_for_removal_list_) {
    displays_.erase(handle);
  }
}

bool DrmHwc::BindDisplay(std::shared_ptr<DrmDisplayPipeline> pipeline) {
  if (display_handles_.count(pipeline) != 0) {
    ALOGE("%s, pipeline is already used by another display, FIXME!!!: %p",
          __func__, pipeline.get());
    return false;
  }

  uint32_t disp_handle = kPrimaryDisplay;

  if (displays_.count(kPrimaryDisplay) != 0 &&
      !displays_[kPrimaryDisplay]->IsInHeadlessMode()) {
    disp_handle = ++last_display_handle_;
  }

  if (displays_.count(disp_handle) == 0) {
    auto disp = std::make_unique<HwcDisplay>(disp_handle,
                                             HWC2::DisplayType::Physical, this);
    displays_[disp_handle] = std::move(disp);
  }

  ALOGI("Attaching pipeline '%s' to the display #%d%s",
        pipeline->connector->Get()->GetName().c_str(), (int)disp_handle,
        disp_handle == kPrimaryDisplay ? " (Primary)" : "");

  displays_[disp_handle]->SetPipeline(pipeline);
  display_handles_[pipeline] = disp_handle;

  return true;
}

bool DrmHwc::UnbindDisplay(std::shared_ptr<DrmDisplayPipeline> pipeline) {
  if (display_handles_.count(pipeline) == 0) {
    ALOGE("%s, can't find the display, pipeline: %p", __func__, pipeline.get());
    return false;
  }
  auto handle = display_handles_[pipeline];
  display_handles_.erase(pipeline);

  ALOGI("Detaching the pipeline '%s' from the display #%i%s",
        pipeline->connector->Get()->GetName().c_str(), (int)handle,
        handle == kPrimaryDisplay ? " (Primary)" : "");

  if (displays_.count(handle) == 0) {
    ALOGE("%s, can't find the display, handle: %" PRIu64, __func__, handle);
    return false;
  }
  displays_[handle]->SetPipeline({});

  /* We must defer display disposal and removal, since it may still have pending
   * HWC_API calls scheduled and waiting until ueventlistener thread releases
   * main lock, otherwise transaction may fail and SF may crash
   */
  if (handle != kPrimaryDisplay) {
    displays_for_removal_list_.emplace_back(handle);
  }
  return true;
}

void DrmHwc::NotifyDisplayLinkStatus(
    std::shared_ptr<DrmDisplayPipeline> pipeline) {
  if (display_handles_.count(pipeline) == 0) {
    ALOGE("%s, can't find the display, pipeline: %p", __func__, pipeline.get());
    return;
  }
  ScheduleHotplugEvent(display_handles_[pipeline],
                       DisplayStatus::kLinkTrainingFailed);
}

HWC2::Error DrmHwc::CreateVirtualDisplay(
    uint32_t width, uint32_t height,
    int32_t *format,  // NOLINT(readability-non-const-parameter)
    hwc2_display_t *display) {
  ALOGI("Creating virtual display %dx%d format %d", width, height, *format);

  auto virtual_pipeline = resource_manager_.GetVirtualDisplayPipeline();
  if (!virtual_pipeline)
    return HWC2::Error::Unsupported;

  *display = ++last_display_handle_;
  auto disp = std::make_unique<HwcDisplay>(*display, HWC2::DisplayType::Virtual,
                                           this);

  disp->SetVirtualDisplayResolution(width, height);
  disp->SetPipeline(virtual_pipeline);
  displays_[*display] = std::move(disp);
  return HWC2::Error::None;
}

HWC2::Error DrmHwc::DestroyVirtualDisplay(hwc2_display_t display) {
  ALOGI("Destroying virtual display %" PRIu64, display);

  if (displays_.count(display) == 0) {
    ALOGE("Trying to destroy non-existent display %" PRIu64, display);
    return HWC2::Error::BadDisplay;
  }

  displays_[display]->SetPipeline({});

  /* Wait 0.2s before removing the displays to flush pending HWC2 transactions
   */
  auto &mutex = GetResMan().GetMainLock();
  mutex.unlock();
  const int time_for_sf_to_dispose_display_us = 200000;
  usleep(time_for_sf_to_dispose_display_us);
  mutex.lock();

  displays_.erase(display);

  return HWC2::Error::None;
}

void DrmHwc::Dump(uint32_t *out_size, char *out_buffer) {
  if (out_buffer != nullptr) {
    auto copied_bytes = dump_string_.copy(out_buffer, *out_size);
    *out_size = static_cast<uint32_t>(copied_bytes);
    return;
  }

  std::stringstream output;

  output << "-- drm_hwcomposer --\n\n";

  for (auto &disp : displays_)
    output << disp.second->Dump();

  dump_string_ = output.str();
  *out_size = static_cast<uint32_t>(dump_string_.size());
}

uint32_t DrmHwc::GetMaxVirtualDisplayCount() {
  auto writeback_count = resource_manager_.GetWritebackConnectorsCount();
  writeback_count = std::min(writeback_count, 1U);
  /* Currently, only 1 virtual display is supported. Other cases need testing */
  ALOGI("Max virtual display count: %d", writeback_count);
  return writeback_count;
}

void DrmHwc::DeinitDisplays() {
  for (auto &pair : Displays()) {
    pair.second->SetPipeline(nullptr);
  }
}

}  // namespace android