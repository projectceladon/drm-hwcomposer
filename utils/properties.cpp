/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "properties.h"

#include <cerrno>
#include <cstdlib>
#include <limits>

namespace {

auto GetUintProperty(const char *key, uint32_t default_value) -> uint32_t {
  char buf[PROPERTY_VALUE_MAX] = {};
  if (property_get(key, buf, "") <= 0) {
    return default_value;
  }

  char *end = nullptr;
  errno = 0;
  const auto value = std::strtoul(buf, &end, 10);
  if (end == buf || *end != '\0' || errno != 0 ||
      value > std::numeric_limits<uint32_t>::max()) {
    return default_value;
  }

  return static_cast<uint32_t>(value);
}

}  // namespace

/**
 * @brief Determine if the "Present Not Reliable" property is enabled.
 *
 * @return boolean
 */
auto Properties::IsPresentFenceNotReliable() -> bool {
  return (property_get_bool("ro.vendor.hwc.drm.present_fence_not_reliable",
                            0) != 0);
}

auto Properties::UseConfigGroups() -> bool {
  return (property_get_bool("ro.vendor.hwc.drm.use_config_groups", 0) != 0);
}

auto Properties::UseOverlayPlanes() -> bool {
  return (property_get_bool("ro.vendor.hwc.use_overlay_planes", 1) != 0);
}

auto Properties::ScaleWithGpu() -> bool {
  return (property_get_bool("vendor.hwc.drm.scale_with_gpu", 0) != 0);
}

auto Properties::EnableVirtualDisplay() -> bool {
  return (property_get_bool("vendor.hwc.drm.enable_virtual_display", 0) != 0);
}

auto Properties::GetVirtualDisplayWidth() -> uint32_t {
  return GetUintProperty("vendor.hwc.drm.virtual_display.width", 0);
}

auto Properties::GetVirtualDisplayHeight() -> uint32_t {
  return GetUintProperty("vendor.hwc.drm.virtual_display.height", 0);
}

auto Properties::GetHeadlessVRefresh() -> uint32_t {
  return GetUintProperty("vendor.hwc.drm.headless.vrefresh", 0);
}

auto Properties::EnableHdrDisplay() -> bool {
  return (property_get_bool("vendor.hwc.drm.enable_hdr_display", 0) != 0);
}
