/*
 * Copyright (C) 2021 The Android Open Source Project
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

#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include "ComposerClient.h"

#include <android-base/logging.h>
#include <android/binder_ibinder_platform.h>

#include "utils/log.h"

namespace aidl::android::hardware::graphics::composer3::impl {

// NOLINTNEXTLINE
#define DEBUG_FUNC() ALOGV("%s", __func__)

ComposerClient::~ComposerClient() {
  DEBUG_FUNC();

  LOG(DEBUG) << "removed composer client";
}

// no need to check nullptr for output parameter, the aidl stub code won't pass
// nullptr
ndk::ScopedAStatus ComposerClient::createLayer(int64_t /*display*/,
                                               int32_t /*bufferSlotCount*/,
                                               int64_t* /*layer*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::createVirtualDisplay(
    int32_t /*width*/, int32_t /*height*/, AidlPixelFormat /*formatHint*/,
    int32_t /*outputBufferSlotCount*/, VirtualDisplay* /*display*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::destroyLayer(int64_t /*display*/,
                                                int64_t /*layer*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::destroyVirtualDisplay(int64_t /*display*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::executeCommands(
    const std::vector<DisplayCommand>& /*commands*/,
    std::vector<CommandResultPayload>* /*results*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getActiveConfig(int64_t /*display*/,
                                                   int32_t* /*config*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getColorModes(
    int64_t /*display*/, std::vector<ColorMode>* /*colorModes*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDataspaceSaturationMatrix(
    common::Dataspace /*dataspace*/, std::vector<float>* /*matrix*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayAttribute(
    int64_t /*display*/, int32_t /*config*/, DisplayAttribute /*attribute*/,
    int32_t* /*value*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayCapabilities(
    int64_t /*display*/, std::vector<DisplayCapability>* /*caps*/) {
  DEBUG_FUNC();

  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayConfigs(
    int64_t /*display*/, std::vector<int32_t>* /*configs*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayConnectionType(
    int64_t /*display*/, DisplayConnectionType* /*type*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayIdentificationData(
    int64_t /*display*/, DisplayIdentification* /*id*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayName(int64_t /*display*/,
                                                  std::string* /*name*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayVsyncPeriod(
    int64_t /*display*/, int32_t* /*vsyncPeriod*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayedContentSample(
    int64_t /*display*/, int64_t /*maxFrames*/, int64_t /*timestamp*/,
    DisplayContentSample* /*samples*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayedContentSamplingAttributes(
    int64_t /*display*/, DisplayContentSamplingAttributes* /*attrs*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayPhysicalOrientation(
    int64_t /*display*/, common::Transform* /*orientation*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getHdrCapabilities(
    int64_t /*display*/, HdrCapabilities* /*caps*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getMaxVirtualDisplayCount(
    int32_t* /*count*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getPerFrameMetadataKeys(
    int64_t /*display*/, std::vector<PerFrameMetadataKey>* /*keys*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getReadbackBufferAttributes(
    int64_t /*display*/, ReadbackBufferAttributes* /*attrs*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getReadbackBufferFence(
    int64_t /*display*/, ndk::ScopedFileDescriptor* /*acquireFence*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getRenderIntents(
    int64_t /*display*/, ColorMode /*mode*/,
    std::vector<RenderIntent>* /*intents*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getSupportedContentTypes(
    int64_t /*display*/, std::vector<ContentType>* /*types*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayDecorationSupport(
    int64_t /*display*/,
    std::optional<common::DisplayDecorationSupport>* /*supportStruct*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::registerCallback(
    const std::shared_ptr<IComposerCallback>& /*callback*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setActiveConfig(int64_t /*display*/,
                                                   int32_t /*config*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setActiveConfigWithConstraints(
    int64_t /*display*/, int32_t /*config*/,
    const VsyncPeriodChangeConstraints& /*constraints*/,
    VsyncPeriodChangeTimeline* /*timeline*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setBootDisplayConfig(int64_t /*display*/,
                                                        int32_t /*config*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::clearBootDisplayConfig(int64_t /*display*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getPreferredBootDisplayConfig(
    int64_t /*display*/, int32_t* /*config*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setAutoLowLatencyMode(int64_t /*display*/,
                                                         bool /*on*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setClientTargetSlotCount(int64_t /*display*/,
                                                            int32_t /*count*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setColorMode(int64_t /*display*/,
                                                ColorMode /*mode*/,
                                                RenderIntent /*intent*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setContentType(int64_t /*display*/,
                                                  ContentType /*type*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setDisplayedContentSamplingEnabled(
    int64_t /*display*/, bool /*enable*/,
    FormatColorComponent /*componentMask*/, int64_t /*maxFrames*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setPowerMode(int64_t /*display*/,
                                                PowerMode /*mode*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setReadbackBuffer(
    int64_t /*display*/, const AidlNativeHandle& /*aidlBuffer*/,
    const ndk::ScopedFileDescriptor& /*releaseFence*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setVsyncEnabled(int64_t /*display*/,
                                                   bool /*enabled*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setIdleTimerEnabled(int64_t /*display*/,
                                                       int32_t /*timeout*/) {
  DEBUG_FUNC();
  return ndk::ScopedAStatus::ok();
}

::ndk::SpAIBinder ComposerClient::createBinder() {
  auto binder = BnComposerClient::createBinder();
  AIBinder_setInheritRt(binder.get(), true);
  return binder;
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
