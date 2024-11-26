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

#pragma once

#include <memory>

#include "aidl/android/hardware/graphics/composer3/BnComposerClient.h"
#include "aidl/android/hardware/graphics/composer3/LayerCommand.h"
#include "hwc3/CommandResultWriter.h"
#include "hwc3/ComposerResources.h"
#include "hwc3/Utils.h"
#include "utils/Mutex.h"

using AidlPixelFormat = aidl::android::hardware::graphics::common::PixelFormat;
using AidlNativeHandle = aidl::android::hardware::common::NativeHandle;

namespace android {

class HwcDisplay;
class HwcLayer;

}  // namespace android

namespace aidl::android::hardware::graphics::composer3::impl {

class DrmHwcThree;

class ComposerClient : public BnComposerClient {
 public:
  ComposerClient();
  ~ComposerClient() override;

  bool Init();
  std::string Dump();

  // composer3 interface
  ndk::ScopedAStatus createLayer(int64_t display, int32_t buffer_slot_count,
                                 int64_t* layer) override;
  ndk::ScopedAStatus createVirtualDisplay(int32_t width, int32_t height,
                                          AidlPixelFormat format_hint,
                                          int32_t output_buffer_slot_count,
                                          VirtualDisplay* display) override;
  ndk::ScopedAStatus destroyLayer(int64_t display, int64_t layer) override;
  ndk::ScopedAStatus destroyVirtualDisplay(int64_t display) override;
  ndk::ScopedAStatus executeCommands(
      const std::vector<DisplayCommand>& commands,
      std::vector<CommandResultPayload>* results) override;
  ndk::ScopedAStatus getActiveConfig(int64_t display, int32_t* config) override;
  ndk::ScopedAStatus getColorModes(
      int64_t display, std::vector<ColorMode>* color_modes) override;
  ndk::ScopedAStatus getDataspaceSaturationMatrix(
      common::Dataspace dataspace, std::vector<float>* matrix) override;
  ndk::ScopedAStatus getDisplayAttribute(int64_t display, int32_t config,
                                         DisplayAttribute attribute,
                                         int32_t* value) override;
  ndk::ScopedAStatus getDisplayCapabilities(
      int64_t display, std::vector<DisplayCapability>* caps) override;
  ndk::ScopedAStatus getDisplayConfigs(int64_t display,
                                       std::vector<int32_t>* configs) override;
  ndk::ScopedAStatus getDisplayConnectionType(
      int64_t display, DisplayConnectionType* type) override;
  ndk::ScopedAStatus getDisplayIdentificationData(
      int64_t display, DisplayIdentification* id) override;
  ndk::ScopedAStatus getDisplayName(int64_t display,
                                    std::string* name) override;
  ndk::ScopedAStatus getDisplayVsyncPeriod(int64_t display,
                                           int32_t* vsync_period) override;
  ndk::ScopedAStatus getDisplayedContentSample(
      int64_t display, int64_t max_frames, int64_t timestamp,
      DisplayContentSample* samples) override;
  ndk::ScopedAStatus getDisplayedContentSamplingAttributes(
      int64_t display, DisplayContentSamplingAttributes* attrs) override;
  ndk::ScopedAStatus getDisplayPhysicalOrientation(
      int64_t display, common::Transform* orientation) override;
  ndk::ScopedAStatus getHdrCapabilities(int64_t display,
                                        HdrCapabilities* caps) override;
  ndk::ScopedAStatus getMaxVirtualDisplayCount(int32_t* count) override;
  ndk::ScopedAStatus getPerFrameMetadataKeys(
      int64_t display, std::vector<PerFrameMetadataKey>* keys) override;
  ndk::ScopedAStatus getReadbackBufferAttributes(
      int64_t display, ReadbackBufferAttributes* attrs) override;
  ndk::ScopedAStatus getReadbackBufferFence(
      int64_t display, ndk::ScopedFileDescriptor* acquire_fence) override;
  ndk::ScopedAStatus getRenderIntents(
      int64_t display, ColorMode mode,
      std::vector<RenderIntent>* intents) override;
  ndk::ScopedAStatus getSupportedContentTypes(
      int64_t display, std::vector<ContentType>* types) override;
  ndk::ScopedAStatus getDisplayDecorationSupport(
      int64_t display,
      std::optional<common::DisplayDecorationSupport>* support) override;
  ndk::ScopedAStatus registerCallback(
      const std::shared_ptr<IComposerCallback>& callback) override;
  ndk::ScopedAStatus setActiveConfig(int64_t display, int32_t config) override;
  ndk::ScopedAStatus setActiveConfigWithConstraints(
      int64_t display, int32_t config,
      const VsyncPeriodChangeConstraints& constraints,
      VsyncPeriodChangeTimeline* timeline) override;
  ndk::ScopedAStatus setBootDisplayConfig(int64_t display,
                                          int32_t config) override;
  ndk::ScopedAStatus clearBootDisplayConfig(int64_t display) override;
  ndk::ScopedAStatus getPreferredBootDisplayConfig(int64_t display,
                                                   int32_t* config) override;
  ndk::ScopedAStatus setAutoLowLatencyMode(int64_t display, bool on) override;
  ndk::ScopedAStatus setClientTargetSlotCount(int64_t display,
                                              int32_t count) override;
  ndk::ScopedAStatus setColorMode(int64_t display, ColorMode mode,
                                  RenderIntent intent) override;
  ndk::ScopedAStatus setContentType(int64_t display, ContentType type) override;
  ndk::ScopedAStatus setDisplayedContentSamplingEnabled(
      int64_t display, bool enable, FormatColorComponent component_mask,
      int64_t max_frames) override;
  ndk::ScopedAStatus setPowerMode(int64_t display, PowerMode mode) override;
  ndk::ScopedAStatus setReadbackBuffer(
      int64_t display, const AidlNativeHandle& buffer,
      const ndk::ScopedFileDescriptor& release_fence) override;
  ndk::ScopedAStatus setVsyncEnabled(int64_t display, bool enabled) override;
  ndk::ScopedAStatus setIdleTimerEnabled(int64_t display,
                                         int32_t timeout) override;

#if __ANDROID_API__ >= 34

  ndk::ScopedAStatus getOverlaySupport(
      OverlayProperties* out_overlay_properties) override;
  ndk::ScopedAStatus getHdrConversionCapabilities(
      std::vector<common::HdrConversionCapability>* out_capabilities) override;
  ndk::ScopedAStatus setHdrConversionStrategy(
      const common::HdrConversionStrategy& conversion_strategy,
      common::Hdr* out_hdr) override;
  ndk::ScopedAStatus setRefreshRateChangedCallbackDebugEnabled(
      int64_t display, bool enabled) override;

#endif

#if __ANDROID_API__ >= 35

  ndk::ScopedAStatus getDisplayConfigurations(
      int64_t display, int32_t max_frame_interval_ns,
      std::vector<DisplayConfiguration>* configurations) override;
  ndk::ScopedAStatus notifyExpectedPresent(
      int64_t display, const ClockMonotonicTimestamp& expected_present_time,
      int32_t frame_interval_ns) override;

#endif

 protected:
  ::ndk::SpAIBinder createBinder() override;

 private:
  hwc3::Error ImportLayerBuffer(int64_t display_id, int64_t layer_id,
                                const Buffer& buffer,
                                buffer_handle_t* out_imported_buffer);

  // Layer commands
  void DispatchLayerCommand(int64_t display_id, const LayerCommand& command);

  // Display commands
  void ExecuteDisplayCommand(const DisplayCommand& command);
  void ExecuteSetDisplayColorTransform(uint64_t display_id,
                                       const std::vector<float>& matrix);
  void ExecuteSetDisplayClientTarget(uint64_t display_id,
                                     const ClientTarget& command);
  void ExecuteSetDisplayOutputBuffer(uint64_t display_id, const Buffer& buffer);
  void ExecuteValidateDisplay(
      int64_t display_id,
      std::optional<ClockMonotonicTimestamp> expected_present_time);
  void ExecuteAcceptDisplayChanges(int64_t display_id);
  void ExecutePresentDisplay(int64_t display_id);
  void ExecutePresentOrValidateDisplay(
      int64_t display_id,
      std::optional<ClockMonotonicTimestamp> expected_present_time);

  static hwc3::Error ValidateDisplayInternal(
      ::android::HwcDisplay& display, std::vector<int64_t>* out_changed_layers,
      std::vector<Composition>* out_composition_types,
      int32_t* out_display_request_mask,
      std::vector<int64_t>* out_requested_layers,
      std::vector<int32_t>* out_request_masks,
      ClientTargetProperty* out_client_target_property,
      DimmingStage* out_dimming_stage);

  hwc3::Error PresentDisplayInternal(
      uint64_t display_id, ::android::base::unique_fd& out_display_fence,
      std::unordered_map<int64_t, ::android::base::unique_fd>&
          out_release_fences);

  ::android::HwcDisplay* GetDisplay(uint64_t display_id);

  std::unique_ptr<CommandResultWriter> cmd_result_writer_;

  // Manages importing and caching gralloc buffers for displays and layers.
  std::unique_ptr<ComposerResources> composer_resources_;

  std::unique_ptr<DrmHwcThree> hwc_;
};

}  // namespace aidl::android::hardware::graphics::composer3::impl
