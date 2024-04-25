/*
 * Copyright (C) 2022 The Android Open Source Project
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

#define LOG_TAG "virtual-display"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "VirtualDisplay.h"

#include "DrmHwcTwo.h"
#include "backend/Backend.h"
#include "backend/BackendManager.h"
#include "bufferinfo/BufferInfoGetter.h"
#include "utils/log.h"
#include "utils/properties.h"
#include <numeric>
namespace android {

std::string VirtualDisplay::DumpDelta(VirtualDisplay::Stats delta) {
  if (delta.total_pixops_ == 0)
    return "No stats yet";
  double ratio = 1.0 - double(delta.gpu_pixops_) / double(delta.total_pixops_);

  std::stringstream ss;
  ss << " Total frames count: " << delta.total_frames_ << "\n"
     << " Failed to test commit frames: " << delta.failed_kms_validate_ << "\n"
     << " Failed to commit frames: " << delta.failed_kms_present_ << "\n"
     << ((delta.failed_kms_present_ > 0)
             ? " !!! Internal failure, FIX it please\n"
             : "")
     << " Flattened frames: " << delta.frames_flattened_ << "\n"
     << " Pixel operations (free units)"
     << " : [TOTAL: " << delta.total_pixops_ << " / GPU: " << delta.gpu_pixops_
     << "]\n"
     << " Composition efficiency: " << ratio;

  return ss.str();
}

std::string VirtualDisplay::Dump() {
  std::string flattening_state_str;
  switch (flattenning_state_) {
    case ClientFlattenningState::Disabled:
      flattening_state_str = "Disabled";
      break;
    case ClientFlattenningState::NotRequired:
      flattening_state_str = "Not needed";
      break;
    case ClientFlattenningState::Flattened:
      flattening_state_str = "Active";
      break;
    case ClientFlattenningState::ClientRefreshRequested:
      flattening_state_str = "Refresh requested";
      break;
    default:
      flattening_state_str = std::to_string(flattenning_state_) +
                             " VSync remains";
  }

  std::string connector_name = physical_display_->IsInHeadlessMode()
                                   ? "NULL-VIRTUAL-DISPLAY"
                                   : physical_display_->GetPipe().connector->Get()->GetName();

  std::stringstream ss;
  ss << "- Display on: " << connector_name << "\n"
     << "  Flattening state: " << flattening_state_str << "\n"
     << "Statistics since system boot:\n"
     << DumpDelta(total_stats_) << "\n\n"
     << "Statistics since last dumpsys request:\n"
     << DumpDelta(total_stats_.minus(prev_stats_)) << "\n\n";

  memcpy(&prev_stats_, &total_stats_, sizeof(Stats));
  return ss.str();
}

VirtualDisplay::VirtualDisplay(hwc2_display_t handle,
                                HWC2::DisplayType type,
                                android::VirtualDisplayType vtype,
                                DrmHwcTwo *hwc2,
                                HwcDisplay *physical_display,
                                uint32_t x_offset,
                                uint32_t x_resolution,
                                uint32_t y_resolution)
    : hwc2_(hwc2),
      handle_(handle),
      type_(type),
      client_layer_(physical_display,this),
      color_transform_hint_(HAL_COLOR_TRANSFORM_IDENTITY),
      x_offset_(x_offset),
      x_resolution_(x_resolution),
      y_resolution_(y_resolution),
      vitual_display_type_(vtype) {
  // clang-format off
  color_transform_matrix_ = {1.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 0.0,
                             0.0, 0.0, 0.0, 1.0};
  // clang-format on
  physical_display_ = physical_display;
  Deinit();
  if (!physical_display->IsInHeadlessMode() || handle_ == kPrimaryDisplay) {
    Init();
    hwc2_->ScheduleHotplugEvent(handle_,/*connected = */ true);
  } else {
    hwc2_->ScheduleHotplugEvent(handle_,/*connected = */ false);
  }

}

VirtualDisplay::~VirtualDisplay() = default;

void VirtualDisplay::Deinit() {
  SetClientTarget(nullptr, -1, 0, {});
}

HWC2::Error VirtualDisplay::Init() {
  ChosePreferredConfig();
  if (!physical_display_->IsInHeadlessMode()) {
    int ret = BackendManager::GetInstance().SetBackendForDisplay(this);
    if (ret) {
      ALOGE("Failed to set backend for d=%d %d\n", int(handle_), ret);
      return HWC2::Error::BadDisplay;
    }
  }
  client_layer_.SetLayerBlendMode(HWC2_BLEND_MODE_PREMULTIPLIED);
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::ChosePreferredConfig() {
  configs_ = physical_display_->GetHwcDisplayConfigs();

  return SetActiveConfig(configs_.preferred_config_id);
}

HWC2::Error VirtualDisplay::AcceptDisplayChanges() {
  for (std::pair<const hwc2_layer_t, HwcLayer> &l : layers_)
    l.second.AcceptTypeChange();
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::CreateLayer(hwc2_layer_t *layer) {
  layers_.emplace(static_cast<hwc2_layer_t>(layer_idx_), HwcLayer(physical_display_, this));
  *layer = static_cast<hwc2_layer_t>(layer_idx_);
  ++layer_idx_;
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::DestroyLayer(hwc2_layer_t layer) {
  if (!get_layer(layer)) {
    return HWC2::Error::BadLayer;
  }
  layers_.erase(layer);
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::GetActiveConfig(hwc2_config_t *config) const {
  if (configs_.hwc_configs.count(staged_mode_config_id_) == 0)
    return HWC2::Error::BadConfig;

  *config = staged_mode_config_id_;
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::GetChangedCompositionTypes(uint32_t *num_elements,
                                                   hwc2_layer_t *layers,
                                                   int32_t *types) {
  if (physical_display_->IsInHeadlessMode()) {
    *num_elements = 0;
    return HWC2::Error::None;
  }

  uint32_t num_changes = 0;
  for (std::pair<const hwc2_layer_t, HwcLayer> &l : layers_) {
    if (l.second.IsTypeChanged()) {
      if (layers && num_changes < *num_elements)
        layers[num_changes] = l.first;
      if (types && num_changes < *num_elements)
        types[num_changes] = static_cast<int32_t>(l.second.GetValidatedType());
      ++num_changes;
    }
  }
  if (!layers && !types)
    *num_elements = num_changes;
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::GetClientTargetSupport(uint32_t width, uint32_t height,
                                               int32_t /*format*/,
                                               int32_t dataspace) {
  return physical_display_->GetClientTargetSupport(width, height, 0, dataspace);
}

HWC2::Error VirtualDisplay::GetColorModes(uint32_t *num_modes, int32_t *modes) {

  return physical_display_->GetColorModes(num_modes, modes);
}

HWC2::Error VirtualDisplay::GetDisplayAttribute(hwc2_config_t config,
                                            int32_t attribute_in,
                                            int32_t *value) {
  int conf = static_cast<int>(config);

  if (configs_.hwc_configs.count(conf) == 0) {
    ALOGE("Could not find mode #%d", conf);
    return HWC2::Error::BadConfig;
  }

  auto &hwc_config = configs_.hwc_configs[conf];

  static const int32_t kUmPerInch = 25400;
  uint32_t mm_width = configs_.mm_width;
  uint32_t mm_height = configs_.mm_height;
  auto attribute = static_cast<HWC2::Attribute>(attribute_in);
  switch (attribute) {
    case HWC2::Attribute::Width:
      *value = static_cast<int>(x_resolution_ ? x_resolution_ : hwc_config.mode.h_display());
      break;
    case HWC2::Attribute::Height:
      *value = static_cast<int>(y_resolution_ ? y_resolution_ : hwc_config.mode.v_display());
      break;
    case HWC2::Attribute::VsyncPeriod:
      // in nanoseconds
      *value = static_cast<int>(1E9 / hwc_config.mode.v_refresh());
      break;
    case HWC2::Attribute::DpiX:
      // Dots per 1000 inches
      *value = mm_width ? static_cast<int>(hwc_config.mode.h_display() *
                                           kUmPerInch / mm_width)
                        : -1;
      break;
    case HWC2::Attribute::DpiY:
      // Dots per 1000 inches
      *value = mm_height ? static_cast<int>(hwc_config.mode.v_display() *
                                            kUmPerInch / mm_height)
                         : -1;
      break;
#if PLATFORM_SDK_VERSION > 29
    case HWC2::Attribute::ConfigGroup:
      /* Dispite ConfigGroup is a part of HWC2.4 API, framework
       * able to request it even if service @2.1 is used */
      *value = int(hwc_config.group_id);
      break;
#endif
    default:
      *value = -1;
      return HWC2::Error::BadConfig;
  }
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::GetDisplayConfigs(uint32_t *num_configs,
                                          hwc2_config_t *configs) {
  uint32_t idx = 0;
  for (auto &hwc_config : configs_.hwc_configs) {
    if (hwc_config.second.disabled) {
      continue;
    }

    if (configs != nullptr) {
      if (idx >= *num_configs) {
        break;
      }
      configs[idx] = hwc_config.second.id;
    }

    idx++;
  }
  *num_configs = idx;
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::GetDisplayName(uint32_t *size, char *name) {
  std::ostringstream stream;
  if (physical_display_->IsInHeadlessMode()) {
    stream << "null-virtual-display";
  } else {
    stream << "virtual-display-" << physical_display_->GetPipe().connector->Get()->GetId();
  }
  std::string string = stream.str();
  size_t length = string.length();
  if (!name) {
    *size = length;
    return HWC2::Error::None;
  }

  *size = std::min<uint32_t>(static_cast<uint32_t>(length - 1), *size);
  strncpy(name, string.c_str(), *size);
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::GetDisplayRequests(int32_t * /*display_requests*/,
                                           uint32_t *num_elements,
                                           hwc2_layer_t * /*layers*/,
                                           int32_t * /*layer_requests*/) {
  // TODO(nobody): I think virtual display should request
  //      HWC2_DISPLAY_REQUEST_WRITE_CLIENT_TARGET_TO_OUTPUT here
  *num_elements = 0;
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::GetDisplayType(int32_t *type) {
  *type = static_cast<int32_t>(type_);
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::GetDozeSupport(int32_t *support) {
  *support = 0;
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::GetPerFrameMetadataKeys(uint32_t *outNumKeys, int32_t *outKeys) {
  return physical_display_->GetPerFrameMetadataKeys(outNumKeys, outKeys);
}

HWC2::Error VirtualDisplay::GetHdrCapabilities(uint32_t *num_types,
                                           int32_t * types,
                                           float * max_luminance,
                                           float * max_average_luminance,
                                           float * min_luminance) {
  return physical_display_->GetHdrCapabilities(num_types, types,
                                      max_luminance, max_average_luminance, min_luminance);
}

/* Find API details at:
 * https://cs.android.com/android/platform/superproject/+/android-11.0.0_r3:hardware/libhardware/include/hardware/hwcomposer2.h;l=1767
 *
 * Called after PresentDisplay(), CLIENT is expecting release fence for the
 * prior buffer (not the one assigned to the layer at the moment).
 */
HWC2::Error VirtualDisplay::GetReleaseFences(uint32_t *num_elements,
                                         hwc2_layer_t *layers,
                                         int32_t *fences) {
  if (physical_display_->IsInHeadlessMode()) {
    *num_elements = 0;
    return HWC2::Error::None;
  }

  uint32_t num_layers = 0;

  for (auto &l : layers_) {
    if (!l.second.GetPriorBufferScanOutFlag() || !present_fence_) {
      continue;
    }

    ++num_layers;

    if (layers == nullptr || fences == nullptr)
      continue;

    if (num_layers > *num_elements) {
      ALOGW("Overflow num_elements %d/%d", num_layers, *num_elements);
      return HWC2::Error::None;
    }

    layers[num_layers - 1] = l.first;
    fences[num_layers - 1] = UniqueFd::Dup(present_fence_.Get()).Release();
  }
  *num_elements = num_layers;

  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::CreateComposition(AtomicCommitArgs &a_args) {
  if (physical_display_->IsInHeadlessMode()) {
    ALOGE("%s: Display is in headless mode, should never reach here", __func__);
    return HWC2::Error::None;
  }

  int PrevModeVsyncPeriodNs = static_cast<int>(
      1E9 / physical_display_->GetPipe().connector->Get()->GetActiveMode().v_refresh());

  auto mode_update_commited_ = false;
  if (staged_mode_ &&
      staged_mode_change_time_ <= ResourceManager::GetTimeMonotonicNs()) {
    client_layer_.SetLayerDisplayFrame(
        (hwc_rect_t){.left = 0,
                     .top = 0,
                     .right = static_cast<int>(x_resolution_),
                     .bottom = static_cast<int>(staged_mode_->v_display())});

    configs_.active_config_id = staged_mode_config_id_;

    a_args.display_mode = *staged_mode_;
    if (!a_args.test_only) {
      mode_update_commited_ = true;
    }
  }

  a_args.color_adjustment = physical_display_->GetPipe().device->GetColorAdjustmentEnabling();

  // order the layers by z-order
  bool use_client_layer = false;
  uint32_t client_z_order = UINT32_MAX;
  std::map<uint32_t, HwcLayer *> z_map;
  for (std::pair<const hwc2_layer_t, HwcLayer> &l : layers_) {
    switch (l.second.GetValidatedType()) {
      case HWC2::Composition::Device:
        z_map.emplace(std::make_pair(l.second.GetZOrder(), &l.second));
        break;
      case HWC2::Composition::Client:
        // Place it at the z_order of the lowest client layer
        use_client_layer = true;
        client_z_order = std::min(client_z_order, l.second.GetZOrder());
        break;
      default:
        continue;
    }
  }
  if (use_client_layer)
    z_map.emplace(std::make_pair(client_z_order, &client_layer_));

  if (z_map.empty())
    return HWC2::Error::BadLayer;

  std::vector<LayerData> composition_layers;

  /* Import & populate */
  for (std::pair<const uint32_t, HwcLayer *> &l : z_map) {
    l.second->PopulateLayerData(a_args.test_only);
  }

  // now that they're ordered by z, add them to the composition
  for (std::pair<const uint32_t, HwcLayer *> &l : z_map) {
    if (!l.second->IsLayerUsableAsDevice()) {
      /* This will be normally triggered on validation of the first frame
       * containing CLIENT layer. At this moment client buffer is not yet
       * provided by the CLIENT.
       * This may be triggered once in HwcLayer lifecycle in case FB can't be
       * imported. For example when non-contiguous buffer is imported into
       * contiguous-only DRM/KMS driver.
       */
      return HWC2::Error::BadLayer;
    }
    composition_layers.emplace_back(l.second->GetLayerData().Clone());
  }

  /* Store plan to ensure shared planes won't be stolen by other display
   * in between of ValidateDisplay() and PresentDisplay() calls
   */
  current_plan_ = DrmKmsPlan::CreateDrmKmsPlan(physical_display_->GetPipe(),
                                               std::move(composition_layers));
  if (!current_plan_) {
    if (!a_args.test_only) {
      ALOGE("Failed to create DrmKmsPlan");
    }
    return HWC2::Error::BadConfig;
  }

  a_args.composition = current_plan_;

  int ret = physical_display_->GetPipe().atomic_state_manager->ExecuteAtomicCommit(a_args);

  if (ret) {
    if (!a_args.test_only)
      ALOGE("Failed to apply the frame composition ret=%d", ret);
    return HWC2::Error::BadParameter;
  }

  if (mode_update_commited_) {
    staged_mode_.reset();
    vsync_tracking_en_ = false;
    if (last_vsync_ts_ != 0) {
      physical_display_->GetHwc2()->SendVsyncPeriodTimingChangedEventToClient(
          handle_, last_vsync_ts_ + PrevModeVsyncPeriodNs);
    }
  }

  return HWC2::Error::None;
}

/* Find API details at:
 * https://cs.android.com/android/platform/superproject/+/android-11.0.0_r3:hardware/libhardware/include/hardware/hwcomposer2.h;l=1805
 */
HWC2::Error VirtualDisplay::PresentDisplay(int32_t *out_present_fence) {
  if (physical_display_->IsInHeadlessMode()) {
    *out_present_fence = -1;
    return HWC2::Error::None;
  }
  if (staged_mode_ &&
      staged_mode_change_time_ <= ResourceManager::GetTimeMonotonicNs()) {
    client_layer_.SetLayerDisplayFrame(
        (hwc_rect_t){.left = 0,
                     .top = 0,
                     .right = static_cast<int>(x_resolution_),
                     .bottom = static_cast<int>(staged_mode_->v_display())});

    configs_.active_config_id = staged_mode_config_id_;
  }

  // order the layers by z-order
  bool use_client_layer = false;
  uint32_t client_z_order = UINT32_MAX;
  std::map<uint32_t, HwcLayer *> z_map;
  for (std::pair<const hwc2_layer_t, HwcLayer> &l : layers_) {
    switch (l.second.GetValidatedType()) {
      case HWC2::Composition::Device:
        z_map.emplace(std::make_pair(l.second.GetZOrder(), &l.second));
        break;
      case HWC2::Composition::Client:
        // Place it at the z_order of the lowest client layer
        use_client_layer = true;
        client_z_order = std::min(client_z_order, l.second.GetZOrder());
        break;
      default:
        continue;
    }
  }
  if (use_client_layer)
    z_map.emplace(std::make_pair(client_z_order, &client_layer_));
  if (vitual_display_type_ == VirtualDisplayType::SuperFrame)
    physical_display_->PresentDisplaySuperFrame(z_map, out_present_fence);
  else
    physical_display_->PresentDisplayLogical(z_map, out_present_fence);
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::SetActiveConfigInternal(uint32_t config,
                                                int64_t change_time) {
  if (configs_.hwc_configs.count(config) == 0) {
    ALOGE("Could not find active mode for %u", config);
    return HWC2::Error::BadConfig;
  }
  staged_mode_ = configs_.hwc_configs[config].mode;
  staged_mode_change_time_ = change_time;
  staged_mode_config_id_ = config;

  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::SetActiveConfig(hwc2_config_t config) {
  return SetActiveConfigInternal(config, ResourceManager::GetTimeMonotonicNs());
}

/* Find API details at:
 * https://cs.android.com/android/platform/superproject/+/android-11.0.0_r3:hardware/libhardware/include/hardware/hwcomposer2.h;l=1861
 */
HWC2::Error VirtualDisplay::SetClientTarget(buffer_handle_t target,
                                        int32_t acquire_fence,
                                        int32_t dataspace,
                                        hwc_region_t /*damage*/) {
  client_layer_.SetLayerBuffer(target, acquire_fence);
  client_layer_.SetLayerDataspace(dataspace);

  /*
   * target can be nullptr, this does mean the Composer Service is calling
   * cleanDisplayResources() on after receiving HOTPLUG event. See more at:
   * https://cs.android.com/android/platform/superproject/+/master:hardware/interfaces/graphics/composer/2.1/utils/hal/include/composer-hal/2.1/ComposerClient.h;l=350;drc=944b68180b008456ed2eb4d4d329e33b19bd5166
   */
  if (target == nullptr) {
    client_layer_.SwChainClearCache();
    return HWC2::Error::None;
  }

  if (physical_display_->IsInHeadlessMode()) {
    return HWC2::Error::None;
  }

  client_layer_.PopulateLayerData(/*test = */ true);
  if (!client_layer_.IsLayerUsableAsDevice()) {
    ALOGE("Client layer must be always usable by DRM/KMS");
    return HWC2::Error::BadLayer;
  }

  auto &bi = client_layer_.GetLayerData().bi;
  hwc_frect_t source_crop = {.left = 0.0F,
                             .top = 0.0F,
                             .right = static_cast<float>(bi->width),
                             .bottom = static_cast<float>(bi->height)};
  client_layer_.SetLayerSourceCrop(source_crop);

  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::SetColorMode(int32_t mode) {
  if (mode < HAL_COLOR_MODE_NATIVE || mode > HAL_COLOR_MODE_BT2100_HLG)
    return HWC2::Error::BadParameter;

  if (mode != HAL_COLOR_MODE_NATIVE)
    return HWC2::Error::Unsupported;

  color_mode_ = mode;
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::SetColorTransform(const float *matrix, int32_t hint) {
  return physical_display_->SetColorTransform(matrix, hint);
}

HWC2::Error VirtualDisplay::SetOutputBuffer(buffer_handle_t /*buffer*/,
                                        int32_t /*release_fence*/) {
  // TODO(nobody): Need virtual display support
  return HWC2::Error::Unsupported;
}

HWC2::Error VirtualDisplay::SetPowerMode(int32_t mode_in) {
  if (handle_ == kPrimaryDisplay)
    physical_display_->SetPowerMode(mode_in);
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::SetVsyncEnabled(int32_t enabled) {
  return physical_display_->SetVsyncEnabled(enabled);
}

HWC2::Error VirtualDisplay::ValidateDisplay(uint32_t *num_types,
                                        uint32_t *num_requests) {
  if (physical_display_->IsInHeadlessMode()) {
    *num_types = *num_requests = 0;
    return HWC2::Error::None;
  }

  /* In current drm_hwc design in case previous frame layer was not validated as
   * a CLIENT, it is used by display controller (Front buffer). We have to store
   * this state to provide the CLIENT with the release fences for such buffers.
   */
  for (auto &l : layers_) {
    l.second.SetPriorBufferScanOutFlag(l.second.GetValidatedType() !=
                                       HWC2::Composition::Client);
  }
  return backend_->ValidateDisplay(this, num_types, num_requests);
}

std::vector<HwcLayer *> VirtualDisplay::GetOrderLayersByZPos() {
  std::vector<HwcLayer *> ordered_layers;
  ordered_layers.reserve(layers_.size());

  for (auto &[handle, layer] : layers_) {
    ordered_layers.emplace_back(&layer);
  }

  std::sort(std::begin(ordered_layers), std::end(ordered_layers),
            [](const HwcLayer *lhs, const HwcLayer *rhs) {
              return lhs->GetZOrder() < rhs->GetZOrder();
            });

  return ordered_layers;
}

HWC2::Error VirtualDisplay::GetDisplayVsyncPeriod(
    uint32_t *outVsyncPeriod /* ns */) {
  return GetDisplayAttribute(configs_.active_config_id,
                             HWC2_ATTRIBUTE_VSYNC_PERIOD,
                             (int32_t *)(outVsyncPeriod));
}

#if PLATFORM_SDK_VERSION > 29
HWC2::Error VirtualDisplay::GetDisplayConnectionType(uint32_t *outType) {
  return physical_display_->GetDisplayConnectionType(outType);
}

HWC2::Error VirtualDisplay::SetActiveConfigWithConstraints(
    hwc2_config_t config,
    hwc_vsync_period_change_constraints_t *vsyncPeriodChangeConstraints,
    hwc_vsync_period_change_timeline_t *outTimeline) {
  return physical_display_->SetActiveConfigWithConstraints(config,
                                                          vsyncPeriodChangeConstraints,
                                                          outTimeline);
}

HWC2::Error VirtualDisplay::SetAutoLowLatencyMode(bool /*on*/) {
  return HWC2::Error::Unsupported;
}

HWC2::Error VirtualDisplay::GetSupportedContentTypes(
    uint32_t *outNumSupportedContentTypes,
    const uint32_t *outSupportedContentTypes) {
  if (outSupportedContentTypes == nullptr)
    *outNumSupportedContentTypes = 0;

  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::SetContentType(int32_t contentType) {
  if (contentType != HWC2_CONTENT_TYPE_NONE)
    return HWC2::Error::Unsupported;

  /* TODO: Map to the DRM Connector property:
   * https://elixir.bootlin.com/linux/v5.4-rc5/source/drivers/gpu/drm/drm_connector.c#L809
   */

  return HWC2::Error::None;
}
#endif

#if PLATFORM_SDK_VERSION > 28
HWC2::Error VirtualDisplay::GetDisplayIdentificationData(uint8_t *outPort,
                                                     uint32_t *outDataSize,
                                                     uint8_t *outData) {
  #define ARRAY_SIZE(a) (uint32_t)(sizeof(a) / sizeof(a[0]))
  static uint8_t EMU_EDID[] = {
    0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x05, 0xd7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0xff, 0x21, 0x01, 0x03, 0x80, 0x32, 0x1f, 0x78, 0x07, 0xee, 0x95, 0xa3, 0x54, 0x4c, 0x99, 0x26,
    0x0f, 0x50, 0x54, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
    0x45, 0x00, 0x63, 0xc8, 0x10, 0x00, 0x00, 0x1e, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x17, 0xf0, 0x0f,
    0xff, 0x0f, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0xfc, 0x00, 0x31,
    0x39, 0x32, 0x30, 0x78, 0x31, 0x30, 0x38, 0x30, 0x0a, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x03,
    0x02, 0x03, 0x18, 0x40, 0x23, 0x09, 0x06, 0x07, 0x67, 0x03, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x1e,
    0x67, 0xd8, 0x5d, 0xc4, 0x01, 0x1e, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd7};
  //Fill pnpid 'IVI'
  constexpr size_t kManufacturerOffset = 8;
  const uint16_t manufacturerId = static_cast<uint16_t>('I' - 'A' + 1) << 10 |
                                  static_cast<uint16_t>('V' - 'A' + 1) << 5 |
                                  static_cast<uint16_t>('I' - 'A' + 1);
  EMU_EDID[kManufacturerOffset] = manufacturerId >> 8;
  EMU_EDID[kManufacturerOffset + 1] = static_cast<uint8_t>(manufacturerId);

  constexpr size_t kDescriptorOffset = 54;
  constexpr size_t kDisplayNameOffset = kDescriptorOffset + 36;
  constexpr size_t kDescriptorLength = 18;
  //Fill name 'IVI Display'
  memset(&EMU_EDID[kDisplayNameOffset + 5], 0, kDescriptorLength - 5);
  memcpy(&EMU_EDID[kDisplayNameOffset + 5], "IVI Display", strlen("IVI Display"));
  // EMU_EDID[kDisplayNameOffset + 5 + strlen("IVI Display ")] = uint8_t(handle_) + 0x30;
  EMU_EDID[kDisplayNameOffset + 5 + strlen("IVI Display")] = '\n';
  //caculate checksum
  uint8_t checksum = -(uint8_t)std::accumulate(
                EMU_EDID, EMU_EDID + ARRAY_SIZE(EMU_EDID) / 2 - 1, static_cast<uint8_t>(0));
  EMU_EDID[ARRAY_SIZE(EMU_EDID) / 2 - 1] = checksum;
  checksum = -(uint8_t)std::accumulate(
                EMU_EDID, EMU_EDID + ARRAY_SIZE(EMU_EDID) - 1, static_cast<uint8_t>(0));
  EMU_EDID[ARRAY_SIZE(EMU_EDID) - 1] = checksum;


  *outPort = handle_; /* TDOD(nobody): What should be here? */

  if (outData) {
    *outDataSize = std::min(*outDataSize, ARRAY_SIZE(EMU_EDID));
    memcpy(outData, EMU_EDID, *outDataSize);
  } else {
    *outDataSize = ARRAY_SIZE(EMU_EDID);
  }

  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::GetDisplayCapabilities(uint32_t *outNumCapabilities,
                                               uint32_t * /*outCapabilities*/) {
  if (outNumCapabilities == nullptr) {
    return HWC2::Error::BadParameter;
  }

  *outNumCapabilities = 0;

  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::GetDisplayBrightnessSupport(bool *supported) {
  *supported = false;
  return HWC2::Error::None;
}

HWC2::Error VirtualDisplay::SetDisplayBrightness(float /* brightness */) {
  return HWC2::Error::Unsupported;
}

#endif /* PLATFORM_SDK_VERSION > 28 */

#if PLATFORM_SDK_VERSION > 27

HWC2::Error VirtualDisplay::GetRenderIntents(
    int32_t mode, uint32_t *outNumIntents,
    int32_t * /*android_render_intent_v1_1_t*/ outIntents) {

  return physical_display_->GetRenderIntents(mode, outNumIntents, outIntents);
}

HWC2::Error VirtualDisplay::SetColorModeWithIntent(int32_t mode, int32_t intent) {
  if (intent < HAL_RENDER_INTENT_COLORIMETRIC ||
      intent > HAL_RENDER_INTENT_TONE_MAP_ENHANCE)
    return HWC2::Error::BadParameter;

  if (mode < HAL_COLOR_MODE_NATIVE || mode > HAL_COLOR_MODE_BT2100_HLG)
    return HWC2::Error::BadParameter;

  if (mode != HAL_COLOR_MODE_NATIVE)
    return HWC2::Error::Unsupported;

  if (intent != HAL_RENDER_INTENT_COLORIMETRIC)
    return HWC2::Error::Unsupported;

  color_mode_ = mode;
  return HWC2::Error::None;
}

#endif /* PLATFORM_SDK_VERSION > 27 */

const VirtualBackend *VirtualDisplay::backend() const {
  return backend_.get();
}

void VirtualDisplay::set_backend(std::unique_ptr<VirtualBackend> backend) {
  backend_ = std::move(backend);
}

/* returns true if composition should be sent to client */
bool VirtualDisplay::ProcessClientFlatteningState(bool skip) {
  return physical_display_->ProcessClientFlatteningState(skip);
}
}  // namespace android