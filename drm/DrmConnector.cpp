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

#define LOG_TAG "hwc-drm-connector"

#include "DrmConnector.h"

#include <xf86drmMode.h>
#include <cutils/properties.h>
#include <array>
#include <cerrno>
#include <cstdint>
#include <sstream>
#include <math.h>

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

  if (!GetConnectorProperty(dev, *c, "Content Protection", &c->hdcp_id_property_)) {
      ALOGE("%s GetHDCPConnectorProperty check failed!", __FUNCTION__);
      return {};
  }

  if (!GetConnectorProperty(dev, *c, "HDCP Content Type", &c->hdcp_type_property_)) {
      ALOGE("%s GetHDCPTypeProperty check failed!", __FUNCTION__);
      return {};
  }

  if (!GetConnectorProperty(dev, *c, "DPMS", &c->dpms_property_) ||
      !GetConnectorProperty(dev, *c, "CRTC_ID", &c->crtc_id_property_) ||
      (dev.IsHdrSupportedDevice() && !GetConnectorProperty(dev, *c, "HDR_OUTPUT_METADATA", &c->hdr_op_metadata_prop_))) {
      ALOGE("%s GetConnectorProperty check failed!", __FUNCTION__);
      return {};
  }

  c->hdr_metadata_.valid = false;

  int edid_valid = c->UpdateEdidProperty();

  // Starts to parse HDR meta data at the connecotr initialization stage,so
  // we know if the connector supports HDR or not. This will help to report
  // HDR capabilities to surfaceflinger correctly in later HWC API calls.
  if (edid_valid >=0 && c->IsHdrSupportedDevice()) {
    auto blob = c->GetEdidBlob();
    if (!blob) {
       ALOGE("%s Failed to get edid property value.", __FUNCTION__);
    } else {
      c->ParseCTAFromExtensionBlock((uint8_t*)blob->data);
    }
  }

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

bool DrmConnector::IsHdrSupportedDevice()
{
  return drm_->IsHdrSupportedDevice();
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
    
        if (drm_->preferred_mode_limit_ && connector_id == -1) {
      if (have_preferred_mode) {
        if (!(connector_->modes[i].type & DRM_MODE_TYPE_PREFERRED)) {
          drm_->GetNextModeId();
          continue;
        }
      } else {
        have_preferred_mode = true;
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

  UpdateMultiRefreshRateModes(new_modes);

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

uint16_t DrmConnector::ColorPrimary(short val) {
  short temp = val & 0x3FF;
  short count = 1;
  float result = 0;
  uint16_t output;

  /* Primary values in EDID are ecoded in 10 bit format, where every bit
   * represents 2 pow negative bit position, ex 0.500 = 1/2 = 2 ^ -1 = (1 << 9)
   */
  while (temp) {
    result += ((!!(temp & (1 << 9))) * pow(2, -count));
    count++;
    temp <<= 1;
  }

  /* Primaries are to represented in uint16 format, in power of 0.00002,
   *     * max allowed value is 50,000 */
  output = result * 50000;
  if (output > 50000)
    output = 50000;

  return output;
}

void DrmConnector::GetHDRStaticMetadata(uint8_t *b, uint8_t length) {

  if (length < 2) {
    ALOGE("Invalid metadata input to static parser\n");
    return;
  }

  display_hdrMd_ = (struct cta_edid_hdr_metadata_static *)malloc(
      sizeof(struct cta_edid_hdr_metadata_static));
  if (!display_hdrMd_) {
    ALOGE("OOM while parsing static metadata\n");
    return;
  }
  memset(display_hdrMd_, 0, sizeof(struct cta_edid_hdr_metadata_static));

  ALOGD("Found HDR Static Metadata in EDID extension block.");
  display_hdrMd_->eotf = b[0] & 0x3F;
  display_hdrMd_->metadata_type = b[1];

  edid_contains_hdr_tag_ = true;

  if (length > 2 && length < 6) {
    display_hdrMd_->desired_max_ll = b[2];
    display_hdrMd_->desired_max_fall = b[3];
    display_hdrMd_->desired_min_ll = b[4];

    if (!display_hdrMd_->desired_max_ll)
      display_hdrMd_->desired_max_ll = 0xFF;
  }
  return;
}

#define HIGH_X(val) (val >> 6)
#define HIGH_Y(val) ((val >> 4) & 0x3)
#define LOW_X(val) ((val >> 2) & 0x3)
#define LOW_Y(val) ((val >> 4) & 0x3)


void DrmConnector::GetColorPrimaries( uint8_t *b, struct cta_display_color_primaries *p) {
  uint8_t rxrygxgy_0_1;
  uint8_t bxbywxwy_0_1;
  uint8_t count = 0x19; /* base of chromaticity block values */
  uint16_t val;

  if (!b || !p)
    return;

  rxrygxgy_0_1 = b[count++];
  bxbywxwy_0_1 = b[count++];

  val = (b[count++] << 2) | HIGH_X(rxrygxgy_0_1);
  p->display_primary_r_x = ColorPrimary(val);

  val = (b[count++] << 2) | HIGH_Y(rxrygxgy_0_1);
  p->display_primary_r_y = ColorPrimary(val);

  val = (b[count++] << 2) | LOW_X(rxrygxgy_0_1);
  p->display_primary_g_x = ColorPrimary(val);

  val = (b[count++] << 2) | LOW_Y(rxrygxgy_0_1);
  p->display_primary_g_y = ColorPrimary(val);

  val = (b[count++] << 2) | HIGH_X(bxbywxwy_0_1);
  p->display_primary_b_x = ColorPrimary(val);

  val = (b[count++] << 2) | HIGH_Y(bxbywxwy_0_1);
  p->display_primary_b_y = ColorPrimary(val);

  val = (b[count++] << 2) | LOW_X(bxbywxwy_0_1);
  p->white_point_x = ColorPrimary(val);

  val = (b[count++] << 2) | LOW_X(bxbywxwy_0_1);
  p->white_point_y = ColorPrimary(val);
}

void DrmConnector::ParseCTAFromExtensionBlock(uint8_t *edid) {
  int current_block;
  uint8_t *cta_ext_blk;
  uint8_t dblen;
  uint8_t d;
  uint8_t *cta_db_start;
  uint8_t *cta_db_end;
  uint8_t *dbptr;
  uint8_t tag;

  int num_blocks = edid[126];
  if (!num_blocks) {
    return;
  }

  for (current_block = 1; current_block <= num_blocks; current_block++) {
    cta_ext_blk = edid + 128 * current_block;
    if (cta_ext_blk[0] != CTA_EXTENSION_TAG)
      continue;
    d = cta_ext_blk[2];
    cta_db_start = cta_ext_blk + 4;
    cta_db_end = cta_ext_blk + d - 1;
    for (dbptr = cta_db_start; dbptr < cta_db_end; dbptr++) {
      tag = dbptr[0] >> 0x05;
      dblen = dbptr[0] & 0x1F;

      // Check if the extension has an extended block
      if (tag == CTA_EXTENDED_TAG_CODE) {
        switch (dbptr[1]) {
          case CTA_COLORIMETRY_CODE:
            ALOGE(" Colorimetry Data block\n");
            break;
          case CTA_HDR_STATIC_METADATA:
            ALOGE(" HDR STATICMETADATA block\n");
            DrmConnector::GetHDRStaticMetadata(dbptr + 2, dblen - 1);
            break;
          default:
            ALOGE(" Unknown tag/Parsing option:%x\n", dbptr[1]);
        }
        DrmConnector::GetColorPrimaries(dbptr + 2, &primaries_);
      }
    }
  }
}

bool DrmConnector::GetHdrCapabilities(uint32_t *outNumTypes, int32_t *outTypes,
                                    float *outMaxLuminance,
                                    float *outMaxAverageLuminance,
                                    float *outMinLuminance) {
  if (NULL == outNumTypes) {
    ALOGE("outNumTypes couldn't be NULL!");
    return false;
  }

  if (NULL == outTypes) {
    ALOGE("outTypes couldn't be NULL!");
    //TODO: clarify SF's logic here
    //kindly skip this check now and return nothing if it's NULL
    return false;
  }

  if (NULL == outMaxLuminance) {
    ALOGE("outMaxLuminance couldn't be NULL!");
    return false;
  }

  if (NULL == outMaxAverageLuminance) {
    ALOGE("outMaxAverageLuminance couldn't be NULL!");
    return false;
  }

  if (NULL == outMinLuminance) {
    ALOGE("outMinLuminance couldn't be NULL!");
    return false;
  }

  if (display_hdrMd_) {
    // HDR meta block bit 3 of byte 3: STPTE ST 2084
    if (display_hdrMd_->eotf & 0x04) {
      if(outTypes)
        *(outTypes + *outNumTypes) = (uint32_t)EOTF_ST2084;
      ALOGD("EOTF_ST2084 found!");
      (*outNumTypes)+=1;
    }
    // HDR meta block bit 4 of byte 3: HLG
    if (display_hdrMd_->eotf & 0x08) {
      if(outTypes)
        *(outTypes + *outNumTypes) = (uint32_t)EOTF_HLG;
      ALOGD("EOTF_HLG found!");
      (*outNumTypes)+=1;
    }
    double outmaxluminance, outmaxaverageluminance, outminluminance;
    // Luminance value = 50 * POW(2, coded value / 32)
    // Desired Content Min Luminance = Desired Content Max Luminance * POW(2,
    // coded value/255) / 100
    outmaxluminance = pow(2.0, display_hdrMd_->desired_max_ll / 32.0) * 50.0;
    *outMaxLuminance = float(outmaxluminance);
    outmaxaverageluminance =
        pow(2.0, display_hdrMd_->desired_max_fall / 32.0) * 50.0;
    *outMaxAverageLuminance = float(outmaxaverageluminance);
    outminluminance = display_hdrMd_->desired_max_ll *
                      pow(2.0, display_hdrMd_->desired_min_ll / 255.0) / 100;
    *outMinLuminance = float(outminluminance);

    int ret = GetConnectorProperty(*drm_, *this, "HDR_OUTPUT_METADATA", &hdr_op_metadata_prop_);
    if (ret) {
      ALOGE("%s Could not get HDR_OUTPUT_METADATA property\n", __FUNCTION__);
    }
    return true;
  }

  return false;
}

bool DrmConnector::GetRenderIntents(uint32_t *outNumIntents, int32_t *outIntents) {
  // If HDR is supported, adds HDR render intents accordingly.
  if (display_hdrMd_ && display_hdrMd_->eotf & 0x0C) {
    *(outIntents + *outNumIntents) = HAL_RENDER_INTENT_TONE_MAP_COLORIMETRIC;
    *(outNumIntents)+=1;
    *(outIntents + *outNumIntents) = HAL_RENDER_INTENT_TONE_MAP_ENHANCE;
    *(outNumIntents)+=1;
  }
   return true;
}

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MIN_IF_NT_ZERO(c, d) (c ? MIN(c, d) : d)
void DrmConnector::PrepareHdrMetadata(hdr_md *layer_hdr_metadata,
                                    struct  hdr_output_metadata *final_hdr_metadata) {

  struct hdr_metadata_static *l_md = &layer_hdr_metadata->static_metadata;
  struct hdr_metadata_infoframe *out_static_md =
      &final_hdr_metadata->hdmi_metadata_type1;

  out_static_md->max_cll = l_md->max_cll;
  out_static_md->max_fall = l_md->max_fall;
  out_static_md->max_display_mastering_luminance = l_md->max_luminance;
  out_static_md->min_display_mastering_luminance = l_md->min_luminance;
  out_static_md->display_primaries[0].x =
      MIN_IF_NT_ZERO(ColorPrimary(l_md->primaries.r.x),
                     primaries_.display_primary_r_x);
  out_static_md->display_primaries[0].y =
      MIN_IF_NT_ZERO(ColorPrimary(l_md->primaries.r.y),
                     primaries_.display_primary_r_y);
  out_static_md->display_primaries[1].x =
      MIN_IF_NT_ZERO(ColorPrimary(l_md->primaries.g.x),
                     primaries_.display_primary_g_x);
  out_static_md->display_primaries[1].y =
      MIN_IF_NT_ZERO(ColorPrimary(l_md->primaries.g.y),
                     primaries_.display_primary_g_y);
  out_static_md->display_primaries[2].x =
      MIN_IF_NT_ZERO(ColorPrimary(l_md->primaries.g.x),
                     primaries_.display_primary_b_x);
  out_static_md->display_primaries[2].y =
      MIN_IF_NT_ZERO(ColorPrimary(l_md->primaries.g.y),
                     primaries_.display_primary_b_y);
  out_static_md->white_point.x =
      MIN_IF_NT_ZERO(ColorPrimary(l_md->primaries.white_point.x),
                     primaries_.white_point_x);
  out_static_md->white_point.y =
      MIN_IF_NT_ZERO(ColorPrimary(l_md->primaries.white_point.y),
                     primaries_.white_point_y);
  out_static_md->eotf = CTA_EOTF_HDR_ST2084;
  out_static_md->metadata_type = 1;
}

const DrmProperty &DrmConnector::link_status_property() const {
  return link_status_property_;
}

}  // namespace android
