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

#include <pthread.h>

#include <memory>
#include <optional>

#include "compositor/DisplayInfo.h"
#include "compositor/DrmKmsPlan.h"
#include "compositor/LayerData.h"
#include "drm/DrmPlane.h"
#include "drm/ResourceManager.h"
#include "drm/VSyncWorker.h"

namespace android {

struct AtomicCommitArgs {
  /* inputs. All fields are optional, but at least one has to be specified */
  bool test_only = false;
  bool blocking = false;
  std::optional<DrmMode> display_mode;
  std::optional<bool> active;
  std::shared_ptr<DrmKmsPlan> composition;
  std::shared_ptr<drm_color_ctm> color_matrix;
  std::optional<Colorspace> colorspace;
  std::optional<int32_t> content_type;
  std::shared_ptr<hdr_output_metadata> hdr_metadata;
  bool color_adjustment = false;
  std::shared_ptr<DrmFbIdHandle> writeback_fb;
  SharedFd writeback_release_fence;

  /* out */
  SharedFd out_fence;

  /* helpers */
  auto HasInputs() const -> bool {
    return display_mode || active || composition;
  }
};

struct gamma_colors {
  float red;
  float green;
  float blue;
};

class DrmAtomicStateManager {
 public:
  static auto CreateInstance(DrmDisplayPipeline *pipe)
      -> std::shared_ptr<DrmAtomicStateManager>;

  ~DrmAtomicStateManager() = default;

  auto ExecuteAtomicCommit(AtomicCommitArgs &args) -> int;
  auto ActivateDisplayUsingDPMS() -> int;
  auto SetColorSaturationHue(void) ->int;
  auto SetColorBrightnessContrast(void) ->int;
  auto SetColorTransformMatrix(
      double *color_transform_matrix,
      int32_t color_transform_hint) -> int;
  auto ApplyPendingCTM(struct drm_color_ctm *ctm) -> int;

  auto SetColorCorrection(struct gamma_colors gamma,
                                    uint32_t contrast_c,
                                    uint32_t brightness_c) ->int;
  auto ApplyPendingLUT(struct drm_color_lut *lut,  uint64_t lut_size) -> int;
  void StopThread() {
    {
      const std::unique_lock lock(mutex_);
      exit_thread_ = true;
    }
    cv_.notify_all();
  }

  void SetHDCPState(HWCContentProtection state,
                    HWCContentType content_type);
 private:
  DrmAtomicStateManager() = default;
  auto CommitFrame(AtomicCommitArgs &args) -> int;

  struct KmsState {
    /* Required to cleanup unused planes */
    std::vector<std::shared_ptr<BindingOwner<DrmPlane>>> used_planes;
    /* We have to hold a reference to framebuffer while displaying it ,
     * otherwise picture will blink */
    std::vector<std::shared_ptr<DrmFbIdHandle>> used_framebuffers;

    DrmModeUserPropertyBlobUnique mode_blob;
    DrmModeUserPropertyBlobUnique ctm_blob;
    DrmModeUserPropertyBlobUnique hdr_metadata_blob;

    int release_fence_pt_index{};

    /* To avoid setting the inactive state twice, which will fail the commit */
    bool crtc_active_state{};
  } active_frame_state_;

  auto NewFrameState() -> KmsState {
    auto *prev_frame_state = &active_frame_state_;
    return (KmsState){
        .used_planes = prev_frame_state->used_planes,
        .crtc_active_state = prev_frame_state->crtc_active_state,
    };
  }

  DrmDisplayPipeline *pipe_{};

  void CleanupPriorFrameResources();
  int64_t FloatToFixedPoint(float value);
  void GenerateHueSaturationMatrix(double hue, double saturation, double coeff[3][3]);
  void MatrixMult3x3(const double matrix_1[3][3], const double matrix_2[3][3], double result[3][3]);

  float TransformContrastBrightness(float value, float brightness,
                                                float contrast);
  float TransformGamma(float value, float gamma);
  KmsState staged_frame_state_;
  SharedFd last_present_fence_;
  int frames_staged_{};
  int frames_tracked_{};

  DstRectInfo whole_display_rect_{};

  void ThreadFn(const std::shared_ptr<DrmAtomicStateManager> &dasm);
  std::condition_variable cv_;
  std::mutex mutex_;
  bool exit_thread_{};

  hwcomposer::HWCContentProtection current_protection_support_ =
    hwcomposer::HWCContentProtection::kUnSupported;
  hwcomposer::HWCContentProtection desired_protection_support_ =
    hwcomposer::HWCContentProtection::kUnSupported;
  hwcomposer::HWCContentType content_type_ = hwcomposer::kCONTENT_TYPE0;
};

}  // namespace android
