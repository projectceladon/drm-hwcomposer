/*
// Copyright (c) 2016 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
*/

#ifndef COMMON_DRM_COMPOSITOR_VA_VARENDERER_H_
#define COMMON_DRM_COMPOSITOR_VA_VARENDERER_H_

#include <hardware/hardware.h>
#include <vector>
#include <map>
#include "include/nativebufferhandler.h"
#include <va/va.h>
#include <va/va_drmcommon.h>
#include "vautils.h"
#define NATIVE_BUFFER_VECTOR_SIZE 3

namespace android {

struct OverlayLayer;
struct DrmHwcLayer;
class NativeSurface;
class HwcLayer;
class HwcVaLayer;
class ScopedVABufferID {
 public:
  ScopedVABufferID(VADisplay display) : display_(display) {
  }

  ~ScopedVABufferID() {
    if (buffer_ != VA_INVALID_ID)
      vaDestroyBuffer(display_, buffer_);
  }

  bool CreateBuffer(VAContextID context, VABufferType type, uint32_t size,
                    uint32_t num, void* data) {
    VAStatus ret =
        vaCreateBuffer(display_, context, type, size, num, data, &buffer_);
    return ret == VA_STATUS_SUCCESS ? true : false;
  }

  operator VABufferID() const {
    return buffer_;
  }

  VABufferID buffer() const {
    return buffer_;
  }

  VABufferID& buffer() {
    return buffer_;
  }

 private:
  VADisplay display_;
  VABufferID buffer_ = VA_INVALID_ID;
};

struct HwcColorBalanceCap {
  VAProcFilterCapColorBalance caps_;
  float value_;
  bool use_default_ = true;
};


enum class HWCColorControl : int32_t {
  kColorHue = 0,
  kColorSaturation = 1,
  kColorBrightness = 2,
  kColorContrast = 3,
  kColorSharpness = 4
};

struct HWCColorProp {
  float value_ = 0.0;
  bool use_default_ = true;
};

enum class HWCDeinterlaceFlag : int32_t {
  kDeinterlaceFlagNone = 0,
  kDeinterlaceFlagForce = 1,
  kDeinterlaceFlagAuto = 2
};

enum class HWCDeinterlaceControl : int32_t {
  kDeinterlaceNone = 0,
  kDeinterlaceBob = 1,
  kDeinterlaceWeave = 2,
  kDeinterlaceMotionAdaptive = 3,
  kDeinterlaceMotionCompensated = 4
};

struct HWCDeinterlaceProp {
  HWCDeinterlaceFlag flag_;
  HWCDeinterlaceControl mode_;
};

enum HWCTransform : uint32_t {
  kHwcIdentity = 0,
  kHwcReflectX = 1 << 0,
  kHwcReflectY = 1 << 1,
  kHwcTransform90 = 1 << 2,
  kHwcTransform180 = 1 << 3,
  kHwcTransform270 = 1 << 4,
  kHwcTransform45 = kHwcTransform90 | kHwcReflectY,
  kHwcTransform135 = kHwcTransform90 | kHwcReflectX,
  kHwcMaxTransform = 8
};

enum HWCRotation {
  kHwcRotateNone = 0,
  kHwcRotate90,
  kHwcRotate180,
  kHwcRotate270,
  kHwcMaxRotate
};


struct HwcFilterCap {
  VAProcFilterCap caps_;
  float value_;
  bool use_default_ = true;
};

typedef struct _HwcDeinterlaceCap {
  VAProcFilterCapDeinterlacing caps_[VAProcDeinterlacingCount];
  VAProcDeinterlacingType mode_;
} HwcDeinterlaceCap;

class VARenderer {
 public:
  VARenderer() = default;
  ~VARenderer();

  bool Init(uint32_t fd) ;
  bool startRender(HwcVaLayer* layer,uint32_t format);
  void InsertFence(int32_t /*kms_fence*/)  {
  }

  void SetDisableExplicitSync(bool /*disable_explicit_sync*/)  {
  }
  buffer_handle_t getPreBuffer(){
    int temp =0 ;
    if(current_handle_position ==0){
      temp = NATIVE_BUFFER_VECTOR_SIZE -1 ;
    }else if(current_handle_position ==-1){
      return 0;
    }else{
      temp = current_handle_position -1;
    }
    return native_active_handles.at(temp)->handle_;
  }

 private:
  bool QueryVAProcFilterCaps(VAContextID context, VAProcFilterType type,
                             void* caps, uint32_t* num);
  unsigned int GetVAProcFilterScalingMode(uint32_t mode);
  bool SetVAProcFilterColorValue(HWCColorControl type,
                                 const HWCColorProp& prop);
  bool SetVAProcFilterColorDefaultValue(VAProcFilterCapColorBalance* caps);
  bool SetVAProcFilterDeinterlaceDefaultMode();
  bool MapVAProcFilterColorModetoHwc(HWCColorControl& vppmode,
                                     VAProcColorBalanceType vamode);
  bool CreateContext();
  void DestroyContext();
  bool LoadCaps();
  bool UpdateCaps();

  int getSurfaceIn(buffer_handle_t bufferHandle, VADisplay display,
    VASurfaceID* surface,uint32_t format,uint32_t width, uint32_t height);
#if VA_MAJOR_VERSION >= 1
  void HWCTransformToVA(uint32_t transform, uint32_t& rotation,
                        uint32_t& mirror);
#endif
  bool NeedResizeCache(uint32_t layer_num);
  bool ResizeCache(uint32_t layer_num);
  void ReleaseCache();
  bool AllocateCache(uint32_t layer_num);
  bool update_caps_ = false;
  void* va_display_ = nullptr;
  std::vector<VABufferID> filters_;
  std::vector<ScopedVABufferID> cb_elements_;
  std::vector<ScopedVABufferID> sharp_;
  std::vector<ScopedVABufferID> deinterlace_;
  std::map<HWCColorControl, HwcColorBalanceCap> colorbalance_caps_;
  HwcFilterCap sharp_caps_;
  HwcDeinterlaceCap deinterlace_caps_;
  int render_target_format_ = VA_RT_FORMAT_YUV420;
  VAContextID va_context_ = VA_INVALID_ID;
  VAConfigID va_config_ = VA_INVALID_ID;

  mutable pthread_mutex_t lock_;

  int64_t modifier_bak = -1;
  std::vector<DRMHwcNativeHandle> native_handles;
  std::vector<DRMHwcNativeHandle> native_rotation_handles;
  std::vector<DRMHwcNativeHandle> native_active_handles;
  int current_handle_position = 0;

  std::unique_ptr<NativeBufferHandler> buffer_handler_;
  VASurfaceID *surface_in_ = nullptr;
  VABufferID *va_buffer_id_= nullptr;
  VARectangle *surface_region_ = nullptr;
  VARectangle *output_region_ = nullptr;
  const uint32_t DEFAULT_LAYER_NUM = 10;
  const uint32_t LAYER_STEP = 5;
  uint32_t layer_capacity_ = 0;
};

}  // namespace android
#endif  // COMMON_COMPOSITOR_VA_VARENDERER_H_
