#ifndef DRM_HDR_DEFS
#define DRM_HDR_DEFS

namespace android {

#define CTA_EXTENSION_TAG 0x02
#define CTA_COLORIMETRY_CODE 0x05
#define CTA_HDR_STATIC_METADATA 0x06
#define CTA_EXTENDED_TAG_CODE 0x07

/* CTA-861-G: HDR Metadata names and types */
enum drm_hdr_eotf_type {
  DRM_EOTF_SDR_TRADITIONAL = 0,
  DRM_EOTF_HDR_TRADITIONAL,
  DRM_EOTF_HDR_ST2084,
  DRM_EOTF_HLG_BT2100,
  DRM_EOTF_MAX
};


/* Monitors HDR Metadata */
struct drm_edid_hdr_metadata_static {
  uint8_t eotf;
  uint8_t metadata_type;
  uint8_t desired_max_ll;
  uint8_t desired_max_fall;
  uint8_t desired_min_ll;
};

/* Monitor's color primaries */
struct drm_display_color_primaries {
  uint16_t display_primary_r_x;
  uint16_t display_primary_r_y;
  uint16_t display_primary_g_x;
  uint16_t display_primary_g_y;
  uint16_t display_primary_b_x;
  uint16_t display_primary_b_y;
  uint16_t white_point_x;
  uint16_t white_point_y;
};

/* Static HDR metadata to be sent to kernel, matches kernel structure */
struct drm_hdr_metadata_static {
  uint8_t eotf;
  uint8_t metadata_type;
  struct {
    uint16_t x, y;
  } primaries[3];
  struct {
    uint16_t x, y;
  } white_point;
  uint16_t max_mastering_luminance;
  uint16_t min_mastering_luminance;
  uint16_t max_cll;
  uint16_t max_fall;
};

struct drm_hdr_metadata {
  uint32_t metadata_type;
  struct drm_hdr_metadata_static drm_hdr_static_metadata;
};

}// namespace android

#endif  // DRM_HDR_DEFS