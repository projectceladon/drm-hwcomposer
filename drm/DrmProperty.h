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
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "drm/DrmUnique.h"
#include "utils/fd.h"
#include "utils/log.h"

namespace android {

class DrmProperty {
 public:
  DrmProperty() = default;
  DrmProperty(const SharedFd &fd, uint32_t obj_id, drmModePropertyPtr p,
              uint64_t value);
  DrmProperty(const DrmProperty &) = delete;
  DrmProperty &operator=(const DrmProperty &) = delete;

  auto Init(const SharedFd &fd, uint32_t obj_id, drmModePropertyPtr p,
            uint64_t value) -> void;
  std::tuple<uint64_t, int> GetEnumValueWithName(const std::string &name) const;

  auto GetId() const {
    return id_;
  }

  auto GetName() const {
    return name_;
  }

  auto GetValue() const -> std::optional<uint64_t>;

  bool IsImmutable() const {
    return id_ != 0 && (flags_ & DRM_MODE_PROP_IMMUTABLE) != 0;
  }

  bool IsRange() const {
    return id_ != 0 && (flags_ & DRM_MODE_PROP_RANGE) != 0;
  }

  bool IsBitmask() const {
    return id_ != 0 && (flags_ & DRM_MODE_PROP_BITMASK) != 0;
  }

  auto RangeMin() const -> std::tuple<int, uint64_t>;
  auto RangeMax() const -> std::tuple<int, uint64_t>;

  [[nodiscard]] auto AtomicSet(drmModeAtomicReq &pset, uint64_t value) const
      -> bool;

  template <class E>
  auto AddEnumToMap(const std::string &name, E key, std::map<E, uint64_t> &map)
      -> bool;

  template <class E>
  auto AddEnumToMapReverse(const std::string &name, E value,
                           std::map<uint64_t, E> &map) -> bool;

  auto GetEnumMask(uint64_t &mask) -> bool;

  explicit operator bool() const {
    return id_ != 0;
  }

  auto GetEnumNameFromValue(uint64_t value) const -> std::optional<std::string>;

  bool IsBlob() const {
    return id_ != 0 && (flags_ & DRM_MODE_PROP_BLOB) != 0;
  }
  template <typename T>
  bool GetBlobData(std::vector<T> &data_out) const;

 private:
  class DrmPropertyEnum {
   public:
    explicit DrmPropertyEnum(drm_mode_property_enum *e);
    ~DrmPropertyEnum() = default;

    uint64_t value;
    std::string name;
  };

  SharedFd fd_ = nullptr;
  uint32_t obj_id_ = 0;
  uint32_t id_ = 0;

  uint32_t flags_ = 0;
  std::string name_;
  uint64_t value_ = 0;

  std::vector<uint64_t> values_;
  std::vector<DrmPropertyEnum> enums_;
  std::vector<uint32_t> blob_ids_;
};

template <class E>
auto DrmProperty::AddEnumToMap(const std::string &name, E key,
                               std::map<E, uint64_t> &map) -> bool {
  uint64_t enum_value = UINT64_MAX;
  int err = 0;
  std::tie(enum_value, err) = GetEnumValueWithName(name);
  if (err == 0) {
    map[key] = enum_value;
    return true;
  }

  return false;
}

template <class E>
auto DrmProperty::AddEnumToMapReverse(const std::string &name, E value,
                                      std::map<uint64_t, E> &map) -> bool {
  uint64_t enum_value = UINT64_MAX;
  int err = 0;
  std::tie(enum_value, err) = GetEnumValueWithName(name);
  if (err == 0) {
    map[enum_value] = value;
    return true;
  }

  return false;
}

template <typename T>
bool DrmProperty::GetBlobData(std::vector<T> &data_out) const {
  auto value = GetValue();
  if (!fd_) {
    ALOGE("Could not read blob data from property %s: No fd", name_.c_str());
    return false;
  }
  if (!IsBlob()) {
    ALOGE("Property %s is not blob type", name_.c_str());
    return false;
  }
  if (!value.has_value()) {
    ALOGE("Could not read blob data from property %s: No blob id",
          name_.c_str());
    return false;
  }

  auto blob = MakeDrmModePropertyBlobUnique(*fd_, value.value());
  if (blob == nullptr) {
    ALOGE("Failed to read blob with id=%d from property %s", value.value(),
          name_.c_str());
    return false;
  }

  if (blob->length % sizeof(T) != 0) {
    ALOGE(
        "Property %s blob size of %zu bytes is not divisible by type argument "
        "size of %zu bytes",
        name_.c_str(), blob->length, sizeof(T));
    return false;
  }

  auto cast_data = static_cast<T *>(blob->data);
  size_t cast_data_length = blob->length / sizeof(T);
  data_out.assign(cast_data, cast_data + cast_data_length);

  return true;
}

}  // namespace android
