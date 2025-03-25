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

#include <drm/drm_mode.h>
#define LOG_TAG "drmhwc"

#include "DrmConnector.h"
#include <cutils/properties.h>
#include <xf86drmMode.h>

#include <array>
#include <cerrno>
#include <cinttypes>
#include <cstdint>
#include <sstream>

#include "DrmDevice.h"
#include "compositor/DisplayInfo.h"
#include "utils/log.h"

#ifndef DRM_MODE_CONNECTOR_SPI
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define DRM_MODE_CONNECTOR_SPI 19
#endif

#ifndef DRM_MODE_CONNECTOR_USB
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define DRM_MODE_CONNECTOR_USB 20
#endif

namespace android {

constexpr size_t kTypesCount = 21;

auto DrmConnector::GetConnectorProperty(const char *prop_name,
                                        DrmProperty *property,
                                        bool is_optional) -> bool {
  auto err = drm_->GetProperty(GetId(), DRM_MODE_OBJECT_CONNECTOR, prop_name,
                               property);
  if (err == 0)
    return true;

  if (is_optional) {
    ALOGV("Could not get optional %s property from connector %d", prop_name,
          GetId());
  } else {
    ALOGE("Could not get %s property from connector %d", prop_name, GetId());
  }
  return false;
}

auto DrmConnector::CreateInstance(DrmDevice &dev, uint32_t connector_id,
                                  uint32_t index)
    -> std::unique_ptr<DrmConnector> {
  auto conn = MakeDrmModeConnectorUnique(*dev.GetFd(), connector_id);
  if (!conn) {
    ALOGE("Failed to get connector %d", connector_id);
    return {};
  }

  auto c = std::unique_ptr<DrmConnector>(
      new DrmConnector(std::move(conn), &dev, index));

  if (!c->Init()) {
    ALOGE("Failed to initialize connector %d", connector_id);
    return {};
  }

  return c;
}

auto DrmConnector::Init()-> bool {
  if (!GetConnectorProperty("DPMS", &dpms_property_) ||
      !GetConnectorProperty("CRTC_ID", &crtc_id_property_)) {
    return false;
  }

  UpdateEdidProperty();
#if HAS_LIBDISPLAY_INFO
  auto edid = LibdisplayEdidWrapper::Create(GetEdidBlob());
  edid_wrapper_ = edid ? std::move(edid) : std::make_unique<EdidWrapper>();
#else
  edid_wrapper_ = std::make_unique<EdidWrapper>();
#endif

  if (IsWriteback() &&
      (!GetConnectorProperty("WRITEBACK_PIXEL_FORMATS",
                             &writeback_pixel_formats_) ||
       !GetConnectorProperty("WRITEBACK_FB_ID", &writeback_fb_id_) ||
       !GetConnectorProperty("WRITEBACK_OUT_FENCE_PTR",
                             &writeback_out_fence_))) {
    return false;
  }

  if (GetOptionalConnectorProperty("Colorspace", &colorspace_property_)) {
    colorspace_property_.AddEnumToMap("Default", Colorspace::kDefault,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("SMPTE_170M_YCC", Colorspace::kSmpte170MYcc,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("BT709_YCC", Colorspace::kBt709Ycc,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("XVYCC_601", Colorspace::kXvycc601,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("XVYCC_709", Colorspace::kXvycc709,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("SYCC_601", Colorspace::kSycc601,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("opYCC_601", Colorspace::kOpycc601,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("opRGB", Colorspace::kOprgb,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("BT2020_CYCC", Colorspace::kBt2020Cycc,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("BT2020_RGB", Colorspace::kBt2020Rgb,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("BT2020_YCC", Colorspace::kBt2020Ycc,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("DCI-P3_RGB_D65", Colorspace::kDciP3RgbD65,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("DCI-P3_RGB_Theater", Colorspace::kDciP3RgbTheater,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("RGB_WIDE_FIXED", Colorspace::kRgbWideFixed,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("RGB_WIDE_FLOAT",
                                      Colorspace::kRgbWideFloat,
                                      colorspace_enum_map_);
    colorspace_property_.AddEnumToMap("BT601_YCC", Colorspace::kBt601Ycc,
                                      colorspace_enum_map_);
  }

  GetOptionalConnectorProperty("content type", &content_type_property_);

  GetOptionalConnectorProperty("HDR_OUTPUT_METADATA",
                               &hdr_output_metadata_property_);

  if (GetOptionalConnectorProperty("panel orientation", &panel_orientation_)) {
    panel_orientation_
        .AddEnumToMapReverse("Normal",
                             PanelOrientation::kModePanelOrientationNormal,
                             panel_orientation_enum_map_);
    panel_orientation_
        .AddEnumToMapReverse("Upside Down",
                             PanelOrientation::kModePanelOrientationBottomUp,
                             panel_orientation_enum_map_);
    panel_orientation_
        .AddEnumToMapReverse("Left Side Up",
                             PanelOrientation::kModePanelOrientationLeftUp,
                             panel_orientation_enum_map_);
    panel_orientation_
        .AddEnumToMapReverse("Right Side Up",
                             PanelOrientation::kModePanelOrientationRightUp,
                             panel_orientation_enum_map_);
  }

  return true;
}

int DrmConnector::UpdateEdidProperty() {
  return GetOptionalConnectorProperty("EDID", &edid_property_) ? 0 : -EINVAL;
}

auto DrmConnector::GetEdidBlob() -> DrmModePropertyBlobUnique {
  auto ret = UpdateEdidProperty();
  if (ret != 0) {
    return {};
  }

  auto blob_id = GetEdidProperty().GetValue();
  if (!blob_id) {
    return {};
  }

  return MakeDrmModePropertyBlobUnique(*drm_->GetFd(), *blob_id);
}

bool DrmConnector::IsInternal() const {
  auto type = connector_->connector_type;
  return type == DRM_MODE_CONNECTOR_Unknown ||
         type == DRM_MODE_CONNECTOR_LVDS || type == DRM_MODE_CONNECTOR_eDP ||
         type == DRM_MODE_CONNECTOR_DSI || type == DRM_MODE_CONNECTOR_VIRTUAL ||
         type == DRM_MODE_CONNECTOR_DPI || type == DRM_MODE_CONNECTOR_SPI;
}

bool DrmConnector::IsExternal() const {
  auto type = connector_->connector_type;
  return type == DRM_MODE_CONNECTOR_HDMIA ||
         type == DRM_MODE_CONNECTOR_DisplayPort ||
         type == DRM_MODE_CONNECTOR_DVID || type == DRM_MODE_CONNECTOR_DVII ||
         type == DRM_MODE_CONNECTOR_VGA || type == DRM_MODE_CONNECTOR_USB;
}

bool DrmConnector::IsWriteback() const {
#ifdef DRM_MODE_CONNECTOR_WRITEBACK
  return connector_->connector_type == DRM_MODE_CONNECTOR_WRITEBACK;
#else
  return false;
#endif
}

bool DrmConnector::IsValid() const {
  return IsInternal() || IsExternal() || IsWriteback();
}

std::string DrmConnector::GetName() const {
  constexpr std::array<const char *, kTypesCount> kNames =
      {"None",      "VGA",  "DVI-I",     "DVI-D",   "DVI-A", "Composite",
       "SVIDEO",    "LVDS", "Component", "DIN",     "DP",    "HDMI-A",
       "HDMI-B",    "TV",   "eDP",       "Virtual", "DSI",   "DPI",
       "Writeback", "SPI",  "USB"};

  if (connector_->connector_type < kTypesCount) {
    std::ostringstream name_buf;
    name_buf << kNames[connector_->connector_type] << "-"
             << connector_->connector_type_id;
    return name_buf.str();
  }

  ALOGE("Unknown type in connector %d, could not make his name", GetId());
  return "None";
}

int DrmConnector::UpdateModes() {
  drm_->ResetModeId();
  auto conn = MakeDrmModeConnectorUnique(*drm_->GetFd(), GetId());
  if (!conn) {
    ALOGE("Failed to get connector %d", GetId());
    return -ENODEV;
  }
  connector_ = std::move(conn);

  int32_t connector_id;
  int32_t mode_id;

  char property[PROPERTY_VALUE_MAX];
  memset(property, 0 , PROPERTY_VALUE_MAX);
  property_get("vendor.hwcomposer.connector.id", property, "-1");
  connector_id = atoi(property);
  ALOGD("The property 'vendor.hwcomposer.connector.id' value is %d", connector_id);

  memset(property, 0 , PROPERTY_VALUE_MAX);
  property_get("vendor.hwcomposer.mode.id", property, "-1");
  mode_id = atoi(property);
  ALOGD("The property 'vendor.hwcomposer.mode.id' value is %d", mode_id);

  bool preferred_mode_found = false;
  std::vector<DrmMode> new_modes;

  if (mode_id <= 0 || mode_id > connector_->count_modes)
    mode_id = -1;

  bool have_preferred_mode = false;
  for (int i = 0; i < connector_->count_modes; ++i) {
    if (connector_->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
      have_preferred_mode = true;
      break;
    }
  }
  for (int i = 0; i < connector_->count_modes; ++i) {
    if (connector_->connector_id == connector_id) {
      if (mode_id == -1) {
        if (drm_->preferred_mode_limit_) {
          if (have_preferred_mode) {
            if (!(connector_->modes[i].type & DRM_MODE_TYPE_PREFERRED)) {
              drm_->GetNextModeId();
              continue;
            }
          } else {
            have_preferred_mode = true;
        }
        }
      } else {
        if (mode_id != (i + 1)) {
          drm_->GetNextModeId();
          continue;
        }
      }
    } else {
      if (connector_id != -1) {
        if (drm_->preferred_mode_limit_) {
          if (have_preferred_mode) {
            if (!(connector_->modes[i].type & DRM_MODE_TYPE_PREFERRED)) {
              drm_->GetNextModeId();
              continue;
            }
          } else {
            have_preferred_mode = true;
          }
        }
      }
    }

    bool exists = false;
    for (const DrmMode &mode : modes_) {
      if (mode == connector_->modes[i]) {
        new_modes.push_back(mode);
        exists = true;
        ALOGD("CONNECTOR:%d select one mode, id = %d, name = %s, refresh = %f",
              GetId(), mode.id(), mode.GetName().c_str(), mode.GetVRefresh());
        break;
      }
    }

    if (!exists) {
      DrmMode m(&connector_->modes[i]);
       m.SetId(drm_->GetNextModeId());
       new_modes.push_back(m);
       ALOGD("CONNECTOR:%d select one mode, id = %d, name = %s, refresh = %f",
             GetId(), m.id(), m.GetName().c_str(), m.GetVRefresh());
    }
    if (!preferred_mode_found &&
        (new_modes.back().GetRawMode().type & DRM_MODE_TYPE_PREFERRED)) {
      preferred_mode_id_ = new_modes.back().id();
      preferred_mode_found = true;
      ALOGD("CONNECTOR:%d preferred mode found, set preferred mode id = %d, name "
            "= %s, refresh = %f",
          GetId(), preferred_mode_id_, new_modes.back().GetName().c_str(),
          new_modes.back().GetVRefresh());
    }
  }

  UpdateMultiRefreshRateModes(new_modes);

  modes_.swap(new_modes);
  if (!preferred_mode_found && !modes_.empty()) {
    preferred_mode_id_ = modes_[0].id();
    ALOGD("CONNECTOR:%d preferred mode not found, set preferred mode id = %d, "
          "name = %s, refresh = %f",
        GetId(), preferred_mode_id_, modes_[0].GetName().c_str(),
        modes_[0].GetVRefresh());
  }

  return 0;
}

void DrmConnector::UpdateMultiRefreshRateModes(std::vector<DrmMode> &new_modes) {
  if (new_modes.size() == 1 && connector_->count_modes > 0) {
      DrmMode mode = new_modes[0];
      drm_->ResetModeId();
      new_modes.clear();
      for(int i = 0; i < connector_->count_modes; ++i) {
          drmModeModeInfo info = connector_->modes[i];
          if (info.hdisplay == mode.GetRawMode().hdisplay && info.vdisplay == mode.GetRawMode().vdisplay) {
            DrmMode mode(&info);
            mode.SetId(drm_->GetNextModeId());
            new_modes.push_back(mode);
          }
      }
  }
}

bool DrmConnector::IsLinkStatusGood() {
  if (GetConnectorProperty("link-status", &link_status_property_, false)) {
    auto link_status_property_value = link_status_property_.GetValue();
    if (link_status_property_value &&
        (link_status_property_value == DRM_MODE_LINK_STATUS_BAD))
      return false;
  }

  return true;
}

std::optional<PanelOrientation> DrmConnector::GetPanelOrientation() {
  if (!panel_orientation_.GetValue().has_value()) {
    ALOGW("No panel orientation property available.");
    return {};
  }

  /* The value_or(0) satisfies the compiler warning. However,
   * panel_orientation_.GetValue() is guaranteed to have a value since we check
   * has_value() and return early otherwise.
   */
  uint64_t panel_orientation_value = panel_orientation_.GetValue().value_or(0);

  if (panel_orientation_enum_map_.count(panel_orientation_value) == 1) {
    return panel_orientation_enum_map_[panel_orientation_value];
  }

  ALOGE("Unknown panel orientation: panel_orientation = %" PRIu64,
        panel_orientation_value);
  return {};
}

}  // namespace android
