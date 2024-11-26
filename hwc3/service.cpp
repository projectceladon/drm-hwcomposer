/*
 * Copyright 2024, The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "drmhwc"
#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <sched.h>

#include "Composer.h"
#include "utils/log.h"

using aidl::android::hardware::graphics::composer3::impl::Composer;

int main(int /*argc*/, char* argv[]) {
  (void)argv;
  ALOGI("hwc3-drm starting up");

  // same as SF main thread
  struct sched_param param = {0};
  param.sched_priority = 2;
  if (sched_setscheduler(0, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
    ALOGE("Couldn't set SCHED_FIFO: %d", errno);
  }

  auto composer = ndk::SharedRefBase::make<Composer>();
  if (!composer) {
    ALOGE("Failed to create composer");
    return -ENOMEM;
  }

  const std::string instance = std::string() + Composer::descriptor +
                               "/default";
  ALOGI("HWC3 service name %s", instance.c_str());
#if __ANDROID_API__ >= 34
  auto status = AServiceManager_addServiceWithFlags(
      composer->asBinder().get(), instance.c_str(),
      AServiceManager_AddServiceFlag::ADD_SERVICE_ALLOW_ISOLATED);
#else
  auto status = AServiceManager_addService(composer->asBinder().get(),
                                           instance.c_str());
#endif
  if (status != STATUS_OK) {
    ALOGE("Failed to register service. Error %d", (int)status);
    return -EINVAL;
  }

  ABinderProcess_joinThreadPool();
  return EXIT_FAILURE;  // should not reach
}
