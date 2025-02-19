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

#define LOG_TAG "drmhwc"
#define ATRACE_TAG ATRACE_TAG_GRAPHICS

#include "HwcDisplay.h"

#include <cinttypes>

#include <ui/ColorSpace.h>

#include "backend/Backend.h"
#include "backend/BackendManager.h"
#include "bufferinfo/BufferInfoGetter.h"
#include "compositor/DisplayInfo.h"
#include "drm/DrmConnector.h"
#include "drm/DrmDisplayPipeline.h"
#include "drm/DrmHwc.h"
#include "utils/log.h"
#include "utils/properties.h"

using ::android::DrmDisplayPipeline;
using ColorGamut = ::android::ColorSpace;

namespace android {

namespace {

constexpr int kCtmRows = 3;
constexpr int kCtmCols = 3;

constexpr std::array<float, 16> kIdentityMatrix = {
    1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
    0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
};

uint64_t To3132FixPt(float in) {
  constexpr uint64_t kSignMask = (1ULL << 63);
  constexpr uint64_t kValueMask = ~(1ULL << 63);
  constexpr auto kValueScale = static_cast<float>(1ULL << 32);
  if (in < 0)
    return (static_cast<uint64_t>(-in * kValueScale) & kValueMask) | kSignMask;
  return static_cast<uint64_t>(in * kValueScale) & kValueMask;
}

auto ToColorTransform(const std::array<float, 16> &color_transform_matrix) {
  /* HAL provides a 4x4 float type matrix:
   * | 0  1  2  3|
   * | 4  5  6  7|
   * | 8  9 10 11|
   * |12 13 14 15|
   *
   * R_out = R*0 + G*4 + B*8 + 12
   * G_out = R*1 + G*5 + B*9 + 13
   * B_out = R*2 + G*6 + B*10 + 14
   *
   * DRM expects a 3x3 s31.32 fixed point matrix:
   * out   matrix    in
   * |R|   |0 1 2|   |R|
   * |G| = |3 4 5| x |G|
   * |B|   |6 7 8|   |B|
   *
   * R_out = R*0 + G*1 + B*2
   * G_out = R*3 + G*4 + B*5
   * B_out = R*6 + G*7 + B*8
   */
  auto color_matrix = std::make_shared<drm_color_ctm>();
  for (int i = 0; i < kCtmCols; i++) {
    for (int j = 0; j < kCtmRows; j++) {
      constexpr int kInCtmRows = 4;
      color_matrix->matrix[(i * kCtmRows) + j] = To3132FixPt(
          color_transform_matrix[(j * kInCtmRows) + i]);
    }
  }
  return color_matrix;
}

}  // namespace

std::string HwcDisplay::DumpDelta(HwcDisplay::Stats delta) {
  if (delta.total_pixops_ == 0)
    return "No stats yet";
  auto ratio = 1.0 - (double(delta.gpu_pixops_) / double(delta.total_pixops_));

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

std::string HwcDisplay::Dump() {
  auto connector_name = IsInHeadlessMode()
                            ? std::string("NULL-DISPLAY")
                            : GetPipe().connector->Get()->GetName();

  std::stringstream ss;
  ss << "- Display on: " << connector_name << "\n"
     << "Statistics since system boot:\n"
     << DumpDelta(total_stats_) << "\n\n"
     << "Statistics since last dumpsys request:\n"
     << DumpDelta(total_stats_.minus(prev_stats_)) << "\n\n";

  memcpy(&prev_stats_, &total_stats_, sizeof(Stats));
  return ss.str();
}

HwcDisplay::HwcDisplay(hwc2_display_t handle, HWC2::DisplayType type,
                       DrmHwc *hwc)
    : hwc_(hwc), handle_(handle), type_(type), client_layer_(this) {
  if (type_ == HWC2::DisplayType::Virtual) {
    writeback_layer_ = std::make_unique<HwcLayer>(this);
  }
}

void HwcDisplay::SetColorTransformMatrix(
    const std::array<float, 16> &color_transform_matrix) {
  auto almost_equal = [](auto a, auto b) {
    const float epsilon = 0.001F;
    return std::abs(a - b) < epsilon;
  };
  const bool is_identity = std::equal(color_transform_matrix.begin(),
                                      color_transform_matrix.end(),
                                      kIdentityMatrix.begin(), almost_equal);
  color_transform_hint_ = is_identity ? HAL_COLOR_TRANSFORM_IDENTITY
                                      : HAL_COLOR_TRANSFORM_ARBITRARY_MATRIX;
  if (color_transform_hint_ == is_identity) {
    SetColorMatrixToIdentity();
  } else {
    color_matrix_ = ToColorTransform(color_transform_matrix);
  }
}

void HwcDisplay::SetColorMatrixToIdentity() {
  color_matrix_ = std::make_shared<drm_color_ctm>();
  for (int i = 0; i < kCtmCols; i++) {
    for (int j = 0; j < kCtmRows; j++) {
      constexpr uint64_t kOne = (1ULL << 32); /* 1.0 in s31.32 format */
      color_matrix_->matrix[(i * kCtmRows) + j] = (i == j) ? kOne : 0;
    }
  }

  color_transform_hint_ = HAL_COLOR_TRANSFORM_IDENTITY;
}

HwcDisplay::~HwcDisplay() {
  Deinit();
};

auto HwcDisplay::GetConfig(hwc2_config_t config_id) const
    -> const HwcDisplayConfig * {
  auto config_iter = configs_.hwc_configs.find(config_id);
  if (config_iter == configs_.hwc_configs.end()) {
    return nullptr;
  }
  return &config_iter->second;
}

auto HwcDisplay::GetCurrentConfig() const -> const HwcDisplayConfig * {
  return GetConfig(configs_.active_config_id);
}

auto HwcDisplay::GetLastRequestedConfig() const -> const HwcDisplayConfig * {
  return GetConfig(staged_mode_config_id_.value_or(configs_.active_config_id));
}

HwcDisplay::ConfigError HwcDisplay::SetConfig(hwc2_config_t config) {
  const HwcDisplayConfig *new_config = GetConfig(config);
  if (new_config == nullptr) {
    ALOGE("Could not find active mode for %u", config);
    return ConfigError::kBadConfig;
  }

  const HwcDisplayConfig *current_config = GetCurrentConfig();

  const uint32_t width = new_config->mode.GetRawMode().hdisplay;
  const uint32_t height = new_config->mode.GetRawMode().vdisplay;

  std::optional<LayerData> modeset_layer_data;
  // If a client layer has already been provided, and its size matches the
  // new config, use it for the modeset.
  if (client_layer_.IsLayerUsableAsDevice() && current_config &&
      current_config->mode.GetRawMode().hdisplay == width &&
      current_config->mode.GetRawMode().vdisplay == height) {
    ALOGV("Use existing client_layer for blocking config.");
    modeset_layer_data = client_layer_.GetLayerData();
  } else {
    ALOGV("Allocate modeset buffer.");
    auto modeset_buffer =  //
        GetPipe().device->CreateBufferForModeset(width, height);
    if (modeset_buffer) {
      auto modeset_layer = std::make_unique<HwcLayer>(this);
      HwcLayer::LayerProperties properties;
      properties.slot_buffer = {
          .slot_id = 0,
          .bi = modeset_buffer,
      };
      properties.active_slot = {
          .slot_id = 0,
          .fence = {},
      };
      properties.blend_mode = BufferBlendMode::kNone;
      modeset_layer->SetLayerProperties(properties);
      modeset_layer->PopulateLayerData();
      modeset_layer_data = modeset_layer->GetLayerData();
    }
  }

  ALOGV("Create modeset commit.");
  // Create atomic commit args for a blocking modeset. There's no need to do a
  // separate test commit, since the commit does a test anyways.
  AtomicCommitArgs commit_args = CreateModesetCommit(new_config,
                                                     modeset_layer_data);
  commit_args.blocking = true;
  int ret = GetPipe().atomic_state_manager->ExecuteAtomicCommit(commit_args);

  if (ret) {
    ALOGE("Blocking config failed: %d", ret);
    return HwcDisplay::ConfigError::kBadConfig;
  }

  ALOGV("Blocking config succeeded.");
  configs_.active_config_id = config;
  staged_mode_config_id_.reset();
  vsync_worker_->SetVsyncPeriodNs(new_config->mode.GetVSyncPeriodNs());
  // set new vsync period
  return ConfigError::kNone;
}

auto HwcDisplay::QueueConfig(hwc2_config_t config, int64_t desired_time,
                             bool seamless, QueuedConfigTiming *out_timing)
    -> ConfigError {
  if (configs_.hwc_configs.count(config) == 0) {
    ALOGE("Could not find active mode for %u", config);
    return ConfigError::kBadConfig;
  }

  // TODO: Add support for seamless configuration changes.
  if (seamless) {
    return ConfigError::kSeamlessNotAllowed;
  }

  // Request a refresh from the client one vsync period before the desired
  // time, or simply at the desired time if there is no active configuration.
  const HwcDisplayConfig *current_config = GetCurrentConfig();
  out_timing->refresh_time_ns = desired_time -
                                (current_config
                                     ? current_config->mode.GetVSyncPeriodNs()
                                     : 0);
  out_timing->new_vsync_time_ns = desired_time;

  // Queue the config change timing to be consistent with the requested
  // refresh time.
  staged_mode_change_time_ = out_timing->refresh_time_ns;
  staged_mode_config_id_ = config;

  // Enable vsync events until the mode has been applied.
  vsync_worker_->SetVsyncTimestampTracking(true);

  return ConfigError::kNone;
}

auto HwcDisplay::ValidateStagedComposition() -> std::vector<ChangedLayer> {
  if (IsInHeadlessMode()) {
    return {};
  }

  /* In current drm_hwc design in case previous frame layer was not validated as
   * a CLIENT, it is used by display controller (Front buffer). We have to store
   * this state to provide the CLIENT with the release fences for such buffers.
   */
  for (auto &l : layers_) {
    l.second.SetPriorBufferScanOutFlag(l.second.GetValidatedType() !=
                                       HWC2::Composition::Client);
  }

  // ValidateDisplay returns the number of layers that may be changed.
  uint32_t num_types = 0;
  uint32_t num_requests = 0;
  backend_->ValidateDisplay(this, &num_types, &num_requests);

  if (num_types == 0) {
    return {};
  }

  // Iterate through the layers to find which layers actually changed.
  std::vector<ChangedLayer> changed_layers;
  for (auto &l : layers_) {
    if (l.second.IsTypeChanged()) {
      changed_layers.emplace_back(l.first, l.second.GetValidatedType());
    }
  }
  return changed_layers;
}

auto HwcDisplay::GetDisplayBoundsMm() -> std::pair<int32_t, int32_t> {

  const auto bounds = GetEdid()->GetBoundsMm();
  if (bounds.first > 0 || bounds.second > 0) {
    return bounds;
  }

  ALOGE("Failed to get display bounds for d=%d\n", int(handle_));
  // mm_width and mm_height are unreliable. so only provide mm_width to avoid
  // wrong dpi computations or other use of the values.
  return {configs_.mm_width, -1};
}

auto HwcDisplay::AcceptValidatedComposition() -> void {
  for (auto &[_, layer] : layers_) {
    layer.AcceptTypeChange();
  }
}

auto HwcDisplay::PresentStagedComposition(
    SharedFd &out_present_fence, std::vector<ReleaseFence> &out_release_fences)
    -> bool {
  if (IsInHeadlessMode()) {
    return true;
  }
  HWC2::Error ret{};

  ++total_stats_.total_frames_;

  AtomicCommitArgs a_args{};
  ret = CreateComposition(a_args);

  if (ret != HWC2::Error::None)
    ++total_stats_.failed_kms_present_;

  if (ret == HWC2::Error::BadLayer) {
    // Can we really have no client or device layers?
    return true;
  }
  if (ret != HWC2::Error::None)
    return false;

  out_present_fence = a_args.out_fence;

  // Reset the color matrix so we don't apply it over and over again.
  color_matrix_ = {};

  ++frame_no_;

  if (!out_present_fence) {
    return true;
  }

  for (auto &l : layers_) {
    if (l.second.GetPriorBufferScanOutFlag()) {
      out_release_fences.emplace_back(l.first, out_present_fence);
    }
  }

  return true;
}

void HwcDisplay::SetPipeline(std::shared_ptr<DrmDisplayPipeline> pipeline) {
  Deinit();

  pipeline_ = std::move(pipeline);

  if (pipeline_ != nullptr || handle_ == kPrimaryDisplay) {
    Init();
    hwc_->ScheduleHotplugEvent(handle_, DrmHwc::kConnected);
  } else {
    hwc_->ScheduleHotplugEvent(handle_, DrmHwc::kDisconnected);
  }
}

void HwcDisplay::Deinit() {
  if (pipeline_ != nullptr) {
    AtomicCommitArgs a_args{};
    a_args.composition = std::make_shared<DrmKmsPlan>();
    GetPipe().atomic_state_manager->ExecuteAtomicCommit(a_args);
    a_args.composition = {};
    a_args.active = false;
    GetPipe().atomic_state_manager->ExecuteAtomicCommit(a_args);

    current_plan_.reset();
    backend_.reset();
    if (flatcon_) {
      flatcon_->StopThread();
      flatcon_.reset();
    }
  }

  if (vsync_worker_) {
    vsync_worker_->StopThread();
    vsync_worker_ = {};
  }

  client_layer_.ClearSlots();
}

HWC2::Error HwcDisplay::Init() {
  ChosePreferredConfig();

  if (type_ != HWC2::DisplayType::Virtual) {
    vsync_worker_ = VSyncWorker::CreateInstance(pipeline_);
    if (!vsync_worker_) {
      ALOGE("Failed to create event worker for d=%d\n", int(handle_));
      return HWC2::Error::BadDisplay;
    }
  }

  if (!IsInHeadlessMode()) {
    auto ret = BackendManager::GetInstance().SetBackendForDisplay(this);
    if (ret) {
      ALOGE("Failed to set backend for d=%d %d\n", int(handle_), ret);
      return HWC2::Error::BadDisplay;
    }
    auto flatcbk = (struct FlatConCallbacks){
        .trigger = [this]() { hwc_->SendRefreshEventToClient(handle_); }};
    flatcon_ = FlatteningController::CreateInstance(flatcbk);
  }

  HwcLayer::LayerProperties lp;
  lp.blend_mode = BufferBlendMode::kPreMult;
  client_layer_.SetLayerProperties(lp);

  SetColorMatrixToIdentity();

  return HWC2::Error::None;
}

std::optional<PanelOrientation> HwcDisplay::getDisplayPhysicalOrientation() {
  if (IsInHeadlessMode()) {
    // The pipeline can be nullptr in headless mode, so return the default
    // "normal" mode.
    return PanelOrientation::kModePanelOrientationNormal;
  }

  DrmDisplayPipeline &pipeline = GetPipe();
  if (pipeline.connector == nullptr || pipeline.connector->Get() == nullptr) {
    ALOGW(
        "No display pipeline present to query the panel orientation property.");
    return {};
  }

  return pipeline.connector->Get()->GetPanelOrientation();
}

HWC2::Error HwcDisplay::ChosePreferredConfig() {
  HWC2::Error err{};
  if (type_ == HWC2::DisplayType::Virtual) {
    configs_.GenFakeMode(virtual_disp_width_, virtual_disp_height_);
  } else if (!IsInHeadlessMode()) {
    err = configs_.Update(*pipeline_->connector->Get());
  } else {
    configs_.GenFakeMode(0, 0);
  }
  if (!IsInHeadlessMode() && err != HWC2::Error::None) {
    return HWC2::Error::BadDisplay;
  }

  return SetActiveConfig(configs_.preferred_config_id);
}

auto HwcDisplay::CreateLayer(ILayerId new_layer_id) -> bool {
  if (layers_.count(new_layer_id) > 0)
    return false;

  layers_.emplace(new_layer_id, HwcLayer(this));

  return true;
}

auto HwcDisplay::DestroyLayer(ILayerId layer_id) -> bool {
  auto count = layers_.erase(layer_id);
  return count != 0;
}

HWC2::Error HwcDisplay::GetActiveConfig(hwc2_config_t *config) const {
  // If a config has been queued, it is considered the "active" config.
  const HwcDisplayConfig *hwc_config = GetLastRequestedConfig();
  if (hwc_config == nullptr)
    return HWC2::Error::BadConfig;

  *config = hwc_config->id;
  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::GetColorModes(uint32_t *num_modes, int32_t *modes) {
  if (IsInHeadlessMode()) {
    *num_modes = 1;
    if (modes)
      modes[0] = HAL_COLOR_MODE_NATIVE;
    return HWC2::Error::None;
  }

  if (!modes) {
    std::vector<Colormode> temp_modes;
    GetEdid()->GetColorModes(temp_modes);
    *num_modes = temp_modes.size();
    return HWC2::Error::None;
  }

  std::vector<Colormode> temp_modes;
  std::vector<int32_t> out_modes(modes, modes + *num_modes);
  GetEdid()->GetColorModes(temp_modes);
  if (temp_modes.empty()) {
    out_modes.emplace_back(HAL_COLOR_MODE_NATIVE);
    return HWC2::Error::None;
  }

  for (auto &c : temp_modes)
    out_modes.emplace_back(static_cast<int32_t>(c));

  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::GetDisplayAttribute(hwc2_config_t config,
                                            int32_t attribute_in,
                                            int32_t *value) {
  int conf = static_cast<int>(config);

  if (configs_.hwc_configs.count(conf) == 0) {
    ALOGE("Could not find mode #%d", conf);
    return HWC2::Error::BadConfig;
  }

  auto &hwc_config = configs_.hwc_configs[conf];

  static const int32_t kUmPerInch = 25400;
  auto mm_width = configs_.mm_width;
  auto attribute = static_cast<HWC2::Attribute>(attribute_in);
  switch (attribute) {
    case HWC2::Attribute::Width:
      *value = static_cast<int>(hwc_config.mode.GetRawMode().hdisplay);
      break;
    case HWC2::Attribute::Height:
      *value = static_cast<int>(hwc_config.mode.GetRawMode().vdisplay);
      break;
    case HWC2::Attribute::VsyncPeriod:
      // in nanoseconds
      *value = hwc_config.mode.GetVSyncPeriodNs();
      break;
    case HWC2::Attribute::DpiY:
      *value = GetEdid()->GetDpiY();
      if (*value < 0) {
        // default to raw mode DpiX for both x and y when no good value
        // can be provided from edid.
        *value = mm_width ? int(hwc_config.mode.GetRawMode().hdisplay *
                                kUmPerInch / mm_width)
                          : -1;
      }
      break;
    case HWC2::Attribute::DpiX:
      // Dots per 1000 inches
      *value = GetEdid()->GetDpiX();
      if (*value < 0) {
        // default to raw mode DpiX for both x and y when no good value
        // can be provided from edid.
        *value = mm_width ? int(hwc_config.mode.GetRawMode().hdisplay *
                                kUmPerInch / mm_width)
                          : -1;
      }
      break;
#if __ANDROID_API__ > 29
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

HWC2::Error HwcDisplay::LegacyGetDisplayConfigs(uint32_t *num_configs,
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

HWC2::Error HwcDisplay::GetDisplayName(uint32_t *size, char *name) {
  std::ostringstream stream;
  if (IsInHeadlessMode()) {
    stream << "null-display";
  } else {
    stream << "display-" << GetPipe().connector->Get()->GetId();
  }
  auto string = stream.str();
  auto length = string.length();
  if (!name) {
    *size = length;
    return HWC2::Error::None;
  }

  *size = std::min<uint32_t>(static_cast<uint32_t>(length - 1), *size);
  strncpy(name, string.c_str(), *size);
  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::GetDisplayType(int32_t *type) {
  *type = static_cast<int32_t>(type_);
  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::GetHdrCapabilities(uint32_t *num_types, int32_t *types,
                                           float *max_luminance,
                                           float *max_average_luminance,
                                           float *min_luminance) {
  if (IsInHeadlessMode()) {
    *num_types = 0;
    return HWC2::Error::None;
  }

  if (!types) {
    std::vector<ui::Hdr> temp_types;
    float lums[3] = {0.F};
    GetEdid()->GetHdrCapabilities(temp_types, &lums[0], &lums[1], &lums[2]);
    *num_types = temp_types.size();
    return HWC2::Error::None;
  }

  std::vector<ui::Hdr> temp_types;
  std::vector<int32_t> out_types(types, types + *num_types);
  GetEdid()->GetHdrCapabilities(temp_types, max_luminance,
                                max_average_luminance, min_luminance);
  for (auto &t : temp_types) {
    switch (t) {
      case ui::Hdr::HDR10:
        out_types.emplace_back(HAL_HDR_HDR10);
        break;
      case ui::Hdr::HLG:
        out_types.emplace_back(HAL_HDR_HLG);
        break;
      default:
        // Ignore any other HDR types
        break;
    }
  }
  return HWC2::Error::None;
}

AtomicCommitArgs HwcDisplay::CreateModesetCommit(
    const HwcDisplayConfig *config,
    const std::optional<LayerData> &modeset_layer) {
  AtomicCommitArgs args{};

  args.color_matrix = color_matrix_;
  args.content_type = content_type_;
  args.colorspace = colorspace_;
  args.hdr_metadata = hdr_metadata_;

  std::vector<LayerData> composition_layers;
  if (modeset_layer) {
    composition_layers.emplace_back(modeset_layer.value());
  }

  if (composition_layers.empty()) {
    ALOGW("Attempting to create a modeset commit without a layer.");
  }

  args.display_mode = config->mode;
  args.active = true;
  args.composition = DrmKmsPlan::CreateDrmKmsPlan(GetPipe(),
                                                  std::move(
                                                      composition_layers));
  ALOGW_IF(!args.composition, "No composition for blocking modeset");

  return args;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
HWC2::Error HwcDisplay::CreateComposition(AtomicCommitArgs &a_args) {
  if (IsInHeadlessMode()) {
    ALOGE("%s: Display is in headless mode, should never reach here", __func__);
    return HWC2::Error::None;
  }

  a_args.color_matrix = color_matrix_;
  a_args.content_type = content_type_;
  a_args.colorspace = colorspace_;
  a_args.hdr_metadata = hdr_metadata_;

  uint32_t prev_vperiod_ns = 0;
  GetDisplayVsyncPeriod(&prev_vperiod_ns);

  std::optional<uint32_t> new_vsync_period_ns;
  if (staged_mode_config_id_ &&
      staged_mode_change_time_ <= ResourceManager::GetTimeMonotonicNs()) {
    const HwcDisplayConfig *staged_config = GetConfig(
        staged_mode_config_id_.value());
    if (staged_config == nullptr) {
      return HWC2::Error::BadConfig;
    }

    configs_.active_config_id = staged_mode_config_id_.value();
    a_args.display_mode = staged_config->mode;
    if (!a_args.test_only) {
      new_vsync_period_ns = staged_config->mode.GetVSyncPeriodNs();
    }
  }

  // order the layers by z-order
  bool use_client_layer = false;
  uint32_t client_z_order = UINT32_MAX;
  std::map<uint32_t, HwcLayer *> z_map;
  for (auto &[_, layer] : layers_) {
    switch (layer.GetValidatedType()) {
      case HWC2::Composition::Device:
        z_map.emplace(layer.GetZOrder(), &layer);
        break;
      case HWC2::Composition::Client:
        // Place it at the z_order of the lowest client layer
        use_client_layer = true;
        client_z_order = std::min(client_z_order, layer.GetZOrder());
        break;
      default:
        continue;
    }
  }
  if (use_client_layer) {
    z_map.emplace(client_z_order, &client_layer_);

    client_layer_.PopulateLayerData();
    if (!client_layer_.IsLayerUsableAsDevice()) {
      ALOGE_IF(!a_args.test_only,
               "Client layer must be always usable by DRM/KMS");
      /* This may be normally triggered on validation of the first frame
       * containing CLIENT layer. At this moment client buffer is not yet
       * provided by the CLIENT.
       * This may be triggered once in HwcLayer lifecycle in case FB can't be
       * imported. For example when non-contiguous buffer is imported into
       * contiguous-only DRM/KMS driver.
       */
      return HWC2::Error::BadLayer;
    }
  }

  if (z_map.empty())
    return HWC2::Error::BadLayer;

  std::vector<LayerData> composition_layers;

  /* Import & populate */
  for (std::pair<const uint32_t, HwcLayer *> &l : z_map) {
    l.second->PopulateLayerData();
  }

  // now that they're ordered by z, add them to the composition
  for (std::pair<const uint32_t, HwcLayer *> &l : z_map) {
    if (!l.second->IsLayerUsableAsDevice()) {
      return HWC2::Error::BadLayer;
    }
    composition_layers.emplace_back(l.second->GetLayerData());
  }

  /* Store plan to ensure shared planes won't be stolen by other display
   * in between of ValidateDisplay() and PresentDisplay() calls
   */
  current_plan_ = DrmKmsPlan::CreateDrmKmsPlan(GetPipe(),
                                               std::move(composition_layers));

  if (type_ == HWC2::DisplayType::Virtual) {
    writeback_layer_->PopulateLayerData();
    if (!writeback_layer_->IsLayerUsableAsDevice()) {
      ALOGE("Output layer must be always usable by DRM/KMS");
      return HWC2::Error::BadLayer;
    }
    a_args.writeback_fb = writeback_layer_->GetLayerData().fb;
    a_args.writeback_release_fence = writeback_layer_->GetLayerData()
                                         .acquire_fence;
  }

  if (!current_plan_) {
    ALOGE_IF(!a_args.test_only, "Failed to create DrmKmsPlan");
    return HWC2::Error::BadConfig;
  }

  a_args.composition = current_plan_;

  auto ret = GetPipe().atomic_state_manager->ExecuteAtomicCommit(a_args);

  if (ret) {
    ALOGE_IF(!a_args.test_only, "Failed to apply the frame composition ret=%d", ret);
    return HWC2::Error::BadParameter;
  }

  if (new_vsync_period_ns) {
    staged_mode_config_id_.reset();

    vsync_worker_->SetVsyncTimestampTracking(false);
    uint32_t last_vsync_ts = vsync_worker_->GetLastVsyncTimestamp();
    if (last_vsync_ts != 0) {
      hwc_->SendVsyncPeriodTimingChangedEventToClient(handle_,
                                                      last_vsync_ts +
                                                          prev_vperiod_ns);
    }
    vsync_worker_->SetVsyncPeriodNs(new_vsync_period_ns.value());
  }

  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::SetActiveConfigInternal(uint32_t config,
                                                int64_t change_time) {
  if (configs_.hwc_configs.count(config) == 0) {
    ALOGE("Could not find active mode for %u", config);
    return HWC2::Error::BadConfig;
  }

  staged_mode_change_time_ = change_time;
  staged_mode_config_id_ = config;

  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::SetActiveConfig(hwc2_config_t config) {
  return SetActiveConfigInternal(config, ResourceManager::GetTimeMonotonicNs());
}

HWC2::Error HwcDisplay::SetColorMode(int32_t mode) {
  /* Maps to the Colorspace DRM connector property:
   * https://elixir.bootlin.com/linux/v6.11/source/include/drm/drm_connector.h#L538
   */
  if (mode < HAL_COLOR_MODE_NATIVE || mode > HAL_COLOR_MODE_DISPLAY_BT2020)
    return HWC2::Error::BadParameter;

  switch (mode) {
    case HAL_COLOR_MODE_NATIVE:
      hdr_metadata_.reset();
      colorspace_ = Colorspace::kDefault;
      break;
    case HAL_COLOR_MODE_STANDARD_BT601_625:
    case HAL_COLOR_MODE_STANDARD_BT601_625_UNADJUSTED:
    case HAL_COLOR_MODE_STANDARD_BT601_525:
    case HAL_COLOR_MODE_STANDARD_BT601_525_UNADJUSTED:
      hdr_metadata_.reset();
      // The DP spec does not say whether this is the 525 or the 625 line version.
      colorspace_ = Colorspace::kBt601Ycc;
      break;
    case HAL_COLOR_MODE_STANDARD_BT709:
    case HAL_COLOR_MODE_SRGB:
      hdr_metadata_.reset();
      colorspace_ = Colorspace::kBt709Ycc;
      break;
    case HAL_COLOR_MODE_DCI_P3:
    case HAL_COLOR_MODE_DISPLAY_P3:
      hdr_metadata_.reset();
      colorspace_ = Colorspace::kDciP3RgbD65;
      break;
    case HAL_COLOR_MODE_DISPLAY_BT2020: {
      std::vector<ui::Hdr> hdr_types;
      GetEdid()->GetSupportedHdrTypes(hdr_types);
      if (!hdr_types.empty()) {
        auto ret = SetHdrOutputMetadata(hdr_types.front());
        if (ret != HWC2::Error::None)
          return ret;
      }
      colorspace_ = Colorspace::kBt2020Rgb;
      break;
    }
    case HAL_COLOR_MODE_ADOBE_RGB:
    case HAL_COLOR_MODE_BT2020:
    case HAL_COLOR_MODE_BT2100_PQ:
    case HAL_COLOR_MODE_BT2100_HLG:
    default:
      return HWC2::Error::Unsupported;
  }

  color_mode_ = mode;
  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::SetColorTransform(const float *matrix, int32_t hint) {
  if (hint < HAL_COLOR_TRANSFORM_IDENTITY ||
      hint > HAL_COLOR_TRANSFORM_CORRECT_TRITANOPIA)
    return HWC2::Error::BadParameter;

  if (!matrix && hint == HAL_COLOR_TRANSFORM_ARBITRARY_MATRIX)
    return HWC2::Error::BadParameter;

  color_transform_hint_ = static_cast<android_color_transform_t>(hint);

  if (IsInHeadlessMode())
    return HWC2::Error::None;

  if (!GetPipe().crtc->Get()->GetCtmProperty())
    return HWC2::Error::None;

  switch (color_transform_hint_) {
    case HAL_COLOR_TRANSFORM_IDENTITY:
      SetColorMatrixToIdentity();
      break;
    case HAL_COLOR_TRANSFORM_ARBITRARY_MATRIX:
      // Without HW support, we cannot correctly process matrices with an offset.
      {
        for (int i = 12; i < 14; i++) {
          if (matrix[i] != 0.F)
            return HWC2::Error::Unsupported;
        }
        std::array<float, 16> aidl_matrix = kIdentityMatrix;
        memcpy(aidl_matrix.data(), matrix, aidl_matrix.size() * sizeof(float));
        color_matrix_ = ToColorTransform(aidl_matrix);
      }
      break;
    default:
      return HWC2::Error::Unsupported;
  }

  return HWC2::Error::None;
}

bool HwcDisplay::CtmByGpu() {
  if (color_transform_hint_ == HAL_COLOR_TRANSFORM_IDENTITY)
    return false;

  if (GetPipe().crtc->Get()->GetCtmProperty())
    return false;

  if (GetHwc()->GetResMan().GetCtmHandling() == CtmHandling::kDrmOrIgnore)
    return false;

  return true;
}

HWC2::Error HwcDisplay::SetPowerMode(int32_t mode_in) {
  auto mode = static_cast<HWC2::PowerMode>(mode_in);

  AtomicCommitArgs a_args{};

  switch (mode) {
    case HWC2::PowerMode::Off:
      a_args.active = false;
      break;
    case HWC2::PowerMode::On:
      a_args.active = true;
      break;
    case HWC2::PowerMode::Doze:
    case HWC2::PowerMode::DozeSuspend:
      return HWC2::Error::Unsupported;
    default:
      ALOGE("Incorrect power mode value (%d)\n", mode_in);
      return HWC2::Error::BadParameter;
  }

  if (IsInHeadlessMode()) {
    return HWC2::Error::None;
  }

  if (a_args.active && *a_args.active) {
    /*
     * Setting the display to active before we have a composition
     * can break some drivers, so skip setting a_args.active to
     * true, as the next composition frame will implicitly activate
     * the display
     */
    return GetPipe().atomic_state_manager->ActivateDisplayUsingDPMS() == 0
               ? HWC2::Error::None
               : HWC2::Error::BadParameter;
  };

  auto err = GetPipe().atomic_state_manager->ExecuteAtomicCommit(a_args);
  if (err) {
    ALOGE("Failed to apply the dpms composition err=%d", err);
    return HWC2::Error::BadParameter;
  }
  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::SetVsyncEnabled(int32_t enabled) {
  if (type_ == HWC2::DisplayType::Virtual) {
    return HWC2::Error::None;
  }
  if (!vsync_worker_) {
    return HWC2::Error::NoResources;
  }

  vsync_event_en_ = HWC2_VSYNC_ENABLE == enabled;
  std::optional<VSyncWorker::VsyncTimestampCallback> callback = std::nullopt;
  if (vsync_event_en_) {
    DrmHwc *hwc = hwc_;
    hwc2_display_t id = handle_;
    // Callback will be called from the vsync thread.
    callback = [hwc, id](int64_t timestamp, uint32_t period_ns) {
      hwc->SendVsyncEventToClient(id, timestamp, period_ns);
    };
  }
  vsync_worker_->SetTimestampCallback(std::move(callback));
  return HWC2::Error::None;
}

std::vector<HwcLayer *> HwcDisplay::GetOrderLayersByZPos() {
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

HWC2::Error HwcDisplay::GetDisplayVsyncPeriod(
    uint32_t *outVsyncPeriod /* ns */) {
  return GetDisplayAttribute(configs_.active_config_id,
                             HWC2_ATTRIBUTE_VSYNC_PERIOD,
                             (int32_t *)(outVsyncPeriod));
}

// Display primary values are coded as unsigned 16-bit values in units of
// 0.00002, where 0x0000 represents zero and 0xC350 represents 1.0000.
static uint64_t ToU16ColorValue(float in) {
  constexpr float kPrimariesFixedPoint = 50000.F;
  return static_cast<uint64_t>(kPrimariesFixedPoint * in);
}

HWC2::Error HwcDisplay::SetHdrOutputMetadata(ui::Hdr type) {
  hdr_metadata_ = std::make_shared<hdr_output_metadata>();
  hdr_metadata_->metadata_type = 0;
  auto *m = &hdr_metadata_->hdmi_metadata_type1;
  m->metadata_type = 0;

  switch (type) {
    case ui::Hdr::HDR10:
      m->eotf = 2;  // PQ
      break;
    case ui::Hdr::HLG:
      m->eotf = 3;  // HLG
      break;
    default:
      return HWC2::Error::Unsupported;
  }

  // Most luminance values are coded as an unsigned 16-bit value in units of 1
  // cd/m2, where 0x0001 represents 1 cd/m2 and 0xFFFF represents 65535 cd/m2.
  std::vector<ui::Hdr> types;
  float hdr_luminance[3]{0.F, 0.F, 0.F};
  GetEdid()->GetHdrCapabilities(types, &hdr_luminance[0], &hdr_luminance[1],
                                &hdr_luminance[2]);
  m->max_display_mastering_luminance = m->max_cll = static_cast<uint64_t>(
      hdr_luminance[0]);
  m->max_fall = static_cast<uint64_t>(hdr_luminance[1]);
  // The min luminance value is coded as an unsigned 16-bit value in units of
  // 0.0001 cd/m2, where 0x0001 represents 0.0001 cd/m2 and 0xFFFF
  // represents 6.5535 cd/m2.
  m->min_display_mastering_luminance = static_cast<uint64_t>(hdr_luminance[2] *
                                                             10000.F);

  auto gamut = ColorGamut::BT2020();
  auto primaries = gamut.getPrimaries();
  m->display_primaries[0].x = ToU16ColorValue(primaries[0].x);
  m->display_primaries[0].y = ToU16ColorValue(primaries[0].y);
  m->display_primaries[1].x = ToU16ColorValue(primaries[1].x);
  m->display_primaries[1].y = ToU16ColorValue(primaries[1].y);
  m->display_primaries[2].x = ToU16ColorValue(primaries[2].x);
  m->display_primaries[2].y = ToU16ColorValue(primaries[2].y);

  auto whitePoint = gamut.getWhitePoint();
  m->white_point.x = ToU16ColorValue(whitePoint.x);
  m->white_point.y = ToU16ColorValue(whitePoint.y);

  return HWC2::Error::None;
}

#if __ANDROID_API__ > 29
HWC2::Error HwcDisplay::GetDisplayConnectionType(uint32_t *outType) {
  if (IsInHeadlessMode()) {
    *outType = static_cast<uint32_t>(HWC2::DisplayConnectionType::Internal);
    return HWC2::Error::None;
  }
  /* Primary display should be always internal,
   * otherwise SF will be unhappy and will crash
   */
  if (GetPipe().connector->Get()->IsInternal() || handle_ == kPrimaryDisplay)
    *outType = static_cast<uint32_t>(HWC2::DisplayConnectionType::Internal);
  else if (GetPipe().connector->Get()->IsExternal())
    *outType = static_cast<uint32_t>(HWC2::DisplayConnectionType::External);
  else
    return HWC2::Error::BadConfig;

  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::SetActiveConfigWithConstraints(
    hwc2_config_t config,
    hwc_vsync_period_change_constraints_t *vsyncPeriodChangeConstraints,
    hwc_vsync_period_change_timeline_t *outTimeline) {
  if (type_ == HWC2::DisplayType::Virtual) {
    return HWC2::Error::None;
  }

  if (vsyncPeriodChangeConstraints == nullptr || outTimeline == nullptr) {
    return HWC2::Error::BadParameter;
  }

  uint32_t current_vsync_period{};
  GetDisplayVsyncPeriod(&current_vsync_period);

  if (vsyncPeriodChangeConstraints->seamlessRequired) {
    return HWC2::Error::SeamlessNotAllowed;
  }

  outTimeline->refreshTimeNanos = vsyncPeriodChangeConstraints
                                      ->desiredTimeNanos -
                                  current_vsync_period;
  auto ret = SetActiveConfigInternal(config, outTimeline->refreshTimeNanos);
  if (ret != HWC2::Error::None) {
    return ret;
  }

  outTimeline->refreshRequired = true;
  outTimeline->newVsyncAppliedTimeNanos = vsyncPeriodChangeConstraints
                                              ->desiredTimeNanos;

  vsync_worker_->SetVsyncTimestampTracking(true);

  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::SetContentType(int32_t contentType) {
  /* Maps exactly to the content_type DRM connector property:
   * https://elixir.bootlin.com/linux/v6.11/source/include/uapi/drm/drm_mode.h#L107
   */
  if (contentType < HWC2_CONTENT_TYPE_NONE || contentType > HWC2_CONTENT_TYPE_GAME)
    return HWC2::Error::BadParameter;

  content_type_ = contentType;

  return HWC2::Error::None;
}
#endif

#if __ANDROID_API__ > 28
HWC2::Error HwcDisplay::GetDisplayIdentificationData(uint8_t *outPort,
                                                     uint32_t *outDataSize,
                                                     uint8_t *outData) {
  if (IsInHeadlessMode()) {
    return HWC2::Error::Unsupported;
  }

  auto *connector = GetPipe().connector->Get();
  auto blob = connector->GetEdidBlob();
  if (!blob) {
    return HWC2::Error::Unsupported;
  }

  constexpr uint8_t kDrmDeviceBitShift = 5U;
  constexpr uint8_t kDrmDeviceBitMask = 0xE0;
  constexpr uint8_t kConnectorBitMask = 0x1F;
  const auto kDrmIdx = static_cast<uint8_t>(
      connector->GetDev().GetIndexInDevArray());
  const auto kConnectorIdx = static_cast<uint8_t>(
      connector->GetIndexInResArray());
  *outPort = (((kDrmIdx << kDrmDeviceBitShift) & kDrmDeviceBitMask) |
              (kConnectorIdx & kConnectorBitMask));

  if (outData) {
    *outDataSize = std::min(*outDataSize, blob->length);
    memcpy(outData, blob->data, *outDataSize);
  } else {
    *outDataSize = blob->length;
  }

  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::GetDisplayCapabilities(uint32_t *outNumCapabilities,
                                               uint32_t *outCapabilities) {
  if (outNumCapabilities == nullptr) {
    return HWC2::Error::BadParameter;
  }

  bool skip_ctm = false;

  // Skip client CTM if user requested DRM_OR_IGNORE
  if (GetHwc()->GetResMan().GetCtmHandling() == CtmHandling::kDrmOrIgnore)
    skip_ctm = true;

  // Skip client CTM if DRM can handle it
  if (!skip_ctm && !IsInHeadlessMode() &&
      GetPipe().crtc->Get()->GetCtmProperty())
    skip_ctm = true;

  if (!skip_ctm) {
    *outNumCapabilities = 0;
    return HWC2::Error::None;
  }

  *outNumCapabilities = 1;
  if (outCapabilities) {
    outCapabilities[0] = HWC2_DISPLAY_CAPABILITY_SKIP_CLIENT_COLOR_TRANSFORM;
  }

  return HWC2::Error::None;
}

#endif /* __ANDROID_API__ > 28 */

#if __ANDROID_API__ > 27

HWC2::Error HwcDisplay::GetRenderIntents(
    int32_t mode, uint32_t *outNumIntents,
    int32_t * /*android_render_intent_v1_1_t*/ outIntents) {
  if (mode != HAL_COLOR_MODE_NATIVE) {
    return HWC2::Error::BadParameter;
  }

  if (outIntents == nullptr) {
    *outNumIntents = 1;
    return HWC2::Error::None;
  }
  *outNumIntents = 1;
  outIntents[0] = HAL_RENDER_INTENT_COLORIMETRIC;
  return HWC2::Error::None;
}

HWC2::Error HwcDisplay::SetColorModeWithIntent(int32_t mode, int32_t intent) {
  if (intent < HAL_RENDER_INTENT_COLORIMETRIC ||
      intent > HAL_RENDER_INTENT_TONE_MAP_ENHANCE)
    return HWC2::Error::BadParameter;

  if (intent != HAL_RENDER_INTENT_COLORIMETRIC)
    return HWC2::Error::Unsupported;

  auto err = SetColorMode(mode);
  if (err != HWC2::Error::None) return err;

  return HWC2::Error::None;
}

#endif /* __ANDROID_API__ > 27 */

const Backend *HwcDisplay::backend() const {
  return backend_.get();
}

void HwcDisplay::set_backend(std::unique_ptr<Backend> backend) {
  backend_ = std::move(backend);
}

bool HwcDisplay::NeedsClientLayerUpdate() const {
  return std::any_of(layers_.begin(), layers_.end(), [](const auto &pair) {
    const auto &layer = pair.second;
    return layer.GetSfType() == HWC2::Composition::Client ||
           layer.GetValidatedType() == HWC2::Composition::Client;
  });
}

}  // namespace android
