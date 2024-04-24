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

#include "HwcLoader.h"
#include <hwc2on1adapter/HWC2On1Adapter.h>
#include <hwc2onfbadapter/HWC2OnFbAdapter.h>
namespace aidl::android::hardware::graphics::composer3::passthrough{

hwc2_device_t* HwcLoader::load() {
    const hw_module_t* module = loadModule();
    if (!module) {
        return nullptr;
    }

    bool adapted;
    hwc2_device_t* device = openDeviceWithAdapter(module, &adapted);
    if (!device) {
        return nullptr;
    }
    return device;
}

const hw_module_t* HwcLoader::loadModule() {
    const hw_module_t* module;
    int error = hw_get_module(HWC_HARDWARE_MODULE_ID, &module);
    if (error) {
        ALOGI("falling back to gralloc module");
        error = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &module);
    }

    if (error) {
        ALOGE("failed to get hwcomposer or gralloc module");
        return nullptr;
    }

    return module;
}

hwc2_device_t* HwcLoader::openDeviceWithAdapter(const hw_module_t* module, bool* outAdapted) {
    if (module->id && std::string(module->id) == GRALLOC_HARDWARE_MODULE_ID) {
        *outAdapted = true;
        return adaptGrallocModule(module);
    }

    hw_device_t* device;
    int error = module->methods->open(module, HWC_HARDWARE_COMPOSER, &device);
    if (error) {
        ALOGE("failed to open hwcomposer device: %s", strerror(-error));
        return nullptr;
    }

    int major = (device->version >> 24) & 0xf;
    if (major != 2) {
        *outAdapted = true;
        return adaptHwc1Device(std::move(reinterpret_cast<hwc_composer_device_1*>(device)));
    }

    *outAdapted = false;
    return reinterpret_cast<hwc2_device_t*>(device);
}

hwc2_device_t* HwcLoader::adaptGrallocModule(const hw_module_t* module) {
    framebuffer_device_t* device;
    int error = framebuffer_open(module, &device);
    if (error) {
        ALOGE("failed to open framebuffer device: %s", strerror(-error));
        return nullptr;
    }

    return new ::android::HWC2OnFbAdapter(device);
}

hwc2_device_t* HwcLoader::adaptHwc1Device(hwc_composer_device_1* device) {
    int minor = (device->common.version >> 16) & 0xf;
    if (minor < 1) {
        ALOGE("hwcomposer 1.0 is not supported");
        device->common.close(&device->common);
        return nullptr;
    }

    return new ::android::HWC2On1Adapter(device);
}

} //namespace aidl::android::hardware::graphics::composer3::passthrough
