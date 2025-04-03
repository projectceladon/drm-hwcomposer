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
#include "hwc2_device/hwcservice.h"
#include "hwc2_device/hwcservice_lib.h"
#include <binder/IPCThreadState.h>
#include <binder/ProcessState.h>
#include "hwc2_device/DrmHwcTwo.h"

namespace android {

class HwcServiceLib {
public:
  bool Start(DrmHwcTwo* hwc) {
    phwcService_ = new android::HwcService();
    android::HwcService *p = static_cast<android::HwcService*>(phwcService_);
    p->Start(hwc);
    sp<ProcessState> proc(ProcessState::self());
    if (!proc.get())
    {
      ALOGE("Error: Fail to new ProcessState.");
      return false;
    }
    proc->startThreadPool();
    IPCThreadState::self()->joinThreadPool();
    return true;
  }
private:
  void* phwcService_;
};

}  // namespace android

static android::HwcServiceLib hwcservice_lib_;
void StartHwcInfoService(android::DrmHwcTwo* hwc) {
  hwcservice_lib_.Start(hwc);
}
