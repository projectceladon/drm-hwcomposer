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

#ifndef PUBLIC_HDR_METADATA_DEFS_H
#define PUBLIC_HDR_METADATA_DEFS_H

#include <stdint.h>

namespace android {
/** A CIE 1931 color space*/
struct cie_xy {
  double x;
  double y;
};

struct color_primaries {
  struct cie_xy r;
  struct cie_xy g;
  struct cie_xy b;
  struct cie_xy white_point;
};

struct colorspace {
  struct color_primaries primaries;
  const char *name;
  const char *whitepoint_name;
};

enum hdr_metadata_type {
  HDR_METADATA_TYPE1,
  HDR_METADATA_TYPE2,
};

enum hdr_metadata_eotf {
  EOTF_TRADITIONAL_GAMMA_SDR,
  EOTF_TRADITIONAL_GAMMA_HDR,
  EOTF_ST2084,
  EOTF_HLG,
};

enum hdr_per_frame_metadata_keys {
  KEY_DISPLAY_RED_PRIMARY_X,
  KEY_DISPLAY_RED_PRIMARY_Y,
  KEY_DISPLAY_GREEN_PRIMARY_X,
  KEY_DISPLAY_GREEN_PRIMARY_Y,
  KEY_DISPLAY_BLUE_PRIMARY_X,
  KEY_DISPLAY_BLUE_PRIMARY_Y,
  KEY_WHITE_POINT_X,
  KEY_WHITE_POINT_Y,
  KEY_MAX_LUMINANCE,
  KEY_MIN_LUMINANCE,
  KEY_MAX_CONTENT_LIGHT_LEVEL,
  KEY_MAX_FRAME_AVERAGE_LIGHT_LEVEL,
  KEY_NUM_PER_FRAME_METADATA_KEYS
};

struct hdr_metadata_static {
  struct color_primaries primaries;
  double max_luminance;
  double min_luminance;
  uint32_t max_cll;
  uint32_t max_fall;
  uint8_t eotf;
};

typedef struct {
  bool valid = false;
  enum hdr_metadata_type metadata_type;
  struct hdr_metadata_static static_metadata;
} hdr_md;

}

#endif
