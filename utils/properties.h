/*
 * Copyright (C) 2022 The Android Open Source Project
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

#ifdef ANDROID

#include <cutils/properties.h>

#else

#include <cstdio>
#include <cstdlib>
#include <cstring>

// NOLINTNEXTLINE(readability-identifier-naming)
constexpr int PROPERTY_VALUE_MAX = 92;

// NOLINTNEXTLINE(readability-identifier-naming)
auto inline property_get(const char *name, char *value,
                         const char *default_value) -> int {
  // NOLINTNEXTLINE (concurrency-mt-unsafe)
  char *prop = std::getenv(name);
  snprintf(value, PROPERTY_VALUE_MAX, "%s",
           (prop == nullptr) ? default_value : prop);
  return static_cast<int>(strlen(value));
}

/**
 * Bluntly copied from system/core/libcutils/properties.cpp,
 * which is part of the Android Project and licensed under Apache 2.
 * Source:
 * https://cs.android.com/android/platform/superproject/main/+/main:system/core/libcutils/properties.cpp;l=27
 */
auto inline property_get_bool(const char *key, int8_t default_value) -> int8_t {
  if (!key)
    return default_value;

  int8_t result = default_value;
  char buf[PROPERTY_VALUE_MAX] = {};

  int len = property_get(key, buf, "");
  if (len == 1) {
    char ch = buf[0];
    if (ch == '0' || ch == 'n') {
      result = false;
    } else if (ch == '1' || ch == 'y') {
      result = true;
    }
  } else if (len > 1) {
    if (!strcmp(buf, "no") || !strcmp(buf, "false") || !strcmp(buf, "off")) {
      result = false;
    } else if (!strcmp(buf, "yes") || !strcmp(buf, "true") ||
               !strcmp(buf, "on")) {
      result = true;
    }
  }

  return result;
}

#endif

class Properties {
 public:
  static auto IsPresentFenceNotReliable() -> bool;
  static auto UseConfigGroups() -> bool;
};
