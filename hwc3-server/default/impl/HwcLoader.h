/*
 * Copyright 2022 The Android Open Source Project
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

#include <memory>
#include <hardware/fb.h>
#include <hardware/gralloc.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer.h>
#define HWC2_INCLUDE_STRINGIFICATION
#define HWC2_USE_CPP11
#include <hardware/hwcomposer2.h>
#undef HWC2_INCLUDE_STRINGIFICATION
#undef HWC2_USE_CPP11
#include <log/log.h>

namespace aidl::android::hardware::graphics::composer3::passthrough{

class HwcLoader {
   public:
    static hwc2_device_t* load();
    // load hwcomposer2 module
    static const hw_module_t* loadModule();

   protected:
    // open hwcomposer2 device, install an adapter if necessary
    static hwc2_device_t* openDeviceWithAdapter(const hw_module_t* module, bool* outAdapted) ;

   private:
    static hwc2_device_t* adaptGrallocModule(const hw_module_t* module);
    static hwc2_device_t* adaptHwc1Device(hwc_composer_device_1* device);
};

}  // namespace aidl::android::hardware::graphics::composer3::passthrough
