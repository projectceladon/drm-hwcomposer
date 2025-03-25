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

#pragma once

#include <xf86drmMode.h>

#include <cstdint>
#include <string>
#include <vector>

#include "DrmEncoder.h"
#include "DrmMode.h"
#include "DrmProperty.h"
#include "DrmUnique.h"
#include "compositor/DisplayInfo.h"
#include "utils/EdidWrapper.h"

namespace android {

class DrmDevice;

using EdidWrapperUnique = std::unique_ptr<EdidWrapper>;

class DrmConnector : public PipelineBindable<DrmConnector> {
 public:
  static auto CreateInstance(DrmDevice &dev, uint32_t connector_id,
                             uint32_t index) -> std::unique_ptr<DrmConnector>;

  DrmConnector(const DrmProperty &) = delete;
  DrmConnector &operator=(const DrmProperty &) = delete;

  int UpdateEdidProperty();
  auto GetEdidBlob() -> DrmModePropertyBlobUnique;
  auto GetParsedEdid() -> EdidWrapperUnique & {
    return edid_wrapper_;
  }

  auto GetDev() const -> DrmDevice & {
    return *drm_;
  }

  auto GetId() const {
    return connector_->connector_id;
  }

  auto GetIndexInResArray() const {
    return index_in_res_array_;
  }

  auto GetCurrentEncoderId() const {
    return connector_->encoder_id;
  }

  auto SupportsEncoder(DrmEncoder &enc) const {
    for (int i = 0; i < connector_->count_encoders; i++) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      if (connector_->encoders[i] == enc.GetId()) {
        return true;
      }
    }

    return false;
  }

  bool IsInternal() const;
  bool IsExternal() const;
  bool IsWriteback() const;
  bool IsValid() const;

  std::string GetName() const;

  int UpdateModes();

  bool IsLinkStatusGood();

  auto &GetModes() const {
    return modes_;
  }

  auto &GetDpmsProperty() const {
    return dpms_property_;
  }

  auto &GetCrtcIdProperty() const {
    return crtc_id_property_;
  }

  auto &GetEdidProperty() const {
    return edid_property_;
  }

  auto &GetColorspaceProperty() const {
    return colorspace_property_;
  }

  auto GetColorspacePropertyValue(Colorspace c) {
    return colorspace_enum_map_[c];
  }

  auto &GetContentTypeProperty() const {
    return content_type_property_;
  }

  auto &GetHdrOutputMetadataProperty() const {
    return hdr_output_metadata_property_;
  }

  auto &GetWritebackFbIdProperty() const {
    return writeback_fb_id_;
  }

  auto &GetWritebackOutFenceProperty() const {
    return writeback_out_fence_;
  }

  auto &GetPanelOrientationProperty() const {
    return panel_orientation_;
  }

  auto IsConnected() const {
    return connector_->connection == DRM_MODE_CONNECTED;
  }

  auto GetMmWidth() const {
    return connector_->mmWidth;
  }

  auto GetMmHeight() const {
    return connector_->mmHeight;
  };

  auto GetPanelOrientation() -> std::optional<PanelOrientation>;

 private:
  DrmConnector(DrmModeConnectorUnique connector, DrmDevice *drm, uint32_t index)
      : connector_(std::move(connector)),
        drm_(drm),
        index_in_res_array_(index) {};

  DrmModeConnectorUnique connector_;
  DrmDevice *const drm_;

  auto Init() -> bool;
  auto GetConnectorProperty(const char *prop_name, DrmProperty *property,
                            bool is_optional = false) -> bool;
  auto GetOptionalConnectorProperty(const char *prop_name,
                                    DrmProperty *property) -> bool {
    return GetConnectorProperty(prop_name, property, /*is_optional=*/true);
  }

  EdidWrapperUnique edid_wrapper_;

  const uint32_t index_in_res_array_;

  std::vector<DrmMode> modes_;

  DrmProperty dpms_property_;
  DrmProperty crtc_id_property_;
  DrmProperty edid_property_;
  DrmProperty colorspace_property_;
  DrmProperty content_type_property_;
  DrmProperty hdr_output_metadata_property_;

  DrmProperty link_status_property_;
  DrmProperty writeback_pixel_formats_;
  DrmProperty writeback_fb_id_;
  DrmProperty writeback_out_fence_;
  DrmProperty panel_orientation_;

  std::map<Colorspace, uint64_t> colorspace_enum_map_;
  std::map<uint64_t, PanelOrientation> panel_orientation_enum_map_;
  uint32_t preferred_mode_id_{};
};
}  // namespace android
