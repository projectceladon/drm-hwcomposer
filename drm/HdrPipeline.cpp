#include "HdrPipeline.h"
#include <xf86drmMode.h>
#include <xf86drm.h>
#include <math.h>

std::vector<drm_color_lut> HdrPipeline::BuildSrgbDegamma(uint64_t size) {
  std::vector<drm_color_lut> lut(size);
  if (size < 2) return lut;
  for (uint64_t i = 0; i < size; ++i) {
    double e = double(i) / double(size - 1);
    double l = (e <= 0.04045) ? (e / 12.92) : std::pow((e + 0.055) / 1.055, 2.4);
    uint16_t v = static_cast<uint16_t>(std::lround(std::clamp(l, 0.0, 1.0) * 65535.0));
    lut[i].red = lut[i].green = lut[i].blue = v;
  }
  // ensure black exactly 0
  lut[0].red = lut[0].green = lut[0].blue = 0;
  return lut;
}

std::vector<drm_color_lut> HdrPipeline::BuildPqGamma(uint64_t size,
    float max_luminance, float max_average_luminance,
    float min_luminance) {
  std::vector<drm_color_lut> lut(size);
  if (size < 2) return lut;

  // Clamp inputs
  max_luminance        = std::max(1.0f, max_luminance);
  max_average_luminance = std::max(1.0f, std::min(max_average_luminance, max_luminance));
  min_luminance        = std::max(0.0f, std::min(min_luminance, max_average_luminance));

  // ST2084 constants
  const double m1 = 2610.0 / 16384.0;
  const double m2 = 2523.0 / 32.0;
  const double c1 = 3424.0 / 4096.0;
  const double c2 = 2413.0 / 128.0;
  const double c3 = 2392.0 / 128.0;

  // Peak mapping: LUT 1.0 corresponds to panel peak (max_luminance)
  // Tone map (knee) above max_average_luminance
  const double knee_start = max_average_luminance;
  const double knee_end   = max_luminance;
  const double knee_range = std::max(1e-6, knee_end - knee_start);

  for (uint64_t i = 0; i < size; ++i) {
    double r = double(i) / double(size - 1); // 0..1 scene relative
    // Map to physical luminance range
    double L_scene = min_luminance + r * (max_luminance - min_luminance);

    // Knee compression (Reinhard-like)
    if (L_scene > knee_start) {
      double x = (L_scene - knee_start) / knee_range; // 0..1
      // Smooth compression: y = x / (1 + x) keeps derivatives finite
      double comp = x / (1.0 + x);
      L_scene = knee_start + comp * knee_range;
    }

    // Normalize to PQ absolute range (0..10000 nits)
    double L_norm = std::clamp(L_scene / 10000.0, 0.0, 1.0);

    // ST2084 OETF
    double num = c1 + c2 * std::pow(L_norm, m1);
    double den = 1.0 + c3 * std::pow(L_norm, m1);
    double pq  = std::pow(num / den, m2);

    uint16_t v = static_cast<uint16_t>(std::lround(std::clamp(pq, 0.0, 1.0) * 65535.0));
    lut[i].red = lut[i].green = lut[i].blue = v;
  }

  // Ensure LUT[0] == 0
  lut[0].red = lut[0].green = lut[0].blue = 0;
  return lut;
}

std::shared_ptr<drm_color_ctm> HdrPipeline::BuildCtm709To2020() {
  // Approximate Rec.709 -> Rec.2020 conversion matrix
  double m[3][3] = {
      {0.627404, 0.329282, 0.043313},
      {0.069097, 0.919540, 0.011360},
      {0.016391, 0.088013, 0.895595}
  };
  auto ctm = std::make_shared<drm_color_ctm>();
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      double v = m[i][j];
      int64_t fp = (int64_t)llround(std::fabs(v) * (1LL << 32));
      if (v < 0) fp |= (1ULL << 63);
      ctm->matrix[i * 3 + j] = (uint64_t)fp;
    }
  }
  return ctm;
}

std::shared_ptr<drm_color_ctm> HdrPipeline::BuildCtmIdentity() {
  double m[3][3] = {
      {1.0, 0.0, 0.0},
      {0.0, 1.0, 0.0},
      {0.0, 0.0, 1.0}
  };
  auto ctm = std::make_shared<drm_color_ctm>();
  for (int i = 0; i < 3; ++i) {
    for (int j = 0; j < 3; ++j) {
      double v = m[i][j];
      int64_t fp = (int64_t)llround(std::fabs(v) * (1LL << 32));
      if (v < 0) fp |= (1ULL << 63);
      ctm->matrix[i * 3 + j] = (uint64_t)fp;
    }
  }
  return ctm;
}