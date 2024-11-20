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

#undef NDEBUG /* Required for assert to work */

#define ATRACE_TAG ATRACE_TAG_GRAPHICS
#define LOG_TAG "hwc-drm-atomic-state-manager"

#include "DrmAtomicStateManager.h"

#include <drm/drm_mode.h>
#include <pthread.h>
#include <sched.h>
#include <sync/sync.h>
#include <utils/Trace.h>

#include <array>
#include <cassert>
#include <cstdlib>
#include <ctime>
#include <sstream>
#include <vector>

#include "drm/DrmCrtc.h"
#include "drm/DrmDevice.h"
#include "drm/DrmPlane.h"
#include "drm/DrmUnique.h"
#include "utils/log.h"

namespace android {

// NOLINTNEXTLINE (readability-function-cognitive-complexity): Fixme
auto DrmAtomicStateManager::CommitFrame(AtomicCommitArgs &args) -> int {
  ATRACE_CALL();

  if (args.active && *args.active == active_frame_state_.crtc_active_state) {
    /* Don't set the same state twice */
    args.active.reset();
  }

  if (!args.HasInputs()) {
    /* nothing to do */
    return 0;
  }

  if (!active_frame_state_.crtc_active_state) {
    /* Force activate display */
    args.active = true;
  }

  auto new_frame_state = NewFrameState();

  auto *drm = pipe_->device;
  auto *connector = pipe_->connector->Get();
  auto *crtc = pipe_->crtc->Get();

  auto pset = MakeDrmModeAtomicReqUnique();
  if (!pset) {
    ALOGE("Failed to allocate property set");
    return -ENOMEM;
  }

  int out_fence = -1;
  if (!crtc->GetOutFencePtrProperty().AtomicSet(*pset, uint64_t(&out_fence))) {
    return -EINVAL;
  }

  bool nonblock = true;

  if (args.active) {
    nonblock = false;
    new_frame_state.crtc_active_state = *args.active;
    if (!crtc->GetActiveProperty().AtomicSet(*pset, *args.active ? 1 : 0) ||
        !connector->GetCrtcIdProperty().AtomicSet(*pset, crtc->GetId())) {
      return -EINVAL;
    }
  }

  if (args.display_mode) {
    new_frame_state.mode_blob = args.display_mode.value().CreateModeBlob(*drm);

    if (!new_frame_state.mode_blob) {
      ALOGE("Failed to create mode_blob");
      return -EINVAL;
    }

    if (!crtc->GetModeProperty().AtomicSet(*pset, *new_frame_state.mode_blob)) {
      return -EINVAL;
    }
  }

  auto unused_planes = new_frame_state.used_planes;

  bool has_hdr_layer = false;

  if (args.composition) {
    new_frame_state.used_planes.clear();

    for (auto &joining : args.composition->plan) {
      DrmPlane *plane = joining.plane->Get();
      LayerData &layer = joining.layer;

      if (layer.bi->color_space >= BufferColorSpace::kItuRec2020) {
        has_hdr_layer = true;
      }

      new_frame_state.used_framebuffers.emplace_back(layer.fb);
      new_frame_state.used_planes.emplace_back(joining.plane);

      /* Remove from 'unused' list, since plane is re-used */
      auto &v = unused_planes;
      v.erase(std::remove(v.begin(), v.end(), joining.plane), v.end());

      if (plane->AtomicSetState(*pset, layer, joining.z_pos, crtc->GetId()) !=
          0) {
        return -EINVAL;
      }
    }
  }

  if (drm->IsHdrSupportedDevice()) {
    hdr_md& hdr_metadata =  connector->GetHdrMatedata();
    if (has_hdr_layer && hdr_metadata.valid) {
      struct hdr_output_metadata final_hdr_metadata;
      uint32_t id;
      connector->PrepareHdrMetadata(&hdr_metadata, &final_hdr_metadata);
      drmModeCreatePropertyBlob(drm->GetFd(), (void *)&final_hdr_metadata,
                                sizeof(final_hdr_metadata), &id);
      int ret = drmModeAtomicAddProperty(pset.get(), connector->GetId(),
                               connector->GetHdrOpMetadataProp().id(), id) < 0;
      if (ret)
        ALOGE("Failed to add hdr property to plane");

      hdr_mdata_set_ = true;
    }

    if (!has_hdr_layer && hdr_metadata.valid) {
      int ret = drmModeAtomicAddProperty(pset.get(), connector->GetId(),
                                     connector->GetHdrOpMetadataProp().id(),
                                     (uint64_t)0);
      if (ret)
        ALOGE("Failed to reset hdr metadata to plane, ret:%d", ret);

      // Do the hdr meta info clean up twise considering the first time
      // clean up may not taking effect.
      if (hdr_mdata_set_) {
        hdr_mdata_set_ = false;
      } else {
        hdr_metadata.valid = false;
      }
    }
  }

  if (args.composition) {
    for (auto &plane : unused_planes) {
      if (plane->Get()->AtomicDisablePlane(*pset) != 0) {
        return -EINVAL;
      }
    }
  }

  uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;

  if (args.test_only) {
    return drmModeAtomicCommit(drm->GetFd(), pset.get(),
                               flags | DRM_MODE_ATOMIC_TEST_ONLY, drm);
  }

  if (last_present_fence_) {
    ATRACE_NAME("WaitPriorFramePresented");

    constexpr int kTimeoutMs = 500;
    int err = sync_wait(last_present_fence_.Get(), kTimeoutMs);
    if (err != 0) {
      ALOGE("sync_wait(fd=%i) returned: %i (errno: %i)",
            last_present_fence_.Get(), err, errno);
    }

    CleanupPriorFrameResources();
  }

  if (nonblock) {
    flags |= DRM_MODE_ATOMIC_NONBLOCK;
  }

  if (args.color_adjustment == true) {
    SetColorSaturationHue();
    SetColorBrightnessContrast();
  }

  int err = drmModeAtomicCommit(drm->GetFd(), pset.get(), flags, drm);

  if (err != 0) {
    ALOGE("Failed to commit pset ret=%d\n", err);
    return err;
  }

  if (nonblock) {
    last_present_fence_ = UniqueFd::Dup(out_fence);
    staged_frame_state_ = std::move(new_frame_state);
    frames_staged_++;
    ptt_->Notify();
  } else {
    active_frame_state_ = std::move(new_frame_state);
  }

  if (args.display_mode) {
    /* TODO(nobody): we still need this for synthetic vsync, remove after
     * vsync reworked */
    connector->SetActiveMode(*args.display_mode);
  }

  args.out_fence = UniqueFd(out_fence);

  return 0;
}

PresentTrackerThread::PresentTrackerThread(DrmAtomicStateManager *st_man)
    : st_man_(st_man),
      mutex_(&st_man_->pipe_->device->GetResMan().GetMainLock()) {
  pt_ = std::thread(&PresentTrackerThread::PresentTrackerThreadFn, this);
}

PresentTrackerThread::~PresentTrackerThread() {
  ALOGI("PresentTrackerThread successfully destroyed");
}

void PresentTrackerThread::PresentTrackerThreadFn() {
  /* object should be destroyed on thread exit */
  auto self = std::unique_ptr<PresentTrackerThread>(this);

  int tracking_at_the_moment = -1;

  for (;;) {
    UniqueFd present_fence;

    {
      std::unique_lock lk(*mutex_);
      cv_.wait(lk, [&] {
        return st_man_ == nullptr ||
               st_man_->frames_staged_ > tracking_at_the_moment;
      });

      if (st_man_ == nullptr) {
        break;
      }

      tracking_at_the_moment = st_man_->frames_staged_;

      present_fence = UniqueFd::Dup(st_man_->last_present_fence_.Get());
      if (!present_fence) {
        continue;
      }
    }

    {
      ATRACE_NAME("AsyncWaitForBuffersSwap");
      constexpr int kTimeoutMs = 500;
      int err = sync_wait(present_fence.Get(), kTimeoutMs);
      if (err != 0) {
        ALOGE("sync_wait(fd=%i) returned: %i (errno: %i)", present_fence.Get(),
              err, errno);
      }
    }

    {
      std::unique_lock lk(*mutex_);
      if (st_man_ == nullptr) {
        break;
      }

      /* If resources is already cleaned-up by main thread, skip */
      if (tracking_at_the_moment > st_man_->frames_tracked_) {
        st_man_->CleanupPriorFrameResources();
      }
    }
  }
}

void DrmAtomicStateManager::CleanupPriorFrameResources() {
  assert(frames_staged_ - frames_tracked_ == 1);
  assert(last_present_fence_);

  ATRACE_NAME("CleanupPriorFrameResources");
  frames_tracked_++;
  active_frame_state_ = std::move(staged_frame_state_);
  last_present_fence_ = {};
}

auto DrmAtomicStateManager::ExecuteAtomicCommit(AtomicCommitArgs &args) -> int {
  int err = CommitFrame(args);

  if (!args.test_only) {
    if (err != 0) {
      ALOGE("Composite failed for pipeline %s",
            pipe_->connector->Get()->GetName().c_str());
      // Disable the hw used by the last active composition. This allows us to
      // signal the release fences from that composition to avoid hanging.
      AtomicCommitArgs cl_args{};
      cl_args.composition = std::make_shared<DrmKmsPlan>();
      if (CommitFrame(cl_args) != 0) {
        ALOGE("Failed to clean-up active composition for pipeline %s",
              pipe_->connector->Get()->GetName().c_str());
      }
      return err;
    }
  }

  return err;
}  // namespace android

auto DrmAtomicStateManager::ActivateDisplayUsingDPMS() -> int {
  return drmModeConnectorSetProperty(pipe_->device->GetFd(),
                                     pipe_->connector->Get()->GetId(),
                                     pipe_->connector->Get()
                                         ->GetDpmsProperty()
                                         .id(),
                                     DRM_MODE_DPMS_ON);
}

void DrmAtomicStateManager::MatrixMult3x3(const double matrix_1[3][3], const double matrix_2[3][3], double result[3][3])
{
  for (int y = 0; y < 3; y++) {
    for (int x = 0; x < 3; x++) {
      result[y][x] = matrix_1[y][0] * matrix_2[0][x] + matrix_1[y][1] * matrix_2[1][x] + matrix_1[y][2] * matrix_2[2][x];
    }
  }
}

void DrmAtomicStateManager::GenerateHueSaturationMatrix(double hue, double saturation, double coeff[3][3])
{
  const double pi                            = 3.1415926535897932;
  double hue_shift                           = hue * pi / 180.0;
  double c                                   = cos(hue_shift);
  double s                                   = sin(hue_shift);
  double hue_rotation_matrix[3][3]           = { { 1.0, 0.0, 0.0 }, { 0.0, c, -s }, { 0.0, s, c } };
  double saturation_enhancement_matrix[3][3] = { { 1.0, 0.0, 0.0 }, { 0.0, saturation, 0.0 }, { 0.0, 0.0, saturation } };
  double ycbcr2rgb709[3][3]                  = { { 1.0000, 0.0000, 1.5748 }, { 1.0000, -0.1873, -0.4681 }, { 1.0000, 1.8556, 0.0000 } };
  double rgb2ycbcr709[3][3]                  = { { 0.2126, 0.7152, 0.0722 }, { -0.1146, -0.3854, 0.5000 }, { 0.5000, -0.4542, -0.0458 } };
  double result_1[3][3];
  double result_2[3][3];

  // Use Bt.709 coefficients for RGB to YCbCr conversion
  MatrixMult3x3(ycbcr2rgb709, saturation_enhancement_matrix, result_1);
  MatrixMult3x3(result_1, hue_rotation_matrix, result_2);
  MatrixMult3x3(result_2, rgb2ycbcr709, coeff);
}

auto DrmAtomicStateManager::SetColorTransformMatrix(
  double *color_transform_matrix,
  int32_t color_transform_hint) ->int {
  FILE *file_saturation = NULL;
  FILE *file_hue = NULL;
  int read_bytes;
  char buf[4096] = {};
  double hue = 0.0;
  double saturation = 100;
  double coeff[3][3] = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };

  struct drm_color_ctm *ctm =
    (struct drm_color_ctm *)malloc(sizeof(struct drm_color_ctm));
  if (!ctm) {
    ALOGE("Cannot allocate ctm memory");
    return -ENOMEM;
  }

  file_saturation = fopen("/data/vendor/color/saturation", "r+");
  if (file_saturation != NULL) {
    read_bytes = fread(buf, 1, 8, file_saturation);
    if (read_bytes <= 0) {
      ALOGE("COLOR_ fread saturation error");
      free(ctm);
      fclose(file_saturation);
      return -EINVAL;
    }
    saturation = atof(buf);
    fclose(file_saturation);
  }

  file_hue = fopen("/data/vendor/color/hue", "r+");
  if (file_hue != NULL) {
    read_bytes = fread(buf, 1, 8, file_hue);
    if (read_bytes <= 0) {
      ALOGE("COLOR_ fread hue error");
      free(ctm);
      fclose(file_hue);
      return -EINVAL;
    }
    hue = atof(buf);
    fclose(file_hue);
  }

  if (hue < 0.0 || hue > 359.0) {
    hue = 0.0;
  }

  saturation = saturation/100;
  if (saturation < 0.75 || saturation > 1.25) {
    saturation = 1.0;
  }

  ALOGD("COLOR_ hue=%f", hue);
  ALOGD("COLOR_ saturation=%f", saturation);

  GenerateHueSaturationMatrix(hue, saturation, coeff);

  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      color_transform_matrix[i * 4 + j] = coeff[j][i];
    }
  }

  switch (color_transform_hint) {
    case HAL_COLOR_TRANSFORM_IDENTITY: {
      memset(ctm->matrix, 0, sizeof(ctm->matrix));
      for (int i = 0; i < 3; i++) {
        ctm->matrix[i * 3 + i] = (1ll << 32);
      }

      ApplyPendingCTM(ctm);
      break;
    }
    case HAL_COLOR_TRANSFORM_ARBITRARY_MATRIX: {
      for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
          if (color_transform_matrix[i * 4 + j] < 0) {
            ctm->matrix[i * 3 + j] =
                (int64_t) (-color_transform_matrix[i * 4 + j] *
                ((int64_t) 1L << 32));
            ctm->matrix[i * 3 + j] |= 1ULL << 63;
          } else {
            ctm->matrix[i * 3 + j] =
              (int64_t) (color_transform_matrix[i * 4 + j] *
              ((int64_t) 1L << 32));
          }
        }
      }

      ApplyPendingCTM(ctm);
      break;
    }
  }
  free(ctm);
  return 0;
}

auto DrmAtomicStateManager::ApplyPendingCTM(
  struct drm_color_ctm *ctm) -> int {
  if (pipe_->crtc->Get()->GetCtmProperty().id() == 0) {
    ALOGE("GetCtmProperty().id() == 0");
    return -EINVAL;
  }

  uint32_t ctm_id = 0;
  drmModeCreatePropertyBlob(pipe_->device->GetFd(), ctm, sizeof(drm_color_ctm), &ctm_id);
  if (ctm_id == 0) {
    ALOGE("COLOR_ ctm_id == 0");
    return -EINVAL;
  }

  drmModeObjectSetProperty(pipe_->device->GetFd(), pipe_->crtc->Get()->GetId(), DRM_MODE_OBJECT_CRTC,
                           pipe_->crtc->Get()->GetCtmProperty().id(), ctm_id);
  drmModeDestroyPropertyBlob(pipe_->device->GetFd(), ctm_id);

  return 0;
}

float DrmAtomicStateManager::TransformContrastBrightness(float value, float brightness,
                                              float contrast) {
  float result;
  result = (value - 0.5) * contrast + 0.5 + brightness;

  if (result < 0.0) {
    result = 0.0;
  }
  if (result > 1.0) {
    result = 1.0;
  }
  return result;
}

float DrmAtomicStateManager::TransformGamma(float value, float gamma) {
  float result;

  result = pow(value, gamma);
  if (result < 0.0) {
    result = 0.0;
  }
  if (result > 1.0) {
    result = 1.0;
  }

  return result;
}

auto DrmAtomicStateManager::SetColorCorrection(struct gamma_colors gamma,
                                    uint32_t contrast_c,
                                    uint32_t brightness_c) ->int{
  struct drm_color_lut *lut;
  float brightness[3];
  float contrast[3];
  uint8_t temp[3];
  uint64_t lut_size = 0;
  int ret = 0;

  std::tie(ret, lut_size) = pipe_->crtc->Get()->GetGammaLutSizeProperty().value();

  ALOGD("COLOR_ contrast_c=0x%6x", contrast_c);
  ALOGD("COLOR_ brightness_c=0x%6x", brightness_c);

  /* reset lut when contrast and brightness are all 0 */
  if (contrast_c == 0 && brightness_c == 0) {
    lut = NULL;
    ApplyPendingLUT(lut, lut_size);
    free(lut);
    return 0;
  }

  lut = (struct drm_color_lut *)malloc(sizeof(struct drm_color_lut) * lut_size);
  if (!lut) {
    ALOGE("Cannot allocate LUT memory");
    return -ENOMEM;
  }

  /* Unpack brightness values for each channel */
  temp[0] = (brightness_c >> 16) & 0xFF;
  temp[1] = (brightness_c >> 8) & 0xFF;
  temp[2] = (brightness_c) & 0xFF;

  /* Map brightness from -128 - 127 range into -0.5 - 0.5 range */
  brightness[0] = (float)(temp[0]) / 255 - 0.5;
  brightness[1] = (float)(temp[1]) / 255 - 0.5;
  brightness[2] = (float)(temp[2]) / 255 - 0.5;

  /* Unpack contrast values for each channel */
  temp[0] = (contrast_c >> 16) & 0xFF;
  temp[1] = (contrast_c >> 8) & 0xFF;
  temp[2] = (contrast_c) & 0xFF;

  /* Map contrast from 0 - 255 range into 0.0 - 2.0 range */
  contrast[0] = (float)(temp[0]) / 128;
  contrast[1] = (float)(temp[1]) / 128;
  contrast[2] = (float)(temp[2]) / 128;

  for (uint64_t i = 0; i < lut_size; i++) {
    /* Set lut[0] as 0 always as the darkest color should has brightness 0 */
    if (i == 0) {
      lut[i].red = 0;
      lut[i].green = 0;
      lut[i].blue = 0;
      continue;
    }

    lut[i].red = 0xFFFF * TransformGamma(TransformContrastBrightness(
                                         (float)(i) / lut_size,
                                          brightness[0], contrast[0]),
                                          gamma.red);
    lut[i].green = 0xFFFF * TransformGamma(TransformContrastBrightness(
                                            (float)(i) / lut_size,
                                             brightness[1], contrast[1]),
                                             gamma.green);
    lut[i].blue = 0xFFFF * TransformGamma(TransformContrastBrightness(
                                          (float)(i) / lut_size,
                                           brightness[2], contrast[2]),
                                           gamma.blue);
  }

  ApplyPendingLUT(lut, lut_size);
  free(lut);
  return 0;
}

auto DrmAtomicStateManager::ApplyPendingLUT(struct drm_color_lut *lut, uint64_t lut_size) -> int {
  uint32_t lut_blob_id = 0;

  if (pipe_->crtc->Get()->GetGammaLutProperty().id() == 0) {
    ALOGE("GetGammaLutProperty().id() == 0");
    return -EINVAL;
  }

  drmModeCreatePropertyBlob(
    pipe_->device->GetFd(), lut, sizeof(struct drm_color_lut) * lut_size, &lut_blob_id);
  if (lut_blob_id == 0) {
    ALOGE("COLOR_ lut_blob_id == 0");
    return -EINVAL;

  }

  drmModeObjectSetProperty(pipe_->device->GetFd(), pipe_->crtc->Get()->GetId(), DRM_MODE_OBJECT_CRTC,
                           pipe_->crtc->Get()->GetGammaLutProperty().id(), lut_blob_id);
  drmModeDestroyPropertyBlob(pipe_->device->GetFd(), lut_blob_id);
  return 0;
}

auto DrmAtomicStateManager::SetColorSaturationHue(void) ->int{
  double color_transform_matrix[16] = {1.0, 0.0, 0.0, 0.0,
                                       0.0, 1.0, 0.0, 0.0,
                                       0.0, 0.0, 1.0, 0.0,
                                       0.0, 0.0, 0.0, 1.0};

  int32_t color_transform_hint = HAL_COLOR_TRANSFORM_ARBITRARY_MATRIX;

  SetColorTransformMatrix(color_transform_matrix,
                          color_transform_hint);
  return 0;
}

auto DrmAtomicStateManager::SetColorBrightnessContrast(void) ->int{
  struct gamma_colors gamma;
  int read_bytes;
  FILE *file_brightness = NULL;
  FILE *file_contrast   = NULL;
  char buf[4096]        = {};
  uint32_t contrast_c   = 0x808080;
  uint32_t brightness_c = 0x808080;

  file_brightness = fopen("/data/vendor/color/brightness", "r+");
  if (file_brightness != NULL) {
    read_bytes = fread(buf, 1, 8, file_brightness);
    if (read_bytes <= 0) {
      ALOGE("COLOR_ fread brightness error");
      fclose(file_brightness);
      return -EINVAL;
    }
    if (atoi(buf) < 0 || atoi(buf) > 255) {
      brightness_c = 0x80;
    } else {
      brightness_c = atoi(buf) &0xFF;
    }

    brightness_c = ((brightness_c<< 16) |
                    (brightness_c << 8) |
                    (brightness_c));
    fclose(file_brightness);
  }

  file_contrast = fopen("/data/vendor/color/contrast", "r+");
  if (file_contrast != NULL) {
    read_bytes = fread(buf, 1, 8, file_contrast);
    if (read_bytes <= 0) {
      ALOGE("COLOR_ fread contrast error");
      fclose(file_contrast);
      return -EINVAL;
    }
    if (atoi(buf) < 0 || atoi(buf) > 255) {
      contrast_c = 0x80;
    } else {
      contrast_c = atoi(buf) & 0xFF;
    }

    contrast_c =  ((contrast_c<< 16) |
                   (contrast_c << 8) |
                   (contrast_c));
    fclose(file_contrast);
  }

  gamma.red   = 1;
  gamma.green = 1;
  gamma.blue  = 1;

  SetColorCorrection(gamma, contrast_c, brightness_c);

  return 0;
}

void DrmAtomicStateManager::SetHDCPState(HWCContentProtection state,
                              HWCContentType content_type) {
  uint64_t value = 3;
  uint64_t type = 3;
  int ret =0;

  auto *connector = pipe_->connector->Get();

  if (!connector->IsConnected())
    return;

  desired_protection_support_ = state;
  ALOGD("SetHDCPState desired_protection_support_ %d\n",desired_protection_support_);
  ALOGD("SetHDCPState current_protection_support_ %d\n",current_protection_support_);

  if (desired_protection_support_ == current_protection_support_)
    return;

  if (pipe_->connector->Get()->GetHdcpTypeProperty().id() <= 0) {
    ALOGE("Cannot set HDCP state as Type property is not supported \n");
    return;
  }

  std::tie(ret, type) = pipe_->connector->Get()->GetHdcpTypeProperty().value();
  ALOGD("Get Content type %lu \n", type);
  ALOGD("Get Content content_type %u \n", content_type);

  if ((content_type < 2) && (content_type_ != content_type)) {
    content_type_ = content_type;

    ALOGD("Set Content type %u \n", content_type);
    drmModeConnectorSetProperty(pipe_->device->GetFd(),
		  pipe_->connector->Get()->GetId(),
		  pipe_->connector->Get()->GetHdcpTypeProperty().id(),
		  content_type);

  }

  if (pipe_->connector->Get()->GetHdcpProperty().id() <= 0) {
    ALOGE("Cannot set HDCP state as Connector property is not supported \n");
    return;
  }

  std::tie(ret, value) = pipe_->connector->Get()->GetHdcpProperty().value();
  ALOGD("Get Content Protection value %lu \n", value);

  if (value < 3) {
    switch (value) {
      case 0:
        //current_protection_support_ = HWCContentProtection::kUnDesired;
        current_protection_support_ = hwcomposer::HWCContentProtection::kUnDesired;
        ALOGD("%s GetHDCPConnectorProperty value 0", __FUNCTION__);
        break;
      case 1:
        //current_protection_support_ = HWCContentProtection::kDesired;
        current_protection_support_ = hwcomposer::HWCContentProtection::kDesired;
        ALOGD("%s GetHDCPConnectorProperty value 1", __FUNCTION__);
        break;
      default:
        ALOGE("%s GetHDCPConnectorProperty default", __FUNCTION__);
        break;
    }
  }

  if (desired_protection_support_ == HWCContentProtection::kUnSupported) {
    desired_protection_support_ = current_protection_support_;
  }

  current_protection_support_ = desired_protection_support_;
  if (current_protection_support_ == kDesired) {
    value = 1;
  }

  ALOGD("Set Content Protection %lu \n", value);
  drmModeConnectorSetProperty(pipe_->device->GetFd(),
		  pipe_->connector->Get()->GetId(),
		  pipe_->connector->Get()->GetHdcpProperty().id(),
		  value);
}
}  // namespace android
