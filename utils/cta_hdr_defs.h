/*
Copyright (C) <2023> Intel Corporation

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions
and limitations under the License.


SPDX-License-Identifier: Apache-2.0
*/

#ifndef CTA_HDR_DEFS
#define CTA_HDR_DEFS

namespace android {

#define CTA_EXTENSION_TAG 0x02
#define CTA_COLORIMETRY_CODE 0x05
#define CTA_HDR_STATIC_METADATA 0x06
#define CTA_EXTENDED_TAG_CODE 0x07

/* CTA-861-G: HDR Metadata names and types */

enum cta_hdr_eotf_type {
  CTA_EOTF_SDR_TRADITIONAL = 0,
  CTA_EOTF_HDR_TRADITIONAL,
  CTA_EOTF_HDR_ST2084,
  CTA_EOTF_HLG_BT2100,
  CTA_EOTF_MAX
};

/* Display's HDR Metadata */
struct cta_edid_hdr_metadata_static {
  uint8_t eotf;
  uint8_t metadata_type;
  uint8_t desired_max_ll;
  uint8_t desired_max_fall;
  uint8_t desired_min_ll;
};

/* Display's color primaries */
struct cta_display_color_primaries {
  uint16_t display_primary_r_x;
  uint16_t display_primary_r_y;
  uint16_t display_primary_g_x;
  uint16_t display_primary_g_y;
  uint16_t display_primary_b_x;
  uint16_t display_primary_b_y;
  uint16_t white_point_x;
  uint16_t white_point_y;
};

}// namespace android

#endif  // CTA_HDR_DEFS
