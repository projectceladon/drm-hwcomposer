/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include "VSyncWorker.h"

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "drm/ResourceManager.h"
#include "utils/log.h"

namespace android {

auto VSyncWorker::CreateInstance(std::shared_ptr<DrmDisplayPipeline> &pipe)
    -> std::unique_ptr<VSyncWorker> {
  auto vsw = std::unique_ptr<VSyncWorker>(new VSyncWorker());

  if (pipe) {
    vsw->high_crtc_ = pipe->crtc->Get()->GetIndexInResArray()
                      << DRM_VBLANK_HIGH_CRTC_SHIFT;
    vsw->drm_fd_ = pipe->device->GetFd();
  }

  vsw->vswt_ = std::thread(&VSyncWorker::ThreadFn, vsw.get());

  return vsw;
}

VSyncWorker::~VSyncWorker() {
  StopThread();

  vswt_.join();
}

void VSyncWorker::UpdateVSyncControl() {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = ShouldEnable();
    last_timestamp_ = -1;
  }

  cv_.notify_all();
}

void VSyncWorker::SetVsyncPeriodNs(uint32_t vsync_period_ns) {
  const std::lock_guard<std::mutex> lock(mutex_);
  vsync_period_ns_ = vsync_period_ns;
}

void VSyncWorker::SetVsyncTimestampTracking(bool enabled) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    enable_vsync_timestamps_ = enabled;
    if (enabled) {
      // Reset the last timestamp so the caller knows if a vsync timestamp is
      // fresh or not.
      last_vsync_timestamp_ = 0;
    }
  }
  UpdateVSyncControl();
}

uint32_t VSyncWorker::GetLastVsyncTimestamp() {
  const std::lock_guard<std::mutex> lock(mutex_);
  return last_vsync_timestamp_;
}

void VSyncWorker::SetTimestampCallback(
    std::optional<VsyncTimestampCallback> &&callback) {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    callback_ = std::move(callback);
  }
  UpdateVSyncControl();
}

void VSyncWorker::StopThread() {
  {
    const std::lock_guard<std::mutex> lock(mutex_);
    thread_exit_ = true;
    enabled_ = false;
  }

  cv_.notify_all();
}

bool VSyncWorker::ShouldEnable() const {
  return enable_vsync_timestamps_ || callback_.has_value();
};

/*
 * Returns the timestamp of the next vsync in phase with last_timestamp_.
 * For example:
 *  last_timestamp_ = 137
 *  frame_ns = 50
 *  current = 683
 *
 *  ret = (50 * ((683 - 137)/50 + 1)) + 137
 *  ret = 687
 *
 *  Thus, we must sleep until timestamp 687 to maintain phase with the last
 *  timestamp.
 */
int64_t VSyncWorker::GetPhasedVSync(int64_t frame_ns, int64_t current) const {
  if (last_timestamp_ < 0)
    return current + frame_ns;

  return (frame_ns * ((current - last_timestamp_) / frame_ns + 1)) +
         last_timestamp_;
}

static const int64_t kOneSecondNs = 1LL * 1000 * 1000 * 1000;

int VSyncWorker::SyntheticWaitVBlank(int64_t *timestamp) {
  int64_t phased_timestamp = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    int64_t time_now = ResourceManager::GetTimeMonotonicNs();
    phased_timestamp = GetPhasedVSync(vsync_period_ns_, time_now);
  }

  struct timespec vsync {};
  vsync.tv_sec = int(phased_timestamp / kOneSecondNs);
  vsync.tv_nsec = int(phased_timestamp - (vsync.tv_sec * kOneSecondNs));

  int ret = 0;
  do {
    ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &vsync, nullptr);
  } while (ret == EINTR);
  if (ret != 0)
    return ret;

  *timestamp = phased_timestamp;
  return 0;
}

void VSyncWorker::ThreadFn() {
  int ret = 0;

  for (;;) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (thread_exit_)
        break;

      if (!enabled_)
        cv_.wait(lock);

      if (!enabled_)
        continue;
    }

    ret = -EAGAIN;
    int64_t timestamp = 0;
    drmVBlank vblank{};

    if (drm_fd_) {
      vblank.request.type = (drmVBlankSeqType)(DRM_VBLANK_RELATIVE |
                                               (high_crtc_ &
                                                DRM_VBLANK_HIGH_CRTC_MASK));
      vblank.request.sequence = 1;

      ret = drmWaitVBlank(*drm_fd_, &vblank);
      if (ret == -EINTR)
        continue;
    }

    if (ret != 0) {
      ret = SyntheticWaitVBlank(&timestamp);
      if (ret != 0)
        continue;
    } else {
      constexpr int kUsToNsMul = 1000;
      timestamp = (int64_t)vblank.reply.tval_sec * kOneSecondNs +
                  (int64_t)vblank.reply.tval_usec * kUsToNsMul;
    }

    std::optional<VsyncTimestampCallback> vsync_callback;
    int64_t vsync_period_ns = 0;

    {
      const std::lock_guard<std::mutex> lock(mutex_);
      if (!enabled_)
        continue;
      if (enable_vsync_timestamps_) {
        last_vsync_timestamp_ = timestamp;
      }
      vsync_callback = callback_;
      vsync_period_ns = vsync_period_ns_;
      last_timestamp_ = timestamp;
    }

    if (vsync_callback) {
      vsync_callback.value()(timestamp, vsync_period_ns);
    }
  }

  ALOGI("VSyncWorker thread exit");
}
}  // namespace android
