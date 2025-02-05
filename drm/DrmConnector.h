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

#ifndef ANDROID_DRM_CONNECTOR_H_
#define ANDROID_DRM_CONNECTOR_H_
#include <cstdint>
#include <xf86drmMode.h>


#include <string>
#include <vector>

#include <drm/drm_mode.h>

#include "DrmEncoder.h"
#include "DrmMode.h"
#include "DrmProperty.h"
#include "DrmUnique.h"
#include "utils/hdr_metadata_defs.h"
#include "utils/cta_hdr_defs.h"
#include "utils/hwcdefs.h"

namespace android {

class DrmDevice;

class DrmConnector : public PipelineBindable<DrmConnector> {
 public:
  static auto CreateInstance(DrmDevice &dev, uint32_t connector_id,
                             uint32_t index) -> std::unique_ptr<DrmConnector>;

  DrmConnector(const DrmProperty &) = delete;
  DrmConnector &operator=(const DrmProperty &) = delete;
  int UpdateLinkStatusProperty();
  int UpdateEdidProperty();
  auto GetEdidBlob() -> DrmModePropertyBlobUnique;

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
  void UpdateMultiRefreshRateModes(std::vector<DrmMode> &new_modes);

  auto &GetModes() const {
    return modes_;
  }

  auto &GetActiveMode() const {
    return active_mode_;
  }

  void SetActiveMode(DrmMode &mode);

  auto &GetDpmsProperty() const {
    return dpms_property_;
  }

  auto &GetCrtcIdProperty() const {
    return crtc_id_property_;
  }

  auto &GetEdidProperty() const {
    return edid_property_;
  }

  auto &GetHdrOpMetadataProp() const {
    return hdr_op_metadata_prop_;
  }

  auto &GetHdrMatedata() {
    return hdr_metadata_;
  }

  auto &GetHdcpProperty() const {
    return hdcp_id_property_;
  }

  auto &GetHdcpTypeProperty() const {
    return hdcp_type_property_;
  }

  auto IsConnected() const {
    return connector_->connection == DRM_MODE_CONNECTED;
  }

  bool IsHdrSupportedDevice();
  bool IsConnectorHdrCapable() {
    return edid_contains_hdr_tag_;
  }

  auto GetMmWidth() const {
    return connector_->mmWidth;
  }

  auto GetMmHeight() const {
    return connector_->mmHeight;
  };

  void GetHDRStaticMetadata(uint8_t *b, uint8_t length);
  uint16_t ColorPrimary(short val);
  void GetColorPrimaries(uint8_t *b, struct cta_display_color_primaries *primaries);
  void ParseCTAFromExtensionBlock(uint8_t *edid);
  bool GetHdrCapabilities(uint32_t *outNumTypes, int32_t *outTypes,
                                    float *outMaxLuminance,
                                    float *outMaxAverageLuminance,
                                    float *outMinLuminance);
  bool GetRenderIntents( uint32_t *outNumIntents, int32_t *outIntents);

  void PrepareHdrMetadata(hdr_md *layer_hdr_metadata,
                          struct hdr_output_metadata *final_hdr_metadata);
  void SetHDCPState(hwcomposer::HWCContentProtection state,
                    hwcomposer::HWCContentType content_type);
  const DrmProperty &link_status_property() const;
 private:
  DrmConnector(DrmModeConnectorUnique connector, DrmDevice *drm, uint32_t index)
      : connector_(std::move(connector)),
        drm_(drm),
        index_in_res_array_(index){};

  DrmModeConnectorUnique connector_;
  DrmDevice *const drm_;

  const uint32_t index_in_res_array_;

  DrmMode active_mode_;
  std::vector<DrmMode> modes_;

  DrmProperty dpms_property_;
  DrmProperty crtc_id_property_;
  DrmProperty edid_property_;
  DrmProperty writeback_pixel_formats_;
  DrmProperty writeback_fb_id_;
  DrmProperty writeback_out_fence_;
  DrmProperty link_status_property_;
  DrmProperty hdcp_id_property_;
  DrmProperty hdcp_type_property_;

  uint32_t preferred_mode_id_{};
  //hdr_output_metadata property
  DrmProperty hdr_op_metadata_prop_;

  bool edid_contains_hdr_tag_ = false;

  /* Display's color primaries */
  struct cta_display_color_primaries primaries_ = {};

  /* Display's static HDR metadata */
  struct cta_edid_hdr_metadata_static *display_hdrMd_ = nullptr;

  hdr_md hdr_metadata_;
};
}  // namespace android

#endif  // ANDROID_DRM_PLANE_H_
