/*
 * Copyright 2021, The Android Open Source Project
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

#include "HalImpl.h"

#include <aidl/android/hardware/graphics/composer3/IComposerCallback.h>
#include <aidl/android/hardware/graphics/composer3/IComposerClient.h>
#include <android-base/logging.h>
#include "TranslateHwcAidl.h"
#include "Util.h"
#include "HwcLoader.h"
#include <cmath>

using namespace aidl::android::hardware::graphics::composer3::passthrough;

namespace aidl::android::hardware::graphics::composer3::impl {

std::unique_ptr<IComposerHal> IComposerHal::create() {
    hwc2_device_t* device = HwcLoader::load();
    if (!device) {
        ALOGE("HwcLoader::load() failed");
        return nullptr;
    }
    return std::make_unique<HalImpl>(device);
}

namespace hook {

void hotplug(hwc2_callback_data_t callbackData, hwc2_display_t hwcDisplay,
                        int32_t connected) {
    auto hal = static_cast<HalImpl*>(callbackData);
    int64_t display;

    h2a::translate(hwcDisplay, display);
    hal->getEventCallback()->onHotplug(display, connected == HWC2_CONNECTION_CONNECTED);
}

void refresh(hwc2_callback_data_t callbackData, hwc2_display_t hwcDisplay) {
    auto hal = static_cast<HalImpl*>(callbackData);
    int64_t display;

    h2a::translate(hwcDisplay, display);
    hal->getEventCallback()->onRefresh(display);
}

void vsync(hwc2_callback_data_t callbackData, hwc2_display_t hwcDisplay,
                           int64_t timestamp, hwc2_vsync_period_t hwcVsyncPeriodNanos) {
    auto hal = static_cast<HalImpl*>(callbackData);
    int64_t display;
    int32_t vsyncPeriodNanos;

    h2a::translate(hwcDisplay, display);
    h2a::translate(hwcVsyncPeriodNanos, vsyncPeriodNanos);
    hal->getEventCallback()->onVsync(display, timestamp, vsyncPeriodNanos);
}

void vsyncPeriodTimingChanged(hwc2_callback_data_t callbackData,
                                         hwc2_display_t hwcDisplay,
                                         hwc_vsync_period_change_timeline_t* hwcTimeline) {
    auto hal = static_cast<HalImpl*>(callbackData);
    int64_t display;
    VsyncPeriodChangeTimeline timeline;

    h2a::translate(hwcDisplay, display);
    h2a::translate(*hwcTimeline, timeline);
    hal->getEventCallback()->onVsyncPeriodTimingChanged(display, timeline);
}

void vsyncIdle(hwc2_callback_data_t callbackData, hwc2_display_t hwcDisplay) {
    auto hal = static_cast<HalImpl*>(callbackData);
    int64_t display;

    h2a::translate(hwcDisplay, display);
    hal->getEventCallback()->onVsyncIdle(display);
}

void seamlessPossible(hwc2_callback_data_t callbackData, hwc2_display_t hwcDisplay) {
    auto hal = static_cast<HalImpl*>(callbackData);
    int64_t display;

    h2a::translate(hwcDisplay, display);
    hal->getEventCallback()->onSeamlessPossible(display);
}

} // nampesapce hook

HalImpl::HalImpl(hwc2_device_t* device) : mDevice(device) {
    initCaps();
    if (hasCapability(aidl::android::hardware::graphics::composer3::Capability::PRESENT_FENCE_IS_NOT_RELIABLE)) {
        ALOGE("present fence must be reliable");
        mDevice->common.close(&mDevice->common);
        mDevice = nullptr;
    }

    if (!initDispatch()) {
        ALOGE("initDispatch failed!");
        mDevice->common.close(&mDevice->common);
        mDevice = nullptr;
    }
}

HalImpl::~HalImpl(){
    if (mDevice) {
        mDevice->common.close(&mDevice->common);
        mDevice = nullptr;
    }
}

void HalImpl::initCaps() {
    uint32_t count = 0;
    mDevice->getCapabilities(mDevice, &count, nullptr);

    std::vector<int32_t> halCaps(count);
    mDevice->getCapabilities(mDevice, &count, halCaps.data());

    for (auto hwcCap : halCaps) {
        Capability cap;
        h2a::translate(hwcCap, cap);
        mCaps.insert(cap);
    }

    mCaps.insert(Capability::BOOT_DISPLAY_CONFIG);
}

template <typename T>
bool HalImpl::initDispatch(hwc2_function_descriptor_t desc, T* outPfn) {
    auto pfn = mDevice->getFunction(mDevice, desc);
    if (pfn) {
        *outPfn = reinterpret_cast<T>(pfn);
        return true;
    } else {
        ALOGE("failed to get hwcomposer2 function %d", desc);
        return false;
    }
}

template <typename T>
bool HalImpl::initOptionalDispatch(hwc2_function_descriptor_t desc, T* outPfn) {
    auto pfn = mDevice->getFunction(mDevice, desc);
    if (pfn) {
        *outPfn = reinterpret_cast<T>(pfn);
        return true;
    } else {
        return false;
    }
}

bool HalImpl::initDispatch() {
    if (
        !initDispatch(HWC2_FUNCTION_ACCEPT_DISPLAY_CHANGES, &mDispatch.acceptDisplayChanges) ||
        !initDispatch(HWC2_FUNCTION_CREATE_LAYER, &mDispatch.createLayer) ||
        !initDispatch(HWC2_FUNCTION_CREATE_VIRTUAL_DISPLAY, &mDispatch.createVirtualDisplay) ||
        !initDispatch(HWC2_FUNCTION_DESTROY_LAYER, &mDispatch.destroyLayer) ||
        !initDispatch(HWC2_FUNCTION_DESTROY_VIRTUAL_DISPLAY, &mDispatch.destroyVirtualDisplay) ||
        !initDispatch(HWC2_FUNCTION_DUMP, &mDispatch.dump) ||
        !initDispatch(HWC2_FUNCTION_GET_ACTIVE_CONFIG, &mDispatch.getActiveConfig) ||
        !initDispatch(HWC2_FUNCTION_GET_CHANGED_COMPOSITION_TYPES, &mDispatch.getChangedCompositionTypes) ||
        !initDispatch(HWC2_FUNCTION_GET_CLIENT_TARGET_SUPPORT, &mDispatch.getClientTargetSupport) ||
        !initDispatch(HWC2_FUNCTION_GET_COLOR_MODES, &mDispatch.getColorModes) ||
        !initDispatch(HWC2_FUNCTION_GET_DISPLAY_ATTRIBUTE, &mDispatch.getDisplayAttribute) ||
        !initDispatch(HWC2_FUNCTION_GET_DISPLAY_CONFIGS, &mDispatch.getDisplayConfigs) ||
        !initDispatch(HWC2_FUNCTION_GET_DISPLAY_NAME, &mDispatch.getDisplayName) ||
        !initDispatch(HWC2_FUNCTION_GET_DISPLAY_REQUESTS, &mDispatch.getDisplayRequests) ||
        !initDispatch(HWC2_FUNCTION_GET_DISPLAY_TYPE, &mDispatch.getDisplayType) ||
        !initDispatch(HWC2_FUNCTION_GET_DOZE_SUPPORT, &mDispatch.getDozeSupport) ||
        !initDispatch(HWC2_FUNCTION_GET_HDR_CAPABILITIES, &mDispatch.getHdrCapabilities) ||
        !initDispatch(HWC2_FUNCTION_GET_MAX_VIRTUAL_DISPLAY_COUNT, &mDispatch.getMaxVirtualDisplayCount) ||
        !initDispatch(HWC2_FUNCTION_GET_RELEASE_FENCES, &mDispatch.getReleaseFences) ||
        !initDispatch(HWC2_FUNCTION_PRESENT_DISPLAY, &mDispatch.presentDisplay) ||
        !initDispatch(HWC2_FUNCTION_REGISTER_CALLBACK, &mDispatch.registerCallback) ||
        !initDispatch(HWC2_FUNCTION_SET_ACTIVE_CONFIG, &mDispatch.setActiveConfig) ||
        !initDispatch(HWC2_FUNCTION_SET_CLIENT_TARGET, &mDispatch.setClientTarget) ||
        !initDispatch(HWC2_FUNCTION_SET_COLOR_MODE, &mDispatch.setColorMode) ||
        !initDispatch(HWC2_FUNCTION_SET_COLOR_TRANSFORM, &mDispatch.setColorTransform) ||
        !initDispatch(HWC2_FUNCTION_SET_CURSOR_POSITION, &mDispatch.setCursorPosition) ||
        !initDispatch(HWC2_FUNCTION_SET_LAYER_BLEND_MODE, &mDispatch.setLayerBlendMode) ||
        !initDispatch(HWC2_FUNCTION_SET_LAYER_BUFFER, &mDispatch.setLayerBuffer) ||
        !initDispatch(HWC2_FUNCTION_SET_LAYER_COLOR, &mDispatch.setLayerColor) ||
        !initDispatch(HWC2_FUNCTION_SET_LAYER_COMPOSITION_TYPE, &mDispatch.setLayerCompositionType) ||
        !initDispatch(HWC2_FUNCTION_SET_LAYER_DATASPACE, &mDispatch.setLayerDataspace) ||
        !initDispatch(HWC2_FUNCTION_SET_LAYER_DISPLAY_FRAME, &mDispatch.setLayerDisplayFrame) ||
        !initDispatch(HWC2_FUNCTION_SET_LAYER_PLANE_ALPHA, &mDispatch.setLayerPlaneAlpha)
        ) {
        return false;
    }

    if (hasCapability(aidl::android::hardware::graphics::composer3::Capability::SIDEBAND_STREAM)) {
        if (!initDispatch(HWC2_FUNCTION_SET_LAYER_SIDEBAND_STREAM,
                          &mDispatch.setLayerSidebandStream)) {
            return false;
        }
    }

    if (!initDispatch(HWC2_FUNCTION_SET_LAYER_SOURCE_CROP, &mDispatch.setLayerSourceCrop) ||
        !initDispatch(HWC2_FUNCTION_SET_LAYER_SURFACE_DAMAGE, &mDispatch.setLayerSurfaceDamage) ||
        !initDispatch(HWC2_FUNCTION_SET_LAYER_TRANSFORM, &mDispatch.setLayerTransform) ||
        !initDispatch(HWC2_FUNCTION_SET_LAYER_VISIBLE_REGION, &mDispatch.setLayerVisibleRegion) ||
        !initDispatch(HWC2_FUNCTION_SET_LAYER_Z_ORDER, &mDispatch.setLayerZOrder) ||
        !initDispatch(HWC2_FUNCTION_SET_OUTPUT_BUFFER, &mDispatch.setOutputBuffer) ||
        !initDispatch(HWC2_FUNCTION_SET_POWER_MODE, &mDispatch.setPowerMode) ||
        !initDispatch(HWC2_FUNCTION_SET_VSYNC_ENABLED, &mDispatch.setVsyncEnabled) ||
        !initDispatch(HWC2_FUNCTION_VALIDATE_DISPLAY, &mDispatch.validateDisplay)
        ) {
        return false;
    }
    //  2.2
    initOptionalDispatch(HWC2_FUNCTION_SET_LAYER_FLOAT_COLOR, &mDispatch.setLayerFloatColor);
    initOptionalDispatch(HWC2_FUNCTION_SET_LAYER_PER_FRAME_METADATA,&mDispatch.setLayerPerFrameMetadata) ;
    initOptionalDispatch(HWC2_FUNCTION_GET_PER_FRAME_METADATA_KEYS, &mDispatch.getPerFrameMetadataKeys) ;
    initOptionalDispatch(HWC2_FUNCTION_SET_READBACK_BUFFER, &mDispatch.setReadbackBuffer);
    initOptionalDispatch(HWC2_FUNCTION_GET_READBACK_BUFFER_ATTRIBUTES,&mDispatch.getReadbackBufferAttributes) ;
    initOptionalDispatch(HWC2_FUNCTION_GET_READBACK_BUFFER_FENCE,&mDispatch.getReadbackBufferFence);
    initOptionalDispatch(HWC2_FUNCTION_GET_RENDER_INTENTS, &mDispatch.getRenderIntents);
    initOptionalDispatch(HWC2_FUNCTION_SET_COLOR_MODE_WITH_RENDER_INTENT, &mDispatch.setColorModeWithRenderIntent);
    initOptionalDispatch(HWC2_FUNCTION_GET_DATASPACE_SATURATION_MATRIX, &mDispatch.getDataspaceSaturationMatrix);
   
    //  2.3
    if(!initDispatch(HWC2_FUNCTION_GET_DISPLAY_CAPABILITIES, &mDispatch.getDisplayCapabilities)||
       !initDispatch(HWC2_FUNCTION_SET_DISPLAY_BRIGHTNESS, &mDispatch.setDisplayBrightness)){
        return false;
    }

    initOptionalDispatch(HWC2_FUNCTION_GET_DISPLAY_IDENTIFICATION_DATA,&mDispatch.getDisplayIdentificationData);
    initOptionalDispatch(HWC2_FUNCTION_SET_LAYER_COLOR_TRANSFORM, &mDispatch.setLayerColorTransform);
    initOptionalDispatch(HWC2_FUNCTION_GET_DISPLAYED_CONTENT_SAMPLING_ATTRIBUTES,&mDispatch.getDisplayedContentSamplingAttributes);
    initOptionalDispatch(HWC2_FUNCTION_SET_DISPLAYED_CONTENT_SAMPLING_ENABLED,&mDispatch.setDisplayedContentSamplingEnabled);
    initOptionalDispatch(HWC2_FUNCTION_GET_DISPLAYED_CONTENT_SAMPLE, &mDispatch.getDisplayedContentSample);
    initOptionalDispatch(HWC2_FUNCTION_SET_LAYER_PER_FRAME_METADATA_BLOBS, &mDispatch.setLayerPerFrameMetadataBlobs);
    initOptionalDispatch(HWC2_FUNCTION_GET_DISPLAY_BRIGHTNESS_SUPPORT, &mDispatch.getDisplayBrightnessSupport);
    //  2.4
    if(!initDispatch(HWC2_FUNCTION_GET_DISPLAY_VSYNC_PERIOD, &mDispatch.getDisplayVsyncPeriod) ||
       !initDispatch(HWC2_FUNCTION_SET_ACTIVE_CONFIG_WITH_CONSTRAINTS, &mDispatch.setActiveConfigWithConstraints)){
        return false;
    }
 
    initOptionalDispatch(HWC2_FUNCTION_GET_DISPLAY_CONNECTION_TYPE, &mDispatch.getDisplayConnectionType);
    initOptionalDispatch(HWC2_FUNCTION_SET_AUTO_LOW_LATENCY_MODE, &mDispatch.setAutoLowLatencyMode);
    initOptionalDispatch(HWC2_FUNCTION_GET_SUPPORTED_CONTENT_TYPES, &mDispatch.getSupportedContentTypes);
    initOptionalDispatch(HWC2_FUNCTION_SET_CONTENT_TYPE, &mDispatch.setContentType);
    initOptionalDispatch(HWC2_FUNCTION_GET_CLIENT_TARGET_PROPERTY, &mDispatch.getClientTargetProperty) ;
    initOptionalDispatch(HWC2_FUNCTION_SET_LAYER_GENERIC_METADATA, &mDispatch.setLayerGenericMetadata);
    initOptionalDispatch(HWC2_FUNCTION_GET_LAYER_GENERIC_METADATA_KEY, &mDispatch.getLayerGenericMetadataKey);
 

    return true;
}

bool HalImpl::hasCapability(Capability cap) {
    return mCaps.find(cap) != mCaps.end();
}

void HalImpl::getCapabilities(std::vector<Capability>* caps) {
    caps->clear();
    caps->insert(caps->begin(), mCaps.begin(), mCaps.end());
}

void HalImpl::dumpDebugInfo(std::string* output) {
    if (output == nullptr) return;
    if (!mDispatch.dump) return;

    uint32_t len = 0;
    mDispatch.dump(mDevice, &len, nullptr);
    if (len > 0) {
        // mDispatch.dump(..) api is expecting char *.
        // So reserve memory before passing address as an argument.
        output->reserve(len);
        mDispatch.dump(mDevice, &len, output->data());
    }
}

void HalImpl::registerEventCallback(EventCallback* callback) {
    mEventCallback = callback;

    mDispatch.registerCallback(mDevice, HWC2_CALLBACK_HOTPLUG, this,
                               reinterpret_cast<hwc2_function_pointer_t>(hook::hotplug));
    mDispatch.registerCallback(mDevice, HWC2_CALLBACK_REFRESH, this,
                               reinterpret_cast<hwc2_function_pointer_t>(hook::refresh));
    mDispatch.registerCallback(mDevice, HWC2_CALLBACK_VSYNC_2_4, this,
                               reinterpret_cast<hwc2_function_pointer_t>(hook::vsync));
    mDispatch.registerCallback(mDevice, HWC2_CALLBACK_VSYNC_PERIOD_TIMING_CHANGED, this,
                               reinterpret_cast<hwc2_function_pointer_t>(hook::vsyncPeriodTimingChanged));
    mDispatch.registerCallback(mDevice, HWC2_CALLBACK_SEAMLESS_POSSIBLE, this,
                               reinterpret_cast<hwc2_function_pointer_t>(hook::seamlessPossible));
    // ToDo register HWC3 Callback TRANSACTION_onVsyncIdle
}

void HalImpl::unregisterEventCallback() {
    mDispatch.registerCallback(mDevice, HWC2_CALLBACK_HOTPLUG, this, nullptr);
    mDispatch.registerCallback(mDevice, HWC2_CALLBACK_REFRESH, this, nullptr);
    mDispatch.registerCallback(mDevice, HWC2_CALLBACK_VSYNC_2_4, this, nullptr);
    mDispatch.registerCallback(mDevice, HWC2_CALLBACK_VSYNC_PERIOD_TIMING_CHANGED, this, nullptr);
    mDispatch.registerCallback(mDevice, HWC2_CALLBACK_SEAMLESS_POSSIBLE, this, nullptr);
    // ToDo unregister HWC3 Callback TRANSACTION_onVsyncIdle
    mEventCallback = nullptr;
}

int32_t HalImpl::acceptDisplayChanges(int64_t display) {
    if (!mDispatch.acceptDisplayChanges) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    return mDispatch.acceptDisplayChanges(mDevice, display);
}

int32_t HalImpl::createLayer(int64_t display, int64_t* outLayer) {
    if (!mDispatch.createLayer) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    hwc2_layer_t hwcLayer = 0;
    RET_IF_ERR(mDispatch.createLayer(mDevice, display,&hwcLayer));

    h2a::translate(hwcLayer, *outLayer);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::destroyLayer(int64_t display, int64_t layer) {
    if (!mDispatch.destroyLayer) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    RET_IF_ERR(mDispatch.destroyLayer(mDevice, display, layer));
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::createVirtualDisplay(uint32_t width, uint32_t height, AidlPixelFormat format,
                                      VirtualDisplay* outDisplay) {
    if (!mDispatch.createVirtualDisplay) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    int32_t hwcFormat;
    a2h::translate(format, hwcFormat);
    RET_IF_ERR(mDispatch.createVirtualDisplay(mDevice, width, height, &hwcFormat,(uint64_t*)&(outDisplay->display)));

    return HWC2_ERROR_NONE;
}

int32_t HalImpl::destroyVirtualDisplay(int64_t display) {
    if (!mDispatch.destroyVirtualDisplay) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    return mDispatch.destroyVirtualDisplay(mDevice, display);
}

int32_t HalImpl::getActiveConfig(int64_t display, int32_t* outConfig) {
    if (!mDispatch.getActiveConfig) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    hwc2_config_t hwcConfig;
    RET_IF_ERR(mDispatch.getActiveConfig(mDevice, display,&hwcConfig));

    h2a::translate(hwcConfig, *outConfig);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getColorModes(int64_t display, std::vector<ColorMode>* outModes) {
    if (!mDispatch.getColorModes) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    uint32_t count = 0;
    RET_IF_ERR(mDispatch.getColorModes(mDevice, display,&count, nullptr));

    std::vector<int32_t> hwcModes(count);
    RET_IF_ERR(mDispatch.getColorModes(mDevice, display,&count, hwcModes.data()));

    h2a::translate(hwcModes, *outModes);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDataspaceSaturationMatrix([[maybe_unused]] common::Dataspace dataspace,
                                              std::vector<float>* matrix) {
    // if (!mDispatch.getDataspaceSaturationMatrix) {
    //     return HWC2_ERROR_UNSUPPORTED;
    // }
    // auto error = mDispatch.getDataspaceSaturationMatrix(mDevice, static_cast<int32_t>(dataspace),matrix->data());
    if(dataspace == common::Dataspace::UNKNOWN)
       return HWC2_ERROR_BAD_PARAMETER;

    auto error = HWC2_ERROR_UNSUPPORTED;
    if (error != HWC2_ERROR_NONE) {
        std::vector<float> unitMatrix = {
                1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
                0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
        };

        *matrix = std::move(unitMatrix);
    }
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDisplayAttribute(int64_t display, int32_t config,
                                     DisplayAttribute attribute, int32_t* outValue) {
    if (!mDispatch.getDisplayAttribute) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    hwc2_config_t hwcConfig;
    int32_t hwcAttr;
    a2h::translate(config, hwcConfig);
    a2h::translate(attribute, hwcAttr);

    auto err = mDispatch.getDisplayAttribute(mDevice, display, hwcConfig, hwcAttr, outValue);
    if (err != HWC2_ERROR_NONE && *outValue == -1) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDisplayBrightnessSupport(int64_t display, bool& outSupport) {
    if (!mDispatch.getDisplayBrightnessSupport) {
        outSupport = false;
        std::vector<DisplayCapability> capabilities;

        auto error = getDisplayCapabilities(display, &capabilities);
        if (error != HWC2_ERROR_NONE) 
            return HWC2_ERROR_UNSUPPORTED;
        
        outSupport =std::find(capabilities.begin(), capabilities.end(),
                          DisplayCapability::BRIGHTNESS) != capabilities.end();
        return HWC2_ERROR_UNSUPPORTED;
    }

    RET_IF_ERR(mDispatch.getDisplayBrightnessSupport(mDevice, display, &outSupport));
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDisplayCapabilities(int64_t display,
                                        std::vector<DisplayCapability>* caps) {
    if (!mDispatch.getDisplayCapabilities) {
        ALOGE(" unsupported getDisplayCapabilities ");
        return HWC2_ERROR_UNSUPPORTED;
    }

    uint32_t count = 0;
    RET_IF_ERR(mDispatch.getDisplayCapabilities(mDevice, display, &count, nullptr));

    std::vector<uint32_t> hwcCaps(count);
    RET_IF_ERR(mDispatch.getDisplayCapabilities(mDevice, display, &count, hwcCaps.data()));

    h2a::translate(hwcCaps, *caps);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDisplayConfigs(int64_t display, std::vector<int32_t>* configs) {
    if (!mDispatch.getDisplayConfigs) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    uint32_t count = 0;
    RET_IF_ERR(mDispatch.getDisplayConfigs(mDevice, display, &count, nullptr));

    std::vector<hwc2_config_t> hwcConfigs(count);
    RET_IF_ERR(mDispatch.getDisplayConfigs(mDevice, display, &count, hwcConfigs.data()));

    h2a::translate(hwcConfigs, *configs);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDisplayConnectionType(int64_t display, DisplayConnectionType* outType) {
    if (!mDispatch.getDisplayConnectionType) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    uint32_t hwcType = HWC2_DISPLAY_CONNECTION_TYPE_INTERNAL;
    RET_IF_ERR(mDispatch.getDisplayConnectionType(mDevice,display,&hwcType));
    h2a::translate(hwcType, *outType);

    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDisplayIdentificationData(int64_t display, DisplayIdentification *id) {
    if (!mDispatch.getDisplayIdentificationData) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    uint8_t port;
    uint32_t count = 0;
    RET_IF_ERR(mDispatch.getDisplayIdentificationData(mDevice, display,&port, &count, nullptr));

    id->data.resize(count);
    RET_IF_ERR(mDispatch.getDisplayIdentificationData(mDevice, display,&port, &count, id->data.data()));

    h2a::translate(port, id->port);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDisplayName(int64_t display, std::string* outName) {
    if (!mDispatch.getDisplayName) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    uint32_t count = 0;
    RET_IF_ERR(mDispatch.getDisplayName(mDevice, display,&count, nullptr));

    outName->resize(count);
    RET_IF_ERR(mDispatch.getDisplayName(mDevice, display,&count, outName->data()));

    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDisplayVsyncPeriod(int64_t display, int32_t* outVsyncPeriod) {
    if (!mDispatch.getDisplayVsyncPeriod) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    hwc2_vsync_period_t hwcVsyncPeriod;
    RET_IF_ERR(mDispatch.getDisplayVsyncPeriod(mDevice, display, &hwcVsyncPeriod));

    h2a::translate(hwcVsyncPeriod, *outVsyncPeriod);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDisplayedContentSample([[maybe_unused]] int64_t display,
                                           [[maybe_unused]] int64_t maxFrames,
                                           [[maybe_unused]] int64_t timestamp,
                                           [[maybe_unused]] DisplayContentSample* samples) {
    return HWC2_ERROR_UNSUPPORTED;
}


int32_t HalImpl::getDisplayedContentSamplingAttributes(
        [[maybe_unused]] int64_t display,
        [[maybe_unused]] DisplayContentSamplingAttributes* attrs) {
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HalImpl::getDisplayPhysicalOrientation([[maybe_unused]] int64_t display,
                                               [[maybe_unused]] common::Transform* orientation) {
    if(static_cast<int>(display) == -1)
        return HWC2_ERROR_BAD_DISPLAY;
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDozeSupport(int64_t display, bool& support) {
    if (!mDispatch.getDozeSupport) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t hwcSupport;
    RET_IF_ERR(mDispatch.getDozeSupport(mDevice, display, &hwcSupport));

    h2a::translate(hwcSupport, support);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getHdrCapabilities(int64_t display, HdrCapabilities* caps) {
    if (!mDispatch.getHdrCapabilities) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    uint32_t count = 0;
    RET_IF_ERR(mDispatch.getHdrCapabilities(mDevice, display,&count, nullptr, &caps->maxLuminance,
                                              &caps->maxAverageLuminance,
                                              &caps->minLuminance));
    std::vector<int32_t> hwcHdrTypes(count);
    RET_IF_ERR(mDispatch.getHdrCapabilities(mDevice, display,&count, hwcHdrTypes.data(),
                                              &caps->maxLuminance,
                                              &caps->maxAverageLuminance,
                                              &caps->minLuminance));

    h2a::translate(hwcHdrTypes, caps->types);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getMaxVirtualDisplayCount(int32_t* count) {
    if (!mDispatch.getMaxVirtualDisplayCount) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    uint32_t hwcCount = mDispatch.getMaxVirtualDisplayCount(mDevice);
    h2a::translate(hwcCount, *count);

    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getPerFrameMetadataKeys(int64_t display,
                                         std::vector<PerFrameMetadataKey>* keys) {
    if (!mDispatch.getPerFrameMetadataKeys) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    uint32_t count = 0;
    RET_IF_ERR(mDispatch.getPerFrameMetadataKeys(mDevice, display, &count, nullptr));

    std::vector<PerFrameMetadataKey> outkeys(count);
    RET_IF_ERR(mDispatch.getPerFrameMetadataKeys(mDevice, display, &count,
            reinterpret_cast<std::underlying_type<PerFrameMetadataKey>::type*>(
                outkeys.data())));
    outkeys.resize(count);
    *keys = std::move(outkeys);

    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getReadbackBufferAttributes(int64_t display,
                                             ReadbackBufferAttributes* attrs) {
    if (!mDispatch.getReadbackBufferAttributes) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t format = -1;
    int32_t dataspace = -1;
    RET_IF_ERR(mDispatch.getReadbackBufferAttributes(mDevice, display, &format, &dataspace));

    h2a::translate(format, attrs->format);
    h2a::translate(dataspace, attrs->dataspace);

    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getReadbackBufferFence(int64_t display,
                                        ndk::ScopedFileDescriptor* acquireFence) {
    if (!mDispatch.getReadbackBufferFence) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t fd = -1;
    RET_IF_ERR(mDispatch.getReadbackBufferFence(mDevice, display, &fd));

    h2a::translate(fd, *acquireFence);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getRenderIntents(int64_t display, ColorMode mode,
                                  std::vector<RenderIntent>* intents) {
    if (!mDispatch.getRenderIntents) {
       
        int32_t hwc_type = HWC2_DISPLAY_TYPE_INVALID;
        if (mDispatch.getDisplayType(mDevice, display, &hwc_type) == HWC2_ERROR_BAD_DISPLAY) {
            return HWC2_ERROR_BAD_DISPLAY;
        }
        if (mode < ColorMode::NATIVE || mode > ColorMode::DISPLAY_P3) {
            return HWC2_ERROR_BAD_PARAMETER;
        }

        *intents = std::vector<RenderIntent>({RenderIntent::COLORIMETRIC});
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t hwcMode;
    uint32_t count = 0;
    a2h::translate(mode, hwcMode);
    RET_IF_ERR(mDispatch.getRenderIntents(mDevice, display, hwcMode, &count, nullptr));

    std::vector<int32_t> hwcIntents(count);
    RET_IF_ERR(mDispatch.getRenderIntents(mDevice, display,hwcMode, &count, hwcIntents.data()));
    RET_IF_ERR(mDispatch.getRenderIntents(mDevice, display, hwcMode, &count,hwcIntents.data()));

    h2a::translate(hwcIntents, *intents);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getSupportedContentTypes(int64_t display, std::vector<ContentType>* types) {
    if (!mDispatch.getSupportedContentTypes) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    uint32_t count = 0;
    RET_IF_ERR(mDispatch.getSupportedContentTypes(mDevice, display, &count, nullptr));

    std::vector<uint32_t> hwcTypes(count);
    RET_IF_ERR(mDispatch.getSupportedContentTypes(mDevice, display, &count, hwcTypes.data()));

    h2a::translate(hwcTypes, *types);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::flushDisplayBrightnessChange([[maybe_unused]] int64_t display) {
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HalImpl::presentDisplay(int64_t display, ndk::ScopedFileDescriptor& fence,
                       std::vector<int64_t>* outLayers,
                       std::vector<ndk::ScopedFileDescriptor>* outReleaseFences) {
    if (!mDispatch.presentDisplay) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t hwcFence;
    RET_IF_ERR(mDispatch.presentDisplay(mDevice, display, &hwcFence));
    h2a::translate(hwcFence, fence);

    uint32_t count = 0;
    RET_IF_ERR(mDispatch.getReleaseFences(mDevice, display, &count, nullptr, nullptr));

    std::vector<hwc2_layer_t> hwcLayers(count);
    std::vector<int32_t> hwcFences(count);
    RET_IF_ERR(mDispatch.getReleaseFences(mDevice, display, &count, hwcLayers.data(),hwcFences.data()));

    h2a::translate(hwcLayers, *outLayers);
    h2a::translate(hwcFences, *outReleaseFences);

    return HWC2_ERROR_NONE;
}

int32_t HalImpl::setActiveConfig(int64_t display, int32_t config) {
    if (!mDispatch.setActiveConfig) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    return mDispatch.setActiveConfig(mDevice, display, config);
}

int32_t HalImpl::setActiveConfigWithConstraints(
            int64_t display, int32_t config,
            const VsyncPeriodChangeConstraints& vsyncPeriodChangeConstraints,
            VsyncPeriodChangeTimeline* timeline) {

    if (!mDispatch.setActiveConfigWithConstraints) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    hwc2_config_t hwcConfig;
    hwc_vsync_period_change_constraints_t hwcConstraints;

    a2h::translate(config, hwcConfig);
    a2h::translate(vsyncPeriodChangeConstraints, hwcConstraints);

    hwc_vsync_period_change_timeline_t hwcTimeline;
    RET_IF_ERR(mDispatch.setActiveConfigWithConstraints(
                mDevice, display, hwcConfig, &hwcConstraints, &hwcTimeline));

    h2a::translate(hwcTimeline, *timeline);
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::setBootDisplayConfig([[maybe_unused]] int64_t display, 
                                      [[maybe_unused]] int32_t config) {
    if(static_cast<int>(display) == -1)
        return HWC2_ERROR_BAD_DISPLAY;
    if(config == IComposerClient::INVALID_CONFIGURATION)
        return HWC2_ERROR_BAD_CONFIG;
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::clearBootDisplayConfig([[maybe_unused]] int64_t display) {
    if(static_cast<int>(display) == -1)
        return HWC2_ERROR_BAD_DISPLAY;
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getPreferredBootDisplayConfig([[maybe_unused]] int64_t display,
                                               [[maybe_unused]] int32_t* config) {
    if(static_cast<int>(display) == -1)
        return HWC2_ERROR_BAD_DISPLAY;
    std::vector<int32_t> configs;
    getDisplayConfigs(display, &configs);
    if(!configs.empty())
        *config = configs[0];
    else
        *config = 1;
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::setAutoLowLatencyMode(int64_t display, bool on) {
    if (!mDispatch.setAutoLowLatencyMode) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    return mDispatch.setAutoLowLatencyMode(mDevice, display, on);
}

int32_t HalImpl::setClientTarget(int64_t display, buffer_handle_t target,
                                 const ndk::ScopedFileDescriptor& fence,
                                 common::Dataspace dataspace,
                                 const std::vector<common::Rect>& damage) {
    if (!mDispatch.setClientTarget) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t hwcFence;
    int32_t hwcDataspace;
    std::vector<hwc_rect_t> hwcDamage;

    a2h::translate(fence, hwcFence);
    a2h::translate(dataspace, hwcDataspace);
    a2h::translate(damage, hwcDamage);
    hwc_region_t region = { hwcDamage.size(), hwcDamage.data() };
    return mDispatch.setClientTarget(mDevice, display, target, hwcFence, hwcDataspace, region);
}

int32_t HalImpl::setColorMode(int64_t display, ColorMode mode, RenderIntent intent) {
    int32_t hwcMode;
    a2h::translate(mode, hwcMode);
    if (!mDispatch.setColorModeWithRenderIntent ) {
        if (intent < RenderIntent::COLORIMETRIC || intent > RenderIntent::TONE_MAP_ENHANCE) {
            return HWC2_ERROR_BAD_PARAMETER;
        }
        if (intent != RenderIntent::COLORIMETRIC) {
            return HWC2_ERROR_UNSUPPORTED;
        }

        if (!mDispatch.setColorMode) {
            return HWC2_ERROR_UNSUPPORTED;
        }

        return mDispatch.setColorMode(mDevice,display,hwcMode);
    }
    int32_t hwcIntent;
    a2h::translate(intent, hwcIntent);
    return mDispatch.setColorModeWithRenderIntent(mDevice, display, hwcMode, hwcIntent);
}

int32_t HalImpl::setColorTransform(int64_t display, const std::vector<float>& matrix) {

    if (!mDispatch.setColorTransform) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    const bool isIdentity = (std::equal(matrix.begin(), matrix.end(), mkIdentity.begin()));
    const common::ColorTransform hint = isIdentity ? common::ColorTransform::IDENTITY
                                                   : common::ColorTransform::ARBITRARY_MATRIX;
    int32_t hwcHint;
    a2h::translate(hint, hwcHint);
    return mDispatch.setColorTransform(mDevice, display, matrix.data(), hwcHint);
}

int32_t HalImpl::setContentType(int64_t display, ContentType contentType) {

    if (!mDispatch.setContentType) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t type;
    a2h::translate(contentType, type);
    return mDispatch.setContentType(mDevice, display, type);
}

int32_t HalImpl::setDisplayBrightness(int64_t display, float brightness) {
    if (std::isnan(brightness) || brightness > 1.0f ||
         (brightness < 0.0f && brightness != -1.0f)) {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    if (!mDispatch.setDisplayBrightness) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    return mDispatch.setDisplayBrightness(mDevice, display, brightness);
}

int32_t HalImpl::setDisplayedContentSamplingEnabled(int64_t display, bool enable,
                                                    FormatColorComponent componentMask,int64_t maxFrames) {

    if (!mDispatch.setDisplayedContentSamplingEnabled) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    return mDispatch.setDisplayedContentSamplingEnabled(
            mDevice, display, static_cast<int32_t>(enable), static_cast<uint8_t>(componentMask), maxFrames);
}

int32_t HalImpl::setLayerBlendMode(int64_t display, int64_t layer, common::BlendMode mode) {

    if (!mDispatch.setLayerBlendMode) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t hwcMode;
    a2h::translate(mode, hwcMode);
    return mDispatch.setLayerBlendMode(mDevice, display, layer, hwcMode);
}

int32_t HalImpl::setLayerBuffer(int64_t display, int64_t layer, buffer_handle_t buffer,
                                const ndk::ScopedFileDescriptor& acquireFence) {
    if (!mDispatch.setLayerBuffer) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    int32_t hwcFd;
    a2h::translate(acquireFence, hwcFd);
    return mDispatch.setLayerBuffer(mDevice, display, layer, buffer, hwcFd);
}

int32_t HalImpl::setLayerColor(int64_t display, int64_t layer, Color color) {

    if (!mDispatch.setLayerColor) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    hwc_color_t hwcColor;
    a2h::translate(color, hwcColor);
    return mDispatch.setLayerColor(mDevice, display, layer, hwcColor);
}

int32_t HalImpl::setLayerColorTransform(int64_t display, int64_t layer,
                                        const std::vector<float>& matrix) {
    if (!mDispatch.setLayerColorTransform) {
        const bool isIdentity = (std::equal(matrix.begin(), matrix.end(), mkIdentity.begin()));
        if (isIdentity) {
            mClientCompositionLayers[display].erase(layer);
            return HWC2_ERROR_UNSUPPORTED;
        }

        mClientCompositionLayers[display].insert(layer);
        return HWC2_ERROR_UNSUPPORTED;
    }
    return mDispatch.setLayerColorTransform(mDevice, display, layer, matrix.data());
}

int32_t HalImpl::setLayerCompositionType(int64_t display, int64_t layer, Composition type) {

    if (!mDispatch.setLayerCompositionType) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t hwcType;
    a2h::translate(type, hwcType);
    return mDispatch.setLayerCompositionType(mDevice, display, layer, hwcType);
}

int32_t HalImpl::setLayerCursorPosition(int64_t display, int64_t layer, int32_t x, int32_t y) {
    if (!mDispatch.setCursorPosition) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    return mDispatch.setCursorPosition(mDevice, display, layer, x, y);
}

int32_t HalImpl::setLayerDataspace(int64_t display, int64_t layer, common::Dataspace dataspace) {
    if (!mDispatch.setLayerDataspace) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t hwcDataspace;
    a2h::translate(dataspace, hwcDataspace);
    return mDispatch.setLayerDataspace(mDevice, display, layer, hwcDataspace);
}

int32_t HalImpl::setLayerDisplayFrame(int64_t display, int64_t layer, const common::Rect& frame) {
    if (!mDispatch.setLayerDisplayFrame) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    hwc_rect_t hwcFrame;
    a2h::translate(frame, hwcFrame);
    return mDispatch.setLayerDisplayFrame(mDevice, display, layer, hwcFrame);
}

int32_t HalImpl::setLayerPerFrameMetadata(int64_t display, int64_t layer,
                                          const std::vector<std::optional<PerFrameMetadata>>& metadata) {
    if (!mDispatch.setLayerPerFrameMetadata) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    std::vector<int32_t> keys;
    std::vector<float> values;

    for (const auto& m : metadata) {
        if (m) {
            int32_t key;
            a2h::translate(m->key, key);
            keys.push_back(key);
            values.push_back(m->value);
        }
    }

    return mDispatch.setLayerPerFrameMetadata(mDevice, display, layer, metadata.size(), keys.data(),
                                              values.data());
}

int32_t HalImpl::setLayerPerFrameMetadataBlobs(int64_t display, int64_t layer,
                           const std::vector<std::optional<PerFrameMetadataBlob>>& blobs) {
    if (!mDispatch.setLayerPerFrameMetadataBlobs) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    std::vector<int32_t> keys;
    std::vector<uint32_t> sizes;
    std::vector<uint8_t> values;
    for(auto b: blobs){
        if(b){
            int32_t key;
            a2h::translate(b->key, key);
            keys.push_back(key);
            sizes.push_back(b->blob.size());
            values.insert(values.end(), b->blob.begin(), b->blob.end());
        }
    }

    return mDispatch.setLayerPerFrameMetadataBlobs(
            mDevice, display, layer, static_cast<uint32_t>(blobs.size()),
           keys.data(), reinterpret_cast<uint32_t*>(sizes.data()),
            values.data());
}

int32_t HalImpl::setLayerPlaneAlpha(int64_t display, int64_t layer, float alpha) {
    if (!mDispatch.setLayerPlaneAlpha) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    return mDispatch.setLayerPlaneAlpha(mDevice, display, layer, alpha);
}

int32_t HalImpl::setLayerSidebandStream([[maybe_unused]] int64_t display,
                                        [[maybe_unused]] int64_t layer,
                                        [[maybe_unused]] buffer_handle_t stream) {

    if (!mDispatch.setLayerSidebandStream) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    return mDispatch.setLayerSidebandStream(mDevice, display, layer, stream);
}

int32_t HalImpl::setLayerSourceCrop(int64_t display, int64_t layer, const common::FRect& crop) {
    if (!mDispatch.setLayerSourceCrop) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    hwc_frect_t hwcCrop;
    a2h::translate(crop, hwcCrop);
    return mDispatch.setLayerSourceCrop(mDevice, display, layer, hwcCrop);
}

int32_t HalImpl::setLayerSurfaceDamage(int64_t display, int64_t layer,
                                  const std::vector<std::optional<common::Rect>>& damage) {
    if (!mDispatch.setLayerSurfaceDamage) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    std::vector<hwc_rect_t> hwcDamage;
    a2h::translate(damage, hwcDamage);
    hwc_region_t region = { hwcDamage.size(), hwcDamage.data() };
    return mDispatch.setLayerSurfaceDamage(mDevice, display, layer, region);
}

int32_t HalImpl::setLayerTransform(int64_t display, int64_t layer, common::Transform transform) {
    if (!mDispatch.setLayerTransform) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t hwcTransform;
    a2h::translate(transform, hwcTransform);
    return mDispatch.setLayerTransform(mDevice, display, layer, hwcTransform);
}

int32_t HalImpl::setLayerVisibleRegion(int64_t display, int64_t layer,
                               const std::vector<std::optional<common::Rect>>& visible) {
    if (!mDispatch.setLayerVisibleRegion) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    std::vector<hwc_rect_t> hwcVisible;
    a2h::translate(visible, hwcVisible);
    hwc_region_t region = { hwcVisible.size(), hwcVisible.data() };
    return mDispatch.setLayerVisibleRegion(mDevice, display, layer, region);
}

int32_t HalImpl::setLayerBrightness([[maybe_unused]] int64_t display, 
                                    [[maybe_unused]] int64_t layer, 
                                    [[maybe_unused]] float brightness) {
    if (std::isnan(brightness) || brightness > 1.0f || brightness < 0.0f ) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::setLayerZOrder(int64_t display, int64_t layer, uint32_t z) {
    if (!mDispatch.setLayerZOrder) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    return mDispatch.setLayerZOrder(mDevice, display, layer, z);
}

int32_t HalImpl::setOutputBuffer(int64_t display, buffer_handle_t buffer,
                                 const ndk::ScopedFileDescriptor& releaseFence) {
    if (!mDispatch.setOutputBuffer) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t hwcFence;
    a2h::translate(releaseFence, hwcFence);

    auto err = mDispatch.setOutputBuffer(mDevice, display, buffer, hwcFence);;
    if (err == HWC2_ERROR_NONE && hwcFence >= 0) {
        close(hwcFence);
    }
    return err;
}

int32_t HalImpl::setPowerMode(int64_t display, PowerMode mode) {
    if (mode == PowerMode::ON_SUSPEND || mode == PowerMode::DOZE_SUSPEND) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    if (!mDispatch.setPowerMode) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t hwcMode;
    a2h::translate(mode, hwcMode);
    return mDispatch.setPowerMode(mDevice, display, hwcMode);
}

int32_t HalImpl::setReadbackBuffer(int64_t display, buffer_handle_t buffer,
                                   const ndk::ScopedFileDescriptor& releaseFence) {
    if (!mDispatch.setReadbackBuffer) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    int32_t hwcFence;
    a2h::translate(releaseFence, hwcFence);

    return mDispatch.setReadbackBuffer(mDevice, display, buffer, hwcFence);
}

int32_t HalImpl::setVsyncEnabled(int64_t display, bool enabled) {
    if (!mDispatch.setVsyncEnabled) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    hwc2_vsync_t hwcEnable;
    a2h::translate(enabled, hwcEnable);
    return mDispatch.setVsyncEnabled(mDevice, display, static_cast<int32_t>(hwcEnable));
}

int32_t HalImpl::setIdleTimerEnabled([[maybe_unused]] int64_t display, 
                                     [[maybe_unused]] int32_t timeout) {
    return HWC2_ERROR_UNSUPPORTED;
}

int32_t HalImpl::validateDisplay(int64_t display, std::vector<int64_t>* outChangedLayers,
                                 std::vector<Composition>* outCompositionTypes,
                                 uint32_t* outDisplayRequestMask,
                                 std::vector<int64_t>* outRequestedLayers,
                                 std::vector<int32_t>* outRequestMasks,
                                 ClientTargetProperty* outClientTargetProperty,
                                 [[maybe_unused]] DimmingStage* outDimmingStage) {
    if (!mDispatch.validateDisplay) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    uint32_t typesCount = 0;
    uint32_t reqsCount = 0;
    auto err = mDispatch.validateDisplay(mDevice, display, &typesCount, &reqsCount);

    if (err != HWC2_ERROR_NONE && err != HWC2_ERROR_HAS_CHANGES) {
        return err;
    }
    RET_IF_ERR( mDispatch.getChangedCompositionTypes(mDevice, display, &typesCount, nullptr, nullptr));
    std::vector<hwc2_layer_t> hwcChangedLayers(typesCount);
    std::vector<int32_t> hwcCompositionTypes(typesCount);
    RET_IF_ERR( mDispatch.getChangedCompositionTypes(mDevice, display, &typesCount, hwcChangedLayers.data(),
                                                      hwcCompositionTypes.data()));

    int32_t displayReqs;
    RET_IF_ERR(mDispatch.getDisplayRequests(mDevice, display, &displayReqs, &reqsCount, nullptr,nullptr));

    std::vector<hwc2_layer_t> hwcRequestedLayers(reqsCount);
    outRequestMasks->resize(reqsCount);
    RET_IF_ERR(mDispatch.getDisplayRequests(mDevice, display, &displayReqs, &reqsCount,
                                              hwcRequestedLayers.data(), outRequestMasks->data()));

    h2a::translate(hwcChangedLayers, *outChangedLayers);
    h2a::translate(hwcCompositionTypes, *outCompositionTypes);
    *outDisplayRequestMask = displayReqs;
    h2a::translate(hwcRequestedLayers, *outRequestedLayers);

    if (mDispatch.getClientTargetProperty)
    {
        hwc_client_target_property hwcProperty;
        [[maybe_unused]]auto err = mDispatch.getClientTargetProperty(mDevice,display,&hwcProperty);
        h2a::translate(hwcProperty, *outClientTargetProperty); 
    }

    return HWC2_ERROR_NONE;
}

int HalImpl::setExpectedPresentTime([[maybe_unused]] int64_t display, 
                                    [[maybe_unused]] const std::optional<ClockMonotonicTimestamp> expectedPresentTime) {

    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getRCDLayerSupport([[maybe_unused]] int64_t display, 
                                    [[maybe_unused]]bool& outSupport) {
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::setLayerBlockingRegion([[maybe_unused]] int64_t display, 
                                        [[maybe_unused]] int64_t layer,
                                        [[maybe_unused]] const std::vector<std::optional<common::Rect>>& blockingRegion) {
    return HWC2_ERROR_NONE;
}

int32_t HalImpl::getDisplayIdleTimerSupport([[maybe_unused]] int64_t display, 
                                            [[maybe_unused]] bool& outSupport) {
    return HWC2_ERROR_NONE;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
