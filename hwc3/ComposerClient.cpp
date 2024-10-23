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
#define ATRACE_TAG (ATRACE_TAG_GRAPHICS | ATRACE_TAG_HAL)

#include "ComposerClient.h"

#include <cinttypes>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

#include <aidl/android/hardware/graphics/common/Transform.h>
#include <aidl/android/hardware/graphics/composer3/ClientTarget.h>
#include <aidl/android/hardware/graphics/composer3/Composition.h>
#include <aidl/android/hardware/graphics/composer3/DisplayRequest.h>
#include <aidl/android/hardware/graphics/composer3/IComposerClient.h>
#include <aidl/android/hardware/graphics/composer3/PowerMode.h>
#include <aidl/android/hardware/graphics/composer3/PresentOrValidate.h>
#include <aidl/android/hardware/graphics/composer3/RenderIntent.h>
#include <aidlcommonsupport/NativeHandle.h>
#include <android-base/logging.h>
#include <android/binder_auto_utils.h>
#include <android/binder_ibinder_platform.h>
#include <cutils/native_handle.h>
#include <hardware/hwcomposer2.h>
#include <hardware/hwcomposer_defs.h>

#include "bufferinfo/BufferInfo.h"
#include "compositor/DisplayInfo.h"
#include "hwc2_device/HwcDisplay.h"
#include "hwc2_device/HwcDisplayConfigs.h"
#include "hwc2_device/HwcLayer.h"
#include "hwc3/DrmHwcThree.h"
#include "hwc3/Utils.h"

using ::android::HwcDisplay;
using ::android::HwcDisplayConfig;
using ::android::HwcDisplayConfigs;
using ::android::HwcLayer;
using ::android::LayerTransform;

#include "utils/log.h"

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

// clang-format off
constexpr std::array<float, 16> kIdentityMatrix = {
    1.0F, 0.0F, 0.0F, 0.0F,
    0.0F, 1.0F, 0.0F, 0.0F,
    0.0F, 0.0F, 1.0F, 0.0F,
    0.0F, 0.0F, 0.0F, 1.0F,
};
// clang-format on

std::optional<BufferBlendMode> AidlToBlendMode(
    const std::optional<ParcelableBlendMode>& aidl_blend_mode) {
  if (!aidl_blend_mode) {
    return std::nullopt;
  }

  switch (aidl_blend_mode->blendMode) {
    case common::BlendMode::NONE:
      return BufferBlendMode::kNone;
    case common::BlendMode::PREMULTIPLIED:
      return BufferBlendMode::kPreMult;
    case common::BlendMode::COVERAGE:
      return BufferBlendMode::kCoverage;
    case common::BlendMode::INVALID:
      ALOGE("Invalid BlendMode");
      return std::nullopt;
  }
}

std::optional<BufferColorSpace> AidlToColorSpace(
    const std::optional<ParcelableDataspace>& dataspace) {
  if (!dataspace) {
    return std::nullopt;
  }

  int32_t standard = static_cast<int32_t>(dataspace->dataspace) &
                     static_cast<int32_t>(common::Dataspace::STANDARD_MASK);
  switch (standard) {
    case static_cast<int32_t>(common::Dataspace::STANDARD_BT709):
      return BufferColorSpace::kItuRec709;
    case static_cast<int32_t>(common::Dataspace::STANDARD_BT601_625):
    case static_cast<int32_t>(common::Dataspace::STANDARD_BT601_625_UNADJUSTED):
    case static_cast<int32_t>(common::Dataspace::STANDARD_BT601_525):
    case static_cast<int32_t>(common::Dataspace::STANDARD_BT601_525_UNADJUSTED):
      return BufferColorSpace::kItuRec601;
    case static_cast<int32_t>(common::Dataspace::STANDARD_BT2020):
    case static_cast<int32_t>(
        common::Dataspace::STANDARD_BT2020_CONSTANT_LUMINANCE):
      return BufferColorSpace::kItuRec2020;
    default:
      ALOGE("Unsupported standard: %d", standard);
      return std::nullopt;
  }
}

std::optional<BufferSampleRange> AidlToSampleRange(
    const std::optional<ParcelableDataspace>& dataspace) {
  if (!dataspace) {
    return std::nullopt;
  }

  int32_t sample_range = static_cast<int32_t>(dataspace->dataspace) &
                         static_cast<int32_t>(common::Dataspace::RANGE_MASK);
  switch (sample_range) {
    case static_cast<int32_t>(common::Dataspace::RANGE_FULL):
      return BufferSampleRange::kFullRange;
    case static_cast<int32_t>(common::Dataspace::RANGE_LIMITED):
      return BufferSampleRange::kLimitedRange;
    default:
      ALOGE("Unsupported sample range: %d", sample_range);
      return std::nullopt;
  }
}

bool IsSupportedCompositionType(
    const std::optional<ParcelableComposition> composition) {
  if (!composition) {
    return true;
  }
  switch (composition->composition) {
    case Composition::INVALID:
    case Composition::CLIENT:
    case Composition::DEVICE:
    case Composition::SOLID_COLOR:
    case Composition::CURSOR:
      return true;

    // Unsupported composition types. Set an error for the current
    // DisplayCommand and return.
    case Composition::DISPLAY_DECORATION:
    case Composition::SIDEBAND:
    case Composition::REFRESH_RATE_INDICATOR:
      return false;
  }
}

bool ValidateLayerBrightness(const std::optional<LayerBrightness>& brightness) {
  if (!brightness) {
    return true;
  }
  return !(std::signbit(brightness->brightness) ||
           std::isnan(brightness->brightness));
}

std::optional<HWC2::Composition> AidlToCompositionType(
    const std::optional<ParcelableComposition> composition) {
  if (!composition) {
    return std::nullopt;
  }

  switch (composition->composition) {
    case Composition::INVALID:
      return HWC2::Composition::Invalid;
    case Composition::CLIENT:
      return HWC2::Composition::Client;
    case Composition::DEVICE:
      return HWC2::Composition::Device;
    case Composition::SOLID_COLOR:
      return HWC2::Composition::SolidColor;
    case Composition::CURSOR:
      return HWC2::Composition::Cursor;

    // Unsupported composition types.
    case Composition::DISPLAY_DECORATION:
    case Composition::SIDEBAND:
    case Composition::REFRESH_RATE_INDICATOR:
      ALOGE("Unsupported composition type: %s",
            toString(composition->composition).c_str());
      return std::nullopt;
  }
}

#if __ANDROID_API__ < 35

class DisplayConfiguration {
 public:
  class Dpi {
   public:
    float x = 0.000000F;
    float y = 0.000000F;
  };
  // NOLINTNEXTLINE(readability-identifier-naming)
  int32_t configId = 0;
  int32_t width = 0;
  int32_t height = 0;
  std::optional<Dpi> dpi;
  // NOLINTNEXTLINE(readability-identifier-naming)
  int32_t configGroup = 0;
  // NOLINTNEXTLINE(readability-identifier-naming)
  int32_t vsyncPeriod = 0;
};

#endif

DisplayConfiguration HwcDisplayConfigToAidlConfiguration(
    const HwcDisplayConfigs& configs, const HwcDisplayConfig& config) {
  DisplayConfiguration aidl_configuration =
      {.configId = static_cast<int32_t>(config.id),
       .width = config.mode.GetRawMode().hdisplay,
       .height = config.mode.GetRawMode().vdisplay,
       .configGroup = static_cast<int32_t>(config.group_id),
       .vsyncPeriod = config.mode.GetVSyncPeriodNs()};

  if (configs.mm_width != 0) {
    // ideally this should be vdisplay/mm_heigth, however mm_height
    // comes from edid parsing and is highly unreliable. Viewing the
    // rarity of anisotropic displays, falling back to a single value
    // for dpi yield more correct output.
    static const float kMmPerInch = 25.4;
    float dpi = float(config.mode.GetRawMode().hdisplay) * kMmPerInch /
                float(configs.mm_width);
    aidl_configuration.dpi = {.x = dpi, .y = dpi};
  }
  // TODO: Populate vrrConfig.
  return aidl_configuration;
}

std::optional<hwc_rect> AidlToRect(const std::optional<common::Rect>& rect) {
  if (!rect) {
    return std::nullopt;
  }
  return hwc_rect{rect->left, rect->top, rect->right, rect->bottom};
}

std::optional<hwc_frect> AidlToFRect(const std::optional<common::FRect>& rect) {
  if (!rect) {
    return std::nullopt;
  }
  return hwc_frect{rect->left, rect->top, rect->right, rect->bottom};
}

std::optional<float> AidlToAlpha(const std::optional<PlaneAlpha>& alpha) {
  if (!alpha) {
    return std::nullopt;
  }
  return alpha->alpha;
}

std::optional<uint32_t> AidlToZOrder(const std::optional<ZOrder>& z_order) {
  if (!z_order) {
    return std::nullopt;
  }
  return z_order->z;
}

std::optional<LayerTransform> AidlToLayerTransform(
    const std::optional<ParcelableTransform>& aidl_transform) {
  if (!aidl_transform) {
    return std::nullopt;
  }

  uint32_t transform = LayerTransform::kIdentity;
  // 270* and 180* cannot be combined with flips. More specifically, they
  // already contain both horizontal and vertical flips, so those fields are
  // redundant in this case. 90* rotation can be combined with either horizontal
  // flip or vertical flip, so treat it differently
  if (aidl_transform->transform == common::Transform::ROT_270) {
    transform = LayerTransform::kRotate270;
  } else if (aidl_transform->transform == common::Transform::ROT_180) {
    transform = LayerTransform::kRotate180;
  } else {
    auto aidl_transform_bits = static_cast<uint32_t>(aidl_transform->transform);
    if ((aidl_transform_bits &
         static_cast<uint32_t>(common::Transform::FLIP_H)) != 0)
      transform |= LayerTransform::kFlipH;
    if ((aidl_transform_bits &
         static_cast<uint32_t>(common::Transform::FLIP_V)) != 0)
      transform |= LayerTransform::kFlipV;
    if ((aidl_transform_bits &
         static_cast<uint32_t>(common::Transform::ROT_90)) != 0)
      transform |= LayerTransform::kRotate90;
  }
  return static_cast<LayerTransform>(transform);
}

}  // namespace

ComposerClient::ComposerClient() {
  DEBUG_FUNC();
}

bool ComposerClient::Init() {
  DEBUG_FUNC();
  composer_resources_ = ComposerResources::Create();
  if (composer_resources_) {
    hwc_ = std::make_unique<DrmHwcThree>(composer_resources_.get());
  }
  return composer_resources_ != nullptr;
}

ComposerClient::~ComposerClient() {
  DEBUG_FUNC();
  {
    // First Deinit the displays to start shutting down the Display's dependent
    // threads such as VSyncWorker.
    const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
    hwc_->DeinitDisplays();
  }
  // Sleep to wait for threads to complete and exit.
  const int time_for_threads_to_exit_us = 200000;
  usleep(time_for_threads_to_exit_us);
  {
    // Hold the lock while destructing the hwc_ and the objects that it owns.
    const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
    hwc_.reset();
  }
  LOG(DEBUG) << "removed composer client";
}

ndk::ScopedAStatus ComposerClient::createLayer(int64_t display_id,
                                               int32_t buffer_slot_count,
                                               int64_t* layer_id) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());

  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  hwc2_layer_t hwc2_layer_id = 0;
  auto err = Hwc2toHwc3Error(display->CreateLayer(&hwc2_layer_id));
  if (err != hwc3::Error::kNone) {
    return ToBinderStatus(err);
  }

  const int64_t created_layer_id = Hwc2LayerToHwc3(hwc2_layer_id);
  err = composer_resources_->AddLayer(display_id, created_layer_id,
                                      buffer_slot_count);
  if (err != hwc3::Error::kNone) {
    destroyLayer(display_id, created_layer_id);
    return ToBinderStatus(err);
  }

  *layer_id = created_layer_id;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::createVirtualDisplay(
    int32_t width, int32_t height, AidlPixelFormat format_hint,
    int32_t output_buffer_slot_count, VirtualDisplay* out_display) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());

  hwc2_display_t hwc2_display_id = 0;
  // TODO: Format is currently not used in drm_hwcomposer.
  int32_t hwc2_format = 0;
  auto err = Hwc2toHwc3Error(hwc_->CreateVirtualDisplay(width, height,
                                                        &hwc2_format,
                                                        &hwc2_display_id));
  if (err != hwc3::Error::kNone) {
    return ToBinderStatus(err);
  }

  const int64_t created_display_id = Hwc2DisplayToHwc3(hwc2_display_id);
  err = composer_resources_->AddVirtualDisplay(hwc2_display_id,
                                               output_buffer_slot_count);
  if (err != hwc3::Error::kNone) {
    hwc_->DestroyVirtualDisplay(hwc2_display_id);
    return ToBinderStatus(err);
  }

  out_display->display = created_display_id;
  out_display->format = format_hint;
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::destroyLayer(int64_t display_id,
                                                int64_t layer_id) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  auto err = Hwc2toHwc3Error(display->DestroyLayer(Hwc3LayerToHwc2(layer_id)));
  if (err != hwc3::Error::kNone) {
    return ToBinderStatus(err);
  }

  err = composer_resources_->RemoveLayer(display_id, layer_id);
  return ToBinderStatus(err);
}

ndk::ScopedAStatus ComposerClient::destroyVirtualDisplay(int64_t display_id) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  auto err = Hwc2toHwc3Error(hwc_->DestroyVirtualDisplay(display_id));
  return ToBinderStatus(err);
}

hwc3::Error ComposerClient::ValidateDisplayInternal(
    HwcDisplay& display, std::vector<int64_t>* out_changed_layers,
    std::vector<Composition>* out_composition_types,
    int32_t* out_display_request_mask,
    std::vector<int64_t>* out_requested_layers,
    std::vector<int32_t>* out_request_masks,
    ClientTargetProperty* /*out_client_target_property*/,
    DimmingStage* /*out_dimming_stage*/) {
  DEBUG_FUNC();

  uint32_t num_types = 0;
  uint32_t num_requests = 0;
  const HWC2::Error hwc2_error = display.ValidateDisplay(&num_types,
                                                         &num_requests);

  /* Check if display has pending changes and no errors */
  if (hwc2_error != HWC2::Error::None &&
      hwc2_error != HWC2::Error::HasChanges) {
    return Hwc2toHwc3Error(hwc2_error);
  }

  hwc3::Error error = Hwc2toHwc3Error(
      display.GetChangedCompositionTypes(&num_types, nullptr, nullptr));
  if (error != hwc3::Error::kNone) {
    return error;
  }

  std::vector<hwc2_layer_t> hwc_changed_layers(num_types);
  std::vector<int32_t> hwc_composition_types(num_types);
  error = Hwc2toHwc3Error(
      display.GetChangedCompositionTypes(&num_types, hwc_changed_layers.data(),
                                         hwc_composition_types.data()));
  if (error != hwc3::Error::kNone) {
    return error;
  }

  int32_t display_reqs = 0;
  out_request_masks->resize(num_requests);
  std::vector<hwc2_layer_t> hwc_requested_layers(num_requests);
  error = Hwc2toHwc3Error(
      display.GetDisplayRequests(&display_reqs, &num_requests,
                                 hwc_requested_layers.data(),
                                 out_request_masks->data()));
  if (error != hwc3::Error::kNone) {
    return error;
  }

  for (const auto& layer : hwc_changed_layers) {
    out_changed_layers->emplace_back(Hwc2LayerToHwc3(layer));
  }
  for (const auto& type : hwc_composition_types) {
    out_composition_types->emplace_back(Hwc2CompositionTypeToHwc3(type));
  }
  for (const auto& layer : hwc_requested_layers) {
    out_requested_layers->emplace_back(Hwc2LayerToHwc3(layer));
  }
  *out_display_request_mask = display_reqs;

  /* Client target property/dimming stage unsupported */
  return hwc3::Error::kNone;
}

hwc3::Error ComposerClient::PresentDisplayInternal(
    uint64_t display_id, ::android::base::unique_fd& out_display_fence,
    std::unordered_map<int64_t, ::android::base::unique_fd>&
        out_release_fences) {
  DEBUG_FUNC();
  auto* display = GetDisplay(display_id);
  if (display == nullptr) {
    return hwc3::Error::kBadDisplay;
  }

  if (composer_resources_->MustValidateDisplay(display_id)) {
    return hwc3::Error::kNotValidated;
  }

  int32_t present_fence = -1;
  auto error = Hwc2toHwc3Error(display->PresentDisplay(&present_fence));
  if (error != hwc3::Error::kNone) {
    return error;
  }
  out_display_fence.reset(present_fence);

  uint32_t release_fence_count = 0;
  error = Hwc2toHwc3Error(
      display->GetReleaseFences(&release_fence_count, nullptr, nullptr));
  if (error != hwc3::Error::kNone) {
    return error;
  }

  std::vector<hwc2_layer_t> hwc_layers(release_fence_count);
  std::vector<int32_t> hwc_fences(release_fence_count);
  error = Hwc2toHwc3Error(display->GetReleaseFences(&release_fence_count,
                                                    hwc_layers.data(),
                                                    hwc_fences.data()));
  if (error != hwc3::Error::kNone) {
    return error;
  }

  for (size_t i = 0; i < hwc_layers.size(); i++) {
    auto layer = Hwc2LayerToHwc3(hwc_layers[i]);
    out_release_fences[layer] = ::android::base::unique_fd{hwc_fences[i]};
  }

  return hwc3::Error::kNone;
}

::android::HwcDisplay* ComposerClient::GetDisplay(uint64_t display_id) {
  return hwc_->GetDisplay(display_id);
}

void ComposerClient::DispatchLayerCommand(int64_t display_id,
                                          const LayerCommand& command) {
  auto* display = GetDisplay(display_id);
  if (display == nullptr) {
    cmd_result_writer_->AddError(hwc3::Error::kBadDisplay);
    return;
  }

  auto* layer = display->get_layer(command.layer);
  if (layer == nullptr) {
    cmd_result_writer_->AddError(hwc3::Error::kBadLayer);
    return;
  }

  // If the requested composition type is not supported, the HWC should return
  // an error and not process any further commands.
  if (!IsSupportedCompositionType(command.composition)) {
    cmd_result_writer_->AddError(hwc3::Error::kUnsupported);
    return;
  }

  // For some invalid parameters, the HWC should return an error and not process
  // any further commands.
  if (!ValidateLayerBrightness(command.brightness)) {
    cmd_result_writer_->AddError(hwc3::Error::kBadParameter);
    return;
  }

  HwcLayer::LayerProperties properties;
  if (command.buffer) {
    HwcLayer::Buffer buffer;
    auto err = ImportLayerBuffer(display_id, command.layer, *command.buffer,
                                 &buffer.buffer_handle);
    if (err != hwc3::Error::kNone) {
      cmd_result_writer_->AddError(err);
      return;
    }
    buffer.acquire_fence = ::android::MakeSharedFd(
        command.buffer->fence.dup().release());
    properties.buffer.emplace(buffer);
  }

  properties.blend_mode = AidlToBlendMode(command.blendMode);
  properties.color_space = AidlToColorSpace(command.dataspace);
  properties.sample_range = AidlToSampleRange(command.dataspace);
  properties.composition_type = AidlToCompositionType(command.composition);
  properties.display_frame = AidlToRect(command.displayFrame);
  properties.alpha = AidlToAlpha(command.planeAlpha);
  properties.source_crop = AidlToFRect(command.sourceCrop);
  properties.transform = AidlToLayerTransform(command.transform);
  properties.z_order = AidlToZOrder(command.z);

  layer->SetLayerProperties(properties);

  // Some unsupported functionality returns kUnsupported, and others
  // are just a no-op.
  // TODO: Audit whether some of these should actually return kUnsupported
  // instead.
  if (command.sidebandStream) {
    cmd_result_writer_->AddError(hwc3::Error::kUnsupported);
  }
  // TODO: Blocking region handling missing.
  // TODO: Layer surface damage.
  // TODO: Layer visible region.
  // TODO: Per-frame metadata.
  // TODO: Layer color transform.
  // TODO: Layer cursor position.
  // TODO: Layer color.
}

void ComposerClient::ExecuteDisplayCommand(const DisplayCommand& command) {
  const int64_t display_id = command.display;
  if (hwc_->GetDisplay(display_id) == nullptr) {
    cmd_result_writer_->AddError(hwc3::Error::kBadDisplay);
    return;
  }

  for (const auto& layer_cmd : command.layers) {
    DispatchLayerCommand(command.display, layer_cmd);
  }

  // TODO: Implement support for display brightness.
  if (command.colorTransformMatrix) {
    ExecuteSetDisplayColorTransform(command.display,
                                    *command.colorTransformMatrix);
  }
  if (command.clientTarget) {
    ExecuteSetDisplayClientTarget(command.display, *command.clientTarget);
  }
  if (command.virtualDisplayOutputBuffer) {
    ExecuteSetDisplayOutputBuffer(command.display,
                                  *command.virtualDisplayOutputBuffer);
  }
  if (command.validateDisplay) {
    ExecuteValidateDisplay(command.display, command.expectedPresentTime);
  }
  if (command.acceptDisplayChanges) {
    ExecuteAcceptDisplayChanges(command.display);
  }
  if (command.presentDisplay) {
    ExecutePresentDisplay(command.display);
  }
  if (command.presentOrValidateDisplay) {
    ExecutePresentOrValidateDisplay(command.display,
                                    command.expectedPresentTime);
  }
}

ndk::ScopedAStatus ComposerClient::executeCommands(
    const std::vector<DisplayCommand>& commands,
    std::vector<CommandResultPayload>* results) {
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  DEBUG_FUNC();
  cmd_result_writer_ = std::make_unique<CommandResultWriter>(results);
  for (const auto& cmd : commands) {
    ExecuteDisplayCommand(cmd);
    cmd_result_writer_->IncrementCommand();
  }
  cmd_result_writer_.reset();

  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getActiveConfig(int64_t display_id,
                                                   int32_t* config) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  uint32_t hwc2_config = 0;
  const hwc3::Error error = Hwc2toHwc3Error(
      display->GetActiveConfig(&hwc2_config));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }
  *config = Hwc2ConfigIdToHwc3(hwc2_config);
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getColorModes(
    int64_t display_id, std::vector<ColorMode>* color_modes) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  uint32_t num_modes = 0;
  auto error = Hwc2toHwc3Error(display->GetColorModes(&num_modes, nullptr));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }

  std::vector<int32_t> hwc2_color_modes(num_modes);
  error = Hwc2toHwc3Error(
      display->GetColorModes(&num_modes, hwc2_color_modes.data()));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }

  for (const auto& mode : hwc2_color_modes) {
    color_modes->push_back(Hwc2ColorModeToHwc3(mode));
  }

  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDataspaceSaturationMatrix(
    common::Dataspace dataspace, std::vector<float>* matrix) {
  DEBUG_FUNC();
  if (dataspace != common::Dataspace::SRGB_LINEAR) {
    return ToBinderStatus(hwc3::Error::kBadParameter);
  }

  matrix->clear();
  matrix->insert(matrix->begin(), kIdentityMatrix.begin(),
                 kIdentityMatrix.end());

  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayAttribute(
    int64_t display_id, int32_t config_id, DisplayAttribute attribute,
    int32_t* value) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  const HwcDisplayConfigs& configs = display->GetDisplayConfigs();
  auto config = configs.hwc_configs.find(config_id);
  if (config == configs.hwc_configs.end()) {
    return ToBinderStatus(hwc3::Error::kBadConfig);
  }

  DisplayConfiguration
      aidl_configuration = HwcDisplayConfigToAidlConfiguration(configs,
                                                               config->second);
  // Legacy API for querying DPI uses units of dots per 1000 inches.
  static const int kLegacyDpiUnit = 1000;
  switch (attribute) {
    case DisplayAttribute::WIDTH:
      *value = aidl_configuration.width;
      break;
    case DisplayAttribute::HEIGHT:
      *value = aidl_configuration.height;
      break;
    case DisplayAttribute::VSYNC_PERIOD:
      *value = aidl_configuration.vsyncPeriod;
      break;
    case DisplayAttribute::DPI_X:
      *value = aidl_configuration.dpi
                   ? static_cast<int>(aidl_configuration.dpi->x *
                                      kLegacyDpiUnit)
                   : -1;
      break;
    case DisplayAttribute::DPI_Y:
      *value = aidl_configuration.dpi
                   ? static_cast<int>(aidl_configuration.dpi->y *
                                      kLegacyDpiUnit)
                   : -1;
      break;
    case DisplayAttribute::CONFIG_GROUP:
      *value = aidl_configuration.configGroup;
      break;
    case DisplayAttribute::INVALID:
      return ToBinderStatus(hwc3::Error::kUnsupported);
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayCapabilities(
    int64_t display_id, std::vector<DisplayCapability>* caps) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  uint32_t num_capabilities = 0;
  hwc3::Error error = Hwc2toHwc3Error(
      display->GetDisplayCapabilities(&num_capabilities, nullptr));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }

  std::vector<uint32_t> out_caps(num_capabilities);
  error = Hwc2toHwc3Error(
      display->GetDisplayCapabilities(&num_capabilities, out_caps.data()));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }

  caps->reserve(num_capabilities);
  for (const auto cap : out_caps) {
    caps->emplace_back(Hwc2DisplayCapabilityToHwc3(cap));
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayConfigs(
    int64_t display_id, std::vector<int32_t>* out_configs) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  const HwcDisplayConfigs& configs = display->GetDisplayConfigs();
  for (const auto& [id, config] : configs.hwc_configs) {
    out_configs->push_back(static_cast<int32_t>(id));
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayConnectionType(
    int64_t display_id, DisplayConnectionType* type) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  uint32_t out_type = 0;
  const hwc3::Error error = Hwc2toHwc3Error(
      display->GetDisplayConnectionType(&out_type));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }

  *type = Hwc2DisplayConnectionTypeToHwc3(out_type);
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayIdentificationData(
    int64_t display_id, DisplayIdentification* id) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  uint8_t port = 0;
  uint32_t data_size = 0;
  hwc3::Error error = Hwc2toHwc3Error(
      display->GetDisplayIdentificationData(&port, &data_size, nullptr));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }

  id->data.resize(data_size);
  error = Hwc2toHwc3Error(
      display->GetDisplayIdentificationData(&port, &data_size,
                                            id->data.data()));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }

  id->port = static_cast<int8_t>(port);
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayName(int64_t display_id,
                                                  std::string* name) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  uint32_t size = 0;
  auto error = Hwc2toHwc3Error(display->GetDisplayName(&size, nullptr));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }

  name->resize(size);
  error = Hwc2toHwc3Error(display->GetDisplayName(&size, name->data()));
  return ToBinderStatus(error);
}

ndk::ScopedAStatus ComposerClient::getDisplayVsyncPeriod(
    int64_t display_id, int32_t* vsync_period) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  // getDisplayVsyncPeriod should return the vsync period of the config that
  // is currently committed to the kernel. If a config change is pending due to
  // setActiveConfigWithConstraints, return the pre-change vsync period.
  const HwcDisplayConfig* config = display->GetCurrentConfig();
  if (config == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadConfig);
  }

  *vsync_period = config->mode.GetVSyncPeriodNs();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayedContentSample(
    int64_t /*display_id*/, int64_t /*max_frames*/, int64_t /*timestamp*/,
    DisplayContentSample* /*samples*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::getDisplayedContentSamplingAttributes(
    int64_t /*display_id*/, DisplayContentSamplingAttributes* /*attrs*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::getDisplayPhysicalOrientation(
    int64_t display_id, common::Transform* orientation) {
  DEBUG_FUNC();

  if (orientation == nullptr) {
    ALOGE("Invalid 'orientation' pointer.");
    return ToBinderStatus(hwc3::Error::kBadParameter);
  }

  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  PanelOrientation
      drm_orientation = display->getDisplayPhysicalOrientation().value_or(
          PanelOrientation::kModePanelOrientationNormal);

  switch (drm_orientation) {
    case PanelOrientation::kModePanelOrientationNormal:
      *orientation = common::Transform::NONE;
      break;
    case PanelOrientation::kModePanelOrientationBottomUp:
      *orientation = common::Transform::ROT_180;
      break;
    case PanelOrientation::kModePanelOrientationLeftUp:
      *orientation = common::Transform::ROT_270;
      break;
    case PanelOrientation::kModePanelOrientationRightUp:
      *orientation = common::Transform::ROT_90;
      break;
    default:
      ALOGE("Unknown panel orientation value: %d", drm_orientation);
      return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getHdrCapabilities(int64_t display_id,
                                                      HdrCapabilities* caps) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  /* No HDR capabilities */
  caps->types.clear();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getMaxVirtualDisplayCount(int32_t* count) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  *count = static_cast<int32_t>(hwc_->GetMaxVirtualDisplayCount());
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getPerFrameMetadataKeys(
    int64_t /*display_id*/, std::vector<PerFrameMetadataKey>* /*keys*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::getReadbackBufferAttributes(
    int64_t /*display_id*/, ReadbackBufferAttributes* /*attrs*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::getReadbackBufferFence(
    int64_t /*display_id*/, ndk::ScopedFileDescriptor* /*acquireFence*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::getRenderIntents(
    int64_t display_id, ColorMode mode, std::vector<RenderIntent>* intents) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  const int32_t hwc2_color_mode = Hwc3ColorModeToHwc2(mode);
  uint32_t out_num_intents = 0;
  auto error = Hwc2toHwc3Error(
      display->GetRenderIntents(hwc2_color_mode, &out_num_intents, nullptr));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }

  std::vector<int32_t> out_intents(out_num_intents);
  error = Hwc2toHwc3Error(display->GetRenderIntents(hwc2_color_mode,
                                                    &out_num_intents,
                                                    out_intents.data()));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }

  intents->reserve(out_num_intents);
  for (const auto intent : out_intents) {
    intents->emplace_back(Hwc2RenderIntentToHwc3(intent));
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getSupportedContentTypes(
    int64_t display_id, std::vector<ContentType>* types) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  // Support for ContentType is not implemented.
  types->clear();
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::getDisplayDecorationSupport(
    int64_t /*display_id*/,
    std::optional<common::DisplayDecorationSupport>* /*support_struct*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::registerCallback(
    const std::shared_ptr<IComposerCallback>& callback) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  // This function is specified to be called exactly once.
  hwc_->Init(callback);
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setActiveConfig(int64_t display_id,
                                                   int32_t config) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  return ToBinderStatus(Hwc2toHwc3Error(display->SetActiveConfig(config)));
}

ndk::ScopedAStatus ComposerClient::setActiveConfigWithConstraints(
    int64_t display_id, int32_t config,
    const VsyncPeriodChangeConstraints& constraints,
    VsyncPeriodChangeTimeline* timeline) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  hwc_vsync_period_change_constraints_t hwc2_constraints;
  hwc2_constraints.desiredTimeNanos = constraints.desiredTimeNanos;
  hwc2_constraints.seamlessRequired = static_cast<uint8_t>(
      constraints.seamlessRequired);

  hwc_vsync_period_change_timeline_t hwc2_timeline{};
  auto error = Hwc2toHwc3Error(
      display->SetActiveConfigWithConstraints(config, &hwc2_constraints,
                                              &hwc2_timeline));
  if (error != hwc3::Error::kNone) {
    return ToBinderStatus(error);
  }

  timeline->refreshTimeNanos = hwc2_timeline.refreshTimeNanos;
  timeline->newVsyncAppliedTimeNanos = hwc2_timeline.newVsyncAppliedTimeNanos;
  timeline->refreshRequired = static_cast<bool>(hwc2_timeline.refreshRequired);
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setBootDisplayConfig(int64_t /*display_id*/,
                                                        int32_t /*config*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::clearBootDisplayConfig(
    int64_t /*display_id*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::getPreferredBootDisplayConfig(
    int64_t /*display_id*/, int32_t* /*config*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::setAutoLowLatencyMode(int64_t display_id,
                                                         bool on) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  auto error = Hwc2toHwc3Error(display->SetAutoLowLatencyMode(on));
  return ToBinderStatus(error);
}

ndk::ScopedAStatus ComposerClient::setClientTargetSlotCount(int64_t display_id,
                                                            int32_t count) {
  DEBUG_FUNC();
  return ToBinderStatus(
      composer_resources_->SetDisplayClientTargetCacheSize(display_id, count));
}

ndk::ScopedAStatus ComposerClient::setColorMode(int64_t display_id,
                                                ColorMode mode,
                                                RenderIntent intent) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  auto error = display->SetColorModeWithIntent(Hwc3ColorModeToHwc2(mode),
                                               Hwc3RenderIntentToHwc2(intent));
  return ToBinderStatus(Hwc2toHwc3Error(error));
}

ndk::ScopedAStatus ComposerClient::setContentType(int64_t display_id,
                                                  ContentType type) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  if (type == ContentType::NONE) {
    return ndk::ScopedAStatus::ok();
  }
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::setDisplayedContentSamplingEnabled(
    int64_t /*display_id*/, bool /*enable*/,
    FormatColorComponent /*componentMask*/, int64_t /*maxFrames*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::setPowerMode(int64_t display_id,
                                                PowerMode mode) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  if (mode == PowerMode::ON_SUSPEND) {
    return ToBinderStatus(hwc3::Error::kUnsupported);
  }

  auto error = display->SetPowerMode(Hwc3PowerModeToHwc2(mode));
  return ToBinderStatus(Hwc2toHwc3Error(error));
}

ndk::ScopedAStatus ComposerClient::setReadbackBuffer(
    int64_t /*display_id*/, const AidlNativeHandle& /*aidlBuffer*/,
    const ndk::ScopedFileDescriptor& /*releaseFence*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::setVsyncEnabled(int64_t display_id,
                                                   bool enabled) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  auto error = display->SetVsyncEnabled(static_cast<int32_t>(enabled));
  return ToBinderStatus(Hwc2toHwc3Error(error));
}

ndk::ScopedAStatus ComposerClient::setIdleTimerEnabled(int64_t /*display_id*/,
                                                       int32_t /*timeout*/) {
  DEBUG_FUNC();
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::getOverlaySupport(
    OverlayProperties* /*out_overlay_properties*/) {
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::getHdrConversionCapabilities(
    std::vector<common::HdrConversionCapability>* /*out_capabilities*/) {
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::setHdrConversionStrategy(
    const common::HdrConversionStrategy& /*conversion_strategy*/,
    common::Hdr* /*out_hdr*/) {
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

ndk::ScopedAStatus ComposerClient::setRefreshRateChangedCallbackDebugEnabled(
    int64_t /*display*/, bool /*enabled*/) {
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

#if __ANDROID_API__ >= 35

ndk::ScopedAStatus ComposerClient::getDisplayConfigurations(
    int64_t display_id, int32_t /*max_frame_interval_ns*/,
    std::vector<DisplayConfiguration>* configurations) {
  DEBUG_FUNC();
  const std::unique_lock lock(hwc_->GetResMan().GetMainLock());
  HwcDisplay* display = GetDisplay(display_id);
  if (display == nullptr) {
    return ToBinderStatus(hwc3::Error::kBadDisplay);
  }

  const HwcDisplayConfigs& configs = display->GetDisplayConfigs();
  for (const auto& [id, config] : configs.hwc_configs) {
    configurations->push_back(
        HwcDisplayConfigToAidlConfiguration(configs, config));
  }
  return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::notifyExpectedPresent(
    int64_t /*display*/, const ClockMonotonicTimestamp& /*expected_present_time*/,
    int32_t /*frame_interval_ns*/) {
  return ToBinderStatus(hwc3::Error::kUnsupported);
}

#endif

std::string ComposerClient::Dump() {
  uint32_t size = 0;
  hwc_->Dump(&size, nullptr);

  std::string buffer(size, '\0');
  hwc_->Dump(&size, &buffer.front());
  return buffer;
}

::ndk::SpAIBinder ComposerClient::createBinder() {
  auto binder = BnComposerClient::createBinder();
  AIBinder_setInheritRt(binder.get(), true);
  return binder;
}

hwc3::Error ComposerClient::ImportLayerBuffer(
    int64_t display_id, int64_t layer_id, const Buffer& buffer,
    buffer_handle_t* out_imported_buffer) {
  *out_imported_buffer = nullptr;

  auto releaser = composer_resources_->CreateResourceReleaser(true);
  auto err = composer_resources_->GetLayerBuffer(display_id, layer_id, buffer,
                                                 out_imported_buffer,
                                                 releaser.get());
  return err;
}

void ComposerClient::ExecuteSetDisplayColorTransform(
    uint64_t display_id, const std::vector<float>& matrix) {
  auto* display = GetDisplay(display_id);
  if (display == nullptr) {
    cmd_result_writer_->AddError(hwc3::Error::kBadDisplay);
    return;
  }

  auto almost_equal = [](auto a, auto b) {
    const float epsilon = 0.001F;
    return std::abs(a - b) < epsilon;
  };
  const bool is_identity = std::equal(matrix.begin(), matrix.end(),
                                      kIdentityMatrix.begin(), almost_equal);

  const int32_t hint = is_identity ? HAL_COLOR_TRANSFORM_IDENTITY
                                   : HAL_COLOR_TRANSFORM_ARBITRARY_MATRIX;

  auto error = Hwc2toHwc3Error(display->SetColorTransform(matrix.data(), hint));
  if (error != hwc3::Error::kNone) {
    cmd_result_writer_->AddError(error);
  }
}
void ComposerClient::ExecuteSetDisplayClientTarget(
    uint64_t display_id, const ClientTarget& command) {
  auto* display = GetDisplay(display_id);
  if (display == nullptr) {
    cmd_result_writer_->AddError(hwc3::Error::kBadDisplay);
    return;
  }

  hwc_region_t damage_regions;
  damage_regions.numRects = command.damage.size();

  std::vector<hwc_rect_t> regions(command.damage.size());
  for (const auto& region : command.damage) {
    regions.push_back({region.left, region.top, region.right, region.bottom});
  }
  damage_regions.rects = regions.data();

  buffer_handle_t imported_buffer = nullptr;
  auto buf_releaser = composer_resources_->CreateResourceReleaser(true);

  auto error = composer_resources_->GetDisplayClientTarget(display_id,
                                                           command.buffer,
                                                           &imported_buffer,
                                                           buf_releaser.get());
  if (error != hwc3::Error::kNone) {
    cmd_result_writer_->AddError(error);
    return;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  auto fence = const_cast<::ndk::ScopedFileDescriptor&>(command.buffer.fence)
                   .release();
  error = Hwc2toHwc3Error(
      display->SetClientTarget(imported_buffer, fence,
                               Hwc3DataspaceToHwc2(command.dataspace),
                               damage_regions));
  if (error != hwc3::Error::kNone) {
    cmd_result_writer_->AddError(error);
  }
}

void ComposerClient::ExecuteSetDisplayOutputBuffer(uint64_t display_id,
                                                   const Buffer& buffer) {
  auto* display = GetDisplay(display_id);
  if (display == nullptr) {
    cmd_result_writer_->AddError(hwc3::Error::kBadDisplay);
    return;
  }

  buffer_handle_t imported_buffer = nullptr;
  auto buf_releaser = composer_resources_->CreateResourceReleaser(true);

  auto error = composer_resources_->GetDisplayOutputBuffer(display_id, buffer,
                                                           &imported_buffer,
                                                           buf_releaser.get());
  if (error != hwc3::Error::kNone) {
    cmd_result_writer_->AddError(error);
    return;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
  auto fence = const_cast<::ndk::ScopedFileDescriptor&>(buffer.fence).release();
  error = Hwc2toHwc3Error(display->SetOutputBuffer(imported_buffer, fence));
  if (error != hwc3::Error::kNone) {
    cmd_result_writer_->AddError(error);
    return;
  }
}
void ComposerClient::ExecuteValidateDisplay(
    int64_t display_id,
    std::optional<ClockMonotonicTimestamp> /*expected_present_time*/
) {
  auto* display = GetDisplay(display_id);
  if (display == nullptr) {
    cmd_result_writer_->AddError(hwc3::Error::kBadDisplay);
    return;
  }

  /* TODO: Handle expectedPresentTime */
  /* This can be implemented in multiple ways. For example, the expected present
   * time property can be implemented by the DRM driver directly as a CRTC
   * property. See:
   * https://cs.android.com/android/platform/superproject/main/+/b8b3b1646e64d0235f77b9e717a3e4082e26f2a8:hardware/google/graphics/common/libhwc2.1/libdrmresource/drm/drmcrtc.cpp;drc=468f6172546ab98983de18210222f231f16b21e1;l=88
   * Unfortunately there doesn't seem to be a standardised way of delaying
   * presentation with a timestamp in the DRM API. What we can do alternatively
   * is to spawn a separate presentation thread that could handle the VBlank
   * events by using DRM_MODE_PAGE_FLIP_EVENT and schedule them appropriately.
   */

  std::vector<int64_t> changed_layers;
  std::vector<Composition> composition_types;
  int32_t display_request_mask = 0;
  std::vector<int64_t> requested_layers;
  std::vector<int32_t> request_masks;

  const hwc3::Error error = ValidateDisplayInternal(*display, &changed_layers,
                                                    &composition_types,
                                                    &display_request_mask,
                                                    &requested_layers,
                                                    &request_masks, nullptr,
                                                    nullptr);

  if (error != hwc3::Error::kNone) {
    cmd_result_writer_->AddError(error);
  }

  // If a CommandError has been been set for the current DisplayCommand, then
  // no other results should be returned besides the error.
  if (cmd_result_writer_->HasError()) {
    return;
  }

  DisplayChanges changes{};
  for (size_t i = 0; i < composition_types.size(); i++) {
    changes.AddLayerCompositionChange(display_id, changed_layers[i],
                                      composition_types[i]);
  }

  std::vector<DisplayRequest::LayerRequest> layer_requests;
  for (size_t i = 0; i < requested_layers.size(); i++) {
    layer_requests.push_back({requested_layers[i], request_masks[i]});
  }

  const DisplayRequest request_changes{display_id, display_request_mask,
                                       layer_requests};
  changes.display_request_changes = request_changes;

  cmd_result_writer_->AddChanges(changes);
  composer_resources_->SetDisplayMustValidateState(display_id, false);
}

void ComposerClient::ExecuteAcceptDisplayChanges(int64_t display_id) {
  auto* display = GetDisplay(display_id);
  if (display == nullptr) {
    cmd_result_writer_->AddError(hwc3::Error::kBadDisplay);
    return;
  }

  auto error = Hwc2toHwc3Error(display->AcceptDisplayChanges());
  if (error != hwc3::Error::kNone) {
    cmd_result_writer_->AddError(error);
    return;
  }
}

void ComposerClient::ExecutePresentDisplay(int64_t display_id) {
  auto* display = GetDisplay(display_id);
  if (display == nullptr) {
    cmd_result_writer_->AddError(hwc3::Error::kBadDisplay);
    return;
  }

  ::android::base::unique_fd display_fence;
  std::unordered_map<int64_t, ::android::base::unique_fd> release_fences;
  auto error = PresentDisplayInternal(display_id, display_fence,
                                      release_fences);
  if (error != hwc3::Error::kNone) {
    cmd_result_writer_->AddError(error);
  }
  if (cmd_result_writer_->HasError()) {
    return;
  }

  cmd_result_writer_->AddPresentFence(display_id, std::move(display_fence));
  cmd_result_writer_->AddReleaseFence(display_id, release_fences);
}

void ComposerClient::ExecutePresentOrValidateDisplay(
    int64_t display_id,
    std::optional<ClockMonotonicTimestamp> expected_present_time) {
  auto* display = GetDisplay(display_id);
  if (display == nullptr) {
    cmd_result_writer_->AddError(hwc3::Error::kBadDisplay);
    return;
  }

  /* TODO: Handle expectedPresentTime */
  /* This can be implemented in multiple ways. For example, the expected present
   * time property can be implemented by the DRM driver directly as a CRTC
   * property. See:
   * https://cs.android.com/android/platform/superproject/main/+/b8b3b1646e64d0235f77b9e717a3e4082e26f2a8:hardware/google/graphics/common/libhwc2.1/libdrmresource/drm/drmcrtc.cpp;drc=468f6172546ab98983de18210222f231f16b21e1;l=88
   * Unfortunately there doesn't seem to be a standardised way of delaying
   * presentation with a timestamp in the DRM API. What we can do alternatively
   * is to spawn a separate presentation thread that could handle the VBlank
   * events by using DRM_MODE_PAGE_FLIP_EVENT and schedule them appropriately.
   */

  /* TODO: Add check if it's possible to skip display validation */
  ExecuteValidateDisplay(display_id, expected_present_time);
  cmd_result_writer_
      ->AddPresentOrValidateResult(display_id,
                                   PresentOrValidate::Result::Validated);
}

}  // namespace aidl::android::hardware::graphics::composer3::impl
