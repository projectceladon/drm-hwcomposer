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

#pragma once

#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include "DrmDevice.h"
#include "utils/thread_annotations.h"

namespace android {

class VSyncWorker {
 public:
  using VsyncTimestampCallback = std::function<void(int64_t /*timestamp*/,
                                                    uint32_t /*period*/)>;

  ~VSyncWorker();

  auto static CreateInstance(std::shared_ptr<DrmDisplayPipeline> &pipe)
      -> std::unique_ptr<VSyncWorker>;

  // Set the expected vsync period. Resets internal timestamp tracking until the
  // next vsync event is tracked.
  void SetVsyncPeriodNs(uint32_t vsync_period_ns);

  // Set or clear a callback to be fired on vsync.
  void SetTimestampCallback(std::optional<VsyncTimestampCallback> &&callback);

  // Enable vsync timestamp tracking. GetLastVsyncTimestamp will return 0 if
  // vsync tracking is disabled, or if no vsync has happened since it was
  // enabled.
  void SetVsyncTimestampTracking(bool enabled);
  uint32_t GetLastVsyncTimestamp();

  void StopThread();

 private:
  VSyncWorker() = default;

  void ThreadFn();

  int64_t GetPhasedVSync(int64_t frame_ns, int64_t current) const
      REQUIRES(mutex_);
  int SyntheticWaitVBlank(int64_t *timestamp);

  void UpdateVSyncControl();
  bool ShouldEnable() const REQUIRES(mutex_);

  SharedFd drm_fd_;
  uint32_t high_crtc_ = 0;

  bool enabled_ GUARDED_BY(mutex_) = false;
  bool thread_exit_ GUARDED_BY(mutex_) = false;
  std::optional<int64_t> last_timestamp_ GUARDED_BY(mutex_);

  // Default to 60Hz refresh rate
  static constexpr uint32_t kDefaultVSPeriodNs = 16666666;
  uint32_t vsync_period_ns_ GUARDED_BY(mutex_) = kDefaultVSPeriodNs;
  bool enable_vsync_timestamps_ GUARDED_BY(mutex_) = false;
  bool last_timestamp_is_fresh_ GUARDED_BY(mutex_) = false;
  std::optional<VsyncTimestampCallback> callback_ GUARDED_BY(mutex_);

  std::condition_variable cv_;
  std::thread vswt_;
  std::mutex mutex_;
};
}  // namespace android
