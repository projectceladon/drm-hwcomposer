/*
// Copyright (c) 2017 Intel Corporation
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
#include "va/sysdeps.h"
#include "varenderer.h"
#include <sync/sync.h>
#include <drm_fourcc.h>
#include <math.h>
#include <xf86drm.h>
#include <log/log.h>
#include "va/va_backend.h"
#include "va/va_internal.h"
// #include "va/va_fool.h"
#include "va/va_android.h"
#include "va/va_drmcommon.h"
#include "va/drm/va_drm_utils.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <errno.h>
#include "cros_gralloc_handle.h"
#include "vautils.h"
#include <pthread.h>
#include "utils/autolock.h"
#include <string.h>
// #include "drmhwcomposer.h"
#include "hwc2_device/HwcLayer.h"
namespace android {
#define ANDROID_DISPLAY_HANDLE 0x18C34078
#define CHECK_SYMBOL(func) { if (!func) printf("func %s not found\n", #func); return VA_STATUS_ERROR_UNKNOWN; }
#define DEVICE_NAME "/dev/dri/renderD128"
#ifdef LOG_TAG
#undef LOG_TAG
#define LOG_TAG "hwc-varender"
#endif
VARenderer::~VARenderer() {
  DestroyContext();
  if (va_display_) {
    vaTerminate(va_display_);
  }
  DRMHwcNativeHandle temp_handle;
  if(native_handles.size() == NATIVE_BUFFER_VECTOR_SIZE){
    for( int32_t i=0; i<NATIVE_BUFFER_VECTOR_SIZE; i++ ) {
      temp_handle = native_handles.at(i);
      buffer_handler_->ReleaseBuffer(temp_handle);
      buffer_handler_->DestroyHandle(temp_handle);
    }
    native_handles.clear();
  }
  if(native_rotation_handles.size() == NATIVE_BUFFER_VECTOR_SIZE){
    for( int32_t i=0; i<NATIVE_BUFFER_VECTOR_SIZE; i++ ) {
      temp_handle = native_rotation_handles.at(i);
      buffer_handler_->ReleaseBuffer(temp_handle);
      buffer_handler_->DestroyHandle(temp_handle);
    }
    native_rotation_handles.clear();
  }
  if(native_active_handles.size() == NATIVE_BUFFER_VECTOR_SIZE){
    for( int32_t i=0; i<NATIVE_BUFFER_VECTOR_SIZE; i++ ) {
      temp_handle = native_active_handles.at(i);
      buffer_handler_->ReleaseBuffer(temp_handle);
      buffer_handler_->DestroyHandle(temp_handle);
    }
    native_active_handles.clear();
  }
  ReleaseCache();
}

bool VARenderer::Init(uint32_t fd) {
  unsigned int native_display = ANDROID_DISPLAY_HANDLE;
  buffer_handler_.reset(NativeBufferHandler::CreateInstance(fd));
  VAStatus ret = VA_STATUS_SUCCESS;
  va_display_ = vaGetDisplay(&native_display);
  if (!va_display_) {
    ALOGE("vaGetDisplay failed\n");
    return false;
  }
  ret = pthread_mutex_init(&lock_, NULL);
  if (ret)
    ALOGE("Failed to initialize the mutex lock %d\n", ret);
  int major, minor;
  ret = vaInitialize(va_display_, &major, &minor);
  if (ret == VA_STATUS_SUCCESS) {
    AllocateCache(DEFAULT_LAYER_NUM);
  }

  return ret == VA_STATUS_SUCCESS ? true : false;
}

void VARenderer::ReleaseCache() {
  if (va_buffer_id_) {
    free(va_buffer_id_);
    va_buffer_id_ = nullptr;
  }
  if (surface_in_) {
    free(surface_in_);
    surface_in_ = nullptr;
  }
  if (surface_region_) {
    free(surface_region_);
    surface_region_ = nullptr;
  }
  if (output_region_) {
    free(output_region_);
    output_region_ = nullptr;
  }
}

bool VARenderer::NeedResizeCache(uint32_t layer_num) {
  return layer_num > layer_capacity_;
}

bool VARenderer::AllocateCache(uint32_t layer_capacity) {
  surface_in_ = (VASurfaceID*)malloc(sizeof(VASurfaceID) * layer_capacity);
  va_buffer_id_ = (VABufferID*)malloc(sizeof(VABufferID) * layer_capacity);
  surface_region_ = (VARectangle*)malloc(sizeof(VARectangle) * layer_capacity);
  output_region_ = (VARectangle*)malloc(sizeof(VARectangle) * layer_capacity);
  if (surface_in_ == nullptr || va_buffer_id_ == nullptr ||
      surface_region_ == nullptr || output_region_ == nullptr) {
    ReleaseCache();
    return false;
  }
  layer_capacity_ = layer_capacity;
  return true;
}

bool VARenderer::ResizeCache(uint32_t layer_num) {
  ReleaseCache();
  uint32_t step_num = ((layer_num - layer_capacity_) / LAYER_STEP + 1);

  ALOGD("VARenderer resize cache from %d to %d, layer_num %d",
    layer_capacity_, layer_capacity_ + step_num * LAYER_STEP, layer_num);

  return AllocateCache(layer_capacity_ + step_num * LAYER_STEP);
}

bool VARenderer::QueryVAProcFilterCaps(VAContextID context,
                                       VAProcFilterType type, void* caps,
                                       uint32_t* num) {
  VAStatus ret =
      vaQueryVideoProcFilterCaps(va_display_, context, type, caps, num);
  if (ret != VA_STATUS_SUCCESS)
    ALOGE("Query Filter Caps failed\n");
  return ret == VA_STATUS_SUCCESS ? true : false;
}

bool VARenderer::MapVAProcFilterColorModetoHwc(HWCColorControl& vppmode,
                                               VAProcColorBalanceType vamode) {
  switch (vamode) {
    case VAProcColorBalanceHue:
      vppmode = HWCColorControl::kColorHue;
      break;
    case VAProcColorBalanceSaturation:
      vppmode = HWCColorControl::kColorSaturation;
      break;
    case VAProcColorBalanceBrightness:
      vppmode = HWCColorControl::kColorBrightness;
      break;
    case VAProcColorBalanceContrast:
      vppmode = HWCColorControl::kColorContrast;
      break;
    default:
      return false;
  }
  return true;
}

bool VARenderer::SetVAProcFilterColorDefaultValue(
    VAProcFilterCapColorBalance* caps) {
  HWCColorControl mode;
  for (int i = 0; i < VAProcColorBalanceCount; i++) {
    if (MapVAProcFilterColorModetoHwc(mode, caps[i].type)) {
      colorbalance_caps_[mode].caps_ = caps[i];
      colorbalance_caps_[mode].value_ = caps[i].range.default_value;
    }
  }
  sharp_caps_.value_ = sharp_caps_.caps_.range.default_value;
  update_caps_ = true;
  return true;
}

bool VARenderer::SetVAProcFilterDeinterlaceDefaultMode() {
  if (deinterlace_caps_.mode_ != VAProcDeinterlacingNone) {
    deinterlace_caps_.mode_ = VAProcDeinterlacingNone;
    update_caps_ = true;
  }
  return true;
}

bool VARenderer::SetVAProcFilterColorValue(HWCColorControl mode,
                                           const HWCColorProp& prop) {
  if (mode == HWCColorControl::kColorHue ||
      mode == HWCColorControl::kColorSaturation ||
      mode == HWCColorControl::kColorBrightness ||
      mode == HWCColorControl::kColorContrast) {
    if (prop.use_default_) {
      if (!colorbalance_caps_[mode].use_default_) {
        colorbalance_caps_[mode].use_default_ = true;
        update_caps_ = true;
      }
    } else if (prop.value_ != colorbalance_caps_[mode].value_) {
      if (prop.value_ > colorbalance_caps_[mode].caps_.range.max_value ||
          prop.value_ < colorbalance_caps_[mode].caps_.range.min_value) {
        ALOGE("VA Filter value out of range. Mode %d range shoud be %f~%f\n",
               mode, colorbalance_caps_[mode].caps_.range.min_value,
               colorbalance_caps_[mode].caps_.range.max_value);
        return false;
      }
      colorbalance_caps_[mode].value_ = prop.value_;
      colorbalance_caps_[mode].use_default_ = false;
      update_caps_ = true;
    }
    return true;
  } else if (mode == HWCColorControl::kColorSharpness) {
    if (prop.use_default_) {
      if (!sharp_caps_.use_default_) {
        sharp_caps_.use_default_ = true;
        update_caps_ = true;
      }
    } else if (prop.value_ != sharp_caps_.value_) {
      if (prop.value_ > sharp_caps_.caps_.range.max_value ||
          prop.value_ < sharp_caps_.caps_.range.min_value) {
        ALOGE("VA Filter sharp value out of range. should be %f~%f\n",
               sharp_caps_.caps_.range.min_value,
               sharp_caps_.caps_.range.max_value);
        return false;
      }
      sharp_caps_.value_ = prop.value_;
      sharp_caps_.use_default_ = false;
      update_caps_ = true;
    }
    return true;
  } else {
    ALOGE("VA Filter undefined color mode\n");
    return false;
  }
}

unsigned int VARenderer::GetVAProcFilterScalingMode(uint32_t mode) {
  if (deinterlace_caps_.mode_ == VAProcDeinterlacingNone) {
    switch (mode) {
      case 1:
        return VA_FILTER_SCALING_FAST;
      case 2:
        return VA_FILTER_SCALING_HQ;
      default:
        return VA_FILTER_SCALING_HQ;
    }
  } else
    return VA_FILTER_SCALING_FAST;
}


//get vasurface by the buffer_hande_t from the layer
int VARenderer::getSurfaceIn(buffer_handle_t bufferHandle, VADisplay display, VASurfaceID* surface, 
  uint32_t format, uint32_t width, uint32_t height){
  if (NULL == bufferHandle) {
    return -1;
  }
  cros_gralloc_handle *gr_handle = (cros_gralloc_handle *)bufferHandle;
  if ((gr_handle->width == 0) || (gr_handle->height == 0)){
    return -1;
  }

  auto bi = BufferInfoGetter::GetInstance()->GetBoInfo(bufferHandle);
  const uint32_t rt_format = DrmFormatToRTFormat(format);
  std::array<VASurfaceAttrib, 2> attribs;
  VADRMPRIMESurfaceDescriptor desc{};

  desc.fourcc = DrmFormatToVAFormat(format);
  desc.width = width;
  desc.height = height;
  desc.num_objects = 1;
  desc.objects[0].fd = gr_handle->fds[0];
  desc.objects[0].size = gr_handle->total_size;
  desc.objects[0].drm_format_modifier = bi->modifiers[0];
  desc.num_layers = 1;
  desc.layers[0].drm_format = format;
  int planes = 1;
  if (gr_handle->numFds > 1) {
    for (int i = 1; i < gr_handle->numFds; i++) {
      if (gr_handle->offsets[i] > 0)
        ++planes;
    }
  }
  desc.layers[0].num_planes = planes;
  desc.layers[0].object_index[0] = 0;
  for (unsigned i = 0; i < desc.layers[0].num_planes; ++i) {
    desc.layers[0].offset[i] = gr_handle->offsets[i];
    desc.layers[0].pitch[i] = gr_handle->strides[i];
  }

  attribs[0].type = (VASurfaceAttribType)VASurfaceAttribMemoryType;
  attribs[0].flags = VA_SURFACE_ATTRIB_SETTABLE;
  attribs[0].value.type = VAGenericValueTypeInteger;
  attribs[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2;
  attribs[1].type = VASurfaceAttribExternalBufferDescriptor;
  attribs[1].flags = VA_SURFACE_ATTRIB_SETTABLE;
  attribs[1].value.type = VAGenericValueTypePointer;
  attribs[1].value.value.p = (void *)&desc;

  VAStatus ret = vaCreateSurfaces(display, rt_format, width, height,
				 surface, 1, attribs.data(), attribs.size());
  if (ret != VA_STATUS_SUCCESS)
    ALOGE("Failed to create VASurface from drmbuffer with ret %d", ret);
  return ret;
}

bool VARenderer::startRender(HwcVaLayer* layer,uint32_t format){
  
  int64_t modifier = 0;
  uint32_t usage =3;
  VAStatus ret = VA_STATUS_SUCCESS;
  bool modifer_succeeded = false;
  DRMHwcNativeHandle temp_handle = 0;
  uint32_t input_layer_numer = 1;
  std::map<uint32_t, HwcLayer *, std::greater<int>> va_layer_map = layer->getVaLayerMapData();
  input_layer_numer = va_layer_map.size();
  int connector_width = layer->GetLayerData().pi.display_frame.right - layer->GetLayerData().pi.display_frame.left;
  int connector_height = layer->GetLayerData().pi.display_frame.bottom - layer->GetLayerData().pi.display_frame.top;
  int rt_format = DrmFormatToRTFormat(format);
  if(render_target_format_ != rt_format)
    render_target_format_ = rt_format;
  if ((layer->GetLayerData().pi.transform == kHwcTransform270) ||
      (layer->GetLayerData().pi.transform == kHwcTransform90))
    modifier = I915_FORMAT_MOD_Y_TILED;
  else
    modifier = 0;

  //if don't init the context , create the va context and gralloc buffer for the native_handles
  std::vector<DRMHwcNativeHandle> relese_handles;
  AutoLock lock(&lock_, __func__);
  ret = lock.Lock();
  if (va_context_ == VA_INVALID_ID ) {
    if (!CreateContext()) {
      ALOGE("Failed to create VA context");
      return false;
    }
    for( int32_t i=0; i<NATIVE_BUFFER_VECTOR_SIZE; i++ ) {
      buffer_handler_->CreateBuffer(connector_width,connector_height,format,
                                   &temp_handle,usage,&modifer_succeeded,modifier);
      if (modifier == 0) {
        native_handles.push_back(temp_handle);
        native_active_handles.push_back(temp_handle);
      }else{
        native_rotation_handles.push_back(temp_handle);
        native_active_handles.push_back(temp_handle);
      }
    }
    modifier_bak = modifier;
    current_handle_position = 0;
  }
  if (modifier_bak !=modifier) {
    if (modifier == I915_FORMAT_MOD_Y_TILED) {
      if (native_rotation_handles.size() == 0) {
        for( int32_t i=0; i<NATIVE_BUFFER_VECTOR_SIZE; i++ ) {
          buffer_handler_->CreateBuffer(connector_width,connector_height,format,
                                       &temp_handle,usage,&modifer_succeeded,modifier);
          native_rotation_handles.push_back(temp_handle);
        }
      }
      native_handles.swap(native_active_handles);
      native_active_handles.swap(native_rotation_handles);
    } else {
      if (native_handles.size() == 0) {
        for( int32_t i=0; i<NATIVE_BUFFER_VECTOR_SIZE; i++ ) {
          buffer_handler_->CreateBuffer(connector_width,connector_height,format,
                                       &temp_handle,usage,&modifer_succeeded,modifier);
          native_handles.push_back(temp_handle);
        }
      }
      native_rotation_handles.swap(native_active_handles);
      native_active_handles.swap(native_handles);
    }
    modifier_bak = modifier;
    current_handle_position = 0;
  }

  //create va output surface
  VASurfaceID surface_out = VA_INVALID_ID;
  for (int i =0; i<NATIVE_BUFFER_VECTOR_SIZE; i++) {
    ret = getSurfaceIn(native_active_handles.at(current_handle_position)->handle_, va_display_,
                      &surface_out, format, connector_width, connector_height);
    if (VA_STATUS_SUCCESS != ret) {
      if (current_handle_position == (NATIVE_BUFFER_VECTOR_SIZE - 1 ))
        current_handle_position = 0;
      else
        current_handle_position ++;
    }else
      break;
  }
  if (VA_STATUS_SUCCESS != ret) {
    ALOGE("Failed to create VASurface");
    return false;
  }
  ret = vaBeginPicture(va_display_, va_context_, surface_out);
  std::vector<ScopedVABufferID> pipeline_buffers(va_layer_map.size(), va_display_);
  if (NeedResizeCache(va_layer_map.size())) {
    if (!ResizeCache(va_layer_map.size())) {
      ALOGE("There is no enough memory, layers count is %zd", va_layer_map.size());
      return false;
    }
  }
  uint8_t index = 0;
  uint8_t infence_number = 0;
  for (std::map<uint32_t, HwcLayer *>::reverse_iterator a = va_layer_map.rbegin(); a != va_layer_map.rend(); a++,index++) {
    ScopedVABufferID* pipeline_buffer = &pipeline_buffers[index];
    VAProcPipelineParameterBuffer pipe_param = {};
    cros_gralloc_handle *gr_handle_t = (cros_gralloc_handle *)a->second->GetBufferHandle();
    hwc_frect_t source_crop = a->second->GetLayerData().pi.source_crop;
    //VARectangle surface_region;
    //create va input surface
    surface_region_[index].x = source_crop.left;//gr_handle->left;
    surface_region_[index].y = source_crop.top;//gr_handle->top;
    surface_region_[index].width = source_crop.right - source_crop.left;
    surface_region_[index].height = source_crop.bottom - source_crop.top;

    if ((0 == surface_region_[index].width) || (0 == surface_region_[index].height)) {
      ALOGE("Invalid source crop, zorder = %d", a->second->GetZOrder());
      continue;
    }
    hwc_rect_t display_frame = a->second->GetLayerData().pi.display_frame;
    //VARectangle output_region;
    output_region_[index].x = display_frame.left;
    output_region_[index].y = display_frame.top;
    output_region_[index].width = display_frame.right - display_frame.left;
    output_region_[index].height = display_frame.bottom - display_frame.top;
    ret = getSurfaceIn(a->second->GetBufferHandle(), va_display_, surface_in_ + index,
                       gr_handle_t->format, gr_handle_t->width, gr_handle_t->height);
    if (VA_STATUS_SUCCESS != ret) {
      ALOGE("Failed to create VASurface");
      return false;
    }
    pipe_param.surface = *(surface_in_ + index);
    pipe_param.surface_region = &surface_region_[index];
    pipe_param.surface_color_standard = VAProcColorStandardBT601;
    pipe_param.output_region = &output_region_[index];
    pipe_param.output_color_standard = VAProcColorStandardBT601;
    VABlendState bs = {};
    bs.flags = VA_BLEND_PREMULTIPLIED_ALPHA;
    pipe_param.blend_state = &bs;
    pipe_param.filter_flags = GetVAProcFilterScalingMode(1);
    if (filters_.size())
      pipe_param.filters = filters_.data();
    pipe_param.num_filters = static_cast<unsigned int>(filters_.size());
#if VA_MAJOR_VERSION >= 1
    // currently rotation is only supported by VA on Android.
    uint32_t rotation = 0, mirror = 0;
    HWCTransformToVA(layer->GetLayerData().pi.transform, rotation, mirror);
    pipe_param.rotation_state = rotation;
    pipe_param.mirror_state = mirror;
#endif
#ifdef VA_SUPPORT_COLOR_RANGE
    uint32_t dataspace = layer->dataspace;
    if ((dataspace & HAL_DATASPACE_RANGE_FULL) != 0) {
      pipe_param.input_color_properties.color_range = VA_SOURCE_RANGE_FULL;
    }
#endif
    if (!pipeline_buffer->CreateBuffer(va_context_, VAProcPipelineParameterBufferType,
                                      sizeof(VAProcPipelineParameterBuffer), 1, &pipe_param)) {
      ALOGE("Failed to create VAPipelineBuffer");
      return false;
    }
    va_buffer_id_[index] = pipeline_buffer->buffer();
    if (a->second->GetLayerData().acquire_fence.Get() > 0)
      sync_fds_[1 + infence_number++] = a->second->GetLayerData().acquire_fence.Get();
  }
  ret |= vaRenderPicture(va_display_, va_context_, &va_buffer_id_[0], va_layer_map.size());
  if (ret != VA_STATUS_SUCCESS) {
    ALOGE("Failed to vaRenderPicture, ret = %d\n", ret);
    return false;
  }
  ret |= vaEndPicture2(va_display_, va_context_, sync_fds_, infence_number);
  if (ret != VA_STATUS_SUCCESS) {
    ALOGE(" Failed to vaEndPicture, ret = %d\n", ret);
  }
  current_handle_position++;
  if (current_handle_position >= NATIVE_BUFFER_VECTOR_SIZE)
    current_handle_position = 0;

  vaDestroySurfaces(va_display_, surface_in_, va_layer_map.size());
  vaDestroySurfaces(va_display_, &surface_out, 1);
  return true;
}

bool VARenderer::LoadCaps() {
  VAProcFilterCapColorBalance colorbalancecaps[VAProcColorBalanceCount];
  uint32_t colorbalance_num = VAProcColorBalanceCount;
  uint32_t sharp_num = 1;
  uint32_t deinterlace_num = VAProcDeinterlacingCount;
  memset(colorbalancecaps, 0,
         sizeof(VAProcFilterCapColorBalance) * VAProcColorBalanceCount);
  if (!QueryVAProcFilterCaps(va_context_, VAProcFilterColorBalance,
                             colorbalancecaps, &colorbalance_num)) {
    return false;
  }
  if (!QueryVAProcFilterCaps(va_context_, VAProcFilterSharpening,
                             &sharp_caps_.caps_, &sharp_num)) {
    return false;
  }
  if (!QueryVAProcFilterCaps(va_context_, VAProcFilterDeinterlacing,
                             &deinterlace_caps_.caps_, &deinterlace_num)) {
    return false;
  }

  SetVAProcFilterColorDefaultValue(&colorbalancecaps[0]);
  SetVAProcFilterDeinterlaceDefaultMode();

  return true;
}

bool VARenderer::CreateContext() {
  DestroyContext();
  VAConfigAttrib config_attrib;
  config_attrib.type = VAConfigAttribRTFormat;
  config_attrib.value = render_target_format_;
  VAStatus ret =
      vaCreateConfig(va_display_, VAProfileNone, VAEntrypointVideoProc,
                     &config_attrib, 1, &va_config_);
  if (ret != VA_STATUS_SUCCESS) {
    ALOGE("Failed to create VA Config");
    return false;
  }
  // These parameters are not used in vaCreateContext so just set them to dummy
  int width = 1;
  int height = 1;
  ret = vaCreateContext(va_display_, va_config_, width, height, 0x00, nullptr,
                        0, &va_context_);

  update_caps_ = true;
  if (ret == VA_STATUS_SUCCESS) {
    if (!LoadCaps() || !UpdateCaps())
      return false;
  }
  return ret == VA_STATUS_SUCCESS ? true : false;
}

void VARenderer::DestroyContext() {
  if (va_context_ != VA_INVALID_ID) {
    vaDestroyContext(va_display_, va_context_);
    va_context_ = VA_INVALID_ID;
  }
  if (va_config_ != VA_INVALID_ID) {
    vaDestroyConfig(va_display_, va_config_);
    va_config_ = VA_INVALID_ID;
  }
  std::vector<VABufferID>().swap(filters_);
  std::vector<ScopedVABufferID>().swap(cb_elements_);
  std::vector<ScopedVABufferID>().swap(sharp_);
}

bool VARenderer::UpdateCaps() {
  if (!update_caps_) {
    return true;
  }

  update_caps_ = false;

  std::vector<ScopedVABufferID> cb_elements(1, va_display_);
  std::vector<ScopedVABufferID> sharp(1, va_display_);
  std::vector<ScopedVABufferID> deinterlace(1, va_display_);

  std::vector<VABufferID>().swap(filters_);
  std::vector<ScopedVABufferID>().swap(cb_elements_);
  std::vector<ScopedVABufferID>().swap(sharp_);
  std::vector<ScopedVABufferID>().swap(deinterlace_);

  VAProcFilterParameterBufferColorBalance cbparam[VAProcColorBalanceCount];
  VAProcFilterParameterBuffer sharpparam;
  VAProcFilterParameterBufferDeinterlacing deinterlaceparam;
  memset(cbparam, 0, VAProcColorBalanceCount *
                         sizeof(VAProcFilterParameterBufferColorBalance));
  int index = 0;
  for (auto itr = colorbalance_caps_.begin(); itr != colorbalance_caps_.end();
       itr++) {
    if (itr->second.use_default_) {
      itr->second.value_ = itr->second.caps_.range.default_value;
    }
    if (fabs(itr->second.value_ - itr->second.caps_.range.default_value) >=
        itr->second.caps_.range.step) {
      cbparam[index].type = VAProcFilterColorBalance;
      cbparam[index].value = itr->second.value_;
      cbparam[index].attrib = itr->second.caps_.type;
      index++;
    }
  }

  if (index) {
    if (!cb_elements[0].CreateBuffer(
            va_context_, VAProcFilterParameterBufferType,
            sizeof(VAProcFilterParameterBufferColorBalance), index, cbparam)) {
      ALOGE("Create color fail\n");
      return false;
    }
    filters_.push_back(cb_elements[0].buffer());
  }
  cb_elements_.swap(cb_elements);

  if (sharp_caps_.use_default_) {
    sharp_caps_.value_ = sharp_caps_.caps_.range.default_value;
  }
  if (fabs(sharp_caps_.value_ - sharp_caps_.caps_.range.default_value) >=
      sharp_caps_.caps_.range.step) {
    sharpparam.value = sharp_caps_.value_;
    sharpparam.type = VAProcFilterSharpening;
    if (!sharp[0].CreateBuffer(va_context_, VAProcFilterParameterBufferType,
                               sizeof(VAProcFilterParameterBuffer), 1,
                               &sharpparam)) {
      return false;
    }
    filters_.push_back(sharp[0].buffer());
  }
  sharp_.swap(sharp);

  if (deinterlace_caps_.mode_ != VAProcDeinterlacingNone) {
    deinterlaceparam.algorithm = deinterlace_caps_.mode_;
    deinterlaceparam.type = VAProcFilterDeinterlacing;
    if (!deinterlace[0].CreateBuffer(
            va_context_, VAProcFilterParameterBufferType,
            sizeof(VAProcFilterParameterBufferDeinterlacing), 1,
            &deinterlaceparam)) {
      return false;
    }
    filters_.push_back(deinterlace[0].buffer());
  }
  deinterlace_.swap(deinterlace);

  return true;
}

#if VA_MAJOR_VERSION >= 1
void VARenderer::HWCTransformToVA(uint32_t transform, uint32_t& rotation,
                                  uint32_t& mirror) {
  rotation = VA_ROTATION_NONE;
  mirror = VA_MIRROR_NONE;

  if (transform & kHwcReflectX)
    mirror |= VA_MIRROR_HORIZONTAL;
  if (transform & kHwcReflectY)
    mirror |= VA_MIRROR_VERTICAL;

  if (mirror == VA_MIRROR_NONE ||
      mirror == (VA_MIRROR_HORIZONTAL | VA_MIRROR_VERTICAL)) {
    transform &= ~kHwcReflectX;
    transform &= ~kHwcReflectY;
    switch (transform) {
      case kHwcTransform270:
        rotation = VA_ROTATION_270;
        break;
      case kHwcTransform180:
        rotation = VA_ROTATION_180;
        break;
      case kHwcTransform90:
        rotation = VA_ROTATION_90;
        break;
      default:
        break;
    }
  } else {
    // Fixme? WA added. VA is using rotation then mirror order
    // CTS Cameration orientation is expecting mirror, then rotation
    // WA added to use inverse rotation to make the same result
    if (transform & kHwcTransform180)
      rotation = VA_ROTATION_180;
    else if (transform & kHwcTransform90)
      rotation = VA_ROTATION_270;
    else if (transform & kHwcTransform270)
      rotation = VA_ROTATION_90;
  }
}
#endif

}  // namespace android
