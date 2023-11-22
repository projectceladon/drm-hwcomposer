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

#undef NDEBUG
#define LOG_NDEBUG 0
#define LOG_TAG "hwc-drm-connector"

#include "DrmConnector.h"

#include <xf86drmMode.h>
#include <cutils/properties.h>
#include <array>
#include <cerrno>
#include <cstdint>
#include <sstream>

#include "DrmDevice.h"
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

static bool GetOptionalConnectorProperty(const DrmDevice &dev,
                                         const DrmConnector &connector,
                                         const char *prop_name,
                                         DrmProperty *property) {
  return dev.GetProperty(connector.GetId(), DRM_MODE_OBJECT_CONNECTOR,
                         prop_name, property) == 0;
}

static bool GetConnectorProperty(const DrmDevice &dev,
                                 const DrmConnector &connector,
                                 const char *prop_name, DrmProperty *property) {
  if (!GetOptionalConnectorProperty(dev, connector, prop_name, property)) {
    ALOGE("Could not get %s property\n", prop_name);
    return false;
  }
  return true;
}

auto DrmConnector::CreateInstance(DrmDevice &dev, uint32_t connector_id,
                                  uint32_t index)
    -> std::unique_ptr<DrmConnector> {
  auto conn = MakeDrmModeConnectorUnique(dev.GetFd(), connector_id);
  if (!conn) {
    ALOGE("Failed to get connector %d", connector_id);
    return {};
  }

  auto c = std::unique_ptr<DrmConnector>(
      new DrmConnector(std::move(conn), &dev, index));

  if (!GetConnectorProperty(dev, *c, "DPMS", &c->dpms_property_) ||
      !GetConnectorProperty(dev, *c, "CRTC_ID", &c->crtc_id_property_)) {
    return {};
  }

  c->UpdateEdidProperty();

  if (c->IsWriteback() &&
      (!GetConnectorProperty(dev, *c, "WRITEBACK_PIXEL_FORMATS",
                             &c->writeback_pixel_formats_) ||
       !GetConnectorProperty(dev, *c, "WRITEBACK_FB_ID",
                             &c->writeback_fb_id_) ||
       !GetConnectorProperty(dev, *c, "WRITEBACK_OUT_FENCE_PTR",
                             &c->writeback_out_fence_))) {
    return {};
  }

  return c;
}

int DrmConnector::UpdateLinkStatusProperty() {
  int ret = GetConnectorProperty(*drm_, *this, "link-status",
                                 &link_status_property_);
  if (!ret) {
    ALOGW("Conn %u Could not get link-status property\n", GetId());
  }
  return ret;
}

int DrmConnector::UpdateEdidProperty() {
  return GetOptionalConnectorProperty(*drm_, *this, "EDID", &edid_property_)
             ? 0
             : -EINVAL;
}

auto DrmConnector::GetEdidBlob() -> DrmModePropertyBlobUnique {
  uint64_t blob_id = 0;
  int ret = UpdateEdidProperty();
  if (ret != 0) {
    return {};
  }

  std::tie(ret, blob_id) = GetEdidProperty().value();
  if (ret != 0) {
    return {};
  }

  return MakeDrmModePropertyBlobUnique(drm_->GetFd(), blob_id);
}

bool DrmConnector::IsInternal() const {
  auto type = connector_->connector_type;
  return type == DRM_MODE_CONNECTOR_LVDS || type == DRM_MODE_CONNECTOR_eDP ||
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
  auto conn = MakeDrmModeConnectorUnique(drm_->GetFd(), GetId());
  if (!conn) {
    ALOGE("Failed to get connector %d", GetId());
    return -ENODEV;
  }
  connector_ = std::move(conn);
  int32_t connector_id;
  int32_t mode_id;

  int32_t disable_safe_mode;

  char property[PROPERTY_VALUE_MAX];
  memset(property, 0 , PROPERTY_VALUE_MAX);
  property_get("vendor.hwcomposer.connector.id", property, "-1");
  connector_id = atoi(property);
  ALOGD("The property 'vendor.hwcomposer.connector.id' value is %d", connector_id);

  memset(property, 0 , PROPERTY_VALUE_MAX);
  property_get("vendor.hwcomposer.mode.id", property, "-1");
  mode_id = atoi(property);
  ALOGD("The property 'vendor.hwcomposer.mode.id' value is %d", mode_id);

  memset(property, 0 , PROPERTY_VALUE_MAX);
  property_get("vendor.hwcomposer.disable.safe_mode", property, "0");
  disable_safe_mode= atoi(property);
  ALOGD("The property 'vendor.hwcomposer.disable.safe_mode' value is %d", disable_safe_mode);

  bool preferred_mode_found = false;
  std::vector<DrmMode> new_modes;

  if (mode_id <= 0 || mode_id > connector_->count_modes)
    mode_id = -1;

  bool have_preferred_mode = false;
  if (disable_safe_mode) {
    for (int i = 0; i < connector_->count_modes; ++i) {
      if (connector_->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
        have_preferred_mode = true;
        break;
      }
    }
  }

  /*
   *  WA to use the downgraded resolution for A14 on RPL to avoid screen blink issue.
   *  The real root cause of the blink is under invetigating.
   */
#if PLATFORM_SDK_VERSION != 34
   disable_safe_mode = 1;
   ALOGE("API level is not 34(Android 14), don't use resolution safe mode!");
#else
   ALOGE("API level is 34 and keeps the safe mode resolution property no changed:%d", disable_safe_mode);
#endif

  bool has_the_safe_resolution = false;
  int32_t lowest_fps = 60;
  for (int i = 0; i < connector_->count_modes; ++i) {
      ALOGE("connector mode id: %d, hdisplay:%d, vdisplay:%d, vrefresh:%d",
	     i, connector_->modes[i].hdisplay, connector_->modes[i].vdisplay, connector_->modes[i].vrefresh);

      if (connector_->modes[i].hdisplay == 1920 && connector_->modes[i].vdisplay == 1080
          && connector_->modes[i].vrefresh >= 30) {
	  has_the_safe_resolution = true;
	  if (lowest_fps > connector_->modes[i].vrefresh) {
	      lowest_fps = connector_->modes[i].vrefresh;
	  }
          ALOGE("the matched mode: %d, fps:%d", i, connector_->modes[i].vrefresh);
      }
  }

  ALOGE("has_the_safe_resolution: %d, lowest_fps:%d", has_the_safe_resolution, lowest_fps);

  bool added_safe_resolution = false;

  for (int i = 0; i < connector_->count_modes; ++i) {
    ALOGE("connector mode id: %d, hdisplay:%d, vdisplay:%d, vrefresh:%d",
	  i, connector_->modes[i].hdisplay, connector_->modes[i].vdisplay, connector_->modes[i].vrefresh);

    if (!disable_safe_mode && has_the_safe_resolution) {
      if (connector_->modes[i].hdisplay == 1920 && connector_->modes[i].vdisplay == 1080
          && connector_->modes[i].vrefresh == lowest_fps && !added_safe_resolution) {
        ALOGE("the matched mode: %d", i);
	added_safe_resolution = true;
      }
      else {
        ALOGE("The mode id:%d is above 1080p@%dfps, need to skip it.", i, lowest_fps);
        if (has_the_safe_resolution) {
          drm_->GetNextModeId();
          continue;
        }
      }
    }

    if (drm_->preferred_mode_limit_ && connector_id == -1) {
      if (have_preferred_mode) {
        if (!(connector_->modes[i].type & DRM_MODE_TYPE_PREFERRED)) {
          drm_->GetNextModeId();
          continue;
        }
      } else {
        if (disable_safe_mode || !has_the_safe_resolution) {
          have_preferred_mode = true;
	}
      }
    }
    if (connector_->connector_id == connector_id) {
      if (mode_id == -1) {
        if (drm_->preferred_mode_limit_) {
          if (have_preferred_mode) {
            if (!(connector_->modes[i].type & DRM_MODE_TYPE_PREFERRED)) {
              drm_->GetNextModeId();
              continue;
            }
          } else {
            if (disable_safe_mode || !has_the_safe_resolution) {
              have_preferred_mode = true;
	    }
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
            if (disable_safe_mode || !has_the_safe_resolution) {
              have_preferred_mode = true;
            }
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
              GetId(), mode.id(), mode.name().c_str(), mode.v_refresh());
        break;
      }
    }

    if (!exists) {
      DrmMode m(&connector_->modes[i]);
      m.SetId(drm_->GetNextModeId());
      new_modes.push_back(m);
      ALOGD("CONNECTOR:%d select one mode, id = %d, name = %s, refresh = %f",
	     GetId(), m.id(), m.name().c_str(), m.v_refresh());
    }
    if (!preferred_mode_found &&
        (new_modes.back().type() & DRM_MODE_TYPE_PREFERRED)) {
      preferred_mode_id_ = new_modes.back().id();
      preferred_mode_found = true;
      ALOGD("CONNECTOR:%d preferred mode found, set preferred mode id = %d, name "
            "= %s, refresh = %f",
          GetId(), preferred_mode_id_, new_modes.back().name().c_str(),
          new_modes.back().v_refresh());
    }
  }

  ALOGE("Disable the multi mode to avoid blink on A14 + RPL NUC.");
  //UpdateMultiRefreshRateModes(new_modes);

  modes_.swap(new_modes);
  if (!preferred_mode_found && !modes_.empty()) {
    preferred_mode_id_ = modes_[0].id();
    ALOGD("CONNECTOR:%d preferred mode not found, set preferred mode id = %d, "
          "name = %s, refresh = %f",
        GetId(), preferred_mode_id_, modes_[0].name().c_str(),
        modes_[0].v_refresh());
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
          if (info.hdisplay == mode.h_display() && info.vdisplay == mode.v_display()) {
            DrmMode mode(&info);
            mode.SetId(drm_->GetNextModeId());
            new_modes.push_back(mode);
          }
      }
  }
}

void DrmConnector::SetActiveMode(DrmMode &mode) {
  active_mode_ = mode;
}

const DrmProperty &DrmConnector::link_status_property() const {
  return link_status_property_;
}
}  // namespace android
