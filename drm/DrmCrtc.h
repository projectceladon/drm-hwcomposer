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

#ifndef ANDROID_DRM_CRTC_H_
#define ANDROID_DRM_CRTC_H_
#include <cstdint>
#include <xf86drmMode.h>



#include "DrmDisplayPipeline.h"
#include "DrmMode.h"
#include "DrmProperty.h"
#include "DrmUnique.h"

namespace android {

class DrmDevice;

class DrmCrtc : public PipelineBindable<DrmCrtc> {
 public:
  static auto CreateInstance(DrmDevice &dev, uint32_t crtc_id, uint32_t index)
      -> std::unique_ptr<DrmCrtc>;

  DrmCrtc() = delete;
  DrmCrtc(const DrmCrtc &) = delete;
  DrmCrtc &operator=(const DrmCrtc &) = delete;

  auto GetId() const {
    return crtc_->crtc_id;
  }

  auto CanBind(uint32_t connector_id) {
    return connector_id_ == 0 || connector_id_ == connector_id;
  }

  void BindConnector(uint32_t connector_id) {
    connector_id_ = connector_id;
  }
  
  auto GetIndexInResArray() const {
    return index_in_res_array_;
  }

  auto &GetActiveProperty() const {
    return active_property_;
  }

  auto &GetModeProperty() const {
    return mode_property_;
  }

  auto &GetOutFencePtrProperty() const {
    return out_fence_ptr_property_;
  }

 auto &GetCtmProperty() const {
   return ctm_property_;
 }

 auto &GetGammaLutProperty() const {
   return gamma_lut_property_;
 }

 auto &GetGammaLutSizeProperty() const {
   return gamma_lut_size_property_;
 }

 private:
  DrmCrtc(DrmModeCrtcUnique crtc, uint32_t index)
      : crtc_(std::move(crtc)), index_in_res_array_(index){};

  DrmModeCrtcUnique crtc_;

  const uint32_t index_in_res_array_;

  DrmProperty active_property_;
  DrmProperty mode_property_;
  DrmProperty out_fence_ptr_property_;
  DrmProperty ctm_property_;
  DrmProperty gamma_lut_property_;
  DrmProperty gamma_lut_size_property_;

  uint32_t connector_id_ = 0;
};
}  // namespace android

#endif  // ANDROID_DRM_CRTC_H_
