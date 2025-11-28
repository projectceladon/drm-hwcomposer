#pragma once
#include <drm_mode.h>
#include <stdint.h>
#include <vector>

class HdrPipeline {
 public:
  static std::vector<drm_color_lut> BuildSrgbDegamma(uint64_t size);
  static std::vector<drm_color_lut> BuildPqGamma(uint64_t size,
                                                 float max_luminance,
                                                 float max_average_luminance,
                                                 float min_luminance);
  static std::shared_ptr<drm_color_ctm> BuildCtm709To2020();
  static std::shared_ptr<drm_color_ctm> BuildCtmIdentity();
};
